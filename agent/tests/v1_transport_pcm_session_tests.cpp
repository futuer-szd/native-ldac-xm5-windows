// SPDX-License-Identifier: Apache-2.0
#include "v1_transport_pcm_session.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "ldac_native/avdtp.h"

namespace {

int failures = 0;

native_ldac::agent::V1TransportPcmOptions ContinuousFidelityOptions();
constexpr std::uint32_t kRetryableOpenError = 71u;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", \
    __LINE__, #x); ++failures; } } while (0)

class Backend final : public native_ldac::agent::V1TransportSilenceBackend {
public:
    bool OpenSignaling(std::uint32_t, std::uint32_t* error) override {
        if (open_error != 0u) {
            *error = open_error;
            return false;
        }
        *error = 0u;
        return true;
    }
    bool GetLastOpenDiagnostics(
        native_ldac::agent::V1TransportOpenDiagnostics* diagnostics)
            const override {
        if (diagnostics == nullptr) {
            return false;
        }
        *diagnostics = open_diagnostics;
        return open_diagnostics.available;
    }
    bool ExchangeSignaling(const std::uint8_t* request,
                           std::size_t request_size,
                           std::uint8_t* response,
                           std::size_t capacity,
                           std::size_t* response_size,
                           std::uint32_t,
                           std::uint32_t* error) override {
        avdtp_header header{};
        if (avdtp_parse_header(request, request_size, &header) != AVDTP_OK) {
            *error = 1u;
            return false;
        }
        signals.push_back(header.signal_id);
        signal_write_attempts.push_back(write_attempts);
        if (header.signal_id == AVDTP_SIGNAL_SUSPEND) {
            suspend_command_received = true;
        }
        if (peer_discover_before_first_response && !peer_command_sent) {
            pending_transaction_label = header.transaction_label;
            pending_signal_id = header.signal_id;
            const auto written = avdtp_write_single(
                response, capacity, 1u, AVDTP_MESSAGE_COMMAND,
                AVDTP_SIGNAL_DISCOVER, nullptr, 0u);
            peer_command_sent = true;
            *response_size = written;
            *error = 0u;
            return written != 0u;
        }
        if (peer_discover_before_first_response &&
            peer_command_sent && !peer_response_received) {
            if (header.transaction_label != 1u ||
                header.message_type != AVDTP_MESSAGE_ACCEPT ||
                header.signal_id != AVDTP_SIGNAL_DISCOVER ||
                request_size != 4u || request[2] != 0x06u ||
                request[3] != 0x00u) {
                *error = 2u;
                return false;
            }
            peer_response_received = true;
            if (repeat_peer_discover) {
                const auto written = avdtp_write_single(
                    response, capacity, 2u, AVDTP_MESSAGE_COMMAND,
                    AVDTP_SIGNAL_DISCOVER, nullptr, 0u);
                *response_size = written;
                *error = 0u;
                return written != 0u;
            }
            header.transaction_label = pending_transaction_label;
            header.message_type = AVDTP_MESSAGE_COMMAND;
            header.signal_id = pending_signal_id;
        }
        if (peer_capabilities_after_discover && peer_response_received &&
            !peer_capability_command_sent &&
            header.message_type == AVDTP_MESSAGE_COMMAND &&
            header.signal_id == AVDTP_SIGNAL_GET_ALL_CAPABILITIES) {
            pending_capability_transaction_label = header.transaction_label;
            const std::uint8_t seid = 0x04u;
            const auto written = avdtp_write_single(
                response, capacity, header.transaction_label,
                AVDTP_MESSAGE_COMMAND,
                AVDTP_SIGNAL_GET_ALL_CAPABILITIES, &seid, 1u);
            peer_capability_command_sent = true;
            *response_size = written;
            *error = 0u;
            return written != 0u;
        }
        if (peer_capabilities_after_discover &&
            peer_capability_command_sent &&
            !peer_capability_response_received) {
            if (header.transaction_label !=
                    pending_capability_transaction_label ||
                header.message_type != AVDTP_MESSAGE_ACCEPT ||
                header.signal_id != AVDTP_SIGNAL_GET_ALL_CAPABILITIES ||
                request_size != 16u || request[2] != 0x01u ||
                request[3] != 0x00u || request[4] != 0x07u ||
                request[5] != 0x0Au || request[6] != 0x00u ||
                request[7] != 0xFFu || request[14] != LDAC_SF_ALL ||
                request[15] != LDAC_CM_STEREO) {
                *error = 3u;
                return false;
            }
            peer_capability_response_received = true;
            if (peer_set_configuration_after_capabilities) {
                std::array<std::uint8_t, 16u> configuration{};
                const ldac_configuration selected = {
                    LDAC_SF_44100, LDAC_CM_STEREO,
                };
                const auto configuration_size =
                    ldac_build_set_configuration_payload(
                        configuration.data(), configuration.size(),
                        1u, 3u, selected);
                if (configuration_size != configuration.size()) {
                    *error = 4u;
                    return false;
                }
                if (malformed_peer_set_configuration) {
                    configuration[0] = 0x08u;
                }
                const auto written = avdtp_write_single(
                    response, capacity, 2u, AVDTP_MESSAGE_COMMAND,
                    AVDTP_SIGNAL_SET_CONFIGURATION,
                    configuration.data(), configuration.size());
                peer_set_configuration_command_sent = true;
                *response_size = written;
                *error = 0u;
                return written != 0u;
            }
            header.message_type = AVDTP_MESSAGE_COMMAND;
        }
        if (peer_set_configuration_after_capabilities &&
            peer_set_configuration_command_sent &&
            !peer_set_configuration_response_received) {
            if (header.transaction_label != 2u ||
                header.message_type != AVDTP_MESSAGE_REJECT ||
                header.signal_id != AVDTP_SIGNAL_SET_CONFIGURATION ||
                request_size != 4u || request[2] != 0x00u ||
                request[3] != 0x13u) {
                *error = 5u;
                return false;
            }
            peer_set_configuration_response_received = true;
            if (repeat_peer_set_configuration) {
                std::array<std::uint8_t, 16u> configuration{};
                const ldac_configuration selected = {
                    LDAC_SF_44100, LDAC_CM_STEREO,
                };
                const auto configuration_size =
                    ldac_build_set_configuration_payload(
                        configuration.data(), configuration.size(),
                        1u, 3u, selected);
                const auto written = avdtp_write_single(
                    response, capacity, 3u, AVDTP_MESSAGE_COMMAND,
                    AVDTP_SIGNAL_SET_CONFIGURATION,
                    configuration.data(), configuration_size);
                *response_size = written;
                *error = 0u;
                return written != 0u;
            }
            header.transaction_label = pending_capability_transaction_label;
            header.message_type = AVDTP_MESSAGE_COMMAND;
            header.signal_id = AVDTP_SIGNAL_GET_ALL_CAPABILITIES;
        }
        std::array<std::uint8_t, 32u> payload{};
        std::size_t size = 0u;
        if (header.signal_id == AVDTP_SIGNAL_DISCOVER) {
            payload[0] = 0x0Cu;
            payload[1] = 0x08u;
            size = 2u;
        } else if (header.signal_id == AVDTP_SIGNAL_GET_ALL_CAPABILITIES) {
            const std::uint8_t caps[] = {
                AVDTP_SERVICE_MEDIA_TRANSPORT, 0x00u,
                AVDTP_SERVICE_MEDIA_CODEC, 0x0Au,
                0x00u, AVDTP_CODEC_VENDOR,
                0x2Du, 0x01u, 0x00u, 0x00u,
                0xAAu, 0x00u, 0x3Cu, 0x07u,
            };
            std::memcpy(payload.data(), caps, sizeof(caps));
            size = sizeof(caps);
        }
        const auto written = avdtp_write_single(
            response, capacity,
            mismatched_first_response && signals.size() == 1u
                ? static_cast<std::uint8_t>(
                    (header.transaction_label + 1u) & 0x0Fu)
                : header.transaction_label,
            AVDTP_MESSAGE_ACCEPT, header.signal_id,
            payload.data(), size);
        *response_size = written;
        *error = 0u;
        return written != 0u;
    }
    bool OpenMedia(std::uint32_t, std::uint16_t preferred,
                   std::uint16_t* incoming, std::uint16_t* outgoing,
                   std::uint32_t* error) override {
        *incoming = preferred;
        *outgoing = 895u;
        *error = 0u;
        return true;
    }
    bool WriteMedia(const std::uint8_t* packet, std::size_t size,
                    std::uint32_t, std::uint32_t* error) override {
        ++write_attempts;
        if (not_ready_write_attempts_remaining != 0u &&
            write_attempts >= not_ready_write_attempt_start) {
            --not_ready_write_attempts_remaining;
            *error = 21u;
            return false;
        }
        if (fail_write_attempt != 0u &&
            write_attempts == fail_write_attempt) {
            *error = 29u;
            return false;
        }
        if (packet == nullptr || size < 14u || size > 895u) {
            *error = 2u;
            return false;
        }
        packets.emplace_back(packet, packet + size);
        *error = 0u;
        return true;
    }
    bool BeginPeerSignalingRead(std::uint32_t,
                                std::uint32_t* error) override {
        if (peer_signaling_read_active) {
            *error = 1u;
            return false;
        }
        peer_signaling_read_active = true;
        ++peer_signaling_read_begin_count;
        *error = 0u;
        return true;
    }
    native_ldac::agent::V1TransportSignalingReadDisposition
    PollPeerSignalingRead(std::uint8_t* packet,
                          std::size_t packet_capacity,
                          std::size_t* packet_size,
                          std::uint32_t* error) override {
        if (!peer_signaling_read_active || packet == nullptr ||
            packet_size == nullptr) {
            *error = 1u;
            return native_ldac::agent::
                V1TransportSignalingReadDisposition::Failure;
        }
        *packet_size = 0u;
        if (peer_signaling_read_timeout_once &&
            !peer_signaling_read_timeout_returned) {
            peer_signaling_read_timeout_returned = true;
            peer_signaling_read_active = false;
            *error = 0u;
            return native_ldac::agent::
                V1TransportSignalingReadDisposition::TimedOut;
        }
        const bool close_after_write =
            peer_close_after_write_attempt != 0u &&
            write_attempts >= peer_close_after_write_attempt;
        const bool close_after_suspend =
            peer_close_after_suspend && suspend_command_received;
        if ((!close_after_write && !close_after_suspend) ||
            peer_close_command_sent) {
            *error = 0u;
            return native_ldac::agent::
                V1TransportSignalingReadDisposition::NoPacket;
        }
        const std::uint8_t seid = malformed_peer_close ? 0x08u : 0x04u;
        const auto written = avdtp_write_single(
            packet, packet_capacity, 9u, AVDTP_MESSAGE_COMMAND,
            AVDTP_SIGNAL_CLOSE, &seid, 1u);
        if (written == 0u) {
            *error = 2u;
            return native_ldac::agent::
                V1TransportSignalingReadDisposition::Failure;
        }
        *packet_size = written;
        peer_close_command_sent = true;
        peer_signaling_read_active = false;
        *error = 0u;
        return native_ldac::agent::
            V1TransportSignalingReadDisposition::Packet;
    }
    bool SendPeerSignalingResponse(const std::uint8_t* packet,
                                   std::size_t packet_size,
                                   std::uint32_t,
                                   std::uint32_t* error) override {
        avdtp_header header{};
        if (avdtp_parse_header(packet, packet_size, &header) != AVDTP_OK ||
            header.transaction_label != 9u ||
            header.message_type != AVDTP_MESSAGE_ACCEPT ||
            header.signal_id != AVDTP_SIGNAL_CLOSE ||
            header.payload_offset != packet_size) {
            *error = 3u;
            return false;
        }
        peer_close_response_sent = true;
        *error = 0u;
        return true;
    }
    bool CancelPeerSignalingRead(std::uint32_t* error) override {
        peer_signaling_read_active = false;
        ++peer_signaling_read_cancel_count;
        *error = 0u;
        return true;
    }
    bool CloseSignaling(std::uint32_t* error) override {
        peer_signaling_read_active = false;
        *error = 0u;
        return true;
    }

    std::vector<std::uint8_t> signals;
    std::vector<std::size_t> signal_write_attempts;
    std::vector<std::vector<std::uint8_t>> packets;
    std::uint32_t open_error = 0u;
    native_ldac::agent::V1TransportOpenDiagnostics open_diagnostics = {};
    std::size_t write_attempts = 0u;
    std::size_t fail_write_attempt = 0u;
    std::size_t not_ready_write_attempt_start = 0u;
    std::size_t not_ready_write_attempts_remaining = 0u;
    bool mismatched_first_response = false;
    bool peer_discover_before_first_response = false;
    bool peer_command_sent = false;
    bool peer_response_received = false;
    bool repeat_peer_discover = false;
    bool peer_capabilities_after_discover = false;
    bool peer_capability_command_sent = false;
    bool peer_capability_response_received = false;
    bool peer_set_configuration_after_capabilities = false;
    bool peer_set_configuration_command_sent = false;
    bool peer_set_configuration_response_received = false;
    bool malformed_peer_set_configuration = false;
    bool repeat_peer_set_configuration = false;
    bool peer_signaling_read_active = false;
    bool peer_signaling_read_timeout_once = false;
    bool peer_signaling_read_timeout_returned = false;
    bool peer_close_command_sent = false;
    bool peer_close_response_sent = false;
    bool malformed_peer_close = false;
    bool peer_close_after_suspend = false;
    bool suspend_command_received = false;
    std::size_t peer_close_after_write_attempt = 0u;
    std::size_t peer_signaling_read_begin_count = 0u;
    std::size_t peer_signaling_read_cancel_count = 0u;
    std::uint8_t pending_transaction_label = 0u;
    std::uint8_t pending_signal_id = 0u;
    std::uint8_t pending_capability_transaction_label = 0u;
};

class Source final : public native_ldac::agent::V1TransportPcmSource {
public:
    bool Prepare(native_ldac::agent::V1TransportPcmFormat* format,
                 std::uint32_t,
                 std::uint32_t* error) override {
        ++prepare_count;
        if (prepare_count > 1u &&
            pause_prepare_timeouts_remaining != 0u) {
            --pause_prepare_timeouts_remaining;
            *error = 258u;
            return false;
        }
        if (prepare_count > 1u && fail_rebind_prepare) {
            *error = fail_rebind_error;
            return false;
        }
        format->sample_rate_hz = prepare_count > 1u &&
                rebound_sample_rate_hz != 0u
            ? rebound_sample_rate_hz
            : sample_rate_hz;
        format->bits_per_sample = prepare_count > 1u &&
                rebound_bits_per_sample != 0u
            ? rebound_bits_per_sample
            : 16u;
        format->stream_epoch = 3u + prepare_count;
        format->volume_control_available = true;
        format->muted = prepare_count > 1u && mute_after_rebind;
        format->volume_scalar = prepare_count > 1u &&
                change_volume_after_rebind
            ? 0.5f
            : 1.0f;
        format->volume_db = format->volume_scalar == 1.0f ? 0.0f : -6.0f;
        *error = 0u;
        return true;
    }
    native_ldac::agent::V1TransportPcmReadDisposition ReadFrames(
        float* pcm, std::size_t frames, std::uint32_t,
        std::size_t* frames_read, std::uint32_t* error) override {
        ++read_count;
        if (pcm_timeout_after_reads != 0u &&
            read_count >= pcm_timeout_after_reads &&
            pcm_timeouts_remaining != 0u) {
            --pcm_timeouts_remaining;
            *frames_read = 0u;
            *error = 258u;
            return native_ldac::agent::
                V1TransportPcmReadDisposition::Timeout;
        }
        if (stream_stop_index < stream_stop_reads.size() &&
            read_count >= stream_stop_reads[stream_stop_index]) {
            ++stream_stop_index;
            *frames_read = 0u;
            *error = 232u;
            return native_ldac::agent::
                V1TransportPcmReadDisposition::StreamStopped;
        }
        if (stop_once_after_reads != 0u && !stopped_once &&
            read_count >= stop_once_after_reads) {
            stopped_once = true;
            *frames_read = 0u;
            *error = 232u;
            return native_ldac::agent::
                V1TransportPcmReadDisposition::StreamStopped;
        }
        for (std::size_t index = 0u; index < frames * 2u; ++index) {
            if (quiet || read_count <= quiet_reads_before_audio) {
                pcm[index] = 0.0f;
            } else if (non_finite_samples) {
                const float values[] = {
                    0.5f,
                    std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity(),
                };
                pcm[index] = values[index % 4u];
            } else if (quiet_until_restart && prepare_count == 1u) {
                pcm[index] = 0.0f;
            } else {
                pcm[index] = sample;
            }
        }
        *frames_read = frames;
        *error = 0u;
        return native_ldac::agent::V1TransportPcmReadDisposition::Data;
    }
    bool QueryFormat(native_ldac::agent::V1TransportPcmFormat* format,
                     std::uint32_t* error) override {
        ++query_count;
        format->sample_rate_hz = prepare_count > 1u &&
                rebound_sample_rate_hz != 0u
            ? rebound_sample_rate_hz
            : sample_rate_hz;
        format->bits_per_sample = prepare_count > 1u &&
                rebound_bits_per_sample != 0u
            ? rebound_bits_per_sample
            : 16u;
        format->stream_epoch = 3u + prepare_count +
            (prepare_count == 1u && change_epoch_after_query != 0u &&
                     query_count >= change_epoch_after_query
                 ? 1u
                 : 0u);
        format->volume_control_available = true;
        format->muted = (change_mute_after_query != 0u &&
                query_count >= change_mute_after_query) ||
            (prepare_count > 1u && mute_after_rebind);
        format->volume_scalar =
            (change_volume_after_query != 0u &&
                    query_count >= change_volume_after_query
                ? 0.5f
                : (prepare_count > 1u && change_volume_after_rebind
                    ? 0.5f
                    : 1.0f));
        format->volume_db = format->volume_scalar == 1.0f ? 0.0f : -6.0f;
        *error = 0u;
        return true;
    }
    bool QuerySnapshot(native_ldac::agent::V1TransportPcmSnapshot* snapshot,
                       std::uint32_t* error) override {
        if (snapshot == nullptr) {
            *error = 87u;
            return false;
        }
        if (!QueryFormat(&snapshot->format, error)) return false;
        snapshot->available_bytes = snapshot_available_bytes;
        snapshot->capacity_bytes = snapshot_capacity_bytes;
        snapshot->stream_active = snapshot_stream_active ||
            (snapshot_active_after_query != 0u &&
             query_count >= snapshot_active_after_query);
        snapshot->discontinuity = snapshot_discontinuity;
        snapshot->total_bytes_written = snapshot_total_bytes_written;
        snapshot->total_bytes_read = snapshot_total_bytes_read;
        snapshot->total_bytes_dropped = snapshot_total_bytes_dropped;
        return true;
    }
    bool WaitUntilSample(std::uint64_t offset, unsigned,
                         std::uint32_t* error) override {
        waits.push_back(offset);
        *error = 0u;
        return true;
    }
    bool ResetPacing(std::uint32_t* error) override {
        ++reset_pacing_count;
        *error = 0u;
        return true;
    }
    bool Release(std::uint32_t* error) override {
        ++release_count;
        *error = 0u;
        return true;
    }
    bool quiet = false;
    bool non_finite_samples = false;
    bool quiet_until_restart = false;
    bool stopped_once = false;
    float sample = 0.5f;
    unsigned sample_rate_hz = 48000u;
    unsigned rebound_sample_rate_hz = 0u;
    unsigned rebound_bits_per_sample = 0u;
    unsigned stop_once_after_reads = 0u;
    unsigned quiet_reads_before_audio = 0u;
    unsigned prepare_count = 0u;
    unsigned pause_prepare_timeouts_remaining = 0u;
    unsigned pcm_timeout_after_reads = 0u;
    unsigned pcm_timeouts_remaining = 0u;
    unsigned reset_pacing_count = 0u;
    std::vector<unsigned> stream_stop_reads;
    std::size_t stream_stop_index = 0u;
    unsigned read_count = 0u;
    unsigned release_count = 0u;
    unsigned query_count = 0u;
    unsigned change_volume_after_query = 0u;
    unsigned change_mute_after_query = 0u;
    unsigned change_epoch_after_query = 0u;
    bool change_volume_after_rebind = false;
    bool mute_after_rebind = false;
    bool fail_rebind_prepare = false;
    std::uint32_t fail_rebind_error = 258u;
    bool snapshot_stream_active = false;
    unsigned snapshot_active_after_query = 0u;
    bool snapshot_discontinuity = false;
    unsigned snapshot_available_bytes = 0u;
    unsigned snapshot_capacity_bytes = 0u;
    std::uint64_t snapshot_total_bytes_written = 0u;
    std::uint64_t snapshot_total_bytes_read = 0u;
    std::uint64_t snapshot_total_bytes_dropped = 0u;
    std::vector<std::uint64_t> waits;
};

struct SourceWaitStopContext {
    Source* source = nullptr;
    native_ldac::agent::V1TransportPcmStopDisposition disposition =
        native_ldac::agent::V1TransportPcmStopDisposition::None;
};

struct SourcePrepareStopContext {
    Source* source = nullptr;
    unsigned prepare_count = 0u;
    native_ldac::agent::V1TransportPcmStopDisposition disposition =
        native_ldac::agent::V1TransportPcmStopDisposition::None;
};

native_ldac::agent::V1TransportPcmStopDisposition StopAfterSourcePrepares(
    void* context) {
    auto* value = static_cast<SourcePrepareStopContext*>(context);
    return value != nullptr && value->source != nullptr &&
            value->source->prepare_count >= value->prepare_count
        ? value->disposition
        : native_ldac::agent::V1TransportPcmStopDisposition::None;
}

native_ldac::agent::V1TransportPcmStopDisposition StopAfterSourceWait(
    void* context) {
    auto* value = static_cast<SourceWaitStopContext*>(context);
    return value != nullptr && value->source != nullptr &&
            !value->source->waits.empty()
        ? value->disposition
        : native_ldac::agent::V1TransportPcmStopDisposition::None;
}

struct SourceStreamStopContext {
    Source* source = nullptr;
    native_ldac::agent::V1TransportPcmStopDisposition disposition =
        native_ldac::agent::V1TransportPcmStopDisposition::None;
    unsigned int probes_before_stop = 0u;
    unsigned int probe_count = 0u;
};

native_ldac::agent::V1TransportPcmStopDisposition StopAfterSourceStreamStop(
    void* context) {
    auto* value = static_cast<SourceStreamStopContext*>(context);
    if (value == nullptr || value->source == nullptr ||
        !value->source->stopped_once) {
        return native_ldac::agent::V1TransportPcmStopDisposition::None;
    }
    ++value->probe_count;
    return value->probe_count > value->probes_before_stop
        ? value->disposition
        : native_ldac::agent::V1TransportPcmStopDisposition::None;
}

struct StopContext {
    Backend* backend = nullptr;
    std::size_t after_packets = 0u;
    native_ldac::agent::V1TransportPcmStopDisposition disposition =
        native_ldac::agent::V1TransportPcmStopDisposition::None;
};

native_ldac::agent::V1TransportPcmStopDisposition Stop(void* context) {
    auto* value = static_cast<StopContext*>(context);
    return value != nullptr && value->backend->packets.size() >=
            value->after_packets
        ? value->disposition
        : native_ldac::agent::V1TransportPcmStopDisposition::None;
}

struct SourceReadStopContext {
    Source* source = nullptr;
    unsigned after_reads = 0u;
    native_ldac::agent::V1TransportPcmStopDisposition disposition =
        native_ldac::agent::V1TransportPcmStopDisposition::None;
};

native_ldac::agent::V1TransportPcmStopDisposition StopAfterSourceReads(
    void* context) {
    auto* value = static_cast<SourceReadStopContext*>(context);
    return value != nullptr && value->source != nullptr &&
            value->source->read_count >= value->after_reads
        ? value->disposition
        : native_ldac::agent::V1TransportPcmStopDisposition::None;
}

bool NotifyStarted(void* context, std::uint32_t* error) {
    *static_cast<bool*>(context) = true;
    *error = 0u;
    return true;
}

void Happy() {
    Backend backend;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 100u;
    bool notified = false;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, nullptr, nullptr,
        NotifyStarted, &notified);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(notified);
    CHECK(result.media_started_notified);
    CHECK(result.audible_pcm_confirmed_before_open);
    CHECK(result.completed_full_duration);
    CHECK(result.actual_duration_ms >= 100u);
    CHECK(result.actual_duration_ms <= 110u);
    CHECK(result.maximum_gain_scalar == 0.01f);
    CHECK(result.maximum_pre_gain_peak == 0.5f);
    CHECK(result.maximum_unlimited_post_gain_peak <= 0.0051f);
    CHECK(result.maximum_post_gain_peak <= 0.0051f);
    CHECK(result.limited_output_samples == 0u);
    CHECK(result.limiter_mode == native_ldac::agent::
        V1TransportPcmLimiterMode::HardClip);
    CHECK(result.limiter_blocks_processed == 0u);
    CHECK(result.limiter_fallback_clamp_count == 0u);
    CHECK(result.pcm_frames_read > 0u);
    CHECK(result.pcm_frames_sent >= 4800u);
    CHECK(result.pre_start_pcm_frames_discarded == 0u);
    CHECK(result.media_packets_written > 4u);
    CHECK(result.media_packets_written == result.pacing_waits);
    CHECK(result.avdtp_start_accepted);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
    CHECK(!result.remote_stream_cleanup_required);
    CHECK(result.consumer_lease_released);
    CHECK(source.prepare_count == 1u);
    CHECK(source.release_count == 1u);
    CHECK(backend.signals.back() == AVDTP_SIGNAL_CLOSE);
}

void RequestedChannelModesReachNegotiation() {
    for (const auto mode : {
            LDAC_ENCODER_CHANNEL_DUAL,
            LDAC_ENCODER_CHANNEL_MONO}) {
        Backend backend;
        Source source;
        native_ldac::agent::V1TransportPcmOptions options;
        options.duration_ms = 100u;
        options.channel_mode = mode;
        const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
            &backend, &source, options);
        const auto expected = mode == LDAC_ENCODER_CHANNEL_DUAL
            ? LDAC_CM_DUAL
            : LDAC_CM_MONO;
        CHECK(result.disposition == native_ldac::agent::
            V1TransportConfigurationDisposition::Succeeded);
        CHECK(result.configuration.channel_mode == expected);
        CHECK(result.media_packets_written > 0u);
        CHECK(result.consumer_lease_released);
    }
}

void PreStartSilentFramesAreTrackedSeparately() {
    Backend backend;
    Source source;
    source.quiet_reads_before_audio = 3u;
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 100u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.audible_pcm_confirmed_before_open);
    CHECK(result.pre_start_pcm_frames_discarded == 3u * 128u);
    CHECK(result.pcm_frames_read - result.pcm_frames_sent ==
          result.pre_start_pcm_frames_discarded);
    CHECK(result.media_packets_written > 4u);
    CHECK(result.consumer_lease_released);
}

void InvalidGainNeverPrepares() {
    Backend backend;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    options.maximum_gain_scalar = 1.01f;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::InvalidConfiguration);
    CHECK(source.prepare_count == 0u);
    CHECK(backend.signals.empty());
}

void InvalidOutputPeakNeverPrepares() {
    for (const float value : {0.0f, -0.1f, 0.251f}) {
        Backend backend;
        Source source;
        native_ldac::agent::V1TransportPcmOptions options;
        options.maximum_output_peak = value;
        const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
            &backend, &source, options);
        CHECK(result.disposition == native_ldac::agent::
            V1TransportConfigurationDisposition::InvalidConfiguration);
        CHECK(source.prepare_count == 0u);
        CHECK(backend.signals.empty());
    }
}

void InvalidPreflightBoundNeverPrepares() {
    Backend backend;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    options.audible_preflight_timeout_ms = 120001u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::InvalidConfiguration);
    CHECK(source.prepare_count == 0u);
    CHECK(backend.signals.empty());
}

void InvalidLinkedLimiterConfigurationNeverPrepares() {
    for (const bool invalid_mode : {false, true}) {
        Backend backend;
        Source source;
        native_ldac::agent::V1TransportPcmOptions options;
        if (invalid_mode) {
            options.limiter_mode = static_cast<
                native_ldac::agent::V1TransportPcmLimiterMode>(99u);
        } else {
            options.limiter_mode = native_ldac::agent::
                V1TransportPcmLimiterMode::LinkedStereoBlock;
            options.limiter_release_ms = 0.0f;
        }
        const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
            &backend, &source, options);
        CHECK(result.disposition == native_ldac::agent::
            V1TransportConfigurationDisposition::InvalidConfiguration);
        CHECK(source.prepare_count == 0u);
        CHECK(backend.signals.empty());
    }
}

void InvalidLongProfileBoundsNeverPrepare() {
    for (const bool duration_bound : {false, true}) {
        Backend backend;
        Source source;
        native_ldac::agent::V1TransportPcmOptions options;
        if (duration_bound) {
            options.duration_ms = 60001u;
        } else {
            options.maximum_packets = 32769u;
        }
        const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
            &backend, &source, options);
        CHECK(result.disposition == native_ldac::agent::
            V1TransportConfigurationDisposition::InvalidConfiguration);
        CHECK(source.prepare_count == 0u);
        CHECK(backend.signals.empty());
    }
}

void LongProfileBoundsAreAccepted() {
    Backend backend;
    backend.open_error = kRetryableOpenError;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 60000u;
    options.maximum_packets = 32768u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::BackendFailure);
    CHECK(result.stage == native_ldac::agent::
        V1TransportSilenceStage::OpenSignaling);
    CHECK(result.target_duration_ms == 60000u);
    CHECK(source.prepare_count == 1u);
    CHECK(source.release_count == 1u);
}

void InvalidContinuousProfileBoundsNeverPrepare() {
    for (const unsigned int invalid_case : {0u, 1u, 2u, 3u, 4u}) {
        Backend backend;
        Source source;
        native_ldac::agent::V1TransportPcmOptions options;
        StopContext stop{&backend, 2u,
            native_ldac::agent::V1TransportPcmStopDisposition::Graceful};
        native_ldac::agent::V1TransportPcmStopProbe stop_probe = Stop;
        if (invalid_case == 0u) {
            options.duration_ms = 0u;
        } else if (invalid_case == 1u) {
            options.maximum_packets = 0u;
        } else {
            options.continuous_until_stop = true;
            if (invalid_case == 2u) {
                options.maximum_packets = 0u;
            } else if (invalid_case == 3u) {
                options.duration_ms = 0u;
            } else {
                options.duration_ms = 0u;
                options.maximum_packets = 0u;
                stop_probe = nullptr;
            }
        }
        const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
            &backend, &source, options, stop_probe, &stop);
        CHECK(result.disposition == native_ldac::agent::
            V1TransportConfigurationDisposition::InvalidConfiguration);
        CHECK(source.prepare_count == 0u);
        CHECK(backend.signals.empty());
    }
}

void InvalidStopClassificationBoundNeverPrepares() {
    Backend backend;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    options.post_start_stop_classification_timeout_ms = 30001u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::InvalidConfiguration);
    CHECK(source.prepare_count == 0u);
    CHECK(backend.signals.empty());
}

void InvalidPcmTimeoutToleranceNeverPrepares() {
    Backend backend;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    options.pcm_timeout_tolerance_ms = 5001u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::InvalidConfiguration);
    CHECK(source.prepare_count == 0u);
    CHECK(backend.signals.empty());
}

void OpenFailureDiagnosticsArePreserved() {
    Backend backend;
    backend.open_error = kRetryableOpenError;
    backend.open_diagnostics.available = true;
    backend.open_diagnostics.query_attempts = 1u;
    backend.open_diagnostics.query_error = 0u;
    backend.open_diagnostics.query_bytes = 48u;
    backend.open_diagnostics.remote_response_valid = true;
    backend.open_diagnostics.sequence = 17u;
    backend.open_diagnostics.operation = 1u;
    backend.open_diagnostics.io_status = -1073741616;
    backend.open_diagnostics.brb_status = -1073741616;
    backend.open_diagnostics.bluetooth_status = 0xFFu;
    backend.open_diagnostics.remote_bluetooth_address =
        0x00001122334455ull;
    backend.open_diagnostics.channel_flags = 0x00060000u;
    backend.open_diagnostics.flags = 0x0Bu;
    backend.open_diagnostics.psm = 0x0019u;
    backend.open_diagnostics.response = 4u;
    backend.open_diagnostics.response_status = 0u;
    Source source;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, native_ldac::agent::V1TransportPcmOptions{});
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::BackendFailure);
    CHECK(result.stage == native_ldac::agent::
        V1TransportSilenceStage::OpenSignaling);
    CHECK(result.open_diagnostics.available);
    CHECK(result.open_diagnostics.query_attempts == 1u);
    CHECK(result.open_diagnostics.query_error == 0u);
    CHECK(result.open_diagnostics.query_bytes == 48u);
    CHECK(result.open_diagnostics.remote_response_valid);
    CHECK(result.open_diagnostics.sequence == 17u);
    CHECK(result.open_diagnostics.operation == 1u);
    CHECK(result.open_diagnostics.remote_bluetooth_address ==
        0x00001122334455ull);
    CHECK(result.open_diagnostics.channel_flags == 0x00060000u);
    CHECK(result.open_diagnostics.psm == 0x0019u);
    CHECK(result.open_diagnostics.response == 4u);
    CHECK(result.open_diagnostics.response_status == 0u);
    CHECK(native_ldac::agent::
        IsV1StrictlyRetryableRemoteNoResources(result));
    auto permanent = result;
    permanent.open_diagnostics.response = 2u;
    CHECK(!native_ldac::agent::
        IsV1StrictlyRetryableRemoteNoResources(permanent));
    CHECK(source.release_count == 1u);

    Backend unavailable;
    unavailable.open_error = kRetryableOpenError;
    unavailable.open_diagnostics.query_attempts = 2u;
    unavailable.open_diagnostics.query_error = 13u;
    unavailable.open_diagnostics.query_bytes = 48u;
    const auto unavailable_result =
        native_ldac::agent::RunV1TransportPcmBurstOnce(
            &unavailable, &source,
            native_ldac::agent::V1TransportPcmOptions{});
    CHECK(!unavailable_result.open_diagnostics.available);
    CHECK(unavailable_result.open_diagnostics.query_attempts == 2u);
    CHECK(unavailable_result.open_diagnostics.query_error == 13u);
    CHECK(unavailable_result.open_diagnostics.query_bytes == 48u);
}

void SuccessfulOpenDiagnosticsArePreserved() {
    Backend backend;
    backend.open_diagnostics.available = true;
    backend.open_diagnostics.query_attempts = 1u;
    backend.open_diagnostics.query_error = 0u;
    backend.open_diagnostics.query_bytes = 48u;
    backend.open_diagnostics.sequence = 19u;
    backend.open_diagnostics.operation = 1u;
    backend.open_diagnostics.remote_bluetooth_address =
        0x00001122334455ull;
    backend.open_diagnostics.channel_flags = 0x00060000u;
    backend.open_diagnostics.flags = 0x17u;
    backend.open_diagnostics.psm = 0x0019u;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 100u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.open_diagnostics.available);
    CHECK(result.open_diagnostics.query_attempts == 1u);
    CHECK(result.open_diagnostics.query_error == 0u);
    CHECK(result.open_diagnostics.query_bytes == 48u);
    CHECK(result.open_diagnostics.sequence == 19u);
    CHECK(result.open_diagnostics.operation == 1u);
    CHECK(result.open_diagnostics.remote_bluetooth_address ==
        0x00001122334455ull);
    CHECK(result.open_diagnostics.channel_flags == 0x00060000u);
    CHECK(result.open_diagnostics.flags == 0x17u);
    CHECK(result.open_diagnostics.psm == 0x0019u);
    CHECK(!result.open_diagnostics.remote_response_valid);
}

void PreflightReacquiresAcrossEpoch() {
    Backend backend;
    backend.open_error = kRetryableOpenError;
    Source source;
    source.quiet_until_restart = true;
    source.stop_once_after_reads = 2u;
    native_ldac::agent::V1TransportPcmOptions options;
    options.audible_preflight_timeout_ms = 1000u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::BackendFailure);
    CHECK(result.stage == native_ldac::agent::
        V1TransportSilenceStage::OpenSignaling);
    CHECK(result.audible_pcm_confirmed_before_open);
    CHECK(result.pcm_prepare_attempts == 2u);
    CHECK(result.pcm_epoch_restarts == 1u);
    CHECK(result.consumer_lease_acquire_count == 2u);
    CHECK(result.consumer_lease_release_count == 2u);
    CHECK(result.pcm_format.stream_epoch == 5u);
    CHECK(source.prepare_count == 2u);
    CHECK(source.release_count == 2u);
    CHECK(result.consumer_lease_released);
}

void AudibleProfileIsStillHardBounded() {
    Backend backend;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 50u;
    options.maximum_gain_scalar = 0.25f;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.maximum_gain_scalar == 0.25f);
    CHECK(result.maximum_output_peak_ceiling == 0.25f);
    CHECK(result.maximum_unlimited_post_gain_peak <= 0.1251f);
    CHECK(result.maximum_post_gain_peak <= 0.1251f);
    CHECK(result.limited_output_samples == 0u);
    CHECK(result.completed_full_duration);
    CHECK(result.consumer_lease_released);
}

void UnityGainPreservesLowInput() {
    Backend backend;
    Source source;
    source.sample = 0.04f;
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 50u;
    options.maximum_gain_scalar = 1.0f;
    options.maximum_output_peak = 0.25f;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.maximum_gain_scalar == 1.0f);
    CHECK(result.maximum_output_peak_ceiling == 0.25f);
    CHECK(std::fabs(result.maximum_pre_gain_peak - 0.04f) < 0.00001f);
    CHECK(std::fabs(result.maximum_unlimited_post_gain_peak - 0.04f) <
        0.00001f);
    CHECK(std::fabs(result.maximum_post_gain_peak - 0.04f) < 0.00001f);
    CHECK(result.limited_output_samples == 0u);
    CHECK(result.consumer_lease_released);
}

void UnityGainLimiterCapsFullScaleAndSanitizesNonFinite() {
    Backend backend;
    Source source;
    source.non_finite_samples = true;
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 50u;
    options.maximum_gain_scalar = 1.0f;
    options.maximum_output_peak = 0.25f;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.maximum_pre_gain_peak == 0.5f);
    CHECK(result.maximum_unlimited_post_gain_peak == 0.5f);
    CHECK(result.maximum_post_gain_peak == 0.25f);
    CHECK(result.limited_output_samples > 0u);
    CHECK(result.consumer_lease_released);
}

void LinkedLimiterReportsBoundedStereoReduction() {
    Backend backend;
    Source source;
    source.sample = 0.5f;
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 50u;
    options.maximum_gain_scalar = 1.0f;
    options.maximum_output_peak = 0.25f;
    options.limiter_mode = native_ldac::agent::
        V1TransportPcmLimiterMode::LinkedStereoBlock;
    options.limiter_release_ms = 50.0f;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.limiter_mode == native_ldac::agent::
        V1TransportPcmLimiterMode::LinkedStereoBlock);
    CHECK(result.limiter_release_ms == 50.0f);
    CHECK(result.limiter_blocks_processed > 0u);
    CHECK(result.limiter_attack_count >= 1u);
    CHECK(std::fabs(result.limiter_minimum_gain - 0.5f) < 0.00001f);
    CHECK(std::fabs(result.limiter_last_gain - 0.5f) < 0.00001f);
    CHECK(result.limiter_maximum_gain_step >= 0.5f);
    CHECK(result.limiter_gain_reduced_frames > 0u);
    CHECK(result.limiter_gain_reduced_samples ==
        result.limiter_gain_reduced_frames * 2u);
    CHECK(result.limiter_fallback_clamp_count == 0u);
    CHECK(result.limiter_pre_over_ceiling_frames > 0u);
    CHECK(result.limiter_pre_over_ceiling_samples ==
        result.limiter_pre_over_ceiling_frames * 2u);
    CHECK(result.maximum_post_gain_peak <= 0.25f);
    CHECK(result.consumer_lease_released);
}

native_ldac::agent::V1TransportPcmOptions FidelityOptions() {
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 200u;
    options.maximum_gain_scalar = 1.0f;
    options.maximum_output_peak = 0.89125094f;
    options.limiter_mode = native_ldac::agent::
        V1TransportPcmLimiterMode::LinkedStereoSamplePeakFidelity;
    options.limiter_release_ms = 50.0f;
    options.session_generation = 7u;
    options.require_stable_volume = true;
    options.fade_in_ms = 100.0f;
    options.ceiling_ramp_start = 0.25f;
    options.ceiling_ramp_ms = 2000.0f;
    return options;
}

void FidelityBridgeTracksSentFadeAndStableVolume() {
    Backend backend;
    Source source;
    source.sample = 0.35f;
    const auto options = FidelityOptions();
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.session_generation == 7u);
    CHECK(result.volume_stable);
    CHECK(result.volume_query_count > 0u);
    CHECK(result.volume_change_count == 0u);
    CHECK(result.volume_scalar_minimum == 1.0f);
    CHECK(result.volume_scalar_maximum == 1.0f);
    CHECK(result.fade_session_started);
    CHECK(result.fade_duration_frames == 4800u);
    CHECK(result.fade_committed_sent_frames == result.pcm_frames_sent);
    CHECK(result.fade_blocks_prepared == result.fade_blocks_committed);
    CHECK(result.fade_commit_failures == 0u);
    CHECK(result.fade_frames_below_unity == 4799u);
    CHECK(result.fade_minimum_gain > 0.0f);
    CHECK(result.fade_minimum_gain < 0.001f);
    CHECK(result.fade_last_gain == 1.0f);
    CHECK(result.ceiling_ramp_last > 0.25f);
    CHECK(result.ceiling_ramp_last < 0.89125094f);
    CHECK(result.limiter_fallback_clamp_count == 0u);
    CHECK(result.maximum_post_gain_peak <= result.ceiling_ramp_last);
    CHECK(result.consumer_lease_released);
}

void SingleGainFidelityProfileIsValid() {
    Backend backend;
    Source source;
    source.sample = 0.35f;
    auto options = FidelityOptions();
    options.single_gain_mode = true;
    options.require_stable_volume = false;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.completed_full_duration);
    CHECK(result.volume_query_count == 0u);
    CHECK(result.volume_change_count == 0u);
    CHECK(result.pcm_rebind_attempts == 0u);
    CHECK(result.consumer_lease_released);
}

void FidelityFixedCeilingGracefulStopIsBounded() {
    Backend backend;
    Source source;
    source.sample = 0.1f;
    auto options = FidelityOptions();
    options.duration_ms = 2000u;
    options.ceiling_ramp_start = options.maximum_output_peak;
    options.ceiling_ramp_ms = 0.0f;
    StopContext stop{&backend, 5u,
        native_ldac::agent::V1TransportPcmStopDisposition::Graceful};
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, Stop, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.ended_by_graceful_stop);
    CHECK(!result.completed_full_duration);
    CHECK(result.actual_duration_ms > 0u);
    CHECK(result.ceiling_ramp_start == options.maximum_output_peak);
    CHECK(result.ceiling_ramp_ms == 0.0f);
    CHECK(result.ceiling_ramp_last == options.maximum_output_peak);
    CHECK(result.limiter_attack_count == 0u);
    CHECK(result.limiter_gain_reduced_frames == 0u);
    CHECK(result.limiter_gain_reduced_samples == 0u);
    CHECK(result.limiter_fallback_clamp_count == 0u);
    CHECK(result.fade_committed_sent_frames == result.pcm_frames_sent);
    CHECK(result.fade_commit_failures == 0u);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
    CHECK(!result.remote_stream_cleanup_required);
    CHECK(result.consumer_lease_released);
}

void FidelityUnityBoundaryPreservesFullScale() {
    Backend backend;
    Source source;
    source.sample = 1.0f;
    auto options = FidelityOptions();
    options.maximum_output_peak = 1.0f;
    options.ceiling_ramp_start = 1.0f;
    options.ceiling_ramp_ms = 0.0f;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.maximum_output_peak_ceiling == 1.0f);
    CHECK(result.maximum_unlimited_post_gain_peak == 1.0f);
    CHECK(result.maximum_post_gain_peak == 1.0f);
    CHECK(result.limiter_attack_count == 0u);
    CHECK(result.limiter_gain_reduced_frames == 0u);
    CHECK(result.limiter_gain_reduced_samples == 0u);
    CHECK(result.limited_output_samples == 0u);
    CHECK(result.consumer_lease_released);
}

void FidelityFixedCeilingRequiresFinalCeiling() {
    Backend backend;
    Source source;
    auto options = FidelityOptions();
    options.ceiling_ramp_start = 0.5f;
    options.ceiling_ramp_ms = 0.0f;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(!result.pcm_prepared);
    CHECK(result.open_attempts == 0u);
    CHECK(backend.signals.empty());
}

void FidelityBridgeStopsOnVolumeChange() {
    Backend backend;
    Source source;
    source.sample = 0.35f;
    source.change_volume_after_query = 3u;
    const auto options = FidelityOptions();
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.volume_query_count == 3u);
    CHECK(result.volume_change_count == 1u);
    CHECK(!result.volume_stable);
    CHECK(result.media_packets_written == 0u);
    CHECK(result.fade_committed_sent_frames == result.pcm_frames_sent);
    CHECK(result.fade_blocks_prepared >= 2u);
    CHECK(result.fade_blocks_committed == 0u);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
    CHECK(result.consumer_lease_released);
}

void DynamicVolumeAndMuteContinueStreaming() {
    Backend backend;
    Source source;
    source.sample = 0.35f;
    source.change_volume_after_query = 3u;
    source.change_mute_after_query = 3u;
    auto options = FidelityOptions();
    options.allow_dynamic_volume = true;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.completed_full_duration);
    CHECK(result.media_packets_written > 0u);
    CHECK(result.volume_change_count == 1u);
    CHECK(!result.volume_stable);
    CHECK(result.volume_scalar_minimum == 0.5f);
    CHECK(result.volume_scalar_maximum == 1.0f);
    CHECK(result.volume_scalar_last == 0.5f);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
    CHECK(result.consumer_lease_released);
}

void FidelityBridgeTreatsEpochChangeAsGracefulStop() {
    Backend backend;
    Source source;
    source.sample = 0.35f;
    source.change_epoch_after_query = 3u;
    const auto options = FidelityOptions();
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.ended_by_graceful_stop);
    CHECK(!result.completed_full_duration);
    CHECK(result.volume_query_count == 3u);
    CHECK(result.volume_change_count == 0u);
    CHECK(result.volume_stable);
    CHECK(result.fade_committed_sent_frames == result.pcm_frames_sent);
    CHECK(result.fade_commit_failures == 0u);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
    CHECK(result.consumer_lease_released);
}

void FidelityBridgeNinetySixKhzCommitsAtomically() {
    {
        Backend backend;
        Source source;
        source.sample_rate_hz = 96000u;
        source.sample = 0.35f;
        auto options = FidelityOptions();
        options.duration_ms = 10u;
        const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
            &backend, &source, options);
        CHECK(result.disposition == native_ldac::agent::
            V1TransportConfigurationDisposition::Succeeded);
        CHECK(result.fade_blocks_prepared == result.fade_blocks_committed);
        CHECK(result.fade_blocks_committed >= 2u);
        CHECK(result.fade_committed_sent_frames == result.pcm_frames_sent);
        CHECK(result.fade_commit_failures == 0u);
    }
    {
        Backend backend;
        backend.fail_write_attempt = 1u;
        Source source;
        source.sample_rate_hz = 96000u;
        source.sample = 0.35f;
        auto options = FidelityOptions();
        options.duration_ms = 10u;
        const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
            &backend, &source, options);
        CHECK(result.disposition == native_ldac::agent::
            V1TransportConfigurationDisposition::BackendFailure);
        CHECK(result.media_packets_written == 0u);
        CHECK(result.pcm_frames_sent == 0u);
        CHECK(result.fade_blocks_prepared >= 2u);
        CHECK(result.fade_blocks_committed == 0u);
        CHECK(result.fade_committed_sent_frames == 0u);
        CHECK(result.fade_commit_failures == 0u);
    }
}

void FidelityBridgeRechecksStopAfterPacing() {
    for (const auto disposition : {
            native_ldac::agent::V1TransportPcmStopDisposition::Graceful,
            native_ldac::agent::V1TransportPcmStopDisposition::Cancel}) {
        Backend backend;
        Source source;
        source.sample = 0.35f;
        SourceWaitStopContext stop{&source, disposition};
        const auto options = FidelityOptions();
        const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
            &backend, &source, options,
            StopAfterSourceWait, &stop);
        CHECK(result.disposition == native_ldac::agent::
            V1TransportConfigurationDisposition::Cancelled);
        CHECK(result.media_packets_written == 0u);
        CHECK(result.pcm_frames_sent == 0u);
        CHECK(result.fade_blocks_committed == 0u);
        CHECK(result.fade_committed_sent_frames == 0u);
        if (disposition == native_ldac::agent::
                V1TransportPcmStopDisposition::Graceful) {
            CHECK(result.avdtp_suspend_accepted);
            CHECK(result.avdtp_close_accepted);
        } else {
            CHECK(!result.avdtp_suspend_accepted);
            CHECK(!result.avdtp_close_accepted);
        }
    }
}

void QuietPcmNeverOpensBluetooth() {
    Backend backend;
    Source source;
    source.quiet = true;
    native_ldac::agent::V1TransportPcmOptions options;
    options.audible_preflight_timeout_ms = 100u;
    options.pcm_read_timeout_ms = 10u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::ProtocolFailure);
    CHECK(!result.audible_pcm_confirmed_before_open);
    CHECK(result.open_attempts == 0u);
    CHECK(backend.signals.empty());
    CHECK(source.release_count == 1u);
}

void RetryableOpenFailureReleasesConsumer() {
    Backend backend;
    backend.open_error = kRetryableOpenError;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::BackendFailure);
    CHECK(result.stage == native_ldac::agent::
        V1TransportSilenceStage::OpenSignaling);
    CHECK(result.backend_error == kRetryableOpenError);
    CHECK(result.open_attempts == 1u);
    CHECK(result.signaling_exchanges == 0u);
    CHECK(result.audible_pcm_confirmed_before_open);
    CHECK(result.consumer_lease_released);
    CHECK(source.release_count == 1u);
    CHECK(backend.signals.empty());
}

void NegotiationMismatchCapturesSignalingHeaders() {
    Backend backend;
    backend.mismatched_first_response = true;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::ProtocolFailure);
    CHECK(result.stage == native_ldac::agent::
        V1TransportSilenceStage::Negotiate);
    CHECK(result.protocol_error == AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE);
    CHECK(result.signaling_exchanges == 1u);
    CHECK(result.last_signaling_response_size == 4u);
    CHECK(result.last_signaling_tx_header_available);
    CHECK(result.last_signaling_tx_transaction_label == 0u);
    CHECK(result.last_signaling_tx_message_type == AVDTP_MESSAGE_COMMAND);
    CHECK(result.last_signaling_tx_signal_id == AVDTP_SIGNAL_DISCOVER);
    CHECK(result.last_signaling_rx_header_available);
    CHECK(result.last_signaling_rx_transaction_label == 1u);
    CHECK(result.last_signaling_rx_message_type == AVDTP_MESSAGE_ACCEPT);
    CHECK(result.last_signaling_rx_signal_id == AVDTP_SIGNAL_DISCOVER);
    CHECK(result.consumer_lease_released);
}

void PeerDiscoverCollisionIsAnsweredAndNegotiationContinues() {
    Backend backend;
    backend.peer_discover_before_first_response = true;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 100u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(backend.peer_command_sent);
    CHECK(backend.peer_response_received);
    CHECK(result.peer_signaling_commands_received == 1u);
    CHECK(result.peer_discover_commands_accepted == 1u);
    CHECK(result.signaling_exchanges == 8u);
    CHECK(result.last_signaling_tx_header_available);
    CHECK(result.last_signaling_rx_header_available);
    CHECK(result.media_packets_written > 4u);
    CHECK(result.consumer_lease_released);
}

void RepeatedPeerDiscoverCollisionFailsBounded() {
    Backend backend;
    backend.peer_discover_before_first_response = true;
    backend.repeat_peer_discover = true;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::ProtocolFailure);
    CHECK(result.stage == native_ldac::agent::
        V1TransportSilenceStage::Negotiate);
    CHECK(result.protocol_error == AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE);
    CHECK(result.peer_signaling_commands_received == 2u);
    CHECK(result.peer_discover_commands_accepted == 1u);
    CHECK(result.signaling_exchanges == 2u);
    CHECK(result.media_packets_written == 0u);
    CHECK(result.consumer_lease_released);
}

void PeerDiscoveryAndCapabilitiesCollisionsContinue() {
    Backend backend;
    backend.peer_discover_before_first_response = true;
    backend.peer_capabilities_after_discover = true;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 100u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(backend.peer_capability_command_sent);
    CHECK(backend.peer_capability_response_received);
    CHECK(result.peer_signaling_commands_received == 2u);
    CHECK(result.peer_discover_commands_accepted == 1u);
    CHECK(result.peer_capability_commands_accepted == 1u);
    CHECK(result.signaling_exchanges == 9u);
    CHECK(result.media_packets_written > 4u);
    CHECK(result.consumer_lease_released);
}

void PeerConfigurationCollisionIsRejectedAndNegotiationContinues() {
    Backend backend;
    backend.peer_discover_before_first_response = true;
    backend.peer_capabilities_after_discover = true;
    backend.peer_set_configuration_after_capabilities = true;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 100u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(backend.peer_set_configuration_command_sent);
    CHECK(backend.peer_set_configuration_response_received);
    CHECK(result.peer_signaling_commands_received == 3u);
    CHECK(result.peer_discover_commands_accepted == 1u);
    CHECK(result.peer_capability_commands_accepted == 1u);
    CHECK(result.peer_configuration_commands_rejected == 1u);
    CHECK(result.signaling_exchanges == 10u);
    CHECK(result.media_packets_written > 4u);
    CHECK(result.consumer_lease_released);
}

void MalformedPeerConfigurationCollisionFailsBounded() {
    Backend backend;
    backend.peer_discover_before_first_response = true;
    backend.peer_capabilities_after_discover = true;
    backend.peer_set_configuration_after_capabilities = true;
    backend.malformed_peer_set_configuration = true;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::ProtocolFailure);
    CHECK(result.stage == native_ldac::agent::
        V1TransportSilenceStage::Negotiate);
    CHECK(result.protocol_error == AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE);
    CHECK(result.peer_signaling_commands_received == 3u);
    CHECK(result.peer_configuration_commands_rejected == 0u);
    CHECK(result.signaling_exchanges == 4u);
    CHECK(result.media_packets_written == 0u);
    CHECK(result.consumer_lease_released);
}

void RepeatedPeerConfigurationCollisionFailsBounded() {
    Backend backend;
    backend.peer_discover_before_first_response = true;
    backend.peer_capabilities_after_discover = true;
    backend.peer_set_configuration_after_capabilities = true;
    backend.repeat_peer_set_configuration = true;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::ProtocolFailure);
    CHECK(result.stage == native_ldac::agent::
        V1TransportSilenceStage::Negotiate);
    CHECK(result.protocol_error == AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE);
    CHECK(result.peer_signaling_commands_received == 4u);
    CHECK(result.peer_configuration_commands_rejected == 1u);
    CHECK(result.signaling_exchanges == 5u);
    CHECK(result.media_packets_written == 0u);
    CHECK(result.consumer_lease_released);
}

void PeerCloseDuringStreamingIsAcceptedAndStopsLocally() {
    Backend backend;
    backend.peer_close_after_write_attempt = 3u;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 1000u;
    options.observe_peer_close_while_streaming = true;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.stage == native_ldac::agent::
        V1TransportSilenceStage::WritePcm);
    CHECK(result.peer_signaling_commands_received == 1u);
    CHECK(result.peer_close_commands_accepted == 1u);
    CHECK(result.ended_by_peer_close);
    CHECK(!result.remote_stream_cleanup_required);
    CHECK(!result.avdtp_suspend_accepted);
    CHECK(!result.avdtp_close_accepted);
    CHECK(result.signaling_exchanges == 5u);
    CHECK(result.media_packets_written == 3u);
    CHECK(backend.peer_close_command_sent);
    CHECK(backend.peer_close_response_sent);
    CHECK(backend.peer_signaling_read_begin_count == 1u);
    CHECK(result.consumer_lease_released);
}

void PeerCloseAfterPauseSuspendIsAcceptedAndStopsLocally() {
    Backend backend;
    backend.peer_close_after_suspend = true;
    Source source;
    source.stop_once_after_reads = 10u;
    source.pause_prepare_timeouts_remaining = 10u;
    auto options = ContinuousFidelityOptions();
    options.pause_suspend = true;
    SourceReadStopContext stop{
        &source, std::numeric_limits<unsigned>::max(),
        native_ldac::agent::V1TransportPcmStopDisposition::Graceful};
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, StopAfterSourceReads, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.stage == native_ldac::agent::
        V1TransportSilenceStage::WritePcm);
    CHECK(result.protocol_error == 0);
    CHECK(result.pause_suspend_count == 1u);
    CHECK(result.pause_resume_start_count == 0u);
    CHECK(result.peer_signaling_commands_received == 1u);
    CHECK(result.peer_close_commands_accepted == 1u);
    CHECK(result.ended_by_peer_close);
    CHECK(!result.remote_stream_cleanup_required);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(!result.avdtp_close_accepted);
    CHECK(backend.peer_close_command_sent);
    CHECK(backend.peer_close_response_sent);
    CHECK(result.consumer_lease_released);
}

void PeerSignalingReadTimeoutRearmsAndNormalStopContinues() {
    Backend backend;
    backend.peer_signaling_read_timeout_once = true;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 100u;
    options.observe_peer_close_while_streaming = true;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.peer_signaling_read_timeouts == 1u);
    CHECK(result.peer_close_commands_accepted == 0u);
    CHECK(!result.ended_by_peer_close);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
    CHECK(backend.peer_signaling_read_begin_count == 2u);
    CHECK(backend.peer_signaling_read_cancel_count == 1u);
    CHECK(result.consumer_lease_released);
}

void MalformedPeerCloseDuringStreamingFailsBounded() {
    Backend backend;
    backend.peer_close_after_write_attempt = 1u;
    backend.malformed_peer_close = true;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 1000u;
    options.observe_peer_close_while_streaming = true;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::ProtocolFailure);
    CHECK(result.stage == native_ldac::agent::
        V1TransportSilenceStage::WritePcm);
    CHECK(result.protocol_error == AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE);
    CHECK(result.peer_signaling_commands_received == 1u);
    CHECK(result.peer_close_commands_accepted == 0u);
    CHECK(!backend.peer_close_response_sent);
    CHECK(!result.avdtp_suspend_accepted);
    CHECK(!result.avdtp_close_accepted);
    CHECK(result.consumer_lease_released);
}

void WriteFailureStopsLocally() {
    Backend backend;
    backend.fail_write_attempt = 3u;
    Source source;
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 100u;
    bool notified = false;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, nullptr, nullptr,
        NotifyStarted, &notified);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::BackendFailure);
    CHECK(result.backend_error == 29u);
    CHECK(result.media_packets_written == 2u);
    CHECK(result.remote_stream_cleanup_required);
    CHECK(result.consumer_lease_released);
    CHECK(backend.signals.back() == AVDTP_SIGNAL_START);
}

void TransientNotReadyMediaWriteRetriesBounded() {
    Backend backend;
    backend.not_ready_write_attempt_start = 10u;
    // Eight consecutive failures exceed the old 60 ms window and model the
    // longer device-ready transition observed after a real pause/resume.
    backend.not_ready_write_attempts_remaining = 8u;
    Source source;
    auto options = FidelityOptions();
    options.duration_ms = 200u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.completed_full_duration);
    CHECK(result.backend_error == 0u);
    CHECK(backend.not_ready_write_attempts_remaining == 0u);
    CHECK(result.media_write_not_ready_retries == 8u);
    CHECK(result.media_write_not_ready_exhaustions == 0u);
    CHECK(backend.write_attempts ==
        result.media_packets_written + 8u);
    CHECK(result.consumer_lease_released);
}

void TransientPcmTimeoutRecoversWithFreshPacingBoundary() {
    Backend backend;
    Source source;
    source.pcm_timeout_after_reads = 10u;
    source.pcm_timeouts_remaining = 3u;
    auto options = ContinuousFidelityOptions();
    options.pcm_timeout_tolerance_ms = 2000u;
    options.startup_silence_ms = 20.0f;
    options.fade_in_ms = 100.0f;
    SourceReadStopContext stop{
        &source, 25u,
        native_ldac::agent::V1TransportPcmStopDisposition::Graceful};
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, StopAfterSourceReads, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.pcm_transient_timeout_count == 3u);
    CHECK(result.pcm_transient_timeout_recovery_count == 1u);
    CHECK(result.pcm_transient_timeout_exhausted_count == 0u);
    CHECK(result.pcm_transient_timeout_max_streak_ms == 750u);
    CHECK(source.reset_pacing_count == 1u);
    CHECK(result.boundary_resume_count == 1u);
    CHECK(result.media_packets_written > 0u);
    CHECK(result.consumer_lease_released);
}

void PcmTimeoutToleranceExhaustsBounded() {
    Backend backend;
    Source source;
    source.pcm_timeout_after_reads = 10u;
    source.pcm_timeouts_remaining = 3u;
    auto options = ContinuousFidelityOptions();
    options.pcm_timeout_tolerance_ms = 500u;
    StopContext stop{
        &backend, 1000u,
        native_ldac::agent::V1TransportPcmStopDisposition::Graceful};
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, Stop, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::BackendFailure);
    CHECK(result.backend_error == 258u);
    CHECK(result.pcm_transient_timeout_count == 3u);
    CHECK(result.pcm_transient_timeout_recovery_count == 0u);
    CHECK(result.pcm_transient_timeout_exhausted_count == 1u);
    CHECK(result.pcm_transient_timeout_max_streak_ms == 750u);
    CHECK(source.reset_pacing_count == 0u);
    CHECK(result.consumer_lease_released);
}

void TimeoutThenStreamStopUsesPauseSuspend() {
    Backend backend;
    Source source;
    source.pcm_timeout_after_reads = 10u;
    source.pcm_timeouts_remaining = 1u;
    source.stop_once_after_reads = 10u;
    source.pause_prepare_timeouts_remaining = 1u;
    auto options = ContinuousFidelityOptions();
    options.pause_suspend = true;
    options.pcm_timeout_tolerance_ms = 2000u;
    SourceReadStopContext stop{
        &source, 20u,
        native_ldac::agent::V1TransportPcmStopDisposition::Graceful};
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, StopAfterSourceReads, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.pcm_transient_timeout_count == 1u);
    CHECK(result.pcm_transient_timeout_recovery_count == 0u);
    CHECK(result.pcm_transient_timeout_exhausted_count == 0u);
    CHECK(result.pause_suspend_count == 1u);
    CHECK(result.pause_resume_start_count == 1u);
    CHECK(source.reset_pacing_count == 0u);
    CHECK(result.consumer_lease_released);
}

void StreamStopAfterStartSuspendsAndCloses() {
    Backend backend;
    Source source;
    source.stop_once_after_reads = 10u;
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 1000u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.ended_by_graceful_stop);
    CHECK(!result.completed_full_duration);
    CHECK(result.actual_duration_ms > 0u);
    CHECK(result.media_packets_written > 0u);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
    CHECK(!result.remote_stream_cleanup_required);
    CHECK(result.consumer_lease_released);
    CHECK(backend.signals.back() == AVDTP_SIGNAL_CLOSE);
}

void PostStartEpochRebindContinuesSameTransport() {
    Backend backend;
    Source source;
    source.sample = 0.35f;
    source.stop_once_after_reads = 10u;
    auto options = FidelityOptions();
    options.duration_ms = 200u;
    options.post_start_stop_classification_timeout_ms = 100u;
    options.allow_dynamic_volume = true;
    options.allow_post_start_pcm_rebind = true;
    options.startup_silence_ms = 20.0f;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.completed_full_duration);
    CHECK(result.pcm_prepare_attempts == 2u);
    CHECK(result.pcm_epoch_restarts == 1u);
    CHECK(result.pcm_stream_stop_count == 1u);
    CHECK(result.pcm_stream_stop_detected);
    CHECK(result.pcm_stream_stop_error == 232u);
    CHECK(result.pcm_stream_stop_snapshot_valid);
    CHECK(!result.pcm_stream_stop_snapshot.stream_active);
    CHECK(result.pcm_rebind_attempts == 1u);
    CHECK(result.pcm_rebind_successes == 1u);
    CHECK(result.pcm_rebind_failures == 0u);
    CHECK(result.consumer_lease_acquire_count == 2u);
    CHECK(result.consumer_lease_release_count == 2u);
    CHECK(result.media_packets_written > 0u);
    CHECK(result.open_attempts == 1u);
    CHECK(result.boundary_resume_count == 1u);
    CHECK(result.boundary_resume_fade_frames > 0u);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
    CHECK(result.consumer_lease_released);
}

void PostStartRebindFailureRecordsPcmEvidence() {
    Backend backend;
    Source source;
    source.sample = 0.35f;
    source.stop_once_after_reads = 10u;
    source.fail_rebind_prepare = true;
    source.snapshot_discontinuity = true;
    source.snapshot_available_bytes = 0u;
    source.snapshot_capacity_bytes = 48000u;
    source.snapshot_total_bytes_written = 100000u;
    source.snapshot_total_bytes_read = 90000u;
    source.snapshot_total_bytes_dropped = 1000u;
    auto options = FidelityOptions();
    options.duration_ms = 1000u;
    options.post_start_stop_classification_timeout_ms = 100u;
    options.allow_dynamic_volume = true;
    options.allow_post_start_pcm_rebind = true;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::BackendFailure);
    CHECK(result.stage == native_ldac::agent::
        V1TransportSilenceStage::PreparePcm);
    CHECK(result.backend_error == 258u);
    CHECK(result.pcm_stream_stop_count == 1u);
    CHECK(result.pcm_stream_stop_detected);
    CHECK(result.pcm_stream_stop_error == 232u);
    CHECK(result.pcm_stream_stop_snapshot_valid);
    CHECK(!result.pcm_stream_stop_snapshot.stream_active);
    CHECK(result.pcm_stream_stop_snapshot.discontinuity);
    CHECK(result.pcm_stream_stop_snapshot.capacity_bytes == 48000u);
    CHECK(result.pcm_stream_stop_snapshot.total_bytes_written == 100000u);
    CHECK(result.pcm_stream_stop_snapshot.total_bytes_read == 90000u);
    CHECK(result.pcm_stream_stop_snapshot.total_bytes_dropped == 1000u);
    CHECK(result.pcm_rebind_attempts == 1u);
    CHECK(result.pcm_rebind_successes == 0u);
    CHECK(result.pcm_rebind_failures == 1u);
    CHECK(result.pcm_rebind_last_error == 258u);
    CHECK(result.pcm_rebind_last_timeout_ms == 100u);
    CHECK(result.consumer_lease_released);
}

void PostStartObservedEpochRebindContinuesSameTransport() {
    Backend backend;
    Source source;
    source.sample = 0.35f;
    source.change_epoch_after_query = 7u;
    auto options = FidelityOptions();
    options.duration_ms = 200u;
    options.post_start_stop_classification_timeout_ms = 100u;
    options.allow_dynamic_volume = true;
    options.allow_post_start_pcm_rebind = true;
    options.startup_silence_ms = 20.0f;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.completed_full_duration);
    CHECK(result.pcm_prepare_attempts == 2u);
    CHECK(result.pcm_epoch_restarts == 1u);
    CHECK(result.consumer_lease_acquire_count == 2u);
    CHECK(result.consumer_lease_release_count == 2u);
    CHECK(result.open_attempts == 1u);
    CHECK(result.boundary_resume_count == 1u);
    CHECK(result.boundary_resume_fade_frames > 0u);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
    CHECK(result.consumer_lease_released);
}

void PostStartRebindFormatChangeStopsSafely() {
    Backend backend;
    Source source;
    source.sample = 0.35f;
    source.stop_once_after_reads = 10u;
    source.rebound_sample_rate_hz = 44100u;
    auto options = FidelityOptions();
    options.duration_ms = 1000u;
    options.post_start_stop_classification_timeout_ms = 100u;
    options.allow_dynamic_volume = true;
    options.allow_post_start_pcm_rebind = true;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.protocol_error != 0);
    CHECK(result.ended_by_graceful_stop);
    CHECK(result.pcm_prepare_attempts == 2u);
    CHECK(result.consumer_lease_acquire_count == 2u);
    CHECK(result.consumer_lease_release_count == 2u);
    CHECK(result.open_attempts == 1u);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
    CHECK(result.consumer_lease_released);
}

void CancelWinsPostStartRebindWithoutSignaling() {
    Backend backend;
    Source source;
    source.sample = 0.35f;
    source.stop_once_after_reads = 10u;
    SourceStreamStopContext stop{
        &source, native_ldac::agent::V1TransportPcmStopDisposition::Cancel,
        1u, 0u};
    auto options = FidelityOptions();
    options.duration_ms = 1000u;
    options.post_start_stop_classification_timeout_ms = 100u;
    options.allow_dynamic_volume = true;
    options.allow_post_start_pcm_rebind = true;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, StopAfterSourceStreamStop, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(!result.ended_by_graceful_stop);
    CHECK(result.pcm_prepare_attempts == 2u);
    CHECK(result.consumer_lease_acquire_count == 2u);
    CHECK(result.consumer_lease_release_count == 2u);
    CHECK(!result.avdtp_suspend_accepted);
    CHECK(!result.avdtp_close_accepted);
    CHECK(result.remote_stream_cleanup_required);
    CHECK(result.consumer_lease_released);
    CHECK(backend.signals.back() == AVDTP_SIGNAL_START);
}

void CancelWinsConcurrentStreamStopWithoutSignaling() {
    Backend backend;
    Source source;
    source.stop_once_after_reads = 10u;
    SourceStreamStopContext stop{
        &source, native_ldac::agent::V1TransportPcmStopDisposition::Cancel,
        0u, 0u};
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 1000u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, StopAfterSourceStreamStop, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(!result.ended_by_graceful_stop);
    CHECK(!result.avdtp_suspend_accepted);
    CHECK(!result.avdtp_close_accepted);
    CHECK(result.remote_stream_cleanup_required);
    CHECK(result.consumer_lease_released);
    CHECK(backend.signals.back() == AVDTP_SIGNAL_START);
}

void DelayedCancelWinsStreamStopClassificationWithoutSignaling() {
    Backend backend;
    Source source;
    source.stop_once_after_reads = 10u;
    SourceStreamStopContext stop{
        &source, native_ldac::agent::V1TransportPcmStopDisposition::Cancel,
        2u, 0u};
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 1000u;
    options.post_start_stop_classification_timeout_ms = 100u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, StopAfterSourceStreamStop, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(stop.probe_count > stop.probes_before_stop);
    CHECK(!result.ended_by_graceful_stop);
    CHECK(!result.avdtp_suspend_accepted);
    CHECK(!result.avdtp_close_accepted);
    CHECK(result.remote_stream_cleanup_required);
    CHECK(result.consumer_lease_released);
    CHECK(backend.signals.back() == AVDTP_SIGNAL_START);
}

void UnclassifiedStreamStopFailsLocallyWithoutSignaling() {
    Backend backend;
    Source source;
    source.stop_once_after_reads = 10u;
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 1000u;
    options.post_start_stop_classification_timeout_ms = 1u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::BackendFailure);
    CHECK(result.actual_duration_ms > 0u);
    CHECK(!result.ended_by_graceful_stop);
    CHECK(!result.avdtp_suspend_accepted);
    CHECK(!result.avdtp_close_accepted);
    CHECK(result.remote_stream_cleanup_required);
    CHECK(result.consumer_lease_released);
    CHECK(backend.signals.back() == AVDTP_SIGNAL_START);
}

void GracefulStopSuspendsAndCloses() {
    Backend backend;
    Source source;
    StopContext stop{&backend, 2u,
        native_ldac::agent::V1TransportPcmStopDisposition::Graceful};
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 1000u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, Stop, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.ended_by_graceful_stop);
    CHECK(!result.completed_full_duration);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
    CHECK(!result.remote_stream_cleanup_required);
    CHECK(backend.signals.back() == AVDTP_SIGNAL_CLOSE);
}

void CancelStopsWithoutBlindSignaling() {
    Backend backend;
    Source source;
    StopContext stop{&backend, 2u,
        native_ldac::agent::V1TransportPcmStopDisposition::Cancel};
    native_ldac::agent::V1TransportPcmOptions options;
    options.duration_ms = 1000u;
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, Stop, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.actual_duration_ms > 0u);
    CHECK(!result.completed_full_duration);
    CHECK(!result.avdtp_suspend_accepted);
    CHECK(!result.avdtp_close_accepted);
    CHECK(result.remote_stream_cleanup_required);
    CHECK(backend.signals.back() == AVDTP_SIGNAL_START);
}

native_ldac::agent::V1TransportPcmOptions ContinuousFidelityOptions() {
    auto options = FidelityOptions();
    options.duration_ms = 0u;
    options.maximum_packets = 0u;
    options.continuous_until_stop = true;
    options.post_start_stop_classification_timeout_ms = 30000u;
    options.allow_dynamic_volume = true;
    options.allow_post_start_pcm_rebind = true;
    options.observe_peer_close_while_streaming = true;
    options.ceiling_ramp_start = options.maximum_output_peak;
    options.ceiling_ramp_ms = 0.0f;
    return options;
}

void ContinuousGracefulStopSuspendsAndCloses() {
    Backend backend;
    Source source;
    StopContext stop{&backend, 3u,
        native_ldac::agent::V1TransportPcmStopDisposition::Graceful};
    const auto options = ContinuousFidelityOptions();
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, Stop, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.target_duration_ms == 0u);
    CHECK(result.actual_duration_ms > 0u);
    CHECK(!result.completed_full_duration);
    CHECK(result.ended_by_graceful_stop);
    CHECK(result.media_packets_written == 3u);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
    CHECK(!result.remote_stream_cleanup_required);
    CHECK(result.consumer_lease_released);
    CHECK(backend.signals.back() == AVDTP_SIGNAL_CLOSE);
}

void ContinuousStartupSilenceIsTransportOnly() {
    Backend backend;
    Source source;
    auto options = ContinuousFidelityOptions();
    options.startup_silence_ms = 20.0f;
    SourceReadStopContext stop{
        &source, 5u,
        native_ldac::agent::V1TransportPcmStopDisposition::Graceful};
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, StopAfterSourceReads, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.ended_by_graceful_stop);
    CHECK(result.startup_silence_ms == 20.0f);
    CHECK(result.startup_silence_frames_sent > 0u);
    CHECK(result.startup_silence_packets_written > 0u);
    CHECK(result.boundary_resume_count == 0u);
    CHECK(result.boundary_resume_fade_frames == 0u);
    CHECK(result.transport_frames_sent ==
        result.startup_silence_frames_sent + result.pcm_frames_sent);
    CHECK(result.media_packets_written == result.pacing_waits);
    CHECK(result.fade_committed_sent_frames == result.pcm_frames_sent);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
}

void DailyExtendedFadeInIsSupported() {
    Backend backend;
    Source source;
    source.sample_rate_hz = 44100u;
    auto options = ContinuousFidelityOptions();
    options.startup_silence_ms = 20.0f;
    options.fade_in_ms = 500.0f;
    SourceReadStopContext stop{
        &source, 220u,
        native_ldac::agent::V1TransportPcmStopDisposition::Graceful};
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, StopAfterSourceReads, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.ended_by_graceful_stop);
    CHECK(result.fade_in_ms == 500.0f);
    CHECK(result.fade_duration_frames == 22050u);
    CHECK(result.fade_last_gain == 1.0f);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
}

void TransientBoundaryResumeThenGracefulStopUsesResumeFade() {
    Backend backend;
    Source source;
    source.sample = 0.35f;
    source.stop_once_after_reads = 10u;
    auto options = ContinuousFidelityOptions();
    options.startup_silence_ms = 20.0f;
    SourceReadStopContext stop{
        &source, 20u,
        native_ldac::agent::V1TransportPcmStopDisposition::Graceful};
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, StopAfterSourceReads, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.ended_by_graceful_stop);
    CHECK(result.boundary_resume_count == 1u);
    CHECK(result.boundary_resume_fade_frames > 0u);
    CHECK(result.open_attempts == 1u);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
    CHECK(result.consumer_lease_released);
}

void PauseSuspendResumesSameTransportWithoutMediaPackets() {
    Backend backend;
    Source source;
    source.sample = 0.35f;
    source.stop_once_after_reads = 10u;
    source.pause_prepare_timeouts_remaining = 3u;
    auto options = ContinuousFidelityOptions();
    options.pause_suspend = true;
    options.startup_silence_ms = 20.0f;
    options.fade_in_ms = 100.0f;
    SourceReadStopContext stop{
        &source, 20u,
        native_ldac::agent::V1TransportPcmStopDisposition::Graceful};
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, StopAfterSourceReads, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.ended_by_graceful_stop);
    CHECK(result.open_attempts == 1u);
    CHECK(result.pcm_prepare_attempts == 5u);
    CHECK(result.pcm_rebind_attempts == 0u);
    CHECK(result.pause_suspend_count == 1u);
    CHECK(result.pause_resume_start_count == 1u);
    CHECK(result.pause_wait_prepare_attempts == 4u);
    CHECK(result.boundary_resume_count == 1u);
    CHECK(result.boundary_resume_fade_frames > 0u);
    CHECK(result.consumer_lease_acquire_count == 2u);
    CHECK(result.consumer_lease_release_count == 2u);
    CHECK(std::count(backend.signals.begin(), backend.signals.end(),
                     AVDTP_SIGNAL_START) == 2);
    CHECK(std::count(backend.signals.begin(), backend.signals.end(),
                     AVDTP_SIGNAL_SUSPEND) == 2);
    CHECK(std::count(backend.signals.begin(), backend.signals.end(),
                     AVDTP_SIGNAL_CLOSE) == 1);
    const auto first_suspend = std::find(
        backend.signals.begin(), backend.signals.end(), AVDTP_SIGNAL_SUSPEND);
    CHECK(first_suspend != backend.signals.end());
    if (first_suspend != backend.signals.end()) {
        const auto resumed_start = std::find(
            first_suspend + 1, backend.signals.end(), AVDTP_SIGNAL_START);
        CHECK(resumed_start != backend.signals.end());
        if (resumed_start != backend.signals.end()) {
            const auto suspend_index = static_cast<std::size_t>(
                first_suspend - backend.signals.begin());
            const auto resume_index = static_cast<std::size_t>(
                resumed_start - backend.signals.begin());
            CHECK(backend.signal_write_attempts[suspend_index] ==
                  backend.signal_write_attempts[resume_index]);
        }
    }
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
    CHECK(result.consumer_lease_released);
}

void PauseSuspendThenGracefulStopClosesWithoutResumeStart() {
    Backend backend;
    Source source;
    source.stop_once_after_reads = 10u;
    source.pause_prepare_timeouts_remaining = 10u;
    auto options = ContinuousFidelityOptions();
    options.pause_suspend = true;
    SourcePrepareStopContext stop{
        &source, 3u,
        native_ldac::agent::V1TransportPcmStopDisposition::Graceful};
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, StopAfterSourcePrepares, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.ended_by_graceful_stop);
    CHECK(result.pause_suspend_count == 1u);
    CHECK(result.pause_resume_start_count == 0u);
    CHECK(result.boundary_resume_count == 0u);
    CHECK(std::count(backend.signals.begin(), backend.signals.end(),
                     AVDTP_SIGNAL_START) == 1);
    CHECK(std::count(backend.signals.begin(), backend.signals.end(),
                     AVDTP_SIGNAL_SUSPEND) == 1);
    CHECK(std::count(backend.signals.begin(), backend.signals.end(),
                     AVDTP_SIGNAL_CLOSE) == 1);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
    CHECK(result.consumer_lease_released);
}

void RepeatedPauseSuspendCyclesReuseOneTransport() {
    Backend backend;
    Source source;
    source.stream_stop_reads = {10u, 20u, 30u};
    auto options = ContinuousFidelityOptions();
    options.pause_suspend = true;
    SourceReadStopContext stop{
        &source, 40u,
        native_ldac::agent::V1TransportPcmStopDisposition::Graceful};
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, StopAfterSourceReads, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.ended_by_graceful_stop);
    CHECK(result.open_attempts == 1u);
    CHECK(result.pause_suspend_count == 3u);
    CHECK(result.pause_resume_start_count == 3u);
    CHECK(result.boundary_resume_count == 3u);
    CHECK(result.consumer_lease_acquire_count == 4u);
    CHECK(result.consumer_lease_release_count == 4u);
    CHECK(std::count(backend.signals.begin(), backend.signals.end(),
                     AVDTP_SIGNAL_START) == 4);
    CHECK(std::count(backend.signals.begin(), backend.signals.end(),
                     AVDTP_SIGNAL_SUSPEND) == 4);
    CHECK(std::count(backend.signals.begin(), backend.signals.end(),
                     AVDTP_SIGNAL_CLOSE) == 1);
    CHECK(result.avdtp_close_accepted);
    CHECK(result.consumer_lease_released);
}

void PauseSuspendThenCancelDoesNotSignalRemoteClose() {
    Backend backend;
    Source source;
    source.stop_once_after_reads = 10u;
    source.pause_prepare_timeouts_remaining = 10u;
    auto options = ContinuousFidelityOptions();
    options.pause_suspend = true;
    SourcePrepareStopContext stop{
        &source, 3u,
        native_ldac::agent::V1TransportPcmStopDisposition::Cancel};
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, StopAfterSourcePrepares, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(!result.ended_by_graceful_stop);
    CHECK(result.pause_suspend_count == 1u);
    CHECK(result.pause_resume_start_count == 0u);
    CHECK(std::count(backend.signals.begin(), backend.signals.end(),
                     AVDTP_SIGNAL_START) == 1);
    CHECK(std::count(backend.signals.begin(), backend.signals.end(),
                     AVDTP_SIGNAL_SUSPEND) == 1);
    CHECK(std::count(backend.signals.begin(), backend.signals.end(),
                     AVDTP_SIGNAL_CLOSE) == 0);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(!result.avdtp_close_accepted);
    CHECK(!result.remote_stream_cleanup_required);
    CHECK(result.consumer_lease_released);
}

void ContinuousCancelStopsWithoutBlindSignaling() {
    Backend backend;
    Source source;
    StopContext stop{&backend, 3u,
        native_ldac::agent::V1TransportPcmStopDisposition::Cancel};
    const auto options = ContinuousFidelityOptions();
    const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
        &backend, &source, options, Stop, &stop);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.target_duration_ms == 0u);
    CHECK(result.actual_duration_ms > 0u);
    CHECK(!result.completed_full_duration);
    CHECK(!result.ended_by_graceful_stop);
    CHECK(result.media_packets_written == 3u);
    CHECK(!result.avdtp_suspend_accepted);
    CHECK(!result.avdtp_close_accepted);
    CHECK(result.remote_stream_cleanup_required);
    CHECK(result.consumer_lease_released);
    CHECK(backend.signals.back() == AVDTP_SIGNAL_START);
}

}  // namespace

int main() {
    Happy();
    RequestedChannelModesReachNegotiation();
    PreStartSilentFramesAreTrackedSeparately();
    InvalidGainNeverPrepares();
    InvalidOutputPeakNeverPrepares();
    InvalidPreflightBoundNeverPrepares();
    InvalidLinkedLimiterConfigurationNeverPrepares();
    InvalidLongProfileBoundsNeverPrepare();
    LongProfileBoundsAreAccepted();
    InvalidContinuousProfileBoundsNeverPrepare();
    InvalidStopClassificationBoundNeverPrepares();
    InvalidPcmTimeoutToleranceNeverPrepares();
    OpenFailureDiagnosticsArePreserved();
    SuccessfulOpenDiagnosticsArePreserved();
    PreflightReacquiresAcrossEpoch();
    AudibleProfileIsStillHardBounded();
    UnityGainPreservesLowInput();
    UnityGainLimiterCapsFullScaleAndSanitizesNonFinite();
    LinkedLimiterReportsBoundedStereoReduction();
    FidelityBridgeTracksSentFadeAndStableVolume();
    SingleGainFidelityProfileIsValid();
    FidelityFixedCeilingGracefulStopIsBounded();
    FidelityUnityBoundaryPreservesFullScale();
    FidelityFixedCeilingRequiresFinalCeiling();
    FidelityBridgeStopsOnVolumeChange();
    DynamicVolumeAndMuteContinueStreaming();
    FidelityBridgeTreatsEpochChangeAsGracefulStop();
    FidelityBridgeNinetySixKhzCommitsAtomically();
    FidelityBridgeRechecksStopAfterPacing();
    QuietPcmNeverOpensBluetooth();
    RetryableOpenFailureReleasesConsumer();
    NegotiationMismatchCapturesSignalingHeaders();
    PeerDiscoverCollisionIsAnsweredAndNegotiationContinues();
    RepeatedPeerDiscoverCollisionFailsBounded();
    PeerDiscoveryAndCapabilitiesCollisionsContinue();
    PeerConfigurationCollisionIsRejectedAndNegotiationContinues();
    MalformedPeerConfigurationCollisionFailsBounded();
    RepeatedPeerConfigurationCollisionFailsBounded();
    PeerCloseDuringStreamingIsAcceptedAndStopsLocally();
    PeerCloseAfterPauseSuspendIsAcceptedAndStopsLocally();
    PeerSignalingReadTimeoutRearmsAndNormalStopContinues();
    MalformedPeerCloseDuringStreamingFailsBounded();
    WriteFailureStopsLocally();
    TransientNotReadyMediaWriteRetriesBounded();
    TransientPcmTimeoutRecoversWithFreshPacingBoundary();
    PcmTimeoutToleranceExhaustsBounded();
    TimeoutThenStreamStopUsesPauseSuspend();
    StreamStopAfterStartSuspendsAndCloses();
    PostStartEpochRebindContinuesSameTransport();
    PostStartRebindFailureRecordsPcmEvidence();
    PostStartObservedEpochRebindContinuesSameTransport();
    PostStartRebindFormatChangeStopsSafely();
    CancelWinsPostStartRebindWithoutSignaling();
    CancelWinsConcurrentStreamStopWithoutSignaling();
    DelayedCancelWinsStreamStopClassificationWithoutSignaling();
    UnclassifiedStreamStopFailsLocallyWithoutSignaling();
    GracefulStopSuspendsAndCloses();
    CancelStopsWithoutBlindSignaling();
    ContinuousGracefulStopSuspendsAndCloses();
    ContinuousCancelStopsWithoutBlindSignaling();
    ContinuousStartupSilenceIsTransportOnly();
    DailyExtendedFadeInIsSupported();
    TransientBoundaryResumeThenGracefulStopUsesResumeFade();
    PauseSuspendResumesSameTransportWithoutMediaPackets();
    PauseSuspendThenGracefulStopClosesWithoutResumeStart();
    RepeatedPauseSuspendCyclesReuseOneTransport();
    PauseSuspendThenCancelDoesNotSignalRemoteClose();
    if (failures != 0) return 1;
    std::puts("V1 bounded low-gain PCM transport session tests passed.");
    return 0;
}
