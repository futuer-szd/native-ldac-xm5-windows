// SPDX-License-Identifier: Apache-2.0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <swdevice.h>

#include <cstdio>
#include <cwchar>
#include <string>

#include "v1_endpoint_presence_sink.h"

namespace {

constexpr wchar_t kEnumeratorName[] = L"NativeLdacSwdEndpoint";
constexpr wchar_t kInstanceId[] = L"Xm5EndpointCandidate";
constexpr wchar_t kHardwareId[] = L"SWD\\NativeLdacAudioXm5";
constexpr wchar_t kHardwareIds[] = L"SWD\\NativeLdacAudioXm5\0";
constexpr wchar_t kDescription[] =
    L"Native LDAC XM5 transport-owned endpoint candidate";

struct CreateContext {
    HANDLE completed = nullptr;
    HRESULT result = E_PENDING;
    std::wstring instance_id;
};

void WINAPI OnCreated(HSWDEVICE,
                      HRESULT create_result,
                      void* raw_context,
                      PCWSTR instance_id) {
    auto* context = static_cast<CreateContext*>(raw_context);
    if (context == nullptr) return;
    context->result = create_result;
    context->instance_id = instance_id == nullptr ? L"" : instance_id;
    if (context->completed != nullptr) {
        (void)SetEvent(context->completed);
    }
}

bool ParseDuration(const wchar_t* text, DWORD* seconds) {
    if (text == nullptr || seconds == nullptr || *text == L'\0') {
        return false;
    }
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(text, &end, 10);
    if (end == text || *end != L'\0' || parsed < 10ul || parsed > 300ul) {
        return false;
    }
    *seconds = static_cast<DWORD>(parsed);
    return true;
}

bool ParseGuid(const std::wstring& text, GUID* value) {
    return value != nullptr && !text.empty() &&
           SUCCEEDED(CLSIDFromString(text.c_str(), value));
}

void PrintPlan() {
    std::wprintf(
        L"V1 transport-owned endpoint candidate plan:\n"
        L"  enumerator: %ls\n"
        L"  instance: %ls\n"
        L"  hardware ID: %ls\n"
        L"  parent: exact XM5 A2DP PDO supplied by the caller\n"
        L"  container: exact XM5 Container ID supplied by the caller\n"
        L"  driver package: must already be staged by a separate approved gate\n"
        L"  lifetime: handle-bound, 10..300 seconds\n"
        L"  expected effect when explicitly created: bind the isolated "
        L"NativeLdacSwdAudio service and publish one candidate render endpoint\n"
        L"  default action: read-only plan; no device or endpoint is created\n",
        kEnumeratorName,
        kInstanceId,
        kHardwareId);
}

void PrintUsage() {
    std::wprintf(
        L"Usage:\n"
        L"  v1_swd_endpoint_host.exe --plan\n"
        L"  v1_swd_endpoint_host.exe --create --parent <instance-id> "
        L"--container <guid> --duration-seconds <10..300> "
        L"--confirm-endpoint-binding-probe "
        L"[--stop-event <name>] "
        L"[--publish-presence --confirm-volume-observation]\n\n"
        L"Target hardware ID: SWD\\NativeLdacAudioXm5.\n"
        L"The default/--plan path is read-only. The create path is reserved "
        L"for a separately approved gate after the isolated driver package "
        L"has been staged. It does not install a driver, change Bluetooth, "
        L"or replace the current ROOT endpoint.\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    (void)SetConsoleOutputCP(CP_UTF8);
    bool create = false;
    bool confirmed = false;
    bool publish_presence = false;
    bool volume_observation_confirmed = false;
    std::wstring parent;
    std::wstring container_text;
    std::wstring stop_event_name;
    DWORD duration_seconds = 0u;

    if (argc == 1) {
        PrintPlan();
        return 0;
    }
    for (int index = 1; index < argc; ++index) {
        if (std::wcscmp(argv[index], L"--help") == 0 ||
            std::wcscmp(argv[index], L"-h") == 0) {
            PrintUsage();
            return 0;
        }
        if (std::wcscmp(argv[index], L"--plan") == 0) {
            continue;
        }
        if (std::wcscmp(argv[index], L"--create") == 0) {
            create = true;
            continue;
        }
        if (std::wcscmp(argv[index],
                        L"--confirm-endpoint-binding-probe") == 0) {
            confirmed = true;
            continue;
        }
        if (std::wcscmp(argv[index], L"--publish-presence") == 0) {
            publish_presence = true;
            continue;
        }
        if (std::wcscmp(argv[index],
                        L"--confirm-volume-observation") == 0) {
            volume_observation_confirmed = true;
            continue;
        }
        if (std::wcscmp(argv[index], L"--stop-event") == 0 &&
            index + 1 < argc) {
            stop_event_name = argv[++index];
            if (stop_event_name.empty()) return 2;
            continue;
        }
        if (std::wcscmp(argv[index], L"--parent") == 0 &&
            index + 1 < argc) {
            parent = argv[++index];
            continue;
        }
        if (std::wcscmp(argv[index], L"--container") == 0 &&
            index + 1 < argc) {
            container_text = argv[++index];
            continue;
        }
        if (std::wcscmp(argv[index], L"--duration-seconds") == 0 &&
            index + 1 < argc &&
            ParseDuration(argv[index + 1], &duration_seconds)) {
            ++index;
            continue;
        }
        std::fwprintf(stderr, L"Invalid argument: %ls\n", argv[index]);
        PrintUsage();
        return 2;
    }

    if (!create) {
        PrintPlan();
        return 0;
    }
    if (!confirmed || parent.empty() || container_text.empty() ||
        duration_seconds == 0u) {
        std::fwprintf(
            stderr,
            L"Creation requires the exact parent/container, a 10..300 "
            L"second bound, and --confirm-endpoint-binding-probe.\n");
        return 2;
    }
    if (publish_presence != volume_observation_confirmed) {
        std::fwprintf(
            stderr,
            L"Candidate presence publication requires both "
            L"--publish-presence and --confirm-volume-observation.\n");
        return 2;
    }

    GUID container{};
    if (!ParseGuid(container_text, &container)) {
        std::fwprintf(stderr, L"The container GUID is invalid.\n");
        return 2;
    }

    CreateContext context;
    context.completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (context.completed == nullptr) {
        std::fwprintf(stderr,
                      L"CreateEvent failed (Win32 %lu).\n",
                      GetLastError());
        return 3;
    }

    SW_DEVICE_CREATE_INFO create_info{};
    create_info.cbSize = sizeof(create_info);
    create_info.pszInstanceId = kInstanceId;
    create_info.pszzHardwareIds = kHardwareIds;
    create_info.pszzCompatibleIds = nullptr;
    create_info.pContainerId = &container;
    create_info.CapabilityFlags =
        SWDeviceCapabilitiesDriverRequired |
        SWDeviceCapabilitiesSilentInstall |
        SWDeviceCapabilitiesNoDisplayInUI;
    create_info.pszDeviceDescription = kDescription;

    HSWDEVICE device = nullptr;
    const HRESULT create_hr = SwDeviceCreate(
        kEnumeratorName,
        parent.c_str(),
        &create_info,
        0u,
        nullptr,
        OnCreated,
        &context,
        &device);
    if (FAILED(create_hr) || device == nullptr) {
        CloseHandle(context.completed);
        std::fwprintf(stderr,
                      L"SwDeviceCreate failed immediately (0x%08lX).\n",
                      static_cast<unsigned long>(create_hr));
        return 4;
    }

    const DWORD wait = WaitForSingleObject(context.completed, 20000u);
    if (wait != WAIT_OBJECT_0 || FAILED(context.result)) {
        SwDeviceClose(device);
        CloseHandle(context.completed);
        std::fwprintf(stderr,
                      L"SWD endpoint callback failed or timed out "
                      L"(wait %lu, HRESULT 0x%08lX).\n",
                      wait,
                      static_cast<unsigned long>(context.result));
        return 5;
    }
    const HRESULT lifetime_hr =
        SwDeviceSetLifetime(device, SWDeviceLifetimeHandle);
    if (FAILED(lifetime_hr)) {
        SwDeviceClose(device);
        CloseHandle(context.completed);
        std::fwprintf(stderr,
                      L"SwDeviceSetLifetime failed (0x%08lX).\n",
                      static_cast<unsigned long>(lifetime_hr));
        return 6;
    }

    HANDLE stop_event = nullptr;
    if (!stop_event_name.empty()) {
        stop_event = OpenEventW(SYNCHRONIZE,
                                FALSE,
                                stop_event_name.c_str());
        if (stop_event == nullptr) {
            SwDeviceClose(device);
            CloseHandle(context.completed);
            std::fwprintf(stderr,
                          L"Could not open the bounded stop event "
                          L"(Win32 %lu).\n",
                          GetLastError());
            return 7;
        }
    }

    native_ldac::agent::V1EndpointPresenceSink presence_sink;
    std::uint64_t presence_generation = 0u;
    if (publish_presence) {
        const ULONGLONG open_deadline = GetTickCount64() + 20000u;
        DWORD open_error = ERROR_NOT_FOUND;
        bool presence_opened = false;
        while (GetTickCount64() < open_deadline && !presence_opened) {
            presence_opened = presence_sink.OpenForInstanceId(
                context.instance_id,
                &open_error);
            if (presence_opened) break;
            Sleep(100u);
        }
        if (!presence_opened) {
            if (stop_event != nullptr) CloseHandle(stop_event);
            SwDeviceClose(device);
            CloseHandle(context.completed);
            std::fwprintf(stderr,
                          L"Could not open the exact candidate presence "
                          L"interface (Win32 %lu).\n",
                          open_error);
            return 8;
        }
        presence_generation =
            static_cast<std::uint64_t>(GetTickCount64());
        if (presence_generation == 0u) presence_generation = 1u;
        DWORD presence_error = ERROR_SUCCESS;
        if (!presence_sink.Set(true,
                               presence_generation,
                               &presence_error)) {
            presence_sink.Close();
            if (stop_event != nullptr) CloseHandle(stop_event);
            SwDeviceClose(device);
            CloseHandle(context.completed);
            std::fwprintf(stderr,
                          L"Could not publish candidate presence "
                          L"(Win32 %lu).\n",
                          presence_error);
            return 9;
        }
        std::wprintf(L"SWD endpoint candidate presence published: "
                     L"generation %llu.\n",
                     static_cast<unsigned long long>(presence_generation));
    }

    std::wprintf(L"SWD endpoint candidate created: %ls\n",
                 context.instance_id.c_str());
    std::wprintf(L"Holding for %lu second(s); the approved gate must "
                 L"validate driver/endpoint state.\n",
                 duration_seconds);
    const ULONGLONG hold_deadline =
        GetTickCount64() + static_cast<ULONGLONG>(duration_seconds) * 1000u;
    bool hold_failed = false;
    DWORD hold_error = ERROR_SUCCESS;
    bool stop_event_observed = false;
    ULONGLONG next_presence_heartbeat = GetTickCount64() + 5000u;
    while (GetTickCount64() < hold_deadline) {
        const ULONGLONG now = GetTickCount64();
        DWORD wait_ms = static_cast<DWORD>(
            (hold_deadline - now) > 1000u ? 1000u : hold_deadline - now);
        if (publish_presence && next_presence_heartbeat > now) {
            const ULONGLONG until_heartbeat = next_presence_heartbeat - now;
            if (until_heartbeat < wait_ms) {
                wait_ms = static_cast<DWORD>(until_heartbeat);
            }
        }
        const DWORD wait_result = stop_event == nullptr
            ? (Sleep(wait_ms), WAIT_TIMEOUT)
            : WaitForSingleObject(stop_event, wait_ms);
        if (wait_result == WAIT_OBJECT_0) {
            stop_event_observed = true;
            std::wprintf(L"SWD endpoint candidate stop event observed.\n");
            break;
        }
        if (wait_result != WAIT_TIMEOUT) {
            hold_failed = true;
            hold_error = GetLastError();
            break;
        }
        if (publish_presence && GetTickCount64() >= next_presence_heartbeat) {
            if (!presence_sink.Set(true,
                                   presence_generation,
                                   &hold_error)) {
                hold_failed = true;
                break;
            }
            next_presence_heartbeat = GetTickCount64() + 5000u;
        }
    }
    if (stop_event != nullptr && !stop_event_observed && !hold_failed) {
        hold_failed = true;
        hold_error = ERROR_TIMEOUT;
    }
    if (publish_presence) {
        DWORD release_error = ERROR_SUCCESS;
        if (!presence_sink.Set(false,
                               presence_generation,
                               &release_error) &&
            !hold_failed) {
            hold_failed = true;
            hold_error = release_error;
        }
        presence_sink.Close();
        std::wprintf(L"SWD endpoint candidate presence released.\n");
    }
    if (stop_event != nullptr) CloseHandle(stop_event);
    SwDeviceClose(device);
    CloseHandle(context.completed);
    std::wprintf(L"SWD endpoint candidate handle closed.\n");
    if (hold_failed) {
        std::fwprintf(stderr,
                      L"SWD endpoint candidate hold failed (Win32 %lu).\n",
                      hold_error);
        return 10;
    }
    return 0;
}
