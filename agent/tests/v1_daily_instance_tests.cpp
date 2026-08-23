#include "../v1_daily_instance.h"

#include <cstdio>
#include <string>

namespace {

int Fail(const char* message, DWORD error = ERROR_SUCCESS) {
    std::fprintf(stderr, "%s (Win32 %lu)\n", message, error);
    return 1;
}

}  // namespace

int main() {
    using native_ldac::agent::IsValidV1DailyInstanceSuffix;
    using native_ldac::agent::V1DailyInstance;

    if (!IsValidV1DailyInstanceSuffix(L"default") ||
        !IsValidV1DailyInstanceSuffix(L"ctest-1.alpha_beta") ||
        IsValidV1DailyInstanceSuffix(L"") ||
        IsValidV1DailyInstanceSuffix(L"bad suffix") ||
        IsValidV1DailyInstanceSuffix(L"bad\\suffix") ||
        IsValidV1DailyInstanceSuffix(std::wstring(65u, L'a'))) {
        return Fail("Daily instance suffix validation changed.");
    }

    const std::wstring suffix = L"ctest-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(GetTickCount64());
    V1DailyInstance first;
    DWORD error = ERROR_SUCCESS;
    if (!first.Acquire(suffix, &error)) {
        return Fail("First daily instance did not acquire.", error);
    }
    if (WaitForSingleObject(first.stop_event(), 0u) != WAIT_TIMEOUT) {
        return Fail("Fresh daily stop event was already signaled.");
    }

    V1DailyInstance duplicate;
    error = ERROR_SUCCESS;
    if (duplicate.Acquire(suffix, &error) ||
        error != ERROR_ALREADY_EXISTS) {
        return Fail("Duplicate daily instance was not rejected.", error);
    }

    error = ERROR_SUCCESS;
    if (!V1DailyInstance::SignalStop(suffix, &error)) {
        return Fail("Daily stop signal failed.", error);
    }
    if (WaitForSingleObject(first.stop_event(), 1000u) != WAIT_OBJECT_0) {
        return Fail("Daily stop signal did not wake the owner.");
    }

    first.Close();
    error = ERROR_SUCCESS;
    if (V1DailyInstance::SignalStop(suffix, &error) ||
        error != ERROR_FILE_NOT_FOUND) {
        return Fail("Missing daily instance accepted a stop signal.", error);
    }

    std::printf("V1 daily instance tests passed.\n");
    return 0;
}
