// SPDX-License-Identifier: Apache-2.0
#include "v1_transport_configuration_session.h"

#include <array>

#include "ldac_native/avdtp.h"

namespace native_ldac::agent {
namespace {

constexpr std::size_t kMaximumSignalingResponse = 4096u;
constexpr std::uint32_t kMaximumTimeoutMs = 30000u;

bool IsCancelled(V1TransportCancelProbe probe, void* context) {
    return probe != nullptr && probe(context);
}

bool OptionsAreValid(const V1TransportConfigurationOptions& options) {
    return options.open_timeout_ms != 0u &&
           options.open_timeout_ms <= kMaximumTimeoutMs &&
           options.exchange_timeout_ms != 0u &&
           options.exchange_timeout_ms <= kMaximumTimeoutMs &&
           options.media_open_timeout_ms != 0u &&
           options.media_open_timeout_ms <= kMaximumTimeoutMs &&
           options.preferred_media_mtu != 0u &&
           options.local_seid != 0u && options.local_seid <= 0x3Fu &&
           (options.local_capabilities.sample_rates & LDAC_SF_ALL) != 0u &&
           (options.local_capabilities.channel_modes & LDAC_CM_ALL) != 0u;
}

void SetPrimary(V1TransportConfigurationResult* result,
                V1TransportConfigurationDisposition disposition,
                V1TransportConfigurationStage stage) {
    result->primary_disposition = disposition;
    result->disposition = disposition;
    result->stage = stage;
}

V1TransportConfigurationResult Finish(
    V1TransportConfigurationBackend* backend,
    V1TransportConfigurationResult result) {
    if (!result.signaling_opened) {
        return result;
    }
    result.close_attempted = true;
    std::uint32_t close_error = 0u;
    result.close_succeeded = backend->CloseSignaling(&close_error);
    result.cleanup_error = close_error;
    if (!result.close_succeeded) {
        result.disposition =
            V1TransportConfigurationDisposition::CleanupFailure;
        result.stage = V1TransportConfigurationStage::CloseChannels;
    }
    return result;
}

V1TransportConfigurationStage StageForSignal(std::uint8_t signal) {
    switch (signal) {
        case AVDTP_SIGNAL_DISCOVER:
        case AVDTP_SIGNAL_GET_CAPABILITIES:
        case AVDTP_SIGNAL_GET_ALL_CAPABILITIES:
            return V1TransportConfigurationStage::DiscoverCapabilities;
        case AVDTP_SIGNAL_SET_CONFIGURATION:
            return V1TransportConfigurationStage::SetConfiguration;
        case AVDTP_SIGNAL_OPEN:
            return V1TransportConfigurationStage::AvdtpOpen;
        case AVDTP_SIGNAL_CLOSE:
            return V1TransportConfigurationStage::AvdtpClose;
        default:
            return V1TransportConfigurationStage::None;
    }
}

bool ExchangeAction(V1TransportConfigurationBackend* backend,
                    const V1TransportConfigurationOptions& options,
                    const avdtp_action& action,
                    avdtp_source* source,
                    V1TransportConfigurationResult* result,
                    V1TransportCancelProbe cancel_probe,
                    void* cancel_context,
                    avdtp_action* next) {
    avdtp_header header = {};
    if (action.kind != AVDTP_ACTION_SEND_SIGNALING ||
        avdtp_parse_header(action.packet, action.packet_size, &header) !=
            AVDTP_OK) {
        result->protocol_error = AVDTP_SOURCE_ERROR_BAD_PACKET;
        SetPrimary(result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   result->stage);
        return false;
    }
    result->stage = StageForSignal(header.signal_id);
    if (IsCancelled(cancel_probe, cancel_context)) {
        SetPrimary(result,
                   V1TransportConfigurationDisposition::Cancelled,
                   result->stage);
        return false;
    }
    std::array<std::uint8_t, kMaximumSignalingResponse> response = {};
    std::size_t response_size = 0u;
    std::uint32_t backend_error = 0u;
    ++result->signaling_exchanges;
    if (!backend->ExchangeSignaling(action.packet,
                                    action.packet_size,
                                    response.data(),
                                    response.size(),
                                    &response_size,
                                    options.exchange_timeout_ms,
                                    &backend_error)) {
        result->backend_error = backend_error;
        SetPrimary(
            result,
            IsCancelled(cancel_probe, cancel_context)
                ? V1TransportConfigurationDisposition::Cancelled
                : V1TransportConfigurationDisposition::BackendFailure,
            result->stage);
        return false;
    }
    *next = avdtp_source_handle_signaling(
        source, response.data(), response_size);
    if (header.signal_id == AVDTP_SIGNAL_SET_CONFIGURATION &&
        next->kind != AVDTP_ACTION_ERROR) {
        result->set_configuration_accepted = true;
    } else if (header.signal_id == AVDTP_SIGNAL_OPEN &&
               next->kind != AVDTP_ACTION_ERROR) {
        result->avdtp_open_accepted = true;
    } else if (header.signal_id == AVDTP_SIGNAL_CLOSE &&
               next->kind == AVDTP_ACTION_SESSION_CLOSED) {
        result->avdtp_close_accepted = true;
    }
    if (next->kind == AVDTP_ACTION_ERROR) {
        result->protocol_error = next->error_code;
        SetPrimary(result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   result->stage);
        return false;
    }
    return true;
}

}  // namespace

V1TransportConfigurationResult RunV1TransportConfigurationOnce(
    V1TransportConfigurationBackend* backend,
    const V1TransportConfigurationOptions& options,
    V1TransportCancelProbe cancel_probe,
    void* cancel_context) {
    V1TransportConfigurationResult result;
    if (backend == nullptr || !OptionsAreValid(options)) {
        return result;
    }
    if (IsCancelled(cancel_probe, cancel_context)) {
        SetPrimary(&result,
                   V1TransportConfigurationDisposition::Cancelled,
                   V1TransportConfigurationStage::OpenSignaling);
        return result;
    }

    result.stage = V1TransportConfigurationStage::OpenSignaling;
    result.open_attempts = 1u;
    std::uint32_t backend_error = 0u;
    if (!backend->OpenSignaling(options.open_timeout_ms, &backend_error)) {
        result.backend_error = backend_error;
        SetPrimary(
            &result,
            IsCancelled(cancel_probe, cancel_context)
                ? V1TransportConfigurationDisposition::Cancelled
                : V1TransportConfigurationDisposition::BackendFailure,
            V1TransportConfigurationStage::OpenSignaling);
        return result;
    }
    result.signaling_opened = true;

    avdtp_source source = {};
    avdtp_source_init(&source,
                      options.local_capabilities,
                      options.local_seid,
                      options.preferred_sample_rate_hz);
    avdtp_action action = avdtp_source_begin(&source);
    while (action.kind == AVDTP_ACTION_SEND_SIGNALING) {
        avdtp_action next = {};
        if (!ExchangeAction(backend,
                            options,
                            action,
                            &source,
                            &result,
                            cancel_probe,
                            cancel_context,
                            &next)) {
            return Finish(backend, result);
        }
        action = next;
    }
    if (action.kind != AVDTP_ACTION_OPEN_MEDIA_CHANNEL) {
        result.protocol_error = action.kind == AVDTP_ACTION_ERROR
                                    ? action.error_code
                                    : AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE;
        SetPrimary(&result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   result.stage);
        return Finish(backend, result);
    }
    result.remote_seid = source.remote_seid;
    result.configuration = source.configuration;

    if (IsCancelled(cancel_probe, cancel_context)) {
        SetPrimary(&result,
                   V1TransportConfigurationDisposition::Cancelled,
                   V1TransportConfigurationStage::OpenMedia);
        return Finish(backend, result);
    }
    result.stage = V1TransportConfigurationStage::OpenMedia;
    if (!backend->OpenMedia(options.media_open_timeout_ms,
                            options.preferred_media_mtu,
                            &result.incoming_mtu,
                            &result.outgoing_mtu,
                            &backend_error)) {
        result.backend_error = backend_error;
        SetPrimary(&result,
                   V1TransportConfigurationDisposition::BackendFailure,
                   V1TransportConfigurationStage::OpenMedia);
        return Finish(backend, result);
    }
    result.media_opened = true;
    action = avdtp_source_media_channel_opened(&source);
    if (action.kind != AVDTP_ACTION_SESSION_OPEN) {
        result.protocol_error = action.error_code;
        SetPrimary(&result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   V1TransportConfigurationStage::OpenMedia);
        return Finish(backend, result);
    }

    action = avdtp_source_close(&source);
    avdtp_action closed = {};
    if (!ExchangeAction(backend,
                        options,
                        action,
                        &source,
                        &result,
                        cancel_probe,
                        cancel_context,
                        &closed)) {
        return Finish(backend, result);
    }
    if (closed.kind != AVDTP_ACTION_SESSION_CLOSED) {
        result.protocol_error = closed.kind == AVDTP_ACTION_ERROR
                                    ? closed.error_code
                                    : AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE;
        SetPrimary(&result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   V1TransportConfigurationStage::AvdtpClose);
        return Finish(backend, result);
    }

    SetPrimary(&result,
               V1TransportConfigurationDisposition::Succeeded,
               V1TransportConfigurationStage::AvdtpClose);
    return Finish(backend, result);
}

}  // namespace native_ldac::agent
