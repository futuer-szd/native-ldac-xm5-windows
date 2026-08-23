// SPDX-License-Identifier: Apache-2.0
#include "v1_daily_config_ipc.h"

#include <sddl.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace native_ldac::agent {
namespace {

void SetError(DWORD value, DWORD* error) {
    if (error != nullptr) *error = value;
}

bool ReadFileExact(HANDLE pipe, void* buffer, DWORD bytes, HANDLE stop) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr) return false;
    DWORD read = 0u;
    const BOOL started = ReadFile(pipe, buffer, bytes, &read, &overlapped);
    if (!started && GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(overlapped.hEvent);
        return false;
    }
    HANDLE waits[] = {overlapped.hEvent, stop};
    const DWORD wait = WaitForMultipleObjects(2u, waits, FALSE, 2000u);
    bool ok = false;
    if (wait == WAIT_OBJECT_0 && GetOverlappedResult(pipe, &overlapped,
                                                       &read, FALSE)) {
        ok = read == bytes;
    } else {
        CancelIoEx(pipe, &overlapped);
    }
    CloseHandle(overlapped.hEvent);
    return ok;
}

bool WriteFileExact(HANDLE pipe, const void* buffer, DWORD bytes,
                    HANDLE stop) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr) return false;
    DWORD written = 0u;
    const BOOL started = WriteFile(pipe, buffer, bytes, &written, &overlapped);
    if (!started && GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(overlapped.hEvent);
        return false;
    }
    HANDLE waits[] = {overlapped.hEvent, stop};
    const DWORD wait = WaitForMultipleObjects(2u, waits, FALSE, 2000u);
    bool ok = false;
    if (wait == WAIT_OBJECT_0 && GetOverlappedResult(pipe, &overlapped,
                                                       &written, FALSE)) {
        ok = written == bytes;
    } else {
        CancelIoEx(pipe, &overlapped);
    }
    CloseHandle(overlapped.hEvent);
    return ok;
}

bool SameUserAndSessionClient(HANDLE pipe) {
    if (!ImpersonateNamedPipeClient(pipe)) {
        ULONG client_pid = 0u;
        if (!GetNamedPipeClientProcessId(pipe, &client_pid) ||
            client_pid == 0u) return false;
        HANDLE client_process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                             FALSE, client_pid);
        HANDLE client_token = nullptr;
        HANDLE server_token = nullptr;
        bool matches = false;
        if (client_process != nullptr &&
            OpenProcessToken(client_process, TOKEN_QUERY, &client_token) &&
            OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &server_token)) {
            DWORD client_session = MAXDWORD;
            DWORD server_session = MAXDWORD;
            DWORD returned = 0u;
            std::array<std::uint8_t, 256u> client_user{};
            std::array<std::uint8_t, 256u> server_user{};
            if (GetTokenInformation(client_token, TokenSessionId,
                                    &client_session, sizeof(client_session),
                                    &returned) &&
                GetTokenInformation(server_token, TokenSessionId,
                                    &server_session, sizeof(server_session),
                                    &returned) &&
                GetTokenInformation(client_token, TokenUser,
                                    client_user.data(),
                                    static_cast<DWORD>(client_user.size()),
                                    &returned) &&
                GetTokenInformation(server_token, TokenUser,
                                    server_user.data(),
                                    static_cast<DWORD>(server_user.size()),
                                    &returned)) {
                const auto* client = reinterpret_cast<const TOKEN_USER*>(
                    client_user.data());
                const auto* server = reinterpret_cast<const TOKEN_USER*>(
                    server_user.data());
                matches = client_session == server_session &&
                    EqualSid(client->User.Sid, server->User.Sid) != FALSE;
            }
        }
        if (client_token != nullptr) CloseHandle(client_token);
        if (server_token != nullptr) CloseHandle(server_token);
        if (client_process != nullptr) CloseHandle(client_process);
        return matches;
    }
    HANDLE client_token = nullptr;
    HANDLE server_token = nullptr;
    bool matches = false;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE,
                        &client_token) &&
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &server_token)) {
        DWORD client_session = MAXDWORD;
        DWORD server_session = MAXDWORD;
        DWORD returned = 0u;
        std::array<std::uint8_t, 256u> client_user{};
        std::array<std::uint8_t, 256u> server_user{};
        if (GetTokenInformation(client_token, TokenSessionId,
                                &client_session, sizeof(client_session),
                                &returned) &&
            GetTokenInformation(server_token, TokenSessionId,
                                &server_session, sizeof(server_session),
                                &returned) &&
            GetTokenInformation(client_token, TokenUser, client_user.data(),
                                static_cast<DWORD>(client_user.size()),
                                &returned) &&
            GetTokenInformation(server_token, TokenUser, server_user.data(),
                                static_cast<DWORD>(server_user.size()),
                                &returned)) {
            const auto* client = reinterpret_cast<const TOKEN_USER*>(
                client_user.data());
            const auto* server = reinterpret_cast<const TOKEN_USER*>(
                server_user.data());
            matches = client_session == server_session &&
                EqualSid(client->User.Sid, server->User.Sid) != FALSE;
        }
    }
    if (client_token != nullptr) CloseHandle(client_token);
    if (server_token != nullptr) CloseHandle(server_token);
    RevertToSelf();
    return matches;
}

}  // namespace

bool IsV1DailyQuality(std::uint32_t value) {
    return value <= static_cast<std::uint32_t>(V1DailyQuality::Mq);
}

const wchar_t* V1DailyQualityName(V1DailyQuality quality) {
    switch (quality) {
        case V1DailyQuality::Hq: return L"HQ";
        case V1DailyQuality::Sq: return L"SQ";
        case V1DailyQuality::Mq: return L"MQ";
    }
    return L"invalid";
}

bool ParseV1DailyQuality(const std::wstring& value,
                         V1DailyQuality* quality) {
    if (quality == nullptr) return false;
    if (_wcsicmp(value.c_str(), L"hq") == 0) {
        *quality = V1DailyQuality::Hq;
    } else if (_wcsicmp(value.c_str(), L"sq") == 0) {
        *quality = V1DailyQuality::Sq;
    } else if (_wcsicmp(value.c_str(), L"mq") == 0) {
        *quality = V1DailyQuality::Mq;
    } else {
        return false;
    }
    return true;
}

bool ValidateV1DailyConfigRequest(const V1DailyConfigRequest& request) {
    return request.magic == 0x31434C4Eu && request.version == 1u &&
           request.message_type == 1u &&
           request.message_bytes == sizeof(request) && request.revision != 0u &&
           IsV1DailyQuality(request.quality) && request.reserved == 0u;
}

V1DailyConfigServer::~V1DailyConfigServer() { Stop(); }

bool V1DailyConfigServer::Start(const std::wstring& pipe_name,
                                const std::wstring& persistence_path,
                                V1DailyQuality default_quality,
                                std::uint64_t default_revision,
                                DWORD* error) {
    Stop();
    if (pipe_name.empty() || persistence_path.empty() ||
        !IsV1DailyQuality(static_cast<std::uint32_t>(default_quality))) {
        SetError(ERROR_INVALID_PARAMETER, error);
        return false;
    }
    InitializeCriticalSection(&lock_);
    lock_initialized_ = true;
    pipe_name_ = pipe_name;
    persistence_path_ = persistence_path;
    requested_ = {};
    requested_.revision = default_revision;
    requested_.quality = static_cast<std::uint32_t>(default_quality);
    applied_ = requested_;
    pending_ = {};
    pending_available_ = false;
    rejected_count_ = 0u;
    last_error_ = ERROR_SUCCESS;
    DWORD load_error = ERROR_SUCCESS;
    if (!LoadPersisted(&load_error) && load_error != ERROR_FILE_NOT_FOUND) {
        last_error_ = load_error;
    }
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stop_event_ == nullptr) {
        SetError(GetLastError(), error);
        Stop();
        return false;
    }
    thread_ = CreateThread(nullptr, 0u, ThreadMain, this, 0u, nullptr);
    if (thread_ == nullptr) {
        SetError(GetLastError(), error);
        Stop();
        return false;
    }
    const std::wstring full_pipe_name =
        pipe_name_.rfind(L"\\\\.\\pipe\\", 0u) == 0u
            ? pipe_name_
            : L"\\\\.\\pipe\\" + pipe_name_;
    bool ready = false;
    for (unsigned attempt = 0u; attempt < 100u; ++attempt) {
        if (WaitNamedPipeW(full_pipe_name.c_str(), 20u)) {
            ready = true;
            break;
        }
        const DWORD wait_error = GetLastError();
        if (wait_error != ERROR_FILE_NOT_FOUND &&
            wait_error != ERROR_SEM_TIMEOUT) {
            SetError(wait_error, error);
            Stop();
            return false;
        }
        Sleep(20u);
    }
    if (!ready) {
        SetError(WAIT_TIMEOUT, error);
        Stop();
        return false;
    }
    SetError(ERROR_SUCCESS, error);
    return true;
}

void V1DailyConfigServer::Stop() {
    if (stop_event_ != nullptr) SetEvent(stop_event_);
    if (thread_ != nullptr) {
        WaitForSingleObject(thread_, 5000u);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (stop_event_ != nullptr) {
        CloseHandle(stop_event_);
        stop_event_ = nullptr;
    }
    if (lock_initialized_) {
        DeleteCriticalSection(&lock_);
        lock_initialized_ = false;
    }
}

DWORD WINAPI V1DailyConfigServer::ThreadMain(void* context) {
    static_cast<V1DailyConfigServer*>(context)->Run();
    return 0u;
}

void V1DailyConfigServer::Run() {
    const std::wstring full_pipe_name =
        pipe_name_.rfind(L"\\\\.\\pipe\\", 0u) == 0u
            ? pipe_name_
            : L"\\\\.\\pipe\\" + pipe_name_;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;SY)(A;;GA;;;OW)S:(ML;;NW;;;ME)",
            SDDL_REVISION_1, &descriptor, nullptr)) {
        EnterCriticalSection(&lock_);
        last_error_ = GetLastError();
        LeaveCriticalSection(&lock_);
        return;
    }
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.lpSecurityDescriptor = descriptor;
    for (;;) {
        HANDLE pipe = CreateNamedPipeW(
            full_pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
                PIPE_REJECT_REMOTE_CLIENTS,
            1u, sizeof(V1DailyConfigResponse), sizeof(V1DailyConfigRequest),
            1000u, &security);
        if (pipe == INVALID_HANDLE_VALUE) {
            EnterCriticalSection(&lock_);
            last_error_ = GetLastError();
            LeaveCriticalSection(&lock_);
            LocalFree(descriptor);
            return;
        }
        OVERLAPPED connect{};
        connect.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (connect.hEvent == nullptr) {
            CloseHandle(pipe);
            LocalFree(descriptor);
            return;
        }
        const BOOL connected = ConnectNamedPipe(pipe, &connect);
        const DWORD connect_error = connected ? ERROR_SUCCESS : GetLastError();
        if (!connected && connect_error != ERROR_IO_PENDING &&
            connect_error != ERROR_PIPE_CONNECTED) {
            CloseHandle(connect.hEvent);
            CloseHandle(pipe);
            if (WaitForSingleObject(stop_event_, 0u) == WAIT_OBJECT_0) return;
            continue;
        }
        HANDLE waits[] = {connect.hEvent, stop_event_};
        const DWORD wait = (connected || connect_error == ERROR_PIPE_CONNECTED)
            ? WAIT_OBJECT_0
            : WaitForMultipleObjects(2u, waits, FALSE, INFINITE);
        if (!connected && connect_error == ERROR_IO_PENDING &&
            wait == WAIT_OBJECT_0) {
            DWORD transferred = 0u;
            if (!GetOverlappedResult(pipe, &connect, &transferred, FALSE)) {
                CloseHandle(connect.hEvent);
                DisconnectNamedPipe(pipe);
                CloseHandle(pipe);
                continue;
            }
        }
        CloseHandle(connect.hEvent);
        if (wait == WAIT_OBJECT_0 + 1u) {
            CancelIoEx(pipe, nullptr);
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            LocalFree(descriptor);
            return;
        }
        V1DailyConfigRequest request{};
        V1DailyConfigResponse response = {};
        if (SameUserAndSessionClient(pipe) &&
            ReadFileExact(pipe, &request, sizeof(request), stop_event_)) {
            response = Handle(request);
            (void)WriteFileExact(pipe, &response, sizeof(response),
                                 stop_event_);
        }
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        if (WaitForSingleObject(stop_event_, 0u) == WAIT_OBJECT_0) {
            LocalFree(descriptor);
            return;
        }
    }
}

V1DailyConfigResponse V1DailyConfigServer::Handle(
    const V1DailyConfigRequest& request) {
    V1DailyConfigResponse response{};
    response.requested_revision = request.revision;
    EnterCriticalSection(&lock_);
    response.applied_revision = applied_.revision;
    if (!ValidateV1DailyConfigRequest(request)) {
        response.status = static_cast<std::uint32_t>(
            V1DailyConfigStatus::Invalid);
        response.error = ERROR_INVALID_DATA;
        ++rejected_count_;
        last_error_ = response.error;
    } else if (request.revision <= requested_.revision) {
        response.status = static_cast<std::uint32_t>(
            V1DailyConfigStatus::StaleRevision);
        response.error = ERROR_REVISION_MISMATCH;
        ++rejected_count_;
        last_error_ = response.error;
    } else {
        DWORD error = ERROR_SUCCESS;
        if (!Persist(request, &error)) {
            response.status = static_cast<std::uint32_t>(V1DailyConfigStatus::Error);
            response.error = error;
            ++rejected_count_;
            last_error_ = error;
        } else {
            requested_ = request;
            pending_ = request;
            pending_available_ = true;
            response.status = static_cast<std::uint32_t>(
                V1DailyConfigStatus::Accepted);
            response.error = ERROR_SUCCESS;
            last_error_ = ERROR_SUCCESS;
        }
    }
    LeaveCriticalSection(&lock_);
    return response;
}

bool V1DailyConfigServer::LoadPersisted(DWORD* error) {
    HANDLE file = CreateFileW(persistence_path_.c_str(), GENERIC_READ,
                              FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        SetError(GetLastError(), error);
        return false;
    }
    V1DailyConfigRequest request{};
    DWORD read = 0u;
    const bool ok = ReadFile(file, &request, sizeof(request), &read, nullptr) &&
                    read == sizeof(request) && ValidateV1DailyConfigRequest(request);
    CloseHandle(file);
    if (!ok) {
        SetError(ERROR_INVALID_DATA, error);
        return false;
    }
    requested_ = request;
    applied_ = request;
    SetError(ERROR_SUCCESS, error);
    return true;
}

bool V1DailyConfigServer::Persist(const V1DailyConfigRequest& request,
                                  DWORD* error) {
    const std::wstring temporary = persistence_path_ + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0u, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        SetError(GetLastError(), error);
        return false;
    }
    DWORD written = 0u;
    const bool ok = WriteFile(file, &request, sizeof(request), &written, nullptr) &&
                    written == sizeof(request) && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || !MoveFileExW(temporary.c_str(), persistence_path_.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        SetError(GetLastError(), error);
        return false;
    }
    SetError(ERROR_SUCCESS, error);
    return true;
}

bool V1DailyConfigServer::TakeAccepted(V1DailyConfigRequest* request) {
    if (request == nullptr) return false;
    EnterCriticalSection(&lock_);
    if (!pending_available_) {
        LeaveCriticalSection(&lock_);
        return false;
    }
    *request = pending_;
    pending_available_ = false;
    LeaveCriticalSection(&lock_);
    return true;
}

bool V1DailyConfigServer::MarkApplied(std::uint64_t revision, DWORD* error) {
    EnterCriticalSection(&lock_);
    if (revision != requested_.revision) {
        LeaveCriticalSection(&lock_);
        SetError(ERROR_REVISION_MISMATCH, error);
        return false;
    }
    applied_ = requested_;
    LeaveCriticalSection(&lock_);
    SetError(ERROR_SUCCESS, error);
    return true;
}

V1DailyQuality V1DailyConfigServer::requested_quality() const {
    EnterCriticalSection(const_cast<CRITICAL_SECTION*>(&lock_));
    const auto value = static_cast<V1DailyQuality>(requested_.quality);
    LeaveCriticalSection(const_cast<CRITICAL_SECTION*>(&lock_));
    return value;
}

std::uint64_t V1DailyConfigServer::requested_revision() const {
    EnterCriticalSection(const_cast<CRITICAL_SECTION*>(&lock_));
    const auto value = requested_.revision;
    LeaveCriticalSection(const_cast<CRITICAL_SECTION*>(&lock_));
    return value;
}

V1DailyQuality V1DailyConfigServer::applied_quality() const {
    EnterCriticalSection(const_cast<CRITICAL_SECTION*>(&lock_));
    const auto value = static_cast<V1DailyQuality>(applied_.quality);
    LeaveCriticalSection(const_cast<CRITICAL_SECTION*>(&lock_));
    return value;
}

std::uint64_t V1DailyConfigServer::applied_revision() const {
    EnterCriticalSection(const_cast<CRITICAL_SECTION*>(&lock_));
    const auto value = applied_.revision;
    LeaveCriticalSection(const_cast<CRITICAL_SECTION*>(&lock_));
    return value;
}

std::uint32_t V1DailyConfigServer::rejected_count() const {
    EnterCriticalSection(const_cast<CRITICAL_SECTION*>(&lock_));
    const auto value = rejected_count_;
    LeaveCriticalSection(const_cast<CRITICAL_SECTION*>(&lock_));
    return value;
}

DWORD V1DailyConfigServer::last_error() const {
    EnterCriticalSection(const_cast<CRITICAL_SECTION*>(&lock_));
    const auto value = last_error_;
    LeaveCriticalSection(const_cast<CRITICAL_SECTION*>(&lock_));
    return value;
}

}  // namespace native_ldac::agent
