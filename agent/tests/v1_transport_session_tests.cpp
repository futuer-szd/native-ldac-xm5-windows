// SPDX-License-Identifier: Apache-2.0
#include "v1_transport_session.h"

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
            std::fprintf(stderr, "FAIL %s:%d: %s\n",                         \
                         __FILE__, __LINE__, #condition);                      \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

enum class Scenario {
    Happy,
    LegacyFallback,
    NoLdac,
    WrongLabel,
    RejectDiscover,
    FailOpen,
    FailSecondExchange,
    FailClose,
    CancelDuringSecondExchange,
};

struct CancelState {
    bool cancelled = false;
};

class MockBackend final
    : public native_ldac::agent::V1TransportDiscoveryBackend {
public:
    explicit MockBackend(Scenario scenario,
                         CancelState* cancel_state = nullptr)
        : scenario_(scenario), cancel_state_(cancel_state) {}

    bool OpenSignaling(std::uint32_t timeout_ms,
                       std::uint32_t* error) override {
        ++open_calls;
        last_open_timeout_ms = timeout_ms;
        if (scenario_ == Scenario::FailOpen) {
            *error = 1001u;
            return false;
        }
        opened = true;
        *error = 0u;
        return true;
    }

    bool ExchangeSignaling(const std::uint8_t* request,
                           std::size_t request_size,
                           std::uint8_t* response,
                           std::size_t response_capacity,
                           std::size_t* response_size,
                           std::uint32_t timeout_ms,
                           std::uint32_t* error) override {
        ++exchange_calls;
        last_exchange_timeout_ms = timeout_ms;
        avdtp_header header = {};
        if (!opened ||
            avdtp_parse_header(request, request_size, &header) != AVDTP_OK) {
            *error = 1002u;
            return false;
        }
        signals.push_back(header.signal_id);
        if (scenario_ == Scenario::FailSecondExchange &&
            exchange_calls == 2u) {
            *error = 1003u;
            return false;
        }
        if (scenario_ == Scenario::CancelDuringSecondExchange &&
            exchange_calls == 2u) {
            if (cancel_state_ != nullptr) {
                cancel_state_->cancelled = true;
            }
            *error = 1223u;
            return false;
        }

        std::array<std::uint8_t, 64> payload = {};
        std::size_t payload_size = 0u;
        avdtp_message_type message = AVDTP_MESSAGE_ACCEPT;
        if (header.signal_id == AVDTP_SIGNAL_DISCOVER) {
            if (scenario_ == Scenario::RejectDiscover) {
                message = AVDTP_MESSAGE_REJECT;
                payload[0] = 0x31u;
                payload_size = 1u;
            } else {
                payload[0] = 0x04u;
                payload[1] = 0x08u;
                payload[2] = 0x0Cu;
                payload[3] = 0x08u;
                payload_size = 4u;
            }
        } else if (header.signal_id ==
                   AVDTP_SIGNAL_GET_ALL_CAPABILITIES) {
            if (scenario_ == Scenario::LegacyFallback) {
                message = AVDTP_MESSAGE_REJECT;
                payload[0] = 0x19u;
                payload_size = 1u;
            } else {
                const bool first_sink = request_size > header.payload_offset &&
                    (request[header.payload_offset] >> 2u) == 1u;
                BuildCapabilities(first_sink, payload.data(),
                                  &payload_size);
            }
        } else if (header.signal_id == AVDTP_SIGNAL_GET_CAPABILITIES) {
            const bool first_sink = request_size > header.payload_offset &&
                (request[header.payload_offset] >> 2u) == 1u;
            BuildCapabilities(first_sink, payload.data(), &payload_size);
        } else {
            *error = 1004u;
            return false;
        }

        std::uint8_t label = header.transaction_label;
        if (scenario_ == Scenario::WrongLabel) {
            label = static_cast<std::uint8_t>((label + 1u) & 0x0Fu);
        }
        const std::size_t written = avdtp_write_single(
            response,
            response_capacity,
            label,
            message,
            header.signal_id,
            payload.data(),
            payload_size);
        if (written == 0u) {
            *error = 1005u;
            return false;
        }
        *response_size = written;
        *error = 0u;
        return true;
    }

    bool CloseSignaling(std::uint32_t* error) override {
        ++close_calls;
        opened = false;
        if (scenario_ == Scenario::FailClose) {
            *error = 1006u;
            return false;
        }
        *error = 0u;
        return true;
    }

    void BuildCapabilities(bool first_sink,
                           std::uint8_t* payload,
                           std::size_t* payload_size) const {
        if (first_sink || scenario_ == Scenario::NoLdac) {
            const std::uint8_t sbc[] = {
                AVDTP_SERVICE_MEDIA_TRANSPORT, 0x00u,
                AVDTP_SERVICE_MEDIA_CODEC, 0x06u,
                0x00u, 0x00u, 0x3Fu, 0xFFu, 0x02u, 0x35u,
            };
            std::memcpy(payload, sbc, sizeof(sbc));
            *payload_size = sizeof(sbc);
            return;
        }
        const std::uint8_t ldac[] = {
            AVDTP_SERVICE_MEDIA_TRANSPORT, 0x00u,
            AVDTP_SERVICE_MEDIA_CODEC, 0x0Au,
            0x00u, AVDTP_CODEC_VENDOR,
            0x2Du, 0x01u, 0x00u, 0x00u,
            0xAAu, 0x00u, 0x3Cu, 0x07u,
        };
        std::memcpy(payload, ldac, sizeof(ldac));
        *payload_size = sizeof(ldac);
    }

    Scenario scenario_;
    CancelState* cancel_state_ = nullptr;
    bool opened = false;
    std::uint32_t open_calls = 0u;
    std::uint32_t exchange_calls = 0u;
    std::uint32_t close_calls = 0u;
    std::uint32_t last_open_timeout_ms = 0u;
    std::uint32_t last_exchange_timeout_ms = 0u;
    std::vector<std::uint8_t> signals;
};

bool IsCancelled(void* context) {
    return static_cast<CancelState*>(context)->cancelled;
}

void CheckHappyPath() {
    MockBackend backend(Scenario::Happy);
    native_ldac::agent::V1TransportDiscoveryOptions options;
    const auto result = native_ldac::agent::RunV1TransportDiscoveryOnce(
        &backend, options);
    CHECK(result.disposition ==
          native_ldac::agent::V1TransportDiscoveryDisposition::Succeeded);
    CHECK(result.primary_disposition == result.disposition);
    CHECK(result.remote_seid == 3u);
    CHECK(result.configuration.sample_rate == LDAC_SF_96000);
    CHECK(result.configuration.channel_mode == LDAC_CM_STEREO);
    CHECK(result.open_attempts == 1u);
    CHECK(result.signaling_exchanges == 3u);
    CHECK(result.sink_candidates == 2u);
    CHECK(result.legacy_capability_fallbacks == 0u);
    CHECK(result.close_attempted && result.close_succeeded);
    CHECK(backend.open_calls == 1u);
    CHECK(backend.close_calls == 1u);
    CHECK(backend.signals.size() == 3u);
    CHECK(backend.signals[0] == AVDTP_SIGNAL_DISCOVER);
    CHECK(backend.signals[1] == AVDTP_SIGNAL_GET_ALL_CAPABILITIES);
    CHECK(backend.signals[2] == AVDTP_SIGNAL_GET_ALL_CAPABILITIES);
}

void CheckLegacyFallback() {
    MockBackend backend(Scenario::LegacyFallback);
    native_ldac::agent::V1TransportDiscoveryOptions options;
    const auto result = native_ldac::agent::RunV1TransportDiscoveryOnce(
        &backend, options);
    CHECK(result.disposition ==
          native_ldac::agent::V1TransportDiscoveryDisposition::Succeeded);
    CHECK(result.legacy_capability_fallbacks == 2u);
    CHECK(result.signaling_exchanges == 5u);
    CHECK(result.remote_seid == 3u);
    CHECK(backend.close_calls == 1u);
}

void CheckNoLdac() {
    MockBackend backend(Scenario::NoLdac);
    native_ldac::agent::V1TransportDiscoveryOptions options;
    const auto result = native_ldac::agent::RunV1TransportDiscoveryOnce(
        &backend, options);
    CHECK(result.disposition ==
          native_ldac::agent::V1TransportDiscoveryDisposition::ProtocolFailure);
    CHECK(result.protocol_error ==
          native_ldac::agent::V1TransportDiscoveryProtocolError::LdacNotSupported);
    CHECK(backend.open_calls == 1u);
    CHECK(backend.close_calls == 1u);
}

void CheckProtocolFailures() {
    MockBackend wrong_label(Scenario::WrongLabel);
    native_ldac::agent::V1TransportDiscoveryOptions options;
    auto result = native_ldac::agent::RunV1TransportDiscoveryOnce(
        &wrong_label, options);
    CHECK(result.disposition ==
          native_ldac::agent::V1TransportDiscoveryDisposition::ProtocolFailure);
    CHECK(result.protocol_error ==
          native_ldac::agent::V1TransportDiscoveryProtocolError::InvalidResponse);
    CHECK(wrong_label.close_calls == 1u);

    MockBackend rejected(Scenario::RejectDiscover);
    result = native_ldac::agent::RunV1TransportDiscoveryOnce(
        &rejected, options);
    CHECK(result.disposition ==
          native_ldac::agent::V1TransportDiscoveryDisposition::ProtocolFailure);
    CHECK(result.protocol_error ==
          native_ldac::agent::V1TransportDiscoveryProtocolError::RemoteRejected);
    CHECK(result.remote_reject_error == 0x31u);
    CHECK(rejected.close_calls == 1u);
}

void CheckBackendFailures() {
    native_ldac::agent::V1TransportDiscoveryOptions options;
    MockBackend fail_open(Scenario::FailOpen);
    auto result = native_ldac::agent::RunV1TransportDiscoveryOnce(
        &fail_open, options);
    CHECK(result.disposition ==
          native_ldac::agent::V1TransportDiscoveryDisposition::BackendFailure);
    CHECK(result.backend_error == 1001u);
    CHECK(fail_open.open_calls == 1u);
    CHECK(fail_open.close_calls == 0u);

    MockBackend fail_exchange(Scenario::FailSecondExchange);
    result = native_ldac::agent::RunV1TransportDiscoveryOnce(
        &fail_exchange, options);
    CHECK(result.disposition ==
          native_ldac::agent::V1TransportDiscoveryDisposition::BackendFailure);
    CHECK(result.backend_error == 1003u);
    CHECK(fail_exchange.open_calls == 1u);
    CHECK(fail_exchange.close_calls == 1u);

    MockBackend fail_close(Scenario::FailClose);
    result = native_ldac::agent::RunV1TransportDiscoveryOnce(
        &fail_close, options);
    CHECK(result.disposition ==
          native_ldac::agent::V1TransportDiscoveryDisposition::CleanupFailure);
    CHECK(result.primary_disposition ==
          native_ldac::agent::V1TransportDiscoveryDisposition::Succeeded);
    CHECK(result.cleanup_error == 1006u);
    CHECK(fail_close.close_calls == 1u);
}

void CheckCancellationAndValidation() {
    native_ldac::agent::V1TransportDiscoveryOptions options;
    CancelState cancel{true};
    MockBackend backend(Scenario::Happy);
    auto result = native_ldac::agent::RunV1TransportDiscoveryOnce(
        &backend, options, IsCancelled, &cancel);
    CHECK(result.disposition ==
          native_ldac::agent::V1TransportDiscoveryDisposition::Cancelled);
    CHECK(backend.open_calls == 0u);
    CHECK(backend.close_calls == 0u);

    cancel.cancelled = false;
    MockBackend cancelled_io(
        Scenario::CancelDuringSecondExchange, &cancel);
    result = native_ldac::agent::RunV1TransportDiscoveryOnce(
        &cancelled_io, options, IsCancelled, &cancel);
    CHECK(result.disposition ==
          native_ldac::agent::V1TransportDiscoveryDisposition::Cancelled);
    CHECK(result.primary_disposition == result.disposition);
    CHECK(result.backend_error == 1223u);
    CHECK(cancelled_io.open_calls == 1u);
    CHECK(cancelled_io.exchange_calls == 2u);
    CHECK(cancelled_io.close_calls == 1u);

    options.open_timeout_ms = 0u;
    result = native_ldac::agent::RunV1TransportDiscoveryOnce(
        &backend, options);
    CHECK(result.disposition ==
          native_ldac::agent::V1TransportDiscoveryDisposition::InvalidConfiguration);
    CHECK(backend.open_calls == 0u);
}

}  // namespace

int main() {
    CheckHappyPath();
    CheckLegacyFallback();
    CheckNoLdac();
    CheckProtocolFailures();
    CheckBackendFailures();
    CheckCancellationAndValidation();
    if (failures != 0) {
        std::fprintf(stderr, "%d V1 transport session test(s) failed.\n",
                     failures);
        return 1;
    }
    std::puts("V1 transport discovery session tests passed.");
    return 0;
}
