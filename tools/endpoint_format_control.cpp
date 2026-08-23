// SPDX-License-Identifier: Apache-2.0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <audioclient.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>

#include <array>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <string>

#include "nativeldac_remote_container.h"

static const PROPERTYKEY kAudioEngineDeviceFormat = {
    {0xf19f064d, 0x082c, 0x4e27,
     {0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c}},
    0,
};

namespace {

struct DeviceShareMode {
    AUDCLNT_SHAREMODE mode;
};

struct __declspec(uuid("F8679F50-850A-41CF-9C72-430F290290C8"))
    IPolicyConfig : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(
        PCWSTR device_id, WAVEFORMATEX** format) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(
        PCWSTR device_id, INT default_format, WAVEFORMATEX** format) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR device_id) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(
        PCWSTR device_id,
        WAVEFORMATEX* endpoint_format,
        WAVEFORMATEX* mix_format) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(
        PCWSTR device_id,
        INT default_period,
        PINT64 period,
        PINT64 minimum_period) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(
        PCWSTR device_id, PINT64 period) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(
        PCWSTR device_id, DeviceShareMode* mode) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(
        PCWSTR device_id, DeviceShareMode* mode) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(
        PCWSTR device_id,
        BOOL store,
        const PROPERTYKEY& key,
        PROPVARIANT* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(
        PCWSTR device_id,
        BOOL store,
        const PROPERTYKEY& key,
        const PROPVARIANT* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnusedDefaultRoleSetter(
        PCWSTR device_id, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(
        PCWSTR device_id, BOOL visible) = 0;
};

const CLSID kPolicyConfigClient = {
    0x870af99c,
    0x171d,
    0x4f9e,
    {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9},
};

struct EndpointSelection {
    IMMDevice* device = nullptr;
    std::wstring id;
    std::wstring name;
};

struct FormatDescription {
    unsigned sample_rate_hz = 0u;
    unsigned channels = 0u;
    unsigned container_bits = 0u;
    unsigned valid_bits = 0u;
    unsigned block_align = 0u;
};

void ReleaseEndpoint(EndpointSelection* endpoint) {
    if (endpoint != nullptr && endpoint->device != nullptr) {
        endpoint->device->Release();
        endpoint->device = nullptr;
    }
}

bool IsNativeLdacEndpoint(IPropertyStore* properties) {
    if (properties == nullptr) return false;
    PROPVARIANT name;
    PROPVARIANT container;
    PropVariantInit(&name);
    PropVariantInit(&container);
    const HRESULT name_hr = properties->GetValue(PKEY_Device_FriendlyName,
                                                  &name);
    const HRESULT container_hr = properties->GetValue(PKEY_Device_ContainerId,
                                                       &container);
    const bool matches = SUCCEEDED(name_hr) &&
        name.vt == VT_LPWSTR && name.pwszVal != nullptr &&
        std::wcsstr(name.pwszVal, L"Native LDAC") != nullptr &&
        SUCCEEDED(container_hr) && container.vt == VT_CLSID &&
        container.puuid != nullptr &&
        IsEqualGUID(*container.puuid, NativeLdacRemoteContainerId) != FALSE;
    PropVariantClear(&container);
    PropVariantClear(&name);
    return matches;
}

HRESULT SelectNativeLdacEndpoint(EndpointSelection* selected) {
    if (selected == nullptr) return E_INVALIDARG;
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* collection = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                  nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || enumerator == nullptr) return hr;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATEMASK_ALL,
                                        &collection);
    if (FAILED(hr) || collection == nullptr) {
        enumerator->Release();
        return hr;
    }
    UINT count = 0u;
    hr = collection->GetCount(&count);
    if (SUCCEEDED(hr)) {
        for (UINT index = 0u; index < count; ++index) {
            IMMDevice* candidate = nullptr;
            IPropertyStore* properties = nullptr;
            if (FAILED(collection->Item(index, &candidate)) ||
                candidate == nullptr) {
                continue;
            }
            const HRESULT store_hr = candidate->OpenPropertyStore(
                STGM_READ, &properties);
            const bool matches = SUCCEEDED(store_hr) &&
                IsNativeLdacEndpoint(properties);
            if (matches) {
                if (selected->device != nullptr) {
                    if (properties != nullptr) properties->Release();
                    candidate->Release();
                    hr = HRESULT_FROM_WIN32(ERROR_DUP_NAME);
                    break;
                }
                LPWSTR id = nullptr;
                if (FAILED(candidate->GetId(&id)) || id == nullptr) {
                    hr = E_FAIL;
                } else {
                    selected->device = candidate;
                    candidate = nullptr;
                    selected->id = id;
                    CoTaskMemFree(id);
                    PROPVARIANT name;
                    PropVariantInit(&name);
                    if (properties != nullptr && SUCCEEDED(properties->GetValue(
                            PKEY_Device_FriendlyName, &name)) &&
                        name.vt == VT_LPWSTR && name.pwszVal != nullptr) {
                        selected->name = name.pwszVal;
                    }
                    PropVariantClear(&name);
                }
            }
            if (properties != nullptr) properties->Release();
            if (candidate != nullptr) candidate->Release();
            if (FAILED(hr)) break;
        }
    }
    if (SUCCEEDED(hr) && selected->device == nullptr) {
        hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    collection->Release();
    enumerator->Release();
    return hr;
}

bool DescribeFormat(const WAVEFORMATEX* format,
                    FormatDescription* description) {
    if (format == nullptr || description == nullptr ||
        format->nSamplesPerSec == 0u || format->nChannels == 0u ||
        format->wBitsPerSample == 0u || format->nBlockAlign == 0u) {
        return false;
    }
    unsigned valid_bits = format->wBitsPerSample;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto* extended =
            reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        valid_bits = extended->Samples.wValidBitsPerSample;
    }
    description->sample_rate_hz = format->nSamplesPerSec;
    description->channels = format->nChannels;
    description->container_bits = format->wBitsPerSample;
    description->valid_bits = valid_bits;
    description->block_align = format->nBlockAlign;
    return true;
}

HRESULT CreatePolicyConfig(IPolicyConfig** policy) {
    if (policy == nullptr) return E_POINTER;
    *policy = nullptr;
    return CoCreateInstance(kPolicyConfigClient,
                            nullptr,
                            CLSCTX_INPROC_SERVER,
                            __uuidof(IPolicyConfig),
                            reinterpret_cast<void**>(policy));
}

HRESULT QueryDeviceFormat(IPolicyConfig* policy,
                          IMMDevice* device,
                          const std::wstring& device_id,
                          WAVEFORMATEX** format) {
    if (policy == nullptr || device == nullptr || format == nullptr) {
        return E_INVALIDARG;
    }
    *format = nullptr;
    IPropertyStore* properties = nullptr;
    PROPVARIANT value;
    PropVariantInit(&value);
    HRESULT hr = device->OpenPropertyStore(STGM_READ, &properties);
    if (SUCCEEDED(hr) && properties != nullptr) {
        hr = properties->GetValue(kAudioEngineDeviceFormat, &value);
    }
    if (SUCCEEDED(hr) && value.vt == VT_BLOB &&
        value.blob.pBlobData != nullptr &&
        value.blob.cbSize >= sizeof(WAVEFORMATEX)) {
        auto* copy = static_cast<WAVEFORMATEX*>(
            CoTaskMemAlloc(value.blob.cbSize));
        if (copy == nullptr) {
            hr = E_OUTOFMEMORY;
        } else {
            std::memcpy(copy, value.blob.pBlobData, value.blob.cbSize);
            *format = copy;
        }
    } else if (SUCCEEDED(hr)) {
        hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    PropVariantClear(&value);
    if (properties != nullptr) properties->Release();
    if (SUCCEEDED(hr) && *format != nullptr) return hr;
    return policy->GetDeviceFormat(device_id.c_str(), FALSE, format);
}

WAVEFORMATEXTENSIBLE BuildFormat(unsigned sample_rate_hz,
                                 unsigned bits_per_sample) {
    const unsigned container_bits = bits_per_sample == 24u
        ? 32u
        : bits_per_sample;
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = 2u;
    format.Format.nSamplesPerSec = sample_rate_hz;
    format.Format.wBitsPerSample = static_cast<WORD>(container_bits);
    format.Format.nBlockAlign = static_cast<WORD>(
        format.Format.nChannels * container_bits / 8u);
    format.Format.nAvgBytesPerSec =
        format.Format.nSamplesPerSec * format.Format.nBlockAlign;
    format.Format.cbSize =
        sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample =
        static_cast<WORD>(bits_per_sample);
    format.dwChannelMask = KSAUDIO_SPEAKER_STEREO;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    return format;
}

void PrintFormat(const EndpointSelection& endpoint,
                 const FormatDescription& format) {
    std::wprintf(L"Endpoint: %ls\n", endpoint.name.c_str());
    std::wprintf(L"Endpoint ID: %ls\n", endpoint.id.c_str());
    std::wprintf(
        L"Windows shared-mode device format: %u Hz, %u channel(s), "
        L"%u-bit container, %u valid bit(s), block %u bytes.\n",
        format.sample_rate_hz,
        format.channels,
        format.container_bits,
        format.valid_bits,
        format.block_align);
}

bool ParseUnsigned(const wchar_t* value, unsigned* parsed) {
    if (value == nullptr || parsed == nullptr || value[0] == L'\0') {
        return false;
    }
    wchar_t* end = nullptr;
    const unsigned long result = std::wcstoul(value, &end, 10);
    if (end == value || end == nullptr || *end != L'\0') return false;
    *parsed = static_cast<unsigned>(result);
    return true;
}

void PrintUsage() {
    std::wprintf(
        L"Usage: endpoint_format_control.exe "
        L"[--format | --set-format rate bits]\n"
        L"  --format      Show the exact Native LDAC Windows shared-mode "
        L"device format.\n"
        L"  --set-format  Set 44100/48000/88200/96000 Hz and 16/24-bit "
        L"stereo PCM.\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    bool set_format = false;
    unsigned sample_rate_hz = 0u;
    unsigned bits_per_sample = 0u;
    if (argc == 2 && (std::wcscmp(argv[1], L"--help") == 0 ||
                      std::wcscmp(argv[1], L"-h") == 0)) {
        PrintUsage();
        return 0;
    }
    if (argc == 2 && std::wcscmp(argv[1], L"--format") == 0) {
        // Query only.
    } else if (argc == 4 &&
               std::wcscmp(argv[1], L"--set-format") == 0 &&
               ParseUnsigned(argv[2], &sample_rate_hz) &&
               ParseUnsigned(argv[3], &bits_per_sample) &&
               (sample_rate_hz == 44100u || sample_rate_hz == 48000u ||
                sample_rate_hz == 88200u || sample_rate_hz == 96000u) &&
               (bits_per_sample == 16u || bits_per_sample == 24u)) {
        set_format = true;
    } else {
        PrintUsage();
        return 2;
    }

    const HRESULT initialize_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(initialize_hr);
    if (FAILED(initialize_hr) && initialize_hr != RPC_E_CHANGED_MODE) {
        std::fwprintf(stderr, L"CoInitializeEx failed: 0x%08lX\n",
                      static_cast<unsigned long>(initialize_hr));
        return 3;
    }

    EndpointSelection endpoint;
    HRESULT hr = SelectNativeLdacEndpoint(&endpoint);
    IPolicyConfig* policy = nullptr;
    if (SUCCEEDED(hr)) hr = CreatePolicyConfig(&policy);
    if (FAILED(hr) || policy == nullptr) {
        std::fwprintf(stderr,
                      L"Native LDAC shared-mode format control is unavailable: "
                      L"0x%08lX\n",
                      static_cast<unsigned long>(hr));
        ReleaseEndpoint(&endpoint);
        if (uninitialize) CoUninitialize();
        return 4;
    }

    if (set_format) {
        WAVEFORMATEXTENSIBLE requested =
            BuildFormat(sample_rate_hz, bits_per_sample);
        hr = policy->SetDeviceFormat(
            endpoint.id.c_str(),
            reinterpret_cast<WAVEFORMATEX*>(&requested),
            reinterpret_cast<WAVEFORMATEX*>(&requested));
        if (FAILED(hr)) {
            std::fwprintf(stderr,
                          L"SetDeviceFormat failed: 0x%08lX\n",
                          static_cast<unsigned long>(hr));
        } else {
            Sleep(250u);
        }
    }

    WAVEFORMATEX* current = nullptr;
    if (SUCCEEDED(hr)) {
        hr = QueryDeviceFormat(policy, endpoint.device, endpoint.id, &current);
    }
    FormatDescription description;
    if (FAILED(hr) || !DescribeFormat(current, &description)) {
        std::fwprintf(stderr,
                      L"GetDeviceFormat failed: 0x%08lX\n",
                      static_cast<unsigned long>(hr));
        if (current != nullptr) CoTaskMemFree(current);
        policy->Release();
        ReleaseEndpoint(&endpoint);
        if (uninitialize) CoUninitialize();
        return 5;
    }
    PrintFormat(endpoint, description);
    const unsigned expected_container_bits = bits_per_sample == 24u
        ? 32u
        : bits_per_sample;
    const bool matched = !set_format ||
        (description.sample_rate_hz == sample_rate_hz &&
         description.channels == 2u &&
         description.container_bits == expected_container_bits &&
         description.valid_bits == bits_per_sample);

    CoTaskMemFree(current);
    policy->Release();
    ReleaseEndpoint(&endpoint);
    if (uninitialize) CoUninitialize();
    if (!matched) {
        std::fwprintf(stderr,
                      L"Windows retained a different shared-mode format.\n");
        return 6;
    }
    return 0;
}
