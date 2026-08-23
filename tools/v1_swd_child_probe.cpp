// SPDX-License-Identifier: Apache-2.0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <swdevice.h>

#include <cstdio>
#include <cwchar>
#include <string>

namespace {

constexpr wchar_t kEnumeratorName[] = L"NativeLdacVolumeSyncProbe";
constexpr wchar_t kInstanceId[] = L"Xm5TopologyProbe";
constexpr wchar_t kDescription[] =
    L"Native LDAC XM5 volume-sync topology probe";

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
    if (end == text || *end != L'\0' || parsed < 1ul || parsed > 30ul) {
        return false;
    }
    *seconds = static_cast<DWORD>(parsed);
    return true;
}

bool ParseGuid(const std::wstring& text, GUID* value) {
    if (value == nullptr || text.empty()) return false;
    return SUCCEEDED(CLSIDFromString(text.c_str(), value));
}

void PrintPlan() {
    std::wprintf(
        L"V1 driverless SWD topology probe plan:\n"
        L"  enumerator: %ls\n"
        L"  instance: %ls\n"
        L"  hardware IDs: none\n"
        L"  compatible IDs: none\n"
        L"  driver binding: forbidden\n"
        L"  audio endpoint creation: forbidden\n"
        L"  lifetime: handle-bound, maximum 30 seconds\n"
        L"  parent: exact XM5 A2DP PDO supplied by the caller\n"
        L"  container: exact XM5 Container ID supplied by the caller\n"
        L"  close behavior: SwDeviceClose removes the probe\n",
        kEnumeratorName,
        kInstanceId);
}

void PrintUsage() {
    std::wprintf(
        L"Usage:\n"
        L"  v1_swd_child_probe.exe --plan\n"
        L"  v1_swd_child_probe.exe --create --parent <instance-id> "
        L"--container <guid> --duration-seconds <1..30> "
        L"--confirm-driverless-probe\n\n"
        L"The default/--plan path is read-only. --create makes one bounded "
        L"driverless software child and removes it by closing its handle. "
        L"It never supplies hardware IDs, binds NativeLdacAudio, creates an "
        L"audio endpoint, changes Bluetooth, or sends AVRCP data.\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    (void)SetConsoleOutputCP(CP_UTF8);
    bool create = false;
    bool confirmed = false;
    std::wstring parent;
    std::wstring container_text;
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
        if (std::wcscmp(argv[index], L"--confirm-driverless-probe") == 0) {
            confirmed = true;
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
            L"Creation requires the exact parent/container, a 1..30 second "
            L"bound, and --confirm-driverless-probe.\n");
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
    create_info.pszzHardwareIds = nullptr;
    create_info.pszzCompatibleIds = nullptr;
    create_info.pContainerId = &container;
    create_info.CapabilityFlags =
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

    const DWORD wait = WaitForSingleObject(context.completed, 10000u);
    if (wait != WAIT_OBJECT_0 || FAILED(context.result)) {
        SwDeviceClose(device);
        CloseHandle(context.completed);
        std::fwprintf(stderr,
                      L"SWD creation callback failed or timed out "
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

    std::wprintf(L"Driverless SWD child created: %ls\n",
                 context.instance_id.c_str());
    std::wprintf(L"Holding for %lu second(s); no driver or interface is "
                 L"registered.\n",
                 duration_seconds);
    Sleep(duration_seconds * 1000u);
    SwDeviceClose(device);
    CloseHandle(context.completed);
    std::wprintf(L"Driverless SWD child handle closed; the probe is removed.\n");
    return 0;
}
