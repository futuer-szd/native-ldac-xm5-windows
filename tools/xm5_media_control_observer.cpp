// SPDX-License-Identifier: Apache-2.0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <hidsdi.h>
#include <hidpi.h>

#include <algorithm>
#include <cstdarg>
#include <cctype>
#include <cstdio>
#include <cwctype>
#include <cwchar>
#include <fcntl.h>
#include <io.h>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "xm5_media_control_event.h"

namespace {

constexpr UINT_PTR kStopTimer = 1u;
constexpr std::uint16_t kConsumerUsagePage = 0x000Cu;

struct Options {
    DWORD duration_seconds = 120u;
    std::wstring output_path;
    bool include_all_devices = false;
};

struct DeviceIdentity {
    std::wstring path;
    USHORT vendor_id = 0u;
    USHORT product_id = 0u;
    bool is_xm5 = false;
};

struct ObserverState {
    HWND window = nullptr;
    HHOOK keyboard_hook = nullptr;
    FILE* output = nullptr;
    ULONGLONG started_at = 0u;
    bool include_all_devices = false;
    std::map<HANDLE, DeviceIdentity> devices;
    std::map<HANDLE, std::set<std::uint16_t>> consumer_usages;
};

ObserverState* g_state = nullptr;

void PrintHelp() {
    std::wprintf(
        L"Usage: xm5_media_control_observer "
        L"[--duration-seconds 5..600] [--output PATH] "
        L"[--include-all-devices]\n"
        L"Observes Raw Input HID Consumer Control, media virtual keys, and "
        L"WM_APPCOMMAND. It never injects input or changes audio/media state.\n");
}

bool ParseUnsigned(const wchar_t* text, DWORD minimum, DWORD maximum,
                   DWORD* value) {
    if (text == nullptr || value == nullptr || *text == L'\0') return false;
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(text, &end, 10);
    if (end == nullptr || *end != L'\0' || parsed < minimum ||
        parsed > maximum) {
        return false;
    }
    *value = static_cast<DWORD>(parsed);
    return true;
}

bool ParseArguments(int argc, wchar_t** argv, Options* options,
                    bool* show_help) {
    if (options == nullptr || show_help == nullptr) return false;
    *show_help = false;
    for (int index = 1; index < argc; ++index) {
        if (std::wcscmp(argv[index], L"--help") == 0 ||
            std::wcscmp(argv[index], L"-h") == 0) {
            *show_help = true;
            return true;
        }
        if (std::wcscmp(argv[index], L"--include-all-devices") == 0) {
            options->include_all_devices = true;
            continue;
        }
        if (std::wcscmp(argv[index], L"--duration-seconds") == 0 &&
            index + 1 < argc) {
            if (!ParseUnsigned(argv[++index], 5u, 600u,
                               &options->duration_seconds)) {
                return false;
            }
            continue;
        }
        if (std::wcscmp(argv[index], L"--output") == 0 &&
            index + 1 < argc) {
            options->output_path = argv[++index];
            if (options->output_path.empty()) return false;
            continue;
        }
        return false;
    }
    return true;
}

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t character) {
                       return static_cast<wchar_t>(std::towlower(character));
                   });
    return value;
}

DeviceIdentity GetDeviceIdentity(HANDLE device) {
    DeviceIdentity identity;
    UINT name_size = 0u;
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr,
                               &name_size) == 0u &&
        name_size != 0u) {
        std::vector<wchar_t> name(name_size + 1u, L'\0');
        UINT copied = name_size;
        if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, name.data(),
                                   &copied) != static_cast<UINT>(-1)) {
            identity.path.assign(name.data());
        }
    }

    RID_DEVICE_INFO info{};
    info.cbSize = sizeof(info);
    UINT info_size = sizeof(info);
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICEINFO, &info,
                               &info_size) != static_cast<UINT>(-1) &&
        info.dwType == RIM_TYPEHID) {
        identity.vendor_id = static_cast<USHORT>(info.hid.dwVendorId);
        identity.product_id = static_cast<USHORT>(info.hid.dwProductId);
    }
    const std::wstring lower = Lowercase(identity.path);
    identity.is_xm5 =
        (identity.vendor_id == 0x054Cu &&
         identity.product_id == 0x0DF0u) ||
        (lower.find(L"vid_054c") != std::wstring::npos &&
         lower.find(L"pid_0df0") != std::wstring::npos) ||
        (lower.find(L"vid&0002054c") != std::wstring::npos &&
         lower.find(L"pid&0df0") != std::wstring::npos);
    return identity;
}

void WriteLine(ObserverState* state, const wchar_t* format, ...) {
    if (state == nullptr || format == nullptr) return;
    va_list arguments;
    va_start(arguments, format);
    std::vwprintf(format, arguments);
    va_end(arguments);
    std::wprintf(L"\n");
    std::fflush(stdout);
    if (state->output != nullptr) {
        va_start(arguments, format);
        std::vfwprintf(state->output, format, arguments);
        va_end(arguments);
        std::fwprintf(state->output, L"\n");
        std::fflush(state->output);
    }
}

void LogControl(ObserverState* state, const wchar_t* source,
                native_ldac::agent::Xm5MediaControl control,
                const wchar_t* phase, std::uint32_t code,
                const DeviceIdentity* identity, bool injected) {
    if (state == nullptr || control ==
            native_ldac::agent::Xm5MediaControl::Unknown) {
        return;
    }
    const ULONGLONG elapsed = GetTickCount64() - state->started_at;
    if (identity != nullptr) {
        WriteLine(state,
                  L"%llu ms source=%ls control=%ls phase=%ls code=0x%04X "
                  L"vid=%04X pid=%04X xm5=%ls device=%ls",
                  elapsed, source,
                  native_ldac::agent::Xm5MediaControlName(control), phase,
                  code, identity->vendor_id, identity->product_id,
                  identity->is_xm5 ? L"yes" : L"no",
                  identity->path.empty() ? L"(unknown)" :
                                           identity->path.c_str());
    } else {
        WriteLine(state,
                  L"%llu ms source=%ls control=%ls phase=%ls code=0x%04X "
                  L"injected=%ls device=(not-attributed)",
                  elapsed, source,
                  native_ldac::agent::Xm5MediaControlName(control), phase,
                  code, injected ? L"yes" : L"no");
    }
}

void LogRawReport(ObserverState* state, const DeviceIdentity& identity,
                  const BYTE* report, DWORD report_size) {
    if (state == nullptr || report == nullptr || report_size == 0u) return;
    std::wstring hex;
    wchar_t byte_text[4]{};
    for (DWORD index = 0u; index < report_size; ++index) {
        if (index != 0u) hex.push_back(L' ');
        (void)swprintf_s(byte_text, L"%02X", report[index]);
        hex.append(byte_text);
    }
    const ULONGLONG elapsed = GetTickCount64() - state->started_at;
    WriteLine(state,
              L"%llu ms source=raw-hid-report vid=%04X pid=%04X xm5=%ls "
              L"bytes=%ls device=%ls",
              elapsed, identity.vendor_id, identity.product_id,
              identity.is_xm5 ? L"yes" : L"no", hex.c_str(),
              identity.path.empty() ? L"(unknown)" : identity.path.c_str());
}

void LogDeviceInventory(ObserverState* state) {
    if (state == nullptr) return;
    UINT count = 0u;
    if (GetRawInputDeviceList(nullptr, &count,
                              sizeof(RAWINPUTDEVICELIST)) != 0u ||
        count == 0u) {
        WriteLine(state, L"0 ms source=raw-device-inventory count=0");
        return;
    }
    std::vector<RAWINPUTDEVICELIST> devices(count);
    UINT copied = count;
    const UINT result = GetRawInputDeviceList(
        devices.data(), &copied, sizeof(RAWINPUTDEVICELIST));
    if (result == static_cast<UINT>(-1)) {
        WriteLine(state, L"0 ms source=raw-device-inventory error=%lu",
                  GetLastError());
        return;
    }
    UINT logged = 0u;
    for (UINT index = 0u; index < result; ++index) {
        const DeviceIdentity identity =
            GetDeviceIdentity(devices[index].hDevice);
        if (!identity.is_xm5 && !state->include_all_devices) continue;
        state->devices[devices[index].hDevice] = identity;
        const wchar_t* type = L"other";
        if (devices[index].dwType == RIM_TYPEHID) type = L"hid";
        if (devices[index].dwType == RIM_TYPEKEYBOARD) type = L"keyboard";
        WriteLine(state,
                  L"%llu ms source=raw-device-inventory type=%ls "
                  L"vid=%04X pid=%04X xm5=%ls device=%ls",
                  GetTickCount64() - state->started_at, type,
                  identity.vendor_id, identity.product_id,
                  identity.is_xm5 ? L"yes" : L"no",
                  identity.path.empty() ? L"(unknown)" :
                                          identity.path.c_str());
        ++logged;
    }
    WriteLine(state,
              L"%llu ms source=raw-device-inventory matched=%u total=%u",
              GetTickCount64() - state->started_at, logged, result);
}

std::set<std::uint16_t> GetConsumerUsages(
    PHIDP_PREPARSED_DATA preparsed, const HIDP_CAPS& caps,
    const BYTE* report, DWORD report_size) {
    std::set<std::uint16_t> values;
    if (preparsed == nullptr || report == nullptr || report_size == 0u ||
        caps.NumberInputButtonCaps == 0u) {
        return values;
    }
    std::vector<HIDP_BUTTON_CAPS> button_caps(caps.NumberInputButtonCaps);
    USHORT cap_count = caps.NumberInputButtonCaps;
    if (HidP_GetButtonCaps(HidP_Input, button_caps.data(), &cap_count,
                           preparsed) != HIDP_STATUS_SUCCESS) {
        return values;
    }
    button_caps.resize(cap_count);
    const ULONG maximum_usages =
        HidP_MaxUsageListLength(HidP_Input, kConsumerUsagePage, preparsed);
    if (maximum_usages == 0u) return values;

    for (const auto& cap : button_caps) {
        if (cap.UsagePage != kConsumerUsagePage) continue;
        std::vector<USAGE> usages(maximum_usages);
        ULONG usage_count = maximum_usages;
        if (HidP_GetUsages(
                HidP_Input, kConsumerUsagePage, cap.LinkCollection,
                usages.data(), &usage_count, preparsed,
                reinterpret_cast<PCHAR>(const_cast<BYTE*>(report)),
                report_size) != HIDP_STATUS_SUCCESS) {
            continue;
        }
        for (ULONG index = 0u; index < usage_count; ++index) {
            values.insert(static_cast<std::uint16_t>(usages[index]));
        }
    }
    return values;
}

void HandleRawHid(ObserverState* state, const RAWINPUT& input,
                  const DeviceIdentity& identity) {
    if (state == nullptr || (!identity.is_xm5 &&
                             !state->include_all_devices)) {
        return;
    }
    UINT preparsed_size = 0u;
    if (GetRawInputDeviceInfoW(input.header.hDevice, RIDI_PREPARSEDDATA,
                               nullptr, &preparsed_size) ==
            static_cast<UINT>(-1) ||
        preparsed_size == 0u) {
        return;
    }
    std::vector<BYTE> preparsed_buffer(preparsed_size);
    UINT copied_size = preparsed_size;
    if (GetRawInputDeviceInfoW(input.header.hDevice, RIDI_PREPARSEDDATA,
                               preparsed_buffer.data(), &copied_size) ==
        static_cast<UINT>(-1)) {
        return;
    }
    auto* preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(
        preparsed_buffer.data());
    HIDP_CAPS caps{};
    if (HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS) return;

    auto& previous = state->consumer_usages[input.header.hDevice];
    for (DWORD report_index = 0u;
         report_index < input.data.hid.dwCount;
         ++report_index) {
        const BYTE* report = input.data.hid.bRawData +
            report_index * input.data.hid.dwSizeHid;
        LogRawReport(state, identity, report, input.data.hid.dwSizeHid);
        const auto current = GetConsumerUsages(
            preparsed, caps, report, input.data.hid.dwSizeHid);
        for (const std::uint16_t usage : current) {
            if (previous.find(usage) != previous.end()) continue;
            LogControl(state, L"raw-hid-consumer",
                       native_ldac::agent::
                           Xm5MediaControlFromConsumerUsage(usage),
                       L"press", usage, &identity, false);
        }
        for (const std::uint16_t usage : previous) {
            if (current.find(usage) != current.end()) continue;
            LogControl(state, L"raw-hid-consumer",
                       native_ldac::agent::
                           Xm5MediaControlFromConsumerUsage(usage),
                       L"release", usage, &identity, false);
        }
        previous = current;
    }
}

void HandleRawInput(ObserverState* state, HRAWINPUT handle) {
    UINT size = 0u;
    if (GetRawInputData(handle, RID_INPUT, nullptr, &size,
                        sizeof(RAWINPUTHEADER)) != 0u || size == 0u) {
        return;
    }
    std::vector<BYTE> buffer(size);
    UINT copied = size;
    if (GetRawInputData(handle, RID_INPUT, buffer.data(), &copied,
                        sizeof(RAWINPUTHEADER)) != size) {
        return;
    }
    const auto* input = reinterpret_cast<const RAWINPUT*>(buffer.data());
    const DeviceIdentity identity = GetDeviceIdentity(input->header.hDevice);
    if (identity.is_xm5 || state->include_all_devices) {
        state->devices[input->header.hDevice] = identity;
    }
    if (input->header.dwType == RIM_TYPEKEYBOARD) {
        if (!identity.is_xm5 && !state->include_all_devices) return;
        const auto control = native_ldac::agent::
            Xm5MediaControlFromVirtualKey(input->data.keyboard.VKey);
        const bool released =
            (input->data.keyboard.Flags & RI_KEY_BREAK) != 0u;
        LogControl(state, L"raw-keyboard", control,
                   released ? L"release" : L"press",
                   input->data.keyboard.VKey, &identity, false);
    } else if (input->header.dwType == RIM_TYPEHID) {
        HandleRawHid(state, *input, identity);
    }
}

LRESULT CALLBACK KeyboardHook(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION && g_state != nullptr &&
        (message == WM_KEYDOWN || message == WM_KEYUP ||
         message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        const auto control = native_ldac::agent::
            Xm5MediaControlFromVirtualKey(
                static_cast<std::uint16_t>(key->vkCode));
        LogControl(g_state, L"low-level-keyboard", control,
                   (message == WM_KEYUP || message == WM_SYSKEYUP)
                       ? L"release" : L"press",
                   key->vkCode, nullptr,
                   (key->flags & LLKHF_INJECTED) != 0u);
    }
    return CallNextHookEx(nullptr, code, message, data);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                            LPARAM lparam) {
    auto* state = reinterpret_cast<ObserverState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<ObserverState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(state));
    }
    switch (message) {
        case WM_INPUT:
            HandleRawInput(state, reinterpret_cast<HRAWINPUT>(lparam));
            return 0;
        case WM_APPCOMMAND: {
            const std::uint16_t command = static_cast<std::uint16_t>(
                GET_APPCOMMAND_LPARAM(lparam));
            LogControl(state, L"wm-appcommand",
                       native_ldac::agent::
                           Xm5MediaControlFromAppCommand(command),
                       L"command", command, nullptr, false);
            break;
        }
        case WM_INPUT_DEVICE_CHANGE:
            if (state != nullptr) {
                const HANDLE device = reinterpret_cast<HANDLE>(lparam);
                DeviceIdentity identity;
                if (wparam == GIDC_ARRIVAL) {
                    identity = GetDeviceIdentity(device);
                    if (identity.is_xm5 || state->include_all_devices) {
                        state->devices[device] = identity;
                    }
                } else {
                    const auto known = state->devices.find(device);
                    if (known != state->devices.end()) {
                        identity = known->second;
                    }
                }
                if (identity.is_xm5 || state->include_all_devices) {
                    WriteLine(
                        state,
                        L"%llu ms source=raw-device-change action=%ls "
                        L"vid=%04X pid=%04X xm5=%ls device=%ls",
                        GetTickCount64() - state->started_at,
                        wparam == GIDC_ARRIVAL ? L"arrival" : L"removal",
                        identity.vendor_id, identity.product_id,
                        identity.is_xm5 ? L"yes" : L"no",
                        identity.path.empty() ? L"(unknown)" :
                                                identity.path.c_str());
                }
                if (wparam == GIDC_REMOVAL) {
                    state->devices.erase(device);
                    state->consumer_usages.erase(device);
                }
            }
            return 0;
        case WM_TIMER:
            if (wparam == kStopTimer) DestroyWindow(window);
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

BOOL WINAPI ConsoleHandler(DWORD type) {
    if ((type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
         type == CTRL_CLOSE_EVENT) &&
        g_state != nullptr && g_state->window != nullptr) {
        PostMessageW(g_state->window, WM_CLOSE, 0u, 0);
        return TRUE;
    }
    return FALSE;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    (void)_setmode(_fileno(stdout), _O_U16TEXT);
    (void)_setmode(_fileno(stderr), _O_U16TEXT);

    Options options;
    bool show_help = false;
    if (!ParseArguments(argc, argv, &options, &show_help)) {
        PrintHelp();
        return 2;
    }
    if (show_help) {
        PrintHelp();
        return 0;
    }

    ObserverState state;
    state.started_at = GetTickCount64();
    state.include_all_devices = options.include_all_devices;
    if (!options.output_path.empty()) {
        if (_wfopen_s(&state.output, options.output_path.c_str(),
                      L"wt, ccs=UTF-8") != 0 || state.output == nullptr) {
            std::fwprintf(stderr, L"Could not create output file: %ls\n",
                          options.output_path.c_str());
            return 1;
        }
    }

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    constexpr wchar_t kWindowClass[] = L"NativeLdacXm5MediaControlObserver";
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = WindowProc;
    window_class.lpszClassName = kWindowClass;
    if (RegisterClassExW(&window_class) == 0u) {
        if (state.output != nullptr) std::fclose(state.output);
        return 1;
    }
    state.window = CreateWindowExW(
        0u, kWindowClass, L"XM5 media control observer", WS_OVERLAPPED,
        0, 0, 0, 0, nullptr, nullptr, instance, &state);
    if (state.window == nullptr) {
        UnregisterClassW(kWindowClass, instance);
        if (state.output != nullptr) std::fclose(state.output);
        return 1;
    }

    RAWINPUTDEVICE devices[] = {
        {0x0001u, 0x0006u, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, state.window},
        {kConsumerUsagePage, 0x0001u,
         RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, state.window},
    };
    if (!RegisterRawInputDevices(devices, ARRAYSIZE(devices),
                                 sizeof(devices[0]))) {
        DestroyWindow(state.window);
        UnregisterClassW(kWindowClass, instance);
        if (state.output != nullptr) std::fclose(state.output);
        return 1;
    }

    g_state = &state;
    state.keyboard_hook = SetWindowsHookExW(
        WH_KEYBOARD_LL, KeyboardHook, instance, 0u);
    (void)SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    (void)SetTimer(state.window, kStopTimer,
                   options.duration_seconds * 1000u, nullptr);
    WriteLine(&state,
              L"XM5 media-control observer armed for %lu second(s). "
              L"Operate only the XM5 touch panel; no input is injected.",
              options.duration_seconds);
    LogDeviceInventory(&state);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0u, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (state.keyboard_hook != nullptr) {
        UnhookWindowsHookEx(state.keyboard_hook);
    }
    (void)SetConsoleCtrlHandler(ConsoleHandler, FALSE);
    g_state = nullptr;
    UnregisterClassW(kWindowClass, instance);
    if (state.output != nullptr) std::fclose(state.output);
    return 0;
}
