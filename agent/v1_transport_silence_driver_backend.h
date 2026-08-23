// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "v1_transport_driver_backend.h"
#include "v1_transport_silence_session.h"

namespace native_ldac::agent {

class V1TransportSilenceDriverBackend final
    : public V1TransportSilenceBackend {
public:
    explicit V1TransportSilenceDriverBackend(HANDLE cancel_event = nullptr)
        : backend_(cancel_event) {}
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
    bool WriteMedia(const std::uint8_t* packet,
                    std::size_t packet_size,
                    std::uint32_t timeout_ms,
                    std::uint32_t* error) override;
    bool GetLastMediaWriteDiagnostics(
        V1TransportMediaWriteDiagnostics* diagnostics) override;
    bool BeginPeerSignalingRead(std::uint32_t timeout_ms,
                                std::uint32_t* error) override;
    V1TransportSignalingReadDisposition PollPeerSignalingRead(
        std::uint8_t* packet,
        std::size_t packet_capacity,
        std::size_t* packet_size,
        std::uint32_t* error) override;
    bool SendPeerSignalingResponse(const std::uint8_t* packet,
                                   std::size_t packet_size,
                                   std::uint32_t timeout_ms,
                                   std::uint32_t* error) override;
    bool CancelPeerSignalingRead(std::uint32_t* error) override;
    bool CloseSignaling(std::uint32_t* error) override;
private:
    V1TransportDriverBackend backend_;
};

}  // namespace native_ldac::agent
