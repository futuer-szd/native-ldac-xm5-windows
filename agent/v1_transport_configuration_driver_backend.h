// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "v1_transport_configuration_session.h"
#include "v1_transport_driver_backend.h"

namespace native_ldac::agent {

class V1TransportConfigurationDriverBackend final
    : public V1TransportConfigurationBackend {
public:
    explicit V1TransportConfigurationDriverBackend(
        HANDLE cancel_event = nullptr);

    bool OpenSignaling(std::uint32_t timeout_ms,
                       std::uint32_t* error) override;
    bool GetLastOpenDiagnostics(
        V1TransportOpenDiagnostics* diagnostics) const override;
    bool ExchangeSignaling(const std::uint8_t* request,
                           std::size_t request_size,
                           std::uint8_t* response,
                           std::size_t response_capacity,
                           std::size_t* response_size,
                           std::uint32_t timeout_ms,
                           std::uint32_t* error) override;
    bool OpenMedia(std::uint32_t timeout_ms,
                   std::uint16_t preferred_mtu,
                   std::uint16_t* incoming_mtu,
                   std::uint16_t* outgoing_mtu,
                   std::uint32_t* error) override;
    bool CloseSignaling(std::uint32_t* error) override;

private:
    V1TransportDriverBackend backend_;
};

}  // namespace native_ldac::agent
