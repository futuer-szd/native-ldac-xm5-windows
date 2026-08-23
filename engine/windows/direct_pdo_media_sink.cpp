// SPDX-License-Identifier: Apache-2.0
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>

#include <ks.h>
#include <ksmedia.h>
#include <setupapi.h>

#include <cstring>
#include <new>
#include <vector>

#include "ldac_native/direct_pdo_media_sink.h"
#include "nativeldac_direct_pdo_public.h"

static const GUID g_direct_pdo_property_set = {
    STATIC_KSPROPSETID_NativeLdacDirectPdo};
static const GUID g_audio_category = {STATIC_KSCATEGORY_AUDIO};

struct direct_pdo_media_sink {
    HANDLE device = INVALID_HANDLE_VALUE;
    unsigned long last_error = ERROR_SUCCESS;
};

static direct_pdo_media_sink_status map_error(unsigned long error) {
    if (error == ERROR_SET_NOT_FOUND || error == ERROR_NOT_FOUND ||
        error == ERROR_NOT_SUPPORTED || error == ERROR_INVALID_FUNCTION) {
        return DIRECT_PDO_MEDIA_SINK_UNSUPPORTED;
    }
    if (error == ERROR_NOT_READY || error == ERROR_DEVICE_NOT_CONNECTED) {
        return DIRECT_PDO_MEDIA_SINK_NOT_READY;
    }
    if (error == ERROR_RETRY) {
        return DIRECT_PDO_MEDIA_SINK_STALE_SESSION;
    }
    return DIRECT_PDO_MEDIA_SINK_IO_ERROR;
}

extern "C" direct_pdo_media_sink_status direct_pdo_media_sink_create(
    const wchar_t* interface_path,
    direct_pdo_media_sink** out) {
    if (interface_path == nullptr || interface_path[0] == L'\0' ||
        out == nullptr) {
        return DIRECT_PDO_MEDIA_SINK_INVALID_ARGUMENT;
    }
    *out = nullptr;
    auto* sink = new (std::nothrow) direct_pdo_media_sink();
    if (sink == nullptr) return DIRECT_PDO_MEDIA_SINK_NO_MEMORY;
    sink->device = CreateFileW(interface_path,
                               GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);
    if (sink->device == INVALID_HANDLE_VALUE) {
        sink->last_error = GetLastError();
        delete sink;
        return DIRECT_PDO_MEDIA_SINK_OPEN_FAILED;
    }
    *out = sink;
    return DIRECT_PDO_MEDIA_SINK_OK;
}

extern "C" direct_pdo_media_sink_status direct_pdo_media_sink_create_first(
    direct_pdo_media_sink** out,
    wchar_t* interface_path,
    size_t interface_path_capacity) {
    if (out == nullptr || interface_path == nullptr ||
        interface_path_capacity == 0u) {
        return DIRECT_PDO_MEDIA_SINK_INVALID_ARGUMENT;
    }
    *out = nullptr;
    interface_path[0] = L'\0';
    HDEVINFO devices = SetupDiGetClassDevsW(
        &g_audio_category,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) {
        return DIRECT_PDO_MEDIA_SINK_OPEN_FAILED;
    }

    direct_pdo_media_sink_status result =
        DIRECT_PDO_MEDIA_SINK_UNSUPPORTED;
    SP_DEVICE_INTERFACE_DATA interface_data{};
    interface_data.cbSize = sizeof(interface_data);
    for (DWORD index = 0;
         SetupDiEnumDeviceInterfaces(devices,
                                     nullptr,
                                     &g_audio_category,
                                     index,
                                     &interface_data);
         ++index) {
        DWORD required_size = 0;
        (void)SetupDiGetDeviceInterfaceDetailW(devices,
                                               &interface_data,
                                               nullptr,
                                               0,
                                               &required_size,
                                               nullptr);
        if (required_size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            continue;
        }
        std::vector<unsigned char> detail_storage(required_size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
            detail_storage.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(devices,
                                               &interface_data,
                                               detail,
                                               required_size,
                                               nullptr,
                                               nullptr)) {
            continue;
        }
        direct_pdo_media_sink* candidate = nullptr;
        result = direct_pdo_media_sink_create(detail->DevicePath, &candidate);
        if (result != DIRECT_PDO_MEDIA_SINK_OK) continue;
        NLD_DIRECT_PDO_MEDIA_STATUS_V1 status{};
        result = direct_pdo_media_sink_get_status(candidate, &status);
        if (result == DIRECT_PDO_MEDIA_SINK_OK) {
            if (wcsncpy_s(interface_path,
                          interface_path_capacity,
                          detail->DevicePath,
                          _TRUNCATE) != 0) {
                direct_pdo_media_sink_destroy(candidate);
                result = DIRECT_PDO_MEDIA_SINK_INVALID_ARGUMENT;
            } else {
                *out = candidate;
            }
            break;
        }
        direct_pdo_media_sink_destroy(candidate);
    }
    SetupDiDestroyDeviceInfoList(devices);
    return result;
}

extern "C" void direct_pdo_media_sink_destroy(
    direct_pdo_media_sink* sink) {
    if (sink == nullptr) return;
    if (sink->device != INVALID_HANDLE_VALUE) CloseHandle(sink->device);
    delete sink;
}

extern "C" direct_pdo_media_sink_status direct_pdo_media_sink_get_status(
    direct_pdo_media_sink* sink,
    NLD_DIRECT_PDO_MEDIA_STATUS_V1* status) {
    if (sink == nullptr || status == nullptr) {
        return DIRECT_PDO_MEDIA_SINK_INVALID_ARGUMENT;
    }
    KSPROPERTY property{};
    property.Set = g_direct_pdo_property_set;
    property.Id = NldDirectPdoPropertyMediaStatus;
    property.Flags = KSPROPERTY_TYPE_GET;
    DWORD bytes_returned = 0;
    std::memset(status, 0, sizeof(*status));
    if (!DeviceIoControl(sink->device,
                         IOCTL_KS_PROPERTY,
                         &property,
                         sizeof(property),
                         status,
                         sizeof(*status),
                         &bytes_returned,
                         nullptr)) {
        sink->last_error = GetLastError();
        return map_error(sink->last_error);
    }
    if (bytes_returned < sizeof(*status) || status->Size != sizeof(*status) ||
        status->Version != NLD_DIRECT_PDO_MEDIA_ABI_VERSION) {
        sink->last_error = ERROR_INVALID_DATA;
        return DIRECT_PDO_MEDIA_SINK_IO_ERROR;
    }
    sink->last_error = ERROR_SUCCESS;
    return DIRECT_PDO_MEDIA_SINK_OK;
}

extern "C" direct_pdo_media_sink_status direct_pdo_media_sink_write(
    direct_pdo_media_sink* sink,
    unsigned long media_generation,
    const void* packet,
    size_t packet_size) {
    if (sink == nullptr || media_generation == 0u || packet == nullptr ||
        packet_size == 0u ||
        packet_size > NLD_DIRECT_PDO_MEDIA_MAX_PACKET_SIZE) {
        return DIRECT_PDO_MEDIA_SINK_INVALID_ARGUMENT;
    }
    const size_t value_size = NLD_DIRECT_PDO_MEDIA_PACKET_HEADER_SIZE +
                              packet_size;
    std::vector<unsigned char> value(value_size, 0u);
    auto* request = reinterpret_cast<NLD_DIRECT_PDO_MEDIA_PACKET_V1*>(
        value.data());
    request->Size = static_cast<NLD_DIRECT_PDO_MEDIA_U32>(value_size);
    request->Version = NLD_DIRECT_PDO_MEDIA_ABI_VERSION;
    request->MediaGeneration = media_generation;
    request->PayloadLength =
        static_cast<NLD_DIRECT_PDO_MEDIA_U32>(packet_size);
    std::memcpy(value.data() + NLD_DIRECT_PDO_MEDIA_PACKET_HEADER_SIZE,
                packet,
                packet_size);

    KSPROPERTY property{};
    property.Set = g_direct_pdo_property_set;
    property.Id = NldDirectPdoPropertyMediaPacket;
    property.Flags = KSPROPERTY_TYPE_SET;
    DWORD bytes_returned = 0;
    if (!DeviceIoControl(sink->device,
                         IOCTL_KS_PROPERTY,
                         &property,
                         sizeof(property),
                         value.data(),
                         static_cast<DWORD>(value.size()),
                         &bytes_returned,
                         nullptr)) {
        sink->last_error = GetLastError();
        return map_error(sink->last_error);
    }
    sink->last_error = ERROR_SUCCESS;
    return DIRECT_PDO_MEDIA_SINK_OK;
}

extern "C" unsigned long direct_pdo_media_sink_last_error(
    const direct_pdo_media_sink* sink) {
    return sink == nullptr ? ERROR_INVALID_PARAMETER : sink->last_error;
}
