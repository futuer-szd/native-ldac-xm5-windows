// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_AGENT_V1_TRANSPORT_DRIVER_BACKEND_H_
#define NATIVE_LDAC_AGENT_V1_TRANSPORT_DRIVER_BACKEND_H_

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "v1_transport_session.h"

namespace native_ldac::agent {

struct V1TransportMediaWriteDiagnostics;

// A single-use ABI 0.5 signaling backend. It never retries OPEN, never opens
// the media channel, and never sends AVDTP commands on its own.
class V1TransportDriverBackend final
    : public V1TransportDiscoveryBackend {
public:
    explicit V1TransportDriverBackend(HANDLE cancel_event = nullptr);
    ~V1TransportDriverBackend() override;

    V1TransportDriverBackend(const V1TransportDriverBackend&) = delete;
    V1TransportDriverBackend& operator=(
        const V1TransportDriverBackend&) = delete;

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

    bool CloseSignaling(std::uint32_t* error) override;

    bool OpenMedia(std::uint32_t timeout_ms,
                   std::uint16_t preferred_mtu,
                   std::uint16_t* incoming_mtu,
                   std::uint16_t* outgoing_mtu,
                   std::uint32_t* error);

    bool WriteMedia(const std::uint8_t* packet,
                    std::size_t packet_size,
                    std::uint32_t timeout_ms,
                    std::uint32_t* error);
    bool GetLastMediaWriteDiagnostics(
        V1TransportMediaWriteDiagnostics* diagnostics);
    bool BeginPeerSignalingRead(std::uint32_t timeout_ms,
                                std::uint32_t* error);
    V1TransportSignalingReadDisposition PollPeerSignalingRead(
        std::uint8_t* packet,
        std::size_t packet_capacity,
        std::size_t* packet_size,
        std::uint32_t* error);
    bool SendPeerSignalingResponse(const std::uint8_t* packet,
                                   std::size_t packet_size,
                                   std::uint32_t timeout_ms,
                                   std::uint32_t* error);
    bool CancelPeerSignalingRead(std::uint32_t* error);

private:
    bool OpenUniqueInterface(std::uint32_t* error);
    bool ValidateDriver(std::uint32_t* error);
    bool RunIoctl(DWORD code,
                  void* input,
                  DWORD input_size,
                  void* output,
                  DWORD output_size,
                  DWORD wait_ms,
                  bool observe_cancel,
                  DWORD* bytes_returned,
                  std::uint32_t* error);
    bool WaitForIo(OVERLAPPED* overlapped,
                   DWORD wait_ms,
                   bool observe_cancel,
                   DWORD* bytes_returned,
                   std::uint32_t* error);
    void CloseHandleOnly();
    void CaptureLastOpenDiagnostics();

    HANDLE device_ = INVALID_HANDLE_VALUE;
    HANDLE cancel_event_ = nullptr;
    bool signaling_open_ = false;
    OVERLAPPED peer_signaling_read_ = {};
    std::array<std::uint8_t, 4096u>
        peer_signaling_packet_ = {};
    DWORD peer_signaling_immediate_bytes_ = 0u;
    bool peer_signaling_read_active_ = false;
    bool peer_signaling_read_completed_ = false;
    V1TransportOpenDiagnostics last_open_diagnostics_ = {};
};

}  // namespace native_ldac::agent

#endif  // NATIVE_LDAC_AGENT_V1_TRANSPORT_DRIVER_BACKEND_H_
