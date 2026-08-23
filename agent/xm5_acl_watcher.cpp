#include "xm5_acl_watcher.h"

#include <BluetoothAPIs.h>
#include <dbt.h>

namespace native_ldac::agent {
namespace {

constexpr wchar_t kWindowClass[] =
    L"NativeLdacV1.Xm5AclWatcherWindow";

bool ResolveTargetAddress(BTH_ADDR* address) {
    if (address == nullptr) {
        return false;
    }
    *address = 0;
    BLUETOOTH_DEVICE_SEARCH_PARAMS search{};
    search.dwSize = sizeof(search);
    search.fReturnAuthenticated = TRUE;
    search.fReturnRemembered = TRUE;
    search.fReturnUnknown = TRUE;
    search.fReturnConnected = TRUE;
    search.fIssueInquiry = FALSE;

    BLUETOOTH_DEVICE_INFO device{};
    device.dwSize = sizeof(device);
    HBLUETOOTH_DEVICE_FIND find =
        BluetoothFindFirstDevice(&search, &device);
    if (find == nullptr) {
        return false;
    }
    BTH_ADDR match = 0;
    unsigned matches = 0;
    BOOL has_device = TRUE;
    while (has_device) {
        if (_wcsicmp(device.szName, L"WH-1000XM5") == 0) {
            match = device.Address.ullLong;
            ++matches;
        }
        device = {};
        device.dwSize = sizeof(device);
        has_device = BluetoothFindNextDevice(find, &device);
    }
    BluetoothFindDeviceClose(find);
    if (matches != 1u) {
        return false;
    }
    *address = match;
    return true;
}

}  // namespace

Xm5AclWatcher::~Xm5AclWatcher() {
    Stop();
}

bool Xm5AclWatcher::Start() {
    if (thread_ != nullptr) {
        SetLastError(ERROR_ALREADY_INITIALIZED);
        return false;
    }
    if (target_address_ == 0 && !ResolveTargetAddress(&target_address_)) {
        start_error_ = ERROR_NOT_FOUND;
        SetLastError(start_error_);
        return false;
    }
    ready_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    change_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ready_event_ == nullptr || change_event_ == nullptr) {
        start_error_ = GetLastError();
        Stop();
        SetLastError(start_error_);
        return false;
    }
    start_error_ = ERROR_SUCCESS;
    InterlockedExchange(&ready_, 0);
    thread_ = CreateThread(nullptr,
                           0,
                           &Xm5AclWatcher::ThreadEntry,
                           this,
                           0,
                           &thread_id_);
    if (thread_ == nullptr) {
        start_error_ = GetLastError();
        Stop();
        SetLastError(start_error_);
        return false;
    }
    const DWORD wait = WaitForSingleObject(ready_event_, 5000u);
    if (wait != WAIT_OBJECT_0 ||
        InterlockedCompareExchange(&ready_, 0, 0) == 0) {
        if (start_error_ == ERROR_SUCCESS) {
            start_error_ = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT
                                                : ERROR_GEN_FAILURE;
        }
        const DWORD error = start_error_;
        Stop();
        SetLastError(error);
        return false;
    }
    return true;
}

void Xm5AclWatcher::Stop() {
    if (thread_ != nullptr) {
        (void)PostThreadMessageW(thread_id_, WM_QUIT, 0, 0);
        (void)WaitForSingleObject(thread_, 5000u);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (ready_event_ != nullptr) {
        CloseHandle(ready_event_);
        ready_event_ = nullptr;
    }
    if (change_event_ != nullptr) {
        CloseHandle(change_event_);
        change_event_ = nullptr;
    }
    AcquireSRWLockExclusive(&queue_lock_);
    queue_.clear();
    ReleaseSRWLockExclusive(&queue_lock_);
    thread_id_ = 0u;
    window_ = nullptr;
    InterlockedExchange(&ready_, 0);
}

bool Xm5AclWatcher::TryPop(Xm5AclEvent* event) {
    if (event == nullptr || change_event_ == nullptr) {
        return false;
    }
    bool found = false;
    AcquireSRWLockExclusive(&queue_lock_);
    if (!queue_.empty()) {
        *event = queue_.front();
        queue_.pop_front();
        found = true;
    }
    if (queue_.empty()) {
        (void)ResetEvent(change_event_);
    }
    ReleaseSRWLockExclusive(&queue_lock_);
    return found;
}

void Xm5AclWatcher::Push(Xm5AclEvent event) {
    if (event == Xm5AclEvent::None || change_event_ == nullptr) {
        return;
    }
    AcquireSRWLockExclusive(&queue_lock_);
    queue_.push_back(event);
    (void)SetEvent(change_event_);
    ReleaseSRWLockExclusive(&queue_lock_);
}

LRESULT CALLBACK Xm5AclWatcher::WindowProcedure(HWND window,
                                                 UINT message,
                                                 WPARAM wparam,
                                                 LPARAM lparam) {
    Xm5AclWatcher* watcher = reinterpret_cast<Xm5AclWatcher*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        watcher = static_cast<Xm5AclWatcher*>(create->lpCreateParams);
        SetWindowLongPtrW(window,
                          GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(watcher));
        return TRUE;
    }
    if (watcher != nullptr && message == WM_DEVICECHANGE) {
        const Xm5AclEvent event = ParseXm5AclDeviceChange(
            watcher->target_address_, wparam, lparam);
        if (event != Xm5AclEvent::None) {
            watcher->Push(event);
            return TRUE;
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

DWORD WINAPI Xm5AclWatcher::ThreadEntry(void* context) {
    auto* watcher = static_cast<Xm5AclWatcher*>(context);
    BLUETOOTH_FIND_RADIO_PARAMS radio_params{
        sizeof(BLUETOOTH_FIND_RADIO_PARAMS)};
    HANDLE radio = nullptr;
    HBLUETOOTH_RADIO_FIND radio_find =
        BluetoothFindFirstRadio(&radio_params, &radio);
    if (radio_find == nullptr || radio == nullptr) {
        watcher->start_error_ = GetLastError();
        (void)SetEvent(watcher->ready_event_);
        return 1;
    }

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = &Xm5AclWatcher::WindowProcedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = kWindowClass;
    const ATOM atom = RegisterClassExW(&window_class);
    if (atom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        watcher->start_error_ = GetLastError();
        CloseHandle(radio);
        BluetoothFindRadioClose(radio_find);
        (void)SetEvent(watcher->ready_event_);
        return 2;
    }

    HWND window = CreateWindowExW(0,
                                  kWindowClass,
                                  L"",
                                  0,
                                  0,
                                  0,
                                  0,
                                  0,
                                  HWND_MESSAGE,
                                  nullptr,
                                  instance,
                                  watcher);
    if (window == nullptr) {
        watcher->start_error_ = GetLastError();
        CloseHandle(radio);
        BluetoothFindRadioClose(radio_find);
        (void)SetEvent(watcher->ready_event_);
        return 3;
    }

    DEV_BROADCAST_HANDLE filter{};
    filter.dbch_size = sizeof(filter);
    filter.dbch_devicetype = DBT_DEVTYP_HANDLE;
    filter.dbch_handle = radio;
    HDEVNOTIFY notification = RegisterDeviceNotificationW(
        window, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (notification == nullptr) {
        watcher->start_error_ = GetLastError();
        (void)DestroyWindow(window);
        CloseHandle(radio);
        BluetoothFindRadioClose(radio_find);
        (void)SetEvent(watcher->ready_event_);
        return 4;
    }

    watcher->window_ = window;
    InterlockedExchange(&watcher->ready_, 1);
    (void)SetEvent(watcher->ready_event_);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    (void)UnregisterDeviceNotification(notification);
    if (IsWindow(window)) {
        (void)DestroyWindow(window);
    }
    watcher->window_ = nullptr;
    CloseHandle(radio);
    BluetoothFindRadioClose(radio_find);
    if (atom != 0) {
        (void)UnregisterClassW(kWindowClass, instance);
    }
    return 0;
}

}  // namespace native_ldac::agent
