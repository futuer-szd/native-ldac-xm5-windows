#include "v1_avrcp_handoff_ipc.h"

#include <sddl.h>
#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <utility>
#include <vector>

namespace native_ldac::agent {
namespace {

constexpr wchar_t kHandoffRequestName[] =
    L"Global\\NativeLdac.AvrcpHandoffRequest";
constexpr wchar_t kHandoffDoneName[] =
    L"Global\\NativeLdac.AvrcpHandoffDone";
constexpr wchar_t kRestoreRequestName[] =
    L"Global\\NativeLdac.AvrcpRestoreRequest";
constexpr wchar_t kRestoreDoneName[] =
    L"Global\\NativeLdac.AvrcpRestoreDone";
constexpr wchar_t kStateFileName[] = L"avrcp-handoff-state.json";
constexpr wchar_t kStateHandoffPending[] = L"handoff-pending";
constexpr wchar_t kStateObserverActive[] = L"observer-active";
constexpr wchar_t kStateRestorePending[] = L"restore-pending";
constexpr wchar_t kStateMicrosoftHeld[] = L"microsoft-held";

void StoreError(DWORD value, DWORD* error) {
    if (error != nullptr) {
        *error = value;
    }
}

std::wstring MakeEventName(const wchar_t* base,
                           const std::wstring& suffix) {
    if (suffix.empty()) {
        return std::wstring(base);
    }
    return std::wstring(base) + L"-" + suffix;
}

std::wstring MakeStatePath(const std::wstring& suffix) {
    wchar_t buffer[MAX_PATH] = {};
    // The handoff host runs as SYSTEM and the daily host as the logged-in
    // user, whose LOCALAPPDATA differ (systemprofile vs user profile). The
    // state file must live in a shared, machine-wide location so both sides
    // read and write the same file. ProgramData is writable by Users and
    // SYSTEM, and the LogHost directory is already created there.
    if (GetEnvironmentVariableW(L"ProgramData", buffer, MAX_PATH) == 0u) {
        wcscpy_s(buffer, L"C:\\ProgramData");
    }
    std::wstring path(buffer);
    path += L"\\NativeLdac";
    if (!suffix.empty()) {
        path += L"-" + suffix;
    }
    path += L"\\";
    path += kStateFileName;
    return path;
}

void FormatTimestamp(wchar_t* buffer, size_t capacity) {
    SYSTEMTIME local{};
    GetLocalTime(&local);
    (void)swprintf_s(
        buffer,
        capacity,
        L"%04u-%02u-%02uT%02u:%02u:%02u",
        local.wYear,
        local.wMonth,
        local.wDay,
        local.wHour,
        local.wMinute,
        local.wSecond);
}

std::wstring EscapeJson(const std::wstring& text) {
    std::wstring escaped;
    escaped.reserve(text.size());
    for (const wchar_t character : text) {
        if (character == L'\\' || character == L'"') {
            escaped += L'\\';
        }
        escaped += character;
    }
    return escaped;
}

}  // namespace

V1AvrcpHandoffIpc::V1AvrcpHandoffIpc(bool enabled,
                                     std::wstring name_suffix)
    : enabled_(enabled), name_suffix_(std::move(name_suffix)) {
    if (!enabled_) {
        valid_ = true;
        return;
    }
    state_path_ = MakeStatePath(name_suffix_);
    valid_ = !state_path_.empty();
}

V1AvrcpHandoffIpc::~V1AvrcpHandoffIpc() {
    Close();
}

bool V1AvrcpHandoffIpc::Open() {
    if (!enabled_) {
        return true;
    }
    if (handoff_request_ != nullptr && handoff_done_ != nullptr &&
        restore_request_ != nullptr && restore_done_ != nullptr) {
        return true;
    }
    const std::wstring handoff_request_name =
        MakeEventName(kHandoffRequestName, name_suffix_);
    const std::wstring handoff_done_name =
        MakeEventName(kHandoffDoneName, name_suffix_);
    const std::wstring restore_request_name =
        MakeEventName(kRestoreRequestName, name_suffix_);
    const std::wstring restore_done_name =
        MakeEventName(kRestoreDoneName, name_suffix_);

    // Global-named events are created by whichever side starts first (the
    // SYSTEM handoff host or the elevated daily host). The default DACL of
    // the creator would deny the other side, so every creation applies an
    // explicit SDDL granting SYSTEM, Administrators, and Everyone.
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;WD)",
            SDDL_REVISION_1,
            &descriptor,
            nullptr)) {
        security.lpSecurityDescriptor = descriptor;
    }
    handoff_request_ = CreateEventW(
        &security, TRUE, FALSE, handoff_request_name.c_str());
    handoff_done_ = CreateEventW(
        &security, TRUE, FALSE, handoff_done_name.c_str());
    restore_request_ = CreateEventW(
        &security, TRUE, FALSE, restore_request_name.c_str());
    restore_done_ = CreateEventW(
        &security, TRUE, FALSE, restore_done_name.c_str());
    if (descriptor != nullptr) {
        LocalFree(descriptor);
    }
    if (handoff_request_ == nullptr || handoff_done_ == nullptr ||
        restore_request_ == nullptr || restore_done_ == nullptr) {
        Close();
        return false;
    }
    return true;
}

void V1AvrcpHandoffIpc::Close() {
    if (handoff_request_ != nullptr) {
        CloseHandle(handoff_request_);
        handoff_request_ = nullptr;
    }
    if (handoff_done_ != nullptr) {
        CloseHandle(handoff_done_);
        handoff_done_ = nullptr;
    }
    if (restore_request_ != nullptr) {
        CloseHandle(restore_request_);
        restore_request_ = nullptr;
    }
    if (restore_done_ != nullptr) {
        CloseHandle(restore_done_);
        restore_done_ = nullptr;
    }
}

bool V1AvrcpHandoffIpc::WriteState(const wchar_t* state,
                                   std::uint64_t acl_generation,
                                   std::uint32_t restart_count,
                                   const wchar_t* error_text,
                                   bool completed,
                                   DWORD* error) {
    if (!enabled_) {
        StoreError(ERROR_SUCCESS, error);
        return true;
    }
    if (state_path_.empty()) {
        StoreError(ERROR_PATH_NOT_FOUND, error);
        return false;
    }
    const std::wstring parent =
        state_path_.substr(0, state_path_.find_last_of(L'\\'));
    if (CreateDirectoryW(parent.c_str(), nullptr) == FALSE &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        StoreError(GetLastError(), error);
        return false;
    }
    wchar_t timestamp[64] = {};
    FormatTimestamp(timestamp, 64);
    wchar_t completed_text[64] = {};
    if (completed) {
        FormatTimestamp(completed_text, 64);
    }
    const std::wstring state_text =
        std::wstring(L"{\r\n") +
        L"  \"schema_version\": 1,\r\n" +
        L"  \"state\": \"" + EscapeJson(state) + L"\",\r\n" +
        L"  \"generation\": " + std::to_wstring(acl_generation) + L",\r\n" +
        L"  \"restart_count\": " + std::to_wstring(restart_count) + L",\r\n" +
        L"  \"error\": \"" + EscapeJson(error_text) + L"\",\r\n" +
        L"  \"requested_at\": \"" + timestamp + L"\",\r\n" +
        L"  \"completed_at\": \"" + completed_text + L"\"\r\n" +
        L"}\r\n";

    const std::wstring temporary =
        state_path_ + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE file = CreateFileW(
        temporary.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        StoreError(GetLastError(), error);
        return false;
    }
    DWORD written = 0u;
    const BOOL wrote = WriteFile(
        file,
        state_text.c_str(),
        static_cast<DWORD>(state_text.size() * sizeof(wchar_t)),
        &written,
        nullptr);
    const DWORD write_error = wrote ? ERROR_SUCCESS : GetLastError();
    if (wrote) {
        (void)FlushFileBuffers(file);
    }
    CloseHandle(file);
    if (!wrote) {
        (void)DeleteFileW(temporary.c_str());
        StoreError(write_error, error);
        return false;
    }
    if (MoveFileExW(temporary.c_str(),
                    state_path_.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ==
        FALSE) {
        (void)DeleteFileW(temporary.c_str());
        StoreError(GetLastError(), error);
        return false;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1AvrcpHandoffIpc::ReadState(std::wstring* state,
                                  std::uint64_t* generation,
                                  std::uint32_t* restart_count,
                                  std::wstring* error_text,
                                  DWORD* error) const {
    if (state == nullptr || generation == nullptr || restart_count == nullptr ||
        error_text == nullptr || state_path_.empty()) {
        StoreError(ERROR_INVALID_PARAMETER, error);
        return false;
    }
    HANDLE file = CreateFileW(
        state_path_.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        StoreError(GetLastError(), error);
        return false;
    }
    LARGE_INTEGER size{};
    std::wstring content;
    if (GetFileSizeEx(file, &size) != FALSE && size.QuadPart > 0 &&
        size.QuadPart < 1024 * 1024) {
        const size_t characters = static_cast<size_t>(size.QuadPart / 2);
        std::vector<wchar_t> buffer(characters + 1u, L'\0');
        DWORD read_bytes = 0u;
        if (ReadFile(
                file,
                buffer.data(),
                static_cast<DWORD>(characters * sizeof(wchar_t)),
                &read_bytes,
                nullptr) != FALSE) {
            content.assign(buffer.data());
        }
    }
    CloseHandle(file);
    if (content.empty()) {
        StoreError(ERROR_FILE_NOT_FOUND, error);
        return false;
    }
    const size_t state_pos = content.find(L"\"state\": \"");
    if (state_pos == std::wstring::npos) {
        StoreError(ERROR_INVALID_DATA, error);
        return false;
    }
    const size_t value_start = state_pos + 10u;
    const size_t value_end = content.find(L'"', value_start);
    if (value_end == std::wstring::npos) {
        StoreError(ERROR_INVALID_DATA, error);
        return false;
    }
    *state = content.substr(value_start, value_end - value_start);
    const size_t generation_pos = content.find(L"\"generation\": ");
    const size_t restart_pos = content.find(L"\"restart_count\": ");
    const size_t error_pos = content.find(L"\"error\": \"");
    if (generation_pos == std::wstring::npos ||
        restart_pos == std::wstring::npos || error_pos == std::wstring::npos) {
        StoreError(ERROR_INVALID_DATA, error);
        return false;
    }
    wchar_t* generation_end = nullptr;
    const std::uint64_t parsed_generation = _wcstoui64(
        content.c_str() + generation_pos + 14u, &generation_end, 10);
    wchar_t* restart_end = nullptr;
    const unsigned long parsed_restart = wcstoul(
        content.c_str() + restart_pos + 17u, &restart_end, 10);
    const size_t error_start = error_pos + 10u;
    const size_t error_end = content.find(L'"', error_start);
    if (generation_end == content.c_str() + generation_pos + 14u ||
        restart_end == content.c_str() + restart_pos + 17u ||
        parsed_restart > UINT32_MAX || error_end == std::wstring::npos) {
        StoreError(ERROR_INVALID_DATA, error);
        return false;
    }
    *generation = parsed_generation;
    *restart_count = static_cast<std::uint32_t>(parsed_restart);
    *error_text = content.substr(error_start, error_end - error_start);
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1AvrcpHandoffIpc::SignalRequest(bool handoff, DWORD* error) {
    if (!enabled_) {
        StoreError(ERROR_SUCCESS, error);
        return true;
    }
    if (!Open()) {
        StoreError(GetLastError(), error);
        return false;
    }
    const HANDLE request = handoff ? handoff_request_ : restore_request_;
    if (request == nullptr || SetEvent(request) == FALSE) {
        StoreError(GetLastError(), error);
        return false;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

V1AvrcpHandoffWaitResult V1AvrcpHandoffIpc::WaitDone(
    bool handoff,
    DWORD timeout_ms,
    DWORD* error) {
    if (!enabled_) {
        StoreError(ERROR_SUCCESS, error);
        return V1AvrcpHandoffWaitResult::Ok;
    }
    if (!Open()) {
        StoreError(GetLastError(), error);
        return V1AvrcpHandoffWaitResult::Failed;
    }
    const HANDLE done = handoff ? handoff_done_ : restore_done_;
    if (done == nullptr) {
        StoreError(ERROR_INVALID_HANDLE, error);
        return V1AvrcpHandoffWaitResult::Failed;
    }
    const DWORD wait = WaitForSingleObject(done, timeout_ms);
    if (wait == WAIT_OBJECT_0) {
        std::wstring state;
        std::uint64_t generation = 0u;
        std::uint32_t restart_count = 0u;
        std::wstring error_text;
        if (!ReadState(
                &state, &generation, &restart_count, &error_text, error)) {
            return V1AvrcpHandoffWaitResult::Failed;
        }
        const wchar_t* expected =
            handoff ? kStateObserverActive : kStateMicrosoftHeld;
        const std::uint64_t expected_generation = handoff
            ? expected_handoff_generation_
            : expected_restore_generation_;
        const std::uint32_t expected_restart_count = handoff
            ? expected_handoff_restart_count_
            : expected_restore_restart_count_;
        if (state != expected || generation != expected_generation ||
            restart_count != expected_restart_count || !error_text.empty()) {
            StoreError(ERROR_INVALID_STATE, error);
            return V1AvrcpHandoffWaitResult::Failed;
        }
        StoreError(ERROR_SUCCESS, error);
        return V1AvrcpHandoffWaitResult::Ok;
    }
    if (wait == WAIT_TIMEOUT) {
        StoreError(ERROR_TIMEOUT, error);
        return V1AvrcpHandoffWaitResult::TimedOut;
    }
    StoreError(GetLastError(), error);
    return V1AvrcpHandoffWaitResult::Failed;
}

bool V1AvrcpHandoffIpc::RequestHandoff(std::uint64_t acl_generation,
                                       std::uint32_t restart_count,
                                       DWORD* error) {
    if (!enabled_) {
        StoreError(ERROR_SUCCESS, error);
        return true;
    }
    if (!Open() || handoff_done_ == nullptr ||
        ResetEvent(handoff_done_) == FALSE) {
        StoreError(GetLastError(), error);
        return false;
    }
    expected_handoff_generation_ = acl_generation;
    expected_handoff_restart_count_ = restart_count;
    if (!WriteState(kStateHandoffPending,
                    acl_generation,
                    restart_count,
                    L"",
                    false,
                    error)) {
        return false;
    }
    return SignalRequest(true, error);
}

V1AvrcpHandoffWaitResult V1AvrcpHandoffIpc::WaitHandoffDone(
    DWORD timeout_ms,
    DWORD* error) {
    return WaitDone(true, timeout_ms, error);
}

bool V1AvrcpHandoffIpc::RequestRestore(std::uint64_t acl_generation,
                                       std::uint32_t restart_count,
                                       DWORD* error) {
    if (!enabled_) {
        StoreError(ERROR_SUCCESS, error);
        return true;
    }
    if (!Open() || restore_done_ == nullptr ||
        ResetEvent(restore_done_) == FALSE) {
        StoreError(GetLastError(), error);
        return false;
    }
    expected_restore_generation_ = acl_generation;
    expected_restore_restart_count_ = restart_count;
    if (!WriteState(kStateRestorePending,
                    acl_generation,
                    restart_count,
                    L"",
                    false,
                    error)) {
        return false;
    }
    return SignalRequest(false, error);
}

V1AvrcpHandoffWaitResult V1AvrcpHandoffIpc::WaitRestoreDone(
    DWORD timeout_ms,
    DWORD* error) {
    return WaitDone(false, timeout_ms, error);
}

V1AvrcpHandoffWaitResult V1AvrcpHandoffIpc::WaitForHandoffRequest(
    DWORD timeout_ms,
    DWORD* error) {
    if (!enabled_) {
        StoreError(ERROR_SUCCESS, error);
        return V1AvrcpHandoffWaitResult::Ok;
    }
    if (!Open()) {
        StoreError(GetLastError(), error);
        return V1AvrcpHandoffWaitResult::Failed;
    }
    if (handoff_request_ == nullptr) {
        StoreError(ERROR_INVALID_HANDLE, error);
        return V1AvrcpHandoffWaitResult::Failed;
    }
    const DWORD wait = WaitForSingleObject(handoff_request_, timeout_ms);
    if (wait == WAIT_OBJECT_0) {
        StoreError(ERROR_SUCCESS, error);
        return V1AvrcpHandoffWaitResult::Ok;
    }
    if (wait == WAIT_TIMEOUT) {
        StoreError(ERROR_TIMEOUT, error);
        return V1AvrcpHandoffWaitResult::TimedOut;
    }
    StoreError(GetLastError(), error);
    return V1AvrcpHandoffWaitResult::Failed;
}

V1AvrcpHandoffWaitResult V1AvrcpHandoffIpc::WaitForRestoreRequest(
    DWORD timeout_ms,
    DWORD* error) {
    if (!enabled_) {
        StoreError(ERROR_SUCCESS, error);
        return V1AvrcpHandoffWaitResult::Ok;
    }
    if (!Open()) {
        StoreError(GetLastError(), error);
        return V1AvrcpHandoffWaitResult::Failed;
    }
    if (restore_request_ == nullptr) {
        StoreError(ERROR_INVALID_HANDLE, error);
        return V1AvrcpHandoffWaitResult::Failed;
    }
    const DWORD wait = WaitForSingleObject(restore_request_, timeout_ms);
    if (wait == WAIT_OBJECT_0) {
        StoreError(ERROR_SUCCESS, error);
        return V1AvrcpHandoffWaitResult::Ok;
    }
    if (wait == WAIT_TIMEOUT) {
        StoreError(ERROR_TIMEOUT, error);
        return V1AvrcpHandoffWaitResult::TimedOut;
    }
    StoreError(GetLastError(), error);
    return V1AvrcpHandoffWaitResult::Failed;
}

void V1AvrcpHandoffIpc::ResetHandoffRequest() {
    if (handoff_request_ != nullptr) {
        (void)ResetEvent(handoff_request_);
    }
}

void V1AvrcpHandoffIpc::ResetRestoreRequest() {
    if (restore_request_ != nullptr) {
        (void)ResetEvent(restore_request_);
    }
}

V1AvrcpHandoffWaitResult V1AvrcpHandoffIpc::WaitForAnyRequest(
    DWORD timeout_ms,
    DWORD* error,
    bool* is_handoff) {
    if (is_handoff != nullptr) {
        *is_handoff = false;
    }
    if (!enabled_) {
        StoreError(ERROR_SUCCESS, error);
        return V1AvrcpHandoffWaitResult::Ok;
    }
    if (!Open()) {
        StoreError(GetLastError(), error);
        return V1AvrcpHandoffWaitResult::Failed;
    }
    if (handoff_request_ == nullptr || restore_request_ == nullptr) {
        StoreError(ERROR_INVALID_HANDLE, error);
        return V1AvrcpHandoffWaitResult::Failed;
    }
    HANDLE handles[2] = {handoff_request_, restore_request_};
    const DWORD wait = WaitForMultipleObjects(2u, handles, FALSE, timeout_ms);
    if (wait == WAIT_OBJECT_0) {
        if (is_handoff != nullptr) {
            *is_handoff = true;
        }
        StoreError(ERROR_SUCCESS, error);
        return V1AvrcpHandoffWaitResult::Ok;
    }
    if (wait == WAIT_OBJECT_0 + 1u) {
        StoreError(ERROR_SUCCESS, error);
        return V1AvrcpHandoffWaitResult::Ok;
    }
    if (wait == WAIT_TIMEOUT) {
        StoreError(ERROR_TIMEOUT, error);
        return V1AvrcpHandoffWaitResult::TimedOut;
    }
    StoreError(GetLastError(), error);
    return V1AvrcpHandoffWaitResult::Failed;
}

bool V1AvrcpHandoffIpc::ReadRequestInfo(std::uint64_t* generation,
                                        std::uint32_t* restart_count,
                                        DWORD* error) {
    if (generation == nullptr || restart_count == nullptr) {
        StoreError(ERROR_INVALID_PARAMETER, error);
        return false;
    }
    if (!enabled_) {
        *generation = 0u;
        *restart_count = 0u;
        StoreError(ERROR_SUCCESS, error);
        return true;
    }
    HANDLE file = CreateFileW(
        state_path_.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        StoreError(GetLastError(), error);
        return false;
    }
    LARGE_INTEGER size{};
    std::wstring content;
    if (GetFileSizeEx(file, &size) != FALSE && size.QuadPart > 0 &&
        size.QuadPart < 1024 * 1024) {
        std::vector<wchar_t> buffer(
            static_cast<size_t>(size.QuadPart / 2) + 1u, L'\0');
        DWORD read_bytes = 0u;
        if (ReadFile(
                file,
                buffer.data(),
                static_cast<DWORD>(
                    (buffer.size() - 1u) * sizeof(wchar_t)),
                &read_bytes,
                nullptr) != FALSE) {
            content.assign(buffer.data());
        }
    }
    CloseHandle(file);
    if (content.empty()) {
        StoreError(ERROR_FILE_NOT_FOUND, error);
        return false;
    }
    std::uint64_t parsed_generation = 0u;
    std::uint32_t parsed_restart_count = 0u;
    const size_t generation_pos = content.find(L"\"generation\": ");
    if (generation_pos != std::wstring::npos) {
        parsed_generation = _wcstoui64(
            content.c_str() + generation_pos + 14u, nullptr, 10);
    }
    const size_t restart_pos = content.find(L"\"restart_count\": ");
    if (restart_pos != std::wstring::npos) {
        parsed_restart_count = static_cast<std::uint32_t>(
            wcstoul(content.c_str() + restart_pos + 17u, nullptr, 10));
    }
    *generation = parsed_generation;
    *restart_count = parsed_restart_count;
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1AvrcpHandoffIpc::SignalHandoffCompleted(
    std::uint64_t acl_generation,
    std::uint32_t restart_count,
    const wchar_t* error_text,
    DWORD* error) {
    if (!enabled_) {
        StoreError(ERROR_SUCCESS, error);
        return true;
    }
    if (!WriteState(kStateObserverActive,
                    acl_generation,
                    restart_count,
                    error_text,
                    true,
                    error)) {
        return false;
    }
    if (!Open()) {
        StoreError(GetLastError(), error);
        return false;
    }
    if (handoff_done_ == nullptr || SetEvent(handoff_done_) == FALSE) {
        StoreError(GetLastError(), error);
        return false;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1AvrcpHandoffIpc::SignalRestoreCompleted(
    std::uint64_t acl_generation,
    std::uint32_t restart_count,
    const wchar_t* error_text,
    DWORD* error) {
    if (!enabled_) {
        StoreError(ERROR_SUCCESS, error);
        return true;
    }
    if (!WriteState(kStateMicrosoftHeld,
                    acl_generation,
                    restart_count,
                    error_text,
                    true,
                    error)) {
        return false;
    }
    if (!Open()) {
        StoreError(GetLastError(), error);
        return false;
    }
    if (restore_done_ == nullptr || SetEvent(restore_done_) == FALSE) {
        StoreError(GetLastError(), error);
        return false;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

}  // namespace native_ldac::agent
