// SPDX-License-Identifier: Apache-2.0
#include "v1_transport_driver_backend.h"

#include <setupapi.h>

#include <initguid.h>

#include <cstring>
#include <limits>

#include "ldac_native_ioctl.h"

namespace native_ldac::agent {
namespace {

constexpr DWORD kControlTimeoutMs = 3000u;
constexpr DWORD kCloseTimeoutMs = 5000u;
constexpr DWORD kDriverCompletionSlackMs = 2000u;

void StoreError(std::uint32_t value, std::uint32_t* error) {
    if (error != nullptr) {
        *error = value;
    }
}

DWORD AddCompletionSlack(std::uint32_t timeout_ms) {
    if (timeout_ms >
        std::numeric_limits<DWORD>::max() - kDriverCompletionSlackMs) {
        return std::numeric_limits<DWORD>::max();
    }
    return static_cast<DWORD>(timeout_ms) + kDriverCompletionSlackMs;
}

}  // namespace

V1TransportDriverBackend::V1TransportDriverBackend(HANDLE cancel_event)
    : cancel_event_(cancel_event) {}

V1TransportDriverBackend::~V1TransportDriverBackend() {
    CloseHandleOnly();
}

bool V1TransportDriverBackend::OpenUniqueInterface(std::uint32_t* error) {
    HDEVINFO devices = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_LDAC_NATIVE_TRANSPORT,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) {
        StoreError(GetLastError(), error);
        return false;
    }

    SP_DEVICE_INTERFACE_DATA interface_data = {};
    interface_data.cbSize = sizeof(interface_data);
    if (!SetupDiEnumDeviceInterfaces(
            devices,
            nullptr,
            &GUID_DEVINTERFACE_LDAC_NATIVE_TRANSPORT,
            0u,
            &interface_data)) {
        const DWORD failure = GetLastError();
        SetupDiDestroyDeviceInfoList(devices);
        StoreError(failure, error);
        return false;
    }
    SP_DEVICE_INTERFACE_DATA duplicate = {};
    duplicate.cbSize = sizeof(duplicate);
    if (SetupDiEnumDeviceInterfaces(
            devices,
            nullptr,
            &GUID_DEVINTERFACE_LDAC_NATIVE_TRANSPORT,
            1u,
            &duplicate)) {
        SetupDiDestroyDeviceInfoList(devices);
        StoreError(ERROR_MORE_DATA, error);
        return false;
    }
    const DWORD enumeration_error = GetLastError();
    if (enumeration_error != ERROR_NO_MORE_ITEMS) {
        SetupDiDestroyDeviceInfoList(devices);
        StoreError(enumeration_error, error);
        return false;
    }

    DWORD required_size = 0u;
    (void)SetupDiGetDeviceInterfaceDetailW(
        devices,
        &interface_data,
        nullptr,
        0u,
        &required_size,
        nullptr);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required_size == 0u) {
        const DWORD failure = GetLastError();
        SetupDiDestroyDeviceInfoList(devices);
        StoreError(failure, error);
        return false;
    }

    auto* detail = static_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, required_size));
    if (detail == nullptr) {
        SetupDiDestroyDeviceInfoList(devices);
        StoreError(ERROR_NOT_ENOUGH_MEMORY, error);
        return false;
    }
    detail->cbSize = sizeof(*detail);
    if (!SetupDiGetDeviceInterfaceDetailW(
            devices,
            &interface_data,
            detail,
            required_size,
            nullptr,
            nullptr)) {
        const DWORD failure = GetLastError();
        HeapFree(GetProcessHeap(), 0u, detail);
        SetupDiDestroyDeviceInfoList(devices);
        StoreError(failure, error);
        return false;
    }

    device_ = CreateFileW(
        detail->DevicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        nullptr);
    const DWORD open_error = device_ == INVALID_HANDLE_VALUE
        ? GetLastError()
        : ERROR_SUCCESS;
    HeapFree(GetProcessHeap(), 0u, detail);
    SetupDiDestroyDeviceInfoList(devices);
    StoreError(open_error, error);
    return device_ != INVALID_HANDLE_VALUE;
}

bool V1TransportDriverBackend::WaitForIo(
    OVERLAPPED* overlapped,
    DWORD wait_ms,
    bool observe_cancel,
    DWORD* bytes_returned,
    std::uint32_t* error) {
    if (overlapped == nullptr || overlapped->hEvent == nullptr ||
        device_ == INVALID_HANDLE_VALUE) {
        StoreError(ERROR_INVALID_PARAMETER, error);
        return false;
    }
    DWORD wait_result = WAIT_FAILED;
    if (observe_cancel && cancel_event_ != nullptr) {
        HANDLE waits[] = {overlapped->hEvent, cancel_event_};
        wait_result = WaitForMultipleObjects(
            ARRAYSIZE(waits), waits, FALSE, wait_ms);
        if (wait_result == WAIT_OBJECT_0 + 1u) {
            (void)CancelIoEx(device_, overlapped);
            (void)WaitForSingleObject(overlapped->hEvent, INFINITE);
            DWORD ignored = 0u;
            (void)GetOverlappedResult(device_, overlapped, &ignored, FALSE);
            StoreError(ERROR_CANCELLED, error);
            return false;
        }
    } else {
        wait_result = WaitForSingleObject(overlapped->hEvent, wait_ms);
    }
    if (wait_result == WAIT_TIMEOUT) {
        (void)CancelIoEx(device_, overlapped);
        (void)WaitForSingleObject(overlapped->hEvent, INFINITE);
        DWORD ignored = 0u;
        (void)GetOverlappedResult(device_, overlapped, &ignored, FALSE);
        StoreError(ERROR_TIMEOUT, error);
        return false;
    }
    if (wait_result != WAIT_OBJECT_0) {
        const DWORD failure = GetLastError();
        (void)CancelIoEx(device_, overlapped);
        (void)WaitForSingleObject(overlapped->hEvent, INFINITE);
        StoreError(failure, error);
        return false;
    }

    DWORD transferred = 0u;
    if (!GetOverlappedResult(device_, overlapped, &transferred, FALSE)) {
        StoreError(GetLastError(), error);
        return false;
    }
    if (bytes_returned != nullptr) {
        *bytes_returned = transferred;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1TransportDriverBackend::RunIoctl(
    DWORD code,
    void* input,
    DWORD input_size,
    void* output,
    DWORD output_size,
    DWORD wait_ms,
    bool observe_cancel,
    DWORD* bytes_returned,
    std::uint32_t* error) {
    if (device_ == INVALID_HANDLE_VALUE) {
        StoreError(ERROR_INVALID_HANDLE, error);
        return false;
    }
    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr) {
        StoreError(GetLastError(), error);
        return false;
    }
    DWORD immediate_bytes = 0u;
    const BOOL started = DeviceIoControl(
        device_,
        code,
        input,
        input_size,
        output,
        output_size,
        &immediate_bytes,
        &overlapped);
    if (started) {
        if (bytes_returned != nullptr) {
            *bytes_returned = immediate_bytes;
        }
        CloseHandle(overlapped.hEvent);
        StoreError(ERROR_SUCCESS, error);
        return true;
    }
    const DWORD start_error = GetLastError();
    if (start_error != ERROR_IO_PENDING) {
        CloseHandle(overlapped.hEvent);
        StoreError(start_error, error);
        return false;
    }
    const bool completed = WaitForIo(
        &overlapped,
        wait_ms,
        observe_cancel,
        bytes_returned,
        error);
    CloseHandle(overlapped.hEvent);
    return completed;
}

bool V1TransportDriverBackend::ValidateDriver(std::uint32_t* error) {
    LDAC_NATIVE_ABI_VERSION version = {};
    DWORD bytes = 0u;
    if (!RunIoctl(IOCTL_LDAC_NATIVE_GET_VERSION,
                  nullptr,
                  0u,
                  &version,
                  sizeof(version),
                  kControlTimeoutMs,
                  true,
                  &bytes,
                  error) ||
        bytes < sizeof(version) || version.Size != sizeof(version) ||
        version.Major != LDAC_NATIVE_ABI_MAJOR ||
        version.Minor < LDAC_NATIVE_ABI_MINOR) {
        if (error != nullptr && *error == ERROR_SUCCESS) {
            *error = ERROR_REVISION_MISMATCH;
        }
        return false;
    }

    LDAC_NATIVE_DEVICE_INFO info = {};
    bytes = 0u;
    if (!RunIoctl(IOCTL_LDAC_NATIVE_GET_DEVICE_INFO,
                  nullptr,
                  0u,
                  &info,
                  sizeof(info),
                  kControlTimeoutMs,
                  true,
                  &bytes,
                  error) ||
        bytes < sizeof(info) || info.Size != sizeof(info) ||
        (info.Flags & (LDAC_NATIVE_DEVICE_INFO_PROFILE_READY |
                       LDAC_NATIVE_DEVICE_INFO_REMOTE_READY |
                       LDAC_NATIVE_DEVICE_INFO_LOCAL_READY |
                       LDAC_NATIVE_DEVICE_INFO_INBOUND_SIGNALING_READY)) !=
            (LDAC_NATIVE_DEVICE_INFO_PROFILE_READY |
             LDAC_NATIVE_DEVICE_INFO_REMOTE_READY |
             LDAC_NATIVE_DEVICE_INFO_LOCAL_READY |
             LDAC_NATIVE_DEVICE_INFO_INBOUND_SIGNALING_READY) ||
        info.RemoteBluetoothAddress == 0u ||
        info.LocalBluetoothAddress == 0u ||
        info.SignalingPsm != 0x0019u) {
        if (error != nullptr && *error == ERROR_SUCCESS) {
            *error = ERROR_DEVICE_NOT_AVAILABLE;
        }
        return false;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1TransportDriverBackend::OpenSignaling(
    std::uint32_t timeout_ms,
    std::uint32_t* error) {
    last_open_diagnostics_ = {};
    if (device_ != INVALID_HANDLE_VALUE || signaling_open_ ||
        timeout_ms == 0u || timeout_ms > LDAC_NATIVE_MAX_TIMEOUT_MS) {
        StoreError(ERROR_INVALID_STATE, error);
        return false;
    }
    if (cancel_event_ != nullptr &&
        WaitForSingleObject(cancel_event_, 0u) == WAIT_OBJECT_0) {
        StoreError(ERROR_CANCELLED, error);
        return false;
    }
    if (!OpenUniqueInterface(error) || !ValidateDriver(error)) {
        CloseHandleOnly();
        return false;
    }

    LDAC_NATIVE_OPEN_SIGNALING_REQUEST request = {};
    request.Size = sizeof(request);
    request.TimeoutMs = timeout_ms;
    request.PreferredMtu = 672u;
    LDAC_NATIVE_CHANNEL_INFO channel = {};
    DWORD bytes = 0u;
    std::uint32_t local_error = ERROR_SUCCESS;
    std::uint32_t* open_error_target = error != nullptr
        ? error
        : &local_error;
    const bool opened = RunIoctl(IOCTL_LDAC_NATIVE_OPEN_SIGNALING,
                                 &request,
                                 sizeof(request),
                                 &channel,
                                 sizeof(channel),
                                 AddCompletionSlack(timeout_ms),
                                 true,
                                 &bytes,
                                 open_error_target);
    if (!opened ||
        bytes < sizeof(channel) || channel.Size != sizeof(channel) ||
        channel.State != LDAC_NATIVE_CHANNEL_CONNECTED ||
        channel.Psm != 0x0019u || channel.IncomingMtu == 0u ||
        channel.OutgoingMtu == 0u) {
        if (*open_error_target == ERROR_SUCCESS) {
            *open_error_target = ERROR_INVALID_DATA;
        }
        const std::uint32_t open_error = *open_error_target;
        CaptureLastOpenDiagnostics();
        CloseHandleOnly();
        if (!last_open_diagnostics_.available) {
            std::uint32_t reopen_error = ERROR_SUCCESS;
            if (OpenUniqueInterface(&reopen_error)) {
                CaptureLastOpenDiagnostics();
                CloseHandleOnly();
            } else {
                last_open_diagnostics_.query_error = reopen_error;
            }
        }
        StoreError(open_error, error);
        return false;
    }
    signaling_open_ = true;
    CaptureLastOpenDiagnostics();
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1TransportDriverBackend::GetLastOpenDiagnostics(
    V1TransportOpenDiagnostics* diagnostics) const {
    if (diagnostics == nullptr) {
        return false;
    }
    *diagnostics = last_open_diagnostics_;
    return diagnostics->available;
}

void V1TransportDriverBackend::CaptureLastOpenDiagnostics() {
    if (device_ == INVALID_HANDLE_VALUE) {
        return;
    }
    LDAC_NATIVE_OPEN_DIAGNOSTICS native = {};
    DWORD bytes = 0u;
    std::uint32_t diagnostic_error = ERROR_SUCCESS;
    ++last_open_diagnostics_.query_attempts;
    const bool queried = RunIoctl(
        IOCTL_LDAC_NATIVE_GET_OPEN_DIAGNOSTICS,
        nullptr,
        0u,
        &native,
        sizeof(native),
        kControlTimeoutMs,
        false,
        &bytes,
        &diagnostic_error);
    last_open_diagnostics_.query_error = diagnostic_error;
    last_open_diagnostics_.query_bytes = bytes;
    if (!queried ||
        bytes < sizeof(native) || native.Size != sizeof(native) ||
        (native.Flags & LDAC_NATIVE_OPEN_DIAGNOSTIC_ATTEMPTED) == 0u) {
        if (queried && diagnostic_error == ERROR_SUCCESS) {
            last_open_diagnostics_.query_error = ERROR_INVALID_DATA;
        }
        return;
    }
    last_open_diagnostics_.sequence = native.Sequence;
    last_open_diagnostics_.operation = native.Operation;
    last_open_diagnostics_.io_status = native.IoStatus;
    last_open_diagnostics_.brb_status = native.BrbStatus;
    last_open_diagnostics_.bluetooth_status = native.BtStatus;
    last_open_diagnostics_.remote_bluetooth_address =
        native.RemoteBluetoothAddress;
    last_open_diagnostics_.channel_flags = native.ChannelFlags;
    last_open_diagnostics_.flags = native.Flags;
    last_open_diagnostics_.psm = native.Psm;
    last_open_diagnostics_.response = native.Response;
    last_open_diagnostics_.response_status = native.ResponseStatus;
    last_open_diagnostics_.available = true;
    last_open_diagnostics_.remote_response_valid =
        (native.Flags &
         LDAC_NATIVE_OPEN_DIAGNOSTIC_REMOTE_RESPONSE_VALID) != 0u;
}

bool V1TransportDriverBackend::ExchangeSignaling(
    const std::uint8_t* request,
    std::size_t request_size,
    std::uint8_t* response,
    std::size_t response_capacity,
    std::size_t* response_size,
    std::uint32_t timeout_ms,
    std::uint32_t* error) {
    if (!signaling_open_ || request == nullptr || request_size == 0u ||
        request_size > LDAC_NATIVE_MAX_SIGNALING_TRANSFER ||
        response == nullptr || response_capacity == 0u ||
        response_capacity > LDAC_NATIVE_MAX_SIGNALING_TRANSFER ||
        response_size == nullptr || timeout_ms == 0u ||
        timeout_ms > LDAC_NATIVE_MAX_TIMEOUT_MS) {
        StoreError(ERROR_INVALID_PARAMETER, error);
        return false;
    }
    *response_size = 0u;
    LDAC_NATIVE_SIGNALING_TRANSFER_REQUEST transfer = {};
    transfer.Size = sizeof(transfer);
    transfer.TimeoutMs = timeout_ms;

    OVERLAPPED read = {};
    read.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (read.hEvent == nullptr) {
        StoreError(GetLastError(), error);
        return false;
    }
    DWORD immediate_read = 0u;
    const BOOL read_started = DeviceIoControl(
        device_,
        IOCTL_LDAC_NATIVE_READ_SIGNALING,
        &transfer,
        sizeof(transfer),
        response,
        static_cast<DWORD>(response_capacity),
        &immediate_read,
        &read);
    const DWORD read_start_error = read_started
        ? ERROR_SUCCESS
        : GetLastError();
    if (!read_started && read_start_error != ERROR_IO_PENDING) {
        CloseHandle(read.hEvent);
        StoreError(read_start_error, error);
        return false;
    }

    DWORD written = 0u;
    std::uint32_t write_error = 0u;
    if (!RunIoctl(IOCTL_LDAC_NATIVE_WRITE_SIGNALING,
                  &transfer,
                  sizeof(transfer),
                  const_cast<std::uint8_t*>(request),
                  static_cast<DWORD>(request_size),
                  AddCompletionSlack(timeout_ms),
                  true,
                  &written,
                  &write_error) ||
        written != request_size) {
        (void)CancelIoEx(device_, &read);
        (void)WaitForSingleObject(read.hEvent, INFINITE);
        DWORD ignored = 0u;
        (void)GetOverlappedResult(device_, &read, &ignored, FALSE);
        CloseHandle(read.hEvent);
        StoreError(write_error != ERROR_SUCCESS
                       ? write_error
                       : ERROR_WRITE_FAULT,
                   error);
        return false;
    }

    DWORD read_bytes = immediate_read;
    bool read_completed = read_started != FALSE;
    if (!read_completed) {
        read_completed = WaitForIo(
            &read,
            AddCompletionSlack(timeout_ms),
            true,
            &read_bytes,
            error);
    }
    CloseHandle(read.hEvent);
    if (!read_completed || read_bytes == 0u) {
        if (read_completed) {
            StoreError(ERROR_INVALID_DATA, error);
        }
        return false;
    }
    *response_size = read_bytes;
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1TransportDriverBackend::BeginPeerSignalingRead(
    std::uint32_t timeout_ms,
    std::uint32_t* error) {
    if (!signaling_open_ || device_ == INVALID_HANDLE_VALUE ||
        peer_signaling_read_active_ || timeout_ms == 0u ||
        timeout_ms > LDAC_NATIVE_MAX_TIMEOUT_MS) {
        StoreError(ERROR_INVALID_STATE, error);
        return false;
    }
    peer_signaling_packet_.fill(0u);
    peer_signaling_read_ = {};
    peer_signaling_read_.hEvent =
        CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (peer_signaling_read_.hEvent == nullptr) {
        StoreError(GetLastError(), error);
        return false;
    }
    LDAC_NATIVE_SIGNALING_TRANSFER_REQUEST transfer = {};
    transfer.Size = sizeof(transfer);
    transfer.TimeoutMs = timeout_ms;
    peer_signaling_immediate_bytes_ = 0u;
    const BOOL started = DeviceIoControl(
        device_, IOCTL_LDAC_NATIVE_READ_SIGNALING,
        &transfer, sizeof(transfer),
        peer_signaling_packet_.data(),
        static_cast<DWORD>(peer_signaling_packet_.size()),
        &peer_signaling_immediate_bytes_, &peer_signaling_read_);
    const DWORD start_error = started ? ERROR_SUCCESS : GetLastError();
    if (!started && start_error != ERROR_IO_PENDING) {
        CloseHandle(peer_signaling_read_.hEvent);
        peer_signaling_read_ = {};
        StoreError(start_error, error);
        return false;
    }
    peer_signaling_read_active_ = true;
    peer_signaling_read_completed_ = started != FALSE;
    StoreError(ERROR_SUCCESS, error);
    return true;
}

V1TransportSignalingReadDisposition
V1TransportDriverBackend::PollPeerSignalingRead(
    std::uint8_t* packet,
    std::size_t packet_capacity,
    std::size_t* packet_size,
    std::uint32_t* error) {
    if (!peer_signaling_read_active_ || packet == nullptr ||
        packet_capacity == 0u || packet_size == nullptr) {
        StoreError(ERROR_INVALID_STATE, error);
        return V1TransportSignalingReadDisposition::Failure;
    }
    *packet_size = 0u;
    if (!peer_signaling_read_completed_) {
        const DWORD wait = WaitForSingleObject(
            peer_signaling_read_.hEvent, 0u);
        if (wait == WAIT_TIMEOUT) {
            StoreError(ERROR_SUCCESS, error);
            return V1TransportSignalingReadDisposition::NoPacket;
        }
        if (wait != WAIT_OBJECT_0) {
            StoreError(GetLastError(), error);
            return V1TransportSignalingReadDisposition::Failure;
        }
    }
    DWORD bytes = peer_signaling_immediate_bytes_;
    BOOL completed = TRUE;
    DWORD completion_error = ERROR_SUCCESS;
    if (!peer_signaling_read_completed_) {
        completed = GetOverlappedResult(
            device_, &peer_signaling_read_, &bytes, FALSE);
        if (!completed) {
            completion_error = GetLastError();
        }
    }
    CloseHandle(peer_signaling_read_.hEvent);
    peer_signaling_read_ = {};
    peer_signaling_immediate_bytes_ = 0u;
    peer_signaling_read_active_ = false;
    peer_signaling_read_completed_ = false;
    if (!completed) {
        if (completion_error == ERROR_TIMEOUT ||
            completion_error == ERROR_SEM_TIMEOUT) {
            StoreError(ERROR_SUCCESS, error);
            return V1TransportSignalingReadDisposition::TimedOut;
        }
        StoreError(completion_error, error);
        return V1TransportSignalingReadDisposition::Failure;
    }
    if (bytes == 0u || bytes > packet_capacity ||
        bytes > peer_signaling_packet_.size()) {
        StoreError(ERROR_INVALID_DATA, error);
        return V1TransportSignalingReadDisposition::Failure;
    }
    std::memcpy(packet, peer_signaling_packet_.data(), bytes);
    *packet_size = bytes;
    StoreError(ERROR_SUCCESS, error);
    return V1TransportSignalingReadDisposition::Packet;
}

bool V1TransportDriverBackend::SendPeerSignalingResponse(
    const std::uint8_t* packet,
    std::size_t packet_size,
    std::uint32_t timeout_ms,
    std::uint32_t* error) {
    if (!signaling_open_ || packet == nullptr || packet_size == 0u ||
        packet_size > LDAC_NATIVE_MAX_SIGNALING_TRANSFER ||
        timeout_ms == 0u || timeout_ms > LDAC_NATIVE_MAX_TIMEOUT_MS) {
        StoreError(ERROR_INVALID_PARAMETER, error);
        return false;
    }
    LDAC_NATIVE_SIGNALING_TRANSFER_REQUEST transfer = {};
    transfer.Size = sizeof(transfer);
    transfer.TimeoutMs = timeout_ms;
    DWORD written = 0u;
    std::uint32_t write_error = 0u;
    const bool succeeded = RunIoctl(
        IOCTL_LDAC_NATIVE_WRITE_SIGNALING,
        &transfer, sizeof(transfer),
        const_cast<std::uint8_t*>(packet),
        static_cast<DWORD>(packet_size),
        AddCompletionSlack(timeout_ms), true,
        &written, &write_error);
    StoreError(succeeded && written == packet_size
                   ? ERROR_SUCCESS
                   : write_error != ERROR_SUCCESS
                       ? write_error
                       : ERROR_WRITE_FAULT,
               error);
    return succeeded && written == packet_size;
}

bool V1TransportDriverBackend::CancelPeerSignalingRead(
    std::uint32_t* error) {
    if (!peer_signaling_read_active_) {
        StoreError(ERROR_SUCCESS, error);
        return true;
    }
    if (!peer_signaling_read_completed_) {
        if (!CancelIoEx(device_, &peer_signaling_read_) &&
            GetLastError() != ERROR_NOT_FOUND) {
            StoreError(GetLastError(), error);
            return false;
        }
        const DWORD wait = WaitForSingleObject(
            peer_signaling_read_.hEvent, INFINITE);
        if (wait != WAIT_OBJECT_0) {
            StoreError(GetLastError(), error);
            return false;
        }
        DWORD ignored = 0u;
        (void)GetOverlappedResult(
            device_, &peer_signaling_read_, &ignored, FALSE);
    }
    CloseHandle(peer_signaling_read_.hEvent);
    peer_signaling_read_ = {};
    peer_signaling_immediate_bytes_ = 0u;
    peer_signaling_read_active_ = false;
    peer_signaling_read_completed_ = false;
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1TransportDriverBackend::CloseSignaling(std::uint32_t* error) {
    if (device_ == INVALID_HANDLE_VALUE) {
        StoreError(ERROR_SUCCESS, error);
        return true;
    }
    std::uint32_t read_error = 0u;
    const bool read_cancelled = CancelPeerSignalingRead(&read_error);
    DWORD bytes = 0u;
    std::uint32_t close_error = 0u;
    const bool closed = RunIoctl(
        IOCTL_LDAC_NATIVE_CLOSE_CHANNELS,
        nullptr,
        0u,
        nullptr,
        0u,
        kCloseTimeoutMs,
        false,
        &bytes,
        &close_error);
    CloseHandleOnly();
    StoreError(!read_cancelled ? read_error : close_error, error);
    return read_cancelled && closed;
}

void V1TransportDriverBackend::CloseHandleOnly() {
    if (device_ != INVALID_HANDLE_VALUE) {
        std::uint32_t ignored_error = 0u;
        (void)CancelPeerSignalingRead(&ignored_error);
        (void)CancelIoEx(device_, nullptr);
        CloseHandle(device_);
        device_ = INVALID_HANDLE_VALUE;
    }
    signaling_open_ = false;
}

}  // namespace native_ldac::agent
