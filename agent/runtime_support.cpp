#include "runtime_support.h"

#include <bluetoothapis.h>
#include <ks.h>
#include <ksmedia.h>
#include <setupapi.h>

#include <initguid.h>
#include "ldac_native_ioctl.h"
#include "nativeldac_pcm_abi.h"
#include "nativeldac_direct_pdo_public.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace native_ldac::agent {
namespace {

const GUID kAudioCategory = {STATIC_KSCATEGORY_AUDIO};
const GUID kNativePcmPropertySet = {STATIC_KSPROPSETID_NativeLdacPcm};
const GUID kDirectPdoPropertySet = {
    STATIC_KSPROPSETID_NativeLdacDirectPdo};

bool EnsureParentDirectory(const std::wstring& path) {
    const std::filesystem::path parent =
        std::filesystem::path(path).parent_path();
    if (parent.empty()) {
        return true;
    }
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    return !error;
}

std::string Utf8(const std::wstring& value) {
    if (value.empty()) {
        return std::string();
    }
    const int required = WideCharToMultiByte(CP_UTF8,
                                             0,
                                             value.c_str(),
                                             static_cast<int>(value.size()),
                                             nullptr,
                                             0,
                                             nullptr,
                                             nullptr);
    if (required <= 0) {
        return std::string();
    }
    std::string result(static_cast<size_t>(required), '\0');
    (void)WideCharToMultiByte(CP_UTF8,
                             0,
                             value.c_str(),
                             static_cast<int>(value.size()),
                             result.data(),
                             required,
                             nullptr,
                             nullptr);
    return result;
}

std::wstring JsonEscape(const std::wstring& value) {
    std::wstring escaped;
    escaped.reserve(value.size());
    for (const wchar_t character : value) {
        switch (character) {
            case L'\\':
                escaped += L"\\\\";
                break;
            case L'\"':
                escaped += L"\\\"";
                break;
            case L'\r':
                escaped += L"\\r";
                break;
            case L'\n':
                escaped += L"\\n";
                break;
            case L'\t':
                escaped += L"\\t";
                break;
            default:
                if (character >= 0 && character < 0x20) {
                    wchar_t sequence[7]{};
                    (void)swprintf_s(sequence,
                                     L"\\u%04X",
                                     static_cast<unsigned int>(character));
                    escaped += sequence;
                } else {
                    escaped.push_back(character);
                }
                break;
        }
    }
    return escaped;
}

std::wstring Iso8601UtcNow() {
    SYSTEMTIME now{};
    GetSystemTime(&now);
    wchar_t timestamp[32]{};
    (void)swprintf_s(timestamp,
                     L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                     static_cast<unsigned int>(now.wYear),
                     static_cast<unsigned int>(now.wMonth),
                     static_cast<unsigned int>(now.wDay),
                     static_cast<unsigned int>(now.wHour),
                     static_cast<unsigned int>(now.wMinute),
                     static_cast<unsigned int>(now.wSecond),
                     static_cast<unsigned int>(now.wMilliseconds));
    return timestamp;
}

bool WriteAll(HANDLE file, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::size_t offset = 0;
    while (offset < size) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            size - offset,
            static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (!WriteFile(file, bytes + offset, chunk, &written, nullptr) ||
            written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

bool FindJsonValue(const std::string& json,
                   const char* key,
                   std::size_t* value_offset) {
    if (key == nullptr || value_offset == nullptr) {
        return false;
    }
    const std::string token = std::string("\"") + key + "\"";
    const std::size_t key_offset = json.find(token);
    if (key_offset == std::string::npos ||
        json.find(token, key_offset + token.size()) != std::string::npos) {
        return false;
    }
    std::size_t offset = key_offset + token.size();
    while (offset < json.size() &&
           (json[offset] == ' ' || json[offset] == '\t' ||
            json[offset] == '\r' || json[offset] == '\n')) {
        ++offset;
    }
    if (offset >= json.size() || json[offset] != ':') {
        return false;
    }
    ++offset;
    while (offset < json.size() &&
           (json[offset] == ' ' || json[offset] == '\t' ||
            json[offset] == '\r' || json[offset] == '\n')) {
        ++offset;
    }
    if (offset >= json.size()) {
        return false;
    }
    *value_offset = offset;
    return true;
}

bool IsJsonValueTerminated(const std::string& json, std::size_t offset) {
    while (offset < json.size() &&
           (json[offset] == ' ' || json[offset] == '\t' ||
            json[offset] == '\r' || json[offset] == '\n')) {
        ++offset;
    }
    return offset < json.size() &&
           (json[offset] == ',' || json[offset] == '}');
}

bool ParseJsonUnsigned(const std::string& json,
                       const char* key,
                       std::uint64_t* value) {
    std::size_t offset = 0;
    if (value == nullptr || !FindJsonValue(json, key, &offset) ||
        json[offset] < '0' || json[offset] > '9') {
        return false;
    }
    std::uint64_t parsed = 0;
    do {
        const unsigned int digit =
            static_cast<unsigned int>(json[offset] - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) /
                         10u) {
            return false;
        }
        parsed = parsed * 10u + digit;
        ++offset;
    } while (offset < json.size() &&
             json[offset] >= '0' && json[offset] <= '9');
    if (!IsJsonValueTerminated(json, offset)) {
        return false;
    }
    *value = parsed;
    return true;
}

bool ParseJsonBool(const std::string& json,
                   const char* key,
                   bool* value) {
    std::size_t offset = 0;
    if (value == nullptr || !FindJsonValue(json, key, &offset)) {
        return false;
    }
    if (json.compare(offset, 4, "true") == 0) {
        if (!IsJsonValueTerminated(json, offset + 4u)) {
            return false;
        }
        *value = true;
        return true;
    }
    if (json.compare(offset, 5, "false") == 0) {
        if (!IsJsonValueTerminated(json, offset + 5u)) {
            return false;
        }
        *value = false;
        return true;
    }
    return false;
}

bool ParseJsonAsciiString(const std::string& json,
                          const char* key,
                          std::string* value) {
    std::size_t offset = 0;
    if (value == nullptr || !FindJsonValue(json, key, &offset) ||
        json[offset] != '"') {
        return false;
    }
    const std::size_t end = json.find('"', offset + 1u);
    if (end == std::string::npos ||
        json.find('\\', offset + 1u) < end ||
        !IsJsonValueTerminated(json, end + 1u)) {
        return false;
    }
    *value = json.substr(offset + 1u, end - offset - 1u);
    return true;
}

bool ReadSmallFile(const std::wstring& path,
                   std::string* contents,
                   DWORD* read_error) {
    if (contents == nullptr) {
        if (read_error != nullptr) {
            *read_error = ERROR_INVALID_PARAMETER;
        }
        return false;
    }
    HANDLE file = CreateFileW(path.c_str(),
                              GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (read_error != nullptr) {
            *read_error = GetLastError();
        }
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        const DWORD error = GetLastError();
        CloseHandle(file);
        if (read_error != nullptr) {
            *read_error = error;
        }
        return false;
    }
    if (size.QuadPart <= 0 || size.QuadPart > 4096) {
        CloseHandle(file);
        if (read_error != nullptr) {
            *read_error = ERROR_INVALID_DATA;
        }
        return false;
    }
    contents->assign(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD bytes_read = 0;
    const BOOL read = ReadFile(file,
                               contents->data(),
                               static_cast<DWORD>(contents->size()),
                               &bytes_read,
                               nullptr);
    const DWORD error = read ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!read || bytes_read != contents->size()) {
        if (read_error != nullptr) {
            *read_error = error == ERROR_SUCCESS ? ERROR_READ_FAULT : error;
        }
        return false;
    }
    return true;
}

}  // namespace

RotatingLog::~RotatingLog() { Close(); }

bool RotatingLog::Open(const std::wstring& path,
                       std::uint64_t max_bytes,
                       unsigned int backup_count) {
    Close();
    if (path.empty() || !EnsureParentDirectory(path)) {
        return false;
    }
    path_ = path;
    max_bytes_ = max_bytes;
    backup_count_ = backup_count;
    if (!OpenCurrent()) {
        return false;
    }
    if (max_bytes_ != 0 && current_size_ >= max_bytes_) {
        return Rotate();
    }
    return true;
}

bool RotatingLog::Write(const void* data, std::size_t size) {
    if (handle_ == INVALID_HANDLE_VALUE ||
        (data == nullptr && size != 0)) {
        return false;
    }
    if (size == 0) {
        return true;
    }
    if (max_bytes_ != 0 && current_size_ != 0 &&
        size > max_bytes_ - std::min(current_size_, max_bytes_)) {
        if (!Rotate()) {
            return false;
        }
    }
    if (!WriteAll(handle_, data, size)) {
        return false;
    }
    current_size_ += size;
    return true;
}

void RotatingLog::Flush() const {
    if (handle_ != INVALID_HANDLE_VALUE) {
        (void)FlushFileBuffers(handle_);
    }
}

void RotatingLog::Close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        (void)FlushFileBuffers(handle_);
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    current_size_ = 0;
}

bool RotatingLog::OpenCurrent() {
    handle_ = CreateFileW(path_.c_str(),
                          FILE_APPEND_DATA,
                          FILE_SHARE_READ | FILE_SHARE_WRITE |
                              FILE_SHARE_DELETE,
                          nullptr,
                          OPEN_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL,
                          nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle_, &size) || size.QuadPart < 0) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
        return false;
    }
    current_size_ = static_cast<std::uint64_t>(size.QuadPart);
    return true;
}

std::wstring RotatingLog::BackupPath(unsigned int index) const {
    return path_ + L"." + std::to_wstring(index);
}

bool RotatingLog::Rotate() {
    Close();
    if (backup_count_ == 0) {
        if (!DeleteFileW(path_.c_str()) &&
            GetLastError() != ERROR_FILE_NOT_FOUND) {
            return false;
        }
    } else {
        const std::wstring oldest = BackupPath(backup_count_);
        if (!DeleteFileW(oldest.c_str()) &&
            GetLastError() != ERROR_FILE_NOT_FOUND) {
            return false;
        }
        for (unsigned int index = backup_count_; index > 1; --index) {
            const std::wstring source = BackupPath(index - 1);
            const std::wstring destination = BackupPath(index);
            if (!MoveFileExW(source.c_str(),
                             destination.c_str(),
                             MOVEFILE_REPLACE_EXISTING |
                                 MOVEFILE_WRITE_THROUGH) &&
                GetLastError() != ERROR_FILE_NOT_FOUND) {
                return false;
            }
        }
        if (!MoveFileExW(path_.c_str(),
                         BackupPath(1).c_str(),
                         MOVEFILE_REPLACE_EXISTING |
                             MOVEFILE_WRITE_THROUGH) &&
            GetLastError() != ERROR_FILE_NOT_FOUND) {
            return false;
        }
    }
    return OpenCurrent();
}

bool WriteStateAtomically(const std::wstring& path,
                          const StateSnapshot& snapshot) {
    if (path.empty() || !EnsureParentDirectory(path)) {
        return false;
    }
    const std::wstring json =
        L"{\r\n"
        L"  \"version\": 2,\r\n"
        L"  \"updated_at\": \"" + JsonEscape(Iso8601UtcNow()) +
        L"\",\r\n"
        L"  \"state\": \"" + JsonEscape(snapshot.state) + L"\",\r\n"
        L"  \"quality\": \"" + JsonEscape(snapshot.quality) +
        L"\",\r\n"
        L"  \"agent_pid\": " + std::to_wstring(snapshot.agent_pid) +
        L",\r\n"
        L"  \"probe_pid\": " + std::to_wstring(snapshot.probe_pid) +
        L",\r\n"
        L"  \"generation\": " + std::to_wstring(snapshot.generation) +
        L",\r\n"
        L"  \"last_probe_exit_code\": " +
        std::to_wstring(snapshot.last_probe_exit_code) + L",\r\n"
        L"  \"retry_delay_ms\": " +
        std::to_wstring(snapshot.retry_delay_ms) + L",\r\n"
        L"  \"config_enabled\": " +
        std::wstring(snapshot.config_enabled ? L"true" : L"false") +
        L",\r\n"
        L"  \"config_revision\": " +
        std::to_wstring(snapshot.config_revision) + L"\r\n"
        L"}\r\n";
    const std::string utf8 = Utf8(json);
    if (utf8.empty()) {
        return false;
    }

    const std::wstring temporary =
        path + L".tmp." + std::to_wstring(GetCurrentProcessId());
    HANDLE file = CreateFileW(temporary.c_str(),
                              GENERIC_WRITE,
                              0,
                              nullptr,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    const bool written = WriteAll(file, utf8.data(), utf8.size());
    const bool flushed = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (!written || !flushed) {
        (void)DeleteFileW(temporary.c_str());
        return false;
    }
    if (!MoveFileExW(temporary.c_str(),
                     path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        (void)DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

DWORD ReconnectDelayMs(unsigned int failure_count,
                       DWORD probe_exit_code) {
    const unsigned int shift = failure_count == 0
                                   ? 0
                                   : std::min(failure_count - 1u, 4u);
    const std::uint64_t backoff = std::min<std::uint64_t>(
        2000ull << shift,
        30000ull);
    const std::uint64_t disconnect_cooldown =
        probe_exit_code == 5u ? 30000ull : 0ull;
    return static_cast<DWORD>(std::max(backoff, disconnect_cooldown));
}

bool IsXm5DeviceName(const wchar_t* name) {
    return name != nullptr &&
           _wcsicmp(name, L"WH-1000XM5") == 0;
}

Xm5ConnectionState QueryXm5Connection(DWORD* query_error) {
    if (query_error != nullptr) {
        *query_error = ERROR_SUCCESS;
    }

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
        const DWORD error = GetLastError();
        if (error == ERROR_SUCCESS || error == ERROR_NO_MORE_ITEMS) {
            return Xm5ConnectionState::Disconnected;
        }
        if (query_error != nullptr) {
            *query_error = error;
        }
        return Xm5ConnectionState::QueryFailed;
    }

    Xm5ConnectionState result = Xm5ConnectionState::Disconnected;
    BOOL has_device = TRUE;
    DWORD enumeration_error = ERROR_SUCCESS;
    while (has_device) {
        if (IsXm5DeviceName(device.szName) && device.fConnected) {
            result = Xm5ConnectionState::Connected;
            break;
        }
        device = {};
        device.dwSize = sizeof(device);
        has_device = BluetoothFindNextDevice(find, &device);
        if (!has_device) {
            enumeration_error = GetLastError();
        }
    }

    if (result != Xm5ConnectionState::Connected &&
        enumeration_error != ERROR_NO_MORE_ITEMS) {
        if (query_error != nullptr) {
            *query_error = enumeration_error;
        }
        result = Xm5ConnectionState::QueryFailed;
    }

    BluetoothFindDeviceClose(find);
    return result;
}

LdacTransportState QueryLdacTransport(DWORD* query_error) {
    if (query_error != nullptr) {
        *query_error = ERROR_SUCCESS;
    }

    HDEVINFO devices = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_LDAC_NATIVE_TRANSPORT,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) {
        if (query_error != nullptr) {
            *query_error = GetLastError();
        }
        return LdacTransportState::QueryFailed;
    }

    SP_DEVICE_INTERFACE_DATA interface_data{};
    interface_data.cbSize = sizeof(interface_data);
    if (!SetupDiEnumDeviceInterfaces(
            devices,
            nullptr,
            &GUID_DEVINTERFACE_LDAC_NATIVE_TRANSPORT,
            0,
            &interface_data)) {
        const DWORD error = GetLastError();
        SetupDiDestroyDeviceInfoList(devices);
        if (error == ERROR_NO_MORE_ITEMS) {
            return LdacTransportState::Unavailable;
        }
        if (query_error != nullptr) {
            *query_error = error;
        }
        return LdacTransportState::QueryFailed;
    }

    DWORD required_size = 0;
    (void)SetupDiGetDeviceInterfaceDetailW(devices,
                                           &interface_data,
                                           nullptr,
                                           0,
                                           &required_size,
                                           nullptr);
    if (required_size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        const DWORD error = GetLastError();
        SetupDiDestroyDeviceInfoList(devices);
        if (query_error != nullptr) {
            *query_error = error == ERROR_SUCCESS ? ERROR_INVALID_DATA : error;
        }
        return LdacTransportState::QueryFailed;
    }

    std::vector<unsigned char> detail_storage(required_size);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
        detail_storage.data());
    detail->cbSize = sizeof(*detail);
    if (!SetupDiGetDeviceInterfaceDetailW(devices,
                                           &interface_data,
                                           detail,
                                           required_size,
                                           nullptr,
                                           nullptr)) {
        const DWORD error = GetLastError();
        SetupDiDestroyDeviceInfoList(devices);
        if (query_error != nullptr) {
            *query_error = error;
        }
        return LdacTransportState::QueryFailed;
    }
    SetupDiDestroyDeviceInfoList(devices);

    HANDLE transport = CreateFileW(detail->DevicePath,
                                   GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr,
                                   OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
    if (transport == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (query_error != nullptr) {
            *query_error = error;
        }
        if (error == ERROR_FILE_NOT_FOUND ||
            error == ERROR_PATH_NOT_FOUND ||
            error == ERROR_DEVICE_NOT_CONNECTED ||
            error == ERROR_NOT_READY) {
            return LdacTransportState::Unavailable;
        }
        return LdacTransportState::QueryFailed;
    }

    LDAC_NATIVE_DEVICE_INFO info{};
    DWORD bytes_returned = 0;
    const BOOL queried = DeviceIoControl(
        transport,
        IOCTL_LDAC_NATIVE_GET_DEVICE_INFO,
        nullptr,
        0,
        &info,
        sizeof(info),
        &bytes_returned,
        nullptr);
    const DWORD error = queried ? ERROR_SUCCESS : GetLastError();
    CloseHandle(transport);
    if (!queried) {
        if (query_error != nullptr) {
            *query_error = error;
        }
        if (error == ERROR_DEVICE_NOT_CONNECTED ||
            error == ERROR_NOT_READY) {
            return LdacTransportState::Unavailable;
        }
        return LdacTransportState::QueryFailed;
    }
    if (bytes_returned < sizeof(info) || info.Size != sizeof(info)) {
        if (query_error != nullptr) {
            *query_error = ERROR_INVALID_DATA;
        }
        return LdacTransportState::QueryFailed;
    }

    constexpr ULONG kRequiredReadyFlags =
        LDAC_NATIVE_DEVICE_INFO_PROFILE_READY |
        LDAC_NATIVE_DEVICE_INFO_REMOTE_READY |
        LDAC_NATIVE_DEVICE_INFO_LOCAL_READY |
        LDAC_NATIVE_DEVICE_INFO_INBOUND_SIGNALING_READY;
    if ((info.Flags & kRequiredReadyFlags) != kRequiredReadyFlags) {
        if (query_error != nullptr) {
            *query_error = ERROR_NOT_READY;
        }
        return LdacTransportState::Unavailable;
    }
    return LdacTransportState::Ready;
}

NativePcmRunState QueryNativePcmRunState(DWORD* query_error) {
    if (query_error != nullptr) {
        *query_error = ERROR_SUCCESS;
    }

    HDEVINFO devices = SetupDiGetClassDevsW(
        &kAudioCategory,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) {
        if (query_error != nullptr) {
            *query_error = GetLastError();
        }
        return NativePcmRunState::QueryFailed;
    }

    NativePcmRunState result = NativePcmRunState::Unavailable;
    DWORD last_error = ERROR_NOT_FOUND;
    SP_DEVICE_INTERFACE_DATA interface_data{};
    interface_data.cbSize = sizeof(interface_data);
    for (DWORD index = 0;
         SetupDiEnumDeviceInterfaces(devices,
                                     nullptr,
                                     &kAudioCategory,
                                     index,
                                     &interface_data);
         ++index) {
        DWORD required_size = 0;
        (void)SetupDiGetDeviceInterfaceDetailW(devices,
                                               &interface_data,
                                               nullptr,
                                               0,
                                               &required_size,
                                               nullptr);
        if (required_size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            last_error = GetLastError();
            continue;
        }

        std::vector<unsigned char> detail_storage(required_size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
            detail_storage.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(devices,
                                               &interface_data,
                                               detail,
                                               required_size,
                                               nullptr,
                                               nullptr)) {
            last_error = GetLastError();
            continue;
        }

        HANDLE endpoint = CreateFileW(detail->DevicePath,
                                      GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL,
                                      nullptr);
        if (endpoint == INVALID_HANDLE_VALUE) {
            last_error = GetLastError();
            continue;
        }

        KSPROPERTY property{};
        property.Set = kNativePcmPropertySet;
        property.Id = NativeLdacPcmPropertyInfo;
        property.Flags = KSPROPERTY_TYPE_GET;
        NATIVE_LDAC_PCM_INFO info{};
        DWORD bytes_returned = 0;
        const BOOL queried = DeviceIoControl(endpoint,
                                             IOCTL_KS_PROPERTY,
                                             &property,
                                             sizeof(property),
                                             &info,
                                             sizeof(info),
                                             &bytes_returned,
                                             nullptr);
        last_error = queried ? ERROR_SUCCESS : GetLastError();
        CloseHandle(endpoint);
        if (!queried) {
            continue;
        }
        if (bytes_returned < sizeof(info) || info.Size < sizeof(info) ||
            info.AbiVersion != NATIVE_LDAC_PCM_ABI_VERSION) {
            result = NativePcmRunState::QueryFailed;
            last_error = ERROR_INVALID_DATA;
            break;
        }
        result = (info.Flags & NATIVE_LDAC_PCM_FLAG_STREAM_ACTIVE) != 0u
                     ? NativePcmRunState::Running
                     : NativePcmRunState::Idle;
        last_error = ERROR_SUCCESS;
        break;
    }
    if (result == NativePcmRunState::Unavailable) {
        const DWORD enumeration_error = GetLastError();
        if (enumeration_error == ERROR_NO_MORE_ITEMS) {
            last_error = ERROR_SUCCESS;
        } else {
            result = NativePcmRunState::QueryFailed;
            last_error = enumeration_error;
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    if (query_error != nullptr) {
        *query_error = last_error;
    }
    return result;
}

static HANDLE OpenDirectPdoEndpoint(
    NLD_DIRECT_PDO_MEDIA_STATUS_V1* media_status,
    DWORD desired_access,
    DWORD* query_error) {
    if (query_error != nullptr) *query_error = ERROR_SUCCESS;
    if (media_status == nullptr) {
        if (query_error != nullptr) *query_error = ERROR_INVALID_PARAMETER;
        return INVALID_HANDLE_VALUE;
    }
    std::memset(media_status, 0, sizeof(*media_status));
    HDEVINFO devices = SetupDiGetClassDevsW(
        &kAudioCategory,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) {
        if (query_error != nullptr) *query_error = GetLastError();
        return INVALID_HANDLE_VALUE;
    }

    HANDLE result = INVALID_HANDLE_VALUE;
    DWORD last_error = ERROR_NOT_FOUND;
    SP_DEVICE_INTERFACE_DATA interface_data{};
    interface_data.cbSize = sizeof(interface_data);
    for (DWORD index = 0;
         SetupDiEnumDeviceInterfaces(devices,
                                     nullptr,
                                     &kAudioCategory,
                                     index,
                                     &interface_data);
         ++index) {
        DWORD required_size = 0;
        (void)SetupDiGetDeviceInterfaceDetailW(devices,
                                               &interface_data,
                                               nullptr,
                                               0,
                                               &required_size,
                                               nullptr);
        if (required_size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            continue;
        }
        std::vector<unsigned char> detail_storage(required_size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
            detail_storage.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(devices,
                                               &interface_data,
                                               detail,
                                               required_size,
                                               nullptr,
                                               nullptr)) {
            continue;
        }
        HANDLE endpoint = CreateFileW(detail->DevicePath,
                                      desired_access,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL,
                                      nullptr);
        if (endpoint == INVALID_HANDLE_VALUE) continue;

        KSPROPERTY property{};
        property.Set = kDirectPdoPropertySet;
        property.Id = NldDirectPdoPropertyMediaStatus;
        property.Flags = KSPROPERTY_TYPE_GET;
        NLD_DIRECT_PDO_MEDIA_STATUS_V1 status{};
        DWORD bytes_returned = 0;
        const BOOL queried = DeviceIoControl(endpoint,
                                             IOCTL_KS_PROPERTY,
                                             &property,
                                             sizeof(property),
                                             &status,
                                             sizeof(status),
                                             &bytes_returned,
                                             nullptr);
        last_error = queried ? ERROR_SUCCESS : GetLastError();
        if (!queried) {
            CloseHandle(endpoint);
            continue;
        }
        if (bytes_returned < sizeof(status) ||
            status.Size != sizeof(status) ||
            status.Version != NLD_DIRECT_PDO_MEDIA_ABI_VERSION) {
            CloseHandle(endpoint);
            last_error = ERROR_REVISION_MISMATCH;
            break;
        }
        *media_status = status;
        result = endpoint;
        last_error = ERROR_SUCCESS;
        break;
    }
    const DWORD enumeration_error = GetLastError();
    SetupDiDestroyDeviceInfoList(devices);
    if (result == INVALID_HANDLE_VALUE &&
        last_error == ERROR_NOT_FOUND &&
        enumeration_error != ERROR_NO_MORE_ITEMS) {
        last_error = enumeration_error;
    }
    if (query_error != nullptr) *query_error = last_error;
    return result;
}

LdacTransportState QueryDirectPdoTransport(
    DirectPdoTransportInfo* info,
    DWORD* query_error) {
    DirectPdoTransportInfo observed;
    NLD_DIRECT_PDO_MEDIA_STATUS_V1 status{};
    DWORD error = ERROR_SUCCESS;
    HANDLE endpoint = OpenDirectPdoEndpoint(&status,
                                            GENERIC_READ,
                                            &error);

    if (endpoint == INVALID_HANDLE_VALUE) {
        observed.state = error == ERROR_NOT_FOUND ||
                                 error == ERROR_NO_MORE_ITEMS ||
                                 error == ERROR_FILE_NOT_FOUND ||
                                 error == ERROR_PATH_NOT_FOUND ||
                                 error == ERROR_DEVICE_NOT_CONNECTED ||
                                 error == ERROR_NOT_READY
                             ? LdacTransportState::Unavailable
                             : LdacTransportState::QueryFailed;
    } else {
        CloseHandle(endpoint);
        observed.media_state = status.State;
        observed.session_generation = status.SessionGeneration;
        observed.failure_reason = status.FailureReason;
        observed.flags = status.Flags;
        if (status.State == NldDirectPdoMediaFaulted) {
            observed.state = LdacTransportState::Faulted;
        } else {
            observed.state =
                (status.Flags &
                 NLD_DIRECT_PDO_MEDIA_STATUS_PNP_STARTED) != 0u
                    ? LdacTransportState::Ready
                    : LdacTransportState::Unavailable;
            if (observed.state == LdacTransportState::Unavailable) {
                error = ERROR_NOT_READY;
            }
        }
    }
    if (info != nullptr) *info = observed;
    if (query_error != nullptr) {
        *query_error = observed.state == LdacTransportState::Ready ||
                               observed.state == LdacTransportState::Faulted
                           ? ERROR_SUCCESS
                           : error;
    }
    return observed.state;
}

LdacTransportState QueryDirectPdoTransport(DWORD* query_error) {
    return QueryDirectPdoTransport(nullptr, query_error);
}

bool RequestDirectPdoRecovery(const DirectPdoTransportInfo& info,
                              DWORD* request_error) {
    if (request_error != nullptr) *request_error = ERROR_SUCCESS;
    if (info.state != LdacTransportState::Faulted ||
        info.session_generation == 0u ||
        info.failure_reason == NldDirectPdoFailureNone ||
        info.failure_reason > NldDirectPdoFailureBackend) {
        if (request_error != nullptr) {
            *request_error = ERROR_INVALID_PARAMETER;
        }
        return false;
    }

    NLD_DIRECT_PDO_MEDIA_STATUS_V1 observed{};
    DWORD error = ERROR_SUCCESS;
    HANDLE endpoint = OpenDirectPdoEndpoint(&observed,
                                            GENERIC_READ | GENERIC_WRITE,
                                            &error);
    if (endpoint == INVALID_HANDLE_VALUE) {
        if (request_error != nullptr) *request_error = error;
        return false;
    }
    if (observed.State != NldDirectPdoMediaFaulted ||
        observed.SessionGeneration != info.session_generation ||
        observed.FailureReason != info.failure_reason) {
        CloseHandle(endpoint);
        if (request_error != nullptr) *request_error = ERROR_RETRY;
        return false;
    }

    NLD_DIRECT_PDO_RECOVERY_REQUEST_V1 request{};
    request.Size = sizeof(request);
    request.Version = NLD_DIRECT_PDO_MEDIA_ABI_VERSION;
    request.ExpectedSessionGeneration = info.session_generation;
    request.ExpectedFailureReason = info.failure_reason;
    KSPROPERTY property{};
    property.Set = kDirectPdoPropertySet;
    property.Id = NldDirectPdoPropertyRecovery;
    property.Flags = KSPROPERTY_TYPE_SET;
    DWORD bytes_returned = 0;
    const BOOL requested = DeviceIoControl(endpoint,
                                            IOCTL_KS_PROPERTY,
                                            &property,
                                            sizeof(property),
                                            &request,
                                            sizeof(request),
                                            &bytes_returned,
                                            nullptr);
    error = requested ? ERROR_SUCCESS : GetLastError();
    CloseHandle(endpoint);
    if (request_error != nullptr) *request_error = error;
    return requested != FALSE;
}

bool CanRecoverDirectPdoTransport(const DirectPdoTransportInfo& info,
                                  bool disconnect_observed) {
    if (info.state != LdacTransportState::Faulted) return false;
    if (info.failure_reason == NldDirectPdoFailureMediaTimeout) return true;
    return disconnect_observed &&
           (info.failure_reason == NldDirectPdoFailureRemoteDisconnect ||
            info.failure_reason == NldDirectPdoFailureBackend);
}

MediaDemandAction PlanInstalledMediaDemand(
    Xm5ConnectionState state,
    LdacTransportState transport_state,
    NativePcmRunState pcm_state) {
    if (state != Xm5ConnectionState::Connected ||
        transport_state != LdacTransportState::Ready) {
        return MediaDemandAction::WaitForDevice;
    }
    return pcm_state == NativePcmRunState::Running
               ? MediaDemandAction::StartEngine
               : MediaDemandAction::WaitForAudio;
}

MediaDemandAction PlanDirectPdoMediaDemand(
    const DirectPdoTransportInfo& info,
    NativePcmRunState pcm_state) {
    if (info.state != LdacTransportState::Ready) {
        return MediaDemandAction::WaitForDevice;
    }
    if (pcm_state == NativePcmRunState::Running ||
        info.media_state == NldDirectPdoMediaOpen ||
        info.media_state == NldDirectPdoMediaStreaming) {
        return MediaDemandAction::StartEngine;
    }
    return MediaDemandAction::WaitForAudio;
}

Xm5PresenceAction PlanInstalledPresence(Xm5ConnectionState state,
                                        LdacTransportState transport_state,
                                        bool state_changed,
                                        bool recovery_allowed) {
    if (state != Xm5ConnectionState::Connected) {
        return Xm5PresenceAction::Wait;
    }
    if (transport_state == LdacTransportState::Faulted) {
        if (!recovery_allowed) return Xm5PresenceAction::Wait;
        return state_changed ? Xm5PresenceAction::Settle
                             : Xm5PresenceAction::RecoverTransport;
    }
    if (transport_state != LdacTransportState::Ready) {
        return Xm5PresenceAction::Wait;
    }
    return state_changed ? Xm5PresenceAction::Settle
                         : Xm5PresenceAction::StartProbe;
}

Xm5PresenceAction PlanDirectPdoPresence(
    Xm5ConnectionState state,
    const DirectPdoTransportInfo& info,
    bool recovery_allowed) {
    if (info.state == LdacTransportState::Faulted) {
        if (!recovery_allowed) return Xm5PresenceAction::Wait;
        if (info.failure_reason == NldDirectPdoFailureMediaTimeout ||
            state == Xm5ConnectionState::Connected) {
            return Xm5PresenceAction::RecoverTransport;
        }
        return Xm5PresenceAction::Wait;
    }
    return info.state == LdacTransportState::Ready
               ? Xm5PresenceAction::StartProbe
               : Xm5PresenceAction::Wait;
}

void ArmLegacyReconnectGate(LegacyReconnectGate* gate) {
    if (gate == nullptr) return;
    gate->requires_fresh_transport = true;
    gate->transport_absence_observed = false;
}

LegacyReconnectAction ObserveLegacyReconnectGate(
    LegacyReconnectGate* gate,
    Xm5ConnectionState state,
    LdacTransportState transport_state) {
    if (gate == nullptr || !gate->requires_fresh_transport) {
        return LegacyReconnectAction::AllowCurrentConnection;
    }

    if (!gate->transport_absence_observed) {
        if (transport_state == LdacTransportState::Unavailable) {
            gate->transport_absence_observed = true;
            return LegacyReconnectAction::WaitForFreshConnection;
        }
        return LegacyReconnectAction::WaitForTransportAbsent;
    }

    if (state == Xm5ConnectionState::Connected &&
        transport_state == LdacTransportState::Ready) {
        gate->requires_fresh_transport = false;
        gate->transport_absence_observed = false;
        return LegacyReconnectAction::AllowCurrentConnection;
    }
    return LegacyReconnectAction::WaitForFreshConnection;
}

ConfigReadResult ReadAgentConfig(const std::wstring& path,
                                 AgentConfig* config,
                                 DWORD* read_error) {
    if (read_error != nullptr) {
        *read_error = ERROR_SUCCESS;
    }
    if (path.empty() || config == nullptr) {
        if (read_error != nullptr) {
            *read_error = ERROR_INVALID_PARAMETER;
        }
        return ConfigReadResult::Invalid;
    }

    std::string json;
    DWORD error = ERROR_SUCCESS;
    if (!ReadSmallFile(path, &json, &error)) {
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return ConfigReadResult::Missing;
        }
        if (read_error != nullptr) {
            *read_error = error;
        }
        return ConfigReadResult::Invalid;
    }
    if (json.size() >= 3u &&
        static_cast<unsigned char>(json[0]) == 0xEFu &&
        static_cast<unsigned char>(json[1]) == 0xBBu &&
        static_cast<unsigned char>(json[2]) == 0xBFu) {
        json.erase(0, 3u);
    }

    std::uint64_t version = 0;
    std::uint64_t revision = 0;
    bool enabled = false;
    std::string quality;
    std::string channel_mode = "stereo";
    std::uint64_t sample_rate = 48000;
    std::uint64_t bits_per_sample = 16;
    if (!ParseJsonUnsigned(json, "version", &version) ||
        (version != 1u && version != 2u && version != 3u) ||
        !ParseJsonUnsigned(json, "revision", &revision) ||
        !ParseJsonBool(json, "enabled", &enabled) ||
        !ParseJsonAsciiString(json, "quality", &quality) ||
        (quality != "mq" && quality != "sq" && quality != "hq" &&
         quality != "auto") ||
        (version >= 2u &&
         (!ParseJsonAsciiString(json, "channel_mode", &channel_mode) ||
          (channel_mode != "stereo" && channel_mode != "dual" &&
           channel_mode != "mono"))) ||
        (version == 3u &&
         (!ParseJsonUnsigned(json, "sample_rate", &sample_rate) ||
          (sample_rate != 44100u && sample_rate != 48000u &&
           sample_rate != 88200u && sample_rate != 96000u) ||
          !ParseJsonUnsigned(json, "bits_per_sample", &bits_per_sample) ||
          (bits_per_sample != 16u && bits_per_sample != 24u)))) {
        if (read_error != nullptr) {
            *read_error = ERROR_INVALID_DATA;
        }
        return ConfigReadResult::Invalid;
    }

    config->enabled = enabled;
    config->quality.assign(quality.begin(), quality.end());
    config->channel_mode.assign(channel_mode.begin(), channel_mode.end());
    config->sample_rate = static_cast<unsigned int>(sample_rate);
    config->bits_per_sample = static_cast<unsigned int>(bits_per_sample);
    config->revision = revision;
    return ConfigReadResult::Loaded;
}

HANDLE CreateKillOnCloseJob() {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        return nullptr;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION information{};
    information.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job,
                                 JobObjectExtendedLimitInformation,
                                 &information,
                                 sizeof(information))) {
        const DWORD error = GetLastError();
        CloseHandle(job);
        SetLastError(error);
        return nullptr;
    }
    return job;
}

}  // namespace native_ldac::agent
