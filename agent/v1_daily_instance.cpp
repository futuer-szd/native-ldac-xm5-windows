#include "v1_daily_instance.h"

#include <cwctype>

namespace native_ldac::agent {
namespace {

constexpr wchar_t kMutexPrefix[] = L"Local\\NativeLdac.V1.Daily.Mutex.";
constexpr wchar_t kStopPrefix[] = L"Local\\NativeLdac.V1.Daily.Stop.";

void StoreError(DWORD value, DWORD* error) {
    if (error != nullptr) {
        *error = value;
    }
}

std::wstring MakeName(const wchar_t* prefix,
                      const std::wstring& suffix) {
    return std::wstring(prefix) + suffix;
}

}  // namespace

bool IsValidV1DailyInstanceSuffix(const std::wstring& suffix) {
    if (suffix.empty() || suffix.size() > 64u) {
        return false;
    }
    for (const wchar_t character : suffix) {
        if ((character >= L'a' && character <= L'z') ||
            (character >= L'A' && character <= L'Z') ||
            (character >= L'0' && character <= L'9') ||
            character == L'-' || character == L'_' ||
            character == L'.') {
            continue;
        }
        return false;
    }
    return true;
}

V1DailyInstance::~V1DailyInstance() {
    Close();
}

bool V1DailyInstance::Acquire(const std::wstring& suffix, DWORD* error) {
    Close();
    if (!IsValidV1DailyInstanceSuffix(suffix)) {
        StoreError(ERROR_INVALID_NAME, error);
        return false;
    }

    const std::wstring mutex_name = MakeName(kMutexPrefix, suffix);
    mutex_ = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
    if (mutex_ == nullptr) {
        StoreError(GetLastError(), error);
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
        StoreError(ERROR_ALREADY_EXISTS, error);
        return false;
    }

    const std::wstring stop_name = MakeName(kStopPrefix, suffix);
    stop_event_ = CreateEventW(
        nullptr, TRUE, FALSE, stop_name.c_str());
    if (stop_event_ == nullptr ||
        GetLastError() == ERROR_ALREADY_EXISTS) {
        const DWORD create_error = stop_event_ == nullptr
                                       ? GetLastError()
                                       : ERROR_ALREADY_EXISTS;
        Close();
        StoreError(create_error, error);
        return false;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

void V1DailyInstance::Close() {
    if (stop_event_ != nullptr) {
        CloseHandle(stop_event_);
        stop_event_ = nullptr;
    }
    if (mutex_ != nullptr) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
}

bool V1DailyInstance::SignalStop(const std::wstring& suffix,
                                 DWORD* error) {
    if (!IsValidV1DailyInstanceSuffix(suffix)) {
        StoreError(ERROR_INVALID_NAME, error);
        return false;
    }
    const std::wstring stop_name = MakeName(kStopPrefix, suffix);
    HANDLE stop_event = OpenEventW(
        EVENT_MODIFY_STATE, FALSE, stop_name.c_str());
    if (stop_event == nullptr) {
        StoreError(GetLastError(), error);
        return false;
    }
    const bool signaled = SetEvent(stop_event) != FALSE;
    const DWORD signal_error = signaled ? ERROR_SUCCESS : GetLastError();
    CloseHandle(stop_event);
    StoreError(signal_error, error);
    return signaled;
}

}  // namespace native_ldac::agent
