// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_AGENT_V1_TRANSPORT_SESSION_H_
#define NATIVE_LDAC_AGENT_V1_TRANSPORT_SESSION_H_

#include <cstddef>
#include <cstdint>

#include "ldac_native/ldac_codec.h"

namespace native_ldac::agent {

enum class V1TransportSignalingReadDisposition : std::uint32_t {
    NoPacket = 0,
    Packet,
    TimedOut,
    Failure,
};

enum class V1TransportDiscoveryDisposition : std::uint32_t {
    Succeeded = 0,
    Cancelled,
    InvalidConfiguration,
    BackendFailure,
    ProtocolFailure,
    CleanupFailure,
};

enum class V1TransportDiscoveryStage : std::uint32_t {
    None = 0,
    OpenSignaling,
    Discover,
    GetCapabilities,
    CloseSignaling,
};

enum class V1TransportDiscoveryProtocolError : std::uint32_t {
    None = 0,
    InvalidResponse,
    RemoteRejected,
    NoAudioSink,
    LdacNotSupported,
    NoCommonConfiguration,
};

struct V1TransportDiscoveryOptions {
    std::uint32_t open_timeout_ms = 10000u;
    std::uint32_t exchange_timeout_ms = 5000u;
    unsigned preferred_sample_rate_hz = 96000u;
    ldac_capabilities local_capabilities = {
        LDAC_SF_ALL,
        LDAC_CM_STEREO,
    };
};

struct V1TransportDiscoveryResult {
    V1TransportDiscoveryDisposition disposition =
        V1TransportDiscoveryDisposition::InvalidConfiguration;
    V1TransportDiscoveryDisposition primary_disposition =
        V1TransportDiscoveryDisposition::InvalidConfiguration;
    V1TransportDiscoveryStage stage = V1TransportDiscoveryStage::None;
    V1TransportDiscoveryProtocolError protocol_error =
        V1TransportDiscoveryProtocolError::None;
    std::uint32_t backend_error = 0u;
    std::uint32_t cleanup_error = 0u;
    std::uint8_t remote_reject_error = 0u;
    std::uint8_t remote_seid = 0u;
    ldac_capabilities remote_capabilities = {};
    ldac_configuration configuration = {};
    std::uint32_t open_attempts = 0u;
    std::uint32_t signaling_exchanges = 0u;
    std::uint32_t sink_candidates = 0u;
    std::uint32_t legacy_capability_fallbacks = 0u;
    bool signaling_opened = false;
    bool close_attempted = false;
    bool close_succeeded = false;
};

struct V1TransportOpenDiagnostics {
    std::uint32_t query_attempts = 0u;
    std::uint32_t query_error = 0u;
    std::uint32_t query_bytes = 0u;
    std::uint32_t sequence = 0u;
    std::uint32_t operation = 0u;
    std::int32_t io_status = 0;
    std::int32_t brb_status = 0;
    std::uint32_t bluetooth_status = 0u;
    std::uint64_t remote_bluetooth_address = 0u;
    std::uint32_t channel_flags = 0u;
    std::uint32_t flags = 0u;
    std::uint16_t psm = 0u;
    std::uint16_t response = 0u;
    std::uint16_t response_status = 0u;
    bool available = false;
    bool remote_response_valid = false;
};

class V1TransportDiscoveryBackend {
public:
    virtual ~V1TransportDiscoveryBackend() = default;

    virtual bool OpenSignaling(std::uint32_t timeout_ms,
                               std::uint32_t* error) = 0;

    virtual bool GetLastOpenDiagnostics(
        V1TransportOpenDiagnostics* diagnostics) const {
        if (diagnostics != nullptr) {
            *diagnostics = {};
        }
        return false;
    }

    virtual bool ExchangeSignaling(const std::uint8_t* request,
                                   std::size_t request_size,
                                   std::uint8_t* response,
                                   std::size_t response_capacity,
                                   std::size_t* response_size,
                                   std::uint32_t timeout_ms,
                                   std::uint32_t* error) = 0;

    virtual bool CloseSignaling(std::uint32_t* error) = 0;
};

using V1TransportCancelProbe = bool (*)(void* context);

V1TransportDiscoveryResult RunV1TransportDiscoveryOnce(
    V1TransportDiscoveryBackend* backend,
    const V1TransportDiscoveryOptions& options,
    V1TransportCancelProbe cancel_probe = nullptr,
    void* cancel_context = nullptr);

}  // namespace native_ldac::agent

#endif  // NATIVE_LDAC_AGENT_V1_TRANSPORT_SESSION_H_
