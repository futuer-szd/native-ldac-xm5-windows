#pragma once

#define NOMINMAX
#include <windows.h>

#include <string>

namespace native_ldac::agent {

bool IsValidV1DailyInstanceSuffix(const std::wstring& suffix);

class V1DailyInstance {
public:
    V1DailyInstance() = default;
    V1DailyInstance(const V1DailyInstance&) = delete;
    V1DailyInstance& operator=(const V1DailyInstance&) = delete;
    ~V1DailyInstance();

    bool Acquire(const std::wstring& suffix, DWORD* error);
    void Close();

    HANDLE stop_event() const { return stop_event_; }
    bool active() const { return mutex_ != nullptr; }

    static bool SignalStop(const std::wstring& suffix, DWORD* error);

private:
    HANDLE mutex_ = nullptr;
    HANDLE stop_event_ = nullptr;
};

}  // namespace native_ldac::agent
