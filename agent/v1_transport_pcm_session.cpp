// SPDX-License-Identifier: Apache-2.0
#include "v1_transport_pcm_session.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

#include "ldac_native/avdtp.h"
#include "ldac_native/ldac_encoder.h"
#include "ldac_native/rtp_ldac.h"
#include "v1_linked_stereo_block_limiter.h"
#include "v1_sent_frame_fade_envelope.h"

namespace native_ldac::agent {
namespace {

constexpr std::size_t kResponseCapacity = 4096u;
constexpr std::uint32_t kMaximumTimeoutMs = 30000u;
constexpr std::uint32_t kMaximumAudiblePreflightTimeoutMs = 120000u;
constexpr std::uint32_t kMaximumDurationMs = 60000u;
constexpr std::uint32_t kMaximumPacketCount = 32768u;
constexpr std::uint32_t kMaximumPreflightEpochRestarts = 32u;
constexpr std::uint32_t kMaximumPeerSignalingCommands = 3u;
constexpr std::uint32_t kMaximumPcmTimeoutToleranceMs = 5000u;
constexpr std::uint32_t kTimeoutError = 1460u;
constexpr std::uint32_t kNotReadyError = 21u;
constexpr std::uint32_t kWaitTimeoutError = 258u;
constexpr std::uint32_t kDeviceNotConnectedError = 1167u;
constexpr std::uint32_t kFileNotFoundError = 2u;
constexpr std::uint32_t kPathNotFoundError = 3u;
constexpr std::uint32_t kInvalidHandleError = 6u;
constexpr std::uint32_t kOperationAbortedError = 995u;
constexpr std::uint32_t kCancelledError = 1223u;
constexpr std::uint32_t kPauseResumePrepareWindowMs = 1000u;
// A Render restart can briefly overlap the Bluetooth transport's device-
// ready transition. Retry only ERROR_NOT_READY, for at most one second; all
// other failures remain fail-closed and no AVDTP channel is reopened here.
constexpr unsigned int kTransientMediaWriteRetries = 50u;
constexpr std::uint8_t kAvdtpErrorSepInUse = 0x13u;
constexpr float kHardMaximumGainScalar = 1.0f;
constexpr float kHardMaximumOutputPeak = 0.25f;
constexpr float kLegacyFidelitySamplePeakCeiling = 0.89125094f;
constexpr float kTransparentSamplePeakCeiling = 1.0f;
constexpr float kMaximumLimiterReleaseMs = 1000.0f;
constexpr float kMaximumCeilingRampMs = 5000.0f;
constexpr float kMaximumBoundaryEnvelopeMs = 1000.0f;
static_assert(LDAC_ENCODER_PCM_FRAMES_PER_CALL ==
              kV1LinkedStereoLimiterMaximumFrames);
static_assert(LDAC_ENCODER_PCM_FRAMES_PER_CALL ==
              kV1SentFrameFadeMaximumFrames);

std::uint8_t ChannelModeCapability(ldac_encoder_channel_mode mode) {
    switch (mode) {
        case LDAC_ENCODER_CHANNEL_STEREO:
            return LDAC_CM_STEREO;
        case LDAC_ENCODER_CHANNEL_DUAL:
            return LDAC_CM_DUAL;
        case LDAC_ENCODER_CHANNEL_MONO:
            return LDAC_CM_MONO;
    }
    return 0u;
}

bool IsPauseResumeWaitError(std::uint32_t error) {
    return error == kWaitTimeoutError || error == kTimeoutError ||
        error == kNotReadyError || error == kDeviceNotConnectedError ||
        error == kFileNotFoundError || error == kPathNotFoundError ||
        error == kInvalidHandleError || error == kOperationAbortedError ||
        error == kCancelledError;
}

V1TransportPcmStopDisposition ProbeStop(
    V1TransportPcmStopProbe probe,
    void* context) {
    return probe == nullptr
        ? V1TransportPcmStopDisposition::None
        : probe(context);
}

V1TransportPcmStopDisposition WaitForExplicitStop(
    V1TransportPcmStopProbe probe,
    void* context,
    std::uint32_t timeout_ms) {
    auto stop = ProbeStop(probe, context);
    if (stop != V1TransportPcmStopDisposition::None || timeout_ms == 0u) {
        return stop;
    }
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);
    do {
        std::this_thread::sleep_for(std::chrono::milliseconds(10u));
        stop = ProbeStop(probe, context);
        if (stop != V1TransportPcmStopDisposition::None) {
            return stop;
        }
    } while (std::chrono::steady_clock::now() < deadline);
    return V1TransportPcmStopDisposition::None;
}

void SetFailure(V1TransportPcmResult* result,
                V1TransportConfigurationDisposition disposition,
                V1TransportSilenceStage stage) {
    result->primary_disposition = disposition;
    result->disposition = disposition;
    result->stage = stage;
}

void UpdateDurationTelemetry(V1TransportPcmResult* result) {
    if (result->pcm_format.sample_rate_hz == 0u) {
        return;
    }
    const std::uint64_t actual_duration_ms =
        result->pcm_frames_sent * 1000u /
        result->pcm_format.sample_rate_hz;
    result->actual_duration_ms = static_cast<std::uint32_t>(std::min(
        actual_duration_ms,
        static_cast<std::uint64_t>(
            std::numeric_limits<std::uint32_t>::max())));
    const std::uint64_t target_frames =
        static_cast<std::uint64_t>(result->pcm_format.sample_rate_hz) *
        result->target_duration_ms / 1000u;
    result->completed_full_duration = result->target_duration_ms != 0u &&
        result->pcm_frames_sent >= target_frames;
}

V1TransportPcmResult Finish(V1TransportSilenceBackend* backend,
                            V1TransportPcmSource* source,
                            V1TransportPcmResult result) {
    UpdateDurationTelemetry(&result);
    if (result.signaling_opened) {
        result.close_attempted = true;
        std::uint32_t error = 0u;
        result.close_succeeded = backend->CloseSignaling(&error);
        result.cleanup_error = error;
        if (!result.close_succeeded) {
            result.disposition =
                V1TransportConfigurationDisposition::CleanupFailure;
            result.stage = V1TransportSilenceStage::CloseChannels;
        }
    }
    if (result.consumer_lease_held) {
        std::uint32_t error = 0u;
        const bool released = source->Release(&error);
        result.consumer_lease_held = false;
        if (released) {
            ++result.consumer_lease_release_count;
        } else {
            if (result.cleanup_error == 0u) result.cleanup_error = error;
            result.disposition =
                V1TransportConfigurationDisposition::CleanupFailure;
            result.stage = V1TransportSilenceStage::ReleasePcm;
        }
    }
    result.consumer_lease_released =
        result.consumer_lease_acquired &&
        !result.consumer_lease_held &&
        result.consumer_lease_acquire_count ==
            result.consumer_lease_release_count;
    return result;
}

bool StopIsCancel(V1TransportPcmStopProbe probe, void* context) {
    return ProbeStop(probe, context) ==
        V1TransportPcmStopDisposition::Cancel;
}

bool BuildPeerDiscoverAccept(
    const avdtp_source& state,
    const avdtp_header& command,
    const std::uint8_t* packet,
    std::size_t packet_size,
    std::array<std::uint8_t, AVDTP_MAX_SIGNALING_PACKET>* response,
    std::size_t* response_size) {
    if (packet == nullptr || response == nullptr || response_size == nullptr ||
        command.packet_type != AVDTP_PACKET_SINGLE ||
        command.message_type != AVDTP_MESSAGE_COMMAND ||
        command.signal_id != AVDTP_SIGNAL_DISCOVER ||
        command.payload_offset != packet_size ||
        state.state != AVDTP_SOURCE_DISCOVER_SENT ||
        state.pending_signal_id != AVDTP_SIGNAL_DISCOVER ||
        state.local_seid == 0u || state.local_seid > 0x3Fu) {
        return false;
    }
    const std::uint8_t endpoint[] = {
        static_cast<std::uint8_t>((state.local_seid << 2u) | 0x02u),
        0x00u,
    };
    *response_size = avdtp_write_single(
        response->data(), response->size(), command.transaction_label,
        AVDTP_MESSAGE_ACCEPT, AVDTP_SIGNAL_DISCOVER,
        endpoint, sizeof(endpoint));
    return *response_size != 0u;
}

bool BuildPeerSetConfigurationReject(
    const avdtp_source& state,
    const avdtp_header& command,
    const std::uint8_t* packet,
    std::size_t packet_size,
    std::array<std::uint8_t, AVDTP_MAX_SIGNALING_PACKET>* response,
    std::size_t* response_size) {
    constexpr std::size_t kLdacConfigurationPayloadSize = 16u;
    if (packet == nullptr || response == nullptr || response_size == nullptr ||
        command.packet_type != AVDTP_PACKET_SINGLE ||
        command.message_type != AVDTP_MESSAGE_COMMAND ||
        command.signal_id != AVDTP_SIGNAL_SET_CONFIGURATION ||
        command.payload_offset + kLdacConfigurationPayloadSize != packet_size ||
        state.state != AVDTP_SOURCE_CAPABILITIES_SENT ||
        (state.pending_signal_id != AVDTP_SIGNAL_GET_CAPABILITIES &&
         state.pending_signal_id != AVDTP_SIGNAL_GET_ALL_CAPABILITIES) ||
        state.local_seid == 0u || state.local_seid > 0x3Fu) {
        return false;
    }
    const auto* payload = packet + command.payload_offset;
    const auto initiator_seid = static_cast<std::uint8_t>(payload[1] >> 2u);
    if ((payload[0] & 0x03u) != 0u ||
        static_cast<std::uint8_t>(payload[0] >> 2u) != state.local_seid ||
        (payload[1] & 0x03u) != 0u || initiator_seid == 0u ||
        payload[2] != AVDTP_SERVICE_MEDIA_TRANSPORT || payload[3] != 0u) {
        return false;
    }
    ldac_capabilities requested{};
    if (ldac_find_in_service_capabilities(
            payload + 2u, kLdacConfigurationPayloadSize - 2u,
            &requested) != LDAC_CODEC_OK ||
        (requested.sample_rates &
            static_cast<std::uint8_t>(requested.sample_rates - 1u)) != 0u ||
        (requested.channel_modes &
            static_cast<std::uint8_t>(requested.channel_modes - 1u)) != 0u ||
        (requested.sample_rates & state.local_capabilities.sample_rates) !=
            requested.sample_rates ||
        (requested.channel_modes & state.local_capabilities.channel_modes) !=
            requested.channel_modes) {
        return false;
    }
    const std::uint8_t reject[] = {0x00u, kAvdtpErrorSepInUse};
    *response_size = avdtp_write_single(
        response->data(), response->size(), command.transaction_label,
        AVDTP_MESSAGE_REJECT, AVDTP_SIGNAL_SET_CONFIGURATION,
        reject, sizeof(reject));
    return *response_size != 0u;
}

bool BuildPeerCloseAccept(
    const avdtp_source& state,
    const avdtp_header& command,
    const std::uint8_t* packet,
    std::size_t packet_size,
    std::array<std::uint8_t, AVDTP_MAX_SIGNALING_PACKET>* response,
    std::size_t* response_size) {
    if (packet == nullptr || response == nullptr || response_size == nullptr ||
        command.packet_type != AVDTP_PACKET_SINGLE ||
        command.message_type != AVDTP_MESSAGE_COMMAND ||
        command.signal_id != AVDTP_SIGNAL_CLOSE ||
        command.payload_offset + 1u != packet_size ||
        (state.state != AVDTP_SOURCE_STREAMING &&
         state.state != AVDTP_SOURCE_OPEN) ||
        state.local_seid == 0u || state.local_seid > 0x3Fu ||
        (packet[command.payload_offset] & 0x03u) != 0u ||
        static_cast<std::uint8_t>(
            packet[command.payload_offset] >> 2u) != state.local_seid) {
        return false;
    }
    *response_size = avdtp_write_single(
        response->data(), response->size(), command.transaction_label,
        AVDTP_MESSAGE_ACCEPT, AVDTP_SIGNAL_CLOSE, nullptr, 0u);
    return *response_size != 0u;
}

enum class PeerStreamControlDisposition {
    Continue,
    PeerClosed,
    Failure,
};

PeerStreamControlDisposition ObservePeerStreamControl(
    V1TransportSilenceBackend* backend,
    const V1TransportPcmOptions& options,
    const avdtp_source& state,
    V1TransportPcmResult* result) {
    if (!options.observe_peer_close_while_streaming) {
        return PeerStreamControlDisposition::Continue;
    }
    std::array<std::uint8_t, AVDTP_MAX_SIGNALING_PACKET> packet{};
    std::size_t packet_size = 0u;
    std::uint32_t error = 0u;
    const auto disposition = backend->PollPeerSignalingRead(
        packet.data(), packet.size(), &packet_size, &error);
    if (disposition == V1TransportSignalingReadDisposition::NoPacket) {
        return PeerStreamControlDisposition::Continue;
    }
    if (disposition == V1TransportSignalingReadDisposition::TimedOut) {
        ++result->peer_signaling_read_timeouts;
        if (backend->BeginPeerSignalingRead(kMaximumTimeoutMs, &error)) {
            return PeerStreamControlDisposition::Continue;
        }
        result->backend_error = error;
        SetFailure(result,
                   V1TransportConfigurationDisposition::BackendFailure,
                   V1TransportSilenceStage::WritePcm);
        return PeerStreamControlDisposition::Failure;
    }
    if (disposition == V1TransportSignalingReadDisposition::Failure) {
        result->backend_error = error;
        SetFailure(result,
                   V1TransportConfigurationDisposition::BackendFailure,
                   V1TransportSilenceStage::WritePcm);
        return PeerStreamControlDisposition::Failure;
    }

    avdtp_header header{};
    const bool header_available = avdtp_parse_header(
        packet.data(), packet_size, &header) == AVDTP_OK;
    result->last_signaling_response_size =
        static_cast<std::uint32_t>(packet_size);
    result->last_signaling_rx_header_available = header_available;
    if (header_available) {
        result->last_signaling_rx_transaction_label =
            header.transaction_label;
        result->last_signaling_rx_message_type =
            static_cast<std::uint8_t>(header.message_type);
        result->last_signaling_rx_signal_id = header.signal_id;
    }
    if (header_available &&
        header.message_type == AVDTP_MESSAGE_COMMAND) {
        ++result->peer_signaling_commands_received;
    }
    std::array<std::uint8_t, AVDTP_MAX_SIGNALING_PACKET> response{};
    std::size_t response_size = 0u;
    if (!header_available ||
        result->peer_signaling_commands_received >
            kMaximumPeerSignalingCommands + 1u ||
        result->peer_close_commands_accepted != 0u ||
        !BuildPeerCloseAccept(state, header, packet.data(), packet_size,
                              &response, &response_size)) {
        result->protocol_error = AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE;
        SetFailure(result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   V1TransportSilenceStage::WritePcm);
        return PeerStreamControlDisposition::Failure;
    }
    avdtp_header response_header{};
    if (avdtp_parse_header(response.data(), response_size,
                           &response_header) != AVDTP_OK) {
        result->protocol_error = AVDTP_SOURCE_ERROR_BAD_PACKET;
        SetFailure(result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   V1TransportSilenceStage::WritePcm);
        return PeerStreamControlDisposition::Failure;
    }
    result->last_signaling_tx_header_available = true;
    result->last_signaling_tx_transaction_label =
        response_header.transaction_label;
    result->last_signaling_tx_message_type =
        static_cast<std::uint8_t>(response_header.message_type);
    result->last_signaling_tx_signal_id = response_header.signal_id;
    if (!backend->SendPeerSignalingResponse(
            response.data(), response_size,
            options.exchange_timeout_ms, &error)) {
        result->backend_error = error;
        SetFailure(result,
                   V1TransportConfigurationDisposition::BackendFailure,
                   V1TransportSilenceStage::WritePcm);
        return PeerStreamControlDisposition::Failure;
    }
    ++result->peer_close_commands_accepted;
    result->ended_by_peer_close = true;
    result->remote_stream_cleanup_required = false;
    SetFailure(result,
               V1TransportConfigurationDisposition::Cancelled,
               V1TransportSilenceStage::WritePcm);
    return PeerStreamControlDisposition::PeerClosed;
}

bool BuildPeerCapabilitiesAccept(
    const avdtp_source& state,
    const avdtp_header& command,
    const std::uint8_t* packet,
    std::size_t packet_size,
    std::array<std::uint8_t, AVDTP_MAX_SIGNALING_PACKET>* response,
    std::size_t* response_size) {
    if (packet == nullptr || response == nullptr || response_size == nullptr ||
        command.packet_type != AVDTP_PACKET_SINGLE ||
        command.message_type != AVDTP_MESSAGE_COMMAND ||
        (command.signal_id != AVDTP_SIGNAL_GET_CAPABILITIES &&
         command.signal_id != AVDTP_SIGNAL_GET_ALL_CAPABILITIES) ||
        command.payload_offset + 1u != packet_size ||
        static_cast<std::uint8_t>(packet[command.payload_offset] >> 2u) !=
            state.local_seid ||
        state.state != AVDTP_SOURCE_CAPABILITIES_SENT ||
        state.local_seid == 0u || state.local_seid > 0x3Fu ||
        state.local_capabilities.sample_rates == 0u ||
        state.local_capabilities.channel_modes == 0u) {
        return false;
    }
    std::array<std::uint8_t, 14u> capabilities{};
    capabilities[0] = AVDTP_SERVICE_MEDIA_TRANSPORT;
    capabilities[1] = 0x00u;
    capabilities[2] = AVDTP_SERVICE_MEDIA_CODEC;
    capabilities[3] = 0x0Au;
    capabilities[4] = static_cast<std::uint8_t>(
        AVDTP_MEDIA_TYPE_AUDIO << 4u);
    capabilities[5] = AVDTP_CODEC_VENDOR;
    ldac_build_codec_info(capabilities.data() + 6u,
                          state.local_capabilities.sample_rates,
                          state.local_capabilities.channel_modes);
    *response_size = avdtp_write_single(
        response->data(), response->size(), command.transaction_label,
        AVDTP_MESSAGE_ACCEPT, command.signal_id,
        capabilities.data(), capabilities.size());
    return *response_size != 0u;
}

bool Exchange(V1TransportSilenceBackend* backend,
              const V1TransportPcmOptions& options,
              avdtp_source* state,
              const avdtp_action& action,
              V1TransportSilenceStage stage,
              V1TransportPcmStopProbe stop_probe,
              void* stop_context,
              bool cancel_only,
              V1TransportPcmResult* result,
              avdtp_action* next) {
    const auto stop = ProbeStop(stop_probe, stop_context);
    if (action.kind != AVDTP_ACTION_SEND_SIGNALING ||
        (cancel_only ? stop == V1TransportPcmStopDisposition::Cancel
                     : stop != V1TransportPcmStopDisposition::None)) {
        SetFailure(result,
                   stop != V1TransportPcmStopDisposition::None
                       ? V1TransportConfigurationDisposition::Cancelled
                       : V1TransportConfigurationDisposition::ProtocolFailure,
                   stage);
        return false;
    }
    std::array<std::uint8_t, kResponseCapacity> response{};
    std::array<std::uint8_t, AVDTP_MAX_SIGNALING_PACKET> peer_response{};
    std::size_t response_size = 0u;
    std::size_t outbound_size = action.packet_size;
    const std::uint8_t* outbound = action.packet;
    std::uint32_t error = 0u;
    result->last_signaling_response_size = 0u;
    result->last_signaling_tx_header_available = false;
    result->last_signaling_tx_transaction_label = 0u;
    result->last_signaling_tx_message_type = 0u;
    result->last_signaling_tx_signal_id = 0u;
    result->last_signaling_rx_header_available = false;
    result->last_signaling_rx_transaction_label = 0u;
    result->last_signaling_rx_message_type = 0u;
    result->last_signaling_rx_signal_id = 0u;
    avdtp_header tx_header{};
    if (avdtp_parse_header(action.packet, action.packet_size, &tx_header) ==
        AVDTP_OK) {
        result->last_signaling_tx_header_available = true;
        result->last_signaling_tx_transaction_label =
            tx_header.transaction_label;
        result->last_signaling_tx_message_type =
            static_cast<std::uint8_t>(tx_header.message_type);
        result->last_signaling_tx_signal_id = tx_header.signal_id;
    }
    result->stage = stage;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(options.exchange_timeout_ms);
    bool first_transfer = true;
    for (;;) {
        const auto current_stop = ProbeStop(stop_probe, stop_context);
        if (cancel_only
                ? current_stop == V1TransportPcmStopDisposition::Cancel
                : current_stop != V1TransportPcmStopDisposition::None) {
            SetFailure(result,
                       V1TransportConfigurationDisposition::Cancelled,
                       stage);
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            result->backend_error = kTimeoutError;
            SetFailure(result,
                       V1TransportConfigurationDisposition::BackendFailure,
                       stage);
            return false;
        }
        const auto remaining = std::chrono::duration_cast<
            std::chrono::milliseconds>(deadline - now).count();
        const auto timeout_ms = first_transfer
            ? options.exchange_timeout_ms
            : static_cast<std::uint32_t>(
                std::max<std::int64_t>(1, remaining));
        first_transfer = false;
        response.fill(0u);
        response_size = 0u;
        ++result->signaling_exchanges;
        if (!backend->ExchangeSignaling(outbound,
                                        outbound_size,
                                        response.data(),
                                        response.size(),
                                        &response_size,
                                        timeout_ms,
                                        &error)) {
            result->backend_error = error;
            SetFailure(
                result,
                StopIsCancel(stop_probe, stop_context)
                    ? V1TransportConfigurationDisposition::Cancelled
                    : V1TransportConfigurationDisposition::BackendFailure,
                stage);
            return false;
        }
        result->last_signaling_response_size =
            static_cast<std::uint32_t>(response_size);
        avdtp_header rx_header{};
        const bool header_available = avdtp_parse_header(
            response.data(), response_size, &rx_header) == AVDTP_OK;
        result->last_signaling_rx_header_available = header_available;
        if (header_available) {
            result->last_signaling_rx_transaction_label =
                rx_header.transaction_label;
            result->last_signaling_rx_message_type =
                static_cast<std::uint8_t>(rx_header.message_type);
            result->last_signaling_rx_signal_id = rx_header.signal_id;
        }
        if (!header_available ||
            rx_header.message_type != AVDTP_MESSAGE_COMMAND) {
            break;
        }
        ++result->peer_signaling_commands_received;
        std::size_t peer_response_size = 0u;
        bool accepted = false;
        if (result->peer_signaling_commands_received <=
                kMaximumPeerSignalingCommands &&
            rx_header.signal_id == AVDTP_SIGNAL_DISCOVER &&
            result->peer_discover_commands_accepted == 0u) {
            accepted = BuildPeerDiscoverAccept(
                *state, rx_header, response.data(), response_size,
                &peer_response, &peer_response_size);
            if (accepted) {
                ++result->peer_discover_commands_accepted;
            }
        } else if (result->peer_signaling_commands_received <=
                       kMaximumPeerSignalingCommands &&
                   result->peer_discover_commands_accepted == 1u &&
                   result->peer_capability_commands_accepted == 0u) {
            accepted = BuildPeerCapabilitiesAccept(
                *state, rx_header, response.data(), response_size,
                &peer_response, &peer_response_size);
            if (accepted) {
                ++result->peer_capability_commands_accepted;
            }
        } else if (result->peer_signaling_commands_received <=
                       kMaximumPeerSignalingCommands &&
                   result->peer_discover_commands_accepted == 1u &&
                   result->peer_capability_commands_accepted == 1u &&
                   result->peer_configuration_commands_rejected == 0u) {
            accepted = BuildPeerSetConfigurationReject(
                *state, rx_header, response.data(), response_size,
                &peer_response, &peer_response_size);
            if (accepted) {
                ++result->peer_configuration_commands_rejected;
            }
        }
        if (!accepted) {
            result->protocol_error = AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE;
            SetFailure(result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       stage);
            return false;
        }
        outbound = peer_response.data();
        outbound_size = peer_response_size;
    }
    *next = avdtp_source_handle_signaling(
        state, response.data(), response_size);
    if (next->kind == AVDTP_ACTION_ERROR) {
        result->protocol_error = next->error_code;
        SetFailure(result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   stage);
        return false;
    }
    return true;
}

bool SupportedSampleRate(unsigned value) {
    return value == 44100u || value == 48000u ||
           value == 88200u || value == 96000u;
}

float SanitizeSample(float sample) {
    if (!std::isfinite(sample)) return 0.0f;
    return std::clamp(sample, -1.0f, 1.0f);
}

bool ApplyOutputPolicy(
    std::array<float, LDAC_ENCODER_PCM_FRAMES_PER_CALL *
                          LDAC_ENCODER_STEREO_CHANNELS>* pcm,
    const V1TransportPcmOptions& options,
    unsigned sample_rate_hz,
    float output_peak_ceiling,
    bool record_pre_gain_peak,
    V1LinkedStereoBlockLimiterState* limiter_state,
    V1TransportPcmResult* result) {
    for (float& sample : *pcm) {
        const float sanitized = SanitizeSample(sample);
        if (record_pre_gain_peak) {
            result->maximum_pre_gain_peak = std::max(
                result->maximum_pre_gain_peak, std::fabs(sanitized));
        }
        const float unlimited = sanitized * options.maximum_gain_scalar;
        result->maximum_unlimited_post_gain_peak = std::max(
            result->maximum_unlimited_post_gain_peak,
            std::fabs(unlimited));
        sample = unlimited;
    }
    if (options.limiter_mode == V1TransportPcmLimiterMode::HardClip) {
        for (float& sample : *pcm) {
            if (std::fabs(sample) > output_peak_ceiling) {
                ++result->limited_output_samples;
            }
            sample = std::clamp(
                sample,
                -output_peak_ceiling,
                output_peak_ceiling);
            result->maximum_post_gain_peak = std::max(
                result->maximum_post_gain_peak, std::fabs(sample));
        }
        return true;
    }

    V1LinkedStereoBlockLimiterTelemetry telemetry{};
    if (!ProcessV1LinkedStereoBlock(
            pcm->data(), LDAC_ENCODER_PCM_FRAMES_PER_CALL,
            output_peak_ceiling, sample_rate_hz,
            options.limiter_release_ms, limiter_state, &telemetry)) {
        return false;
    }
    ++result->limiter_blocks_processed;
    result->limiter_minimum_gain = std::min(
        result->limiter_minimum_gain, telemetry.minimum_gain);
    result->limiter_last_gain = telemetry.last_gain;
    result->limiter_maximum_gain_step = std::max(
        result->limiter_maximum_gain_step,
        telemetry.maximum_gain_step);
    result->limiter_attack_count += telemetry.attack_count;
    result->limiter_gain_reduced_frames += telemetry.reduced_frame_count;
    result->limiter_gain_reduced_samples += telemetry.reduced_sample_count;
    result->limiter_fallback_clamp_count +=
        telemetry.fallback_clamp_count;
    result->limiter_sanitized_sample_count +=
        telemetry.sanitized_sample_count;
    result->limiter_pre_over_ceiling_frames +=
        telemetry.pre_over_ceiling_frame_count;
    result->limiter_pre_over_ceiling_samples +=
        telemetry.pre_over_ceiling_sample_count;
    result->limited_output_samples +=
        telemetry.pre_over_ceiling_sample_count;
    result->maximum_post_gain_peak = std::max(
        result->maximum_post_gain_peak, telemetry.output_peak);
    return true;
}

bool SameLockedPcmFormat(const V1TransportPcmFormat& locked,
                         const V1TransportPcmFormat& current) {
    return current.sample_rate_hz == locked.sample_rate_hz &&
        current.bits_per_sample == locked.bits_per_sample &&
        current.volume_control_available ==
            locked.volume_control_available;
}

bool SameVolume(const V1TransportPcmFormat& previous,
                const V1TransportPcmFormat& current) {
    constexpr float kScalarTolerance = 0.000001f;
    constexpr float kDbTolerance = 0.001f;
    return current.muted == previous.muted &&
        std::fabs(current.volume_scalar - previous.volume_scalar) <=
            kScalarTolerance &&
        std::fabs(current.volume_db - previous.volume_db) <= kDbTolerance;
}

enum class StableVolumeCheck {
    Stable,
    QueryFailure,
    StreamEpochChanged,
    FormatChanged,
    Changed,
};

StableVolumeCheck CheckStableVolume(
    V1TransportPcmSource* source,
    const V1TransportPcmFormat& locked,
    V1TransportPcmFormat* observed,
    bool allow_dynamic_volume,
    V1TransportPcmResult* result,
    std::uint32_t* error) {
    V1TransportPcmFormat current{};
    ++result->volume_query_count;
    if (!source->QueryFormat(&current, error)) {
        result->volume_stable = false;
        return StableVolumeCheck::QueryFailure;
    }
    result->volume_scalar_minimum = std::min(
        result->volume_scalar_minimum, current.volume_scalar);
    result->volume_scalar_maximum = std::max(
        result->volume_scalar_maximum, current.volume_scalar);
    result->volume_scalar_last = current.volume_scalar;
    result->volume_db_minimum = std::min(
        result->volume_db_minimum, current.volume_db);
    result->volume_db_maximum = std::max(
        result->volume_db_maximum, current.volume_db);
    result->volume_db_last = current.volume_db;
    if (!SameLockedPcmFormat(locked, current) ||
        !std::isfinite(current.volume_scalar) ||
        !std::isfinite(current.volume_db)) {
        result->volume_stable = false;
        return StableVolumeCheck::FormatChanged;
    }
    const bool epoch_changed =
        current.stream_epoch != observed->stream_epoch;
    if (!SameVolume(*observed, current)) {
        ++result->volume_change_count;
        result->volume_stable = false;
        observed->muted = current.muted;
        observed->volume_scalar = current.volume_scalar;
        observed->volume_db = current.volume_db;
        if (!allow_dynamic_volume) {
            return StableVolumeCheck::Changed;
        }
    }
    if (epoch_changed) {
        return StableVolumeCheck::StreamEpochChanged;
    }
    return StableVolumeCheck::Stable;
}

std::uint64_t PcmElapsedMilliseconds(
    const std::chrono::steady_clock::time_point& session_start) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - session_start);
    return elapsed.count() < 0
        ? 0u
        : static_cast<std::uint64_t>(elapsed.count());
}

void RecordPcmStreamStop(
    V1TransportPcmSource* source,
    std::uint32_t read_error,
    const std::chrono::steady_clock::time_point& session_start,
    V1TransportPcmResult* result) {
    if (source == nullptr || result == nullptr) return;
    ++result->pcm_stream_stop_count;
    result->pcm_stream_stop_detected = true;
    result->pcm_stream_stop_error = read_error;
    result->pcm_stream_stop_elapsed_ms =
        PcmElapsedMilliseconds(session_start);

    V1TransportPcmSnapshot snapshot{};
    std::uint32_t snapshot_error = 0u;
    if (source->QuerySnapshot(&snapshot, &snapshot_error)) {
        result->pcm_stream_stop_snapshot = snapshot;
        result->pcm_stream_stop_snapshot_valid = true;
        result->pcm_stream_stop_snapshot_error = 0u;
    } else {
        result->pcm_stream_stop_snapshot_valid = false;
        result->pcm_stream_stop_snapshot_error = snapshot_error;
    }
}

float CeilingForSentFrame(const V1TransportPcmOptions& options,
                          unsigned sample_rate_hz,
                          std::uint64_t committed_sent_frames) {
    if (options.ceiling_ramp_ms <= 0.0f) {
        return options.maximum_output_peak;
    }
    const double ramp_frames = std::ceil(
        static_cast<double>(sample_rate_hz) *
        static_cast<double>(options.ceiling_ramp_ms) / 1000.0);
    const double next_frame = static_cast<double>(committed_sent_frames);
    const double progress = std::clamp(
        next_frame / ramp_frames, 0.0, 1.0);
    return static_cast<float>(
        static_cast<double>(options.ceiling_ramp_start) +
        (static_cast<double>(options.maximum_output_peak) -
         static_cast<double>(options.ceiling_ramp_start)) * progress);
}

bool PrepareAudiblePcm(V1TransportPcmSource* source,
                       const V1TransportPcmOptions& options,
                       V1TransportPcmStopProbe stop_probe,
                       void* stop_context,
                       V1TransportPcmResult* result,
                       const std::chrono::steady_clock::time_point& session_start,
                       std::array<float,
                           LDAC_ENCODER_PCM_FRAMES_PER_CALL *
                           LDAC_ENCODER_STEREO_CHANNELS>* first_block) {
    std::uint32_t error = 0u;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(options.audible_preflight_timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (ProbeStop(stop_probe, stop_context) !=
            V1TransportPcmStopDisposition::None) {
            SetFailure(result,
                       V1TransportConfigurationDisposition::Cancelled,
                       result->stage);
            return false;
        }
        if (!result->consumer_lease_held) {
            const auto prepare_remaining = std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
            const std::uint32_t prepare_timeout_ms =
                static_cast<std::uint32_t>(std::max<std::int64_t>(
                    1, prepare_remaining.count()));
            result->stage = V1TransportSilenceStage::PreparePcm;
            ++result->pcm_prepare_attempts;
            if (!source->Prepare(&result->pcm_format,
                                 prepare_timeout_ms,
                                 &error)) {
                result->backend_error = error;
                SetFailure(result,
                           ProbeStop(stop_probe, stop_context) !=
                                   V1TransportPcmStopDisposition::None
                               ? V1TransportConfigurationDisposition::Cancelled
                               : V1TransportConfigurationDisposition::BackendFailure,
                           result->stage);
                return false;
            }
            result->pcm_prepared = true;
            result->consumer_lease_acquired = true;
            result->consumer_lease_released = false;
            result->consumer_lease_held = true;
            ++result->consumer_lease_acquire_count;
            if (!SupportedSampleRate(result->pcm_format.sample_rate_hz) ||
                (result->pcm_format.bits_per_sample != 16u &&
                 result->pcm_format.bits_per_sample != 24u)) {
                result->protocol_error =
                    LDAC_ENCODER_CONFIGURATION_FAILED;
                SetFailure(
                    result,
                    V1TransportConfigurationDisposition::ProtocolFailure,
                    result->stage);
                return false;
            }
        }
        const auto remaining = std::chrono::duration_cast<
            std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
        const std::uint32_t read_timeout_ms = static_cast<std::uint32_t>(
            std::max<std::int64_t>(1, std::min<std::int64_t>(
                options.pcm_read_timeout_ms, remaining.count())));
        std::size_t frames_read = 0u;
        const auto disposition = source->ReadFrames(
            first_block->data(), LDAC_ENCODER_PCM_FRAMES_PER_CALL,
            read_timeout_ms, &frames_read, &error);
        if (disposition == V1TransportPcmReadDisposition::Timeout) {
            continue;
        }
        if (disposition == V1TransportPcmReadDisposition::StreamStopped) {
            const auto stream_stop = ProbeStop(stop_probe, stop_context);
            if (stream_stop == V1TransportPcmStopDisposition::None) {
                RecordPcmStreamStop(source, error, session_start, result);
            }
            if (stream_stop != V1TransportPcmStopDisposition::None) {
                result->backend_error = error;
                SetFailure(result,
                           V1TransportConfigurationDisposition::Cancelled,
                           result->stage);
                return false;
            }
            result->stage = V1TransportSilenceStage::ReleasePcm;
            const bool released = source->Release(&error);
            result->consumer_lease_held = false;
            if (!released) {
                result->cleanup_error = error;
                SetFailure(
                    result,
                    V1TransportConfigurationDisposition::CleanupFailure,
                    result->stage);
                return false;
            }
            ++result->consumer_lease_release_count;
            result->consumer_lease_released =
                result->consumer_lease_acquire_count ==
                    result->consumer_lease_release_count;
            if (result->pcm_epoch_restarts >=
                kMaximumPreflightEpochRestarts) {
                result->protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
                SetFailure(
                    result,
                    V1TransportConfigurationDisposition::ProtocolFailure,
                    V1TransportSilenceStage::PreparePcm);
                return false;
            }
            ++result->pcm_epoch_restarts;
            continue;
        }
        if (disposition != V1TransportPcmReadDisposition::Data ||
            frames_read != LDAC_ENCODER_PCM_FRAMES_PER_CALL) {
            result->backend_error = error;
            SetFailure(result,
                       V1TransportConfigurationDisposition::BackendFailure,
                       result->stage);
            return false;
        }
        result->pcm_frames_read += frames_read;
        float peak = 0.0f;
        for (float& sample : *first_block) {
            sample = SanitizeSample(sample);
            peak = std::max(peak, std::fabs(sample));
        }
        result->maximum_pre_gain_peak =
            std::max(result->maximum_pre_gain_peak, peak);
        if (peak >= options.audible_peak_threshold) {
            result->audible_pcm_confirmed_before_open = true;
            return true;
        }
        result->pre_start_pcm_frames_discarded += frames_read;
    }
    result->protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
    SetFailure(result,
               V1TransportConfigurationDisposition::ProtocolFailure,
               V1TransportSilenceStage::PreparePcm);
    return false;
}

bool WriteMediaWithTransientNotReadyRetry(
    V1TransportSilenceBackend* backend,
    const std::uint8_t* packet,
    std::size_t packet_size,
    std::uint32_t timeout_ms,
    bool allow_graceful_stop,
    V1TransportPcmStopProbe stop_probe,
    void* stop_context,
    V1TransportPcmResult* result,
    std::uint32_t* error) {
    for (unsigned int attempt = 0u;
         attempt <= kTransientMediaWriteRetries;
         ++attempt) {
        if (backend->WriteMedia(
                packet, packet_size, timeout_ms, error)) {
            return true;
        }
        const auto stop = ProbeStop(stop_probe, stop_context);
        const bool stop_requested =
            stop == V1TransportPcmStopDisposition::Cancel ||
            (!allow_graceful_stop &&
             stop == V1TransportPcmStopDisposition::Graceful);
        if (error == nullptr || *error != kNotReadyError ||
            attempt == kTransientMediaWriteRetries || stop_requested) {
            if (error != nullptr && *error == kNotReadyError &&
                attempt == kTransientMediaWriteRetries && result != nullptr) {
                ++result->media_write_not_ready_exhaustions;
            }
            if (result != nullptr) {
                (void)backend->GetLastMediaWriteDiagnostics(
                    &result->media_write_diagnostics);
            }
            return false;
        }
        if (result != nullptr) {
            ++result->media_write_not_ready_retries;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20u));
    }
    return false;
}

bool SendBoundarySilence(
    V1TransportSilenceBackend* backend,
    V1TransportPcmSource* source,
    const V1TransportPcmOptions& options,
    ldac_encoder* encoder,
    float duration_ms,
    bool allow_graceful_stop,
    V1TransportPcmStopProbe stop_probe,
    void* stop_context,
    std::uint16_t* sequence,
    std::uint32_t* timestamp,
    std::uint64_t* transport_frames_sent,
    std::uint64_t* boundary_frames_sent,
    std::uint32_t* boundary_packets_written,
    V1TransportPcmResult* result) {
    if (duration_ms == 0.0f) {
        return true;
    }
    if (backend == nullptr || source == nullptr || encoder == nullptr ||
        sequence == nullptr || timestamp == nullptr ||
        transport_frames_sent == nullptr || boundary_frames_sent == nullptr ||
        boundary_packets_written == nullptr || result == nullptr ||
        result->pcm_format.sample_rate_hz == 0u) {
        return false;
    }

    constexpr std::uint64_t kFramesPerBlock =
        LDAC_ENCODER_PCM_FRAMES_PER_CALL;
    const auto requested_frames = static_cast<std::uint64_t>(std::ceil(
        static_cast<double>(result->pcm_format.sample_rate_hz) *
        static_cast<double>(duration_ms) / 1000.0));
    const std::uint64_t target_frames =
        ((requested_frames + kFramesPerBlock - 1u) / kFramesPerBlock) *
        kFramesPerBlock;
    if (target_frames == 0u) {
        return true;
    }

    std::array<float, LDAC_ENCODER_PCM_FRAMES_PER_CALL *
                          LDAC_ENCODER_STEREO_CHANNELS> boundary_pcm{};
    std::array<std::uint8_t, LDAC_ENCODER_MAX_OUTPUT_BYTES> encoded{};
    std::array<std::uint8_t, 4096u> packet{};
    const unsigned samples_per_frame =
        ldac_encoder_samples_per_transport_frame(encoder);
    std::uint64_t generated_frames = 0u;
    std::uint64_t pending_frames = 0u;
    std::uint32_t flush_blocks = 0u;
    while (generated_frames < target_frames || pending_frames != 0u) {
        const bool padding = generated_frames >= target_frames;
        generated_frames += kFramesPerBlock;
        pending_frames += kFramesPerBlock;
        if (padding && ++flush_blocks > 64u) {
            result->protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
            SetFailure(result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       V1TransportSilenceStage::WritePcm);
            return false;
        }

        std::size_t encoded_size = 0u;
        std::uint8_t frame_count = 0u;
        const auto encode_status = ldac_encoder_encode_f32(
            encoder, boundary_pcm.data(), LDAC_ENCODER_PCM_FRAMES_PER_CALL,
            encoded.data(), encoded.size(), &encoded_size, &frame_count);
        if (encode_status != LDAC_ENCODER_OK) {
            result->protocol_error = encode_status;
            SetFailure(result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       V1TransportSilenceStage::WritePcm);
            return false;
        }
        if (encoded_size == 0u) {
            continue;
        }

        const std::uint32_t packet_samples =
            frame_count * samples_per_frame;
        if (packet_samples != pending_frames) {
            result->protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
            SetFailure(result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       V1TransportSilenceStage::WritePcm);
            return false;
        }
        std::size_t packet_size = 0u;
        if (ldac_rtp_build_unfragmented(
                packet.data(), packet.size(), result->outgoing_mtu,
                *sequence, *timestamp, 0x56315043u, frame_count,
                encoded.data(), encoded_size, &packet_size) != LDAC_RTP_OK) {
            result->protocol_error = AVDTP_SOURCE_ERROR_BAD_PACKET;
            SetFailure(result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       V1TransportSilenceStage::WritePcm);
            return false;
        }
        if (options.maximum_packets != 0u &&
            result->media_packets_written >= options.maximum_packets) {
            result->protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
            SetFailure(result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       V1TransportSilenceStage::WritePcm);
            return false;
        }

        std::uint32_t error = 0u;
        if (!source->WaitUntilSample(*transport_frames_sent,
                                     result->pcm_format.sample_rate_hz,
                                     &error)) {
            const auto stop = ProbeStop(stop_probe, stop_context);
            result->backend_error = error;
            SetFailure(result,
                       stop == V1TransportPcmStopDisposition::Cancel
                           ? V1TransportConfigurationDisposition::Cancelled
                           : V1TransportConfigurationDisposition::BackendFailure,
                       V1TransportSilenceStage::WritePcm);
            return false;
        }
        ++result->pacing_waits;
        const auto stop = ProbeStop(stop_probe, stop_context);
        if (stop == V1TransportPcmStopDisposition::Cancel ||
            (!allow_graceful_stop &&
             stop == V1TransportPcmStopDisposition::Graceful)) {
            SetFailure(result,
                       V1TransportConfigurationDisposition::Cancelled,
                       V1TransportSilenceStage::WritePcm);
            return false;
        }
        if (!WriteMediaWithTransientNotReadyRetry(
                backend, packet.data(), packet_size,
                options.media_timeout_ms, allow_graceful_stop,
                stop_probe, stop_context, result, &error)) {
            result->backend_error = error;
            SetFailure(result,
                       ProbeStop(stop_probe, stop_context) ==
                               V1TransportPcmStopDisposition::Cancel
                           ? V1TransportConfigurationDisposition::Cancelled
                           : V1TransportConfigurationDisposition::BackendFailure,
                       V1TransportSilenceStage::WritePcm);
            return false;
        }
        ++result->media_packets_written;
        result->media_bytes_written +=
            static_cast<std::uint32_t>(packet_size);
        *transport_frames_sent += packet_samples;
        result->transport_frames_sent = *transport_frames_sent;
        *boundary_frames_sent += packet_samples;
        ++(*boundary_packets_written);
        *timestamp += packet_samples;
        ++(*sequence);
        pending_frames = 0u;
    }
    return true;
}

}  // namespace

V1TransportPcmResult RunV1TransportPcmBurstOnce(
    V1TransportSilenceBackend* backend,
    V1TransportPcmSource* source,
    const V1TransportPcmOptions& options,
    V1TransportPcmStopProbe stop_probe,
    void* stop_context,
    V1TransportPcmStartedNotifier started_notifier,
    void* started_context) {
    const auto session_start = std::chrono::steady_clock::now();
    V1TransportPcmResult result;
    result.target_duration_ms = options.duration_ms;
    result.maximum_gain_scalar = options.maximum_gain_scalar;
    result.maximum_output_peak_ceiling = options.maximum_output_peak;
    result.limiter_mode = options.limiter_mode;
    result.limiter_release_ms = options.limiter_release_ms;
    result.session_generation = options.session_generation;
    result.startup_silence_ms = options.startup_silence_ms;
    result.fade_in_ms = options.fade_in_ms;
    result.ceiling_ramp_start = options.ceiling_ramp_start;
    result.ceiling_ramp_ms = options.ceiling_ramp_ms;
    result.ceiling_ramp_last = options.ceiling_ramp_start;
    const bool limiter_mode_valid =
        options.limiter_mode == V1TransportPcmLimiterMode::HardClip ||
        options.limiter_mode == V1TransportPcmLimiterMode::LinkedStereoBlock ||
        options.limiter_mode ==
            V1TransportPcmLimiterMode::LinkedStereoSamplePeakFidelity;
    const std::uint8_t requested_channel_mode =
        ChannelModeCapability(options.channel_mode);
    const bool fidelity_mode = options.limiter_mode ==
        V1TransportPcmLimiterMode::LinkedStereoSamplePeakFidelity;
    const bool fidelity_ceiling_valid =
        std::fabs(options.maximum_output_peak -
                  kLegacyFidelitySamplePeakCeiling) <= 0.000001f ||
        std::fabs(options.maximum_output_peak -
                  kTransparentSamplePeakCeiling) <= 0.000001f;
    const bool profile_configuration_invalid =
        options.continuous_until_stop
        ? options.duration_ms != 0u || options.maximum_packets != 0u ||
              stop_probe == nullptr
        : options.duration_ms == 0u ||
              options.duration_ms > kMaximumDurationMs ||
              options.maximum_packets == 0u ||
              options.maximum_packets > kMaximumPacketCount;
    if (backend == nullptr || source == nullptr ||
        options.open_timeout_ms == 0u ||
        options.open_timeout_ms > kMaximumTimeoutMs ||
        options.exchange_timeout_ms == 0u ||
        options.exchange_timeout_ms > kMaximumTimeoutMs ||
        options.media_timeout_ms == 0u ||
        options.media_timeout_ms > kMaximumTimeoutMs ||
        options.pcm_read_timeout_ms == 0u ||
        options.pcm_read_timeout_ms > kMaximumTimeoutMs ||
        options.pcm_timeout_tolerance_ms >
            kMaximumPcmTimeoutToleranceMs ||
        options.post_start_stop_classification_timeout_ms >
            kMaximumTimeoutMs ||
        options.audible_preflight_timeout_ms == 0u ||
        options.audible_preflight_timeout_ms >
            kMaximumAudiblePreflightTimeoutMs ||
        profile_configuration_invalid ||
        options.preferred_media_mtu == 0u ||
        !std::isfinite(options.maximum_gain_scalar) ||
        options.maximum_gain_scalar <= 0.0f ||
        options.maximum_gain_scalar > kHardMaximumGainScalar ||
        !std::isfinite(options.maximum_output_peak) ||
        options.maximum_output_peak <= 0.0f ||
        (!fidelity_mode &&
            options.maximum_output_peak > kHardMaximumOutputPeak) ||
        (fidelity_mode &&
            options.maximum_output_peak > kTransparentSamplePeakCeiling) ||
        !std::isfinite(options.audible_peak_threshold) ||
        options.audible_peak_threshold <= 0.0f ||
        !std::isfinite(options.startup_silence_ms) ||
        options.startup_silence_ms < 0.0f ||
        options.startup_silence_ms > kMaximumBoundaryEnvelopeMs ||
        !limiter_mode_valid ||
        (options.limiter_mode != V1TransportPcmLimiterMode::HardClip &&
            (!std::isfinite(options.limiter_release_ms) ||
             options.limiter_release_ms <= 0.0f ||
             options.limiter_release_ms > kMaximumLimiterReleaseMs)) ||
        (fidelity_mode &&
            (!fidelity_ceiling_valid ||
             options.session_generation == 0u ||
             ((!options.require_stable_volume &&
               !options.single_gain_mode) ||
              (options.single_gain_mode &&
               options.require_stable_volume)) ||
             (options.allow_post_start_pcm_rebind &&
              options.post_start_stop_classification_timeout_ms == 0u) ||
             !std::isfinite(options.fade_in_ms) ||
             options.fade_in_ms <= 0.0f ||
             options.fade_in_ms > kV1SentFrameFadeMaximumDurationMs ||
             !std::isfinite(options.ceiling_ramp_start) ||
             options.ceiling_ramp_start <= 0.0f ||
             options.ceiling_ramp_start > options.maximum_output_peak ||
             !std::isfinite(options.ceiling_ramp_ms) ||
             options.ceiling_ramp_ms < 0.0f ||
             options.ceiling_ramp_ms > kMaximumCeilingRampMs ||
             (options.ceiling_ramp_ms == 0.0f &&
              std::fabs(options.ceiling_ramp_start -
                  options.maximum_output_peak) > 0.000001f) ||
             (options.ceiling_ramp_ms > 0.0f &&
              options.ceiling_ramp_start > kHardMaximumOutputPeak))) ||
        (!fidelity_mode &&
            (options.startup_silence_ms != 0.0f ||
             options.fade_in_ms != 0.0f ||
             options.ceiling_ramp_ms != 0.0f ||
             options.single_gain_mode ||
             options.require_stable_volume ||
             options.allow_dynamic_volume ||
             options.allow_post_start_pcm_rebind))) {
        return result;
    }
    if (requested_channel_mode == 0u) {
        return result;
    }

    std::array<float, LDAC_ENCODER_PCM_FRAMES_PER_CALL *
                          LDAC_ENCODER_STEREO_CHANNELS> pcm{};
    if (!PrepareAudiblePcm(source, options, stop_probe, stop_context,
                           &result, session_start, &pcm)) {
        return Finish(backend, source, result);
    }
    const V1TransportPcmFormat locked_format = result.pcm_format;
    V1TransportPcmFormat observed_format = locked_format;
    if (options.require_stable_volume) {
        if (!locked_format.volume_control_available || locked_format.muted ||
            !std::isfinite(locked_format.volume_scalar) ||
            !std::isfinite(locked_format.volume_db)) {
            result.protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
            SetFailure(&result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       V1TransportSilenceStage::PreparePcm);
            return Finish(backend, source, result);
        }
        result.volume_scalar_minimum = locked_format.volume_scalar;
        result.volume_scalar_maximum = locked_format.volume_scalar;
        result.volume_scalar_last = locked_format.volume_scalar;
        result.volume_db_minimum = locked_format.volume_db;
        result.volume_db_maximum = locked_format.volume_db;
        result.volume_db_last = locked_format.volume_db;
        result.volume_stable = true;
    }
    if (ProbeStop(stop_probe, stop_context) !=
        V1TransportPcmStopDisposition::None) {
        SetFailure(&result, V1TransportConfigurationDisposition::Cancelled,
                   V1TransportSilenceStage::OpenSignaling);
        return Finish(backend, source, result);
    }

    result.stage = V1TransportSilenceStage::OpenSignaling;
    result.open_attempts = 1u;
    std::uint32_t error = 0u;
    if (!backend->OpenSignaling(options.open_timeout_ms, &error)) {
        result.backend_error = error;
        (void)backend->GetLastOpenDiagnostics(
            &result.open_diagnostics);
        SetFailure(&result,
                   ProbeStop(stop_probe, stop_context) ==
                           V1TransportPcmStopDisposition::Cancel
                       ? V1TransportConfigurationDisposition::Cancelled
                       : V1TransportConfigurationDisposition::BackendFailure,
                   result.stage);
        return Finish(backend, source, result);
    }
    result.signaling_opened = true;
    (void)backend->GetLastOpenDiagnostics(&result.open_diagnostics);

    avdtp_source avdtp{};
    avdtp_source_init(&avdtp, {LDAC_SF_ALL, requested_channel_mode}, 1u,
                      result.pcm_format.sample_rate_hz);
    avdtp_action action = avdtp_source_begin(&avdtp);
    while (action.kind == AVDTP_ACTION_SEND_SIGNALING) {
        avdtp_header header{};
        if (avdtp_parse_header(action.packet, action.packet_size, &header) !=
            AVDTP_OK) {
            SetFailure(&result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       V1TransportSilenceStage::Negotiate);
            return Finish(backend, source, result);
        }
        avdtp_action next{};
        if (!Exchange(backend, options, &avdtp, action,
                      V1TransportSilenceStage::Negotiate, stop_probe,
                      stop_context, false, &result, &next)) {
            return Finish(backend, source, result);
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
        return Finish(backend, source, result);
    }
    result.remote_seid = avdtp.remote_seid;
    result.configuration = avdtp.configuration;
    if (ldac_sample_rate_to_hz(result.configuration.sample_rate) !=
        result.pcm_format.sample_rate_hz ||
        result.configuration.channel_mode != requested_channel_mode) {
        result.protocol_error = LDAC_ENCODER_CONFIGURATION_FAILED;
        SetFailure(&result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   V1TransportSilenceStage::Negotiate);
        return Finish(backend, source, result);
    }

    result.stage = V1TransportSilenceStage::OpenMedia;
    if (!backend->OpenMedia(options.media_timeout_ms,
                            options.preferred_media_mtu,
                            &result.incoming_mtu,
                            &result.outgoing_mtu,
                            &error)) {
        result.backend_error = error;
        SetFailure(&result,
                   V1TransportConfigurationDisposition::BackendFailure,
                   result.stage);
        return Finish(backend, source, result);
    }
    result.media_opened = true;
    if (result.incoming_mtu == 0u ||
        result.outgoing_mtu <= LDAC_RTP_OVERHEAD ||
        result.outgoing_mtu - LDAC_RTP_OVERHEAD <
            LDAC_ENCODER_MIN_PAYLOAD_MTU ||
        avdtp_source_media_channel_opened(&avdtp).kind !=
            AVDTP_ACTION_SESSION_OPEN) {
        result.protocol_error = AVDTP_SOURCE_ERROR_BAD_PACKET;
        SetFailure(&result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   result.stage);
        return Finish(backend, source, result);
    }

    ldac_encoder* encoder = nullptr;
    const auto create_status = ldac_encoder_create_with_channel_mode(
        &encoder, result.outgoing_mtu - LDAC_RTP_OVERHEAD,
        options.quality, result.pcm_format.sample_rate_hz,
        options.channel_mode);
    if (create_status != LDAC_ENCODER_OK || encoder == nullptr) {
        result.protocol_error = create_status;
        SetFailure(&result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   V1TransportSilenceStage::Start);
        return Finish(backend, source, result);
    }
    struct EncoderOwner {
        ldac_encoder* value;
        ~EncoderOwner() { ldac_encoder_destroy(value); }
    } encoder_owner{encoder};
    result.encoder_quality = options.quality;
    result.nominal_ldac_bitrate_kbps =
        ldac_encoder_nominal_bitrate_kbps(encoder);

    avdtp_action next{};
    if (!Exchange(backend, options, &avdtp, avdtp_source_start(&avdtp),
                  V1TransportSilenceStage::Start, stop_probe,
                  stop_context, false, &result, &next)) {
        return Finish(backend, source, result);
    }
    result.remote_stream_cleanup_required = true;
    if (next.kind != AVDTP_ACTION_STREAM_READY) {
        result.protocol_error = AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE;
        SetFailure(&result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   V1TransportSilenceStage::Start);
        return Finish(backend, source, result);
    }
    result.avdtp_start_accepted = true;
    if (started_notifier != nullptr &&
        !started_notifier(started_context, &error)) {
        result.backend_error = error;
        SetFailure(&result,
                   V1TransportConfigurationDisposition::BackendFailure,
                   V1TransportSilenceStage::Start);
        return Finish(backend, source, result);
    }
    result.media_started_notified = true;
    if (options.observe_peer_close_while_streaming &&
        !backend->BeginPeerSignalingRead(kMaximumTimeoutMs, &error)) {
        result.backend_error = error;
        SetFailure(&result,
                   V1TransportConfigurationDisposition::BackendFailure,
                   V1TransportSilenceStage::WritePcm);
        return Finish(backend, source, result);
    }

    std::array<std::uint8_t, LDAC_ENCODER_MAX_OUTPUT_BYTES> encoded{};
    std::array<std::uint8_t, 4096u> packet{};
    std::array<float, LDAC_ENCODER_PCM_FRAMES_PER_CALL *
                          LDAC_ENCODER_STEREO_CHANNELS> faded_pcm{};
    std::array<float, LDAC_ENCODER_PCM_FRAMES_PER_CALL *
                          LDAC_ENCODER_STEREO_CHANNELS> resume_faded_pcm{};
    const unsigned samples_per_frame =
        ldac_encoder_samples_per_transport_frame(encoder);
    const std::uint64_t target_samples = options.continuous_until_stop
        ? std::numeric_limits<std::uint64_t>::max()
        : static_cast<std::uint64_t>(result.pcm_format.sample_rate_hz) *
              options.duration_ms / 1000u;
    std::uint16_t sequence = 0u;
    std::uint32_t timestamp = 0u;
    std::uint64_t transport_frames_sent = 0u;
    bool use_preflight_block = true;
    bool graceful_stop = false;
    bool transport_suspended_for_pause = false;
    bool force_post_start_pcm_rebind = false;
    bool resume_fade_active = false;
    std::uint64_t resume_fade_frames_processed = 0u;
    std::uint64_t resume_fade_target_frames = 0u;
    std::uint64_t pcm_timeout_streak_ms = 0u;
    V1LinkedStereoBlockLimiterState limiter_state{};
    V1SentFrameFadeState fade_state{};
    constexpr std::size_t kMaximumPendingFadeBlocks = 64u;
    std::array<V1SentFrameFadeBlock,
               kMaximumPendingFadeBlocks> pending_fade_blocks{};
    std::array<V1SentFrameFadeTelemetry,
               kMaximumPendingFadeBlocks> pending_fade_telemetry{};
    std::array<float, kMaximumPendingFadeBlocks> pending_fade_ceilings{};
    std::size_t pending_fade_block_count = 0u;
    std::uint64_t pending_fade_frames = 0u;
    if (fidelity_mode && !BeginV1SentFrameFadeSession(
            &fade_state, options.session_generation)) {
        result.protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
        SetFailure(&result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   V1TransportSilenceStage::WritePcm);
        return Finish(backend, source, result);
    }
    result.fade_session_started = fidelity_mode;
    struct FadeSessionOwner {
        V1SentFrameFadeState* state;
        ~FadeSessionOwner() { EndV1SentFrameFadeSession(state); }
    } fade_owner{fidelity_mode ? &fade_state : nullptr};
    const auto reset_transport_encoder = [&]() -> bool {
        ldac_encoder* replacement = nullptr;
        const auto create_status = ldac_encoder_create_with_channel_mode(
            &replacement,
            result.outgoing_mtu - LDAC_RTP_OVERHEAD,
            options.quality,
            result.pcm_format.sample_rate_hz,
            options.channel_mode);
        if (create_status != LDAC_ENCODER_OK || replacement == nullptr) {
            result.protocol_error = create_status;
            SetFailure(&result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       V1TransportSilenceStage::WritePcm);
            return false;
        }
        ldac_encoder_destroy(encoder_owner.value);
        encoder_owner.value = replacement;
        encoder = replacement;
        return true;
    };
    enum class PauseTransitionResult {
        Resumed,
        GracefulStop,
        Ended,
    };
    const auto suspend_and_wait_for_render = [&]() -> PauseTransitionResult {
        if (options.observe_peer_close_while_streaming &&
            !backend->CancelPeerSignalingRead(&error)) {
            result.backend_error = error;
            SetFailure(&result,
                       V1TransportConfigurationDisposition::BackendFailure,
                       V1TransportSilenceStage::Suspend);
            return PauseTransitionResult::Ended;
        }
        if (!Exchange(backend, options, &avdtp,
                      avdtp_source_suspend(&avdtp),
                      V1TransportSilenceStage::Suspend, stop_probe,
                      stop_context, true, &result, &next)) {
            return PauseTransitionResult::Ended;
        }
        if (next.kind != AVDTP_ACTION_STREAM_SUSPENDED) {
            result.protocol_error = AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE;
            SetFailure(&result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       V1TransportSilenceStage::Suspend);
            return PauseTransitionResult::Ended;
        }
        result.avdtp_suspend_accepted = true;
        result.remote_stream_cleanup_required = false;
        ++result.pause_suspend_count;
        transport_suspended_for_pause = true;
        pending_fade_block_count = 0u;
        pending_fade_frames = 0u;

        result.stage = V1TransportSilenceStage::ReleasePcm;
        if (!source->Release(&error)) {
            result.cleanup_error = error;
            SetFailure(&result,
                       V1TransportConfigurationDisposition::CleanupFailure,
                       result.stage);
            return PauseTransitionResult::Ended;
        }
        result.consumer_lease_held = false;
        ++result.consumer_lease_release_count;
        result.consumer_lease_released =
            result.consumer_lease_acquire_count ==
                result.consumer_lease_release_count;

        if (options.observe_peer_close_while_streaming &&
            !backend->BeginPeerSignalingRead(kMaximumTimeoutMs, &error)) {
            result.backend_error = error;
            SetFailure(&result,
                       V1TransportConfigurationDisposition::BackendFailure,
                       V1TransportSilenceStage::Suspend);
            return PauseTransitionResult::Ended;
        }

        V1TransportPcmFormat resumed_format{};
        for (;;) {
            const auto paused_stop = ProbeStop(stop_probe, stop_context);
            if (paused_stop == V1TransportPcmStopDisposition::Cancel) {
                SetFailure(&result,
                           V1TransportConfigurationDisposition::Cancelled,
                           V1TransportSilenceStage::Suspend);
                return PauseTransitionResult::Ended;
            }
            if (paused_stop == V1TransportPcmStopDisposition::Graceful) {
                graceful_stop = true;
                return PauseTransitionResult::GracefulStop;
            }
            const auto peer_control = ObservePeerStreamControl(
                backend, options, avdtp, &result);
            if (peer_control != PeerStreamControlDisposition::Continue) {
                return PauseTransitionResult::Ended;
            }

            result.stage = V1TransportSilenceStage::PreparePcm;
            ++result.pcm_prepare_attempts;
            ++result.pause_wait_prepare_attempts;
            if (source->Prepare(
                    &resumed_format, kPauseResumePrepareWindowMs, &error)) {
                result.consumer_lease_acquired = true;
                result.consumer_lease_released = false;
                result.consumer_lease_held = true;
                ++result.consumer_lease_acquire_count;
                break;
            }
            const auto prepare_stop = ProbeStop(stop_probe, stop_context);
            if (prepare_stop == V1TransportPcmStopDisposition::Cancel) {
                SetFailure(&result,
                           V1TransportConfigurationDisposition::Cancelled,
                           V1TransportSilenceStage::PreparePcm);
                return PauseTransitionResult::Ended;
            }
            if (prepare_stop == V1TransportPcmStopDisposition::Graceful) {
                graceful_stop = true;
                return PauseTransitionResult::GracefulStop;
            }
            if (!IsPauseResumeWaitError(error)) {
                result.backend_error = error;
                SetFailure(&result,
                           V1TransportConfigurationDisposition::BackendFailure,
                           V1TransportSilenceStage::PreparePcm);
                return PauseTransitionResult::Ended;
            }
        }

        if (!SameLockedPcmFormat(locked_format, resumed_format) ||
            !std::isfinite(resumed_format.volume_scalar) ||
            !std::isfinite(resumed_format.volume_db)) {
            result.protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
            SetFailure(&result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       V1TransportSilenceStage::PreparePcm);
            return PauseTransitionResult::Ended;
        }
        result.pcm_format.stream_epoch = resumed_format.stream_epoch;
        observed_format = resumed_format;

        if (options.observe_peer_close_while_streaming &&
            !backend->CancelPeerSignalingRead(&error)) {
            result.backend_error = error;
            SetFailure(&result,
                       V1TransportConfigurationDisposition::BackendFailure,
                       V1TransportSilenceStage::Start);
            return PauseTransitionResult::Ended;
        }
        if (!Exchange(backend, options, &avdtp,
                      avdtp_source_start(&avdtp),
                      V1TransportSilenceStage::Start, stop_probe,
                      stop_context, false, &result, &next)) {
            return PauseTransitionResult::Ended;
        }
        if (next.kind != AVDTP_ACTION_STREAM_READY) {
            result.protocol_error = AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE;
            SetFailure(&result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       V1TransportSilenceStage::Start);
            return PauseTransitionResult::Ended;
        }
        ++result.pause_resume_start_count;
        transport_suspended_for_pause = false;
        result.remote_stream_cleanup_required = true;
        if (options.observe_peer_close_while_streaming &&
            !backend->BeginPeerSignalingRead(kMaximumTimeoutMs, &error)) {
            result.backend_error = error;
            SetFailure(&result,
                       V1TransportConfigurationDisposition::BackendFailure,
                       V1TransportSilenceStage::WritePcm);
            return PauseTransitionResult::Ended;
        }
        if (!reset_transport_encoder()) {
            return PauseTransitionResult::Ended;
        }
        if (fidelity_mode && options.startup_silence_ms > 0.0f &&
            !SendBoundarySilence(
                backend, source, options, encoder,
                options.startup_silence_ms, true, stop_probe, stop_context,
                &sequence, &timestamp, &transport_frames_sent,
                &result.startup_silence_frames_sent,
                &result.startup_silence_packets_written, &result)) {
            return PauseTransitionResult::Ended;
        }
        ++result.boundary_resume_count;
        resume_fade_frames_processed = 0u;
        resume_fade_target_frames = static_cast<std::uint64_t>(std::ceil(
            static_cast<double>(result.pcm_format.sample_rate_hz) *
            static_cast<double>(options.fade_in_ms) / 1000.0));
        resume_fade_active = fidelity_mode &&
            resume_fade_target_frames != 0u;
        result.stage = V1TransportSilenceStage::WritePcm;
        return PauseTransitionResult::Resumed;
    };
    if (fidelity_mode && options.startup_silence_ms > 0.0f &&
        !SendBoundarySilence(
            backend, source, options, encoder,
            options.startup_silence_ms, true, stop_probe, stop_context,
            &sequence, &timestamp, &transport_frames_sent,
            &result.startup_silence_frames_sent,
            &result.startup_silence_packets_written, &result)) {
        return Finish(backend, source, result);
    }
    while (result.pcm_frames_sent < target_samples) {
        const auto peer_control = ObservePeerStreamControl(
            backend, options, avdtp, &result);
        if (peer_control != PeerStreamControlDisposition::Continue) {
            return Finish(backend, source, result);
        }
        const auto stop = ProbeStop(stop_probe, stop_context);
        if (stop == V1TransportPcmStopDisposition::Cancel) {
            SetFailure(&result,
                       V1TransportConfigurationDisposition::Cancelled,
                       V1TransportSilenceStage::WritePcm);
            return Finish(backend, source, result);
        }
        if (stop == V1TransportPcmStopDisposition::Graceful) {
            graceful_stop = true;
            break;
        }
        if (!use_preflight_block) {
            std::size_t frames_read = 0u;
            bool forced_rebind = force_post_start_pcm_rebind;
            const auto disposition = forced_rebind
                ? V1TransportPcmReadDisposition::StreamStopped
                : source->ReadFrames(
                    pcm.data(), LDAC_ENCODER_PCM_FRAMES_PER_CALL,
                    options.pcm_read_timeout_ms, &frames_read, &error);
            force_post_start_pcm_rebind = false;
            if (disposition == V1TransportPcmReadDisposition::Timeout) {
                ++result.pcm_transient_timeout_count;
                pcm_timeout_streak_ms += options.pcm_read_timeout_ms;
                result.pcm_transient_timeout_max_streak_ms = std::max(
                    result.pcm_transient_timeout_max_streak_ms,
                    pcm_timeout_streak_ms);
                if (options.pcm_timeout_tolerance_ms != 0u &&
                    pcm_timeout_streak_ms <=
                        options.pcm_timeout_tolerance_ms) {
                    continue;
                }
                ++result.pcm_transient_timeout_exhausted_count;
            }
            auto read_stop = V1TransportPcmStopDisposition::None;
            if (disposition != V1TransportPcmReadDisposition::Data ||
                frames_read != LDAC_ENCODER_PCM_FRAMES_PER_CALL) {
                read_stop = ProbeStop(stop_probe, stop_context);
                if (read_stop == V1TransportPcmStopDisposition::None &&
                    disposition ==
                        V1TransportPcmReadDisposition::StreamStopped &&
                    options.pause_suspend && !forced_rebind) {
                    pcm_timeout_streak_ms = 0u;
                    // Pause the existing AVDTP stream without closing its
                    // signaling/media channels. No encoded packets are sent
                    // while Render is stopped; START resumes the same session.
                    const auto pause_result = suspend_and_wait_for_render();
                    if (pause_result == PauseTransitionResult::Ended) {
                        return Finish(backend, source, result);
                    }
                    if (pause_result ==
                        PauseTransitionResult::GracefulStop) {
                        break;
                    }
                    use_preflight_block = false;
                    continue;
                }
            }
            if (disposition != V1TransportPcmReadDisposition::Data ||
                frames_read != LDAC_ENCODER_PCM_FRAMES_PER_CALL) {
                if (read_stop == V1TransportPcmStopDisposition::None &&
                    disposition ==
                        V1TransportPcmReadDisposition::StreamStopped &&
                    !forced_rebind) {
                    RecordPcmStreamStop(
                        source, error, session_start, &result);
                }
                if (read_stop == V1TransportPcmStopDisposition::None &&
                    disposition ==
                        V1TransportPcmReadDisposition::StreamStopped &&
                    options.allow_post_start_pcm_rebind) {
                    pcm_timeout_streak_ms = 0u;
                    pending_fade_block_count = 0u;
                    pending_fade_frames = 0u;
                    result.stage = V1TransportSilenceStage::ReleasePcm;
                    const bool released = source->Release(&error);
                    result.consumer_lease_held = false;
                    if (!released) {
                        result.cleanup_error = error;
                        SetFailure(
                            &result,
                            V1TransportConfigurationDisposition::CleanupFailure,
                            result.stage);
                        return Finish(backend, source, result);
                    }
                    ++result.consumer_lease_release_count;
                    result.consumer_lease_released =
                        result.consumer_lease_acquire_count ==
                            result.consumer_lease_release_count;
                    if (result.pcm_epoch_restarts >=
                        kMaximumPreflightEpochRestarts) {
                        result.protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
                        graceful_stop = true;
                        break;
                    }
                    ++result.pcm_epoch_restarts;
                    V1TransportPcmFormat rebound_format{};
                    result.stage = V1TransportSilenceStage::PreparePcm;
                    ++result.pcm_prepare_attempts;
                    ++result.pcm_rebind_attempts;
                    result.pcm_rebind_last_timeout_ms =
                        options.post_start_stop_classification_timeout_ms;
                    result.pcm_rebind_last_elapsed_ms =
                        PcmElapsedMilliseconds(session_start);
                    if (!source->Prepare(
                            &rebound_format,
                            options.post_start_stop_classification_timeout_ms,
                            &error)) {
                        ++result.pcm_rebind_failures;
                        result.pcm_rebind_last_error = error;
                        result.pcm_rebind_last_elapsed_ms =
                            PcmElapsedMilliseconds(session_start);
                        read_stop = ProbeStop(stop_probe, stop_context);
                        if (read_stop ==
                            V1TransportPcmStopDisposition::Graceful) {
                            graceful_stop = true;
                            break;
                        }
                        result.backend_error = error;
                        SetFailure(
                            &result,
                            read_stop ==
                                    V1TransportPcmStopDisposition::Cancel
                                ? V1TransportConfigurationDisposition::Cancelled
                                : V1TransportConfigurationDisposition::BackendFailure,
                            V1TransportSilenceStage::PreparePcm);
                        return Finish(backend, source, result);
                    }
                    ++result.pcm_rebind_successes;
                    result.pcm_rebind_last_error = 0u;
                    result.pcm_rebind_last_elapsed_ms =
                        PcmElapsedMilliseconds(session_start);
                    result.consumer_lease_acquired = true;
                    result.consumer_lease_released = false;
                    result.consumer_lease_held = true;
                    ++result.consumer_lease_acquire_count;
                    read_stop = ProbeStop(stop_probe, stop_context);
                    if (read_stop == V1TransportPcmStopDisposition::Cancel) {
                        SetFailure(
                            &result,
                            V1TransportConfigurationDisposition::Cancelled,
                            V1TransportSilenceStage::WritePcm);
                        return Finish(backend, source, result);
                    }
                    if (read_stop ==
                        V1TransportPcmStopDisposition::Graceful) {
                        graceful_stop = true;
                        break;
                    }
                    if (!SameLockedPcmFormat(
                            locked_format, rebound_format) ||
                        !std::isfinite(rebound_format.volume_scalar) ||
                        !std::isfinite(rebound_format.volume_db)) {
                        result.volume_stable = false;
                        result.protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
                        graceful_stop = true;
                        break;
                    }
                    result.volume_scalar_minimum = std::min(
                        result.volume_scalar_minimum,
                        rebound_format.volume_scalar);
                    result.volume_scalar_maximum = std::max(
                        result.volume_scalar_maximum,
                        rebound_format.volume_scalar);
                    result.volume_scalar_last =
                        rebound_format.volume_scalar;
                    result.volume_db_minimum = std::min(
                        result.volume_db_minimum, rebound_format.volume_db);
                    result.volume_db_maximum = std::max(
                        result.volume_db_maximum, rebound_format.volume_db);
                    result.volume_db_last = rebound_format.volume_db;
                    if (!SameVolume(observed_format, rebound_format)) {
                        ++result.volume_change_count;
                        result.volume_stable = false;
                        if (!options.allow_dynamic_volume) {
                            result.protocol_error =
                                LDAC_ENCODER_INVALID_OUTPUT;
                            graceful_stop = true;
                            break;
                        }
                    }
                    result.pcm_format.stream_epoch =
                        rebound_format.stream_epoch;
                    observed_format = rebound_format;
                    if (!reset_transport_encoder()) {
                        return Finish(backend, source, result);
                    }
                    if (fidelity_mode && options.startup_silence_ms > 0.0f &&
                        !SendBoundarySilence(
                            backend, source, options, encoder,
                            options.startup_silence_ms, true,
                            stop_probe, stop_context,
                            &sequence, &timestamp, &transport_frames_sent,
                            &result.startup_silence_frames_sent,
                            &result.startup_silence_packets_written,
                            &result)) {
                        return Finish(backend, source, result);
                    }
                    ++result.boundary_resume_count;
                    resume_fade_frames_processed = 0u;
                    resume_fade_target_frames =
                        static_cast<std::uint64_t>(std::ceil(
                            static_cast<double>(result.pcm_format.sample_rate_hz) *
                            static_cast<double>(options.fade_in_ms) / 1000.0));
                    resume_fade_active = fidelity_mode &&
                        resume_fade_target_frames != 0u;
                    continue;
                }
                if (read_stop == V1TransportPcmStopDisposition::None &&
                    disposition ==
                        V1TransportPcmReadDisposition::StreamStopped &&
                    options.post_start_stop_classification_timeout_ms != 0u) {
                    read_stop = WaitForExplicitStop(
                        stop_probe, stop_context,
                        options.post_start_stop_classification_timeout_ms);
                }
                if (read_stop == V1TransportPcmStopDisposition::Graceful) {
                    graceful_stop = true;
                    break;
                }
                if (read_stop != V1TransportPcmStopDisposition::Cancel &&
                    disposition ==
                        V1TransportPcmReadDisposition::StreamStopped &&
                    options.post_start_stop_classification_timeout_ms == 0u) {
                    graceful_stop = true;
                    break;
                }
                result.backend_error = error;
                SetFailure(&result,
                           read_stop == V1TransportPcmStopDisposition::Cancel
                               ? V1TransportConfigurationDisposition::Cancelled
                               : V1TransportConfigurationDisposition::BackendFailure,
                           V1TransportSilenceStage::WritePcm);
                return Finish(backend, source, result);
            }
            if (pcm_timeout_streak_ms != 0u) {
                ++result.pcm_transient_timeout_recovery_count;
                pcm_timeout_streak_ms = 0u;
                pending_fade_block_count = 0u;
                pending_fade_frames = 0u;
                if (!source->ResetPacing(&error)) {
                    result.backend_error = error;
                    SetFailure(
                        &result,
                        V1TransportConfigurationDisposition::BackendFailure,
                        V1TransportSilenceStage::WritePcm);
                    return Finish(backend, source, result);
                }
                if (!reset_transport_encoder()) {
                    return Finish(backend, source, result);
                }
                if (fidelity_mode && options.startup_silence_ms > 0.0f &&
                    !SendBoundarySilence(
                        backend, source, options, encoder,
                        options.startup_silence_ms, true,
                        stop_probe, stop_context,
                        &sequence, &timestamp, &transport_frames_sent,
                        &result.startup_silence_frames_sent,
                        &result.startup_silence_packets_written,
                        &result)) {
                    return Finish(backend, source, result);
                }
                ++result.boundary_resume_count;
                resume_fade_frames_processed = 0u;
                resume_fade_target_frames =
                    static_cast<std::uint64_t>(std::ceil(
                        static_cast<double>(
                            result.pcm_format.sample_rate_hz) *
                        static_cast<double>(options.fade_in_ms) / 1000.0));
                resume_fade_active = fidelity_mode &&
                    resume_fade_target_frames != 0u;
            }
            result.pcm_frames_read += frames_read;
        }
        use_preflight_block = false;
        if (options.require_stable_volume) {
            const auto volume = CheckStableVolume(
                source, locked_format, &observed_format,
                options.allow_dynamic_volume, &result, &error);
            if (volume == StableVolumeCheck::StreamEpochChanged) {
                if (options.allow_post_start_pcm_rebind) {
                    force_post_start_pcm_rebind = true;
                    continue;
                }
                graceful_stop = true;
                break;
            }
            if (volume != StableVolumeCheck::Stable) {
                if (volume == StableVolumeCheck::QueryFailure) {
                    result.backend_error = error;
                } else {
                    result.protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
                }
                graceful_stop = true;
                break;
            }
        }

        std::array<float, LDAC_ENCODER_PCM_FRAMES_PER_CALL *
                              LDAC_ENCODER_STEREO_CHANNELS>* output_pcm = &pcm;
        bool record_pre_gain_peak = true;
        if (fidelity_mode) {
            if (pending_fade_block_count >= kMaximumPendingFadeBlocks) {
                result.protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
                SetFailure(
                    &result,
                    V1TransportConfigurationDisposition::ProtocolFailure,
                    V1TransportSilenceStage::WritePcm);
                return Finish(backend, source, result);
            }
            for (const float sample : pcm) {
                result.maximum_pre_gain_peak = std::max(
                    result.maximum_pre_gain_peak,
                    std::fabs(SanitizeSample(sample)));
            }
            V1SentFrameFadeState prepare_state = fade_state;
            prepare_state.committed_sent_frames += pending_fade_frames;
            if (!PrepareV1SentFrameFadeBlock(
                    pcm.data(), faded_pcm.data(),
                    LDAC_ENCODER_PCM_FRAMES_PER_CALL,
                    result.pcm_format.sample_rate_hz,
                    options.fade_in_ms, &prepare_state,
                    &pending_fade_blocks[pending_fade_block_count],
                    &pending_fade_telemetry[pending_fade_block_count])) {
                result.protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
                SetFailure(
                    &result,
                    V1TransportConfigurationDisposition::ProtocolFailure,
                    V1TransportSilenceStage::WritePcm);
                return Finish(backend, source, result);
            }
            ++result.fade_blocks_prepared;
            ++pending_fade_block_count;
            pending_fade_frames += LDAC_ENCODER_PCM_FRAMES_PER_CALL;
            output_pcm = &faded_pcm;
            record_pre_gain_peak = false;
        }
        if (resume_fade_active) {
            const auto remaining = resume_fade_target_frames -
                resume_fade_frames_processed;
            const auto faded_frames = std::min<std::uint64_t>(
                LDAC_ENCODER_PCM_FRAMES_PER_CALL, remaining);
            for (std::size_t frame = 0u;
                 frame < LDAC_ENCODER_PCM_FRAMES_PER_CALL;
                 ++frame) {
                float gain = 1.0f;
                if (frame < faded_frames) {
                    const auto position = resume_fade_frames_processed +
                        frame + 1u;
                    gain = std::min(
                        1.0f,
                        static_cast<float>(position) /
                            static_cast<float>(resume_fade_target_frames));
                }
                resume_faded_pcm[frame * 2u] =
                    (*output_pcm)[frame * 2u] * gain;
                resume_faded_pcm[frame * 2u + 1u] =
                    (*output_pcm)[frame * 2u + 1u] * gain;
            }
            resume_fade_frames_processed += faded_frames;
            result.boundary_resume_fade_frames += faded_frames;
            resume_fade_active =
                resume_fade_frames_processed < resume_fade_target_frames;
            output_pcm = &resume_faded_pcm;
        }
        const float current_ceiling = CeilingForSentFrame(
            options, result.pcm_format.sample_rate_hz,
            fidelity_mode
                ? fade_state.committed_sent_frames + pending_fade_frames -
                    LDAC_ENCODER_PCM_FRAMES_PER_CALL
                          : result.pcm_frames_sent);
        if (fidelity_mode) {
            pending_fade_ceilings[pending_fade_block_count - 1u] =
                current_ceiling;
        }
        if (!ApplyOutputPolicy(output_pcm, options,
                               result.pcm_format.sample_rate_hz,
                               current_ceiling, record_pre_gain_peak,
                               &limiter_state, &result)) {
            result.protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
            SetFailure(&result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       V1TransportSilenceStage::WritePcm);
            return Finish(backend, source, result);
        }
        std::size_t encoded_size = 0u;
        std::uint8_t frame_count = 0u;
        const auto encode_status = ldac_encoder_encode_f32(
            encoder, output_pcm->data(), LDAC_ENCODER_PCM_FRAMES_PER_CALL,
            encoded.data(), encoded.size(), &encoded_size, &frame_count);
        if (encode_status != LDAC_ENCODER_OK) {
            result.protocol_error = encode_status;
            SetFailure(&result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       V1TransportSilenceStage::WritePcm);
            return Finish(backend, source, result);
        }
        if (encoded_size == 0u) {
            continue;
        }
        std::size_t packet_size = 0u;
        if (ldac_rtp_build_unfragmented(
                packet.data(), packet.size(), result.outgoing_mtu,
                sequence, timestamp, 0x56315043u, frame_count,
                encoded.data(), encoded_size, &packet_size) != LDAC_RTP_OK) {
            result.protocol_error = AVDTP_SOURCE_ERROR_BAD_PACKET;
            SetFailure(&result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       V1TransportSilenceStage::WritePcm);
            return Finish(backend, source, result);
        }
        if (options.maximum_packets != 0u &&
            result.media_packets_written >= options.maximum_packets) {
            result.protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
            SetFailure(&result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       V1TransportSilenceStage::WritePcm);
            return Finish(backend, source, result);
        }
        const std::uint32_t packet_samples =
            frame_count * samples_per_frame;
        V1SentFrameFadeState committed_fade_state = fade_state;
        if (fidelity_mode) {
            if (packet_samples != pending_fade_frames) {
                ++result.fade_commit_failures;
                result.protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
                SetFailure(
                    &result,
                    V1TransportConfigurationDisposition::ProtocolFailure,
                    V1TransportSilenceStage::WritePcm);
                return Finish(backend, source, result);
            }
            for (std::size_t pending = 0u;
                 pending < pending_fade_block_count;
                 ++pending) {
                const auto& block = pending_fade_blocks[pending];
                if (!CommitV1SentFrameFadeBlock(
                        &committed_fade_state, block, block.frame_count)) {
                    ++result.fade_commit_failures;
                    result.protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
                    SetFailure(
                        &result,
                        V1TransportConfigurationDisposition::ProtocolFailure,
                        V1TransportSilenceStage::WritePcm);
                    return Finish(backend, source, result);
                }
            }
        }
        if (!source->WaitUntilSample(transport_frames_sent,
                                     result.pcm_format.sample_rate_hz,
                                     &error)) {
            const auto wait_stop = ProbeStop(stop_probe, stop_context);
            if (wait_stop == V1TransportPcmStopDisposition::Graceful) {
                graceful_stop = true;
                break;
            }
            result.backend_error = error;
            SetFailure(&result,
                       wait_stop == V1TransportPcmStopDisposition::Cancel
                           ? V1TransportConfigurationDisposition::Cancelled
                           : V1TransportConfigurationDisposition::BackendFailure,
                       V1TransportSilenceStage::WritePcm);
            return Finish(backend, source, result);
        }
        ++result.pacing_waits;
        const auto write_stop = ProbeStop(stop_probe, stop_context);
        if (write_stop == V1TransportPcmStopDisposition::Cancel) {
            SetFailure(&result,
                       V1TransportConfigurationDisposition::Cancelled,
                       V1TransportSilenceStage::WritePcm);
            return Finish(backend, source, result);
        }
        if (write_stop == V1TransportPcmStopDisposition::Graceful) {
            graceful_stop = true;
            break;
        }
        if (options.require_stable_volume) {
            const auto volume = CheckStableVolume(
                source, locked_format, &observed_format,
                options.allow_dynamic_volume, &result, &error);
            if (volume == StableVolumeCheck::StreamEpochChanged) {
                if (options.allow_post_start_pcm_rebind) {
                    force_post_start_pcm_rebind = true;
                    continue;
                }
                graceful_stop = true;
                break;
            }
            if (volume != StableVolumeCheck::Stable) {
                if (volume == StableVolumeCheck::QueryFailure) {
                    result.backend_error = error;
                } else {
                    result.protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
                }
                graceful_stop = true;
                break;
            }
        }
        if (!WriteMediaWithTransientNotReadyRetry(
                backend, packet.data(), packet_size,
                options.media_timeout_ms, false,
                stop_probe, stop_context, &result, &error)) {
            result.backend_error = error;
            SetFailure(&result,
                       StopIsCancel(stop_probe, stop_context)
                           ? V1TransportConfigurationDisposition::Cancelled
                           : V1TransportConfigurationDisposition::BackendFailure,
                       V1TransportSilenceStage::WritePcm);
            return Finish(backend, source, result);
        }
        if (fidelity_mode) {
            fade_state = committed_fade_state;
            for (std::size_t pending = 0u;
                 pending < pending_fade_block_count;
                 ++pending) {
                const auto& telemetry = pending_fade_telemetry[pending];
                ++result.fade_blocks_committed;
                result.fade_duration_frames = telemetry.fade_duration_frames;
                result.fade_frames_below_unity +=
                    telemetry.frames_below_unity;
                result.fade_sanitized_sample_count +=
                    telemetry.sanitized_sample_count;
                result.fade_minimum_gain = std::min(
                    result.fade_minimum_gain, telemetry.minimum_gain);
                result.fade_last_gain = telemetry.last_gain;
            }
            result.fade_committed_sent_frames =
                fade_state.committed_sent_frames;
            result.ceiling_ramp_last =
                pending_fade_ceilings[pending_fade_block_count - 1u];
            pending_fade_block_count = 0u;
            pending_fade_frames = 0u;
        }
        ++result.media_packets_written;
        result.media_bytes_written +=
            static_cast<std::uint32_t>(packet_size);
        result.pcm_frames_sent += packet_samples;
        transport_frames_sent += packet_samples;
        result.transport_frames_sent = transport_frames_sent;
        timestamp += packet_samples;
        ++sequence;
    }
    if (fidelity_mode && graceful_stop) {
        pending_fade_block_count = 0u;
        pending_fade_frames = 0u;
    }
    if (fidelity_mode &&
        (result.fade_committed_sent_frames != result.pcm_frames_sent ||
         pending_fade_block_count != 0u || pending_fade_frames != 0u)) {
        ++result.fade_commit_failures;
        result.protocol_error = LDAC_ENCODER_INVALID_OUTPUT;
        SetFailure(&result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   V1TransportSilenceStage::WritePcm);
        return Finish(backend, source, result);
    }
    UpdateDurationTelemetry(&result);
    result.ended_by_graceful_stop = graceful_stop;

    const auto final_peer_control = ObservePeerStreamControl(
        backend, options, avdtp, &result);
    if (final_peer_control != PeerStreamControlDisposition::Continue) {
        return Finish(backend, source, result);
    }
    if (options.observe_peer_close_while_streaming &&
        !backend->CancelPeerSignalingRead(&error)) {
        result.backend_error = error;
        SetFailure(&result,
                   V1TransportConfigurationDisposition::BackendFailure,
                   V1TransportSilenceStage::Suspend);
        return Finish(backend, source, result);
    }

    if (StopIsCancel(stop_probe, stop_context)) {
        SetFailure(&result,
                   V1TransportConfigurationDisposition::Cancelled,
                   V1TransportSilenceStage::Suspend);
        return Finish(backend, source, result);
    }
    if (!transport_suspended_for_pause) {
        if (!Exchange(backend, options, &avdtp,
                      avdtp_source_suspend(&avdtp),
                      V1TransportSilenceStage::Suspend, stop_probe,
                      stop_context, true, &result, &next)) {
            return Finish(backend, source, result);
        }
        if (next.kind != AVDTP_ACTION_STREAM_SUSPENDED) {
            result.protocol_error = AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE;
            SetFailure(&result,
                       V1TransportConfigurationDisposition::ProtocolFailure,
                       V1TransportSilenceStage::Suspend);
            return Finish(backend, source, result);
        }
        result.avdtp_suspend_accepted = true;
        result.remote_stream_cleanup_required = false;
    }
    if (!Exchange(backend, options, &avdtp, avdtp_source_close(&avdtp),
                  V1TransportSilenceStage::Close, stop_probe,
                  stop_context, true, &result, &next)) {
        return Finish(backend, source, result);
    }
    if (next.kind != AVDTP_ACTION_SESSION_CLOSED) {
        result.protocol_error = AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE;
        SetFailure(&result,
                   V1TransportConfigurationDisposition::ProtocolFailure,
                   V1TransportSilenceStage::Close);
        return Finish(backend, source, result);
    }
    result.avdtp_close_accepted = true;
    result.primary_disposition = result.completed_full_duration
        ? V1TransportConfigurationDisposition::Succeeded
        : V1TransportConfigurationDisposition::Cancelled;
    result.disposition = result.primary_disposition;
    return Finish(backend, source, result);
}

}  // namespace native_ldac::agent
