#pragma once

#define NOMINMAX
#include <windows.h>

#include <bthdef.h>

#include <deque>

#include "xm5_acl_event.h"

namespace native_ldac::agent {

class Xm5AclWatcher {
public:
    explicit Xm5AclWatcher(BTH_ADDR target_address)
        : target_address_(target_address) {}
    Xm5AclWatcher(const Xm5AclWatcher&) = delete;
    Xm5AclWatcher& operator=(const Xm5AclWatcher&) = delete;
    ~Xm5AclWatcher();

    bool Start();
    void Stop();

    HANDLE change_event() const { return change_event_; }
    bool TryPop(Xm5AclEvent* event);
    DWORD start_error() const { return start_error_; }

private:
    static LRESULT CALLBACK WindowProcedure(HWND window,
                                             UINT message,
                                             WPARAM wparam,
                                             LPARAM lparam);
    static DWORD WINAPI ThreadEntry(void* context);

    void Push(Xm5AclEvent event);

    HANDLE ready_event_ = nullptr;
    HANDLE change_event_ = nullptr;
    HANDLE thread_ = nullptr;
    DWORD thread_id_ = 0u;
    HWND window_ = nullptr;
    DWORD start_error_ = ERROR_SUCCESS;
    BTH_ADDR target_address_ = 0;
    volatile LONG ready_ = 0;
    SRWLOCK queue_lock_ = SRWLOCK_INIT;
    std::deque<Xm5AclEvent> queue_;
};

}  // namespace native_ldac::agent
