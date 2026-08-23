// SPDX-License-Identifier: Apache-2.0
#include "v1_avrcp_observer_host.h"

#include <setupapi.h>

#include <limits>
#include <vector>

namespace native_ldac::agent {
namespace {

constexpr DWORD kWindowsVolumePollIntervalMs = 200u;
constexpr unsigned int kMaximumEventsPerPoll = 128u;
constexpr GUID kObserverInterfaceGuid = {
    0x59cfd04c,
    0x74a5,
    0x4d89,
    {0x8b, 0xc5, 0x26, 0x37, 0xb6, 0xd8, 0xa2, 0x7a}};

void StoreError(DWORD value, DWORD* error) {
    if (error != nullptr) {
        *error = value;
    }
}

bool IsReadyForActivation(const NLD_AVRCP_OBSERVER_STATUS& status) {
    constexpr ULONG required =
        NLD_AVRCP_OBSERVER_STATUS_OUTBOUND_OPEN |
        NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUIRED;
    return (status.Flags & required) == required;
}

bool IsControlChannelReady(const NLD_AVRCP_OBSERVER_STATUS& status) {
    constexpr ULONG required =
        NLD_AVRCP_OBSERVER_STATUS_CHANNEL_OPEN |
        NLD_AVRCP_OBSERVER_STATUS_VOLUME_SUPPORTED |
        NLD_AVRCP_OBSERVER_STATUS_OBSERVING;
    constexpr ULONG terminal =
        NLD_AVRCP_OBSERVER_STATUS_OPEN_PENDING |
        NLD_AVRCP_OBSERVER_STATUS_REMOTE_DISCONNECTED;
    return (status.Flags & required) == required &&
           (status.Flags & terminal) == 0u;
}

bool MapDriverEvent(const NLD_AVRCP_OBSERVER_EVENT& event,
                    std::uint64_t physical_acl_generation,
                    V1AvrcpObservedEvent* mapped) {
    if (mapped == nullptr || physical_acl_generation == 0u) {
        return false;
    }
    *mapped = V1AvrcpObservedEvent{};
    mapped->generation = physical_acl_generation;
    mapped->flags = event.Flags;
    switch (event.Type) {
        case NldAvrcpObserverEventAclConnected:
        case NldAvrcpObserverEventAclDisconnected:
            return false;
        case NldAvrcpObserverEventVolumeCapability:
            mapped->kind = V1AvrcpObservedEvent::Kind::VolumeCapability;
            mapped->value = event.Value0;
            return true;
        case NldAvrcpObserverEventAbsoluteVolume:
            mapped->kind = V1AvrcpObservedEvent::Kind::AbsoluteVolume;
            mapped->value = event.Value0;
            mapped->volume_event =
                (event.Flags & NLD_AVRCP_EVENT_FLAG_CHANGED) != 0u
                    ? AvrcpXm5VolumeEvent::RemoteNotification
                    : AvrcpXm5VolumeEvent::CommandResponse;
            return true;
        case NldAvrcpObserverEventPassThrough:
            mapped->kind = V1AvrcpObservedEvent::Kind::PassThrough;
            mapped->value = event.Value0;
            return true;
        case NldAvrcpObserverEventWriteResponse:
            if ((event.Value0 & 0xFFu) != 0x50u) {
                return false;
            }
            mapped->kind = V1AvrcpObservedEvent::Kind::AbsoluteVolume;
            mapped->value = event.RawPrefixHigh & 0xFFu;
            mapped->volume_event = AvrcpXm5VolumeEvent::CommandResponse;
            return true;
        case NldAvrcpObserverEventVendorCommand:
            if ((event.Value0 & 0xFFu) != 0x50u) {
                return false;
            }
            mapped->kind =
                V1AvrcpObservedEvent::Kind::SetAbsoluteVolumeCommand;
            mapped->value = event.RawPrefixHigh & 0xFFu;
            return true;
        default:
            return false;
    }
}

}  // namespace

V1AvrcpObserverWin32Io::~V1AvrcpObserverWin32Io() {
    Close();
}

bool V1AvrcpObserverWin32Io::OpenImpl(DWORD access, DWORD* error) {
    Close();
    HDEVINFO devices = SetupDiGetClassDevsW(
        &kObserverInterfaceGuid,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) {
        StoreError(GetLastError(), error);
        return false;
    }

    SP_DEVICE_INTERFACE_DATA interface_data{};
    interface_data.cbSize = sizeof(interface_data);
    if (!SetupDiEnumDeviceInterfaces(
            devices,
            nullptr,
            &kObserverInterfaceGuid,
            0u,
            &interface_data)) {
        const DWORD enum_error = GetLastError();
        SetupDiDestroyDeviceInfoList(devices);
        StoreError(enum_error, error);
        return false;
    }
    SP_DEVICE_INTERFACE_DATA second_interface{};
    second_interface.cbSize = sizeof(second_interface);
    if (SetupDiEnumDeviceInterfaces(
            devices,
            nullptr,
            &kObserverInterfaceGuid,
            1u,
            &second_interface)) {
        SetupDiDestroyDeviceInfoList(devices);
        StoreError(ERROR_DUP_NAME, error);
        return false;
    }
    const DWORD second_error = GetLastError();
    if (second_error != ERROR_NO_MORE_ITEMS) {
        SetupDiDestroyDeviceInfoList(devices);
        StoreError(second_error, error);
        return false;
    }

    DWORD bytes_required = 0u;
    (void)SetupDiGetDeviceInterfaceDetailW(
        devices, &interface_data, nullptr, 0u, &bytes_required, nullptr);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
        bytes_required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
        const DWORD detail_error = GetLastError();
        SetupDiDestroyDeviceInfoList(devices);
        StoreError(detail_error, error);
        return false;
    }
    std::vector<BYTE> detail_storage(bytes_required);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
        detail_storage.data());
    detail->cbSize = sizeof(*detail);
    if (!SetupDiGetDeviceInterfaceDetailW(
            devices,
            &interface_data,
            detail,
            bytes_required,
            nullptr,
            nullptr)) {
        const DWORD detail_error = GetLastError();
        SetupDiDestroyDeviceInfoList(devices);
        StoreError(detail_error, error);
        return false;
    }
    handle_ = CreateFileW(detail->DevicePath,
                          access,
                          0u,
                          nullptr,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          nullptr);
    const DWORD open_error =
        handle_ == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    SetupDiDestroyDeviceInfoList(devices);
    if (handle_ == INVALID_HANDLE_VALUE) {
        StoreError(open_error, error);
        return false;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1AvrcpObserverWin32Io::OpenReadOnly(DWORD* error) {
    return OpenImpl(GENERIC_READ, error);
}

bool V1AvrcpObserverWin32Io::OpenReadWrite(DWORD* error) {
    return OpenImpl(GENERIC_READ | GENERIC_WRITE, error);
}

void V1AvrcpObserverWin32Io::Close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

bool V1AvrcpObserverWin32Io::SendCommand(ULONG pdu,
                                         ULONG response,
                                         const UCHAR* parameters,
                                         ULONG parameter_size,
                                         DWORD* error) {
    if (handle_ == INVALID_HANDLE_VALUE) {
        StoreError(ERROR_INVALID_HANDLE, error);
        return false;
    }
    if (parameter_size > 8u ||
        (parameter_size != 0u && parameters == nullptr)) {
        StoreError(ERROR_INVALID_PARAMETER, error);
        return false;
    }
    NLD_AVRCP_OBSERVER_WRITE_REQUEST request{};
    request.Size = sizeof(request);
    request.PduId = pdu;
    request.Response = response;
    request.ParameterSize = parameter_size;
    for (ULONG index = 0u; index < parameter_size; ++index) {
        request.Parameters[index] = parameters[index];
    }
    DWORD bytes = 0u;
    const BOOL ok = DeviceIoControl(
        handle_,
        IOCTL_NLD_AVRCP_OBSERVER_SEND_COMMAND,
        &request,
        sizeof(request),
        nullptr,
        0u,
        &bytes,
        nullptr);
    if (!ok) {
        StoreError(GetLastError(), error);
        return false;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1AvrcpObserverWin32Io::WriteAvrcp(ULONG pdu,
                                        ULONG response,
                                        const UCHAR* parameters,
                                        ULONG parameter_size) {
    DWORD error = ERROR_SUCCESS;
    return SendCommand(pdu, response, parameters, parameter_size, &error);
}

bool V1AvrcpObserverWin32Io::GetVersion(
    NLD_AVRCP_OBSERVER_ABI_VERSION* version,
    DWORD* error) {
    if (version == nullptr || handle_ == INVALID_HANDLE_VALUE) {
        StoreError(ERROR_INVALID_HANDLE, error);
        return false;
    }
    DWORD bytes = 0u;
    const BOOL ok = DeviceIoControl(
        handle_, IOCTL_NLD_AVRCP_OBSERVER_GET_VERSION, nullptr, 0u, version,
        sizeof(*version), &bytes, nullptr);
    if (!ok || bytes != sizeof(*version)) {
        StoreError(ok ? ERROR_INVALID_DATA : GetLastError(), error);
        return false;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1AvrcpObserverWin32Io::GetStatus(NLD_AVRCP_OBSERVER_STATUS* status,
                                       DWORD* error) {
    if (status == nullptr || handle_ == INVALID_HANDLE_VALUE) {
        StoreError(ERROR_INVALID_HANDLE, error);
        return false;
    }
    DWORD bytes = 0u;
    const BOOL ok = DeviceIoControl(
        handle_, IOCTL_NLD_AVRCP_OBSERVER_GET_STATUS, nullptr, 0u, status,
        sizeof(*status), &bytes, nullptr);
    if (!ok || bytes != sizeof(*status)) {
        StoreError(ok ? ERROR_INVALID_DATA : GetLastError(), error);
        return false;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1AvrcpObserverWin32Io::BeginObservation(DWORD* error) {
    if (handle_ == INVALID_HANDLE_VALUE) {
        StoreError(ERROR_INVALID_HANDLE, error);
        return false;
    }
    DWORD bytes = 0u;
    if (!DeviceIoControl(handle_, IOCTL_NLD_AVRCP_OBSERVER_BEGIN_OBSERVATION,
                         nullptr, 0u, nullptr, 0u, &bytes, nullptr)) {
        StoreError(GetLastError(), error);
        return false;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1AvrcpObserverWin32Io::Dequeue(NLD_AVRCP_OBSERVER_EVENT* event,
                                     DWORD* error) {
    if (event == nullptr || handle_ == INVALID_HANDLE_VALUE) {
        StoreError(ERROR_INVALID_HANDLE, error);
        return false;
    }
    DWORD bytes = 0u;
    const BOOL ok = DeviceIoControl(
        handle_, IOCTL_NLD_AVRCP_OBSERVER_DEQUEUE_EVENT, nullptr, 0u, event,
        sizeof(*event), &bytes, nullptr);
    if (!ok || bytes != sizeof(*event)) {
        StoreError(ok ? ERROR_INVALID_DATA : GetLastError(), error);
        return false;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

V1AvrcpObserverHost::V1AvrcpObserverHost(V1AvrcpObserverIo* io,
                                         bool write_enabled)
    : io_(io), write_enabled_(write_enabled) {}

V1AvrcpBluetoothWriter* V1AvrcpObserverHost::writer() {
    return write_enabled_ && io_ != nullptr ? io_->AsWriter() : nullptr;
}

bool V1AvrcpObserverHost::EnsureOpened(
    DWORD* error,
    V1AvrcpObserverActivationResult* result) {
    if (io_ == nullptr || result == nullptr) {
        StoreError(ERROR_INVALID_PARAMETER, error);
        return false;
    }
    if (!observer_open_) {
        ++stats_.interface_open_attempts;
        DWORD open_error = ERROR_SUCCESS;
        const bool opened = write_enabled_
            ? io_->OpenReadWrite(&open_error)
            : io_->OpenReadOnly(&open_error);
        if (!opened) {
            ++stats_.interface_open_failures;
            StoreError(open_error, error);
            *result = open_error == ERROR_FILE_NOT_FOUND ||
                          open_error == ERROR_NOT_FOUND ||
                          open_error == ERROR_NO_MORE_ITEMS
                          ? V1AvrcpObserverActivationResult::Unavailable
                          : V1AvrcpObserverActivationResult::Failed;
            return false;
        }
        observer_open_ = true;
    }
    NLD_AVRCP_OBSERVER_ABI_VERSION version{};
    DWORD version_error = ERROR_SUCCESS;
    if (!io_->GetVersion(&version, &version_error) ||
        version.Size != sizeof(version)) {
        ++stats_.version_failures;
        Close();
        StoreError(version_error, error);
        *result = V1AvrcpObserverActivationResult::Failed;
        return false;
    }
    if (version.Major != NLD_AVRCP_OBSERVER_ABI_MAJOR ||
        version.Minor != NLD_AVRCP_OBSERVER_ABI_MINOR) {
        ++stats_.version_failures;
        Close();
        StoreError(ERROR_REVISION_MISMATCH, error);
        *result = V1AvrcpObserverActivationResult::Incompatible;
        return false;
    }
    *result = V1AvrcpObserverActivationResult::Active;
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1AvrcpObserverHost::ReadStatus(NLD_AVRCP_OBSERVER_STATUS* status,
                                     DWORD* error) {
    if (status == nullptr || !observer_open_) {
        StoreError(ERROR_INVALID_HANDLE, error);
        return false;
    }
    if (!io_->GetStatus(status, error) || status->Size != sizeof(*status)) {
        ++stats_.status_failures;
        return false;
    }
    if (observer_generation_ != 0u &&
        observer_generation_ != status->AclGeneration) {
        observation_requested_ = false;
        activation_terminal_failure_ = false;
        activation_failure_error_ = ERROR_SUCCESS;
        control_channel_ready_ = false;
    }
    observer_generation_ = status->AclGeneration;
    status_flags_ = status->Flags;
    last_protocol_status_ = status->LastProtocolStatus;
    last_open_status_ = status->LastOpenStatus;
    control_channel_ready_ = IsControlChannelReady(*status);
    return true;
}

std::uint64_t V1AvrcpObserverHost::NextOwnerLease() {
    if (owner_lease_ == std::numeric_limits<std::uint64_t>::max()) {
        owner_lease_ = 1u;
    } else {
        ++owner_lease_;
        if (owner_lease_ == 0u) {
            owner_lease_ = 1u;
        }
    }
    return owner_lease_;
}

bool V1AvrcpObserverHost::EnsureMapperLease() {
    if (!media_session_active_ || !mapper_.acl_generation_current) {
        return true;
    }
    if (mapper_.owner_lease == owner_lease_) {
        return true;
    }
    (void)V1AvrcpAcquireOwnerLease(
        &mapper_, mapper_.acl_generation, owner_lease_);
    return mapper_.owner_lease == owner_lease_;
}

bool V1AvrcpObserverHost::PreparePhysicalMapper(DWORD* error) {
    const std::uint64_t generation = options_.acl_generation;
    if (generation == 0u) {
        StoreError(ERROR_INVALID_PARAMETER, error);
        return false;
    }
    if (mapper_.acl_generation_current &&
        mapper_.acl_generation != generation) {
        EndMapperGeneration();
    }
    const bool new_generation = !mapper_.acl_generation_current;
    if (new_generation) {
        const V1AvrcpActionSet begun =
            V1AvrcpBeginAclGeneration(&mapper_, generation);
        if (begun.acl_generation != generation) {
            StoreError(ERROR_INVALID_STATE, error);
            return false;
        }
    }
    mapper_.headset_preferred = options_.headset_preferred;
    options_.owner_lease = owner_lease_;
    if (sink_ != nullptr) {
        AvrcpWindowsVolume current{};
        if (sink_->QueryWindowsVolume(&current)) {
            options_.initial_windows_volume = current;
        }
    }
    (void)V1AvrcpSetControlMode(
        &mapper_, generation, options_.volume_sync, options_.media_routing);
    (void)V1AvrcpAcquireOwnerLease(&mapper_, generation, owner_lease_);
    (void)V1AvrcpObserveWindowsVolume(
        &mapper_, generation, options_.initial_windows_volume);
    const V1AvrcpActionSet media_actions =
        V1AvrcpSetMediaSessionSnapshot(&mapper_, options_.media_session);
    V1AvrcpDispatchAuthorizedActions(
        &mapper_, media_actions, sink_, &mapper_stats_);
    if (!EnsureMapperLease()) {
        StoreError(ERROR_INVALID_STATE, error);
        return false;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1AvrcpObserverHost::FeedDriverEvent(
    const NLD_AVRCP_OBSERVER_EVENT& event) {
    V1AvrcpObservedEvent mapped;
    if (!MapDriverEvent(event, mapper_.acl_generation, &mapped)) {
        ++stats_.ignored_events;
        return true;
    }
    const std::uint64_t errors_before = mapper_stats_.errors;
    if (!V1AvrcpFeedEvent(&mapper_, mapped, options_, sink_, &mapper_stats_)) {
        ++stats_.mapper_errors;
        return false;
    }
    if (mapper_stats_.errors != errors_before) {
        ++stats_.mapper_errors;
    }
    return EnsureMapperLease();
}

V1AvrcpObserverActivationResult V1AvrcpObserverHost::BeginMediaSession(
    const V1AvrcpReplayOptions& options,
    V1AvrcpActionSink* sink,
    DWORD* error) {
    if (io_ == nullptr) {
        StoreError(ERROR_INVALID_PARAMETER, error);
        return V1AvrcpObserverActivationResult::Failed;
    }
    options_ = options;
    sink_ = sink;
    const bool starting_media_session = !media_session_active_;
    if (starting_media_session) {
        media_session_active_ = true;
        NextOwnerLease();
        ++stats_.media_sessions_started;
    }
    V1AvrcpObserverActivationResult result;
    if (!EnsureOpened(error, &result)) {
        if (starting_media_session) EndMediaSession();
        return result;
    }
    NLD_AVRCP_OBSERVER_STATUS status{};
    if (!ReadStatus(&status, error)) {
        if (starting_media_session) EndMediaSession();
        return V1AvrcpObserverActivationResult::Failed;
    }
    if (!PreparePhysicalMapper(error)) {
        if (starting_media_session) EndMediaSession();
        return V1AvrcpObserverActivationResult::Failed;
    }
    if (activation_terminal_failure_) {
        StoreError(activation_failure_error_, error);
        if (starting_media_session) EndMediaSession();
        return V1AvrcpObserverActivationResult::Failed;
    }
    if (!observation_requested_ && IsReadyForActivation(status)) {
        ++stats_.activation_requests;
        DWORD activation_error = ERROR_SUCCESS;
        if (!io_->BeginObservation(&activation_error)) {
            ++stats_.activation_rejected;
            activation_terminal_failure_ = true;
            activation_failure_error_ = activation_error;
            StoreError(activation_error, error);
            if (starting_media_session) EndMediaSession();
            return V1AvrcpObserverActivationResult::Failed;
        }
        ++stats_.activation_accepted;
        observation_requested_ = true;
    } else if ((status.Flags &
                NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUESTED) != 0u) {
        observation_requested_ = true;
    } else {
        if (starting_media_session) EndMediaSession();
        StoreError(ERROR_NOT_READY, error);
        return V1AvrcpObserverActivationResult::Pending;
    }
    if (!EnsureMapperLease()) {
        if (starting_media_session) EndMediaSession();
        StoreError(ERROR_INVALID_STATE, error);
        return V1AvrcpObserverActivationResult::Failed;
    }
    StoreError(ERROR_SUCCESS, error);
    return V1AvrcpObserverActivationResult::Active;
}

bool V1AvrcpObserverHost::Poll(DWORD* error) {
    if (!media_session_active_ || !observer_open_) {
        StoreError(ERROR_SUCCESS, error);
        return true;
    }
    const bool was_control_ready = control_channel_ready_;
    NLD_AVRCP_OBSERVER_STATUS status{};
    if (!ReadStatus(&status, error)) {
        return false;
    }
    bool write_response_observed = false;
    for (unsigned int index = 0u; index < kMaximumEventsPerPoll; ++index) {
        NLD_AVRCP_OBSERVER_EVENT event{};
        DWORD dequeue_error = ERROR_SUCCESS;
        if (!io_->Dequeue(&event, &dequeue_error)) {
            if (dequeue_error == ERROR_NO_MORE_ITEMS) {
                break;
            }
            StoreError(dequeue_error, error);
            return false;
        }
        ++stats_.dequeued_events;
        write_response_observed = write_response_observed ||
            event.Type == NldAvrcpObserverEventWriteResponse;
        if (!FeedDriverEvent(event)) {
            StoreError(ERROR_INVALID_DATA, error);
            return false;
        }
    }
    if (sink_ != nullptr && mapper_.acl_generation_current &&
        mapper_.owner_lease == owner_lease_) {
        AvrcpWindowsVolume notified{};
        if (sink_->ConsumeWindowsVolumeChange(&notified)) {
            ++stats_.windows_volume_notifications;
            const V1AvrcpActionSet actions = V1AvrcpObserveWindowsVolume(
                &mapper_, mapper_.acl_generation, notified);
            V1AvrcpDispatchAuthorizedActions(
                &mapper_, actions, sink_, &mapper_stats_);
        }
        const ULONGLONG now = GetTickCount64();
        if (!sink_->WindowsVolumeNotificationsActive() &&
            now - last_windows_volume_poll_tick_ >=
            kWindowsVolumePollIntervalMs) {
            last_windows_volume_poll_tick_ = now;
            AvrcpWindowsVolume current{};
            if (sink_->QueryWindowsVolume(&current)) {
                ++stats_.windows_volume_polls;
                const V1AvrcpActionSet actions = V1AvrcpObserveWindowsVolume(
                    &mapper_, mapper_.acl_generation, current);
                V1AvrcpDispatchAuthorizedActions(
                    &mapper_, actions, sink_, &mapper_stats_);
            }
        }
    }
    if (sink_ != nullptr && control_channel_ready_ &&
        (!was_control_ready || write_response_observed)) {
        sink_->RetryPendingWrites();
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

void V1AvrcpObserverHost::SetMediaSessionSnapshot(
    const V1MediaSessionSnapshot& snapshot) {
    options_.media_session = snapshot;
    if (mapper_.acl_generation_current) {
        const V1AvrcpActionSet actions =
            V1AvrcpSetMediaSessionSnapshot(&mapper_, snapshot);
        V1AvrcpDispatchAuthorizedActions(
            &mapper_, actions, sink_, &mapper_stats_);
    }
}

void V1AvrcpObserverHost::EndMediaSession() {
    if (!media_session_active_) {
        return;
    }
    if (mapper_.acl_generation_current && mapper_.owner_lease == owner_lease_) {
        (void)V1AvrcpRevokeOwnerLease(
            &mapper_, mapper_.acl_generation, owner_lease_);
    }
    media_session_active_ = false;
    ++stats_.media_sessions_ended;
}

void V1AvrcpObserverHost::ReleaseTransport() {
    EndMediaSession();
    if (io_ != nullptr) {
        io_->Close();
    }
    observer_open_ = false;
    observation_requested_ = false;
    activation_terminal_failure_ = false;
    activation_failure_error_ = ERROR_SUCCESS;
    observer_generation_ = 0u;
    control_channel_ready_ = false;
    status_flags_ = 0u;
    last_protocol_status_ = 0;
    last_open_status_ = 0;
    last_windows_volume_poll_tick_ = 0u;
}

void V1AvrcpObserverHost::EndMapperGeneration() {
    if (mapper_.acl_generation_current) {
        (void)V1AvrcpEndAclGeneration(&mapper_, mapper_.acl_generation);
    }
}

void V1AvrcpObserverHost::Close() {
    ReleaseTransport();
    EndMapperGeneration();
}

}  // namespace native_ldac::agent
