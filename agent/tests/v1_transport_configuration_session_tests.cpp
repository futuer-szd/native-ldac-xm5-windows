// SPDX-License-Identifier: Apache-2.0
#include "v1_transport_configuration_session.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#include "ldac_native/avdtp.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n",                       \
                         __FILE__, __LINE__, #condition);                      \
            ++failures;                                                       \
        }                                                                      \
    } while (0)

enum class Scenario {
    Happy,
    RejectConfiguration,
    FailMediaOpen,
    RejectClose,
    FailLocalClose,
};

class MockBackend final
    : public native_ldac::agent::V1TransportConfigurationBackend {
public:
    explicit MockBackend(Scenario scenario) : scenario_(scenario) {}

    bool OpenSignaling(std::uint32_t, std::uint32_t* error) override {
        ++open_calls;
        opened = true;
        *error = 0u;
        return true;
    }

    bool ExchangeSignaling(const std::uint8_t* request,
                           std::size_t request_size,
                           std::uint8_t* response,
                           std::size_t response_capacity,
                           std::size_t* response_size,
                           std::uint32_t,
                           std::uint32_t* error) override {
        avdtp_header header = {};
        if (!opened ||
            avdtp_parse_header(request, request_size, &header) != AVDTP_OK) {
            *error = 1001u;
            return false;
        }
        signals.push_back(header.signal_id);
        std::array<std::uint8_t, 32> payload = {};
        std::size_t payload_size = 0u;
        avdtp_message_type message = AVDTP_MESSAGE_ACCEPT;
        if (header.signal_id == AVDTP_SIGNAL_DISCOVER) {
            payload[0] = 0x0Cu;
            payload[1] = 0x08u;
            payload_size = 2u;
        } else if (header.signal_id ==
                   AVDTP_SIGNAL_GET_ALL_CAPABILITIES) {
            const std::uint8_t capabilities[] = {
                AVDTP_SERVICE_MEDIA_TRANSPORT, 0x00u,
                AVDTP_SERVICE_MEDIA_CODEC, 0x0Au,
                0x00u, AVDTP_CODEC_VENDOR,
                0x2Du, 0x01u, 0x00u, 0x00u,
                0xAAu, 0x00u, 0x3Cu, 0x07u,
            };
            std::memcpy(payload.data(), capabilities, sizeof(capabilities));
            payload_size = sizeof(capabilities);
        } else if (header.signal_id == AVDTP_SIGNAL_SET_CONFIGURATION &&
                   scenario_ == Scenario::RejectConfiguration) {
            message = AVDTP_MESSAGE_REJECT;
            payload[0] = 0x29u;
            payload_size = 1u;
        } else if (header.signal_id == AVDTP_SIGNAL_CLOSE &&
                   scenario_ == Scenario::RejectClose) {
            message = AVDTP_MESSAGE_REJECT;
            payload[0] = 0x31u;
            payload_size = 1u;
        } else if (header.signal_id != AVDTP_SIGNAL_SET_CONFIGURATION &&
                   header.signal_id != AVDTP_SIGNAL_OPEN &&
                   header.signal_id != AVDTP_SIGNAL_CLOSE) {
            *error = 1002u;
            return false;
        }
        const std::size_t written = avdtp_write_single(
            response,
            response_capacity,
            header.transaction_label,
            message,
            header.signal_id,
            payload.data(),
            payload_size);
        if (written == 0u) {
            *error = 1003u;
            return false;
        }
        *response_size = written;
        *error = 0u;
        return true;
    }

    bool OpenMedia(std::uint32_t,
                   std::uint16_t preferred_mtu,
                   std::uint16_t* incoming_mtu,
                   std::uint16_t* outgoing_mtu,
                   std::uint32_t* error) override {
        ++media_open_calls;
        if (scenario_ == Scenario::FailMediaOpen) {
            *error = 1004u;
            return false;
        }
        *incoming_mtu = preferred_mtu;
        *outgoing_mtu = 895u;
        *error = 0u;
        return true;
    }

    bool CloseSignaling(std::uint32_t* error) override {
        ++close_calls;
        opened = false;
        if (scenario_ == Scenario::FailLocalClose) {
            *error = 1005u;
            return false;
        }
        *error = 0u;
        return true;
    }

    Scenario scenario_;
    bool opened = false;
    unsigned open_calls = 0u;
    unsigned media_open_calls = 0u;
    unsigned close_calls = 0u;
    std::vector<std::uint8_t> signals;
};

void CheckHappy() {
    MockBackend backend(Scenario::Happy);
    native_ldac::agent::V1TransportConfigurationOptions options;
    const auto result =
        native_ldac::agent::RunV1TransportConfigurationOnce(
            &backend, options);
    using native_ldac::agent::V1TransportConfigurationDisposition;
    CHECK(result.disposition ==
          V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.primary_disposition == result.disposition);
    CHECK(result.remote_seid == 3u);
    CHECK(result.configuration.sample_rate == LDAC_SF_96000);
    CHECK(result.configuration.channel_mode == LDAC_CM_STEREO);
    CHECK(result.set_configuration_accepted);
    CHECK(result.avdtp_open_accepted);
    CHECK(result.media_opened);
    CHECK(result.outgoing_mtu == 895u);
    CHECK(result.avdtp_close_accepted);
    CHECK(result.close_attempted && result.close_succeeded);
    CHECK(backend.open_calls == 1u);
    CHECK(backend.media_open_calls == 1u);
    CHECK(backend.close_calls == 1u);
    CHECK(backend.signals.size() == 5u);
    CHECK(backend.signals[0] == AVDTP_SIGNAL_DISCOVER);
    CHECK(backend.signals[1] == AVDTP_SIGNAL_GET_ALL_CAPABILITIES);
    CHECK(backend.signals[2] == AVDTP_SIGNAL_SET_CONFIGURATION);
    CHECK(backend.signals[3] == AVDTP_SIGNAL_OPEN);
    CHECK(backend.signals[4] == AVDTP_SIGNAL_CLOSE);
    for (const auto signal : backend.signals) {
        CHECK(signal != AVDTP_SIGNAL_START);
        CHECK(signal != AVDTP_SIGNAL_SUSPEND);
    }
}

void CheckFailures() {
    using native_ldac::agent::V1TransportConfigurationDisposition;
    using native_ldac::agent::V1TransportConfigurationStage;
    native_ldac::agent::V1TransportConfigurationOptions options;

    MockBackend reject_config(Scenario::RejectConfiguration);
    auto result = native_ldac::agent::RunV1TransportConfigurationOnce(
        &reject_config, options);
    CHECK(result.disposition ==
          V1TransportConfigurationDisposition::ProtocolFailure);
    CHECK(result.stage == V1TransportConfigurationStage::SetConfiguration);
    CHECK(result.protocol_error == 0x29);
    CHECK(reject_config.media_open_calls == 0u);
    CHECK(reject_config.close_calls == 1u);

    MockBackend fail_media(Scenario::FailMediaOpen);
    result = native_ldac::agent::RunV1TransportConfigurationOnce(
        &fail_media, options);
    CHECK(result.disposition ==
          V1TransportConfigurationDisposition::BackendFailure);
    CHECK(result.stage == V1TransportConfigurationStage::OpenMedia);
    CHECK(result.backend_error == 1004u);
    CHECK(!result.media_opened);
    CHECK(fail_media.close_calls == 1u);

    MockBackend reject_close(Scenario::RejectClose);
    result = native_ldac::agent::RunV1TransportConfigurationOnce(
        &reject_close, options);
    CHECK(result.disposition ==
          V1TransportConfigurationDisposition::ProtocolFailure);
    CHECK(result.stage == V1TransportConfigurationStage::AvdtpClose);
    CHECK(result.protocol_error == 0x31);
    CHECK(reject_close.close_calls == 1u);

    MockBackend fail_local_close(Scenario::FailLocalClose);
    result = native_ldac::agent::RunV1TransportConfigurationOnce(
        &fail_local_close, options);
    CHECK(result.disposition ==
          V1TransportConfigurationDisposition::CleanupFailure);
    CHECK(result.primary_disposition ==
          V1TransportConfigurationDisposition::Succeeded);
    CHECK(result.stage == V1TransportConfigurationStage::CloseChannels);
    CHECK(result.cleanup_error == 1005u);
}

void CheckInvalidOptions() {
    MockBackend backend(Scenario::Happy);
    native_ldac::agent::V1TransportConfigurationOptions options;
    options.preferred_media_mtu = 0u;
    const auto result =
        native_ldac::agent::RunV1TransportConfigurationOnce(
            &backend, options);
    CHECK(result.disposition == native_ldac::agent::
          V1TransportConfigurationDisposition::InvalidConfiguration);
    CHECK(backend.open_calls == 0u);
}

}  // namespace

int main() {
    CheckHappy();
    CheckFailures();
    CheckInvalidOptions();
    if (failures != 0) {
        std::fprintf(stderr,
                     "%d V1 transport configuration test(s) failed.\n",
                     failures);
        return 1;
    }
    std::puts("V1 transport configuration session tests passed.");
    return 0;
}
