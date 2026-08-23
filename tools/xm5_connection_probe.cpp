// SPDX-License-Identifier: Apache-2.0
#include "runtime_support.h"
#include "xm5_acl_event.h"

#include <windows.h>
#include <BluetoothAPIs.h>
#include <bthdef.h>
#include <cfgmgr32.h>
#include <dbt.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <setupapi.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kAclWindowClass[] = L"NativeLdacXm5AclEventWindow";

struct AclWaitContext {
    BTH_ADDR target_address = 0;
    bool desired_connected = true;
    bool observe = false;
    bool matched = false;
    bool timed_out = false;
    unsigned int event_count = 0u;
    ULONGLONG started_tick = 0u;
};

struct A2dpPdoSummary {
    unsigned int count = 0u;
    unsigned int healthy_count = 0u;
    std::vector<std::wstring> services;
};

struct RenderEndpointSummary {
    unsigned int count = 0u;
    unsigned int active_count = 0u;
    unsigned int unplugged_count = 0u;
    unsigned int not_present_count = 0u;
    unsigned int disabled_count = 0u;
    unsigned int native_count = 0u;
    bool query_failed = false;
};

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t character) {
                       return static_cast<wchar_t>(std::towlower(character));
                   });
    return value;
}

bool ResolveXm5BluetoothAddress(BTH_ADDR* address) {
    if (address == nullptr) {
        return false;
    }
    *address = 0;
    BLUETOOTH_DEVICE_SEARCH_PARAMS search{};
    search.dwSize = sizeof(search);
    search.fReturnAuthenticated = TRUE;
    search.fReturnRemembered = TRUE;
    search.fReturnUnknown = TRUE;
    search.fReturnConnected = TRUE;
    BLUETOOTH_DEVICE_INFO device{};
    device.dwSize = sizeof(device);
    HBLUETOOTH_DEVICE_FIND find =
        BluetoothFindFirstDevice(&search, &device);
    unsigned matches = 0u;
    if (find != nullptr) {
        BOOL has_device = TRUE;
        while (has_device) {
            if (_wcsicmp(device.szName, L"WH-1000XM5") == 0) {
                *address = device.Address.ullLong;
                ++matches;
            }
            device = {};
            device.dwSize = sizeof(device);
            has_device = BluetoothFindNextDevice(find, &device);
        }
        BluetoothFindDeviceClose(find);
    }
    return matches == 1u;
}

std::wstring ReadDeviceRegistryString(HDEVINFO devices,
                                      SP_DEVINFO_DATA* device,
                                      DWORD property) {
    wchar_t value[256]{};
    DWORD data_type = 0u;
    DWORD required = 0u;
    if (!SetupDiGetDeviceRegistryPropertyW(
            devices,
            device,
            property,
            &data_type,
            reinterpret_cast<PBYTE>(value),
            sizeof(value),
            &required) ||
        (data_type != REG_SZ && data_type != REG_EXPAND_SZ)) {
        return std::wstring();
    }
    return std::wstring(value);
}

A2dpPdoSummary QueryA2dpPdoSummary(BTH_ADDR target_address) {
    A2dpPdoSummary summary;
    wchar_t target_text[13]{};
    if (target_address == 0 ||
        swprintf_s(target_text,
                   L"%012llx",
                   static_cast<unsigned long long>(target_address)) < 0) {
        return summary;
    }
    HDEVINFO devices = SetupDiGetClassDevsW(
        nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) {
        return summary;
    }

    for (DWORD index = 0u;; ++index) {
        SP_DEVINFO_DATA device{};
        device.cbSize = sizeof(device);
        if (!SetupDiEnumDeviceInfo(devices, index, &device)) {
            break;
        }
        wchar_t instance_id[MAX_DEVICE_ID_LEN]{};
        if (!SetupDiGetDeviceInstanceIdW(devices,
                                         &device,
                                         instance_id,
                                         MAX_DEVICE_ID_LEN,
                                         nullptr)) {
            continue;
        }
        const std::wstring lowered = Lowercase(instance_id);
        if (lowered.find(
                L"bthenum\\{0000110b-0000-1000-8000-00805f9b34fb}") ==
                std::wstring::npos ||
            lowered.find(target_text) == std::wstring::npos) {
            continue;
        }

        ++summary.count;
        ULONG status = 0u;
        ULONG problem = 0u;
        if (CM_Get_DevNode_Status(&status, &problem, device.DevInst, 0u) ==
                CR_SUCCESS &&
            problem == 0u &&
            (status & DN_STARTED) != 0u) {
            ++summary.healthy_count;
        }
        std::wstring service = ReadDeviceRegistryString(
            devices, &device, SPDRP_SERVICE);
        if (service.empty()) {
            service = L"(none)";
        }
        if (std::find(summary.services.begin(),
                      summary.services.end(),
                      service) == summary.services.end()) {
            summary.services.push_back(service);
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    return summary;
}

std::wstring ReadEndpointName(IMMDevice* device) {
    if (device == nullptr) {
        return std::wstring();
    }
    IPropertyStore* properties = nullptr;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &properties)) ||
        properties == nullptr) {
        return std::wstring();
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring name;
    if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) &&
        value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
        name = value.pwszVal;
    }
    PropVariantClear(&value);
    properties->Release();
    return name;
}

RenderEndpointSummary QueryRenderEndpointSummary() {
    RenderEndpointSummary summary;
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* collection = nullptr;
    HRESULT result = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_INPROC_SERVER,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&enumerator));
    if (FAILED(result) || enumerator == nullptr) {
        summary.query_failed = true;
        return summary;
    }
    result = enumerator->EnumAudioEndpoints(
        eRender, DEVICE_STATEMASK_ALL, &collection);
    if (FAILED(result) || collection == nullptr) {
        summary.query_failed = true;
        enumerator->Release();
        return summary;
    }

    UINT count = 0u;
    if (FAILED(collection->GetCount(&count))) {
        summary.query_failed = true;
        collection->Release();
        enumerator->Release();
        return summary;
    }
    for (UINT index = 0u; index < count; ++index) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(index, &device)) || device == nullptr) {
            continue;
        }
        const std::wstring name = ReadEndpointName(device);
        const bool native = name.find(L"Native LDAC") != std::wstring::npos;
        const bool xm5 = name.find(L"WH-1000XM5") != std::wstring::npos;
        if (!native && !xm5) {
            device->Release();
            continue;
        }
        DWORD state = 0u;
        (void)device->GetState(&state);
        ++summary.count;
        if (native) {
            ++summary.native_count;
        }
        if ((state & DEVICE_STATE_ACTIVE) != 0u) {
            ++summary.active_count;
        }
        if ((state & DEVICE_STATE_UNPLUGGED) != 0u) {
            ++summary.unplugged_count;
        }
        if ((state & DEVICE_STATE_NOTPRESENT) != 0u) {
            ++summary.not_present_count;
        }
        if ((state & DEVICE_STATE_DISABLED) != 0u) {
            ++summary.disabled_count;
        }
        device->Release();
    }
    collection->Release();
    enumerator->Release();
    return summary;
}

const wchar_t* ConnectionStateName(
    native_ldac::agent::Xm5ConnectionState state) {
    switch (state) {
        case native_ldac::agent::Xm5ConnectionState::Connected:
            return L"connected";
        case native_ldac::agent::Xm5ConnectionState::Disconnected:
            return L"disconnected";
        case native_ldac::agent::Xm5ConnectionState::QueryFailed:
            return L"query-failed";
    }
    return L"query-failed";
}

void PrintTimelineSnapshot(const AclWaitContext& context,
                           const wchar_t* reason) {
    DWORD query_error = ERROR_SUCCESS;
    const auto connection =
        native_ldac::agent::QueryXm5Connection(&query_error);
    const A2dpPdoSummary pdo =
        QueryA2dpPdoSummary(context.target_address);
    const RenderEndpointSummary endpoints = QueryRenderEndpointSummary();
    std::wstring services;
    for (const std::wstring& service : pdo.services) {
        if (!services.empty()) {
            services += L",";
        }
        services += service;
    }
    if (services.empty()) {
        services = L"none";
    }
    std::wprintf(
        L"+%llums snapshot(%ls): fConnected=%ls, "
        L"a2dp-pdo=%u/%u-healthy service=%ls, "
        L"render=%u active=%u unplugged=%u not-present=%u "
        L"disabled=%u native=%u%ls.\n",
        static_cast<unsigned long long>(
            GetTickCount64() - context.started_tick),
        reason,
        ConnectionStateName(connection),
        pdo.count,
        pdo.healthy_count,
        services.c_str(),
        endpoints.count,
        endpoints.active_count,
        endpoints.unplugged_count,
        endpoints.not_present_count,
        endpoints.disabled_count,
        endpoints.native_count,
        endpoints.query_failed ? L" endpoint-query-failed" : L"");
    std::fflush(stdout);
}

LRESULT CALLBACK AclWindowProc(HWND window,
                               UINT message,
                               WPARAM wparam,
                               LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create =
            reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window,
                          GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(
                              create->lpCreateParams));
        return TRUE;
    }

    auto* context = reinterpret_cast<AclWaitContext*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (context == nullptr) {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    if (message == WM_TIMER) {
        if (wparam == 1u) {
            context->timed_out = true;
            PostQuitMessage(0);
        } else if (context->observe && wparam == 2u) {
            KillTimer(window, 2u);
            PrintTimelineSnapshot(*context, L"acl+1000ms");
        } else if (context->observe && wparam == 3u) {
            KillTimer(window, 3u);
            PrintTimelineSnapshot(*context, L"acl+3000ms");
        }
        return 0;
    }
    if (message == WM_DEVICECHANGE && wparam == DBT_CUSTOMEVENT &&
        lparam != 0) {
        const native_ldac::agent::Xm5AclEvent event =
            native_ldac::agent::ParseXm5AclDeviceChange(
                context->target_address, wparam, lparam);
        if (event != native_ldac::agent::Xm5AclEvent::None) {
            const bool connected =
                event == native_ldac::agent::Xm5AclEvent::Connected;
            if (context->observe) {
                ++context->event_count;
                std::wprintf(L"+%llums ACL %ls.\n",
                             static_cast<unsigned long long>(
                                 GetTickCount64() - context->started_tick),
                             connected ? L"connected" : L"disconnected");
                PrintTimelineSnapshot(*context, L"acl-event");
                KillTimer(window, 2u);
                KillTimer(window, 3u);
                (void)SetTimer(window, 2u, 1000u, nullptr);
                (void)SetTimer(window, 3u, 3000u, nullptr);
                return 0;
            }
            if (connected == context->desired_connected) {
                context->matched = true;
                PostQuitMessage(0);
                return 0;
            }
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int WaitForAclTransition(bool desired_connected,
                         DWORD timeout_seconds) {
    DWORD query_error = ERROR_SUCCESS;
    const native_ldac::agent::Xm5ConnectionState initial_state =
        native_ldac::agent::QueryXm5Connection(&query_error);
    const auto expected_initial = desired_connected
        ? native_ldac::agent::Xm5ConnectionState::Disconnected
        : native_ldac::agent::Xm5ConnectionState::Connected;
    if (initial_state != expected_initial) {
        std::fwprintf(
            stderr,
            L"XM5 ACL watcher was not armed because the initial state "
            L"is not %ls.\n",
            desired_connected ? L"disconnected" : L"connected");
        return 12;
    }

    BLUETOOTH_FIND_RADIO_PARAMS radio_params{
        sizeof(BLUETOOTH_FIND_RADIO_PARAMS)};
    HANDLE radio = nullptr;
    HBLUETOOTH_RADIO_FIND radio_find =
        BluetoothFindFirstRadio(&radio_params, &radio);
    if (radio_find == nullptr || radio == nullptr) {
        std::fwprintf(stderr,
                      L"Bluetooth radio open failed (Win32 %lu).\n",
                      GetLastError());
        return 11;
    }

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = AclWindowProc;
    window_class.hInstance = instance;
    window_class.lpszClassName = kAclWindowClass;
    if (RegisterClassW(&window_class) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        const DWORD error = GetLastError();
        CloseHandle(radio);
        BluetoothFindRadioClose(radio_find);
        std::fwprintf(stderr,
                      L"ACL event window registration failed "
                      L"(Win32 %lu).\n",
                      error);
        return 11;
    }

    AclWaitContext context{};
    if (!ResolveXm5BluetoothAddress(&context.target_address)) {
        CloseHandle(radio);
        BluetoothFindRadioClose(radio_find);
        std::fwprintf(stderr,
                      L"Expected one unique paired WH-1000XM5 address.\n");
        return 11;
    }
    context.desired_connected = desired_connected;
    context.started_tick = GetTickCount64();
    HWND window = CreateWindowExW(0,
                                  kAclWindowClass,
                                  L"",
                                  0,
                                  0,
                                  0,
                                  0,
                                  0,
                                  HWND_MESSAGE,
                                  nullptr,
                                  instance,
                                  &context);
    if (window == nullptr) {
        const DWORD error = GetLastError();
        CloseHandle(radio);
        BluetoothFindRadioClose(radio_find);
        std::fwprintf(stderr,
                      L"ACL event window creation failed (Win32 %lu).\n",
                      error);
        return 11;
    }

    DEV_BROADCAST_HANDLE filter{};
    filter.dbch_size = sizeof(filter);
    filter.dbch_devicetype = DBT_DEVTYP_HANDLE;
    filter.dbch_handle = radio;
    HDEVNOTIFY notification = RegisterDeviceNotificationW(
        window,
        &filter,
        DEVICE_NOTIFY_WINDOW_HANDLE);
    if (notification == nullptr) {
        const DWORD error = GetLastError();
        DestroyWindow(window);
        CloseHandle(radio);
        BluetoothFindRadioClose(radio_find);
        std::fwprintf(stderr,
                      L"ACL event registration failed (Win32 %lu).\n",
                      error);
        return 11;
    }

    const native_ldac::agent::Xm5ConnectionState armed_state =
        native_ldac::agent::QueryXm5Connection(&query_error);
    if (armed_state != expected_initial) {
        UnregisterDeviceNotification(notification);
        DestroyWindow(window);
        CloseHandle(radio);
        BluetoothFindRadioClose(radio_find);
        std::fwprintf(stderr,
                      L"XM5 state changed before the ACL watcher was "
                      L"fully armed; no transition was accepted.\n");
        return 12;
    }

    const UINT_PTR timer = SetTimer(
        window,
        1u,
        timeout_seconds * 1000u,
        nullptr);
    if (timer == 0u) {
        const DWORD error = GetLastError();
        UnregisterDeviceNotification(notification);
        DestroyWindow(window);
        CloseHandle(radio);
        BluetoothFindRadioClose(radio_find);
        std::fwprintf(stderr,
                      L"ACL event timer creation failed (Win32 %lu).\n",
                      error);
        return 11;
    }

    std::wprintf(
        desired_connected
            ? L"XM5 ACL watcher armed. Turn on XM5 normally now.\n"
            : L"XM5 ACL watcher armed. Turn off XM5 normally now.\n");
    std::fflush(stdout);

    MSG message{};
    while (!context.matched && !context.timed_out) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            if (result < 0) {
                std::fwprintf(stderr,
                              L"ACL event message wait failed "
                              L"(Win32 %lu).\n",
                              GetLastError());
            }
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    KillTimer(window, timer);
    UnregisterDeviceNotification(notification);
    DestroyWindow(window);
    CloseHandle(radio);
    BluetoothFindRadioClose(radio_find);

    if (context.matched) {
        std::wprintf(L"XM5 ACL event: %ls.\n",
                     desired_connected ? L"connected" : L"disconnected");
        return 0;
    }
    if (context.timed_out) {
        std::wprintf(L"XM5 ACL event wait timed out.\n");
        return 10;
    }
    return 11;
}

int ObserveAclTimeline(DWORD timeout_seconds) {
    BLUETOOTH_FIND_RADIO_PARAMS radio_params{
        sizeof(BLUETOOTH_FIND_RADIO_PARAMS)};
    HANDLE radio = nullptr;
    HBLUETOOTH_RADIO_FIND radio_find =
        BluetoothFindFirstRadio(&radio_params, &radio);
    if (radio_find == nullptr || radio == nullptr) {
        std::fwprintf(stderr,
                      L"Bluetooth radio open failed (Win32 %lu).\n",
                      GetLastError());
        return 11;
    }

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = AclWindowProc;
    window_class.hInstance = instance;
    window_class.lpszClassName = kAclWindowClass;
    if (RegisterClassW(&window_class) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        const DWORD error = GetLastError();
        CloseHandle(radio);
        BluetoothFindRadioClose(radio_find);
        std::fwprintf(stderr,
                      L"ACL event window registration failed "
                      L"(Win32 %lu).\n",
                      error);
        return 11;
    }

    AclWaitContext context{};
    if (!ResolveXm5BluetoothAddress(&context.target_address)) {
        CloseHandle(radio);
        BluetoothFindRadioClose(radio_find);
        std::fwprintf(stderr,
                      L"Expected one unique paired WH-1000XM5 address.\n");
        return 11;
    }
    context.observe = true;
    context.started_tick = GetTickCount64();
    HWND window = CreateWindowExW(0,
                                  kAclWindowClass,
                                  L"",
                                  0,
                                  0,
                                  0,
                                  0,
                                  0,
                                  HWND_MESSAGE,
                                  nullptr,
                                  instance,
                                  &context);
    if (window == nullptr) {
        const DWORD error = GetLastError();
        CloseHandle(radio);
        BluetoothFindRadioClose(radio_find);
        std::fwprintf(stderr,
                      L"ACL event window creation failed (Win32 %lu).\n",
                      error);
        return 11;
    }

    DEV_BROADCAST_HANDLE filter{};
    filter.dbch_size = sizeof(filter);
    filter.dbch_devicetype = DBT_DEVTYP_HANDLE;
    filter.dbch_handle = radio;
    HDEVNOTIFY notification = RegisterDeviceNotificationW(
        window, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (notification == nullptr) {
        const DWORD error = GetLastError();
        DestroyWindow(window);
        CloseHandle(radio);
        BluetoothFindRadioClose(radio_find);
        std::fwprintf(stderr,
                      L"ACL event registration failed (Win32 %lu).\n",
                      error);
        return 11;
    }

    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize_com = SUCCEEDED(com_result);
    context.started_tick = GetTickCount64();
    std::wprintf(L"XM5 read-only ACL timeline armed for %lu seconds.\n",
                 timeout_seconds);
    PrintTimelineSnapshot(context, L"initial");
    if (SetTimer(window, 1u, timeout_seconds * 1000u, nullptr) == 0u) {
        const DWORD error = GetLastError();
        UnregisterDeviceNotification(notification);
        DestroyWindow(window);
        CloseHandle(radio);
        BluetoothFindRadioClose(radio_find);
        if (uninitialize_com) {
            CoUninitialize();
        }
        std::fwprintf(stderr,
                      L"ACL event timer creation failed (Win32 %lu).\n",
                      error);
        return 11;
    }

    bool message_wait_failed = false;
    MSG message{};
    while (!context.timed_out) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            if (result < 0) {
                message_wait_failed = true;
            }
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    KillTimer(window, 1u);
    KillTimer(window, 2u);
    KillTimer(window, 3u);
    PrintTimelineSnapshot(context, L"final");
    UnregisterDeviceNotification(notification);
    DestroyWindow(window);
    CloseHandle(radio);
    BluetoothFindRadioClose(radio_find);
    if (uninitialize_com) {
        CoUninitialize();
    }
    std::wprintf(L"XM5 ACL timeline complete: %u event(s).\n",
                 context.event_count);
    std::wprintf(L"No inquiry, connection request, AVDTP OPEN, driver, "
                 L"endpoint, or system setting was changed.\n");
    return message_wait_failed ? 11 : 0;
}

int PrintRadioState() {
    BLUETOOTH_FIND_RADIO_PARAMS radio_params{
        sizeof(BLUETOOTH_FIND_RADIO_PARAMS)};
    HANDLE radio = nullptr;
    HBLUETOOTH_RADIO_FIND radio_find =
        BluetoothFindFirstRadio(&radio_params, &radio);
    if (radio_find == nullptr || radio == nullptr) {
        std::wprintf(L"Bluetooth radio state: unavailable.\n");
        return 10;
    }

    const bool connectable = BluetoothIsConnectable(radio) != FALSE;
    CloseHandle(radio);
    BluetoothFindRadioClose(radio_find);
    if (connectable) {
        std::wprintf(L"Bluetooth radio state: ready.\n");
        return 0;
    }
    std::wprintf(L"Bluetooth radio state: not-connectable.\n");
    return 10;
}

void PrintUsage() {
    std::wprintf(
        L"Usage:\n"
        L"  xm5_connection_probe.exe --state\n"
        L"  xm5_connection_probe.exe --radio-state\n"
        L"  xm5_connection_probe.exe --wait-acl-connect <seconds>\n"
        L"  xm5_connection_probe.exe --wait-acl-disconnect <seconds>\n"
        L"  xm5_connection_probe.exe --observe-acl <seconds>\n"
        L"\n"
        L"Reads fConnected state or waits for a real XM5 ACL transition "
        L"notification.\n"
        L"No inquiry, connection, driver, endpoint, or setting is changed.\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 &&
        (std::wcscmp(argv[1], L"--help") == 0 ||
         std::wcscmp(argv[1], L"-h") == 0)) {
        PrintUsage();
        return 0;
    }
    if (argc == 3 &&
        (std::wcscmp(argv[1], L"--wait-acl-connect") == 0 ||
         std::wcscmp(argv[1], L"--wait-acl-disconnect") == 0 ||
         std::wcscmp(argv[1], L"--observe-acl") == 0)) {
        wchar_t* end = nullptr;
        const unsigned long seconds = std::wcstoul(argv[2], &end, 10);
        if (end == argv[2] || *end != L'\0' ||
            seconds == 0 || seconds > 600) {
            std::fwprintf(stderr,
                          L"ACL wait timeout must be 1..600 seconds.\n");
            return 2;
        }
        if (std::wcscmp(argv[1], L"--observe-acl") == 0) {
            return ObserveAclTimeline(static_cast<DWORD>(seconds));
        }
        return WaitForAclTransition(
            std::wcscmp(argv[1], L"--wait-acl-connect") == 0,
            static_cast<DWORD>(seconds));
    }
    if (argc == 2 && std::wcscmp(argv[1], L"--radio-state") == 0) {
        return PrintRadioState();
    }
    if (argc != 2 || std::wcscmp(argv[1], L"--state") != 0) {
        PrintUsage();
        return 2;
    }

    DWORD query_error = ERROR_SUCCESS;
    const native_ldac::agent::Xm5ConnectionState state =
        native_ldac::agent::QueryXm5Connection(&query_error);
    if (state == native_ldac::agent::Xm5ConnectionState::Connected) {
        std::wprintf(L"XM5 Bluetooth state: connected.\n");
        return 0;
    }
    if (state == native_ldac::agent::Xm5ConnectionState::Disconnected) {
        std::wprintf(L"XM5 Bluetooth state: disconnected.\n");
        return 10;
    }

    std::fwprintf(stderr,
                  L"XM5 Bluetooth state query failed (Win32 %lu).\n",
                  query_error);
    return 11;
}
