// SPDX-License-Identifier: Apache-2.0
#define NOMINMAX
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

#include "ldac_native/avdtp.h"
#include "v1_transport_session.h"

#ifndef V1_TRANSPORT_DISCOVERY_MOCK_BACKEND
#include "v1_transport_driver_backend.h"
#endif

namespace {

struct Options {
    const wchar_t* ready_event = nullptr;
    const wchar_t* stop_event = nullptr;
    const wchar_t* transport_open_event = nullptr;
    const wchar_t* capabilities_discovered_event = nullptr;
    const wchar_t* media_started_event = nullptr;
    const wchar_t* media_stopped_event = nullptr;
    const wchar_t* media_failed_event = nullptr;
    const wchar_t* retryable_open_failure_event = nullptr;
    const wchar_t* graceful_transport_stop_event = nullptr;
    const wchar_t* cancel_transport_event = nullptr;
    const wchar_t* session_result = nullptr;
    std::uint64_t session_generation = 0u;
    const wchar_t* quality = L"hq";
    const wchar_t* channel_mode = L"stereo";
};

bool ParseValue(int argc,
                wchar_t** argv,
                int* index,
                const wchar_t** value) {
    if (*index + 1 >= argc || argv[*index + 1][0] == L'\0') {
        return false;
    }
    *value = argv[++(*index)];
    return true;
}

bool ParseOptions(int argc, wchar_t** argv, Options* options) {
    if (options == nullptr) {
        return false;
    }
    for (int index = 1; index < argc; ++index) {
        const wchar_t* argument = argv[index];
        const wchar_t** destination = nullptr;
        if (std::wcscmp(argument, L"--ready-event") == 0) {
            destination = &options->ready_event;
        } else if (std::wcscmp(argument, L"--stop-event") == 0) {
            destination = &options->stop_event;
        } else if (std::wcscmp(
                       argument, L"--transport-open-event") == 0) {
            destination = &options->transport_open_event;
        } else if (std::wcscmp(
                       argument,
                       L"--capabilities-discovered-event") == 0) {
            destination = &options->capabilities_discovered_event;
        } else if (std::wcscmp(
                       argument, L"--media-started-event") == 0) {
            destination = &options->media_started_event;
        } else if (std::wcscmp(
                       argument, L"--media-stopped-event") == 0) {
            destination = &options->media_stopped_event;
        } else if (std::wcscmp(
                       argument, L"--media-failed-event") == 0) {
            destination = &options->media_failed_event;
        } else if (std::wcscmp(
                       argument,
                       L"--retryable-open-failure-event") == 0) {
            destination = &options->retryable_open_failure_event;
        } else if (std::wcscmp(
                       argument,
                       L"--graceful-transport-stop-event") == 0) {
            destination = &options->graceful_transport_stop_event;
        } else if (std::wcscmp(
                       argument, L"--cancel-transport-event") == 0) {
            destination = &options->cancel_transport_event;
        } else if (std::wcscmp(argument, L"--session-result") == 0) {
            destination = &options->session_result;
        } else if (std::wcscmp(argument, L"--session-generation") == 0) {
            const wchar_t* value = nullptr;
            if (!ParseValue(argc, argv, &index, &value)) {
                return false;
            }
            for (const wchar_t* digit = value; *digit != L'\0'; ++digit) {
                if (*digit < L'0' || *digit > L'9') {
                    return false;
                }
            }
            wchar_t* end = nullptr;
            const unsigned long long parsed = _wcstoui64(value, &end, 10);
            if (parsed == 0u || end == value || *end != L'\0') {
                return false;
            }
            options->session_generation =
                static_cast<std::uint64_t>(parsed);
            continue;
        } else if (std::wcscmp(argument, L"--quality") == 0) {
            if (!ParseValue(argc, argv, &index, &options->quality) ||
                (_wcsicmp(options->quality, L"hq") != 0 &&
                 _wcsicmp(options->quality, L"sq") != 0 &&
                 _wcsicmp(options->quality, L"mq") != 0)) {
                return false;
            }
            continue;
        } else if (std::wcscmp(argument, L"--channel-mode") == 0) {
            if (!ParseValue(argc, argv, &index, &options->channel_mode) ||
                (_wcsicmp(options->channel_mode, L"stereo") != 0 &&
                 _wcsicmp(options->channel_mode, L"dual") != 0 &&
                 _wcsicmp(options->channel_mode, L"mono") != 0)) {
                return false;
            }
            continue;
        } else {
            return false;
        }
        if (!ParseValue(argc, argv, &index, destination)) {
            return false;
        }
    }
    return options->ready_event != nullptr &&
           options->stop_event != nullptr &&
           options->transport_open_event != nullptr &&
           options->capabilities_discovered_event != nullptr &&
           options->media_started_event != nullptr &&
           options->media_stopped_event != nullptr &&
           options->media_failed_event != nullptr &&
           options->retryable_open_failure_event != nullptr &&
           options->graceful_transport_stop_event != nullptr &&
           options->cancel_transport_event != nullptr &&
           options->session_result != nullptr &&
           options->session_generation != 0u;
}

void PrintUsage() {
    std::wprintf(
        L"Usage: v1_transport_discovery_worker.exe "
        L"--ready-event <name> --stop-event <name> "
        L"--transport-open-event <name> "
        L"--capabilities-discovered-event <name> "
        L"--media-started-event <name> --media-stopped-event <name> "
        L"--media-failed-event <name> "
        L"--retryable-open-failure-event <name> "
        L"--graceful-transport-stop-event <name> "
        L"--cancel-transport-event <name> --session-result <path> "
        L"--session-generation <positive-integer> [--quality hq|sq|mq] "
        L"[--channel-mode stereo|dual|mono]\n");
}

HANDLE OpenSignal(const wchar_t* name) {
    return OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
}

HANDLE OpenWait(const wchar_t* name) {
    return OpenEventW(SYNCHRONIZE, FALSE, name);
}

void CloseIfValid(HANDLE value) {
    if (value != nullptr) {
        CloseHandle(value);
    }
}

bool IsCancelled(void* context) {
    const HANDLE stop = static_cast<HANDLE>(context);
    return stop != nullptr &&
           WaitForSingleObject(stop, 0u) == WAIT_OBJECT_0;
}

bool IsStrictlyRetryableOpenFailure(
    const native_ldac::agent::V1TransportDiscoveryResult& result) {
    using native_ldac::agent::V1TransportDiscoveryDisposition;
    using native_ldac::agent::V1TransportDiscoveryStage;
    return result.disposition ==
               V1TransportDiscoveryDisposition::BackendFailure &&
           result.primary_disposition ==
               V1TransportDiscoveryDisposition::BackendFailure &&
           result.stage == V1TransportDiscoveryStage::OpenSignaling &&
           result.backend_error == ERROR_REQ_NOT_ACCEP &&
           result.open_attempts == 1u &&
           result.signaling_exchanges == 0u &&
           !result.signaling_opened;
}

const char* DispositionName(
    native_ldac::agent::V1TransportDiscoveryDisposition disposition) {
    using native_ldac::agent::V1TransportDiscoveryDisposition;
    switch (disposition) {
        case V1TransportDiscoveryDisposition::Succeeded:
            return "succeeded";
        case V1TransportDiscoveryDisposition::Cancelled:
            return "cancelled";
        case V1TransportDiscoveryDisposition::InvalidConfiguration:
            return "invalid-configuration";
        case V1TransportDiscoveryDisposition::BackendFailure:
            return "backend-failure";
        case V1TransportDiscoveryDisposition::ProtocolFailure:
            return "protocol-failure";
        case V1TransportDiscoveryDisposition::CleanupFailure:
            return "cleanup-failure";
        default:
            return "unknown";
    }
}

bool WriteResult(
    const wchar_t* path,
    const native_ldac::agent::V1TransportDiscoveryResult& result) {
    if (path == nullptr || path[0] == L'\0') {
        return false;
    }
    const std::wstring temporary = std::wstring(path) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(),
                              GENERIC_WRITE,
                              0u,
                              nullptr,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    char json[2048] = {};
    const int length = sprintf_s(
        json,
        "{\r\n"
        "  \"schema_version\": 2,\r\n"
        "  \"disposition\": \"%s\",\r\n"
        "  \"disposition_code\": %u,\r\n"
        "  \"primary_disposition_code\": %u,\r\n"
        "  \"stage\": %u,\r\n"
        "  \"protocol_error\": %u,\r\n"
        "  \"backend_error\": %u,\r\n"
        "  \"cleanup_error\": %u,\r\n"
        "  \"remote_reject_error\": %u,\r\n"
        "  \"remote_seid\": %u,\r\n"
        "  \"sample_rate_hz\": %u,\r\n"
        "  \"channel_mode\": %u,\r\n"
        "  \"open_attempts\": %u,\r\n"
        "  \"signaling_exchanges\": %u,\r\n"
        "  \"sink_candidates\": %u,\r\n"
        "  \"legacy_capability_fallbacks\": %u,\r\n"
        "  \"signaling_opened\": %s,\r\n"
        "  \"close_attempted\": %s,\r\n"
        "  \"close_succeeded\": %s,\r\n"
        "  \"strictly_retryable_open_failure\": %s\r\n"
        "}\r\n",
        DispositionName(result.disposition),
        static_cast<unsigned>(result.disposition),
        static_cast<unsigned>(result.primary_disposition),
        static_cast<unsigned>(result.stage),
        static_cast<unsigned>(result.protocol_error),
        result.backend_error,
        result.cleanup_error,
        result.remote_reject_error,
        result.remote_seid,
        ldac_sample_rate_to_hz(result.configuration.sample_rate),
        result.configuration.channel_mode,
        result.open_attempts,
        result.signaling_exchanges,
        result.sink_candidates,
        result.legacy_capability_fallbacks,
        result.signaling_opened ? "true" : "false",
        result.close_attempted ? "true" : "false",
        result.close_succeeded ? "true" : "false",
        IsStrictlyRetryableOpenFailure(result) ? "true" : "false");
    if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(json)) {
        CloseHandle(file);
        (void)DeleteFileW(temporary.c_str());
        return false;
    }
    DWORD written = 0u;
    const bool write_ok = WriteFile(file,
                                    json,
                                    static_cast<DWORD>(length),
                                    &written,
                                    nullptr) != FALSE &&
                          written == static_cast<DWORD>(length) &&
                          FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (!write_ok ||
        !MoveFileExW(temporary.c_str(),
                     path,
                     MOVEFILE_REPLACE_EXISTING |
                         MOVEFILE_WRITE_THROUGH)) {
        (void)DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

#ifdef V1_TRANSPORT_DISCOVERY_MOCK_BACKEND
class DiscoveryBackend final
    : public native_ldac::agent::V1TransportDiscoveryBackend {
public:
    explicit DiscoveryBackend(HANDLE) {}

    bool OpenSignaling(std::uint32_t, std::uint32_t* error) override {
#ifdef V1_TRANSPORT_DISCOVERY_MOCK_RETRYABLE_OPEN
        *error = ERROR_REQ_NOT_ACCEP;
        return false;
#else
        *error = ERROR_SUCCESS;
        return true;
#endif
    }

    bool ExchangeSignaling(const std::uint8_t* request,
                           std::size_t request_size,
                           std::uint8_t* response,
                           std::size_t response_capacity,
                           std::size_t* response_size,
                           std::uint32_t,
                           std::uint32_t* error) override {
        avdtp_header header = {};
        if (avdtp_parse_header(request, request_size, &header) != AVDTP_OK) {
            *error = ERROR_INVALID_DATA;
            return false;
        }
        std::array<std::uint8_t, 32> payload = {};
        std::size_t payload_size = 0u;
        if (header.signal_id == AVDTP_SIGNAL_DISCOVER) {
            payload[0] = 0x0Cu;
            payload[1] = 0x08u;
            payload_size = 2u;
        } else if (header.signal_id ==
                   AVDTP_SIGNAL_GET_ALL_CAPABILITIES) {
            const std::uint8_t capabilities[] = {
                AVDTP_SERVICE_MEDIA_TRANSPORT, 0x00u,
                AVDTP_SERVICE_MEDIA_CODEC, 0x0Au,
                0x00u, AVDTP_CODEC_VENDOR,
                0x2Du, 0x01u, 0x00u, 0x00u,
                0xAAu, 0x00u, 0x3Cu, 0x07u,
            };
            std::memcpy(payload.data(), capabilities, sizeof(capabilities));
            payload_size = sizeof(capabilities);
        } else {
            *error = ERROR_INVALID_FUNCTION;
            return false;
        }
        const std::size_t written = avdtp_write_single(
            response,
            response_capacity,
            header.transaction_label,
            AVDTP_MESSAGE_ACCEPT,
            header.signal_id,
            payload.data(),
            payload_size);
        if (written == 0u) {
            *error = ERROR_INSUFFICIENT_BUFFER;
            return false;
        }
        *response_size = written;
        *error = ERROR_SUCCESS;
        return true;
    }

    bool CloseSignaling(std::uint32_t* error) override {
        *error = ERROR_SUCCESS;
        return true;
    }
};
#else
using DiscoveryBackend = native_ldac::agent::V1TransportDriverBackend;
#endif

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 &&
        (std::wcscmp(argv[1], L"--help") == 0 ||
         std::wcscmp(argv[1], L"-h") == 0)) {
        PrintUsage();
        return 0;
    }
    Options options;
    if (!ParseOptions(argc, argv, &options)) {
        PrintUsage();
        return 2;
    }

    HANDLE ready = OpenSignal(options.ready_event);
    HANDLE stop = OpenWait(options.stop_event);
    HANDLE authorize = OpenWait(options.transport_open_event);
    HANDLE discovered = OpenSignal(options.capabilities_discovered_event);
    HANDLE media_started = OpenSignal(options.media_started_event);
    HANDLE media_stopped = OpenSignal(options.media_stopped_event);
    HANDLE media_failed = OpenSignal(options.media_failed_event);
    HANDLE retryable_open_failure =
        OpenSignal(options.retryable_open_failure_event);
    HANDLE graceful_stop = OpenWait(options.graceful_transport_stop_event);
    HANDLE cancel_transport = OpenWait(options.cancel_transport_event);
    if (ready == nullptr || stop == nullptr || authorize == nullptr ||
        discovered == nullptr || media_started == nullptr ||
        media_stopped == nullptr || media_failed == nullptr ||
        retryable_open_failure == nullptr ||
        graceful_stop == nullptr || cancel_transport == nullptr) {
        CloseIfValid(cancel_transport);
        CloseIfValid(graceful_stop);
        CloseIfValid(media_failed);
        CloseIfValid(retryable_open_failure);
        CloseIfValid(media_stopped);
        CloseIfValid(media_started);
        CloseIfValid(discovered);
        CloseIfValid(authorize);
        CloseIfValid(stop);
        CloseIfValid(ready);
        return 3;
    }

    int result_code = 0;
    if (!SetEvent(ready)) {
        result_code = 4;
        goto cleanup;
    }
    bool transport_authorized = false;
    HANDLE initial_waits[] = {stop, authorize};
    const DWORD initial = WaitForMultipleObjects(
        ARRAYSIZE(initial_waits), initial_waits, FALSE, 600000u);
    if (initial == WAIT_OBJECT_0) {
        result_code = 0;
        goto acknowledge_stop;
    }
    if (initial != WAIT_OBJECT_0 + 1u) {
        result_code = 5;
        goto cleanup;
    }
    transport_authorized = true;

    {
        DiscoveryBackend backend(stop);
        native_ldac::agent::V1TransportDiscoveryOptions discovery_options;
        const auto discovery =
            native_ldac::agent::RunV1TransportDiscoveryOnce(
                &backend,
                discovery_options,
                IsCancelled,
                stop);
        if (!WriteResult(options.session_result, discovery)) {
            result_code = 6;
            (void)SetEvent(media_failed);
            goto wait_for_stop;
        }
        if (discovery.disposition ==
            native_ldac::agent::V1TransportDiscoveryDisposition::Succeeded) {
            if (!SetEvent(discovered)) {
                result_code = 7;
                (void)SetEvent(media_failed);
                goto wait_for_stop;
            }
        } else if (IsStrictlyRetryableOpenFailure(discovery)) {
            if (!SetEvent(retryable_open_failure)) {
                result_code = 8;
                goto cleanup;
            }
        } else if (discovery.disposition !=
                   native_ldac::agent::V1TransportDiscoveryDisposition::
                       Cancelled) {
            if (!SetEvent(media_failed)) {
                result_code = 8;
                goto cleanup;
            }
        }
    }

wait_for_stop:
    if (WaitForSingleObject(stop, 600000u) != WAIT_OBJECT_0) {
        result_code = 9;
        goto cleanup;
    }

acknowledge_stop:
    if (transport_authorized &&
        WaitForSingleObject(cancel_transport, 0u) != WAIT_OBJECT_0 &&
        WaitForSingleObject(graceful_stop, 0u) != WAIT_OBJECT_0) {
        result_code = 10;
        goto cleanup;
    }
    if (!SetEvent(media_stopped)) {
        result_code = 11;
    }

cleanup:
    CloseHandle(cancel_transport);
    CloseHandle(graceful_stop);
    CloseHandle(media_failed);
    CloseHandle(retryable_open_failure);
    CloseHandle(media_stopped);
    CloseHandle(media_started);
    CloseHandle(discovered);
    CloseHandle(authorize);
    CloseHandle(stop);
    CloseHandle(ready);
    return result_code;
}
