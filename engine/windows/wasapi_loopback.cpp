// SPDX-License-Identifier: Apache-2.0
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <audioclient.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "ldac_native/wasapi_loopback.h"

enum class sample_kind {
    unsigned_pcm_8,
    signed_pcm_16,
    signed_pcm_24,
    signed_pcm_32,
    float_32
};

struct wasapi_loopback {
    IMMDeviceEnumerator *enumerator = nullptr;
    IMMDevice *device = nullptr;
    IAudioClient *audio_client = nullptr;
    IAudioCaptureClient *capture_client = nullptr;
    WAVEFORMATEX *format = nullptr;
    HANDLE event_handle = nullptr;
    std::vector<float> fifo;
    size_t fifo_read_offset = 0u;
    sample_kind kind = sample_kind::float_32;
    unsigned bytes_per_sample = 0u;
    long last_hresult = S_OK;
    bool com_initialized = false;
    bool started = false;
    wchar_t device_name[WASAPI_LOOPBACK_DEVICE_NAME_CAPACITY] = L"";
};

static void release_source(wasapi_loopback *source) {
    if (source == nullptr) return;
    if (source->started && source->audio_client != nullptr) {
        (void)source->audio_client->Stop();
        source->started = false;
    }
    if (source->capture_client != nullptr) {
        source->capture_client->Release();
        source->capture_client = nullptr;
    }
    if (source->audio_client != nullptr) {
        source->audio_client->Release();
        source->audio_client = nullptr;
    }
    if (source->format != nullptr) {
        CoTaskMemFree(source->format);
        source->format = nullptr;
    }
    if (source->device != nullptr) {
        source->device->Release();
        source->device = nullptr;
    }
    if (source->enumerator != nullptr) {
        source->enumerator->Release();
        source->enumerator = nullptr;
    }
    if (source->event_handle != nullptr) {
        CloseHandle(source->event_handle);
        source->event_handle = nullptr;
    }
    if (source->com_initialized) {
        CoUninitialize();
        source->com_initialized = false;
    }
}

static void read_device_name(wasapi_loopback *source) {
    IPropertyStore *properties = nullptr;
    PROPVARIANT value;
    PropVariantInit(&value);
    HRESULT hr = source->device->OpenPropertyStore(STGM_READ, &properties);
    if (SUCCEEDED(hr)) {
        hr = properties->GetValue(PKEY_Device_FriendlyName, &value);
        if (SUCCEEDED(hr) && value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
            (void)wcsncpy_s(source->device_name,
                            WASAPI_LOOPBACK_DEVICE_NAME_CAPACITY,
                            value.pwszVal,
                            _TRUNCATE);
        }
        properties->Release();
    }
    PropVariantClear(&value);
    if (source->device_name[0] == L'\0') {
        (void)wcsncpy_s(source->device_name,
                        WASAPI_LOOPBACK_DEVICE_NAME_CAPACITY,
                        L"Default render endpoint",
                        _TRUNCATE);
    }
}

static bool select_sample_kind(wasapi_loopback *source) {
    WORD format_tag = source->format->wFormatTag;
    GUID sub_format = GUID_NULL;
    unsigned valid_bits = source->format->wBitsPerSample;

    if (format_tag == WAVE_FORMAT_EXTENSIBLE) {
        if (source->format->cbSize <
            sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
            return false;
        }
        const auto *extended =
            reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(source->format);
        sub_format = extended->SubFormat;
        valid_bits = extended->Samples.wValidBitsPerSample;
        if (IsEqualGUID(sub_format, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            format_tag = WAVE_FORMAT_IEEE_FLOAT;
        } else if (IsEqualGUID(sub_format, KSDATAFORMAT_SUBTYPE_PCM)) {
            format_tag = WAVE_FORMAT_PCM;
        } else {
            return false;
        }
    }
    if (source->format->nChannels == 0u ||
        source->format->nBlockAlign % source->format->nChannels != 0u) {
        return false;
    }
    source->bytes_per_sample =
        source->format->nBlockAlign / source->format->nChannels;
    if (format_tag == WAVE_FORMAT_IEEE_FLOAT &&
        source->format->wBitsPerSample == 32u &&
        source->bytes_per_sample == 4u) {
        source->kind = sample_kind::float_32;
        return true;
    }
    if (format_tag != WAVE_FORMAT_PCM) return false;
    if (source->format->wBitsPerSample == 8u &&
        source->bytes_per_sample == 1u) {
        source->kind = sample_kind::unsigned_pcm_8;
        return true;
    }
    if (source->format->wBitsPerSample == 16u &&
        source->bytes_per_sample == 2u) {
        source->kind = sample_kind::signed_pcm_16;
        return true;
    }
    if (source->format->wBitsPerSample == 24u &&
        source->bytes_per_sample == 3u) {
        source->kind = sample_kind::signed_pcm_24;
        return true;
    }
    if ((source->format->wBitsPerSample == 32u || valid_bits == 24u) &&
        source->bytes_per_sample == 4u) {
        source->kind = sample_kind::signed_pcm_32;
        return true;
    }
    return false;
}

static float decode_sample(const wasapi_loopback *source,
                           const BYTE *sample) {
    switch (source->kind) {
        case sample_kind::unsigned_pcm_8:
            return (static_cast<int>(sample[0]) - 128) / 128.0f;
        case sample_kind::signed_pcm_16: {
            std::int16_t value;
            std::memcpy(&value, sample, sizeof(value));
            return static_cast<float>(value / 32768.0);
        }
        case sample_kind::signed_pcm_24: {
            std::int32_t value = static_cast<std::int32_t>(sample[0]) |
                (static_cast<std::int32_t>(sample[1]) << 8) |
                (static_cast<std::int32_t>(sample[2]) << 16);
            if ((value & 0x00800000) != 0) value |= static_cast<std::int32_t>(0xFF000000u);
            return static_cast<float>(value / 8388608.0);
        }
        case sample_kind::signed_pcm_32: {
            std::int32_t value;
            std::memcpy(&value, sample, sizeof(value));
            return static_cast<float>(value / 2147483648.0);
        }
        case sample_kind::float_32: {
            float value;
            std::memcpy(&value, sample, sizeof(value));
            return value;
        }
    }
    return 0.0f;
}

static wasapi_loopback_status drain_packets(wasapi_loopback *source) {
    for (;;) {
        UINT32 packet_frames = 0u;
        HRESULT hr = source->capture_client->GetNextPacketSize(&packet_frames);
        if (FAILED(hr)) {
            source->last_hresult = hr;
            return WASAPI_LOOPBACK_COM_ERROR;
        }
        if (packet_frames == 0u) return WASAPI_LOOPBACK_OK;

        BYTE *data = nullptr;
        DWORD flags = 0u;
        UINT64 device_position = 0u;
        UINT64 qpc_position = 0u;
        hr = source->capture_client->GetBuffer(&data,
                                               &packet_frames,
                                               &flags,
                                               &device_position,
                                               &qpc_position);
        if (FAILED(hr)) {
            source->last_hresult = hr;
            return WASAPI_LOOPBACK_COM_ERROR;
        }
        try {
            source->fifo.reserve(source->fifo.size() +
                                 static_cast<size_t>(packet_frames) * 2u);
            for (UINT32 frame = 0u; frame < packet_frames; ++frame) {
                float left = 0.0f;
                float right = 0.0f;
                if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0u) {
                    const BYTE *frame_data = data +
                        static_cast<size_t>(frame) * source->format->nBlockAlign;
                    left = decode_sample(source, frame_data);
                    if (source->format->nChannels > 1u) {
                        right = decode_sample(
                            source,
                            frame_data + source->bytes_per_sample);
                    } else {
                        right = left;
                    }
                }
                source->fifo.push_back(left);
                source->fifo.push_back(right);
            }
        } catch (const std::bad_alloc &) {
            (void)source->capture_client->ReleaseBuffer(packet_frames);
            return WASAPI_LOOPBACK_NO_MEMORY;
        }
        hr = source->capture_client->ReleaseBuffer(packet_frames);
        if (FAILED(hr)) {
            source->last_hresult = hr;
            return WASAPI_LOOPBACK_COM_ERROR;
        }
    }
}

extern "C" wasapi_loopback_status wasapi_loopback_create(
    wasapi_loopback **out) {
    wasapi_loopback *source;
    HRESULT hr;

    if (out == nullptr) return WASAPI_LOOPBACK_INVALID_ARGUMENT;
    *out = nullptr;
    source = new (std::nothrow) wasapi_loopback();
    if (source == nullptr) return WASAPI_LOOPBACK_NO_MEMORY;

    hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        source->com_initialized = true;
    } else if (hr != RPC_E_CHANGED_MODE) {
        source->last_hresult = hr;
        release_source(source);
        delete source;
        return WASAPI_LOOPBACK_COM_ERROR;
    }
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                          nullptr,
                          CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void **>(&source->enumerator));
    if (FAILED(hr)) goto com_failure;
    hr = source->enumerator->GetDefaultAudioEndpoint(eRender,
                                                      eMultimedia,
                                                      &source->device);
    if (FAILED(hr)) goto com_failure;
    read_device_name(source);
    hr = source->device->Activate(__uuidof(IAudioClient),
                                  CLSCTX_ALL,
                                  nullptr,
                                  reinterpret_cast<void **>(
                                      &source->audio_client));
    if (FAILED(hr)) goto com_failure;
    hr = source->audio_client->GetMixFormat(&source->format);
    if (FAILED(hr)) goto com_failure;
    if (!select_sample_kind(source)) {
        release_source(source);
        delete source;
        return WASAPI_LOOPBACK_UNSUPPORTED_FORMAT;
    }
    hr = source->audio_client->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK |
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
            AUDCLNT_STREAMFLAGS_NOPERSIST,
        0,
        0,
        source->format,
        nullptr);
    if (FAILED(hr)) goto com_failure;
    source->event_handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (source->event_handle == nullptr) {
        source->last_hresult = HRESULT_FROM_WIN32(GetLastError());
        release_source(source);
        delete source;
        return WASAPI_LOOPBACK_COM_ERROR;
    }
    hr = source->audio_client->SetEventHandle(source->event_handle);
    if (FAILED(hr)) goto com_failure;
    hr = source->audio_client->GetService(
        __uuidof(IAudioCaptureClient),
        reinterpret_cast<void **>(&source->capture_client));
    if (FAILED(hr)) goto com_failure;

    *out = source;
    return WASAPI_LOOPBACK_OK;

com_failure:
    source->last_hresult = hr;
    release_source(source);
    delete source;
    return WASAPI_LOOPBACK_COM_ERROR;
}

extern "C" void wasapi_loopback_destroy(wasapi_loopback *source) {
    if (source == nullptr) return;
    release_source(source);
    delete source;
}

extern "C" wasapi_loopback_status wasapi_loopback_start(
    wasapi_loopback *source) {
    if (source == nullptr || source->audio_client == nullptr) {
        return WASAPI_LOOPBACK_INVALID_ARGUMENT;
    }
    if (source->started) return WASAPI_LOOPBACK_OK;
    source->fifo.clear();
    source->fifo_read_offset = 0u;
    ResetEvent(source->event_handle);
    HRESULT hr = source->audio_client->Start();
    if (FAILED(hr)) {
        source->last_hresult = hr;
        return WASAPI_LOOPBACK_COM_ERROR;
    }
    source->started = true;
    return WASAPI_LOOPBACK_OK;
}

extern "C" void wasapi_loopback_stop(wasapi_loopback *source) {
    if (source == nullptr || !source->started) return;
    (void)source->audio_client->Stop();
    source->started = false;
}

extern "C" wasapi_loopback_status wasapi_loopback_read_f32_stereo(
    wasapi_loopback *source,
    float *interleaved_stereo,
    size_t requested_frames,
    unsigned timeout_ms,
    size_t *frames_read) {
    ULONGLONG deadline;

    if (frames_read != nullptr) *frames_read = 0u;
    if (source == nullptr || !source->started ||
        interleaved_stereo == nullptr || requested_frames == 0u ||
        frames_read == nullptr) {
        return WASAPI_LOOPBACK_INVALID_ARGUMENT;
    }
    deadline = GetTickCount64() + timeout_ms;
    for (;;) {
        size_t available_frames;
        wasapi_loopback_status status = drain_packets(source);
        if (status != WASAPI_LOOPBACK_OK) return status;
        available_frames =
            (source->fifo.size() - source->fifo_read_offset) / 2u;
        if (available_frames >= requested_frames) break;

        ULONGLONG now = GetTickCount64();
        if (now >= deadline) return WASAPI_LOOPBACK_TIMEOUT;
        DWORD remaining = static_cast<DWORD>(
            std::min<ULONGLONG>(deadline - now, MAXDWORD));
        DWORD wait_result = WaitForSingleObject(source->event_handle, remaining);
        if (wait_result == WAIT_TIMEOUT) return WASAPI_LOOPBACK_TIMEOUT;
        if (wait_result != WAIT_OBJECT_0) {
            source->last_hresult = HRESULT_FROM_WIN32(GetLastError());
            return WASAPI_LOOPBACK_COM_ERROR;
        }
    }

    std::memcpy(interleaved_stereo,
                source->fifo.data() + source->fifo_read_offset,
                requested_frames * 2u * sizeof(float));
    source->fifo_read_offset += requested_frames * 2u;
    *frames_read = requested_frames;
    if (source->fifo_read_offset >= 8192u &&
        source->fifo_read_offset * 2u >= source->fifo.size()) {
        source->fifo.erase(source->fifo.begin(),
                           source->fifo.begin() +
                               static_cast<std::ptrdiff_t>(
                                   source->fifo_read_offset));
        source->fifo_read_offset = 0u;
    }
    return WASAPI_LOOPBACK_OK;
}

extern "C" unsigned wasapi_loopback_sample_rate_hz(
    const wasapi_loopback *source) {
    return source != nullptr && source->format != nullptr
        ? source->format->nSamplesPerSec
        : 0u;
}

extern "C" unsigned wasapi_loopback_source_channels(
    const wasapi_loopback *source) {
    return source != nullptr && source->format != nullptr
        ? source->format->nChannels
        : 0u;
}

extern "C" unsigned wasapi_loopback_bits_per_sample(
    const wasapi_loopback *source) {
    return source != nullptr && source->format != nullptr
        ? source->format->wBitsPerSample
        : 0u;
}

extern "C" const wchar_t *wasapi_loopback_device_name(
    const wasapi_loopback *source) {
    return source != nullptr ? source->device_name : L"";
}

extern "C" long wasapi_loopback_last_hresult(
    const wasapi_loopback *source) {
    return source != nullptr ? source->last_hresult : E_INVALIDARG;
}
