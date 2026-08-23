// SPDX-License-Identifier: Apache-2.0
#define NOMINMAX
#include <windows.h>

#include <cfgmgr32.h>
#include <devguid.h>
#include <initguid.h>
#include <devpkey.h>
#include <setupapi.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

#include "v1_avrcp_handoff_ipc.h"
#include "v1_avrcp_handoff_state.h"

namespace {

using native_ldac::agent::V1AvrcpHandoffIpc;
using native_ldac::agent::V1AvrcpHandoffInput;
using native_ldac::agent::V1AvrcpHandoffPhase;
using native_ldac::agent::V1AvrcpHandoffState;
using native_ldac::agent::V1AvrcpHandoffTransition;
using native_ldac::agent::V1AvrcpHandoffWaitResult;

constexpr wchar_t kTargetPrefix[] =
    L"BTHENUM\\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0";
constexpr wchar_t kMicrosoftService[] = L"Microsoft_Bluetooth_AvrcpTransport";
constexpr wchar_t kObserverService[] = L"NativeLdacAvrcpObserver";
constexpr wchar_t kObserverInfName[] = L"NativeLdacAvrcpObserver.inf";
constexpr DWORD kPnpUtilTimeoutMs = 60000u;
constexpr DWORD kBindPollTimeoutMs = 30000u;
constexpr DWORD kRestoreBindPollTimeoutMs = 45000u;

struct DeviceSnapshot {
    std::wstring instance_id;
    std::wstring service;
    std::wstring inf;
    ULONG problem_code = 0u;
    bool present = false;
};

bool StoreError(DWORD value, DWORD* error) {
    if (error != nullptr) {
        *error = value;
    }
    return false;
}

std::wstring Trim(const std::wstring& text) {
    const size_t first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return std::wstring();
    }
    const size_t last = text.find_last_not_of(L" \t\r\n");
    return text.substr(first, last - first + 1u);
}

// The handoff host runs as SYSTEM without a console, so all diagnostic
// output goes to a log file under %ProgramData%\NativeLdac. Creating a
// directory and appending a timestamped line is intentionally simple.
void LogHost(const wchar_t* message) {
    wchar_t program_data[MAX_PATH] = {};
    if (GetEnvironmentVariableW(
            L"ProgramData", program_data, MAX_PATH) == 0u) {
        wcscpy_s(program_data, L"C:\\ProgramData");
    }
    std::wstring directory = program_data;
    directory += L"\\NativeLdac";
    (void)CreateDirectoryW(directory.c_str(), nullptr);
    std::wstring path = directory + L"\\handoff-host.log";
    HANDLE file = CreateFileW(
        path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    wchar_t timestamp[64] = {};
    SYSTEMTIME local{};
    GetLocalTime(&local);
    (void)swprintf_s(
        timestamp,
        64,
        L"[%04u-%02u-%02uT%02u:%02u:%02u] ",
        local.wYear,
        local.wMonth,
        local.wDay,
        local.wHour,
        local.wMinute,
        local.wSecond);
    std::wstring line = timestamp;
    line += message;
    line += L"\r\n";
    DWORD written = 0u;
    (void)WriteFile(
        file,
        line.c_str(),
        static_cast<DWORD>(line.size() * sizeof(wchar_t)),
        &written,
        nullptr);
    CloseHandle(file);
}

std::string RunPnpUtil(const std::vector<std::wstring>& arguments) {
    std::wstring command = L"pnputil.exe";
    for (const std::wstring& argument : arguments) {
        command += L" \"";
        command += argument;
        command += L"\"";
    }
    std::wstring command_line = command;

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (CreatePipe(&read_pipe, &write_pipe, &security, 0) == FALSE) {
        return std::string();
    }
    if (SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0) == FALSE) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return std::string();
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    // SYSTEM sessions have no console, so GetStdHandle(STD_INPUT_HANDLE)
    // returns an invalid handle and STARTF_USESTDHANDLES would fail. Always
    // bind stdin to NUL; pnputil never reads from stdin.
    HANDLE nul_input = CreateFileW(
        L"NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    startup.hStdInput = nul_input;
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutable_command(
        command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');
    const BOOL created = CreateProcessW(
        nullptr,
        mutable_command.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process);
    if (nul_input != nullptr && nul_input != INVALID_HANDLE_VALUE) {
        CloseHandle(nul_input);
    }
    CloseHandle(write_pipe);
    if (created == FALSE) {
        CloseHandle(read_pipe);
        return std::string();
    }
    CloseHandle(process.hThread);

    // pnputil writes ANSI/OEM text (locale dependent), so collect raw bytes
    // and search them directly; never reinterpret as UTF-16.
    std::string output;
    char buffer[4096];
    DWORD read_bytes = 0u;
    while (ReadFile(read_pipe, buffer, sizeof(buffer), &read_bytes, nullptr) !=
               FALSE &&
           read_bytes > 0u) {
        output.append(buffer, read_bytes);
        read_bytes = 0u;
    }
    CloseHandle(read_pipe);
    if (WaitForSingleObject(process.hProcess, kPnpUtilTimeoutMs) !=
        WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1u);
    }
    DWORD exit_code = 0u;
    (void)GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    return output;
}

std::wstring FindPublishedInf(const std::string& pnputil_output) {
    // pnputil reports the published INF as "oemNN.inf"; the token is ASCII
    // even in localized output, so search the raw byte stream.
    size_t position = 0u;
    while (position < pnputil_output.size()) {
        const size_t start = pnputil_output.find("oem", position);
        if (start == std::string::npos) {
            break;
        }
        size_t end = start + 3u;
        while (end < pnputil_output.size() &&
               pnputil_output[end] >= '0' && pnputil_output[end] <= '9') {
            ++end;
        }
        if (end > start + 3u &&
            end + 4u <= pnputil_output.size() &&
            pnputil_output.compare(end, 4u, ".inf") == 0) {
            const std::string ascii =
                pnputil_output.substr(start, end + 4u - start);
            return std::wstring(ascii.begin(), ascii.end());
        }
        position = start + 1u;
    }
    return std::wstring();
}

bool GetInstanceId(DEVINST device, std::wstring* instance_id) {
    if (instance_id == nullptr) {
        return false;
    }
    wchar_t buffer[MAX_DEVICE_ID_LEN] = {};
    if (CM_Get_Device_IDW(device, buffer, MAX_DEVICE_ID_LEN, 0u) !=
        CR_SUCCESS) {
        return false;
    }
    *instance_id = buffer;
    return true;
}

bool GetDeviceRegistryString(DEVINST device,
                             DWORD property,
                             std::wstring* value) {
    if (value == nullptr) {
        return false;
    }
    DWORD type = 0u;
    wchar_t buffer[512] = {};
    DWORD size = sizeof(buffer);
    const CONFIGRET status = CM_Get_DevNode_Registry_PropertyW(
        device,
        property,
        &type,
        buffer,
        &size,
        0u);
    if (status != CR_SUCCESS || type != REG_SZ) {
        return false;
    }
    *value = buffer;
    return true;
}

bool GetDevicePropertyString(DEVINST device,
                             const DEVPROPKEY& property,
                             std::wstring* value) {
    if (value == nullptr) {
        return false;
    }
    DEVPROPTYPE type = 0u;
    wchar_t buffer[512] = {};
    ULONG size = sizeof(buffer);
    const CONFIGRET status = CM_Get_DevNode_PropertyW(
        device,
        &property,
        &type,
        reinterpret_cast<PBYTE>(buffer),
        &size,
        0u);
    if (status != CR_SUCCESS || type != DEVPROP_TYPE_STRING) {
        return false;
    }
    *value = buffer;
    return true;
}

bool GetDeviceSnapshot(DEVINST device, DeviceSnapshot* snapshot) {
    if (snapshot == nullptr) {
        return false;
    }
    if (!GetInstanceId(device, &snapshot->instance_id)) {
        return false;
    }
    (void)GetDeviceRegistryString(
        device, CM_DRP_SERVICE, &snapshot->service);
    (void)GetDevicePropertyString(
        device, DEVPKEY_Device_DriverInfPath, &snapshot->inf);
    ULONG status = 0u;
    ULONG problem = 0u;
    if (CM_Get_DevNode_Status(&status, &problem, device, 0u) == CR_SUCCESS) {
        // FindTargetDevice enumerates with DIGCF_PRESENT, so a device that
        // reaches this point is present; only the problem code is needed.
        snapshot->present = true;
        snapshot->problem_code = problem;
    }
    return true;
}

DeviceSnapshot FindTargetDevice() {
    DeviceSnapshot found;
    HDEVINFO devices = SetupDiGetClassDevsW(
        nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) {
        return found;
    }
    for (DWORD index = 0u;; ++index) {
        SP_DEVINFO_DATA data{};
        data.cbSize = sizeof(data);
        if (SetupDiEnumDeviceInfo(devices, index, &data) == FALSE) {
            break;
        }
        DeviceSnapshot snapshot;
        if (!GetDeviceSnapshot(data.DevInst, &snapshot)) {
            continue;
        }
        if (_wcsnicmp(
                snapshot.instance_id.c_str(),
                kTargetPrefix,
                wcslen(kTargetPrefix)) == 0) {
            found = std::move(snapshot);
            break;
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    return found;
}

bool WaitForBinding(const std::wstring& service,
                    DWORD timeout_ms,
                    DeviceSnapshot* snapshot) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    do {
        DeviceSnapshot current = FindTargetDevice();
        if (!current.instance_id.empty() && current.present &&
            current.problem_code == 0u &&
            _wcsicmp(current.service.c_str(), service.c_str()) == 0) {
            if (snapshot != nullptr) {
                *snapshot = std::move(current);
            }
            return true;
        }
        Sleep(500u);
    } while (GetTickCount64() < deadline);
    return false;
}

bool IsMicrosoftBaseline(const DeviceSnapshot& snapshot) {
    return snapshot.present && snapshot.problem_code == 0u &&
        _wcsicmp(snapshot.service.c_str(), kMicrosoftService) == 0;
}

bool IsObserverBound(const DeviceSnapshot& snapshot) {
    return snapshot.present && snapshot.problem_code == 0u &&
        _wcsicmp(snapshot.service.c_str(), kObserverService) == 0;
}

}  // namespace

namespace {

// Handles one handoff/restore session driven by the pure state machine.
class HandoffSessionRunner {
public:
    HandoffSessionRunner(V1AvrcpHandoffIpc* ipc,
                         const std::wstring& observer_inf_path,
                         const std::wstring& instance_id,
                         std::uint64_t generation,
                         std::uint32_t restart_count,
                         std::wstring* resident_published_inf)
        : ipc_(ipc),
          observer_inf_path_(observer_inf_path),
          instance_id_(instance_id),
          generation_(generation),
          restart_count_(restart_count),
          resident_published_inf_(resident_published_inf) {}

    // Drives one media-period handoff session: stage + bind the observer,
    // report active to the daily host, then restore Microsoft at the end.
    bool RunHandoff(DWORD* error) {
        V1AvrcpHandoffInput input;
        input.media_streaming = true;
        LogHost(L"handoff session started");
        for (;;) {
            const V1AvrcpHandoffTransition transition = state_.Step(input);
            if (!transition.changed) {
                break;
            }
            if (transition.request_stage_observer) {
                if (!StageObserver(error)) {
                    LogHost(L"stage observer failed");
                    input.handoff_restart_failed = true;
                    continue;
                }
                LogHost(L"observer package staged and installed");
                if (!WaitForBinding(
                        kObserverService, kBindPollTimeoutMs, nullptr)) {
                    LogHost(L"observer did not bind; restarting the PDO once");
                    if (!RestartDevice(error) ||
                        !WaitForBinding(
                            kObserverService,
                            kBindPollTimeoutMs,
                            nullptr)) {
                        LogHost(L"observer bind failed after restart");
                        input.handoff_restart_failed = true;
                        continue;
                    }
                }
                LogHost(L"observer bound as the function driver");
                input.handoff_restart_done = true;
            }
            if (transition.request_restore_restart) {
                if (!RestoreMicrosoft(error)) {
                    LogHost(L"restore failed; session marked degraded");
                    input.restore_restart_failed = true;
                    continue;
                }
                if (resident_published_inf_ != nullptr) {
                    resident_published_inf_->clear();
                }
                LogHost(L"Microsoft AVRCP restored");
                input.restore_restart_done = true;
            }
            if (transition.notify_daily_active) {
                if (!ipc_->SignalHandoffCompleted(
                        generation_, restart_count_, L"", error)) {
                    LogHost(L"handoff completion signal failed");
                    const DWORD signal_error =
                        error == nullptr ? ERROR_GEN_FAILURE : *error;
                    if (!RestoreMicrosoft(error)) {
                        LogHost(L"handoff completion failure rollback failed");
                        return false;
                    }
                    if (resident_published_inf_ != nullptr) {
                        resident_published_inf_->clear();
                    }
                    LogHost(L"handoff completion failure rolled back to Microsoft");
                    return StoreError(signal_error, error);
                }
                LogHost(L"handoff completed and daily host notified");
            }
        }
        if (state_.phase() == V1AvrcpHandoffPhase::MicrosoftHeld &&
            state_.restore_restart_used()) {
            if (!ipc_->SignalRestoreCompleted(
                    generation_,
                    restart_count_,
                    state_.degraded() ? L"restore-restart-failed" : L"",
                    error)) {
                LogHost(L"restore completion signal failed");
                return false;
            }
            LogHost(L"restore completed and daily host notified");
        }
        return true;
    }

    // Handles an independent restore request (media stopped with no active
    // handoff session). It must perform the real transaction because the
    // owner normally stays with the observer until the daily host ends the
    // media-scoped lease.
    bool RunRestore(DWORD* error) {
        DeviceSnapshot current = FindTargetDevice();
        if (IsMicrosoftBaseline(current)) {
            LogHost(L"restore request found Microsoft AVRCP already healthy");
            return ipc_->SignalRestoreCompleted(
                generation_, restart_count_, L"", error);
        }
        const bool has_recorded_observer =
            resident_published_inf_ != nullptr &&
            !resident_published_inf_->empty();
        if (has_recorded_observer) {
            published_inf_ = *resident_published_inf_;
        }
        if (current.instance_id.empty() && !published_inf_.empty()) {
            if (!RestoreMicrosoft(error)) {
                const DWORD restore_error =
                    error == nullptr ? ERROR_GEN_FAILURE : *error;
                DWORD signal_error = ERROR_SUCCESS;
                (void)ipc_->SignalRestoreCompleted(
                    generation_,
                    restart_count_,
                    L"restore-restart-failed",
                    &signal_error);
                return StoreError(restore_error, error);
            }
            LogHost(L"absent PDO restore prepared Microsoft for next enumeration");
            return ipc_->SignalRestoreCompleted(
                generation_, restart_count_, L"", error);
        }
        if (published_inf_.empty() &&
            _wcsicmp(current.service.c_str(), kObserverService) == 0 &&
            !current.inf.empty()) {
            published_inf_ = current.inf;
        }
        if (published_inf_.empty() && !IsObserverBound(current)) {
            LogHost(L"restore request found neither healthy Microsoft nor observer binding");
            (void)ipc_->SignalRestoreCompleted(
                generation_,
                restart_count_,
                L"unexpected-current-owner",
                error);
            return StoreError(ERROR_INVALID_STATE, error);
        }
        if (has_recorded_observer && !IsObserverBound(current)) {
            LogHost(L"restore using the recorded observer INF despite a transient unhealthy owner snapshot");
        }
        if (!RestoreMicrosoft(error)) {
            const DWORD restore_error =
                error == nullptr ? ERROR_GEN_FAILURE : *error;
            LogHost(L"independent restore failed");
            DWORD signal_error = ERROR_SUCCESS;
            (void)ipc_->SignalRestoreCompleted(
                generation_,
                restart_count_,
                L"restore-restart-failed",
                &signal_error);
            return StoreError(restore_error, error);
        }
        if (resident_published_inf_ != nullptr) {
            resident_published_inf_->clear();
        }
        LogHost(L"independent restore completed");
        return ipc_->SignalRestoreCompleted(
            generation_, restart_count_, L"", error);
    }

private:
    bool StageObserver(DWORD* error) {
        const std::string output = RunPnpUtil({
            L"/add-driver", observer_inf_path_, L"/install"});
        if (output.empty()) {
            return StoreError(ERROR_GEN_FAILURE, error);
        }
        published_inf_ = FindPublishedInf(output);
        if (!published_inf_.empty()) {
            const std::wstring message =
                L"pnputil published " + published_inf_;
            LogHost(message.c_str());
        }
        if (resident_published_inf_ != nullptr) {
            *resident_published_inf_ = published_inf_;
        }
        return !published_inf_.empty();
    }

    bool RestartDevice(DWORD* error) {
        const std::string output = RunPnpUtil({
            L"/restart-device", instance_id_});
        if (output.empty()) {
            return StoreError(ERROR_GEN_FAILURE, error);
        }
        return true;
    }

    bool RestoreMicrosoft(DWORD* error) {
        const DeviceSnapshot before_restore = FindTargetDevice();
        if (IsMicrosoftBaseline(before_restore)) {
            published_inf_.clear();
            if (resident_published_inf_ != nullptr) {
                resident_published_inf_->clear();
            }
            if (error != nullptr) {
                *error = ERROR_SUCCESS;
            }
            return true;
        }
        if (published_inf_.empty()) {
            if (IsObserverBound(before_restore)) {
                published_inf_ = before_restore.inf;
            }
        }
        if (published_inf_.empty()) {
            LogHost(L"restore failed: no published observer INF known");
            return StoreError(ERROR_INVALID_STATE, error);
        }
        const std::wstring delete_message =
            L"restore: deleting " + published_inf_;
        LogHost(delete_message.c_str());
        (void)RunPnpUtil({
            L"/delete-driver", published_inf_, L"/uninstall", L"/force"});
        (void)RunPnpUtil({L"/scan-devices"});
        const DeviceSnapshot after_scan = FindTargetDevice();
        if (after_scan.instance_id.empty()) {
            published_inf_.clear();
            if (resident_published_inf_ != nullptr) {
                resident_published_inf_->clear();
            }
            if (error != nullptr) {
                *error = ERROR_SUCCESS;
            }
            return true;
        }
        if (IsMicrosoftBaseline(after_scan)) {
            published_inf_.clear();
            if (resident_published_inf_ != nullptr) {
                resident_published_inf_->clear();
            }
            if (error != nullptr) {
                *error = ERROR_SUCCESS;
            }
            return true;
        }
        if (!RestartDevice(error)) {
            return false;
        }
        return WaitForBinding(
            kMicrosoftService, kRestoreBindPollTimeoutMs, nullptr);
    }

    V1AvrcpHandoffIpc* ipc_ = nullptr;
    V1AvrcpHandoffState state_;
    std::wstring observer_inf_path_;
    std::wstring instance_id_;
    std::uint64_t generation_ = 0u;
    std::uint32_t restart_count_ = 0u;
    std::wstring published_inf_;
    std::wstring* resident_published_inf_ = nullptr;
};

void PrintUsage() {
    std::wprintf(
        L"Usage: v1_avrcp_handoff_host.exe --candidate-path <dir> "
        L"[--instance-id <exact 0x110E instance id>] [--once]\n"
        L"Resident elevated AVRCP owner handoff host. Waits on the "
        L"NativeLdac handoff/restore request events, drives the bounded "
        L"PnP switch (stage/restart/bind-verify/restore) and reports "
        L"completion through the state file and done events.\n"
        L"--once processes one request and exits (test mode).\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    (void)setvbuf(stdout, nullptr, _IONBF, 0u);
    (void)setvbuf(stderr, nullptr, _IONBF, 0u);

    std::wstring candidate_path;
    std::wstring instance_id;
    bool once = false;
    for (int index = 1; index < argc; ++index) {
        if (std::wcscmp(argv[index], L"--help") == 0 ||
            std::wcscmp(argv[index], L"-h") == 0) {
            PrintUsage();
            return 0;
        }
        if (std::wcscmp(argv[index], L"--candidate-path") == 0 &&
            index + 1 < argc) {
            candidate_path = argv[++index];
            continue;
        }
        if (std::wcscmp(argv[index], L"--instance-id") == 0 &&
            index + 1 < argc) {
            instance_id = argv[++index];
            continue;
        }
        if (std::wcscmp(argv[index], L"--once") == 0) {
            once = true;
            continue;
        }
        std::fwprintf(stderr, L"Unknown option: %ls\n", argv[index]);
        PrintUsage();
        return 2;
    }
    if (candidate_path.empty()) {
        PrintUsage();
        return 2;
    }
    const std::wstring observer_inf_path =
        candidate_path + L"\\NativeLdacAvrcpObserver.inf";
    if (GetFileAttributesW(observer_inf_path.c_str()) ==
        INVALID_FILE_ATTRIBUTES) {
        std::fwprintf(
            stderr, L"Observer candidate INF is missing: %ls\n",
            observer_inf_path.c_str());
        return 2;
    }

    V1AvrcpHandoffIpc ipc(true);
    DWORD error = ERROR_SUCCESS;
    const std::wstring start_message =
        L"handoff host started (candidate " + candidate_path + L")";
    LogHost(start_message.c_str());
    std::wstring resident_published_inf;
    for (;;) {
        bool is_handoff = false;
        const auto wait_result = ipc.WaitForAnyRequest(
            once ? 1u : INFINITE, &error, &is_handoff);
        if (wait_result == V1AvrcpHandoffWaitResult::TimedOut) {
            if (once) {
                return 0;
            }
            continue;
        }
        if (wait_result != V1AvrcpHandoffWaitResult::Ok) {
            const std::wstring message =
                L"request wait failed (Win32 " +
                std::to_wstring(error) + L")";
            LogHost(message.c_str());
            std::fwprintf(
                stderr, L"Handoff request wait failed (Win32 %lu).\n", error);
            return 3;
        }
        if (is_handoff) {
            ipc.ResetHandoffRequest();
        } else {
            ipc.ResetRestoreRequest();
        }
        std::uint64_t generation = 0u;
        std::uint32_t restart_count = 0u;
        error = ERROR_SUCCESS;
        (void)ipc.ReadRequestInfo(&generation, &restart_count, &error);
        if (instance_id.empty()) {
            const DeviceSnapshot target = FindTargetDevice();
            if (target.instance_id.empty()) {
                LogHost(L"target AVRCP PDO not found");
                std::fwprintf(
                    stderr, L"The exact XM5 AVRCP PDO was not found.\n");
                const wchar_t* error_text =
                    is_handoff ? L"target-pdo-not-found"
                               : L"target-pdo-not-found";
                (void)(is_handoff
                           ? ipc.SignalHandoffCompleted(
                                 generation, restart_count, error_text, &error)
                           : ipc.SignalRestoreCompleted(
                                 generation, restart_count, error_text, &error));
                if (once) {
                    return 4;
                }
                continue;
            }
            instance_id = target.instance_id;
        }
        HandoffSessionRunner runner(
            &ipc,
            observer_inf_path,
            instance_id,
            generation,
            restart_count,
            &resident_published_inf);
        const bool ok = is_handoff ? runner.RunHandoff(&error)
                                   : runner.RunRestore(&error);
        if (!ok) {
            const std::wstring message =
                L"handoff session failed (Win32 " +
                std::to_wstring(error) + L")";
            LogHost(message.c_str());
            std::fwprintf(
                stderr, L"Handoff session failed (Win32 %lu).\n", error);
        }
        if (once) {
            return ok ? 0 : 5;
        }
    }
}
