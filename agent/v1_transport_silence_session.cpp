// SPDX-License-Identifier: Apache-2.0
#include "v1_transport_silence_session.h"

#include <array>

#include "ldac_native/avdtp.h"
#include "ldac_native/ldac_encoder.h"
#include "ldac_native/rtp_ldac.h"

namespace native_ldac::agent {
namespace {

constexpr std::size_t kResponseCapacity = 4096u;
constexpr std::uint32_t kMaximumTimeoutMs = 30000u;

bool Cancelled(V1TransportCancelProbe probe, void* context) {
    return probe != nullptr && probe(context);
}

void SetFailure(V1TransportSilenceResult* result,
                V1TransportConfigurationDisposition disposition,
                V1TransportSilenceStage stage) {
    result->primary_disposition = disposition;
    result->disposition = disposition;
    result->stage = stage;
}

V1TransportSilenceResult Finish(V1TransportSilenceBackend* backend,
                                V1TransportSilenceResult result) {
    if (!result.signaling_opened) return result;
    result.close_attempted = true;
    std::uint32_t error = 0u;
    result.close_succeeded = backend->CloseSignaling(&error);
    result.cleanup_error = error;
    if (!result.close_succeeded) {
        result.disposition =
            V1TransportConfigurationDisposition::CleanupFailure;
        result.stage = V1TransportSilenceStage::CloseChannels;
    }
    return result;
}

bool Exchange(V1TransportSilenceBackend* backend,
              const V1TransportSilenceOptions& options,
              avdtp_source* source,
              const avdtp_action& action,
              V1TransportSilenceStage stage,
              V1TransportCancelProbe probe,
              void* context,
              V1TransportSilenceResult* result,
              avdtp_action* next) {
    if (action.kind != AVDTP_ACTION_SEND_SIGNALING ||
        Cancelled(probe, context)) {
        SetFailure(result,
                   Cancelled(probe, context)
                       ? V1TransportConfigurationDisposition::Cancelled
                       : V1TransportConfigurationDisposition::ProtocolFailure,
                   stage);
        return false;
    }
    std::array<std::uint8_t, kResponseCapacity> response{};
    std::size_t response_size = 0u;
    std::uint32_t error = 0u;
    result->stage = stage;
    ++result->signaling_exchanges;
    if (!backend->ExchangeSignaling(action.packet,
                                    action.packet_size,
                                    response.data(),
                                    response.size(),
                                    &response_size,
                                    options.exchange_timeout_ms,
                                    &error)) {
        result->backend_error = error;
        SetFailure(result,
                   Cancelled(probe, context)
                       ? V1TransportConfigurationDisposition::Cancelled
                       : V1TransportConfigurationDisposition::BackendFailure,
                   stage);
        return false;
    }
    *next = avdtp_source_handle_signaling(
        source, response.data(), response_size);
    if (next->kind == AVDTP_ACTION_ERROR) {
        result->protocol_error = next->error_code;
        SetFailure(result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   stage);
        return false;
    }
    return true;
}

bool SendZeroPackets(V1TransportSilenceBackend* backend,
                     const V1TransportSilenceOptions& options,
                     V1TransportSilenceResult* result,
                     V1TransportCancelProbe probe,
                     void* context) {
    if (result->outgoing_mtu <= LDAC_RTP_OVERHEAD) {
        result->protocol_error = AVDTP_SOURCE_ERROR_BAD_PACKET;
        return false;
    }
    const std::size_t payload_mtu = result->outgoing_mtu - LDAC_RTP_OVERHEAD;
    std::array<float, LDAC_ENCODER_PCM_FRAMES_PER_CALL *
                          LDAC_ENCODER_STEREO_CHANNELS> pcm{};
    std::array<std::uint8_t, LDAC_ENCODER_MAX_OUTPUT_BYTES> encoded{};
    std::array<std::uint8_t, 4096> packet{};
    ldac_encoder* encoder = nullptr;
    const auto mode = result->configuration.channel_mode == LDAC_CM_STEREO
        ? LDAC_ENCODER_CHANNEL_STEREO
        : result->configuration.channel_mode == LDAC_CM_DUAL
        ? LDAC_ENCODER_CHANNEL_DUAL
        : LDAC_ENCODER_CHANNEL_MONO;
    if (ldac_encoder_create_with_channel_mode(
            &encoder,
            payload_mtu,
            LDAC_ENCODER_QUALITY_HQ,
            ldac_sample_rate_to_hz(result->configuration.sample_rate),
            mode) != LDAC_ENCODER_OK || encoder == nullptr) {
        result->protocol_error = LDAC_ENCODER_INITIALIZATION_FAILED;
        return false;
    }
    const unsigned samples_per_frame =
        ldac_encoder_samples_per_transport_frame(encoder);
    std::uint16_t sequence = 0u;
    std::uint32_t timestamp = 0u;
    for (unsigned call = 0u;
         call < 32u && result->media_packets_written < options.packet_limit;
         ++call) {
        if (Cancelled(probe, context)) {
            ldac_encoder_destroy(encoder);
            return false;
        }
        std::size_t encoded_size = 0u;
        std::uint8_t frame_count = 0u;
        if (ldac_encoder_encode_f32(
                encoder, pcm.data(), LDAC_ENCODER_PCM_FRAMES_PER_CALL,
                encoded.data(), encoded.size(), &encoded_size,
                &frame_count) != LDAC_ENCODER_OK) {
            result->protocol_error = LDAC_ENCODER_ENCODING_FAILED;
            ldac_encoder_destroy(encoder);
            return false;
        }
        if (encoded_size == 0u) continue;
        std::size_t packet_size = 0u;
        if (ldac_rtp_build_unfragmented(
                packet.data(), packet.size(), result->outgoing_mtu,
                sequence, timestamp, 0x56315A30u, frame_count,
                encoded.data(), encoded_size, &packet_size) != LDAC_RTP_OK) {
            result->protocol_error = AVDTP_SOURCE_ERROR_BAD_PACKET;
            ldac_encoder_destroy(encoder);
            return false;
        }
        std::uint32_t error = 0u;
        if (!backend->WriteMedia(packet.data(), packet_size,
                                 options.media_timeout_ms, &error)) {
            result->backend_error = error;
            ldac_encoder_destroy(encoder);
            return false;
        }
        ++result->media_packets_written;
        result->media_bytes_written +=
            static_cast<std::uint32_t>(packet_size);
        ++sequence;
        timestamp += frame_count * samples_per_frame;
    }
    ldac_encoder_destroy(encoder);
    if (result->media_packets_written != options.packet_limit &&
        result->protocol_error == 0) {
        result->protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
    }
    return result->media_packets_written == options.packet_limit;
}

}  // namespace

bool IsV1RemoteNoResourcesDiagnostic(
    const V1TransportOpenDiagnostics& diagnostics) {
    constexpr std::uint32_t kAuthenticatedEncryptedFlags = 0x00060000u;
    return diagnostics.available && diagnostics.remote_response_valid &&
        diagnostics.operation == 1u && diagnostics.psm == 0x0019u &&
        diagnostics.remote_bluetooth_address != 0u &&
        diagnostics.channel_flags == kAuthenticatedEncryptedFlags &&
        diagnostics.response == 4u &&
        diagnostics.response_status == 0u;
}

bool IsV1StrictlyRetryableRemoteNoResources(
    const V1TransportSilenceResult& result) {
    return result.disposition ==
               V1TransportConfigurationDisposition::BackendFailure &&
        result.primary_disposition ==
               V1TransportConfigurationDisposition::BackendFailure &&
        result.stage == V1TransportSilenceStage::OpenSignaling &&
        result.backend_error == 71u && result.open_attempts == 1u &&
        result.signaling_exchanges == 0u && !result.signaling_opened &&
        IsV1RemoteNoResourcesDiagnostic(result.open_diagnostics);
}

V1TransportSilenceResult RunV1TransportSilenceBurstOnce(
    V1TransportSilenceBackend* backend,
    const V1TransportSilenceOptions& options,
    V1TransportCancelProbe cancel_probe,
    void* cancel_context) {
    V1TransportSilenceResult result;
    if (backend == nullptr || options.open_timeout_ms == 0u ||
        options.open_timeout_ms > kMaximumTimeoutMs ||
        options.exchange_timeout_ms == 0u ||
        options.exchange_timeout_ms > kMaximumTimeoutMs ||
        options.media_timeout_ms == 0u ||
        options.media_timeout_ms > kMaximumTimeoutMs ||
        options.preferred_media_mtu == 0u ||
        options.packet_limit == 0u || options.packet_limit > 4u) {
        return result;
    }
    if (Cancelled(cancel_probe, cancel_context)) {
        SetFailure(&result, V1TransportConfigurationDisposition::Cancelled,
                   V1TransportSilenceStage::OpenSignaling);
        return result;
    }
    result.stage = V1TransportSilenceStage::OpenSignaling;
    result.open_attempts = 1u;
    std::uint32_t error = 0u;
    if (!backend->OpenSignaling(options.open_timeout_ms, &error)) {
        result.backend_error = error;
        (void)backend->GetLastOpenDiagnostics(
            &result.open_diagnostics);
        SetFailure(&result,
                   V1TransportConfigurationDisposition::BackendFailure,
                   V1TransportSilenceStage::OpenSignaling);
        return result;
    }
    result.signaling_opened = true;
    avdtp_source source{};
    avdtp_source_init(&source, {LDAC_SF_ALL, LDAC_CM_STEREO}, 1u,
                      options.preferred_sample_rate_hz);
    avdtp_action action = avdtp_source_begin(&source);
    while (action.kind == AVDTP_ACTION_SEND_SIGNALING) {
        avdtp_header header{};
        if (avdtp_parse_header(action.packet, action.packet_size, &header) !=
            AVDTP_OK) {
            SetFailure(&result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       V1TransportSilenceStage::Negotiate);
            return Finish(backend, result);
        }
        const auto stage = header.signal_id == AVDTP_SIGNAL_START
            ? V1TransportSilenceStage::Start
            : header.signal_id == AVDTP_SIGNAL_SUSPEND
            ? V1TransportSilenceStage::Suspend
            : header.signal_id == AVDTP_SIGNAL_CLOSE
            ? V1TransportSilenceStage::Close
            : V1TransportSilenceStage::Negotiate;
        avdtp_action next{};
        if (!Exchange(backend, options, &source, action, stage,
                      cancel_probe, cancel_context, &result, &next)) {
            return Finish(backend, result);
        }
        if (header.signal_id == AVDTP_SIGNAL_SET_CONFIGURATION)
            result.set_configuration_accepted = true;
        if (header.signal_id == AVDTP_SIGNAL_OPEN)
            result.avdtp_open_accepted = true;
        action = next;
    }
    if (action.kind != AVDTP_ACTION_OPEN_MEDIA_CHANNEL) {
        SetFailure(&result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   V1TransportSilenceStage::Negotiate);
        return Finish(backend, result);
    }
    result.remote_seid = source.remote_seid;
    result.configuration = source.configuration;
    result.stage = V1TransportSilenceStage::OpenMedia;
    if (!backend->OpenMedia(options.media_timeout_ms,
                            options.preferred_media_mtu, &result.incoming_mtu,
                            &result.outgoing_mtu, &error)) {
        result.backend_error = error;
        SetFailure(&result,
                   V1TransportConfigurationDisposition::BackendFailure,
                   result.stage);
        return Finish(backend, result);
    }
    result.media_opened = true;
    if (result.incoming_mtu == 0u ||
        result.outgoing_mtu <= LDAC_RTP_OVERHEAD ||
        result.outgoing_mtu - LDAC_RTP_OVERHEAD <
            LDAC_ENCODER_MIN_PAYLOAD_MTU) {
        result.protocol_error = AVDTP_SOURCE_ERROR_BAD_PACKET;
        SetFailure(&result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   result.stage);
        return Finish(backend, result);
    }
    if (avdtp_source_media_channel_opened(&source).kind !=
        AVDTP_ACTION_SESSION_OPEN) {
        SetFailure(&result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   result.stage);
        return Finish(backend, result);
    }
    avdtp_action next{};
    if (!Exchange(backend, options, &source, avdtp_source_start(&source),
                  V1TransportSilenceStage::Start, cancel_probe,
                  cancel_context, &result, &next)) {
        return Finish(backend, result);
    }
    result.remote_stream_cleanup_required = true;
    if (next.kind != AVDTP_ACTION_STREAM_READY) {
        result.protocol_error = AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE;
        SetFailure(&result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   V1TransportSilenceStage::Start);
        return Finish(backend, result);
    }
    result.avdtp_start_accepted = true;
    result.stage = V1TransportSilenceStage::WriteSilence;
    if (!SendZeroPackets(backend, options, &result,
                         cancel_probe, cancel_context)) {
        const auto disposition = Cancelled(cancel_probe, cancel_context)
            ? V1TransportConfigurationDisposition::Cancelled
            : result.backend_error != 0u
            ? V1TransportConfigurationDisposition::BackendFailure
            : V1TransportConfigurationDisposition::ProtocolFailure;
        SetFailure(&result, disposition, result.stage);
        return Finish(backend, result);
    }
    if (!Exchange(backend, options, &source, avdtp_source_suspend(&source),
                  V1TransportSilenceStage::Suspend, cancel_probe,
                  cancel_context, &result, &next)) {
        return Finish(backend, result);
    }
    if (next.kind != AVDTP_ACTION_STREAM_SUSPENDED) {
        result.protocol_error = AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE;
        SetFailure(&result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   V1TransportSilenceStage::Suspend);
        return Finish(backend, result);
    }
    result.avdtp_suspend_accepted = true;
    result.remote_stream_cleanup_required = false;
    if (!Exchange(backend, options, &source, avdtp_source_close(&source),
                  V1TransportSilenceStage::Close, cancel_probe,
                  cancel_context, &result, &next)) {
        return Finish(backend, result);
    }
    if (next.kind != AVDTP_ACTION_SESSION_CLOSED) {
        result.protocol_error = AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE;
        SetFailure(&result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   V1TransportSilenceStage::Close);
        return Finish(backend, result);
    }
    result.avdtp_close_accepted = true;
    result.primary_disposition =
        V1TransportConfigurationDisposition::Succeeded;
    result.disposition = V1TransportConfigurationDisposition::Succeeded;
    return Finish(backend, result);
}

}  // namespace native_ldac::agent
