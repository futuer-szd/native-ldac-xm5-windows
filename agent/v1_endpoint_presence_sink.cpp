#include "v1_endpoint_presence_sink.h"

#include <ks.h>
#include <ksmedia.h>
#include <cfgmgr32.h>
#include <setupapi.h>

#include <atomic>
#include <cwchar>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace native_ldac::agent {
namespace {

const GUID kAudioCategory = {STATIC_KSCATEGORY_AUDIO};
const GUID kPcmPropertySet = {STATIC_KSPROPSETID_NativeLdacPcm};

constexpr DWORD kEndpointPropertyQueryTimeoutMs = 250u;
constexpr DWORD kEndpointPropertySetTimeoutMs = 1000u;

struct EndpointPropertyIoState {
    HANDLE device = INVALID_HANDLE_VALUE;
    HANDLE done_event = nullptr;
    std::vector<unsigned char> input;
    std::vector<unsigned char> output;
    bool output_is_input = false;
    bool success = false;
    DWORD error = ERROR_SUCCESS;
    DWORD bytes_returned = 0u;
    std::atomic_long references{2};
};

std::atomic_bool g_endpoint_property_io_in_flight{false};

void ReleaseEndpointPropertyIo(EndpointPropertyIoState* io) {
    if (io == nullptr ||
        io->references.fetch_sub(1, std::memory_order_acq_rel) != 1) {
        return;
    }
    if (io->done_event != nullptr) CloseHandle(io->done_event);
    if (io->device != INVALID_HANDLE_VALUE) CloseHandle(io->device);
    g_endpoint_property_io_in_flight.store(false, std::memory_order_release);
    delete io;
}

void RunEndpointPropertyIoWorker(EndpointPropertyIoState* io) {
    DWORD bytes_returned = 0u;
    io->success = DeviceIoControl(
                      io->device,
                      IOCTL_KS_PROPERTY,
                      io->input.empty() ? nullptr : io->input.data(),
                      static_cast<DWORD>(io->input.size()),
                      io->output.empty() ? nullptr : io->output.data(),
                      static_cast<DWORD>(io->output.size()),
                      &bytes_returned,
                      nullptr) != FALSE;
    io->error = io->success ? ERROR_SUCCESS : GetLastError();
    io->bytes_returned = bytes_returned;
    (void)SetEvent(io->done_event);
    ReleaseEndpointPropertyIo(io);
}

bool RunEndpointPropertyIoctl(HANDLE device,
                              void* input,
                              DWORD input_size,
                              void* output,
                              DWORD output_size,
                              DWORD timeout_ms,
                              bool output_is_input,
                              DWORD* bytes_returned) {
    if (device == INVALID_HANDLE_VALUE || timeout_ms == 0u) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    bool expected = false;
    if (!g_endpoint_property_io_in_flight.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        SetLastError(ERROR_BUSY);
        return false;
    }
    auto* io = new (std::nothrow) EndpointPropertyIoState();
    if (io == nullptr) {
        g_endpoint_property_io_in_flight.store(
            false, std::memory_order_release);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    io->output_is_input = output_is_input;
    if (input_size != 0u && input == nullptr) {
        ReleaseEndpointPropertyIo(io);
        ReleaseEndpointPropertyIo(io);
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    if (output_size != 0u && output == nullptr) {
        ReleaseEndpointPropertyIo(io);
        ReleaseEndpointPropertyIo(io);
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    try {
        if (input_size != 0u) {
            const auto* input_bytes =
                static_cast<const unsigned char*>(input);
            io->input.assign(input_bytes, input_bytes + input_size);
        }
        io->output.resize(output_size);
        if (output_is_input && output_size != 0u) {
            std::memcpy(io->output.data(), output, output_size);
        }
    } catch (...) {
        ReleaseEndpointPropertyIo(io);
        ReleaseEndpointPropertyIo(io);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    if (!DuplicateHandle(GetCurrentProcess(),
                         device,
                         GetCurrentProcess(),
                         &io->device,
                         0u,
                         FALSE,
                         DUPLICATE_SAME_ACCESS)) {
        ReleaseEndpointPropertyIo(io);
        ReleaseEndpointPropertyIo(io);
        return false;
    }
    io->done_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (io->done_event == nullptr) {
        ReleaseEndpointPropertyIo(io);
        ReleaseEndpointPropertyIo(io);
        return false;
    }

    std::thread worker;
    try {
        worker = std::thread([io] { RunEndpointPropertyIoWorker(io); });
    } catch (...) {
        ReleaseEndpointPropertyIo(io);
        ReleaseEndpointPropertyIo(io);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }

    const DWORD wait = WaitForSingleObject(io->done_event, timeout_ms);
    if (wait == WAIT_TIMEOUT) {
        (void)CancelSynchronousIo(worker.native_handle());
        worker.detach();
        ReleaseEndpointPropertyIo(io);
        SetLastError(ERROR_TIMEOUT);
        return false;
    }
    if (wait != WAIT_OBJECT_0) {
        const DWORD wait_error = GetLastError();
        (void)CancelSynchronousIo(worker.native_handle());
        worker.detach();
        ReleaseEndpointPropertyIo(io);
        SetLastError(wait_error);
        return false;
    }

    worker.join();
    const bool completed = io->success;
    const DWORD completion_error = io->error;
    if (completed) {
        if (bytes_returned != nullptr) *bytes_returned = io->bytes_returned;
        if (!output_is_input && output_size != 0u) {
            std::memcpy(output, io->output.data(), output_size);
        }
    }
    ReleaseEndpointPropertyIo(io);
    if (!completed) {
        SetLastError(completion_error);
        return false;
    }
    SetLastError(ERROR_SUCCESS);
    return true;
}

bool QueryProperty(HANDLE device,
                   ULONG property_id,
                   void* output,
                   DWORD output_size,
                   DWORD* bytes_returned) {
    KSPROPERTY property{};
    property.Set = kPcmPropertySet;
    property.Id = property_id;
    property.Flags = KSPROPERTY_TYPE_GET;
    return RunEndpointPropertyIoctl(
        device,
        &property,
        sizeof(property),
        output,
        output_size,
        kEndpointPropertyQueryTimeoutMs,
        false,
        bytes_returned);
}

bool SetProperty(HANDLE device,
                 ULONG property_id,
                 void* value,
                 DWORD value_size) {
    KSPROPERTY property{};
    property.Set = kPcmPropertySet;
    property.Id = property_id;
    property.Flags = KSPROPERTY_TYPE_SET;
    DWORD bytes_returned = 0u;
    return RunEndpointPropertyIoctl(
        device,
        &property,
        sizeof(property),
        value,
        value_size,
        kEndpointPropertySetTimeoutMs,
        true,
        &bytes_returned);
}

bool IsNativeLdacEndpoint(HANDLE device) {
    NATIVE_LDAC_PCM_INFO info{};
    DWORD bytes_returned = 0u;
    if (!QueryProperty(device,
                       NativeLdacPcmPropertyInfo,
                       &info,
                       sizeof(info),
                       &bytes_returned) ||
        bytes_returned < sizeof(info) ||
        info.Size != sizeof(info) ||
        info.AbiVersion != NATIVE_LDAC_PCM_ABI_VERSION) {
        return false;
    }
    NATIVE_LDAC_PRESENCE_STATE presence{};
    bytes_returned = 0u;
    return QueryProperty(device,
                         NativeLdacPcmPropertyPhysicalPresence,
                         &presence,
                         sizeof(presence),
                         &bytes_returned) &&
           bytes_returned >= sizeof(presence) &&
           presence.Size == sizeof(presence) &&
           presence.AbiVersion ==
               NATIVE_LDAC_PRESENCE_STATE_ABI_VERSION;
}

}  // namespace

V1EndpointPresenceSink::~V1EndpointPresenceSink() {
    Close();
}

bool V1EndpointPresenceSink::Open(DWORD* error) {
    return OpenForInstanceId(std::wstring(), error);
}

bool V1EndpointPresenceSink::OpenForInstanceId(
    const std::wstring& instance_id,
    DWORD* error) {
    Close();
    HDEVINFO devices = SetupDiGetClassDevsW(
        &kAudioCategory,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) {
        if (error != nullptr) {
            *error = GetLastError();
        }
        return false;
    }

    DWORD final_error = ERROR_NOT_FOUND;
    for (DWORD index = 0u;; ++index) {
        SP_DEVICE_INTERFACE_DATA interface_data{};
        interface_data.cbSize = sizeof(interface_data);
        if (!SetupDiEnumDeviceInterfaces(devices,
                                         nullptr,
                                         &kAudioCategory,
                                         index,
                                         &interface_data)) {
            const DWORD enumeration_error = GetLastError();
            if (enumeration_error != ERROR_NO_MORE_ITEMS) {
                final_error = enumeration_error;
            }
            break;
        }

        DWORD required = 0u;
        (void)SetupDiGetDeviceInterfaceDetailW(devices,
                                               &interface_data,
                                               nullptr,
                                               0u,
                                               &required,
                                               nullptr);
        if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            continue;
        }
        std::vector<unsigned char> storage(required, 0u);
        auto* detail = reinterpret_cast<
            SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(*detail);
        SP_DEVINFO_DATA device_data{};
        device_data.cbSize = sizeof(device_data);
        if (!SetupDiGetDeviceInterfaceDetailW(devices,
                                              &interface_data,
                                              detail,
                                              required,
                                              nullptr,
                                              &device_data)) {
            final_error = GetLastError();
            continue;
        }
        if (!instance_id.empty()) {
            wchar_t observed_instance_id[MAX_DEVICE_ID_LEN]{};
            if (!SetupDiGetDeviceInstanceIdW(
                    devices,
                    &device_data,
                    observed_instance_id,
                    MAX_DEVICE_ID_LEN,
                    nullptr)) {
                final_error = GetLastError();
                continue;
            }
            if (_wcsicmp(instance_id.c_str(), observed_instance_id) != 0) {
                continue;
            }
        }

        HANDLE candidate = CreateFileW(detail->DevicePath,
                                       GENERIC_READ | GENERIC_WRITE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       nullptr,
                                       OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL,
                                       nullptr);
        if (candidate == INVALID_HANDLE_VALUE) {
            final_error = GetLastError();
            continue;
        }
        if (IsNativeLdacEndpoint(candidate)) {
            device_ = candidate;
            final_error = ERROR_SUCCESS;
            break;
        }
        final_error = GetLastError();
        if (final_error == ERROR_SUCCESS) {
            final_error = ERROR_NOT_SUPPORTED;
        }
        CloseHandle(candidate);
    }
    SetupDiDestroyDeviceInfoList(devices);
    if (error != nullptr) {
        *error = final_error;
    }
    SetLastError(final_error);
    return device_ != INVALID_HANDLE_VALUE;
}

void V1EndpointPresenceSink::Close() {
    if (device_ != INVALID_HANDLE_VALUE) {
        CloseHandle(device_);
        device_ = INVALID_HANDLE_VALUE;
    }
}

bool V1EndpointPresenceSink::Set(
    bool present,
    std::uint64_t presence_generation,
    DWORD* error) {
    if (device_ == INVALID_HANDLE_VALUE || presence_generation == 0u) {
        if (error != nullptr) {
            *error = ERROR_INVALID_STATE;
        }
        SetLastError(ERROR_INVALID_STATE);
        return false;
    }
    NATIVE_LDAC_PRESENCE_STATE state{};
    state.Size = sizeof(state);
    state.AbiVersion = NATIVE_LDAC_PRESENCE_STATE_ABI_VERSION;
    state.State = present ? NativeLdacPresencePresent
                          : NativeLdacPresenceAbsent;
    state.Flags = NATIVE_LDAC_PRESENCE_STATE_FLAG_NONE;
    state.PresenceGeneration = presence_generation;
    const bool set = SetProperty(device_,
                                 NativeLdacPcmPropertyPhysicalPresence,
                                 &state,
                                 sizeof(state));
    const DWORD set_error = set ? ERROR_SUCCESS : GetLastError();
    if (set) {
        NATIVE_LDAC_PRESENCE_STATE observed{};
        DWORD query_error = ERROR_SUCCESS;
        if (!Query(&observed, &query_error) ||
            observed.State != state.State ||
            observed.PresenceGeneration != presence_generation) {
            if (error != nullptr) {
                *error = query_error == ERROR_SUCCESS
                    ? ERROR_INVALID_DATA
                    : query_error;
            }
            SetLastError(query_error == ERROR_SUCCESS
                             ? ERROR_INVALID_DATA
                             : query_error);
            return false;
        }
    }
    if (error != nullptr) {
        *error = set_error;
    }
    return set;
}

bool V1EndpointPresenceSink::Query(
    NATIVE_LDAC_PRESENCE_STATE* state,
    DWORD* error) const {
    if (device_ == INVALID_HANDLE_VALUE || state == nullptr) {
        if (error != nullptr) {
            *error = ERROR_INVALID_PARAMETER;
        }
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    *state = {};
    DWORD bytes_returned = 0u;
    const bool queried = QueryProperty(
        device_,
        NativeLdacPcmPropertyPhysicalPresence,
        state,
        sizeof(*state),
        &bytes_returned);
    const DWORD query_error = queried ? ERROR_SUCCESS : GetLastError();
    const bool valid = queried &&
                       bytes_returned >= sizeof(*state) &&
                       state->Size == sizeof(*state) &&
                       state->AbiVersion ==
                           NATIVE_LDAC_PRESENCE_STATE_ABI_VERSION &&
                       state->Flags == NATIVE_LDAC_PRESENCE_STATE_FLAG_NONE &&
                       state->State <= NativeLdacPresencePresent;
    if (error != nullptr) {
        *error = valid ? ERROR_SUCCESS
                       : (queried ? ERROR_INVALID_DATA : query_error);
    }
    if (!valid) {
        SetLastError(queried ? ERROR_INVALID_DATA : query_error);
    }
    return valid;
}

bool V1EndpointPresenceSink::QueryRenderActive(
    bool* active,
    std::uint64_t* stream_epoch,
    DWORD* error) const {
    if (device_ == INVALID_HANDLE_VALUE || active == nullptr ||
        stream_epoch == nullptr) {
        if (error != nullptr) {
            *error = ERROR_INVALID_PARAMETER;
        }
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    NATIVE_LDAC_PCM_INFO info{};
    DWORD bytes_returned = 0u;
    const bool queried = QueryProperty(device_,
                                       NativeLdacPcmPropertyInfo,
                                       &info,
                                       sizeof(info),
                                       &bytes_returned);
    const DWORD query_error = queried ? ERROR_SUCCESS : GetLastError();
    const bool valid = queried &&
                       bytes_returned >= sizeof(info) &&
                       info.Size == sizeof(info) &&
                       info.AbiVersion == NATIVE_LDAC_PCM_ABI_VERSION &&
                       info.CapacityBytes ==
                           NATIVE_LDAC_PCM_RING_CAPACITY_BYTES &&
                       info.AvailableBytes <= info.CapacityBytes;
    if (!valid) {
        const DWORD final_error = queried ? ERROR_INVALID_DATA : query_error;
        if (error != nullptr) {
            *error = final_error;
        }
        SetLastError(final_error);
        return false;
    }
    *active = (info.Flags & NATIVE_LDAC_PCM_FLAG_STREAM_ACTIVE) != 0u;
    *stream_epoch = info.StreamEpoch;
    if (error != nullptr) {
        *error = ERROR_SUCCESS;
    }
    return true;
}

}  // namespace native_ldac::agent
