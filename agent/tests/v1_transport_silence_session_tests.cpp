// SPDX-License-Identifier: Apache-2.0
#include "v1_transport_silence_session.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#include "ldac_native/avdtp.h"

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", \
    __LINE__, #x); ++failures; } } while (0)

class Backend final : public native_ldac::agent::V1TransportSilenceBackend {
public:
    bool OpenSignaling(std::uint32_t, std::uint32_t* error) override {
        *error = 0u; return true;
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
            *error = 1u; return false;
        }
        signals.push_back(header.signal_id);
        std::array<std::uint8_t, 32> payload{};
        std::size_t size = 0u;
        if (header.signal_id == AVDTP_SIGNAL_DISCOVER) {
            payload[0] = 0x0Cu; payload[1] = 0x08u; size = 2u;
        } else if (header.signal_id == AVDTP_SIGNAL_GET_ALL_CAPABILITIES) {
            const std::uint8_t caps[] = {
                AVDTP_SERVICE_MEDIA_TRANSPORT, 0x00u,
                AVDTP_SERVICE_MEDIA_CODEC, 0x0Au,
                0x00u, AVDTP_CODEC_VENDOR,
                0x2Du, 0x01u, 0x00u, 0x00u,
                0xAAu, 0x00u, 0x3Cu, 0x07u};
            std::memcpy(payload.data(), caps, sizeof(caps)); size = sizeof(caps);
        }
        const auto written = avdtp_write_single(
            response, capacity, header.transaction_label,
            AVDTP_MESSAGE_ACCEPT, header.signal_id, payload.data(), size);
        *response_size = written; *error = 0u; return written != 0u;
    }
    bool OpenMedia(std::uint32_t, std::uint16_t mtu,
                   std::uint16_t* incoming, std::uint16_t* outgoing,
                   std::uint32_t* error) override {
        *incoming = incoming_mtu == 0u ? mtu : incoming_mtu;
        *outgoing = outgoing_mtu;
        *error = 0u;
        return true;
    }
    bool WriteMedia(const std::uint8_t* packet, std::size_t size,
                    std::uint32_t, std::uint32_t* error) override {
        ++write_attempts;
        if (fail_write_attempt != 0u &&
            write_attempts == fail_write_attempt) {
            *error = 29u;
            return false;
        }
        if (packet == nullptr || size > outgoing_mtu || size < 14u) {
            *error = 2u; return false;
        }
        packets.emplace_back(packet, packet + size);
        if (cancel_after_write != 0u &&
            packets.size() == cancel_after_write) {
            cancelled = true;
        }
        *error = 0u; return true;
    }
    bool BeginPeerSignalingRead(std::uint32_t,
                                std::uint32_t* error) override {
        *error = 1u; return false;
    }
    native_ldac::agent::V1TransportSignalingReadDisposition
    PollPeerSignalingRead(std::uint8_t*, std::size_t, std::size_t*,
                          std::uint32_t* error) override {
        *error = 1u;
        return native_ldac::agent::
            V1TransportSignalingReadDisposition::Failure;
    }
    bool SendPeerSignalingResponse(const std::uint8_t*, std::size_t,
                                   std::uint32_t,
                                   std::uint32_t* error) override {
        *error = 1u; return false;
    }
    bool CancelPeerSignalingRead(std::uint32_t* error) override {
        *error = 0u; return true;
    }
    bool CloseSignaling(std::uint32_t* error) override {
        *error = 0u; return true;
    }
    std::vector<std::uint8_t> signals;
    std::vector<std::vector<std::uint8_t>> packets;
    std::size_t write_attempts = 0u;
    std::size_t fail_write_attempt = 0u;
    std::size_t cancel_after_write = 0u;
    std::uint16_t incoming_mtu = 0u;
    std::uint16_t outgoing_mtu = 895u;
    bool cancelled = false;
};

bool Cancel(void* context) {
    return static_cast<Backend*>(context)->cancelled;
}

void Happy() {
    Backend backend;
    native_ldac::agent::V1TransportSilenceOptions options;
    const auto result = native_ldac::agent::RunV1TransportSilenceBurstOnce(
        &backend, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.remote_seid == 3u);
    CHECK(result.configuration.sample_rate == LDAC_SF_96000);
    CHECK(result.avdtp_start_accepted);
    CHECK(result.media_packets_written == 4u);
    CHECK(result.avdtp_suspend_accepted);
    CHECK(result.avdtp_close_accepted);
    CHECK(!result.remote_stream_cleanup_required);
    CHECK(result.close_succeeded);
    CHECK(backend.packets.size() == 4u);
    CHECK(backend.signals.size() == 7u);
    CHECK(backend.signals[4] == AVDTP_SIGNAL_START);
    CHECK(backend.signals[5] == AVDTP_SIGNAL_SUSPEND);
    CHECK(backend.signals[6] == AVDTP_SIGNAL_CLOSE);
    for (const auto& packet : backend.packets) {
        CHECK((packet[0] & 0xC0u) == 0x80u);
        CHECK(packet.size() <= result.outgoing_mtu);
    }
}

void Invalid() {
    Backend backend;
    native_ldac::agent::V1TransportSilenceOptions options;
    options.packet_limit = 5u;
    const auto result = native_ldac::agent::RunV1TransportSilenceBurstOnce(
        &backend, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::InvalidConfiguration);
    CHECK(backend.signals.empty());
}

void OnePacketLimit() {
    Backend backend;
    native_ldac::agent::V1TransportSilenceOptions options;
    options.packet_limit = 1u;
    const auto result = native_ldac::agent::RunV1TransportSilenceBurstOnce(
        &backend, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.media_packets_written == 1u);
    CHECK(backend.write_attempts == 1u);
}

void CancelAfterStartDoesNotSignalBlindly() {
    Backend backend;
    backend.cancel_after_write = 1u;
    native_ldac::agent::V1TransportSilenceOptions options;
    const auto result = native_ldac::agent::RunV1TransportSilenceBurstOnce(
        &backend, options, Cancel, &backend);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::Cancelled);
    CHECK(result.media_packets_written == 1u);
    CHECK(result.remote_stream_cleanup_required);
    CHECK(result.close_succeeded);
    CHECK(backend.signals.back() == AVDTP_SIGNAL_START);
}

void WriteFailureStopsBeforeFifthPacket() {
    Backend backend;
    backend.fail_write_attempt = 3u;
    native_ldac::agent::V1TransportSilenceOptions options;
    const auto result = native_ldac::agent::RunV1TransportSilenceBurstOnce(
        &backend, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::BackendFailure);
    CHECK(result.backend_error == 29u);
    CHECK(result.media_packets_written == 2u);
    CHECK(backend.write_attempts == 3u);
    CHECK(result.remote_stream_cleanup_required);
    CHECK(backend.signals.back() == AVDTP_SIGNAL_START);
}

void TinyMtuNeverStarts() {
    Backend backend;
    backend.outgoing_mtu = 100u;
    native_ldac::agent::V1TransportSilenceOptions options;
    const auto result = native_ldac::agent::RunV1TransportSilenceBurstOnce(
        &backend, options);
    CHECK(result.disposition == native_ldac::agent::
        V1TransportConfigurationDisposition::ProtocolFailure);
    CHECK(!result.avdtp_start_accepted);
    CHECK(!result.remote_stream_cleanup_required);
    CHECK(backend.signals.back() == AVDTP_SIGNAL_OPEN);
}
}  // namespace

int main() {
    Happy();
    Invalid();
    OnePacketLimit();
    CancelAfterStartDoesNotSignalBlindly();
    WriteFailureStopsBeforeFifthPacket();
    TinyMtuNeverStarts();
    if (failures != 0) return 1;
    std::puts("V1 transport silence-burst session tests passed.");
    return 0;
}
