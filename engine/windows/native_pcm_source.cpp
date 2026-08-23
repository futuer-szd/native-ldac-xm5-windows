// SPDX-License-Identifier: Apache-2.0
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>

#include <endpointvolume.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <setupapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "ldac_native/native_pcm_source.h"
#include "native_pcm_volume_logic.h"
#include "nativeldac_pcm_abi.h"
#include "nativeldac_remote_container.h"

static const GUID g_audio_category = { STATIC_KSCATEGORY_AUDIO };
static const GUID g_pcm_property_set = { STATIC_KSPROPSETID_NativeLdacPcm };

struct native_pcm_source {
    HANDLE device = INVALID_HANDLE_VALUE;
    wchar_t interface_path[NATIVE_PCM_SOURCE_PATH_CAPACITY] = L"";
    NATIVE_LDAC_PCM_INFO info{};
    std::vector<float> fifo;
    unsigned long last_error = ERROR_SUCCESS;
    std::uint64_t fifo_epoch = 0u;
    IAudioEndpointVolume *endpoint_volume = nullptr;
    bool com_initialized = false;
    native_pcm_volume_state volume;
    ULONGLONG next_volume_bind_tick = 0u;
    ULONGLONG next_volume_refresh_tick = 0u;
    NATIVE_LDAC_PREFERRED_FORMAT preferred_format{};
    std::uint64_t consumer_generation = 0u;
    bool apply_endpoint_volume = true;
    bool consumer_lease_acquired = false;
};

static void try_bind_endpoint_volume(native_pcm_source *source, bool force);

static void refresh_endpoint_volume(native_pcm_source *source, bool force) {
    const ULONGLONG now = GetTickCount64();
    if (source->endpoint_volume == nullptr) {
        try_bind_endpoint_volume(source, force);
        return;
    }
    if (!force && now < source->next_volume_refresh_tick) return;
    source->next_volume_refresh_tick = now + 20u;

    BOOL muted = FALSE;
    float scalar = 1.0f;
    float decibels = 0.0f;
    if (FAILED(source->endpoint_volume->GetMute(&muted)) ||
        FAILED(source->endpoint_volume->GetMasterVolumeLevelScalar(&scalar)) ||
        FAILED(source->endpoint_volume->GetMasterVolumeLevel(&decibels))) {
        source->endpoint_volume->Release();
        source->endpoint_volume = nullptr;
        native_pcm_volume_fail_muted(&source->volume);
        source->next_volume_bind_tick = now + 1000u;
        return;
    }
    native_pcm_volume_set_endpoint(&source->volume,
                                   muted != FALSE,
                                   scalar,
                                   decibels);
}

static void try_bind_endpoint_volume(native_pcm_source *source, bool force) {
    const ULONGLONG now = GetTickCount64();
    if (source->endpoint_volume != nullptr ||
        (!force && now < source->next_volume_bind_tick)) {
        return;
    }
    source->next_volume_bind_tick = now + 1000u;

    HRESULT hr;
    IMMDeviceEnumerator *enumerator = nullptr;
    IMMDeviceCollection *collection = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                          nullptr,
                          CLSCTX_INPROC_SERVER,
                          __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void **>(&enumerator));
    if (FAILED(hr) || enumerator == nullptr) goto cleanup;
    hr = enumerator->EnumAudioEndpoints(eRender,
                                        DEVICE_STATE_ACTIVE,
                                        &collection);
    if (FAILED(hr) || collection == nullptr) goto cleanup;

    UINT count = 0u;
    if (FAILED(collection->GetCount(&count))) goto cleanup;
    for (UINT index = 0u; index < count; ++index) {
        IMMDevice *endpoint = nullptr;
        IPropertyStore *properties = nullptr;
        PROPVARIANT name;
        PROPVARIANT container;
        PropVariantInit(&name);
        PropVariantInit(&container);

        hr = collection->Item(index, &endpoint);
        if (SUCCEEDED(hr) && endpoint != nullptr) {
            hr = endpoint->OpenPropertyStore(STGM_READ, &properties);
        }
        if (SUCCEEDED(hr) && properties != nullptr) {
            hr = properties->GetValue(PKEY_Device_FriendlyName, &name);
        }
        if (SUCCEEDED(hr) && properties != nullptr) {
            hr = properties->GetValue(PKEY_Device_ContainerId, &container);
        }
        const bool matches_name = name.vt == VT_LPWSTR &&
            name.pwszVal != nullptr &&
            wcsstr(name.pwszVal, L"Native LDAC") != nullptr;
        const bool matches_container = SUCCEEDED(hr) &&
            container.vt == VT_CLSID && container.puuid != nullptr &&
            IsEqualGUID(*container.puuid, NativeLdacRemoteContainerId);
        const bool matches = matches_name && matches_container;
        if (matches) {
            hr = endpoint->Activate(
                __uuidof(IAudioEndpointVolume),
                CLSCTX_INPROC_SERVER,
                nullptr,
                reinterpret_cast<void **>(&source->endpoint_volume));
        }

        PropVariantClear(&name);
        PropVariantClear(&container);
        if (properties != nullptr) properties->Release();
        if (endpoint != nullptr) endpoint->Release();
        if (matches && SUCCEEDED(hr) &&
            source->endpoint_volume != nullptr) {
            refresh_endpoint_volume(source, true);
            break;
        }
    }

cleanup:
    if (collection != nullptr) collection->Release();
    if (enumerator != nullptr) enumerator->Release();
}

static void initialize_endpoint_volume(native_pcm_source *source) {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        source->com_initialized = true;
    } else if (hr != RPC_E_CHANGED_MODE) {
        return;
    }
    try_bind_endpoint_volume(source, true);
}

static BOOL query_property(native_pcm_source *source,
                           ULONG property_id,
                           void *output,
                           DWORD output_size,
                           DWORD *bytes_returned) {
    KSPROPERTY property{};

    property.Set = g_pcm_property_set;
    property.Id = property_id;
    property.Flags = KSPROPERTY_TYPE_GET;
    if (DeviceIoControl(source->device,
                        IOCTL_KS_PROPERTY,
                        &property,
                        static_cast<DWORD>(sizeof(property)),
                        output,
                        output_size,
                        bytes_returned,
                        nullptr)) {
        source->last_error = ERROR_SUCCESS;
        return TRUE;
    }
    source->last_error = GetLastError();
    return FALSE;
}

static BOOL set_property(native_pcm_source *source,
                         ULONG property_id,
                         void *value,
                         DWORD value_size) {
    KSPROPERTY property{};
    DWORD bytes_returned = 0u;

    property.Set = g_pcm_property_set;
    property.Id = property_id;
    property.Flags = KSPROPERTY_TYPE_SET;
    if (DeviceIoControl(source->device,
                        IOCTL_KS_PROPERTY,
                        &property,
                        static_cast<DWORD>(sizeof(property)),
                        value,
                        value_size,
                        &bytes_returned,
                        nullptr)) {
        source->last_error = ERROR_SUCCESS;
        return TRUE;
    }
    source->last_error = GetLastError();
    return FALSE;
}

static native_pcm_source_status property_error_status(
    const native_pcm_source *source) {
    if (source->last_error == ERROR_SET_NOT_FOUND ||
        source->last_error == ERROR_NOT_FOUND ||
        source->last_error == ERROR_NOT_SUPPORTED ||
        source->last_error == ERROR_INVALID_FUNCTION) {
        return NATIVE_PCM_SOURCE_UNSUPPORTED_PROPERTY;
    }
    return NATIVE_PCM_SOURCE_IO_ERROR;
}

static bool supported_preferred_format(
    const NATIVE_LDAC_PREFERRED_FORMAT &format) {
    return format.Size == sizeof(format) &&
           format.AbiVersion == NATIVE_LDAC_FORMAT_ABI_VERSION &&
           format.Flags == NATIVE_LDAC_FORMAT_FLAG_NONE &&
           (format.SampleRate == 44100u || format.SampleRate == 48000u ||
            format.SampleRate == 88200u || format.SampleRate == 96000u) &&
           (format.BitsPerSample == 16u || format.BitsPerSample == 24u) &&
           (format.SupportedSampleRates &
                (NATIVE_LDAC_FORMAT_RATE_44100 |
                 NATIVE_LDAC_FORMAT_RATE_48000 |
                 NATIVE_LDAC_FORMAT_RATE_88200 |
                 NATIVE_LDAC_FORMAT_RATE_96000)) != 0u &&
           (format.SupportedBitsPerSample &
                (NATIVE_LDAC_FORMAT_BITS_16 |
                 NATIVE_LDAC_FORMAT_BITS_24)) != 0u;
}

static bool supported_format(const NATIVE_LDAC_PCM_INFO &info) {
    const bool supported_sample_rate =
        info.SampleRate == 44100u || info.SampleRate == 48000u ||
        info.SampleRate == 88200u || info.SampleRate == 96000u;
    const ULONG expected_block_align = info.BitsPerSample == 24u
        ? info.Channels * 4u
        : info.Channels * 2u;
    return info.Size >= sizeof(info) &&
           info.AbiVersion == NATIVE_LDAC_PCM_ABI_VERSION &&
           supported_sample_rate &&
           info.Channels == 2u &&
           (info.BitsPerSample == 16u || info.BitsPerSample == 24u) &&
           info.BlockAlign == expected_block_align;
}

static native_pcm_source_status open_interface(native_pcm_source *source) {
    HDEVINFO device_info = SetupDiGetClassDevsW(
        &g_audio_category,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (device_info == INVALID_HANDLE_VALUE) {
        source->last_error = GetLastError();
        return NATIVE_PCM_SOURCE_IO_ERROR;
    }

    SP_DEVICE_INTERFACE_DATA interface_data{};
    interface_data.cbSize = sizeof(interface_data);
    native_pcm_source_status status = NATIVE_PCM_SOURCE_NOT_FOUND;
    source->last_error = ERROR_NOT_FOUND;

    for (DWORD index = 0u;
         SetupDiEnumDeviceInterfaces(device_info,
                                     nullptr,
                                     &g_audio_category,
                                     index,
                                     &interface_data);
         ++index) {
        DWORD required_size = 0u;
        (void)SetupDiGetDeviceInterfaceDetailW(device_info,
                                               &interface_data,
                                               nullptr,
                                               0u,
                                               &required_size,
                                               nullptr);
        if (required_size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            continue;
        }

        auto *detail = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, required_size));
        if (detail == nullptr) {
            source->last_error = ERROR_OUTOFMEMORY;
            status = NATIVE_PCM_SOURCE_NO_MEMORY;
            break;
        }
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(device_info,
                                              &interface_data,
                                              detail,
                                              required_size,
                                              nullptr,
                                              nullptr)) {
            HeapFree(GetProcessHeap(), 0u, detail);
            continue;
        }

        HANDLE candidate = CreateFileW(detail->DevicePath,
                                       GENERIC_READ | GENERIC_WRITE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       nullptr,
                                       OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL,
                                       nullptr);
        if (candidate == INVALID_HANDLE_VALUE) {
            HeapFree(GetProcessHeap(), 0u, detail);
            continue;
        }

        source->device = candidate;
        NATIVE_LDAC_PCM_INFO candidate_info{};
        DWORD bytes_returned = 0u;
        if (query_property(source,
                           NativeLdacPcmPropertyInfo,
                           &candidate_info,
                           static_cast<DWORD>(sizeof(candidate_info)),
                           &bytes_returned) &&
            bytes_returned >= sizeof(candidate_info) &&
            candidate_info.AbiVersion == NATIVE_LDAC_PCM_ABI_VERSION) {
            if (!supported_format(candidate_info)) {
                source->last_error = ERROR_NOT_SUPPORTED;
                status = NATIVE_PCM_SOURCE_UNSUPPORTED_FORMAT;
                CloseHandle(candidate);
                source->device = INVALID_HANDLE_VALUE;
                HeapFree(GetProcessHeap(), 0u, detail);
                break;
            }
            (void)wcsncpy_s(source->interface_path,
                            NATIVE_PCM_SOURCE_PATH_CAPACITY,
                            detail->DevicePath,
                            _TRUNCATE);
            source->info = candidate_info;
            source->fifo_epoch = candidate_info.StreamEpoch;
            NATIVE_LDAC_PREFERRED_FORMAT preferred{};
            if (query_property(source,
                               NativeLdacPcmPropertyPreferredFormat,
                               &preferred,
                               static_cast<DWORD>(sizeof(preferred)),
                               &bytes_returned) &&
                bytes_returned >= sizeof(preferred) &&
                supported_preferred_format(preferred)) {
                source->preferred_format = preferred;
            } else {
                source->preferred_format.Size = sizeof(preferred);
                source->preferred_format.AbiVersion =
                    NATIVE_LDAC_FORMAT_ABI_VERSION;
                source->preferred_format.SampleRate = candidate_info.SampleRate;
                source->preferred_format.BitsPerSample =
                    candidate_info.BitsPerSample;
            }
            source->last_error = ERROR_SUCCESS;
            status = NATIVE_PCM_SOURCE_OK;
            HeapFree(GetProcessHeap(), 0u, detail);
            break;
        }

        CloseHandle(candidate);
        source->device = INVALID_HANDLE_VALUE;
        HeapFree(GetProcessHeap(), 0u, detail);
    }

    SetupDiDestroyDeviceInfoList(device_info);
    return status;
}

extern "C" native_pcm_source_status native_pcm_source_create(
    native_pcm_source **out) {
    if (out == nullptr) return NATIVE_PCM_SOURCE_INVALID_ARGUMENT;
    *out = nullptr;

    auto *source = new (std::nothrow) native_pcm_source();
    if (source == nullptr) return NATIVE_PCM_SOURCE_NO_MEMORY;
    native_pcm_source_status status = open_interface(source);
    if (status != NATIVE_PCM_SOURCE_OK) {
        delete source;
        return status;
    }
    initialize_endpoint_volume(source);
    *out = source;
    return NATIVE_PCM_SOURCE_OK;
}

extern "C" native_pcm_source_status native_pcm_source_create_for_interface(
    const wchar_t *interface_path,
    native_pcm_source **out) {
    if (interface_path == nullptr || interface_path[0] == L'\0' ||
        out == nullptr) {
        return NATIVE_PCM_SOURCE_INVALID_ARGUMENT;
    }
    *out = nullptr;
    auto *source = new (std::nothrow) native_pcm_source();
    if (source == nullptr) return NATIVE_PCM_SOURCE_NO_MEMORY;
    source->device = CreateFileW(interface_path,
                                 GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr,
                                 OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL,
                                 nullptr);
    if (source->device == INVALID_HANDLE_VALUE) {
        source->last_error = GetLastError();
        delete source;
        return NATIVE_PCM_SOURCE_NOT_FOUND;
    }

    NATIVE_LDAC_PCM_INFO candidate_info{};
    DWORD bytes_returned = 0u;
    if (!query_property(source,
                        NativeLdacPcmPropertyInfo,
                        &candidate_info,
                        static_cast<DWORD>(sizeof(candidate_info)),
                        &bytes_returned) ||
        bytes_returned < sizeof(candidate_info) ||
        candidate_info.AbiVersion != NATIVE_LDAC_PCM_ABI_VERSION) {
        CloseHandle(source->device);
        source->device = INVALID_HANDLE_VALUE;
        delete source;
        return NATIVE_PCM_SOURCE_NOT_FOUND;
    }
    if (!supported_format(candidate_info)) {
        CloseHandle(source->device);
        source->device = INVALID_HANDLE_VALUE;
        delete source;
        return NATIVE_PCM_SOURCE_UNSUPPORTED_FORMAT;
    }
    (void)wcsncpy_s(source->interface_path,
                    NATIVE_PCM_SOURCE_PATH_CAPACITY,
                    interface_path,
                    _TRUNCATE);
    source->info = candidate_info;
    source->fifo_epoch = candidate_info.StreamEpoch;
    NATIVE_LDAC_PREFERRED_FORMAT preferred{};
    if (query_property(source,
                       NativeLdacPcmPropertyPreferredFormat,
                       &preferred,
                       static_cast<DWORD>(sizeof(preferred)),
                       &bytes_returned) &&
        bytes_returned >= sizeof(preferred) &&
        supported_preferred_format(preferred)) {
        source->preferred_format = preferred;
    } else {
        source->preferred_format.Size = sizeof(preferred);
        source->preferred_format.AbiVersion = NATIVE_LDAC_FORMAT_ABI_VERSION;
        source->preferred_format.SampleRate = candidate_info.SampleRate;
        source->preferred_format.BitsPerSample = candidate_info.BitsPerSample;
    }
    source->last_error = ERROR_SUCCESS;
    initialize_endpoint_volume(source);
    *out = source;
    return NATIVE_PCM_SOURCE_OK;
}

extern "C" void native_pcm_source_destroy(native_pcm_source *source) {
    if (source == nullptr) return;
    (void)native_pcm_source_release_consumer(source);
    if (source->endpoint_volume != nullptr) {
        source->endpoint_volume->Release();
        source->endpoint_volume = nullptr;
    }
    if (source->device != INVALID_HANDLE_VALUE) {
        CloseHandle(source->device);
        source->device = INVALID_HANDLE_VALUE;
    }
    if (source->com_initialized) {
        CoUninitialize();
        source->com_initialized = false;
    }
    delete source;
}

extern "C" native_pcm_source_status native_pcm_source_acquire_consumer(
    native_pcm_source *source,
    uint64_t consumer_generation) {
    if (source == nullptr || consumer_generation == 0u) {
        return NATIVE_PCM_SOURCE_INVALID_ARGUMENT;
    }
    if (source->consumer_lease_acquired) {
        return source->consumer_generation == consumer_generation
            ? NATIVE_PCM_SOURCE_OK
            : NATIVE_PCM_SOURCE_INVALID_ARGUMENT;
    }

    NATIVE_LDAC_PCM_CONSUMER_LEASE lease{};
    lease.Size = sizeof(lease);
    lease.AbiVersion = NATIVE_LDAC_PCM_CONSUMER_LEASE_ABI_VERSION;
    lease.State = NativeLdacPcmConsumerAcquired;
    lease.Flags = NATIVE_LDAC_PCM_CONSUMER_LEASE_FLAG_NONE;
    lease.ConsumerGeneration = consumer_generation;
    if (!set_property(source,
                      NativeLdacPcmPropertyConsumerLease,
                      &lease,
                      static_cast<DWORD>(sizeof(lease)))) {
        return property_error_status(source);
    }
    source->consumer_generation = consumer_generation;
    source->consumer_lease_acquired = true;
    return NATIVE_PCM_SOURCE_OK;
}

extern "C" native_pcm_source_status native_pcm_source_release_consumer(
    native_pcm_source *source) {
    if (source == nullptr) {
        return NATIVE_PCM_SOURCE_INVALID_ARGUMENT;
    }
    if (!source->consumer_lease_acquired) {
        return NATIVE_PCM_SOURCE_OK;
    }

    NATIVE_LDAC_PCM_CONSUMER_LEASE lease{};
    lease.Size = sizeof(lease);
    lease.AbiVersion = NATIVE_LDAC_PCM_CONSUMER_LEASE_ABI_VERSION;
    lease.State = NativeLdacPcmConsumerReleased;
    lease.Flags = NATIVE_LDAC_PCM_CONSUMER_LEASE_FLAG_NONE;
    lease.ConsumerGeneration = source->consumer_generation;
    if (!set_property(source,
                      NativeLdacPcmPropertyConsumerLease,
                      &lease,
                      static_cast<DWORD>(sizeof(lease)))) {
        return property_error_status(source);
    }
    source->consumer_generation = 0u;
    source->consumer_lease_acquired = false;
    return NATIVE_PCM_SOURCE_OK;
}

static native_pcm_source_status drain_driver(native_pcm_source *source) {
    alignas(NATIVE_LDAC_PCM_READ_HEADER) unsigned char buffer[
        sizeof(NATIVE_LDAC_PCM_READ_HEADER) +
        NATIVE_LDAC_PCM_MAX_READ_BYTES]{};
    DWORD bytes_returned = 0u;
    if (!query_property(source,
                        NativeLdacPcmPropertyRead,
                        buffer,
                        static_cast<DWORD>(sizeof(buffer)),
                        &bytes_returned)) {
        return NATIVE_PCM_SOURCE_IO_ERROR;
    }
    if (bytes_returned < sizeof(NATIVE_LDAC_PCM_READ_HEADER)) {
        source->last_error = ERROR_INVALID_DATA;
        return NATIVE_PCM_SOURCE_IO_ERROR;
    }

    const auto *result =
        reinterpret_cast<const NATIVE_LDAC_PCM_READ_HEADER *>(buffer);
    const DWORD payload_capacity =
        bytes_returned - static_cast<DWORD>(sizeof(*result));
    if (!supported_format(result->InfoBeforeRead) ||
        result->BytesReturned > payload_capacity ||
        result->BytesReturned % result->InfoBeforeRead.BlockAlign != 0u) {
        source->last_error = ERROR_INVALID_DATA;
        return NATIVE_PCM_SOURCE_IO_ERROR;
    }

    if (source->fifo_epoch != result->InfoBeforeRead.StreamEpoch) {
        source->fifo.clear();
        source->fifo_epoch = result->InfoBeforeRead.StreamEpoch;
    }
    source->info = result->InfoBeforeRead;
    source->info.AvailableBytes = result->AvailableBytesAfterRead;
    const auto *samples = buffer + sizeof(*result);
    const size_t bytes_per_sample =
        result->InfoBeforeRead.BlockAlign /
        result->InfoBeforeRead.Channels;
    const size_t sample_count =
        result->BytesReturned / bytes_per_sample;
    try {
        source->fifo.reserve(source->fifo.size() + sample_count);
        if (bytes_per_sample == 2u) {
            for (size_t index = 0u; index < sample_count; ++index) {
                const size_t offset = index * 2u;
                const std::uint16_t bits =
                    static_cast<std::uint16_t>(samples[offset]) |
                    (static_cast<std::uint16_t>(samples[offset + 1u]) << 8u);
                source->fifo.push_back(
                    static_cast<float>(static_cast<std::int16_t>(bits) /
                                       32768.0));
            }
        } else if (bytes_per_sample == 3u) {
            for (size_t index = 0u; index < sample_count; ++index) {
                const size_t offset = index * 3u;
                std::int32_t value =
                    static_cast<std::int32_t>(samples[offset]) |
                    (static_cast<std::int32_t>(samples[offset + 1u]) << 8u) |
                    (static_cast<std::int32_t>(samples[offset + 2u]) << 16u);
                if ((value & 0x00800000) != 0) {
                    value |= static_cast<std::int32_t>(0xFF000000u);
                }
                source->fifo.push_back(
                    static_cast<float>(value / 8388608.0));
            }
        } else if (bytes_per_sample == 4u &&
                   result->InfoBeforeRead.BitsPerSample == 24u) {
            for (size_t index = 0u; index < sample_count; ++index) {
                const size_t offset = index * 4u;
                std::int32_t value = 0;
                std::memcpy(&value, samples + offset, sizeof(value));
                // Windows stores 24 valid PCM bits left-aligned in a
                // 32-bit container for the shared-mode format we expose.
                value >>= 8;
                source->fifo.push_back(
                    static_cast<float>(value / 8388608.0));
            }
        } else {
            source->last_error = ERROR_INVALID_DATA;
            return NATIVE_PCM_SOURCE_IO_ERROR;
        }
    } catch (const std::bad_alloc &) {
        source->last_error = ERROR_OUTOFMEMORY;
        return NATIVE_PCM_SOURCE_NO_MEMORY;
    }
    return NATIVE_PCM_SOURCE_OK;
}

extern "C" native_pcm_source_status native_pcm_source_read_f32_stereo(
    native_pcm_source *source,
    float *interleaved_stereo,
    size_t requested_frames,
    unsigned timeout_ms,
    size_t *frames_read) {
    if (source == nullptr || interleaved_stereo == nullptr ||
        requested_frames == 0u || frames_read == nullptr) {
        return NATIVE_PCM_SOURCE_INVALID_ARGUMENT;
    }
    *frames_read = 0u;
    if (requested_frames > SIZE_MAX / 2u) {
        return NATIVE_PCM_SOURCE_INVALID_ARGUMENT;
    }

    const size_t requested_samples =
        requested_frames * 2u;
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    for (;;) {
        if (source->fifo.size() >= requested_samples) {
            refresh_endpoint_volume(source, false);
            std::copy_n(source->fifo.begin(),
                        requested_samples,
                        interleaved_stereo);
            if (source->apply_endpoint_volume) {
                native_pcm_volume_apply(interleaved_stereo,
                                        requested_samples,
                                        source->volume);
            }
            source->fifo.erase(source->fifo.begin(),
                               source->fifo.begin() +
                                   static_cast<std::ptrdiff_t>(
                                       requested_samples));
            *frames_read = requested_frames;
            return NATIVE_PCM_SOURCE_OK;
        }

        native_pcm_source_status status = drain_driver(source);
        if (status != NATIVE_PCM_SOURCE_OK) return status;
        if (source->fifo.size() >= requested_samples) continue;
        if (GetTickCount64() >= deadline) {
            source->last_error = WAIT_TIMEOUT;
            return NATIVE_PCM_SOURCE_TIMEOUT;
        }
        Sleep(1u);
    }
}

extern "C" void native_pcm_source_set_apply_endpoint_volume(
    native_pcm_source *source,
    int apply) {
    if (source == nullptr) return;
    source->apply_endpoint_volume = apply != 0;
}

extern "C" unsigned native_pcm_source_sample_rate_hz(
    const native_pcm_source *source) {
    return source != nullptr ? source->info.SampleRate : 0u;
}

extern "C" unsigned native_pcm_source_channels(
    const native_pcm_source *source) {
    return source != nullptr ? source->info.Channels : 0u;
}

extern "C" unsigned native_pcm_source_bits_per_sample(
    const native_pcm_source *source) {
    return source != nullptr ? source->info.BitsPerSample : 0u;
}

extern "C" const wchar_t *native_pcm_source_interface_path(
    const native_pcm_source *source) {
    return source != nullptr ? source->interface_path : L"";
}

extern "C" unsigned long native_pcm_source_last_error(
    const native_pcm_source *source) {
    return source != nullptr ? source->last_error : ERROR_INVALID_PARAMETER;
}

extern "C" native_pcm_source_status native_pcm_source_get_snapshot(
    native_pcm_source *source,
    native_pcm_source_snapshot *snapshot) {
    if (source == nullptr || snapshot == nullptr) {
        return NATIVE_PCM_SOURCE_INVALID_ARGUMENT;
    }
    NATIVE_LDAC_PCM_INFO queried{};
    DWORD bytes_returned = 0u;
    if (!query_property(source,
                        NativeLdacPcmPropertyInfo,
                        &queried,
                        static_cast<DWORD>(sizeof(queried)),
                        &bytes_returned)) {
        return NATIVE_PCM_SOURCE_IO_ERROR;
    }
    if (bytes_returned < sizeof(queried) || !supported_format(queried)) {
        source->last_error = ERROR_INVALID_DATA;
        return NATIVE_PCM_SOURCE_IO_ERROR;
    }
    source->info = queried;
    refresh_endpoint_volume(source, false);
    snapshot->sample_rate_hz = source->info.SampleRate;
    snapshot->channels = source->info.Channels;
    snapshot->bits_per_sample = source->info.BitsPerSample;
    snapshot->available_bytes = source->info.AvailableBytes;
    snapshot->capacity_bytes = source->info.CapacityBytes;
    snapshot->stream_active =
        (source->info.Flags & NATIVE_LDAC_PCM_FLAG_STREAM_ACTIVE) != 0u;
    snapshot->discontinuity =
        (source->info.Flags & NATIVE_LDAC_PCM_FLAG_DISCONTINUITY) != 0u;
    snapshot->stream_epoch = source->info.StreamEpoch;
    snapshot->total_bytes_written = source->info.TotalBytesWritten;
    snapshot->total_bytes_read = source->info.TotalBytesRead;
    snapshot->total_bytes_dropped = source->info.TotalBytesDropped;
    snapshot->volume_control_available =
        source->volume.available ? 1 : 0;
    snapshot->muted = source->volume.muted ? 1 : 0;
    snapshot->volume_scalar = source->volume.scalar;
    snapshot->volume_db = source->volume.decibels;
    return NATIVE_PCM_SOURCE_OK;
}

extern "C" native_pcm_source_status native_pcm_source_report_link_state(
    native_pcm_source *source,
    native_pcm_link_state state,
    uint64_t session_id) {
    if (source == nullptr ||
        state < NATIVE_PCM_LINK_DISCONNECTED ||
        state > NATIVE_PCM_LINK_STOPPING ||
        (state != NATIVE_PCM_LINK_DISCONNECTED && session_id == 0u)) {
        return NATIVE_PCM_SOURCE_INVALID_ARGUMENT;
    }

    NATIVE_LDAC_LINK_STATE report{};
    report.Size = sizeof(report);
    report.AbiVersion = NATIVE_LDAC_LINK_STATE_ABI_VERSION;
    report.State = static_cast<ULONG>(state);
    report.Flags = NATIVE_LDAC_LINK_STATE_FLAG_NONE;
    report.SessionId = session_id;
    if (!set_property(source,
                      NativeLdacPcmPropertyLinkState,
                      &report,
                      static_cast<DWORD>(sizeof(report)))) {
        return property_error_status(source);
    }
    return NATIVE_PCM_SOURCE_OK;
}

extern "C" native_pcm_source_status native_pcm_source_get_link_state(
    native_pcm_source *source,
    native_pcm_link_snapshot *snapshot) {
    if (source == nullptr || snapshot == nullptr) {
        return NATIVE_PCM_SOURCE_INVALID_ARGUMENT;
    }

    NATIVE_LDAC_LINK_STATE state{};
    DWORD bytes_returned = 0u;
    if (!query_property(source,
                        NativeLdacPcmPropertyLinkState,
                        &state,
                        static_cast<DWORD>(sizeof(state)),
                        &bytes_returned)) {
        return property_error_status(source);
    }
    if (bytes_returned < sizeof(state) ||
        state.Size != sizeof(state) ||
        state.AbiVersion != NATIVE_LDAC_LINK_STATE_ABI_VERSION ||
        state.Flags != NATIVE_LDAC_LINK_STATE_FLAG_NONE ||
        state.State > NativeLdacLinkStateStopping) {
        source->last_error = ERROR_INVALID_DATA;
        return NATIVE_PCM_SOURCE_IO_ERROR;
    }

    snapshot->state = static_cast<native_pcm_link_state>(state.State);
    snapshot->session_id = state.SessionId;
    snapshot->update_sequence = state.UpdateSequence;
    snapshot->updated_interrupt_time_100ns =
        state.UpdatedInterruptTime100ns;
    return NATIVE_PCM_SOURCE_OK;
}

extern "C" native_pcm_source_status native_pcm_source_get_preferred_format(
    native_pcm_source *source,
    native_pcm_preferred_format *format) {
    if (source == nullptr || format == nullptr) {
        return NATIVE_PCM_SOURCE_INVALID_ARGUMENT;
    }

    NATIVE_LDAC_PREFERRED_FORMAT queried{};
    DWORD bytes_returned = 0u;
    if (!query_property(source,
                        NativeLdacPcmPropertyPreferredFormat,
                        &queried,
                        static_cast<DWORD>(sizeof(queried)),
                        &bytes_returned)) {
        return property_error_status(source);
    }
    if (bytes_returned < sizeof(queried) ||
        !supported_preferred_format(queried)) {
        source->last_error = ERROR_INVALID_DATA;
        return NATIVE_PCM_SOURCE_IO_ERROR;
    }
    source->preferred_format = queried;
    format->sample_rate_hz = queried.SampleRate;
    format->bits_per_sample = queried.BitsPerSample;
    format->supported_sample_rates = queried.SupportedSampleRates;
    format->supported_bits_per_sample = queried.SupportedBitsPerSample;
    format->revision = queried.Revision;
    return NATIVE_PCM_SOURCE_OK;
}

extern "C" native_pcm_source_status native_pcm_source_set_preferred_format(
    native_pcm_source *source,
    unsigned sample_rate_hz,
    unsigned bits_per_sample,
    native_pcm_preferred_format *applied_format) {
    if (source == nullptr ||
        (sample_rate_hz != 44100u && sample_rate_hz != 48000u &&
         sample_rate_hz != 88200u && sample_rate_hz != 96000u) ||
        (bits_per_sample != 16u && bits_per_sample != 24u)) {
        return NATIVE_PCM_SOURCE_INVALID_ARGUMENT;
    }

    NATIVE_LDAC_PREFERRED_FORMAT requested{};
    requested.Size = sizeof(requested);
    requested.AbiVersion = NATIVE_LDAC_FORMAT_ABI_VERSION;
    requested.SampleRate = sample_rate_hz;
    requested.BitsPerSample = bits_per_sample;
    requested.Flags = NATIVE_LDAC_FORMAT_FLAG_NONE;
    if (!set_property(source,
                      NativeLdacPcmPropertyPreferredFormat,
                      &requested,
                      static_cast<DWORD>(sizeof(requested)))) {
        return property_error_status(source);
    }

    native_pcm_preferred_format queried{};
    const native_pcm_source_status status =
        native_pcm_source_get_preferred_format(source, &queried);
    if (status != NATIVE_PCM_SOURCE_OK) return status;
    if (queried.sample_rate_hz != sample_rate_hz ||
        queried.bits_per_sample != bits_per_sample) {
        source->last_error = ERROR_INVALID_DATA;
        return NATIVE_PCM_SOURCE_IO_ERROR;
    }
    if (applied_format != nullptr) *applied_format = queried;
    source->fifo.clear();
    return NATIVE_PCM_SOURCE_OK;
}
