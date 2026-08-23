// SPDX-License-Identifier: Apache-2.0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cwchar>

#include "v1_avrcp_filter_decoder.h"
#include "nativeldac_avrcp_filter_ioctl.h"

namespace {

using native_ldac::agent::V1AvrcpFilterDecodePacket;
using native_ldac::agent::V1AvrcpFilterDetectLayout;
using native_ldac::agent::V1AvrcpFilterPayloadLayout;

void PrintHelp() {
    std::printf(
        "Usage: v1_avrcp_filter_probe [--duration-seconds N] "
        "[--wait-for-first-request-seconds N]\n"
        "Reads the exact-XM5 AVRCP upper-filter trace. It cannot write "
        "or activate AVRCP.\n");
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
                    DWORD* first_request_timeout_seconds) {
    *duration_seconds = 20u;
    *first_request_timeout_seconds = 0u;
    for (int index = 1; index < argc; ++index) {
        if (std::wcscmp(argv[index], L"--help") == 0 ||
            std::wcscmp(argv[index], L"-h") == 0) {
            PrintHelp();
            return false;
        }
        if (std::wcscmp(argv[index], L"--duration-seconds") == 0 &&
            index + 1 < argc) {
            if (!ParseUnsigned(argv[++index], duration_seconds)) return false;
            continue;
        }
        if (std::wcscmp(
                argv[index],
                L"--wait-for-first-request-seconds") == 0 &&
            index + 1 < argc) {
            if (!ParseUnsigned(
                    argv[++index],
                    first_request_timeout_seconds)) {
                return false;
            }
            continue;
        }
        return false;
    }
    return true;
}

const char* EventName(ULONG type) {
    switch (type) {
        case NldAvrcpFilterEventLifecycle: return "lifecycle";
        case NldAvrcpFilterEventRequest: return "request";
        case NldAvrcpFilterEventCompletion: return "completion";
        case NldAvrcpFilterEventCaptureFailure: return "capture-failure";
        default: return "unknown";
    }
}

const char* DecodedKindName(avrcp_observer_event_kind kind) {
    switch (kind) {
        case AVRCP_OBSERVER_EVENT_VOLUME_CAPABILITY:
            return "volume-capability";
        case AVRCP_OBSERVER_EVENT_VOLUME_CHANGED:
            return "volume-changed";
        case AVRCP_OBSERVER_EVENT_PASS_THROUGH:
            return "pass-through";
        case AVRCP_OBSERVER_EVENT_VENDOR_COMMAND:
            return "vendor-command";
        case AVRCP_OBSERVER_EVENT_WRITE_RESPONSE:
            return "write-response";
        case AVRCP_OBSERVER_EVENT_PROTOCOL_ERROR:
            return "protocol-error";
        default:
            return "unknown";
    }
}

void PrintDecodedEvent(ULONGLONG sequence,
                       const avrcp_observer_event& event) {
    std::printf("decoded sequence=%llu kind=%s",
                sequence,
                DecodedKindName(event.kind));
    if (event.kind == AVRCP_OBSERVER_EVENT_VOLUME_CHANGED) {
        std::printf(" volume=%u response=0x%02X",
                    event.absolute_volume,
                    event.response_code);
    } else if (event.kind == AVRCP_OBSERVER_EVENT_PASS_THROUGH) {
        std::printf(" operation=0x%02X released=%u response=0x%02X",
                    event.operation_id,
                    event.released != 0u ? 1u : 0u,
                    event.response_code);
    } else if (event.kind == AVRCP_OBSERVER_EVENT_VOLUME_CAPABILITY) {
        std::printf(" supported=%u",
                    event.volume_supported != 0u ? 1u : 0u);
    } else if (event.kind == AVRCP_OBSERVER_EVENT_VENDOR_COMMAND ||
               event.kind == AVRCP_OBSERVER_EVENT_WRITE_RESPONSE) {
        std::printf(" pdu=0x%02X response=0x%02X",
                    event.pdu_id,
                    event.response_code);
        if (event.parameter_size >= 1u) {
            std::printf(" params=0x%02X", event.parameter_bytes[0]);
        }
    }
    std::printf("\n");
    std::fflush(stdout);
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

}  // namespace

int wmain(int argc, wchar_t** argv) {
    DWORD duration_seconds;
    DWORD first_request_timeout_seconds;
    HANDLE handle;
    DWORD bytes;
    NLD_AVRCP_FILTER_ABI_VERSION version{};
    NLD_AVRCP_FILTER_STATUS status{};
    ULONGLONG deadline;
    ULONGLONG first_request_deadline;
    bool first_request_observed;
    ULONGLONG window_requests;
    ULONGLONG window_completions;
    ULONGLONG window_capture_failures;
    ULONGLONG window_decoded_capability;
    ULONGLONG window_decoded_volume_changed;
    ULONGLONG window_decoded_pass_through;
    ULONGLONG window_decoded_vendor_command;
    ULONGLONG window_decoded_protocol_error;
    if (!ParseArguments(argc,
                        argv,
                        &duration_seconds,
                        &first_request_timeout_seconds)) {
        return argc > 1 &&
                (std::wcscmp(argv[1], L"--help") == 0 ||
                 std::wcscmp(argv[1], L"-h") == 0)
            ? 0
            : 2;
    }
    handle = CreateFileW(L"\\\\.\\NativeLdacAvrcpIoFilter",
                         GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr,
                     "AVRCP filter control open failed (Win32 %lu).\n",
                     GetLastError());
        return 3;
    }
    bytes = 0u;
    if (!Ioctl(handle,
               IOCTL_NLD_AVRCP_FILTER_GET_VERSION,
               &version,
               sizeof(version),
               &bytes) ||
        bytes != sizeof(version) ||
        version.Major != NLD_AVRCP_FILTER_ABI_MAJOR ||
        version.Minor != NLD_AVRCP_FILTER_ABI_MINOR) {
        std::fprintf(stderr, "AVRCP filter ABI query failed.\n");
        CloseHandle(handle);
        return 4;
    }
    bytes = 0u;
    if (!Ioctl(handle,
               IOCTL_NLD_AVRCP_FILTER_GET_STATUS,
               &status,
               sizeof(status),
               &bytes) ||
        bytes != sizeof(status)) {
        std::fprintf(stderr, "AVRCP filter status query failed.\n");
        CloseHandle(handle);
        return 5;
    }
    std::printf(
        "ABI: %lu.%lu; flags 0x%08lX; queue %lu; dropped %lu; "
        "requests %llu; completions %llu; capture-failures %llu\n",
        version.Major,
        version.Minor,
        status.Flags,
        status.QueueDepth,
        status.DroppedEvents,
        status.RequestsObserved,
        status.CompletionsObserved,
        status.CaptureFailures);
    window_requests = 0ull;
    window_completions = 0ull;
    window_capture_failures = 0ull;
    window_decoded_capability = 0ull;
    window_decoded_volume_changed = 0ull;
    window_decoded_pass_through = 0ull;
    window_decoded_vendor_command = 0ull;
    window_decoded_protocol_error = 0ull;
    first_request_observed = first_request_timeout_seconds == 0u;
    deadline = first_request_observed
        ? GetTickCount64() +
            static_cast<ULONGLONG>(duration_seconds) * 1000ull
        : 0ull;
    first_request_deadline = first_request_observed
        ? 0ull
        : GetTickCount64() +
            static_cast<ULONGLONG>(first_request_timeout_seconds) * 1000ull;
    if (!first_request_observed) {
        for (;;) {
            NLD_AVRCP_FILTER_EVENT stale_event{};
            bytes = 0u;
            if (Ioctl(handle,
                      IOCTL_NLD_AVRCP_FILTER_DEQUEUE_EVENT,
                      &stale_event,
                      sizeof(stale_event),
                      &bytes)) {
                continue;
            }
            if (GetLastError() != ERROR_NO_MORE_ITEMS) {
                std::fprintf(stderr,
                             "AVRCP filter pre-arm drain failed "
                             "(Win32 %lu).\n",
                             GetLastError());
                CloseHandle(handle);
                return 7;
            }
            break;
        }
        std::printf(
            "AVRCP filter trace watcher armed; waiting up to %lu seconds "
            "for the first request.\n",
            first_request_timeout_seconds);
        std::fflush(stdout);
    }
    while ((!first_request_observed &&
            GetTickCount64() < first_request_deadline) ||
           (first_request_observed && GetTickCount64() < deadline)) {
        NLD_AVRCP_FILTER_EVENT event{};
        bytes = 0u;
        if (Ioctl(handle,
                  IOCTL_NLD_AVRCP_FILTER_DEQUEUE_EVENT,
                  &event,
                  sizeof(event),
                  &bytes)) {
            ULONG index;
            if (bytes != sizeof(event)) {
                std::fprintf(stderr, "Short AVRCP filter event.\n");
                CloseHandle(handle);
                return 6;
            }
            std::printf(
                "event sequence=%llu request=%llu type=%s flags=0x%08lX "
                "ioctl=0x%08lX input=%lu output=%lu status=0x%08lX "
                "information=%llu raw=%lu",
                event.Sequence,
                event.RequestId,
                EventName(event.Type),
                event.Flags,
                event.ControlCode,
                event.InputSize,
                event.OutputSize,
                static_cast<ULONG>(event.Status),
                event.Information,
                event.RawSize);
            for (index = 0u; index < event.RawSize; ++index) {
                std::printf("%s%02X",
                            index == 0u ? "/" : " ",
                            event.RawPrefix[index]);
            }
            std::printf("\n");
            std::fflush(stdout);
            const auto layout = V1AvrcpFilterDetectLayout(
                event.RawPrefix, event.RawSize);
            if (layout != V1AvrcpFilterPayloadLayout::None) {
                avrcp_observer_event decoded{};
                const avrcp_status decode_status =
                    V1AvrcpFilterDecodePacket(
                        event.RawPrefix,
                        event.RawSize,
                        layout,
                        &decoded);
                if (decode_status == AVRCP_OK) {
                    switch (decoded.kind) {
                        case AVRCP_OBSERVER_EVENT_VOLUME_CAPABILITY:
                            ++window_decoded_capability;
                            break;
                        case AVRCP_OBSERVER_EVENT_VOLUME_CHANGED:
                            ++window_decoded_volume_changed;
                            break;
                        case AVRCP_OBSERVER_EVENT_PASS_THROUGH:
                            ++window_decoded_pass_through;
                            break;
                        case AVRCP_OBSERVER_EVENT_VENDOR_COMMAND:
                        case AVRCP_OBSERVER_EVENT_WRITE_RESPONSE:
                            ++window_decoded_vendor_command;
                            break;
                        default:
                            break;
                    }
                    PrintDecodedEvent(event.Sequence, decoded);
                } else {
                    ++window_decoded_protocol_error;
                    std::printf(
                        "decoded sequence=%llu kind=protocol-error "
                        "status=%d\n",
                        event.Sequence,
                        static_cast<int>(decode_status));
                    std::fflush(stdout);
                }
            }
            if (event.Type == NldAvrcpFilterEventRequest) {
                ++window_requests;
            } else if (event.Type == NldAvrcpFilterEventCompletion) {
                ++window_completions;
            } else if (event.Type == NldAvrcpFilterEventCaptureFailure) {
                ++window_capture_failures;
            }
            if (!first_request_observed &&
                event.Type == NldAvrcpFilterEventRequest) {
                first_request_observed = true;
                deadline = GetTickCount64() +
                    static_cast<ULONGLONG>(duration_seconds) * 1000ull;
                std::printf(
                    "First AVRCP filter request observed; collecting for "
                    "%lu seconds.\n",
                    duration_seconds);
                std::fflush(stdout);
            }
            continue;
        }
        if (GetLastError() != ERROR_NO_MORE_ITEMS) {
            std::fprintf(stderr,
                         "AVRCP filter dequeue failed (Win32 %lu).\n",
                         GetLastError());
            CloseHandle(handle);
            return 7;
        }
        Sleep(25u);
    }
    const bool first_request_wait_expired = !first_request_observed;
    if (first_request_wait_expired) {
        std::fprintf(stderr,
                     "No AVRCP filter request was observed before the "
                     "bounded wait expired.\n");
    }
    bytes = 0u;
    if (Ioctl(handle,
              IOCTL_NLD_AVRCP_FILTER_GET_STATUS,
              &status,
              sizeof(status),
              &bytes) &&
        bytes == sizeof(status)) {
        std::printf(
            "final status: flags 0x%08lX; queue %lu; dropped %lu; "
            "requests %llu; completions %llu; capture-failures %llu; "
            "last-status 0x%08lX\n",
            status.Flags,
            status.QueueDepth,
            status.DroppedEvents,
            status.RequestsObserved,
            status.CompletionsObserved,
            status.CaptureFailures,
            static_cast<ULONG>(status.LastCompletionStatus));
    }
    std::printf(
        "window status: requests %llu; completions %llu; "
        "capture-failures %llu\n",
        window_requests,
        window_completions,
        window_capture_failures);
    std::printf(
        "decoded status: capability=%llu; volume-changed=%llu; "
        "pass-through=%llu; vendor-command=%llu; protocol-error=%llu\n",
        window_decoded_capability,
        window_decoded_volume_changed,
        window_decoded_pass_through,
        window_decoded_vendor_command,
        window_decoded_protocol_error);
    std::fflush(stdout);
    CloseHandle(handle);
    if (first_request_wait_expired) {
        return 8;
    }
    std::printf("Read-only AVRCP filter observation completed.\n");
    return 0;
}
