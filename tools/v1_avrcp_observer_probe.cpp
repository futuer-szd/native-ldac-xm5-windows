// SPDX-License-Identifier: Apache-2.0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <initguid.h>
#include <setupapi.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#include "nativeldac_avrcp_observer_ioctl.h"

namespace {

void PrintHelp() {
    std::printf(
        "Usage: v1_avrcp_observer_probe [--duration-seconds N] "
        "[--wait-acl-connect] [--delay-seconds N] "
        "[--verify-same-channel-write]\n"
        "Opens the isolated NativeLdacAvrcpObserver interface, "
        "begins its one-shot observation session when activation is allowed, "
        "prints status, and dequeues observed events.\n"
        "  --wait-acl-connect  Wait for an observed XM5 ACL connect "
        "(generation > 0) before activating.\n"
        "  --delay-seconds N   After the ACL connect, wait N seconds "
        "(0..30) before activating (idle open timing probe).\n"
        "  --verify-same-channel-write  After observing the first XM5 "
        "absolute volume, write the same value back through the same handle "
        "and require its response. This does not change the requested "
        "volume.\n");
}

std::string Utf8FromWide(const wchar_t* text) {
    int required;
    std::string converted;
    if (text == nullptr || *text == L'\0') return converted;
    required = WideCharToMultiByte(CP_UTF8,
                                   0,
                                   text,
                                   -1,
                                   nullptr,
                                   0,
                                   nullptr,
                                   nullptr);
    if (required <= 1) return converted;
    converted.resize(static_cast<size_t>(required));
    if (WideCharToMultiByte(CP_UTF8,
                            0,
                            text,
                            -1,
                            converted.data(),
                            required,
                            nullptr,
                            nullptr) == 0) {
        converted.clear();
    } else {
        converted.pop_back();
    }
    return converted;
}

bool ParseUnsigned(const wchar_t* text, DWORD* value) {
    wchar_t* end = nullptr;
    unsigned long parsed;
    if (text == nullptr || value == nullptr || *text == L'\0') return false;
    parsed = std::wcstoul(text, &end, 10);
    if (end == text || *end != L'\0' || parsed == 0ul || parsed > 3600ul) {
        return false;
    }
    *value = static_cast<DWORD>(parsed);
    return true;
}

bool ParseArguments(int argc,
                     wchar_t** argv,
                     DWORD* duration_seconds,
                     bool* wait_acl,
                     DWORD* delay_seconds,
                     bool* verify_same_channel_write) {
    if (duration_seconds == nullptr || wait_acl == nullptr ||
        delay_seconds == nullptr || verify_same_channel_write == nullptr) {
        return false;
    }
    *duration_seconds = 30u;
    *wait_acl = false;
    *delay_seconds = 0u;
    *verify_same_channel_write = false;
    for (int index = 1; index < argc; ++index) {
        if (std::wcscmp(argv[index], L"--help") == 0 ||
            std::wcscmp(argv[index], L"-h") == 0) {
            PrintHelp();
            return false;
        }
        if (std::wcscmp(argv[index], L"--duration-seconds") == 0 &&
            index + 1 < argc) {
            if (!ParseUnsigned(argv[++index], duration_seconds)) {
                std::fprintf(stderr, "Invalid duration.\n");
                return false;
            }
            continue;
        }
        if (std::wcscmp(argv[index], L"--wait-acl-connect") == 0) {
            *wait_acl = true;
            continue;
        }
        if (std::wcscmp(argv[index], L"--delay-seconds") == 0 &&
            index + 1 < argc) {
            DWORD parsed = 0u;
            wchar_t* end = nullptr;
            unsigned long value = std::wcstoul(argv[++index], &end, 10);
            if (end == argv[index] || *end != L'\0' ||
                value > 30ul) {
                std::fprintf(stderr, "Invalid delay; must be 0..30.\n");
                return false;
            }
            parsed = static_cast<DWORD>(value);
            *delay_seconds = parsed;
            continue;
        }
        if (std::wcscmp(argv[index],
                        L"--verify-same-channel-write") == 0) {
            *verify_same_channel_write = true;
            continue;
        }
        std::fprintf(stderr,
                     "Unknown argument: %s\n",
                     Utf8FromWide(argv[index]).c_str());
        return false;
    }
    return true;
}

ULONG RawWord(const NLD_AVRCP_OBSERVER_EVENT& event,
              unsigned word_index) {
    switch (word_index) {
        case 0u: return event.Value0;
        case 1u: return event.RawPrefixHigh;
        case 2u: return event.RawPrefixHigh2;
        case 3u: return event.RawPrefixHigh3;
        case 4u: return event.RawPrefixHigh4;
        case 5u: return event.RawPrefixHigh5;
        case 6u: return event.RawPrefixHigh6;
        case 7u: return event.RawPrefixHigh7;
        case 8u: return event.RawPrefixHigh8;
        case 9u: return event.RawPrefixHigh9;
        case 10u: return event.RawPrefixHigh10;
        case 11u: return event.RawPrefixHigh11;
        case 12u: return event.RawPrefixHigh12;
        case 13u: return event.RawPrefixHigh13;
        case 14u: return event.RawPrefixHigh14;
        case 15u: return event.RawPrefixHigh15;
        default: return 0u;
    }
}

const char* EventName(ULONG type) {
    switch (type) {
        case NldAvrcpObserverEventAclConnected:
            return "transport-generation-started";
        case NldAvrcpObserverEventAclDisconnected:
            return "transport-generation-ended";
        case NldAvrcpObserverEventVolumeCapability:
            return "volume-capability";
        case NldAvrcpObserverEventAbsoluteVolume:
            return "absolute-volume";
        case NldAvrcpObserverEventPassThrough:
            return "pass-through";
        case NldAvrcpObserverEventProtocolError:
            return "protocol-error";
        case NldAvrcpObserverEventVendorCommand:
            return "vendor-command";
        case NldAvrcpObserverEventWriteResponse:
            return "write-response";
        default:
            return "unknown";
    }
}

bool GetInterfacePath(std::wstring* path) {
    HDEVINFO devices;
    SP_DEVICE_INTERFACE_DATA interface_data{};
    DWORD required = 0u;
    std::vector<BYTE> buffer;
    auto* detail = static_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(nullptr);

    if (path == nullptr) return false;
    devices = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_NATIVE_LDAC_AVRCP_OBSERVER,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) return false;
    interface_data.cbSize = sizeof(interface_data);
    if (!SetupDiEnumDeviceInterfaces(
            devices,
            nullptr,
            &GUID_DEVINTERFACE_NATIVE_LDAC_AVRCP_OBSERVER,
            0u,
            &interface_data)) {
        SetupDiDestroyDeviceInfoList(devices);
        return false;
    }
    (void)SetupDiGetDeviceInterfaceDetailW(
        devices, &interface_data, nullptr, 0u, &required, nullptr);
    if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
        SetupDiDestroyDeviceInfoList(devices);
        return false;
    }
    buffer.resize(required);
    detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
        buffer.data());
    detail->cbSize = sizeof(*detail);
    if (!SetupDiGetDeviceInterfaceDetailW(
            devices,
            &interface_data,
            detail,
            required,
            nullptr,
            nullptr)) {
        SetupDiDestroyDeviceInfoList(devices);
        return false;
    }
    *path = detail->DevicePath;
    if (SetupDiEnumDeviceInterfaces(
            devices,
            nullptr,
            &GUID_DEVINTERFACE_NATIVE_LDAC_AVRCP_OBSERVER,
            1u,
            &interface_data)) {
        path->clear();
    }
    SetupDiDestroyDeviceInfoList(devices);
    return !path->empty();
}

bool Ioctl(HANDLE handle,
           DWORD code,
           void* output,
           DWORD output_size,
           DWORD* bytes) {
    return DeviceIoControl(handle,
                           code,
                           nullptr,
                           0u,
                           output,
                           output_size,
                           bytes,
                           nullptr) != FALSE;
}

bool SubmitSameChannelVolumeWrite(HANDLE handle, UCHAR absolute_volume) {
    NLD_AVRCP_OBSERVER_WRITE_REQUEST request{};
    DWORD bytes = 0u;
    request.Size = sizeof(request);
    request.PduId = 0x50u;
    request.Response = 0u;
    request.ParameterSize = 1u;
    request.Parameters[0] = absolute_volume;
    return DeviceIoControl(handle,
                           IOCTL_NLD_AVRCP_OBSERVER_SEND_COMMAND,
                           &request,
                           sizeof(request),
                           nullptr,
                           0u,
                           &bytes,
                           nullptr) != FALSE;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    DWORD duration_seconds;
    bool wait_acl;
    DWORD delay_seconds;
    bool verify_same_channel_write;
    std::wstring interface_path;
    HANDLE handle;
    NLD_AVRCP_OBSERVER_ABI_VERSION version{};
    NLD_AVRCP_OBSERVER_STATUS status{};
    DWORD bytes = 0u;
    ULONGLONG deadline;
    ULONGLONG first_timestamp;
    ULONGLONG activation_tick = 0ull;
    ULONGLONG channel_held_tick = 0ull;
    ULONGLONG channel_released_tick = 0ull;
    bool same_channel_write_submitted = false;
    bool same_channel_write_responded = false;
    UCHAR same_channel_write_value = 0u;

    if (!ParseArguments(
            argc,
            argv,
            &duration_seconds,
            &wait_acl,
            &delay_seconds,
            &verify_same_channel_write)) {
        return argc > 1 &&
                (std::wcscmp(argv[1], L"--help") == 0 ||
                 std::wcscmp(argv[1], L"-h") == 0)
            ? 0
            : 2;
    }
    if (!GetInterfacePath(&interface_path)) {
        std::fprintf(stderr,
                     "Exactly one present Native AVRCP observer interface "
                     "is required.\n");
        return 3;
    }
    handle = CreateFileW(interface_path.c_str(),
                         verify_same_channel_write
                             ? (GENERIC_READ | GENERIC_WRITE)
                             : GENERIC_READ,
                         verify_same_channel_write ? 0u : FILE_SHARE_READ,
                         nullptr,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr,
                     "Observer interface open failed (Win32 %lu).\n",
                     GetLastError());
        return 4;
    }
    if (!Ioctl(handle,
               IOCTL_NLD_AVRCP_OBSERVER_GET_VERSION,
               &version,
               sizeof(version),
               &bytes) ||
        bytes != sizeof(version) ||
        version.Major != NLD_AVRCP_OBSERVER_ABI_MAJOR ||
        version.Minor != NLD_AVRCP_OBSERVER_ABI_MINOR) {
        std::fprintf(stderr, "Observer ABI query failed.\n");
        CloseHandle(handle);
        return 5;
    }
    if (!Ioctl(handle,
               IOCTL_NLD_AVRCP_OBSERVER_GET_STATUS,
               &status,
               sizeof(status),
               &bytes) ||
        bytes != sizeof(status)) {
        std::fprintf(stderr, "Observer status query failed.\n");
        CloseHandle(handle);
        return 6;
    }
    std::printf("Interface: %s\n",
                Utf8FromWide(interface_path.c_str()).c_str());
    std::printf("ABI: %lu.%lu; flags 0x%08lX; generation %llu; "
                "queue %lu; dropped %lu; protocol 0x%08lX; "
                "open 0x%08lX; close 0x%08lX; "
                "outbound=%u pending=%u held=%u remote-disconnected=%u\n",
                version.Major,
                version.Minor,
                status.Flags,
                status.AclGeneration,
                status.QueueDepth,
                status.DroppedEvents,
                static_cast<ULONG>(status.LastProtocolStatus),
                static_cast<ULONG>(status.LastOpenStatus),
                static_cast<ULONG>(status.LastCloseStatus),
                (status.Flags & NLD_AVRCP_OBSERVER_STATUS_OUTBOUND_OPEN)
                    != 0u,
                (status.Flags & NLD_AVRCP_OBSERVER_STATUS_OPEN_PENDING)
                    != 0u,
                (status.Flags & NLD_AVRCP_OBSERVER_STATUS_CHANNEL_HELD)
                    != 0u,
                (status.Flags &
                    NLD_AVRCP_OBSERVER_STATUS_REMOTE_DISCONNECTED) != 0u);
    if (wait_acl) {
        const ULONGLONG initial_generation = status.AclGeneration;
        std::printf(
            "Waiting for a new XM5 ACL connect (current generation %llu)...\n",
            initial_generation);
        ULONGLONG acl_deadline = GetTickCount64() + 120000ull;
        while (GetTickCount64() < acl_deadline) {
            NLD_AVRCP_OBSERVER_STATUS current{};
            bytes = 0u;
            if (Ioctl(handle,
                      IOCTL_NLD_AVRCP_OBSERVER_GET_STATUS,
                      &current,
                      sizeof(current),
                      &bytes) &&
                bytes == sizeof(current) &&
                current.AclGeneration != 0u &&
                current.AclGeneration != initial_generation) {
                status = current;
                break;
            }
            Sleep(250u);
        }
        if (status.AclGeneration == initial_generation) {
            std::fprintf(stderr,
                         "No new XM5 ACL connect was observed in time.\n");
            CloseHandle(handle);
            return 10;
        }
        std::printf("XM5 ACL connect observed (generation %llu).\n",
                    status.AclGeneration);
    }
    if (delay_seconds != 0u) {
        std::printf("Delaying activation by %lu second(s)...\n",
                    delay_seconds);
        Sleep(delay_seconds * 1000u);
        bytes = 0u;
        if (Ioctl(handle,
                  IOCTL_NLD_AVRCP_OBSERVER_GET_STATUS,
                  &status,
                  sizeof(status),
                  &bytes) &&
            bytes == sizeof(status)) {
            std::printf("status after delay: open 0x%08lX protocol 0x%08lX "
                        "held=%u pending=%u\n",
                        static_cast<ULONG>(status.LastOpenStatus),
                        static_cast<ULONG>(status.LastProtocolStatus),
                        (status.Flags &
                            NLD_AVRCP_OBSERVER_STATUS_CHANNEL_HELD) != 0u,
                        (status.Flags &
                            NLD_AVRCP_OBSERVER_STATUS_OPEN_PENDING) != 0u);
        }
    }
    if ((status.Flags &
            NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUIRED) != 0u) {
        bytes = 0u;
        if (!Ioctl(handle,
                   IOCTL_NLD_AVRCP_OBSERVER_BEGIN_OBSERVATION,
                   nullptr,
                   0u,
                   &bytes)) {
            std::fprintf(stderr,
                         "Observer observation activation failed (Win32 %lu).\n",
                         GetLastError());
            CloseHandle(handle);
            return 7;
        }
        std::printf("Observation activation accepted; one outbound AVCTP "
                    "OPEN is now in progress.\n");
        activation_tick = GetTickCount64();
        bytes = 0u;
        if (Ioctl(handle,
                  IOCTL_NLD_AVRCP_OBSERVER_GET_STATUS,
                  &status,
                  sizeof(status),
                  &bytes) &&
            bytes == sizeof(status)) {
            std::printf("post-activation status: open 0x%08lX "
                        "protocol 0x%08lX held=%u pending=%u\n",
                        static_cast<ULONG>(status.LastOpenStatus),
                        static_cast<ULONG>(status.LastProtocolStatus),
                        (status.Flags &
                            NLD_AVRCP_OBSERVER_STATUS_CHANNEL_HELD) != 0u,
                        (status.Flags &
                            NLD_AVRCP_OBSERVER_STATUS_OPEN_PENDING) != 0u);
        }
    }

    deadline = GetTickCount64() +
        static_cast<ULONGLONG>(duration_seconds) * 1000ull;
    first_timestamp = 0ull;
    while (GetTickCount64() < deadline) {
        NLD_AVRCP_OBSERVER_EVENT event{};
        bytes = 0u;
        if (Ioctl(handle,
                  IOCTL_NLD_AVRCP_OBSERVER_DEQUEUE_EVENT,
                  &event,
                  sizeof(event),
                  &bytes)) {
            if (bytes != sizeof(event)) {
                std::fprintf(stderr, "Short observer event.\n");
                CloseHandle(handle);
                return 8;
            }
            ULONG raw_length = (event.Flags & NLD_AVRCP_EVENT_RAW_LENGTH_MASK) >>
                NLD_AVRCP_EVENT_RAW_LENGTH_SHIFT;
            ULONG packet_size = (event.Flags & NLD_AVRCP_EVENT_PACKET_SIZE_MASK) >>
                NLD_AVRCP_EVENT_PACKET_SIZE_SHIFT;
            ULONG parse_stage = (event.Flags & NLD_AVRCP_EVENT_PARSE_STAGE_MASK) >>
                NLD_AVRCP_EVENT_PARSE_STAGE_SHIFT;
            ULONGLONG dt_ms;
            if (first_timestamp == 0ull) {
                first_timestamp = event.Timestamp100ns;
            }
            dt_ms = (event.Timestamp100ns - first_timestamp) / 10000ull;
            std::printf("event sequence=%llu dt=%llums generation=%llu type=%s "
                        "flags=0x%08lX value=0x%08lX status=0x%08lX "
                        "stage=%lu pkt=%lu",
                        event.Sequence,
                        dt_ms,
                        event.AclGeneration,
                        EventName(event.Type),
                        event.Flags,
                        event.Value0,
                        static_cast<ULONG>(event.ProtocolStatus),
                        parse_stage,
                        packet_size);
            if (event.Type == NldAvrcpObserverEventVendorCommand ||
                event.Type == NldAvrcpObserverEventWriteResponse) {
                std::printf(" pdu=0x%02lX params=0x%08lX 0x%08lX",
                            event.Value0 & 0xFFu,
                            event.RawPrefixHigh,
                            event.RawPrefixHigh2);
                if (event.Type == NldAvrcpObserverEventWriteResponse) {
                    std::printf(" response=0x%02lX",
                                static_cast<ULONG>(
                                    event.ProtocolStatus) & 0xFFu);
                }
            }
            if ((event.Flags & NLD_AVRCP_EVENT_FLAG_RAW_PREFIX) != 0u &&
                raw_length != 0u && raw_length <= 64u) {
                unsigned raw_index;
                std::printf(" raw=%lu/", raw_length);
                for (raw_index = 0u; raw_index < raw_length; ++raw_index) {
                    ULONG raw_byte = (RawWord(event, raw_index / 4u) >>
                        ((raw_index % 4u) * 8u)) & 0xFFu;
                    std::printf("%s%02lX",
                                raw_index == 0u ? "" : " ",
                                raw_byte);
                }
            }
            std::printf("\n");
            if (verify_same_channel_write &&
                !same_channel_write_submitted &&
                event.Type == NldAvrcpObserverEventAbsoluteVolume) {
                same_channel_write_value =
                    static_cast<UCHAR>(event.Value0 & 0x7Fu);
                if (!SubmitSameChannelVolumeWrite(
                        handle, same_channel_write_value)) {
                    std::fprintf(
                        stderr,
                        "Same-channel volume write submission failed "
                        "(Win32 %lu).\n",
                        GetLastError());
                    CloseHandle(handle);
                    return 11;
                }
                same_channel_write_submitted = true;
                std::printf(
                    "same-channel write submitted value=%u pdu=0x50\n",
                    static_cast<unsigned>(same_channel_write_value));
            }
            if (verify_same_channel_write &&
                event.Type == NldAvrcpObserverEventWriteResponse &&
                (event.Value0 & 0xFFu) == 0x50u &&
                (static_cast<ULONG>(event.ProtocolStatus) & 0xFFu) == 0x09u &&
                (event.RawPrefixHigh & 0x7Fu) ==
                    same_channel_write_value) {
                same_channel_write_responded = true;
                std::printf(
                    "same-channel write response value=%u response=0x%02lX\n",
                    static_cast<unsigned>(same_channel_write_value),
                    static_cast<ULONG>(event.ProtocolStatus) & 0xFFu);
            }
            continue;
        }
        if (GetLastError() != ERROR_NO_MORE_ITEMS) {
            std::fprintf(stderr,
                         "Observer dequeue failed (Win32 %lu).\n",
                         GetLastError());
            CloseHandle(handle);
            return 9;
        }
        NLD_AVRCP_OBSERVER_STATUS current{};
        bytes = 0u;
        if (Ioctl(handle,
                  IOCTL_NLD_AVRCP_OBSERVER_GET_STATUS,
                  &current,
                  sizeof(current),
                  &bytes) &&
            bytes == sizeof(current)) {
            const bool held =
                (current.Flags &
                    NLD_AVRCP_OBSERVER_STATUS_CHANNEL_HELD) != 0u;
            if (held && channel_held_tick == 0ull) {
                channel_held_tick = GetTickCount64();
                std::printf(
                    "channel-held at +%llums after activation\n",
                    activation_tick == 0ull
                        ? 0ull
                        : channel_held_tick - activation_tick);
            } else if (!held && channel_held_tick != 0ull &&
                       channel_released_tick == 0ull) {
                channel_released_tick = GetTickCount64();
                std::printf(
                    "channel-released after %llums held\n",
                    channel_released_tick - channel_held_tick);
            }
        }
        Sleep(50u);
    }
    const ULONGLONG finish_tick = GetTickCount64();
    if (channel_held_tick != 0ull) {
        const ULONGLONG held_until =
            channel_released_tick == 0ull ? finish_tick : channel_released_tick;
        std::printf("channel hold summary: held_ms=%llu released=%u\n",
                    held_until - channel_held_tick,
                    channel_released_tick != 0ull);
    } else {
        std::printf("channel hold summary: held_ms=0 released=0\n");
    }
    std::printf(
        "same-channel write summary: requested=%u submitted=%u "
        "responded=%u value=%u\n",
        verify_same_channel_write,
        same_channel_write_submitted,
        same_channel_write_responded,
        static_cast<unsigned>(same_channel_write_value));
    bytes = 0u;
    if (Ioctl(handle,
              IOCTL_NLD_AVRCP_OBSERVER_GET_STATUS,
              &status,
              sizeof(status),
              &bytes) &&
        bytes == sizeof(status)) {
        std::printf("final status: flags 0x%08lX; generation %llu; "
                    "queue %lu; dropped %lu; protocol 0x%08lX; "
                    "open 0x%08lX; close 0x%08lX; "
                    "remote-disconnected=%u held=%u\n",
                    status.Flags,
                    status.AclGeneration,
                    status.QueueDepth,
                    status.DroppedEvents,
                    static_cast<ULONG>(status.LastProtocolStatus),
                    static_cast<ULONG>(status.LastOpenStatus),
                    static_cast<ULONG>(status.LastCloseStatus),
                    (status.Flags &
                        NLD_AVRCP_OBSERVER_STATUS_REMOTE_DISCONNECTED) != 0u,
                    (status.Flags &
                        NLD_AVRCP_OBSERVER_STATUS_CHANNEL_HELD) != 0u);
    }
    CloseHandle(handle);
    std::printf(
        "Observation completed; no endpoint or media-session operation "
        "was issued.\n");
    if (verify_same_channel_write &&
        (!same_channel_write_submitted || !same_channel_write_responded)) {
        return 12;
    }
    return 0;
}
