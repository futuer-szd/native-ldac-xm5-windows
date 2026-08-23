#include "../v1_avrcp_handoff_ipc.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace {

int Fail(const char* message, DWORD error = ERROR_SUCCESS) {
    std::fprintf(stderr, "%s (Win32 %lu)\n", message, error);
    return 1;
}

std::wstring UniqueSuffix() {
    return L"ctest-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(GetTickCount64());
}

std::wstring StatePath(const std::wstring& suffix) {
    wchar_t buffer[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"ProgramData", buffer, MAX_PATH) == 0u) {
        wcscpy_s(buffer, L"C:\\ProgramData");
    }
    if (buffer[0] == L'\0') {
        return std::wstring();
    }
    std::wstring path(buffer);
    path += L"\\NativeLdac";
    if (!suffix.empty()) {
        path += L"-" + suffix;
    }
    path += L"\\avrcp-handoff-state.json";
    return path;
}

bool ReadStateFile(const std::wstring& path,
                   std::wstring* state,
                   std::wstring* content) {
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    std::wstring text;
    if (GetFileSizeEx(file, &size) != FALSE && size.QuadPart > 0 &&
        size.QuadPart < 1024 * 1024) {
        std::wstring buffer(
            static_cast<size_t>(size.QuadPart / 2) + 1u, L'\0');
        DWORD read_bytes = 0u;
        if (ReadFile(
                file,
                &buffer[0],
                static_cast<DWORD>(
                    (buffer.size() - 1u) * sizeof(wchar_t)),
                &read_bytes,
                nullptr) != FALSE) {
            text.assign(buffer.c_str());
        }
    }
    CloseHandle(file);
    if (text.empty()) {
        return false;
    }
    if (content != nullptr) {
        *content = text;
    }
    const size_t state_pos = text.find(L"\"state\": \"");
    if (state_pos == std::wstring::npos) {
        return false;
    }
    const size_t value_start = state_pos + 10u;
    const size_t value_end = text.find(L'"', value_start);
    if (value_end == std::wstring::npos) {
        return false;
    }
    *state = text.substr(value_start, value_end - value_start);
    return true;
}

bool RewriteState(const std::wstring& path,
                  const wchar_t* state,
                  const std::wstring& original) {
    // Simulate the handoff host completing the switch by rewriting the
    // state file with the new owner state and signaling the done event.
    std::wstring updated = original;
    const size_t state_pos = updated.find(L"\"state\": \"");
    if (state_pos == std::wstring::npos) {
        return false;
    }
    const size_t value_start = state_pos + 10u;
    const size_t value_end = updated.find(L'"', value_start);
    if (value_end == std::wstring::npos) {
        return false;
    }
    updated.replace(value_start, value_end - value_start, state);
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0u;
    const bool ok = WriteFile(
        file,
        updated.c_str(),
        static_cast<DWORD>(updated.size() * sizeof(wchar_t)),
        &written,
        nullptr) != FALSE;
    CloseHandle(file);
    return ok;
}

bool ReplaceStateField(const std::wstring& path,
                       const std::wstring& original,
                       const std::wstring& field,
                       const std::wstring& value,
                       bool quoted) {
    std::wstring updated = original;
    const std::wstring prefix = L"\"" + field + L"\": " +
        (quoted ? L"\"" : L"");
    const size_t field_pos = updated.find(prefix);
    if (field_pos == std::wstring::npos) {
        return false;
    }
    const size_t value_start = field_pos + prefix.size();
    const size_t value_end = quoted
        ? updated.find(L'"', value_start)
        : updated.find_first_of(L",\r\n", value_start);
    if (value_end == std::wstring::npos) {
        return false;
    }
    updated.replace(value_start, value_end - value_start, value);
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0u;
    const bool ok = WriteFile(
        file,
        updated.c_str(),
        static_cast<DWORD>(updated.size() * sizeof(wchar_t)),
        &written,
        nullptr) != FALSE;
    CloseHandle(file);
    return ok;
}

}  // namespace

int main() {
    using native_ldac::agent::V1AvrcpHandoffIpc;
    using native_ldac::agent::V1AvrcpHandoffWaitResult;

    wchar_t local_app_data[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH) ==
        0u) {
        return Fail("LOCALAPPDATA is not set.");
    }

    // Disabled IPC must be a complete no-op success.
    {
        V1AvrcpHandoffIpc disabled(false);
        DWORD error = ERROR_SUCCESS;
        if (disabled.enabled() || !disabled.valid() ||
            !disabled.RequestHandoff(1u, 0u, &error) ||
            disabled.WaitHandoffDone(10u, &error) !=
                V1AvrcpHandoffWaitResult::Ok ||
            !disabled.RequestRestore(1u, 0u, &error) ||
            disabled.WaitRestoreDone(10u, &error) !=
                V1AvrcpHandoffWaitResult::Ok) {
            return Fail("Disabled handoff IPC was not a no-op.", error);
        }
    }

    const std::wstring suffix = UniqueSuffix();
    const std::wstring state_path = StatePath(suffix);
    if (state_path.empty()) {
        return Fail("State path is empty.");
    }

    // Handoff request writes the pending state and signals the request event.
    {
        V1AvrcpHandoffIpc ipc(true, suffix);
        DWORD error = ERROR_SUCCESS;
        if (!ipc.valid() ||
            !ipc.RequestHandoff(7u, 2u, &error)) {
            return Fail("Handoff request failed.", error);
        }
        std::wstring state;
        std::wstring content;
        if (!ReadStateFile(state_path, &state, &content) ||
            state != L"handoff-pending") {
            return Fail("Handoff pending state was not written.");
        }
        if (content.find(L"\"schema_version\": 1") == std::wstring::npos ||
            content.find(L"\"generation\": 7") == std::wstring::npos ||
            content.find(L"\"restart_count\": 2") == std::wstring::npos) {
            return Fail("Handoff state file schema fields are missing.");
        }
        HANDLE request = OpenEventW(
            SYNCHRONIZE | EVENT_MODIFY_STATE,
            FALSE,
            (L"Global\\NativeLdac.AvrcpHandoffRequest-" + suffix).c_str());
        if (request == nullptr ||
            WaitForSingleObject(request, 0u) != WAIT_OBJECT_0) {
            if (request != nullptr) {
                CloseHandle(request);
            }
            return Fail("Handoff request event was not signaled.");
        }
        if (request != nullptr) {
            CloseHandle(request);
        }
        // No .tmp residue after the atomic write.
        if (GetFileAttributesW((state_path + L".tmp").c_str()) !=
            INVALID_FILE_ATTRIBUTES) {
            return Fail("Atomic state write left a .tmp residue.");
        }
    }

    // Waiting for the done event times out when nobody completes.
    {
        V1AvrcpHandoffIpc ipc(true, suffix);
        DWORD error = ERROR_SUCCESS;
        if (!ipc.RequestHandoff(7u, 2u, &error)) {
            return Fail("Handoff request failed.", error);
        }
        if (ipc.WaitHandoffDone(50u, &error) !=
            V1AvrcpHandoffWaitResult::TimedOut) {
            return Fail("Handoff done wait did not time out.", error);
        }
    }

    // A stale completion signal from an older request is reset before the
    // new generation is published.
    {
        V1AvrcpHandoffIpc ipc(true, suffix);
        DWORD error = ERROR_SUCCESS;
        HANDLE done = CreateEventW(
            nullptr,
            TRUE,
            TRUE,
            (L"Global\\NativeLdac.AvrcpHandoffDone-" + suffix).c_str());
        if (done == nullptr || !ipc.RequestHandoff(9u, 0u, &error)) {
            if (done != nullptr) {
                CloseHandle(done);
            }
            return Fail("Stale handoff completion setup failed.", error);
        }
        CloseHandle(done);
        if (ipc.WaitHandoffDone(50u, &error) !=
            V1AvrcpHandoffWaitResult::TimedOut) {
            return Fail("A stale handoff completion escaped reset.", error);
        }
    }

    // A completed handoff (done event + observer-active state) is accepted.
    {
        V1AvrcpHandoffIpc ipc(true, suffix);
        DWORD error = ERROR_SUCCESS;
        if (!ipc.RequestHandoff(7u, 2u, &error)) {
            return Fail("Handoff request failed.", error);
        }
        std::wstring state;
        std::wstring content;
        if (!ReadStateFile(state_path, &state, &content)) {
            return Fail("Handoff state file is missing.");
        }
        HANDLE done = OpenEventW(
            SYNCHRONIZE | EVENT_MODIFY_STATE,
            FALSE,
            (L"Global\\NativeLdac.AvrcpHandoffDone-" + suffix).c_str());
        if (done == nullptr ||
            !RewriteState(state_path, L"observer-active", content)) {
            if (done != nullptr) {
                CloseHandle(done);
            }
            return Fail("Handoff completion setup failed.", GetLastError());
        }
        if (SetEvent(done) == FALSE) {
            CloseHandle(done);
            return Fail("Handoff done signal failed.", GetLastError());
        }
        CloseHandle(done);
        if (ipc.WaitHandoffDone(1000u, &error) !=
            V1AvrcpHandoffWaitResult::Ok) {
            return Fail("Handoff done wait was not accepted.", error);
        }
    }

    // Restore request and completion follow the same contract.
    {
        V1AvrcpHandoffIpc ipc(true, suffix);
        DWORD error = ERROR_SUCCESS;
        if (!ipc.RequestRestore(7u, 2u, &error)) {
            return Fail("Restore request failed.", error);
        }
        std::wstring state;
        std::wstring content;
        if (!ReadStateFile(state_path, &state, &content) ||
            state != L"restore-pending") {
            return Fail("Restore pending state was not written.");
        }
        HANDLE done = OpenEventW(
            SYNCHRONIZE | EVENT_MODIFY_STATE,
            FALSE,
            (L"Global\\NativeLdac.AvrcpRestoreDone-" + suffix).c_str());
        if (done == nullptr ||
            !RewriteState(state_path, L"microsoft-held", content)) {
            if (done != nullptr) {
                CloseHandle(done);
            }
            return Fail("Restore completion setup failed.", GetLastError());
        }
        if (SetEvent(done) == FALSE) {
            CloseHandle(done);
            return Fail("Restore done signal failed.", GetLastError());
        }
        CloseHandle(done);
        if (ipc.WaitRestoreDone(1000u, &error) !=
            V1AvrcpHandoffWaitResult::Ok) {
            return Fail("Restore done wait was not accepted.", error);
        }
    }

    // A done event with the wrong state must be rejected.
    {
        V1AvrcpHandoffIpc ipc(true, suffix);
        DWORD error = ERROR_SUCCESS;
        if (!ipc.RequestHandoff(8u, 0u, &error)) {
            return Fail("Handoff request failed.", error);
        }
        std::wstring state;
        std::wstring content;
        if (!ReadStateFile(state_path, &state, &content)) {
            return Fail("Handoff state file is missing.");
        }
        HANDLE done = OpenEventW(
            SYNCHRONIZE | EVENT_MODIFY_STATE,
            FALSE,
            (L"Global\\NativeLdac.AvrcpHandoffDone-" + suffix).c_str());
        if (done == nullptr ||
            !RewriteState(state_path, L"microsoft-held", content)) {
            if (done != nullptr) {
                CloseHandle(done);
            }
            return Fail("Wrong-state completion setup failed.", GetLastError());
        }
        if (SetEvent(done) == FALSE) {
            CloseHandle(done);
            return Fail("Wrong-state done signal failed.", GetLastError());
        }
        CloseHandle(done);
        if (ipc.WaitHandoffDone(1000u, &error) !=
            V1AvrcpHandoffWaitResult::Failed) {
            return Fail("Wrong handoff state was accepted.", error);
        }
    }

    // Completion identity and error text are part of the transaction. A
    // different ACL generation or a host-reported failure is never success.
    {
        V1AvrcpHandoffIpc ipc(true, suffix);
        DWORD error = ERROR_SUCCESS;
        if (!ipc.RequestHandoff(10u, 3u, &error)) {
            return Fail("Handoff request failed.", error);
        }
        std::wstring state;
        std::wstring content;
        if (!ReadStateFile(state_path, &state, &content) ||
            !RewriteState(state_path, L"observer-active", content) ||
            !ReadStateFile(state_path, &state, &content) ||
            !ReplaceStateField(
                state_path, content, L"generation", L"9", false)) {
            return Fail("Mismatched-generation completion setup failed.");
        }
        HANDLE done = OpenEventW(
            EVENT_MODIFY_STATE,
            FALSE,
            (L"Global\\NativeLdac.AvrcpHandoffDone-" + suffix).c_str());
        if (done == nullptr || SetEvent(done) == FALSE) {
            if (done != nullptr) {
                CloseHandle(done);
            }
            return Fail("Mismatched-generation completion signal failed.");
        }
        CloseHandle(done);
        if (ipc.WaitHandoffDone(1000u, &error) !=
            V1AvrcpHandoffWaitResult::Failed) {
            return Fail("A stale ACL generation completion was accepted.");
        }

        if (!ipc.RequestHandoff(10u, 3u, &error) ||
            !ReadStateFile(state_path, &state, &content) ||
            !RewriteState(state_path, L"observer-active", content) ||
            !ReadStateFile(state_path, &state, &content) ||
            !ReplaceStateField(
                state_path, content, L"error", L"bind-failed", true)) {
            return Fail("Error completion setup failed.", error);
        }
        done = OpenEventW(
            EVENT_MODIFY_STATE,
            FALSE,
            (L"Global\\NativeLdac.AvrcpHandoffDone-" + suffix).c_str());
        if (done == nullptr || SetEvent(done) == FALSE) {
            if (done != nullptr) {
                CloseHandle(done);
            }
            return Fail("Error completion signal failed.");
        }
        CloseHandle(done);
        if (ipc.WaitHandoffDone(1000u, &error) !=
            V1AvrcpHandoffWaitResult::Failed) {
            return Fail("A host-reported handoff failure was accepted.");
        }
    }

    std::fprintf(stderr, "V1 AVRCP handoff IPC tests passed.\n");
    return 0;
}
