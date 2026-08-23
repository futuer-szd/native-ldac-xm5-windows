#define UNICODE
#define _UNICODE
#define NOMINMAX

#include "runtime_support.h"

#include <windows.h>
#include <dbt.h>
#include <ks.h>
#include <ksmedia.h>
#include <shellapi.h>
#include <bcrypt.h>

#include "ldac_native_ioctl.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kDefaultMutexName[] = L"Local\\NativeLdacAgent.Singleton";
constexpr wchar_t kDefaultStopEventName[] = L"Local\\NativeLdacAgent.Stop";
constexpr DWORD kGracefulStopTimeoutMs = 30000;
constexpr DWORD kStableSessionMs = 30000;
constexpr DWORD kInstalledDeviceWatchdogMs = 15000;
constexpr DWORD kInstalledTransportRetryMs = 2000;
constexpr DWORD kInstalledDeviceSettleMs = 3000;
constexpr DWORD kInstalledAudioWatchdogMs = 500;
constexpr DWORD kInstalledConfigPollMs = 500;
constexpr std::uint64_t kAgentLogMaxBytes = 1024ull * 1024ull;
constexpr std::uint64_t kProbeLogMaxBytes = 8ull * 1024ull * 1024ull;
constexpr unsigned int kLogBackupCount = 3;
const GUID kAudioCategory = {STATIC_KSCATEGORY_AUDIO};

struct Options {
    std::wstring probe_path;
    std::wstring config_path;
    std::wstring quality = L"hq";
    std::wstring channel_mode = L"stereo";
    unsigned int sample_rate = 48000;
    unsigned int bits_per_sample = 16;
    std::wstring log_path;
    std::wstring probe_log_path;
    std::wstring state_path;
    std::wstring instance_suffix;
    bool stop_existing = false;
    bool once = false;
    bool installed_mode = false;
    bool direct_pdo_trial = false;
    bool wait_for_xm5 = false;
    DWORD run_for_ms = 0;
};

class Logger {
public:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    ~Logger() = default;

    bool Open(const std::wstring& path) {
        return sink_.Open(path, kAgentLogMaxBytes, kLogBackupCount);
    }

    void Write(const std::wstring& message) {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t prefix[64]{};
        const int prefix_length = swprintf_s(
            prefix,
            L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
            static_cast<unsigned int>(now.wYear),
            static_cast<unsigned int>(now.wMonth),
            static_cast<unsigned int>(now.wDay),
            static_cast<unsigned int>(now.wHour),
            static_cast<unsigned int>(now.wMinute),
            static_cast<unsigned int>(now.wSecond),
            static_cast<unsigned int>(now.wMilliseconds));
        if (prefix_length <= 0) {
            return;
        }

        std::wstring line(prefix, static_cast<size_t>(prefix_length));
        line += message;
        line += L"\r\n";
        const int byte_count = WideCharToMultiByte(CP_UTF8,
                                                   0,
                                                   line.c_str(),
                                                   static_cast<int>(line.size()),
                                                   nullptr,
                                                   0,
                                                   nullptr,
                                                   nullptr);
        if (byte_count <= 0) {
            return;
        }
        std::string utf8(static_cast<size_t>(byte_count), '\0');
        (void)WideCharToMultiByte(CP_UTF8,
                                 0,
                                 line.c_str(),
                                 static_cast<int>(line.size()),
                                 utf8.data(),
                                 byte_count,
                                 nullptr,
                                 nullptr);
        (void)sink_.Write(utf8.data(), utf8.size());
        sink_.Flush();
    }

private:
    native_ldac::agent::RotatingLog sink_;
};

struct ProbeProcess {
    HANDLE process = nullptr;
    HANDLE output_thread = nullptr;
    DWORD process_id = 0;
};

struct ProbeOutputContext {
    HANDLE read_pipe = nullptr;
    std::wstring log_path;
};

DWORD WINAPI ProbeOutputThread(void* raw_context) {
    std::unique_ptr<ProbeOutputContext> context(
        static_cast<ProbeOutputContext*>(raw_context));
    native_ldac::agent::RotatingLog output;
    const bool log_opened = output.Open(context->log_path,
                                        kProbeLogMaxBytes,
                                        kLogBackupCount);
    char buffer[4096]{};
    for (;;) {
        DWORD bytes_read = 0;
        if (!ReadFile(context->read_pipe,
                      buffer,
                      static_cast<DWORD>(sizeof(buffer)),
                      &bytes_read,
                      nullptr)) {
            const DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA ||
                error == ERROR_OPERATION_ABORTED) {
                break;
            }
            break;
        }
        if (bytes_read == 0) {
            break;
        }
        if (log_opened) {
            (void)output.Write(buffer, bytes_read);
            output.Flush();
        }
    }
    CloseHandle(context->read_pipe);
    return log_opened ? 0 : 1;
}

void FinishProbeOutput(ProbeProcess* probe) {
    if (probe == nullptr || probe->output_thread == nullptr) {
        return;
    }
    DWORD wait = WaitForSingleObject(probe->output_thread, 5000);
    if (wait == WAIT_TIMEOUT) {
        (void)CancelSynchronousIo(probe->output_thread);
        wait = WaitForSingleObject(probe->output_thread, 5000);
    }
    (void)wait;
    CloseHandle(probe->output_thread);
    probe->output_thread = nullptr;
}

class SessionEndMonitor {
public:
    SessionEndMonitor() = default;
    SessionEndMonitor(const SessionEndMonitor&) = delete;
    SessionEndMonitor& operator=(const SessionEndMonitor&) = delete;

    ~SessionEndMonitor() { Stop(); }

    bool Start(HANDLE stop_event, HANDLE device_change_event) {
        if (thread_ != nullptr || stop_event == nullptr ||
            device_change_event == nullptr) {
            return false;
        }
        stop_event_ = stop_event;
        device_change_event_ = device_change_event;
        ready_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (ready_event_ == nullptr) {
            return false;
        }
        thread_ = CreateThread(nullptr,
                               0,
                               &SessionEndMonitor::ThreadEntry,
                               this,
                               0,
                               &thread_id_);
        if (thread_ == nullptr) {
            CloseHandle(ready_event_);
            ready_event_ = nullptr;
            return false;
        }
        const DWORD wait = WaitForSingleObject(ready_event_, 5000);
        if (wait != WAIT_OBJECT_0 ||
            InterlockedCompareExchange(&window_ready_, 0, 0) == 0) {
            Stop();
            return false;
        }
        return true;
    }

    void Stop() {
        if (thread_ != nullptr) {
            (void)PostThreadMessageW(thread_id_, WM_QUIT, 0, 0);
            (void)WaitForSingleObject(thread_, 5000);
            CloseHandle(thread_);
            thread_ = nullptr;
        }
        if (ready_event_ != nullptr) {
            CloseHandle(ready_event_);
            ready_event_ = nullptr;
        }
        thread_id_ = 0;
        stop_event_ = nullptr;
        device_change_event_ = nullptr;
        InterlockedExchange(&window_ready_, 0);
        InterlockedExchange(&device_notifications_ready_, 0);
    }

    bool DeviceNotificationsReady() const {
        return InterlockedCompareExchange(
                   const_cast<volatile LONG*>(&device_notifications_ready_),
                   0,
                   0) != 0;
    }

private:
    static LRESULT CALLBACK WindowProcedure(HWND window,
                                             UINT message,
                                             WPARAM wparam,
                                             LPARAM lparam) {
        SessionEndMonitor* monitor = reinterpret_cast<SessionEndMonitor*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const CREATESTRUCTW* create =
                reinterpret_cast<const CREATESTRUCTW*>(lparam);
            monitor = static_cast<SessionEndMonitor*>(
                create->lpCreateParams);
            SetWindowLongPtrW(window,
                              GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(monitor));
        }
        if (monitor != nullptr) {
            if (message == WM_QUERYENDSESSION) {
                (void)SetEvent(monitor->stop_event_);
                return TRUE;
            }
            if (message == WM_ENDSESSION && wparam != FALSE) {
                (void)SetEvent(monitor->stop_event_);
                return 0;
            }
            if (message == WM_DEVICECHANGE &&
                (wparam == DBT_DEVICEARRIVAL ||
                 wparam == DBT_DEVICEREMOVECOMPLETE ||
                 wparam == DBT_DEVNODES_CHANGED ||
                 wparam == DBT_DEVICEQUERYREMOVEFAILED)) {
                (void)SetEvent(monitor->device_change_event_);
                return TRUE;
            }
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    static DWORD WINAPI ThreadEntry(void* context) {
        auto* monitor = static_cast<SessionEndMonitor*>(context);
        constexpr wchar_t class_name[] =
            L"NativeLdacAgent.SessionEndWindow";
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = &SessionEndMonitor::WindowProcedure;
        window_class.hInstance = instance;
        window_class.lpszClassName = class_name;
        const ATOM atom = RegisterClassExW(&window_class);
        if (atom == 0) {
            (void)SetEvent(monitor->ready_event_);
            return 1;
        }

        HWND window = CreateWindowExW(0,
                                      class_name,
                                      L"Native LDAC Agent",
                                      WS_OVERLAPPED,
                                      CW_USEDEFAULT,
                                      CW_USEDEFAULT,
                                      CW_USEDEFAULT,
                                      CW_USEDEFAULT,
                                      nullptr,
                                      nullptr,
                                      instance,
                                      monitor);
        if (window == nullptr) {
            (void)UnregisterClassW(class_name, instance);
            (void)SetEvent(monitor->ready_event_);
            return 2;
        }
        DEV_BROADCAST_DEVICEINTERFACE_W transport_filter{};
        transport_filter.dbcc_size = sizeof(transport_filter);
        transport_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        transport_filter.dbcc_classguid =
            GUID_DEVINTERFACE_LDAC_NATIVE_TRANSPORT;
        monitor->transport_notification_ = RegisterDeviceNotificationW(
            window,
            &transport_filter,
            DEVICE_NOTIFY_WINDOW_HANDLE);
        DEV_BROADCAST_DEVICEINTERFACE_W audio_filter{};
        audio_filter.dbcc_size = sizeof(audio_filter);
        audio_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        audio_filter.dbcc_classguid = kAudioCategory;
        monitor->audio_notification_ = RegisterDeviceNotificationW(
            window,
            &audio_filter,
            DEVICE_NOTIFY_WINDOW_HANDLE);
        if (monitor->transport_notification_ != nullptr &&
            monitor->audio_notification_ != nullptr) {
            InterlockedExchange(&monitor->device_notifications_ready_, 1);
        }
        monitor->window_ = window;
        InterlockedExchange(&monitor->window_ready_, 1);
        (void)SetEvent(monitor->ready_event_);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (monitor->audio_notification_ != nullptr) {
            (void)UnregisterDeviceNotification(monitor->audio_notification_);
            monitor->audio_notification_ = nullptr;
        }
        if (monitor->transport_notification_ != nullptr) {
            (void)UnregisterDeviceNotification(
                monitor->transport_notification_);
            monitor->transport_notification_ = nullptr;
        }
        if (IsWindow(window)) {
            (void)DestroyWindow(window);
        }
        monitor->window_ = nullptr;
        (void)UnregisterClassW(class_name, instance);
        return 0;
    }

    HANDLE stop_event_ = nullptr;
    HANDLE device_change_event_ = nullptr;
    HANDLE ready_event_ = nullptr;
    HANDLE thread_ = nullptr;
    DWORD thread_id_ = 0;
    HWND window_ = nullptr;
    HDEVNOTIFY transport_notification_ = nullptr;
    HDEVNOTIFY audio_notification_ = nullptr;
    volatile LONG window_ready_ = 0;
    volatile LONG device_notifications_ready_ = 0;
};

std::wstring FormatWin32Error(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        reinterpret_cast<wchar_t*>(&buffer),
        0,
        nullptr);
    if (length == 0 || buffer == nullptr) {
        return L"Win32 " + std::to_wstring(error);
    }
    std::wstring message(buffer, length);
    LocalFree(buffer);
    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' ||
            message.back() == L' ')) {
        message.pop_back();
    }
    return L"Win32 " + std::to_wstring(error) + L": " + message;
}

std::wstring GetModulePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return std::wstring();
    }
    return std::wstring(buffer.data(), length);
}

std::wstring GetEnvironmentValue(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return std::wstring();
    }
    std::vector<wchar_t> buffer(required);
    const DWORD length = GetEnvironmentVariableW(
        name, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return std::wstring();
    }
    return std::wstring(buffer.data(), length);
}

bool IsQuality(const std::wstring& value) {
    return value == L"mq" || value == L"sq" || value == L"hq" ||
           value == L"auto";
}

bool PathsEqual(const std::filesystem::path& left,
                const std::filesystem::path& right) {
    std::error_code left_error;
    std::error_code right_error;
    const std::filesystem::path canonical_left =
        std::filesystem::weakly_canonical(left, left_error);
    const std::filesystem::path canonical_right =
        std::filesystem::weakly_canonical(right, right_error);
    if (left_error || right_error) {
        return false;
    }
    return _wcsicmp(canonical_left.c_str(), canonical_right.c_str()) == 0;
}

bool OpenRegularFile(const std::wstring& path, HANDLE* file) {
    if (file == nullptr) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    *file = CreateFileW(path.c_str(),
                        GENERIC_READ,
                        FILE_SHARE_READ,
                        nullptr,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                        nullptr);
    if (*file == INVALID_HANDLE_VALUE) {
        *file = nullptr;
        return false;
    }
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!GetFileInformationByHandleEx(*file,
                                      FileAttributeTagInfo,
                                      &tag,
                                      sizeof(tag))) {
        const DWORD error = GetLastError();
        CloseHandle(*file);
        *file = nullptr;
        SetLastError(error);
        return false;
    }
    if ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        CloseHandle(*file);
        *file = nullptr;
        SetLastError(ERROR_ACCESS_DENIED);
        return false;
    }
    return true;
}

int HexNibble(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool ReadExpectedSha256(const std::wstring& path,
                        UCHAR expected[32]) {
    HANDLE file = nullptr;
    if (!OpenRegularFile(path, &file)) return false;
    char text[80]{};
    DWORD bytes_read = 0;
    const BOOL read = ReadFile(file,
                               text,
                               static_cast<DWORD>(sizeof(text)),
                               &bytes_read,
                               nullptr);
    CloseHandle(file);
    if (!read) return false;
    while (bytes_read != 0 &&
           (text[bytes_read - 1] == '\r' ||
            text[bytes_read - 1] == '\n' ||
            text[bytes_read - 1] == ' ' ||
            text[bytes_read - 1] == '\t')) {
        --bytes_read;
    }
    if (bytes_read != 64) {
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }
    for (size_t index = 0; index < 32; ++index) {
        const int high = HexNibble(text[index * 2]);
        const int low = HexNibble(text[index * 2 + 1]);
        if (high < 0 || low < 0) {
            SetLastError(ERROR_INVALID_DATA);
            return false;
        }
        expected[index] = static_cast<UCHAR>((high << 4) | low);
    }
    return true;
}

bool ComputeFileSha256(const std::wstring& path, UCHAR digest[32]) {
    HANDLE file = nullptr;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    ULONG object_length = 0;
    ULONG hash_length = 0;
    ULONG property_bytes = 0;
    NTSTATUS status;
    bool success = false;

    if (!OpenRegularFile(path, &file)) return false;
    status = BCryptOpenAlgorithmProvider(&algorithm,
                                         BCRYPT_SHA256_ALGORITHM,
                                         nullptr,
                                         0);
    if (!BCRYPT_SUCCESS(status)) goto cleanup;
    status = BCryptGetProperty(algorithm,
                               BCRYPT_OBJECT_LENGTH,
                               reinterpret_cast<PUCHAR>(&object_length),
                               sizeof(object_length),
                               &property_bytes,
                               0);
    if (!BCRYPT_SUCCESS(status) || object_length == 0) goto cleanup;
    status = BCryptGetProperty(algorithm,
                               BCRYPT_HASH_LENGTH,
                               reinterpret_cast<PUCHAR>(&hash_length),
                               sizeof(hash_length),
                               &property_bytes,
                               0);
    if (!BCRYPT_SUCCESS(status) || hash_length != 32) goto cleanup;
    {
        std::vector<UCHAR> hash_object(object_length);
        status = BCryptCreateHash(algorithm,
                                  &hash,
                                  hash_object.data(),
                                  object_length,
                                  nullptr,
                                  0,
                                  0);
        if (!BCRYPT_SUCCESS(status)) goto cleanup;
        UCHAR buffer[64 * 1024]{};
        for (;;) {
            DWORD bytes_read = 0;
            if (!ReadFile(file,
                          buffer,
                          static_cast<DWORD>(sizeof(buffer)),
                          &bytes_read,
                          nullptr)) {
                goto cleanup;
            }
            if (bytes_read == 0) break;
            status = BCryptHashData(hash, buffer, bytes_read, 0);
            if (!BCRYPT_SUCCESS(status)) goto cleanup;
        }
        status = BCryptFinishHash(hash, digest, 32, 0);
        success = BCRYPT_SUCCESS(status);
    }

cleanup:
    if (hash != nullptr) BCryptDestroyHash(hash);
    if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (!success) SetLastError(ERROR_INVALID_DATA);
    return success;
}

bool VerifyInstalledMediaExecutable(
    const std::filesystem::path& module_directory,
    const std::wstring& executable_path,
    const wchar_t* hash_file_name) {
    UCHAR expected[32]{};
    UCHAR actual[32]{};
    if (!ReadExpectedSha256(
            (module_directory / hash_file_name).wstring(),
            expected) ||
        !ComputeFileSha256(executable_path, actual)) {
        return false;
    }
    UCHAR difference = 0;
    for (size_t index = 0; index < 32; ++index) {
        difference |= expected[index] ^ actual[index];
    }
    if (difference != 0) SetLastError(ERROR_INVALID_DATA);
    return difference == 0;
}

bool ParseDword(const wchar_t* text, DWORD* value) {
    if (text == nullptr || value == nullptr || text[0] == L'\0') {
        return false;
    }
    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(text, &end, 10);
    if (end == text || end == nullptr || *end != L'\0' ||
        parsed > MAXDWORD) {
        return false;
    }
    *value = static_cast<DWORD>(parsed);
    return true;
}

bool ParseOptions(int argc, wchar_t** argv, Options* options) {
    if (options == nullptr) {
        return false;
    }
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument(argv[index]);
        if (argument == L"--stop") {
            options->stop_existing = true;
        } else if (argument == L"--installed") {
            // The former Direct-PDO login mode is retired. Keep the token
            // fail-closed so an old scheduled task cannot silently select it.
            return false;
        } else if (argument == L"--installed-legacy") {
            options->installed_mode = true;
        } else if (argument == L"--direct-pdo-trial") {
            options->direct_pdo_trial = true;
        } else if (argument == L"--wait-for-xm5") {
            options->wait_for_xm5 = true;
        } else if (argument == L"--once") {
            options->once = true;
        } else if (argument == L"--probe") {
            if (index + 1 >= argc) {
                return false;
            }
            options->probe_path = argv[++index];
        } else if (argument == L"--test-config") {
            if (index + 1 >= argc) {
                return false;
            }
            options->config_path = argv[++index];
        } else if (argument == L"--quality") {
            if (index + 1 >= argc || !IsQuality(argv[index + 1])) {
                return false;
            }
            options->quality = argv[++index];
        } else if (argument == L"--channel-mode") {
            if (index + 1 >= argc ||
                (_wcsicmp(argv[index + 1], L"stereo") != 0 &&
                 _wcsicmp(argv[index + 1], L"dual") != 0 &&
                 _wcsicmp(argv[index + 1], L"mono") != 0)) {
                return false;
            }
            options->channel_mode = argv[++index];
        } else if (argument == L"--sample-rate") {
            DWORD parsed = 0;
            if (index + 1 >= argc ||
                !ParseDword(argv[index + 1], &parsed) ||
                (parsed != 44100u && parsed != 48000u &&
                 parsed != 88200u && parsed != 96000u)) {
                return false;
            }
            options->sample_rate = parsed;
            ++index;
        } else if (argument == L"--bits") {
            DWORD parsed = 0;
            if (index + 1 >= argc ||
                !ParseDword(argv[index + 1], &parsed) ||
                (parsed != 16u && parsed != 24u)) {
                return false;
            }
            options->bits_per_sample = parsed;
            ++index;
        } else if (argument == L"--log") {
            if (index + 1 >= argc) {
                return false;
            }
            options->log_path = argv[++index];
        } else if (argument == L"--probe-log") {
            if (index + 1 >= argc) {
                return false;
            }
            options->probe_log_path = argv[++index];
        } else if (argument == L"--state") {
            if (index + 1 >= argc) {
                return false;
            }
            options->state_path = argv[++index];
        } else if (argument == L"--run-for-ms") {
            if (index + 1 >= argc ||
                !ParseDword(argv[index + 1], &options->run_for_ms)) {
                return false;
            }
            ++index;
        } else if (argument == L"--instance-suffix") {
            if (index + 1 >= argc || argv[index + 1][0] == L'\0') {
                return false;
            }
            options->instance_suffix = argv[++index];
        } else {
            return false;
        }
    }
    if (options->installed_mode &&
        (!options->probe_path.empty() || options->once ||
         options->run_for_ms != 0 || !options->instance_suffix.empty() ||
         !options->log_path.empty() || !options->probe_log_path.empty() ||
         !options->state_path.empty() || !options->config_path.empty())) {
        return false;
    }
    if (options->direct_pdo_trial &&
        (options->installed_mode || !options->probe_path.empty() ||
         options->once || options->run_for_ms == 0 ||
         options->instance_suffix.empty() || options->log_path.empty() ||
         options->probe_log_path.empty() || options->state_path.empty() ||
         !options->config_path.empty())) {
        return false;
    }
    if (!options->config_path.empty() &&
        options->instance_suffix.rfind(L"ctest", 0) != 0) {
        return false;
    }
    return true;
}

bool UsesDirectPdo(const Options& options) {
    return options.direct_pdo_trial;
}

std::wstring AppendInstanceSuffix(const wchar_t* base,
                                  const std::wstring& suffix) {
    std::wstring result(base);
    if (!suffix.empty()) {
        result += L".";
        result += suffix;
    }
    return result;
}

std::wstring QuoteCommandLineArgument(const std::wstring& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }
    if (argument.find_first_of(L" \t\"") == std::wstring::npos) {
        return argument;
    }

    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

DWORD DeadlineTimeout(ULONGLONG deadline) {
    if (deadline == 0) {
        return INFINITE;
    }
    const ULONGLONG now = GetTickCount64();
    if (now >= deadline) {
        return 0;
    }
    return static_cast<DWORD>(
        std::min<ULONGLONG>(deadline - now, MAXDWORD - 1ull));
}

bool StartProbe(const Options& options,
                const std::wstring& stop_event_name,
                HANDLE child_job,
                ProbeProcess* probe) {
    if (child_job == nullptr || probe == nullptr) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    std::wstring command_line = QuoteCommandLineArgument(options.probe_path);
    if (!UsesDirectPdo(options)) {
        command_line += L" --play-endpoint";
    }
    command_line += L" --quality " +
                    QuoteCommandLineArgument(options.quality) +
                    L" --channel-mode " +
                    QuoteCommandLineArgument(options.channel_mode);
    if (!UsesDirectPdo(options)) {
        command_line += L" --sample-rate " +
                        std::to_wstring(options.sample_rate) +
                        L" --bits " +
                        std::to_wstring(options.bits_per_sample) +
                        L" --open-attempts 1";
    }
    command_line += L" --stop-event " +
                    QuoteCommandLineArgument(stop_event_name);
    std::vector<wchar_t> mutable_command(command_line.begin(),
                                         command_line.end());
    mutable_command.push_back(L'\0');

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE output_read = nullptr;
    HANDLE output_write = nullptr;
    if (!CreatePipe(&output_read, &output_write, &security, 0)) {
        return false;
    }
    if (!SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0)) {
        const DWORD pipe_error = GetLastError();
        CloseHandle(output_write);
        CloseHandle(output_read);
        SetLastError(pipe_error);
        return false;
    }

    HANDLE input = CreateFileW(L"NUL",
                               GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &security,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);
    if (input == INVALID_HANDLE_VALUE) {
        const DWORD input_error = GetLastError();
        CloseHandle(output_write);
        CloseHandle(output_read);
        SetLastError(input_error);
        return false;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = input;
    startup.hStdOutput = output_write;
    startup.hStdError = output_write;

    const std::filesystem::path probe_path(options.probe_path);
    const std::wstring working_directory = probe_path.parent_path().wstring();
    PROCESS_INFORMATION process_information{};
    const BOOL created = CreateProcessW(
        options.probe_path.c_str(),
        mutable_command.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
        nullptr,
        working_directory.empty() ? nullptr : working_directory.c_str(),
        &startup,
        &process_information);
    const DWORD create_error = created ? ERROR_SUCCESS : GetLastError();
    CloseHandle(output_write);
    CloseHandle(input);
    if (!created) {
        CloseHandle(output_read);
        SetLastError(create_error);
        return false;
    }

    DWORD failure = ERROR_SUCCESS;
    if (!AssignProcessToJobObject(child_job,
                                  process_information.hProcess)) {
        failure = GetLastError();
    }

    HANDLE output_thread = nullptr;
    if (failure == ERROR_SUCCESS) {
        auto context = std::unique_ptr<ProbeOutputContext>(
            new (std::nothrow) ProbeOutputContext());
        if (!context) {
            failure = ERROR_NOT_ENOUGH_MEMORY;
        } else {
            context->read_pipe = output_read;
            context->log_path = options.probe_log_path;
            output_thread = CreateThread(nullptr,
                                         0,
                                         &ProbeOutputThread,
                                         context.get(),
                                         0,
                                         nullptr);
            if (output_thread == nullptr) {
                failure = GetLastError();
            } else {
                (void)context.release();
            }
        }
    }

    if (failure == ERROR_SUCCESS &&
        ResumeThread(process_information.hThread) ==
            static_cast<DWORD>(-1)) {
        failure = GetLastError();
    }

    if (failure != ERROR_SUCCESS) {
        (void)TerminateProcess(process_information.hProcess,
                               0xE0000002u);
        (void)WaitForSingleObject(process_information.hProcess, 5000);
        CloseHandle(process_information.hThread);
        CloseHandle(process_information.hProcess);
        if (output_thread != nullptr) {
            ProbeProcess failed_probe;
            failed_probe.output_thread = output_thread;
            FinishProbeOutput(&failed_probe);
        } else {
            CloseHandle(output_read);
        }
        SetLastError(failure);
        return false;
    }

    CloseHandle(process_information.hThread);
    probe->process = process_information.hProcess;
    probe->output_thread = output_thread;
    probe->process_id = process_information.dwProcessId;
    return true;
}

bool StopProbeGracefully(HANDLE process,
                         HANDLE probe_stop_event,
                         Logger& logger,
                         DWORD* exit_code) {
    if (!SetEvent(probe_stop_event)) {
        logger.Write(L"Could not signal the probe stop event: " +
                     FormatWin32Error(GetLastError()));
    } else {
        logger.Write(L"Requested a graceful probe stop.");
    }

    DWORD wait = WaitForSingleObject(process, kGracefulStopTimeoutMs);
    if (wait == WAIT_TIMEOUT) {
        logger.Write(L"Probe did not stop within 30 seconds; terminating it "
                     L"as a last resort.");
        (void)TerminateProcess(process, 0xE0000001u);
        wait = WaitForSingleObject(process, 5000);
    }
    if (wait != WAIT_OBJECT_0) {
        logger.Write(L"Could not observe probe termination: " +
                     FormatWin32Error(GetLastError()));
        return false;
    }

    DWORD observed_exit_code = 0;
    if (!GetExitCodeProcess(process, &observed_exit_code)) {
        logger.Write(L"Could not query probe exit code: " +
                     FormatWin32Error(GetLastError()));
        return false;
    }
    if (exit_code != nullptr) {
        *exit_code = observed_exit_code;
    }
    return true;
}

int RunAgent(const Options& parsed_options) {
    Options options = parsed_options;
    const std::wstring module_path = GetModulePath();
    if (module_path.empty()) {
        return 20;
    }
    const std::filesystem::path module_directory =
        std::filesystem::path(module_path).parent_path();
    const std::wstring program_files = GetEnvironmentValue(L"ProgramFiles");
    const bool protected_install = !program_files.empty() && PathsEqual(
        module_directory,
        std::filesystem::path(program_files) / L"NativeLdac" / L"bin");
    if (!options.stop_existing) {
        if (options.installed_mode && !protected_install) {
            return 21;
        }
        if (!options.installed_mode && !options.direct_pdo_trial &&
            protected_install) {
            return 21;
        }
        if (options.direct_pdo_trial && protected_install) {
            return 21;
        }
    }
    if (!options.stop_existing && protected_install &&
        (!options.probe_path.empty() || options.once ||
         options.run_for_ms != 0 || !options.instance_suffix.empty() ||
         !options.log_path.empty() || !options.probe_log_path.empty() ||
         !options.state_path.empty() || !options.config_path.empty())) {
        return 21;
    }
    if (options.probe_path.empty()) {
        options.probe_path =
            (module_directory /
             (UsesDirectPdo(options) ? L"ldac_direct_engine.exe"
                                     : L"transport_probe.exe"))
                .wstring();
    } else {
        std::error_code error;
        options.probe_path =
            std::filesystem::absolute(options.probe_path, error).wstring();
        if (error) {
            return 21;
        }
    }

    const std::wstring stop_event_name = AppendInstanceSuffix(
        kDefaultStopEventName, options.instance_suffix);
    if (options.stop_existing) {
        HANDLE stop_event = OpenEventW(EVENT_MODIFY_STATE,
                                       FALSE,
                                       stop_event_name.c_str());
        if (stop_event == nullptr) {
            return 3;
        }
        const BOOL signaled = SetEvent(stop_event);
        CloseHandle(stop_event);
        return signaled ? 0 : 4;
    }

    if (GetFileAttributesW(options.probe_path.c_str()) ==
        INVALID_FILE_ATTRIBUTES) {
        return 22;
    }
    if (protected_install &&
        !VerifyInstalledMediaExecutable(
            module_directory,
            options.probe_path,
            UsesDirectPdo(options) ? L"ldac_direct_engine.sha256"
                                   : L"transport_probe.sha256")) {
        return 23;
    }

    if (options.log_path.empty()) {
        std::wstring local_app_data = GetEnvironmentValue(L"LOCALAPPDATA");
        if (local_app_data.empty()) {
            local_app_data = module_directory.wstring();
        }
        options.log_path =
            (std::filesystem::path(local_app_data) / L"NativeLdac" /
             L"logs" / L"agent.log")
                .wstring();
    }
    if (options.probe_log_path.empty()) {
        options.probe_log_path =
            (std::filesystem::path(options.log_path).parent_path() /
             L"probe.log")
                .wstring();
    }
    if (options.state_path.empty()) {
        options.state_path =
            (std::filesystem::path(options.log_path).parent_path() /
             L"state.json")
                .wstring();
    }
    std::wstring config_path = options.config_path;
    if (config_path.empty()) {
        config_path =
            (std::filesystem::path(options.state_path)
                 .parent_path()
                 .parent_path() /
             L"config.json")
                .wstring();
    } else {
        std::error_code error;
        config_path = std::filesystem::absolute(config_path, error).wstring();
        if (error) {
            return 21;
        }
    }
    const bool config_mode = options.installed_mode ||
                             !options.config_path.empty();
    const std::wstring startup_quality = options.quality;
    const std::wstring startup_channel_mode = options.channel_mode;
    const unsigned int startup_sample_rate = options.sample_rate;
    const unsigned int startup_bits_per_sample = options.bits_per_sample;

    const std::wstring mutex_name = AppendInstanceSuffix(
        kDefaultMutexName, options.instance_suffix);
    HANDLE singleton = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
    if (singleton == nullptr) {
        return 23;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(singleton);
        return 10;
    }

    HANDLE agent_stop_event = CreateEventW(
        nullptr, TRUE, FALSE, stop_event_name.c_str());
    if (agent_stop_event == nullptr) {
        CloseHandle(singleton);
        return 24;
    }
    HANDLE device_change_event = CreateEventW(
        nullptr, TRUE, FALSE, nullptr);
    if (device_change_event == nullptr) {
        CloseHandle(agent_stop_event);
        CloseHandle(singleton);
        return 24;
    }

    Logger logger;
    if (!logger.Open(options.log_path)) {
        CloseHandle(device_change_event);
        CloseHandle(agent_stop_event);
        CloseHandle(singleton);
        return 25;
    }
    logger.Write(L"Native LDAC agent started. Probe=" + options.probe_path +
                 L", quality=" + options.quality + L", channel_mode=" +
                 options.channel_mode + L", format=" +
                 std::to_wstring(options.sample_rate) + L" Hz/" +
                 std::to_wstring(options.bits_per_sample) + L"-bit, probe log=" +
                 options.probe_log_path + L".");

    SessionEndMonitor session_end_monitor;
    if (!session_end_monitor.Start(agent_stop_event,
                                   device_change_event)) {
        logger.Write(L"Could not create the hidden session-end window: " +
                     FormatWin32Error(GetLastError()));
        CloseHandle(device_change_event);
        CloseHandle(agent_stop_event);
        CloseHandle(singleton);
        return 28;
    }
    logger.Write(session_end_monitor.DeviceNotificationsReady()
                     ? L"Transport device notifications are active; idle "
                       L"state uses a 15-second safety watchdog."
                     : L"Transport device notification registration was "
                       L"unavailable; idle state uses the 15-second safety "
                       L"watchdog.");

    HANDLE child_job = native_ldac::agent::CreateKillOnCloseJob();
    if (child_job == nullptr) {
        logger.Write(L"Could not create the probe containment job: " +
                     FormatWin32Error(GetLastError()));
        session_end_monitor.Stop();
        CloseHandle(device_change_event);
        CloseHandle(agent_stop_event);
        CloseHandle(singleton);
        return 29;
    }

    native_ldac::agent::AgentConfig current_config;
    current_config.quality = startup_quality;
    current_config.channel_mode = startup_channel_mode;
    current_config.sample_rate = startup_sample_rate;
    current_config.bits_per_sample = startup_bits_per_sample;
    native_ldac::agent::StateSnapshot state_snapshot;
    state_snapshot.quality = options.quality;
    state_snapshot.agent_pid = GetCurrentProcessId();
    auto publish_state = [&](const wchar_t* state,
                             DWORD probe_pid,
                             unsigned int generation,
                             DWORD last_probe_exit_code,
                             DWORD retry_delay_ms) {
        state_snapshot.state = state;
        state_snapshot.probe_pid = probe_pid;
        state_snapshot.generation = generation;
        state_snapshot.last_probe_exit_code = last_probe_exit_code;
        state_snapshot.retry_delay_ms = retry_delay_ms;
        state_snapshot.config_enabled = current_config.enabled;
        state_snapshot.config_revision = current_config.revision;
        if (!native_ldac::agent::WriteStateAtomically(options.state_path,
                                                       state_snapshot)) {
            logger.Write(L"Could not update the atomic state file: " +
                         options.state_path + L".");
        }
    };
    publish_state(L"starting", 0, 0, 0, 0);

    const ULONGLONG run_deadline = options.run_for_ms == 0
                                       ? 0
                                       : GetTickCount64() +
                                             options.run_for_ms;
    unsigned int failure_count = 0;
    unsigned int generation = 0;
    DWORD last_probe_exit_code = 0;
    native_ldac::agent::Xm5ConnectionState last_xm5_state =
        native_ldac::agent::Xm5ConnectionState::QueryFailed;
    native_ldac::agent::LdacTransportState last_transport_state =
        native_ldac::agent::LdacTransportState::QueryFailed;
    native_ldac::agent::NativePcmRunState last_pcm_run_state =
        native_ldac::agent::NativePcmRunState::QueryFailed;
    bool xm5_state_initialized = false;
    bool transport_state_initialized = false;
    bool pcm_state_initialized = false;
    bool xm5_disconnect_observed = false;
    native_ldac::agent::LegacyReconnectGate legacy_reconnect_gate;
    bool recovery_error_initialized = false;
    DWORD last_recovery_error = ERROR_SUCCESS;
    bool config_error_active = false;
    int result = 0;

    auto read_installed_config = [&](native_ldac::agent::AgentConfig* config,
                                     DWORD* error) {
        config->enabled = true;
        config->quality = startup_quality;
        config->channel_mode = startup_channel_mode;
        config->sample_rate = startup_sample_rate;
        config->bits_per_sample = startup_bits_per_sample;
        config->revision = 0;
        const native_ldac::agent::ConfigReadResult read_result =
            native_ldac::agent::ReadAgentConfig(config_path, config, error);
        return read_result == native_ldac::agent::ConfigReadResult::Missing
                   ? native_ldac::agent::ConfigReadResult::Loaded
                   : read_result;
    };

    for (;;) {
        if (WaitForSingleObject(agent_stop_event, 0) == WAIT_OBJECT_0 ||
            DeadlineTimeout(run_deadline) == 0) {
            logger.Write(L"Agent stop requested while idle.");
            publish_state(L"stopping",
                          0,
                          generation,
                          last_probe_exit_code,
                          0);
            break;
        }

        if (config_mode) {
            native_ldac::agent::AgentConfig observed_config;
            DWORD config_error = ERROR_SUCCESS;
            if (read_installed_config(&observed_config, &config_error) !=
                native_ldac::agent::ConfigReadResult::Loaded) {
                if (!config_error_active) {
                    logger.Write(L"Agent config is invalid or unreadable: " +
                                 FormatWin32Error(config_error) + L". Media "
                                 L"probe will remain stopped.");
                    config_error_active = true;
                }
                current_config.enabled = false;
                state_snapshot.config_enabled = false;
                publish_state(L"config_error",
                              0,
                              generation,
                              last_probe_exit_code,
                              kInstalledConfigPollMs);
                if (WaitForSingleObject(agent_stop_event,
                                        kInstalledConfigPollMs) ==
                        WAIT_OBJECT_0 ||
                    DeadlineTimeout(run_deadline) == 0) {
                    logger.Write(L"Agent stop requested while waiting for a "
                                 L"valid config.");
                    publish_state(L"stopping",
                                  0,
                                  generation,
                                  last_probe_exit_code,
                                  0);
                    break;
                }
                continue;
            }
            if (config_error_active) {
                logger.Write(L"Agent config became valid again.");
                config_error_active = false;
            }
            if (observed_config.revision != current_config.revision ||
                observed_config.enabled != current_config.enabled ||
                observed_config.quality != current_config.quality ||
                observed_config.channel_mode != current_config.channel_mode ||
                observed_config.sample_rate != current_config.sample_rate ||
                observed_config.bits_per_sample !=
                    current_config.bits_per_sample) {
                logger.Write(L"Applying config revision " +
                             std::to_wstring(observed_config.revision) +
                             L": enabled=" +
                             std::wstring(observed_config.enabled ? L"true"
                                                                  : L"false") +
                             L", quality=" + observed_config.quality +
                             L", channel_mode=" +
                             observed_config.channel_mode + L", format=" +
                             std::to_wstring(observed_config.sample_rate) +
                             L" Hz/" +
                             std::to_wstring(observed_config.bits_per_sample) +
                             L"-bit.");
            }
            current_config = observed_config;
            options.quality = current_config.quality;
            options.channel_mode = current_config.channel_mode;
            options.sample_rate = current_config.sample_rate;
            options.bits_per_sample = current_config.bits_per_sample;
            state_snapshot.quality = current_config.quality;
            if (!current_config.enabled) {
                publish_state(L"disabled",
                              0,
                              generation,
                              last_probe_exit_code,
                              kInstalledConfigPollMs);
                if (WaitForSingleObject(agent_stop_event,
                                        kInstalledConfigPollMs) ==
                        WAIT_OBJECT_0 ||
                    DeadlineTimeout(run_deadline) == 0) {
                    logger.Write(L"Agent stop requested while disabled by "
                                 L"config.");
                    publish_state(L"stopping",
                                  0,
                                  generation,
                                  last_probe_exit_code,
                                  0);
                    break;
                }
                continue;
            }

        }

        if (UsesDirectPdo(options) || options.wait_for_xm5) {
            DWORD query_error = ERROR_SUCCESS;
            const native_ldac::agent::Xm5ConnectionState xm5_state =
                native_ldac::agent::QueryXm5Connection(&query_error);
            DWORD transport_query_error = ERROR_SUCCESS;
            native_ldac::agent::DirectPdoTransportInfo direct_transport_info;
            const native_ldac::agent::LdacTransportState transport_state =
                UsesDirectPdo(options)
                    ? native_ldac::agent::QueryDirectPdoTransport(
                          &direct_transport_info,
                          &transport_query_error)
                    : (xm5_state ==
                               native_ldac::agent::Xm5ConnectionState::Connected ||
                           legacy_reconnect_gate.requires_fresh_transport
                           ? native_ldac::agent::QueryLdacTransport(
                                 &transport_query_error)
                           : native_ldac::agent::LdacTransportState::Unavailable);
            const bool xm5_state_changed = !xm5_state_initialized ||
                                           xm5_state != last_xm5_state;
            const bool transport_state_changed =
                !transport_state_initialized ||
                transport_state != last_transport_state;
            const bool state_changed = xm5_state_changed ||
                                       transport_state_changed;
            if (xm5_state ==
                native_ldac::agent::Xm5ConnectionState::Disconnected) {
                xm5_disconnect_observed = true;
            }
            const bool recovery_allowed =
                UsesDirectPdo(options) &&
                native_ldac::agent::CanRecoverDirectPdoTransport(
                    direct_transport_info,
                    xm5_disconnect_observed);
            const native_ldac::agent::LegacyReconnectGate previous_gate =
                legacy_reconnect_gate;
            const native_ldac::agent::LegacyReconnectAction
                legacy_reconnect_action =
                    UsesDirectPdo(options)
                        ? native_ldac::agent::LegacyReconnectAction::
                              AllowCurrentConnection
                        : native_ldac::agent::ObserveLegacyReconnectGate(
                              &legacy_reconnect_gate,
                              xm5_state,
                              transport_state);
            if (!previous_gate.transport_absence_observed &&
                legacy_reconnect_gate.transport_absence_observed) {
                logger.Write(L"The old Native LDAC transport interface is "
                             L"absent; waiting for a fresh XM5 connection "
                             L"generation.");
            }
            if (previous_gate.requires_fresh_transport &&
                !legacy_reconnect_gate.requires_fresh_transport) {
                logger.Write(L"A fresh XM5 transport generation is ready; "
                             L"one new media session may start.");
            }
            const native_ldac::agent::Xm5PresenceAction presence_action =
                UsesDirectPdo(options)
                    ? native_ldac::agent::PlanDirectPdoPresence(
                          xm5_state,
                          direct_transport_info,
                          recovery_allowed)
                    : (legacy_reconnect_action ==
                               native_ldac::agent::LegacyReconnectAction::
                                   AllowCurrentConnection
                           ? native_ldac::agent::PlanInstalledPresence(
                                 xm5_state,
                                 transport_state,
                                 state_changed,
                                 recovery_allowed)
                           : native_ldac::agent::Xm5PresenceAction::Wait);
            if (xm5_state_changed) {
                if (xm5_state ==
                    native_ldac::agent::Xm5ConnectionState::Connected) {
                    logger.Write(L"Windows reports WH-1000XM5 connected.");
                } else if (xm5_state ==
                           native_ldac::agent::Xm5ConnectionState::Disconnected) {
                    logger.Write(
                        UsesDirectPdo(options)
                            ? L"Windows public Bluetooth state reports "
                              L"WH-1000XM5 disconnected; Direct-PDO runtime "
                              L"state remains authoritative."
                            : L"WH-1000XM5 is not connected; media probe "
                              L"will remain stopped.");
                } else {
                    logger.Write(
                        L"Could not query WH-1000XM5 connection state: " +
                        FormatWin32Error(query_error) +
                        (UsesDirectPdo(options)
                             ? L". Direct-PDO runtime state remains "
                               L"authoritative."
                             : L". Media probe will remain stopped."));
                }
                last_xm5_state = xm5_state;
                xm5_state_initialized = true;
            }
            if (transport_state_changed) {
                if (transport_state ==
                    native_ldac::agent::LdacTransportState::Ready) {
                    logger.Write(
                        UsesDirectPdo(options)
                            ? L"Native LDAC Direct-PDO transport is ready."
                            : L"Native LDAC transport is ready; waiting "
                              L"briefly for profile startup.");
                    xm5_disconnect_observed = false;
                } else if (transport_state ==
                           native_ldac::agent::LdacTransportState::Faulted) {
                    logger.Write(
                        L"Direct-PDO transport is faulted at session " +
                        std::to_wstring(
                            direct_transport_info.session_generation) +
                        L" with reason " +
                        std::to_wstring(
                            direct_transport_info.failure_reason) +
                        (recovery_allowed
                             ? L"; one generation-bound recovery is armed."
                             : L"; waiting for a safe reconnect edge."));
                } else if (transport_state ==
                           native_ldac::agent::LdacTransportState::Unavailable) {
                    std::wstring detail = transport_query_error == ERROR_SUCCESS
                                              ? std::wstring()
                                              : L" (" + FormatWin32Error(
                                                    transport_query_error) +
                                                    L")";
                    logger.Write(L"Native LDAC transport is unavailable" +
                                 detail + L"; media probe will remain stopped.");
                } else {
                    logger.Write(L"Could not query Native LDAC transport: " +
                                 FormatWin32Error(transport_query_error) +
                                 L". Media probe will remain stopped.");
                }
                last_transport_state = transport_state;
                transport_state_initialized = true;
                recovery_error_initialized = false;
            }

            if (presence_action ==
                native_ldac::agent::Xm5PresenceAction::Wait) {
                const DWORD presence_watchdog =
                    xm5_state ==
                            native_ldac::agent::Xm5ConnectionState::Connected
                        ? kInstalledTransportRetryMs
                        : kInstalledDeviceWatchdogMs;
                publish_state(
                    legacy_reconnect_gate.requires_fresh_transport
                        ? L"waiting_fresh_transport"
                        : (xm5_state ==
                            native_ldac::agent::Xm5ConnectionState::Connected
                        ? (transport_state ==
                                   native_ldac::agent::LdacTransportState::Faulted
                               ? L"waiting_recovery"
                               : L"waiting_transport")
                        : L"waiting_device"),
                               0,
                              generation,
                              last_probe_exit_code,
                              presence_watchdog);
                const DWORD deadline_timeout = DeadlineTimeout(run_deadline);
                const DWORD wait_timeout = deadline_timeout == INFINITE
                                               ? presence_watchdog
                                               : std::min(presence_watchdog,
                                                          deadline_timeout);
                HANDLE idle_waits[] = {agent_stop_event,
                                       device_change_event};
                const DWORD wait = WaitForMultipleObjects(2,
                                                          idle_waits,
                                                          FALSE,
                                                          wait_timeout);
                if (wait == WAIT_OBJECT_0 ||
                    DeadlineTimeout(run_deadline) == 0) {
                    logger.Write(L"Agent stop requested while waiting for "
                                 L"WH-1000XM5.");
                    publish_state(L"stopping",
                                  0,
                                  generation,
                                  last_probe_exit_code,
                                  0);
                    break;
                }
                if (wait == WAIT_OBJECT_0 + 1) {
                    (void)ResetEvent(device_change_event);
                } else if (wait == WAIT_FAILED) {
                    logger.Write(L"Device notification wait failed: " +
                                 FormatWin32Error(GetLastError()));
                    result = 30;
                    break;
                }
                continue;
            }

            if (presence_action ==
                native_ldac::agent::Xm5PresenceAction::Settle) {
                publish_state(L"settling_device",
                              0,
                              generation,
                              last_probe_exit_code,
                              kInstalledDeviceSettleMs);
                const DWORD deadline_timeout = DeadlineTimeout(run_deadline);
                const DWORD wait_timeout = deadline_timeout == INFINITE
                                               ? kInstalledDeviceSettleMs
                                               : std::min(kInstalledDeviceSettleMs,
                                                          deadline_timeout);
                HANDLE settle_waits[] = {agent_stop_event,
                                         device_change_event};
                const DWORD wait = WaitForMultipleObjects(2,
                                                          settle_waits,
                                                          FALSE,
                                                          wait_timeout);
                if (wait == WAIT_OBJECT_0 ||
                    DeadlineTimeout(run_deadline) == 0) {
                    logger.Write(L"Agent stop requested while waiting for "
                                 L"XM5 profile startup.");
                    publish_state(L"stopping",
                                  0,
                                  generation,
                                  last_probe_exit_code,
                                  0);
                    break;
                }
                if (wait == WAIT_OBJECT_0 + 1) {
                    (void)ResetEvent(device_change_event);
                } else if (wait == WAIT_FAILED) {
                    logger.Write(L"Device settle wait failed: " +
                                 FormatWin32Error(GetLastError()));
                    result = 30;
                    break;
                }
                continue;
            }

            if (presence_action ==
                native_ldac::agent::Xm5PresenceAction::RecoverTransport) {
                DWORD recovery_error = ERROR_SUCCESS;
                publish_state(L"recovering_transport",
                              0,
                              generation,
                              last_probe_exit_code,
                              0);
                if (native_ldac::agent::RequestDirectPdoRecovery(
                        direct_transport_info,
                        &recovery_error)) {
                    logger.Write(
                        L"Direct-PDO idle recovery accepted for fault "
                        L"generation " +
                        std::to_wstring(
                            direct_transport_info.session_generation) +
                        L"; waiting for the endpoint to republish.");
                    transport_state_initialized = false;
                    pcm_state_initialized = false;
                    recovery_error_initialized = false;
                    xm5_disconnect_observed = false;
                    continue;
                }
                if (!recovery_error_initialized ||
                    recovery_error != last_recovery_error) {
                    logger.Write(
                        L"Direct-PDO recovery is not ready yet: " +
                        FormatWin32Error(recovery_error) +
                        L". No Bluetooth OPEN was submitted.");
                    last_recovery_error = recovery_error;
                    recovery_error_initialized = true;
                }
                publish_state(L"waiting_recovery",
                              0,
                              generation,
                              last_probe_exit_code,
                              kInstalledTransportRetryMs);
                const DWORD deadline_timeout = DeadlineTimeout(run_deadline);
                const DWORD wait_timeout = deadline_timeout == INFINITE
                                               ? kInstalledTransportRetryMs
                                               : std::min(
                                                     kInstalledTransportRetryMs,
                                                     deadline_timeout);
                HANDLE recovery_waits[] = {agent_stop_event,
                                           device_change_event};
                const DWORD wait = WaitForMultipleObjects(2,
                                                          recovery_waits,
                                                          FALSE,
                                                          wait_timeout);
                if (wait == WAIT_OBJECT_0 ||
                    DeadlineTimeout(run_deadline) == 0) {
                    logger.Write(L"Agent stop requested while waiting for "
                                 L"safe Direct-PDO recovery.");
                    publish_state(L"stopping",
                                  0,
                                  generation,
                                  last_probe_exit_code,
                                  0);
                    break;
                }
                if (wait == WAIT_OBJECT_0 + 1) {
                    (void)ResetEvent(device_change_event);
                } else if (wait == WAIT_FAILED) {
                    logger.Write(L"Recovery wait failed: " +
                                 FormatWin32Error(GetLastError()));
                    result = 30;
                    break;
                }
                continue;
            }

            if (UsesDirectPdo(options)) {
                DWORD pcm_query_error = ERROR_SUCCESS;
                const native_ldac::agent::NativePcmRunState pcm_state =
                    native_ldac::agent::QueryNativePcmRunState(
                        &pcm_query_error);
                if (!pcm_state_initialized || pcm_state != last_pcm_run_state) {
                    if (pcm_state ==
                        native_ldac::agent::NativePcmRunState::Running) {
                        logger.Write(L"Native LDAC WaveRT pin entered RUN; "
                                     L"the media engine may start.");
                    } else if (pcm_state ==
                               native_ldac::agent::NativePcmRunState::Idle) {
                        logger.Write(L"Native LDAC WaveRT pin is idle; an "
                                     L"already-acquired Direct-PDO session "
                                     L"may pre-arm the media engine.");
                    } else if (pcm_state ==
                               native_ldac::agent::NativePcmRunState::Unavailable) {
                        logger.Write(L"Native LDAC PCM interface is "
                                     L"unavailable; encoder and media probe "
                                     L"remain stopped.");
                    } else {
                        logger.Write(L"Could not query Native LDAC WaveRT "
                                     L"RUN state: " +
                                     FormatWin32Error(pcm_query_error) +
                                     L". Encoder and media probe remain "
                                     L"stopped.");
                    }
                    last_pcm_run_state = pcm_state;
                    pcm_state_initialized = true;
                }
                if (native_ldac::agent::PlanDirectPdoMediaDemand(
                        direct_transport_info,
                        pcm_state) !=
                    native_ldac::agent::MediaDemandAction::StartEngine) {
                    publish_state(L"waiting_audio",
                                  0,
                                  generation,
                                  last_probe_exit_code,
                                  kInstalledAudioWatchdogMs);
                    const DWORD deadline_timeout =
                        DeadlineTimeout(run_deadline);
                    const DWORD wait_timeout = deadline_timeout == INFINITE
                                                   ? kInstalledAudioWatchdogMs
                                                   : std::min(
                                                         kInstalledAudioWatchdogMs,
                                                         deadline_timeout);
                    HANDLE audio_waits[] = {agent_stop_event,
                                            device_change_event};
                    const DWORD wait = WaitForMultipleObjects(2,
                                                              audio_waits,
                                                              FALSE,
                                                              wait_timeout);
                    if (wait == WAIT_OBJECT_0 ||
                        DeadlineTimeout(run_deadline) == 0) {
                        logger.Write(L"Agent stop requested while waiting "
                                     L"for WaveRT RUN.");
                        publish_state(L"stopping",
                                      0,
                                      generation,
                                      last_probe_exit_code,
                                      0);
                        break;
                    }
                    if (wait == WAIT_OBJECT_0 + 1) {
                        (void)ResetEvent(device_change_event);
                    } else if (wait == WAIT_FAILED) {
                        logger.Write(L"Audio-state wait failed: " +
                                     FormatWin32Error(GetLastError()));
                        result = 30;
                        break;
                    }
                    continue;
                }
            }
        }

        ++generation;
        const std::wstring probe_stop_event_name =
            L"Local\\NativeLdacAgent.ProbeStop." +
            std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(generation) +
            (options.instance_suffix.empty()
                 ? std::wstring()
                 : L"." + options.instance_suffix);
        HANDLE probe_stop_event = CreateEventW(
            nullptr, TRUE, FALSE, probe_stop_event_name.c_str());
        if (probe_stop_event == nullptr) {
            logger.Write(L"Could not create probe stop event: " +
                         FormatWin32Error(GetLastError()));
            result = 26;
            break;
        }

        ProbeProcess probe;
        logger.Write(L"Starting LDAC probe, generation " +
                     std::to_wstring(generation) + L".");
        publish_state(L"connecting",
                      0,
                      generation,
                      last_probe_exit_code,
                      0);
        const ULONGLONG session_started = GetTickCount64();
        if (!StartProbe(options,
                        probe_stop_event_name,
                        child_job,
                        &probe)) {
            logger.Write(L"Could not start transport_probe.exe: " +
                         FormatWin32Error(GetLastError()));
            CloseHandle(probe_stop_event);
            ++failure_count;
        } else {
            publish_state(L"probe_running",
                          probe.process_id,
                          generation,
                          last_probe_exit_code,
                          0);
            HANDLE waits[] = {agent_stop_event, probe.process};
            bool controlled_stop = false;
            bool configuration_restart = false;
            DWORD probe_exit_code = STILL_ACTIVE;
            for (;;) {
                const DWORD deadline_timeout = DeadlineTimeout(run_deadline);
                const DWORD wait_timeout = config_mode
                                               ? std::min(
                                                     kInstalledConfigPollMs,
                                                     deadline_timeout)
                                               : deadline_timeout;
                const DWORD wait = WaitForMultipleObjects(2,
                                                          waits,
                                                          FALSE,
                                                          wait_timeout);
                if (wait == WAIT_OBJECT_0 ||
                    (wait == WAIT_TIMEOUT &&
                     DeadlineTimeout(run_deadline) == 0)) {
                    controlled_stop = true;
                    publish_state(L"stopping",
                                  probe.process_id,
                                  generation,
                                  last_probe_exit_code,
                                  0);
                    (void)StopProbeGracefully(probe.process,
                                              probe_stop_event,
                                              logger,
                                              &probe_exit_code);
                    break;
                }
                if (wait == WAIT_OBJECT_0 + 1) {
                    if (!GetExitCodeProcess(probe.process,
                                            &probe_exit_code)) {
                        logger.Write(L"Could not query exited probe: " +
                                     FormatWin32Error(GetLastError()));
                        probe_exit_code = 0xFFFFFFFFu;
                    }
                    break;
                }
                if (wait == WAIT_TIMEOUT && config_mode) {
                    native_ldac::agent::AgentConfig observed_config;
                    DWORD config_error = ERROR_SUCCESS;
                    const bool config_valid =
                        read_installed_config(&observed_config,
                                              &config_error) ==
                        native_ldac::agent::ConfigReadResult::Loaded;
                    if (!config_valid ||
                        observed_config.revision != current_config.revision ||
                        observed_config.enabled != current_config.enabled ||
                        observed_config.quality != current_config.quality ||
                        observed_config.channel_mode !=
                            current_config.channel_mode ||
                        observed_config.sample_rate !=
                            current_config.sample_rate ||
                        observed_config.bits_per_sample !=
                            current_config.bits_per_sample) {
                        logger.Write(config_valid
                                         ? L"Config changed; restarting the "
                                           L"media session gracefully."
                                         : L"Config became invalid; stopping "
                                           L"the media session safely.");
                        controlled_stop = true;
                        configuration_restart = true;
                        publish_state(L"reconfiguring",
                                      probe.process_id,
                                      generation,
                                      last_probe_exit_code,
                                      0);
                        (void)StopProbeGracefully(probe.process,
                                                  probe_stop_event,
                                                  logger,
                                                  &probe_exit_code);
                        break;
                    }
                    continue;
                }
                logger.Write(L"Probe wait failed: " +
                             FormatWin32Error(GetLastError()));
                controlled_stop = true;
                publish_state(L"stopping",
                              probe.process_id,
                              generation,
                              last_probe_exit_code,
                              0);
                (void)StopProbeGracefully(probe.process,
                                          probe_stop_event,
                                          logger,
                                          &probe_exit_code);
                break;
            }

            FinishProbeOutput(&probe);
            const ULONGLONG session_duration =
                GetTickCount64() - session_started;
            last_probe_exit_code = probe_exit_code;
            if (UsesDirectPdo(options) && probe_exit_code == 5u) {
                last_xm5_state =
                    native_ldac::agent::Xm5ConnectionState::QueryFailed;
                last_transport_state =
                    native_ldac::agent::LdacTransportState::QueryFailed;
                xm5_state_initialized = false;
                transport_state_initialized = false;
                pcm_state_initialized = false;
            }
            logger.Write(L"Probe exited with code " +
                         std::to_wstring(probe_exit_code) + L" after " +
                         std::to_wstring(session_duration) + L" ms.");
            CloseHandle(probe.process);
            CloseHandle(probe_stop_event);

            if (configuration_restart) {
                failure_count = 0;
                continue;
            }
            if (controlled_stop) {
                result = 0;
                break;
            }
            if (options.once) {
                result = probe_exit_code == 0
                             ? 0
                             : static_cast<int>(probe_exit_code);
                break;
            }
            if (!UsesDirectPdo(options) && options.wait_for_xm5) {
                native_ldac::agent::ArmLegacyReconnectGate(
                    &legacy_reconnect_gate);
                logger.Write(
                    L"The legacy media session ended unexpectedly. "
                    L"Reconnect is blocked until the old transport "
                    L"interface disappears and a fresh XM5 connection "
                    L"generation is ready.");
                failure_count = 0;
                continue;
            }
            if (session_duration >= kStableSessionMs) {
                failure_count = 0;
            } else {
                ++failure_count;
            }
        }

        if (options.once) {
            result = 27;
            break;
        }

        const DWORD retry_delay =
            native_ldac::agent::ReconnectDelayMs(failure_count,
                                                  last_probe_exit_code);
        logger.Write(L"Waiting " + std::to_wstring(retry_delay) +
                     L" ms before reconnecting.");
        publish_state(L"retry_wait",
                      0,
                      generation,
                      last_probe_exit_code,
                      retry_delay);
        const DWORD deadline_timeout = DeadlineTimeout(run_deadline);
        const DWORD wait_timeout = deadline_timeout == INFINITE
                                       ? retry_delay
                                       : std::min(retry_delay,
                                                  deadline_timeout);
        if (WaitForSingleObject(agent_stop_event, wait_timeout) ==
                WAIT_OBJECT_0 ||
            DeadlineTimeout(run_deadline) == 0) {
            logger.Write(L"Agent stop requested during reconnect delay.");
            publish_state(L"stopping",
                          0,
                          generation,
                          last_probe_exit_code,
                          0);
            break;
        }
    }

    publish_state(L"stopped",
                  0,
                  generation,
                  last_probe_exit_code,
                  0);
    logger.Write(L"Native LDAC agent stopped with code " +
                 std::to_wstring(result) + L".");
    session_end_monitor.Stop();
    CloseHandle(child_job);
    CloseHandle(device_change_event);
    CloseHandle(agent_stop_event);
    CloseHandle(singleton);
    return result;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return 2;
    }
    Options options;
    const bool parsed = ParseOptions(argc, argv, &options);
    LocalFree(argv);
    if (!parsed) {
        return 2;
    }
    return RunAgent(options);
}
