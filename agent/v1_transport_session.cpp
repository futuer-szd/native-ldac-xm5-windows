// SPDX-License-Identifier: Apache-2.0
#include "v1_transport_session.h"

#include <array>
#include <cstring>

#include "ldac_native/avdtp.h"

namespace native_ldac::agent {
namespace {

constexpr std::size_t kMaximumSignalingResponse = 4096u;
constexpr std::uint32_t kMaximumTimeoutMs = 30000u;

struct ParsedResponse {
    avdtp_message_type message_type = AVDTP_MESSAGE_COMMAND;
    const std::uint8_t* payload = nullptr;
    std::size_t payload_size = 0u;
    std::uint8_t reject_error = 0u;
};

bool IsCancelled(V1TransportCancelProbe probe, void* context) {
    return probe != nullptr && probe(context);
}

bool OptionsAreValid(const V1TransportDiscoveryOptions& options) {
    return options.open_timeout_ms != 0u &&
           options.open_timeout_ms <= kMaximumTimeoutMs &&
           options.exchange_timeout_ms != 0u &&
           options.exchange_timeout_ms <= kMaximumTimeoutMs &&
           (options.local_capabilities.sample_rates & LDAC_SF_ALL) != 0u &&
           (options.local_capabilities.channel_modes & LDAC_CM_ALL) != 0u;
}

bool ParseResponse(const std::uint8_t* response,
                   std::size_t response_size,
                   std::uint8_t expected_label,
                   std::uint8_t expected_signal,
                   ParsedResponse* parsed) {
    if (response == nullptr || parsed == nullptr) {
        return false;
    }
    avdtp_header header = {};
    if (avdtp_parse_header(response, response_size, &header) != AVDTP_OK ||
        header.packet_type != AVDTP_PACKET_SINGLE ||
        header.payload_offset > response_size ||
        header.transaction_label != expected_label ||
        header.signal_id != expected_signal ||
        header.message_type == AVDTP_MESSAGE_COMMAND) {
        return false;
    }
    parsed->message_type = header.message_type;
    parsed->payload = response + header.payload_offset;
    parsed->payload_size = response_size - header.payload_offset;
    parsed->reject_error = 0u;
    if ((header.message_type == AVDTP_MESSAGE_REJECT ||
         header.message_type == AVDTP_MESSAGE_GENERAL_REJECT) &&
        parsed->payload_size != 0u) {
        parsed->reject_error =
            parsed->payload[parsed->payload_size - 1u];
    }
    return true;
}

std::size_t CollectAudioSinks(const std::uint8_t* payload,
                              std::size_t payload_size,
                              std::uint8_t* seids,
                              std::size_t seid_capacity) {
    if (payload == nullptr || seids == nullptr ||
        (payload_size % 2u) != 0u) {
        return 0u;
    }
    std::size_t count = 0u;
    for (std::size_t offset = 0u; offset < payload_size; offset += 2u) {
        const std::uint8_t candidate =
            static_cast<std::uint8_t>(payload[offset] >> 2u);
        const std::uint8_t in_use =
            static_cast<std::uint8_t>((payload[offset] >> 1u) & 0x01u);
        const std::uint8_t media_type =
            static_cast<std::uint8_t>(payload[offset + 1u] >> 4u);
        const std::uint8_t endpoint_type =
            static_cast<std::uint8_t>((payload[offset + 1u] >> 3u) & 0x01u);
        if (candidate != 0u && in_use == 0u &&
            media_type == AVDTP_MEDIA_TYPE_AUDIO && endpoint_type == 1u &&
            count < seid_capacity) {
            seids[count++] = candidate;
        }
    }
    return count;
}

void SetPrimary(V1TransportDiscoveryResult* result,
                V1TransportDiscoveryDisposition disposition,
                V1TransportDiscoveryStage stage) {
    result->primary_disposition = disposition;
    result->disposition = disposition;
    result->stage = stage;
}

V1TransportDiscoveryResult Finish(
    V1TransportDiscoveryBackend* backend,
    V1TransportDiscoveryResult result) {
    if (!result.signaling_opened) {
        return result;
    }
    result.close_attempted = true;
    std::uint32_t close_error = 0u;
    result.close_succeeded = backend->CloseSignaling(&close_error);
    result.cleanup_error = close_error;
    if (!result.close_succeeded) {
        result.disposition = V1TransportDiscoveryDisposition::CleanupFailure;
        result.stage = V1TransportDiscoveryStage::CloseSignaling;
    }
    return result;
}

bool Exchange(V1TransportDiscoveryBackend* backend,
              const V1TransportDiscoveryOptions& options,
              std::uint8_t* next_label,
              std::uint8_t signal,
              const std::uint8_t* payload,
              std::size_t payload_size,
              ParsedResponse* parsed,
              V1TransportDiscoveryResult* result,
              V1TransportCancelProbe cancel_probe,
              void* cancel_context) {
    std::array<std::uint8_t, AVDTP_MAX_SIGNALING_PACKET> request = {};
    std::array<std::uint8_t, kMaximumSignalingResponse> response = {};
    const std::uint8_t label = *next_label;
    *next_label = static_cast<std::uint8_t>((label + 1u) & 0x0Fu);
    const std::size_t request_size = avdtp_write_single(
        request.data(),
        request.size(),
        label,
        AVDTP_MESSAGE_COMMAND,
        signal,
        payload,
        payload_size);
    if (request_size == 0u) {
        result->protocol_error =
            V1TransportDiscoveryProtocolError::InvalidResponse;
        SetPrimary(result,
                   V1TransportDiscoveryDisposition::ProtocolFailure,
                   result->stage);
        return false;
    }
    std::size_t response_size = 0u;
    std::uint32_t backend_error = 0u;
    ++result->signaling_exchanges;
    if (!backend->ExchangeSignaling(
            request.data(),
            request_size,
            response.data(),
            response.size(),
            &response_size,
            options.exchange_timeout_ms,
            &backend_error)) {
        result->backend_error = backend_error;
        SetPrimary(
            result,
            IsCancelled(cancel_probe, cancel_context)
                ? V1TransportDiscoveryDisposition::Cancelled
                : V1TransportDiscoveryDisposition::BackendFailure,
            result->stage);
        return false;
    }
    if (!ParseResponse(response.data(),
                       response_size,
                       label,
                       signal,
                       parsed)) {
        result->protocol_error =
            V1TransportDiscoveryProtocolError::InvalidResponse;
        SetPrimary(result,
                   V1TransportDiscoveryDisposition::ProtocolFailure,
                   result->stage);
        return false;
    }
    return true;
}

}  // namespace

V1TransportDiscoveryResult RunV1TransportDiscoveryOnce(
    V1TransportDiscoveryBackend* backend,
    const V1TransportDiscoveryOptions& options,
    V1TransportCancelProbe cancel_probe,
    void* cancel_context) {
    V1TransportDiscoveryResult result;
    if (backend == nullptr || !OptionsAreValid(options)) {
        return result;
    }
    if (IsCancelled(cancel_probe, cancel_context)) {
        SetPrimary(&result,
                   V1TransportDiscoveryDisposition::Cancelled,
                   V1TransportDiscoveryStage::OpenSignaling);
        return result;
    }

    result.stage = V1TransportDiscoveryStage::OpenSignaling;
    result.open_attempts = 1u;
    std::uint32_t backend_error = 0u;
    if (!backend->OpenSignaling(options.open_timeout_ms, &backend_error)) {
        result.backend_error = backend_error;
        SetPrimary(
            &result,
            IsCancelled(cancel_probe, cancel_context)
                ? V1TransportDiscoveryDisposition::Cancelled
                : V1TransportDiscoveryDisposition::BackendFailure,
            V1TransportDiscoveryStage::OpenSignaling);
        return result;
    }
    result.signaling_opened = true;
    if (IsCancelled(cancel_probe, cancel_context)) {
        SetPrimary(&result,
                   V1TransportDiscoveryDisposition::Cancelled,
                   V1TransportDiscoveryStage::Discover);
        return Finish(backend, result);
    }

    std::uint8_t next_label = 0u;
    ParsedResponse parsed;
    result.stage = V1TransportDiscoveryStage::Discover;
    if (!Exchange(backend,
                  options,
                  &next_label,
                  AVDTP_SIGNAL_DISCOVER,
                  nullptr,
                  0u,
                  &parsed,
                  &result,
                  cancel_probe,
                  cancel_context)) {
        return Finish(backend, result);
    }
    if (parsed.message_type != AVDTP_MESSAGE_ACCEPT) {
        result.protocol_error =
            V1TransportDiscoveryProtocolError::RemoteRejected;
        result.remote_reject_error = parsed.reject_error;
        SetPrimary(&result,
                   V1TransportDiscoveryDisposition::ProtocolFailure,
                   V1TransportDiscoveryStage::Discover);
        return Finish(backend, result);
    }

    std::array<std::uint8_t, AVDTP_MAX_REMOTE_SEIDS> sink_seids = {};
    const std::size_t sink_count = CollectAudioSinks(
        parsed.payload,
        parsed.payload_size,
        sink_seids.data(),
        sink_seids.size());
    result.sink_candidates = static_cast<std::uint32_t>(sink_count);
    if (sink_count == 0u) {
        result.protocol_error =
            V1TransportDiscoveryProtocolError::NoAudioSink;
        SetPrimary(&result,
                   V1TransportDiscoveryDisposition::ProtocolFailure,
                   V1TransportDiscoveryStage::Discover);
        return Finish(backend, result);
    }

    result.stage = V1TransportDiscoveryStage::GetCapabilities;
    for (std::size_t index = 0u; index < sink_count; ++index) {
        if (IsCancelled(cancel_probe, cancel_context)) {
            SetPrimary(&result,
                       V1TransportDiscoveryDisposition::Cancelled,
                       V1TransportDiscoveryStage::GetCapabilities);
            return Finish(backend, result);
        }
        const std::uint8_t seid = sink_seids[index];
        const std::uint8_t seid_payload =
            static_cast<std::uint8_t>(seid << 2u);
        if (!Exchange(backend,
                      options,
                      &next_label,
                      AVDTP_SIGNAL_GET_ALL_CAPABILITIES,
                      &seid_payload,
                      1u,
                      &parsed,
                      &result,
                      cancel_probe,
                      cancel_context)) {
            return Finish(backend, result);
        }
        if (parsed.message_type != AVDTP_MESSAGE_ACCEPT) {
            ++result.legacy_capability_fallbacks;
            if (IsCancelled(cancel_probe, cancel_context)) {
                SetPrimary(&result,
                           V1TransportDiscoveryDisposition::Cancelled,
                           V1TransportDiscoveryStage::GetCapabilities);
                return Finish(backend, result);
            }
            if (!Exchange(backend,
                          options,
                          &next_label,
                          AVDTP_SIGNAL_GET_CAPABILITIES,
                          &seid_payload,
                          1u,
                          &parsed,
                          &result,
                          cancel_probe,
                          cancel_context)) {
                return Finish(backend, result);
            }
        }
        if (parsed.message_type != AVDTP_MESSAGE_ACCEPT) {
            result.remote_reject_error = parsed.reject_error;
            continue;
        }

        ldac_capabilities remote = {};
        const ldac_codec_status capability_status =
            ldac_find_in_service_capabilities(
                parsed.payload, parsed.payload_size, &remote);
        if (capability_status == LDAC_CODEC_NOT_FOUND) {
            continue;
        }
        if (capability_status != LDAC_CODEC_OK) {
            result.protocol_error =
                V1TransportDiscoveryProtocolError::InvalidResponse;
            SetPrimary(&result,
                       V1TransportDiscoveryDisposition::ProtocolFailure,
                       V1TransportDiscoveryStage::GetCapabilities);
            return Finish(backend, result);
        }
        ldac_configuration configuration = {};
        if (ldac_choose_configuration(
                options.local_capabilities,
                remote,
                options.preferred_sample_rate_hz,
                &configuration) != LDAC_CODEC_OK) {
            result.protocol_error =
                V1TransportDiscoveryProtocolError::NoCommonConfiguration;
            SetPrimary(&result,
                       V1TransportDiscoveryDisposition::ProtocolFailure,
                       V1TransportDiscoveryStage::GetCapabilities);
            return Finish(backend, result);
        }
        result.remote_seid = seid;
        result.remote_capabilities = remote;
        result.configuration = configuration;
        result.protocol_error = V1TransportDiscoveryProtocolError::None;
        SetPrimary(&result,
                   V1TransportDiscoveryDisposition::Succeeded,
                   V1TransportDiscoveryStage::GetCapabilities);
        return Finish(backend, result);
    }

    result.protocol_error =
        V1TransportDiscoveryProtocolError::LdacNotSupported;
    SetPrimary(&result,
               V1TransportDiscoveryDisposition::ProtocolFailure,
               V1TransportDiscoveryStage::GetCapabilities);
    return Finish(backend, result);
}

}  // namespace native_ldac::agent
