// SPDX-License-Identifier: Apache-2.0
#include "v1_transport_silence_driver_backend.h"

#include "ldac_native_ioctl.h"

namespace native_ldac::agent {
namespace {

constexpr DWORD kTransferDiagnosticTimeoutMs = 3000u;

}  // namespace

bool V1TransportDriverBackend::WriteMedia(
    const std::uint8_t* packet,
    std::size_t packet_size,
    std::uint32_t timeout_ms,
    std::uint32_t* error) {
    if (packet == nullptr || packet_size == 0u ||
        packet_size > LDAC_NATIVE_MAX_MEDIA_TRANSFER ||
        device_ == INVALID_HANDLE_VALUE || !signaling_open_) {
        if (error != nullptr) *error = ERROR_INVALID_PARAMETER;
        return false;
    }
    LDAC_NATIVE_SIGNALING_TRANSFER_REQUEST transfer{};
    transfer.Size = sizeof(transfer);
    transfer.TimeoutMs = timeout_ms;
    DWORD bytes = 0u;
    std::uint32_t write_error = 0u;
    const bool written = RunIoctl(
        IOCTL_LDAC_NATIVE_WRITE_MEDIA,
        &transfer,
        sizeof(transfer),
        const_cast<std::uint8_t*>(packet),
        static_cast<DWORD>(packet_size),
        timeout_ms + 2000u,
        true,
        &bytes,
        &write_error);
    if (error != nullptr) {
        *error = written && bytes == packet_size
            ? ERROR_SUCCESS
            : write_error != ERROR_SUCCESS ? write_error : ERROR_WRITE_FAULT;
    }
    return written && bytes == packet_size;
}

bool V1TransportDriverBackend::GetLastMediaWriteDiagnostics(
    V1TransportMediaWriteDiagnostics* diagnostics) {
    if (diagnostics == nullptr || device_ == INVALID_HANDLE_VALUE) {
        return false;
    }
    LDAC_NATIVE_TRANSFER_DIAGNOSTICS native{};
    DWORD bytes = 0u;
    std::uint32_t diagnostic_error = ERROR_SUCCESS;
    ++diagnostics->query_attempts;
    const bool queried = RunIoctl(
        IOCTL_LDAC_NATIVE_GET_TRANSFER_DIAGNOSTICS,
        nullptr,
        0u,
        &native,
        sizeof(native),
        kTransferDiagnosticTimeoutMs,
        false,
        &bytes,
        &diagnostic_error);
    diagnostics->query_error = diagnostic_error;
    diagnostics->query_bytes = bytes;
    if (!queried || bytes < sizeof(native) ||
        native.Size != sizeof(native) ||
        native.MediaWrite.Operation !=
            LDAC_NATIVE_TRANSFER_OPERATION_WRITE_MEDIA) {
        if (queried && diagnostic_error == ERROR_SUCCESS) {
            diagnostics->query_error = ERROR_INVALID_DATA;
        }
        return false;
    }
    diagnostics->sequence = native.MediaWrite.Sequence;
    diagnostics->operation = native.MediaWrite.Operation;
    diagnostics->io_status = native.MediaWrite.IoStatus;
    diagnostics->brb_status = native.MediaWrite.BrbStatus;
    diagnostics->bluetooth_status = native.MediaWrite.BtStatus;
    diagnostics->requested_bytes = native.MediaWrite.RequestedBytes;
    diagnostics->brb_buffer_size = native.MediaWrite.BrbBufferSize;
    diagnostics->remaining_bytes = native.MediaWrite.RemainingBytes;
    diagnostics->transfer_flags = native.MediaWrite.TransferFlags;
    diagnostics->available = true;
    return true;
}

bool V1TransportSilenceDriverBackend::OpenSignaling(
    std::uint32_t timeout_ms, std::uint32_t* error) {
    return backend_.OpenSignaling(timeout_ms, error);
}
bool V1TransportSilenceDriverBackend::GetLastOpenDiagnostics(
    V1TransportOpenDiagnostics* diagnostics) const {
    return backend_.GetLastOpenDiagnostics(diagnostics);
}
bool V1TransportSilenceDriverBackend::ExchangeSignaling(
    const std::uint8_t* request, std::size_t request_size,
    std::uint8_t* response, std::size_t response_capacity,
    std::size_t* response_size, std::uint32_t timeout_ms,
    std::uint32_t* error) {
    return backend_.ExchangeSignaling(request, request_size, response,
        response_capacity, response_size, timeout_ms, error);
}
bool V1TransportSilenceDriverBackend::OpenMedia(
    std::uint32_t timeout_ms, std::uint16_t preferred_mtu,
    std::uint16_t* incoming_mtu, std::uint16_t* outgoing_mtu,
    std::uint32_t* error) {
    return backend_.OpenMedia(timeout_ms, preferred_mtu,
                              incoming_mtu, outgoing_mtu, error);
}
bool V1TransportSilenceDriverBackend::WriteMedia(
    const std::uint8_t* packet, std::size_t packet_size,
    std::uint32_t timeout_ms, std::uint32_t* error) {
    return backend_.WriteMedia(packet, packet_size, timeout_ms, error);
}
bool V1TransportSilenceDriverBackend::GetLastMediaWriteDiagnostics(
    V1TransportMediaWriteDiagnostics* diagnostics) {
    return backend_.GetLastMediaWriteDiagnostics(diagnostics);
}
bool V1TransportSilenceDriverBackend::BeginPeerSignalingRead(
    std::uint32_t timeout_ms, std::uint32_t* error) {
    return backend_.BeginPeerSignalingRead(timeout_ms, error);
}
V1TransportSignalingReadDisposition
V1TransportSilenceDriverBackend::PollPeerSignalingRead(
    std::uint8_t* packet, std::size_t packet_capacity,
    std::size_t* packet_size, std::uint32_t* error) {
    return backend_.PollPeerSignalingRead(
        packet, packet_capacity, packet_size, error);
}
bool V1TransportSilenceDriverBackend::SendPeerSignalingResponse(
    const std::uint8_t* packet, std::size_t packet_size,
    std::uint32_t timeout_ms, std::uint32_t* error) {
    return backend_.SendPeerSignalingResponse(
        packet, packet_size, timeout_ms, error);
}
bool V1TransportSilenceDriverBackend::CancelPeerSignalingRead(
    std::uint32_t* error) {
    return backend_.CancelPeerSignalingRead(error);
}
bool V1TransportSilenceDriverBackend::CloseSignaling(
    std::uint32_t* error) {
    return backend_.CloseSignaling(error);
}

}  // namespace native_ldac::agent
