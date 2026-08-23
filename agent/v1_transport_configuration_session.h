// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "v1_transport_session.h"

namespace native_ldac::agent {

enum class V1TransportConfigurationDisposition : std::uint32_t {
    Succeeded = 0,
    Cancelled,
    InvalidConfiguration,
    BackendFailure,
    ProtocolFailure,
    CleanupFailure,
};

enum class V1TransportConfigurationStage : std::uint32_t {
    None = 0,
    OpenSignaling,
    DiscoverCapabilities,
    SetConfiguration,
    AvdtpOpen,
    OpenMedia,
    AvdtpClose,
    CloseChannels,
};

struct V1TransportConfigurationOptions {
    std::uint32_t open_timeout_ms = 10000u;
    std::uint32_t exchange_timeout_ms = 5000u;
    std::uint32_t media_open_timeout_ms = 10000u;
    std::uint16_t preferred_media_mtu = 1000u;
    std::uint8_t local_seid = 1u;
    unsigned preferred_sample_rate_hz = 96000u;
    ldac_capabilities local_capabilities = {
        LDAC_SF_ALL,
        LDAC_CM_STEREO,
    };
};

struct V1TransportConfigurationResult {
    V1TransportConfigurationDisposition disposition =
        V1TransportConfigurationDisposition::InvalidConfiguration;
    V1TransportConfigurationDisposition primary_disposition =
        V1TransportConfigurationDisposition::InvalidConfiguration;
    V1TransportConfigurationStage stage =
        V1TransportConfigurationStage::None;
    std::uint32_t backend_error = 0u;
    std::uint32_t cleanup_error = 0u;
    std::int32_t protocol_error = 0;
    std::uint8_t remote_seid = 0u;
    ldac_configuration configuration = {};
    std::uint32_t open_attempts = 0u;
    std::uint32_t signaling_exchanges = 0u;
    std::uint16_t incoming_mtu = 0u;
    std::uint16_t outgoing_mtu = 0u;
    bool signaling_opened = false;
    bool set_configuration_accepted = false;
    bool avdtp_open_accepted = false;
    bool media_opened = false;
    bool avdtp_close_accepted = false;
    bool close_attempted = false;
    bool close_succeeded = false;
};

class V1TransportConfigurationBackend
    : public V1TransportDiscoveryBackend {
public:
    virtual bool OpenMedia(std::uint32_t timeout_ms,
                           std::uint16_t preferred_mtu,
                           std::uint16_t* incoming_mtu,
                           std::uint16_t* outgoing_mtu,
                           std::uint32_t* error) = 0;
};

V1TransportConfigurationResult RunV1TransportConfigurationOnce(
    V1TransportConfigurationBackend* backend,
    const V1TransportConfigurationOptions& options,
    V1TransportCancelProbe cancel_probe = nullptr,
    void* cancel_context = nullptr);

}  // namespace native_ldac::agent
