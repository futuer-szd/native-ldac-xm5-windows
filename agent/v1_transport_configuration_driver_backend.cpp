// SPDX-License-Identifier: Apache-2.0
#include "v1_transport_configuration_driver_backend.h"

#include <limits>

#include "ldac_native_ioctl.h"

namespace native_ldac::agent {
namespace {

constexpr DWORD kDriverCompletionSlackMs = 2000u;

DWORD AddCompletionSlack(std::uint32_t timeout_ms) {
    if (timeout_ms >
        std::numeric_limits<DWORD>::max() - kDriverCompletionSlackMs) {
        return std::numeric_limits<DWORD>::max();
    }
    return static_cast<DWORD>(timeout_ms) + kDriverCompletionSlackMs;
}

void StoreError(std::uint32_t value, std::uint32_t* error) {
    if (error != nullptr) {
        *error = value;
    }
}

}  // namespace

bool V1TransportDriverBackend::OpenMedia(
    std::uint32_t timeout_ms,
    std::uint16_t preferred_mtu,
    std::uint16_t* incoming_mtu,
    std::uint16_t* outgoing_mtu,
    std::uint32_t* error) {
    if (incoming_mtu == nullptr || outgoing_mtu == nullptr ||
        preferred_mtu == 0u || device_ == INVALID_HANDLE_VALUE ||
        !signaling_open_) {
        StoreError(ERROR_INVALID_STATE, error);
        return false;
    }
    LDAC_NATIVE_OPEN_SIGNALING_REQUEST request = {};
    request.Size = sizeof(request);
    request.TimeoutMs = timeout_ms;
    request.PreferredMtu = preferred_mtu;
    LDAC_NATIVE_CHANNEL_INFO channel = {};
    DWORD bytes = 0u;
    std::uint32_t open_error = 0u;
    if (!RunIoctl(IOCTL_LDAC_NATIVE_OPEN_MEDIA,
                  &request,
                  sizeof(request),
                  &channel,
                  sizeof(channel),
                  AddCompletionSlack(timeout_ms),
                  true,
                  &bytes,
                  &open_error)) {
        StoreError(open_error, error);
        return false;
    }
    if (bytes < sizeof(channel) || channel.Size != sizeof(channel) ||
        channel.State != LDAC_NATIVE_CHANNEL_CONNECTED ||
        channel.OutgoingMtu == 0u ||
        channel.OutgoingMtu > LDAC_NATIVE_MAX_MEDIA_TRANSFER) {
        StoreError(ERROR_INVALID_DATA, error);
        return false;
    }
    *incoming_mtu = channel.IncomingMtu;
    *outgoing_mtu = channel.OutgoingMtu;
    StoreError(ERROR_SUCCESS, error);
    return true;
}

V1TransportConfigurationDriverBackend::
    V1TransportConfigurationDriverBackend(HANDLE cancel_event)
    : backend_(cancel_event) {}

bool V1TransportConfigurationDriverBackend::OpenSignaling(
    std::uint32_t timeout_ms,
    std::uint32_t* error) {
    return backend_.OpenSignaling(timeout_ms, error);
}

bool V1TransportConfigurationDriverBackend::GetLastOpenDiagnostics(
    V1TransportOpenDiagnostics* diagnostics) const {
    return backend_.GetLastOpenDiagnostics(diagnostics);
}

bool V1TransportConfigurationDriverBackend::ExchangeSignaling(
    const std::uint8_t* request,
    std::size_t request_size,
    std::uint8_t* response,
    std::size_t response_capacity,
    std::size_t* response_size,
    std::uint32_t timeout_ms,
    std::uint32_t* error) {
    return backend_.ExchangeSignaling(request,
                                      request_size,
                                      response,
                                      response_capacity,
                                      response_size,
                                      timeout_ms,
                                      error);
}

bool V1TransportConfigurationDriverBackend::OpenMedia(
    std::uint32_t timeout_ms,
    std::uint16_t preferred_mtu,
    std::uint16_t* incoming_mtu,
    std::uint16_t* outgoing_mtu,
    std::uint32_t* error) {
    return backend_.OpenMedia(timeout_ms,
                              preferred_mtu,
                              incoming_mtu,
                              outgoing_mtu,
                              error);
}

bool V1TransportConfigurationDriverBackend::CloseSignaling(
    std::uint32_t* error) {
    return backend_.CloseSignaling(error);
}

}  // namespace native_ldac::agent
