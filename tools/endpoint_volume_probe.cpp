// SPDX-License-Identifier: Apache-2.0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <endpointvolume.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <fcntl.h>
#include <io.h>
#include <string>
#include <vector>

namespace {

struct EndpointObservation {
    IMMDevice* device = nullptr;
    IAudioEndpointVolume* volume = nullptr;
    std::wstring id;
    std::wstring name;
    std::wstring container_id;
    DWORD state = 0;
    float scalar = 0.0f;
    float level_db = 0.0f;
    float range_min_db = 0.0f;
    float range_max_db = 0.0f;
    float range_increment_db = 0.0f;
    UINT step_index = 0;
    UINT step_count = 0;
    BOOL muted = FALSE;
    bool volume_available = false;
    bool range_available = false;
    bool step_available = false;
    bool default_console = false;
    bool default_multimedia = false;
};

void ReleaseEndpoints(std::vector<EndpointObservation>* endpoints) {
    if (endpoints == nullptr) return;
    for (EndpointObservation& endpoint : *endpoints) {
        if (endpoint.volume != nullptr) {
            endpoint.volume->Release();
            endpoint.volume = nullptr;
        }
        if (endpoint.device != nullptr) {
            endpoint.device->Release();
            endpoint.device = nullptr;
        }
    }
    endpoints->clear();
}

const wchar_t* StateName(DWORD state) {
    if ((state & DEVICE_STATE_ACTIVE) != 0u) return L"active";
    if ((state & DEVICE_STATE_DISABLED) != 0u) return L"disabled";
    if ((state & DEVICE_STATE_NOTPRESENT) != 0u) return L"not-present";
    if ((state & DEVICE_STATE_UNPLUGGED) != 0u) return L"unplugged";
    return L"unknown";
}

bool IsRelevantName(const std::wstring& name) {
    return name.find(L"WH-1000XM5") != std::wstring::npos ||
           name.find(L"Native LDAC") != std::wstring::npos;
}

bool IsNativeEndpointName(const std::wstring& name) {
    return name.find(L"Native LDAC") != std::wstring::npos;
}

bool IsDirectPdoEndpointName(const std::wstring& name) {
    return name.find(L"Native LDAC - WH-1000XM5") != std::wstring::npos;
}

bool IsXm5ReferenceEndpointName(const std::wstring& name) {
    return !IsNativeEndpointName(name) &&
           name.find(L"WH-1000XM5") != std::wstring::npos;
}

std::wstring ReadStringProperty(IPropertyStore* properties,
                                REFPROPERTYKEY key) {
    if (properties == nullptr) return std::wstring();
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring result;
    if (SUCCEEDED(properties->GetValue(key, &value)) &&
        value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
        result = value.pwszVal;
    }
    PropVariantClear(&value);
    return result;
}

std::wstring ReadContainerId(IPropertyStore* properties) {
    if (properties == nullptr) return std::wstring();
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring result;
    if (SUCCEEDED(properties->GetValue(PKEY_Device_ContainerId, &value)) &&
        value.vt == VT_CLSID && value.puuid != nullptr) {
        wchar_t buffer[64]{};
        if (StringFromGUID2(*value.puuid,
                            buffer,
                            static_cast<int>(std::size(buffer))) > 0) {
            result = buffer;
        }
    }
    PropVariantClear(&value);
    return result;
}

bool RefreshVolume(EndpointObservation* endpoint) {
    if (endpoint == nullptr || endpoint->volume == nullptr) return false;
    float scalar = 0.0f;
    float level_db = 0.0f;
    BOOL muted = FALSE;
    if (FAILED(endpoint->volume->GetMasterVolumeLevelScalar(&scalar)) ||
        FAILED(endpoint->volume->GetMasterVolumeLevel(&level_db)) ||
        FAILED(endpoint->volume->GetMute(&muted))) {
        return false;
    }
    endpoint->scalar = std::clamp(scalar, 0.0f, 1.0f);
    endpoint->level_db = level_db;
    endpoint->muted = muted;
    endpoint->volume_available = true;
    endpoint->range_available = SUCCEEDED(endpoint->volume->GetVolumeRange(
        &endpoint->range_min_db,
        &endpoint->range_max_db,
        &endpoint->range_increment_db));
    endpoint->step_available = SUCCEEDED(endpoint->volume->GetVolumeStepInfo(
        &endpoint->step_index,
        &endpoint->step_count));
    return true;
}

std::wstring ReadDeviceId(IMMDevice* device) {
    if (device == nullptr) return std::wstring();
    LPWSTR value = nullptr;
    std::wstring result;
    if (SUCCEEDED(device->GetId(&value)) && value != nullptr) {
        result = value;
        CoTaskMemFree(value);
    }
    return result;
}

std::wstring ReadDefaultEndpointId(IMMDeviceEnumerator* enumerator,
                                   ERole role) {
    if (enumerator == nullptr) return std::wstring();
    IMMDevice* device = nullptr;
    std::wstring result;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(
            eRender, role, &device)) && device != nullptr) {
        result = ReadDeviceId(device);
        device->Release();
    }
    return result;
}

HRESULT EnumerateEndpoints(bool include_all,
                           std::vector<EndpointObservation>* endpoints) {
    if (endpoints == nullptr) return E_INVALIDARG;
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* collection = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                  nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || enumerator == nullptr) return hr;

    hr = enumerator->EnumAudioEndpoints(eRender,
                                        DEVICE_STATEMASK_ALL,
                                        &collection);
    if (FAILED(hr) || collection == nullptr) {
        enumerator->Release();
        return hr;
    }

    UINT count = 0;
    hr = collection->GetCount(&count);
    if (FAILED(hr)) {
        collection->Release();
        enumerator->Release();
        return hr;
    }

    for (UINT index = 0; index < count; ++index) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(index, &device)) || device == nullptr) {
            continue;
        }
        IPropertyStore* properties = nullptr;
        std::wstring name;
        std::wstring container_id;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)) &&
            properties != nullptr) {
            name = ReadStringProperty(properties, PKEY_Device_FriendlyName);
            container_id = ReadContainerId(properties);
            properties->Release();
        }
        if (!include_all && !IsRelevantName(name)) {
            device->Release();
            continue;
        }

        EndpointObservation endpoint;
        endpoint.device = device;
        endpoint.name = name.empty() ? L"(unnamed render endpoint)" : name;
        endpoint.container_id = container_id;
        (void)device->GetState(&endpoint.state);
        endpoint.id = ReadDeviceId(device);
        const HRESULT activate_hr = device->Activate(
            __uuidof(IAudioEndpointVolume),
            CLSCTX_INPROC_SERVER,
            nullptr,
            reinterpret_cast<void**>(&endpoint.volume));
        if (SUCCEEDED(activate_hr) && endpoint.volume != nullptr) {
            (void)RefreshVolume(&endpoint);
        }
        endpoints->push_back(endpoint);
    }

    const std::wstring default_console =
        ReadDefaultEndpointId(enumerator, eConsole);
    const std::wstring default_multimedia =
        ReadDefaultEndpointId(enumerator, eMultimedia);
    for (EndpointObservation& endpoint : *endpoints) {
        endpoint.default_console = !default_console.empty() &&
            endpoint.id == default_console;
        endpoint.default_multimedia = !default_multimedia.empty() &&
            endpoint.id == default_multimedia;
    }

    collection->Release();
    enumerator->Release();
    return S_OK;
}

void PrintEndpoint(const EndpointObservation& endpoint) {
    std::wprintf(L"Endpoint: %ls\n", endpoint.name.c_str());
    std::wprintf(L"  state: %ls\n", StateName(endpoint.state));
    std::wprintf(L"  id: %ls\n", endpoint.id.c_str());
    std::wprintf(L"  container: %ls\n",
                 endpoint.container_id.empty()
                     ? L"(none)"
                     : endpoint.container_id.c_str());
    std::wprintf(L"  default roles: %ls%ls%ls\n",
                 endpoint.default_console ? L"console" : L"",
                 endpoint.default_console && endpoint.default_multimedia
                     ? L", "
                     : L"",
                 endpoint.default_multimedia
                     ? L"multimedia"
                     : (endpoint.default_console ? L"" : L"(none)"));
    if (endpoint.volume_available) {
        std::wprintf(L"  volume: %.1f%%, %.4f dB%ls\n",
                     endpoint.scalar * 100.0f,
                     endpoint.level_db,
                     endpoint.muted ? L" (muted)" : L"");
        if (endpoint.range_available) {
            std::wprintf(L"  volume range: %.4f..%.4f dB, %.4f dB "
                         L"increment\n",
                         endpoint.range_min_db,
                         endpoint.range_max_db,
                         endpoint.range_increment_db);
        } else {
            std::wprintf(L"  volume range: unavailable\n");
        }
        if (endpoint.step_available) {
            std::wprintf(L"  volume step: %u/%u\n",
                         endpoint.step_index,
                         endpoint.step_count);
        } else {
            std::wprintf(L"  volume step: unavailable\n");
        }
    } else {
        std::wprintf(L"  volume: unavailable\n");
    }
}

const EndpointObservation* FindEndpointById(
    const std::vector<EndpointObservation>& endpoints,
    const std::wstring& id) {
    const auto found = std::find_if(
        endpoints.begin(),
        endpoints.end(),
        [&id](const EndpointObservation& endpoint) {
            return endpoint.id == id;
        });
    return found == endpoints.end() ? nullptr : &*found;
}

bool ParseSeconds(const wchar_t* text, unsigned* seconds) {
    if (text == nullptr || seconds == nullptr || *text == L'\0') return false;
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(text, &end, 10);
    if (end == text || *end != L'\0' || parsed < 1u || parsed > 600u) {
        return false;
    }
    *seconds = static_cast<unsigned>(parsed);
    return true;
}

int VerifyContainerMatch(
    const std::vector<EndpointObservation>& endpoints) {
    const EndpointObservation* native_endpoint = nullptr;
    std::vector<std::wstring> reference_ids;
    for (const EndpointObservation& endpoint : endpoints) {
        if (IsNativeEndpointName(endpoint.name)) {
            if (native_endpoint != nullptr) {
                std::fwprintf(
                    stderr,
                    L"Container verification requires exactly one Native "
                    L"LDAC endpoint.\n");
                return 5;
            }
            native_endpoint = &endpoint;
            continue;
        }
        if (!IsXm5ReferenceEndpointName(endpoint.name) ||
            endpoint.container_id.empty()) {
            continue;
        }
        const auto existing = std::find_if(
            reference_ids.begin(),
            reference_ids.end(),
            [&endpoint](const std::wstring& value) {
                return _wcsicmp(value.c_str(),
                                endpoint.container_id.c_str()) == 0;
            });
        if (existing == reference_ids.end()) {
            reference_ids.push_back(endpoint.container_id);
        }
    }

    if (native_endpoint == nullptr || native_endpoint->container_id.empty()) {
        std::fwprintf(stderr,
                      L"The Native LDAC endpoint has no Container ID.\n");
        return 5;
    }
    if (reference_ids.size() != 1u) {
        std::fwprintf(
            stderr,
            L"Expected one unique Windows XM5 reference Container ID, "
            L"found %zu.\n",
            reference_ids.size());
        return 5;
    }
    if (_wcsicmp(native_endpoint->container_id.c_str(),
                 reference_ids[0].c_str()) != 0) {
        std::fwprintf(stderr,
                      L"Container mismatch: Native LDAC=%ls, XM5=%ls.\n",
                      native_endpoint->container_id.c_str(),
                      reference_ids[0].c_str());
        return 6;
    }

    std::wprintf(L"Container match: Native LDAC and XM5 both use %ls.\n",
                 reference_ids[0].c_str());
    return 0;
}

int VerifyDirectRoute(const std::vector<EndpointObservation>& endpoints) {
    const EndpointObservation* direct_endpoint = nullptr;
    for (const EndpointObservation& endpoint : endpoints) {
        if (!IsDirectPdoEndpointName(endpoint.name)) continue;
        if (direct_endpoint != nullptr) {
            std::fwprintf(stderr,
                          L"More than one Direct-PDO endpoint was found.\n");
            return 7;
        }
        direct_endpoint = &endpoint;
    }
    if (direct_endpoint == nullptr) {
        std::fwprintf(stderr,
                      L"The Direct-PDO Native LDAC endpoint was not found.\n");
        return 7;
    }
    if ((direct_endpoint->state & DEVICE_STATE_ACTIVE) == 0u) {
        std::fwprintf(
            stderr,
            L"The Direct-PDO Native LDAC endpoint is %ls, not active.\n",
            StateName(direct_endpoint->state));
        return 8;
    }
    if (!direct_endpoint->default_console ||
        !direct_endpoint->default_multimedia) {
        std::fwprintf(
            stderr,
            L"Select 'Native LDAC - WH-1000XM5' as the Windows default "
            L"output for both console and multimedia audio before this "
            L"trial.\n");
        return 9;
    }
    std::wprintf(
        L"Direct route ready: Native LDAC - WH-1000XM5 is active and is "
        L"the console/multimedia default output.\n");
    return 0;
}

void PrintUsage() {
    std::wprintf(
        L"Usage:\n"
        L"  endpoint_volume_probe.exe --info [--all]\n"
        L"  endpoint_volume_probe.exe --verify-container\n"
        L"  endpoint_volume_probe.exe --verify-direct-route\n"
        L"  endpoint_volume_probe.exe --monitor <seconds> [--all]\n"
        L"  endpoint_volume_probe.exe --monitor-state <seconds> [--all]\n"
        L"\n"
        L"The tool only reads endpoint state and volume. It does not set "
        L"volume or change devices.\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    (void)SetConsoleOutputCP(CP_UTF8);
    (void)_setmode(_fileno(stdout), _O_U8TEXT);
    (void)_setmode(_fileno(stderr), _O_U8TEXT);
    bool monitor_volume = false;
    bool monitor_state = false;
    bool verify_container = false;
    bool verify_direct_route = false;
    bool include_all = false;
    unsigned seconds = 0;
    for (int index = 1; index < argc; ++index) {
        if (std::wcscmp(argv[index], L"--help") == 0 ||
            std::wcscmp(argv[index], L"-h") == 0) {
            PrintUsage();
            return 0;
        }
        if (std::wcscmp(argv[index], L"--info") == 0) {
            continue;
        }
        if (std::wcscmp(argv[index], L"--all") == 0) {
            include_all = true;
            continue;
        }
        if (std::wcscmp(argv[index], L"--verify-container") == 0) {
            verify_container = true;
            continue;
        }
        if (std::wcscmp(argv[index], L"--verify-direct-route") == 0) {
            verify_direct_route = true;
            continue;
        }
        if (std::wcscmp(argv[index], L"--monitor") == 0 &&
            index + 1 < argc && ParseSeconds(argv[index + 1], &seconds)) {
            monitor_volume = true;
            ++index;
            continue;
        }
        if (std::wcscmp(argv[index], L"--monitor-state") == 0 &&
            index + 1 < argc && ParseSeconds(argv[index + 1], &seconds)) {
            monitor_state = true;
            ++index;
            continue;
        }
        std::fwprintf(stderr, L"Invalid argument: %ls\n", argv[index]);
        PrintUsage();
        return 2;
    }
    if (monitor_volume && monitor_state) {
        std::fwprintf(stderr,
                      L"Choose either --monitor or --monitor-state.\n");
        return 2;
    }
    if (verify_container && verify_direct_route) {
        std::fwprintf(stderr,
                      L"Choose one verification mode.\n");
        return 2;
    }
    if ((verify_container || verify_direct_route) &&
        (monitor_volume || monitor_state)) {
        std::fwprintf(stderr,
                      L"A verification mode cannot be combined with a "
                      L"monitor mode.\n");
        return 2;
    }

    const HRESULT initialize_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(initialize_hr);
    if (FAILED(initialize_hr) && initialize_hr != RPC_E_CHANGED_MODE) {
        std::fwprintf(stderr,
                      L"CoInitializeEx failed: 0x%08lX\n",
                      static_cast<unsigned long>(initialize_hr));
        return 3;
    }

    std::vector<EndpointObservation> endpoints;
    const HRESULT enumerate_hr = EnumerateEndpoints(include_all, &endpoints);
    if (FAILED(enumerate_hr)) {
        std::fwprintf(stderr,
                      L"Endpoint enumeration failed: 0x%08lX\n",
                      static_cast<unsigned long>(enumerate_hr));
        if (uninitialize) CoUninitialize();
        return 4;
    }
    if (endpoints.empty()) {
        std::wprintf(L"No matching render endpoints were found.\n");
    }
    if (!verify_direct_route) {
        for (const EndpointObservation& endpoint : endpoints) {
            PrintEndpoint(endpoint);
        }
        std::fflush(stdout);
    }

    if (verify_container) {
        const int result = VerifyContainerMatch(endpoints);
        ReleaseEndpoints(&endpoints);
        if (uninitialize) CoUninitialize();
        return result;
    }
    if (verify_direct_route) {
        const int result = VerifyDirectRoute(endpoints);
        ReleaseEndpoints(&endpoints);
        if (uninitialize) CoUninitialize();
        return result;
    }

    unsigned change_count = 0;
    if (monitor_volume) {
        std::wprintf(L"Monitoring read-only volume changes for %u seconds.\n",
                     seconds);
        std::wprintf(L"Adjust the XM5 side control now.\n");
        const ULONGLONG started = GetTickCount64();
        const ULONGLONG deadline = started + seconds * 1000ull;
        while (GetTickCount64() < deadline) {
            for (EndpointObservation& endpoint : endpoints) {
                if (endpoint.volume == nullptr) continue;
                const float old_scalar = endpoint.scalar;
                const float old_level_db = endpoint.level_db;
                const UINT old_step_index = endpoint.step_index;
                const BOOL old_muted = endpoint.muted;
                if (RefreshVolume(&endpoint) &&
                    (std::fabs(endpoint.scalar - old_scalar) >= 0.0001f ||
                     std::fabs(endpoint.level_db - old_level_db) >= 0.0001f ||
                     (endpoint.step_available &&
                      endpoint.step_index != old_step_index) ||
                     endpoint.muted != old_muted)) {
                    ++change_count;
                    std::wprintf(L"+%llums: %ls -> %.1f%%, %.4f dB",
                                 static_cast<unsigned long long>(
                                     GetTickCount64() - started),
                                 endpoint.name.c_str(),
                                 endpoint.scalar * 100.0f,
                                 endpoint.level_db);
                    if (endpoint.step_available) {
                        std::wprintf(L", step %u/%u",
                                     endpoint.step_index,
                                     endpoint.step_count);
                    }
                    std::wprintf(L"%ls\n",
                                 endpoint.muted ? L" (muted)" : L"");
                }
            }
            Sleep(20u);
        }
        std::wprintf(L"Monitor complete: %u Windows endpoint volume "
                     L"change(s) observed.\n",
                     change_count);
    }
    if (monitor_state) {
        std::wprintf(L"Monitoring read-only endpoint state changes for %u "
                     L"seconds.\n",
                     seconds);
        std::wprintf(L"Start or stop the LDAC agent during this window.\n");
        const ULONGLONG started = GetTickCount64();
        const ULONGLONG deadline = started + seconds * 1000ull;
        for (const EndpointObservation& endpoint : endpoints) {
            std::wprintf(L"+0ms: %ls -> %ls (initial)\n",
                         endpoint.name.c_str(),
                         StateName(endpoint.state));
        }
        std::fflush(stdout);
        while (GetTickCount64() < deadline) {
            std::vector<EndpointObservation> refreshed;
            if (SUCCEEDED(EnumerateEndpoints(include_all, &refreshed))) {
                for (const EndpointObservation& endpoint : refreshed) {
                    const EndpointObservation* previous =
                        FindEndpointById(endpoints, endpoint.id);
                    if (previous == nullptr || previous->state != endpoint.state) {
                        ++change_count;
                        std::wprintf(
                            L"+%llums: %ls -> %ls%ls\n",
                            static_cast<unsigned long long>(
                                GetTickCount64() - started),
                            endpoint.name.c_str(),
                            StateName(endpoint.state),
                            previous == nullptr ? L" (appeared)" : L"");
                        std::fflush(stdout);
                    }
                }
                for (const EndpointObservation& endpoint : endpoints) {
                    if (FindEndpointById(refreshed, endpoint.id) == nullptr) {
                        ++change_count;
                        std::wprintf(
                            L"+%llums: %ls -> removed\n",
                            static_cast<unsigned long long>(
                                GetTickCount64() - started),
                            endpoint.name.c_str());
                        std::fflush(stdout);
                    }
                }
                ReleaseEndpoints(&endpoints);
                endpoints.swap(refreshed);
            } else {
                ReleaseEndpoints(&refreshed);
            }
            Sleep(100u);
        }
        std::wprintf(L"Monitor complete: %u Windows endpoint state "
                     L"change(s) observed.\n",
                     change_count);
    }

    ReleaseEndpoints(&endpoints);
    if (uninitialize) CoUninitialize();
    return 0;
}
