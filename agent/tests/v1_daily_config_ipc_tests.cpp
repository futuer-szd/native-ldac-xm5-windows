#include "../v1_daily_config_ipc.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

int Fail(const char* message, DWORD error = ERROR_SUCCESS) {
    std::fprintf(stderr, "%s (Win32 %lu)\n", message, error);
    return 1;
}

bool Transact(const std::wstring& pipe_name,
              const native_ldac::agent::V1DailyConfigRequest& request,
              native_ldac::agent::V1DailyConfigResponse* response,
              DWORD* error) {
    const std::wstring path = L"\\\\.\\pipe\\" + pipe_name;
    if (!WaitNamedPipeW(path.c_str(), 2000u)) {
        *error = GetLastError();
        return false;
    }
    HANDLE pipe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              0u, nullptr, OPEN_EXISTING, 0u, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return false;
    }
    DWORD written = 0u;
    DWORD read = 0u;
    const bool ok = WriteFile(pipe, &request, sizeof(request), &written,
                              nullptr) && written == sizeof(request) &&
        ReadFile(pipe, response, sizeof(*response), &read, nullptr) &&
        read == sizeof(*response);
    if (!ok) *error = GetLastError();
    CloseHandle(pipe);
    return ok;
}

}  // namespace

int wmain() {
    using native_ldac::agent::V1DailyConfigRequest;
    using native_ldac::agent::V1DailyConfigResponse;
    using native_ldac::agent::V1DailyConfigServer;
    using native_ldac::agent::V1DailyConfigStatus;
    using native_ldac::agent::V1DailyQuality;

    V1DailyConfigRequest invalid{};
    invalid.revision = 1u;
    invalid.quality = 99u;
    if (native_ldac::agent::ValidateV1DailyConfigRequest(invalid)) {
        return Fail("Invalid quality passed validation.");
    }

    const std::wstring suffix = std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(GetTickCount64());
    const std::wstring pipe_name = L"NativeLdac.V1.Config.Test." + suffix;
    const auto persistence = std::filesystem::temp_directory_path() /
        (std::wstring(L"NativeLdac-V1-config-test-") + suffix + L".bin");
    DeleteFileW(persistence.wstring().c_str());

    DWORD error = ERROR_SUCCESS;
    V1DailyConfigServer server;
    if (!server.Start(pipe_name, persistence.wstring(), V1DailyQuality::Hq,
                      0u, &error)) {
        return Fail("Could not start config server.", error);
    }
    V1DailyConfigRequest request{};
    request.revision = 1u;
    request.quality = static_cast<std::uint32_t>(V1DailyQuality::Sq);
    V1DailyConfigResponse response{};
    if (!Transact(pipe_name, request, &response, &error) ||
        response.status != static_cast<std::uint32_t>(
            V1DailyConfigStatus::Accepted)) {
        return Fail("Valid quality request was not accepted.",
                    server.last_error() != ERROR_SUCCESS
                        ? server.last_error() : error);
    }
    V1DailyConfigRequest accepted{};
    if (!server.TakeAccepted(&accepted) || accepted.revision != 1u ||
        accepted.quality != request.quality ||
        !server.MarkApplied(1u, &error)) {
        return Fail("Accepted request did not reach the safe boundary.", error);
    }
    request.revision = 1u;
    if (!Transact(pipe_name, request, &response, &error) ||
        response.status != static_cast<std::uint32_t>(
            V1DailyConfigStatus::StaleRevision) ||
        server.rejected_count() != 1u) {
        return Fail("Stale revision was not rejected.", error);
    }
    server.Stop();

    V1DailyConfigServer restored;
    if (!restored.Start(pipe_name, persistence.wstring(), V1DailyQuality::Hq,
                        0u, &error) ||
        restored.requested_revision() != 1u ||
        restored.applied_revision() != 1u ||
        restored.applied_quality() != V1DailyQuality::Sq) {
        return Fail("Last-known-good quality did not persist.", error);
    }
    restored.Stop();
    DeleteFileW(persistence.wstring().c_str());
    return 0;
}
