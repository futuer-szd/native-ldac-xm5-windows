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
#include "ldac_native/ldac_encoder.h"
#ifdef V1_TRANSPORT_PCM_WORKER
#include "v1_transport_pcm_session.h"
#elif defined(V1_TRANSPORT_SILENCE_WORKER)
#include "v1_transport_silence_session.h"
#else
#include "v1_transport_configuration_session.h"
#endif

#if defined(V1_TRANSPORT_PCM_WORKER) && \
    !defined(V1_TRANSPORT_PCM_MOCK_BACKEND)
#include "v1_transport_pcm_source_adapter.h"
#include "v1_transport_silence_driver_backend.h"
#elif defined(V1_TRANSPORT_SILENCE_WORKER) && \
    !defined(V1_TRANSPORT_SILENCE_MOCK_BACKEND)
#include "v1_transport_silence_driver_backend.h"
#elif !defined(V1_TRANSPORT_CONFIGURATION_MOCK_BACKEND)
#include "v1_transport_configuration_driver_backend.h"
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
    const wchar_t* single_gain_ready_event = nullptr;
    const wchar_t* session_result = nullptr;
    std::uint64_t session_generation = 0u;
    bool apply_endpoint_volume = true;
    ldac_encoder_quality quality = LDAC_ENCODER_QUALITY_HQ;
    ldac_encoder_channel_mode channel_mode = LDAC_ENCODER_CHANNEL_STEREO;
    unsigned sample_rate_hz = 48000u;
    unsigned bits_per_sample = 16u;
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
        } else if (std::wcscmp(
                       argument, L"--single-gain-ready-event") == 0) {
            destination = &options->single_gain_ready_event;
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
        } else if (std::wcscmp(
                       argument, L"--apply-endpoint-volume") == 0) {
            const wchar_t* value = nullptr;
            if (!ParseValue(argc, argv, &index, &value)) {
                return false;
            }
            if (std::wcscmp(value, L"0") == 0) {
                options->apply_endpoint_volume = false;
            } else if (std::wcscmp(value, L"1") == 0) {
                options->apply_endpoint_volume = true;
            } else {
                return false;
            }
            continue;
        } else if (std::wcscmp(argument, L"--quality") == 0) {
            const wchar_t* value = nullptr;
            if (!ParseValue(argc, argv, &index, &value)) return false;
            if (_wcsicmp(value, L"hq") == 0) {
                options->quality = LDAC_ENCODER_QUALITY_HQ;
            } else if (_wcsicmp(value, L"sq") == 0) {
                options->quality = LDAC_ENCODER_QUALITY_SQ;
            } else if (_wcsicmp(value, L"mq") == 0) {
                options->quality = LDAC_ENCODER_QUALITY_MQ;
            } else {
                return false;
            }
            continue;
        } else if (std::wcscmp(argument, L"--channel-mode") == 0) {
            const wchar_t* value = nullptr;
            if (!ParseValue(argc, argv, &index, &value)) return false;
            if (_wcsicmp(value, L"stereo") == 0) {
                options->channel_mode = LDAC_ENCODER_CHANNEL_STEREO;
            } else if (_wcsicmp(value, L"dual") == 0) {
                options->channel_mode = LDAC_ENCODER_CHANNEL_DUAL;
            } else if (_wcsicmp(value, L"mono") == 0) {
                options->channel_mode = LDAC_ENCODER_CHANNEL_MONO;
            } else {
                return false;
            }
            continue;
        } else if (std::wcscmp(argument, L"--sample-rate") == 0) {
            const wchar_t* value = nullptr;
            if (!ParseValue(argc, argv, &index, &value)) return false;
            wchar_t* end = nullptr;
            const unsigned long parsed = wcstoul(value, &end, 10);
            if (end == value || end == nullptr || *end != L'\0' ||
                (parsed != 44100u && parsed != 48000u &&
                 parsed != 88200u && parsed != 96000u)) return false;
            options->sample_rate_hz = static_cast<unsigned>(parsed);
            continue;
        } else if (std::wcscmp(argument, L"--bits") == 0) {
            const wchar_t* value = nullptr;
            if (!ParseValue(argc, argv, &index, &value)) return false;
            wchar_t* end = nullptr;
            const unsigned long parsed = wcstoul(value, &end, 10);
            if (end == value || end == nullptr || *end != L'\0' ||
                (parsed != 16u && parsed != 24u)) return false;
            options->bits_per_sample = static_cast<unsigned>(parsed);
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
        L"Usage: v1_transport_configuration_worker.exe "
        L"--ready-event <name> --stop-event <name> "
        L"--transport-open-event <name> "
        L"--capabilities-discovered-event <name> "
        L"--media-started-event <name> --media-stopped-event <name> "
        L"--media-failed-event <name> "
        L"--retryable-open-failure-event <name> "
        L"--graceful-transport-stop-event <name> "
        L"--cancel-transport-event <name> --session-result <path> "
        L"--session-generation <positive-integer> "
        L"[--apply-endpoint-volume 0|1] "
        L"[--quality hq|sq|mq] "
        L"[--channel-mode stereo|dual|mono] "
        L"[--sample-rate 44100|48000|88200|96000] [--bits 16|24] "
        L"[--single-gain-ready-event <name>]\n");
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

#ifdef V1_TRANSPORT_PCM_WORKER
struct PcmStopContext {
    HANDLE graceful = nullptr;
    HANDLE cancel = nullptr;
};

native_ldac::agent::V1TransportPcmStopDisposition PcmStopProbe(
    void* context) {
    const auto* value = static_cast<const PcmStopContext*>(context);
    if (value == nullptr) {
        return native_ldac::agent::V1TransportPcmStopDisposition::None;
    }
    if (value->cancel != nullptr &&
        WaitForSingleObject(value->cancel, 0u) == WAIT_OBJECT_0) {
        return native_ldac::agent::V1TransportPcmStopDisposition::Cancel;
    }
    if (value->graceful != nullptr &&
        WaitForSingleObject(value->graceful, 0u) == WAIT_OBJECT_0) {
        return native_ldac::agent::V1TransportPcmStopDisposition::Graceful;
    }
    return native_ldac::agent::V1TransportPcmStopDisposition::None;
}

bool NotifyPcmStarted(void* context, std::uint32_t* error) {
    const HANDLE event = static_cast<HANDLE>(context);
    const bool notified = event != nullptr && SetEvent(event) != FALSE;
    if (error != nullptr) {
        *error = notified ? ERROR_SUCCESS : GetLastError();
    }
    return notified;
}
#endif

bool IsStrictlyRetryableOpenFailure(
#if defined(V1_TRANSPORT_SILENCE_WORKER) || \
    defined(V1_TRANSPORT_PCM_WORKER)
    const native_ldac::agent::V1TransportSilenceResult& result) {
    using Stage = native_ldac::agent::V1TransportSilenceStage;
#else
    const native_ldac::agent::V1TransportConfigurationResult& result) {
    using Stage = native_ldac::agent::V1TransportConfigurationStage;
#endif
    using native_ldac::agent::V1TransportConfigurationDisposition;
    const bool zero_exchange_open_failure = result.disposition ==
               V1TransportConfigurationDisposition::BackendFailure &&
           result.primary_disposition ==
               V1TransportConfigurationDisposition::BackendFailure &&
           result.stage == Stage::OpenSignaling &&
           result.backend_error == ERROR_REQ_NOT_ACCEP &&
           result.open_attempts == 1u &&
           result.signaling_exchanges == 0u &&
           !result.signaling_opened;
#if defined(V1_TRANSPORT_SILENCE_WORKER) || \
    defined(V1_TRANSPORT_PCM_WORKER)
    (void)zero_exchange_open_failure;
    return native_ldac::agent::
        IsV1StrictlyRetryableRemoteNoResources(result);
#else
    return zero_exchange_open_failure;
#endif
}

#if defined(V1_TRANSPORT_SILENCE_WORKER) || \
    defined(V1_TRANSPORT_PCM_WORKER)
bool IsConfirmedRemoteNoResources(
    const native_ldac::agent::V1TransportSilenceResult& result) {
    return native_ldac::agent::IsV1RemoteNoResourcesDiagnostic(
        result.open_diagnostics);
}
#endif

const char* DispositionName(
    native_ldac::agent::V1TransportConfigurationDisposition disposition) {
    using native_ldac::agent::V1TransportConfigurationDisposition;
    switch (disposition) {
        case V1TransportConfigurationDisposition::Succeeded:
            return "succeeded";
        case V1TransportConfigurationDisposition::Cancelled:
            return "cancelled";
        case V1TransportConfigurationDisposition::InvalidConfiguration:
            return "invalid-configuration";
        case V1TransportConfigurationDisposition::BackendFailure:
            return "backend-failure";
        case V1TransportConfigurationDisposition::ProtocolFailure:
            return "protocol-failure";
        case V1TransportConfigurationDisposition::CleanupFailure:
            return "cleanup-failure";
        default:
            return "unknown";
    }
}

#if !defined(V1_TRANSPORT_SILENCE_WORKER) && \
    !defined(V1_TRANSPORT_PCM_WORKER)
bool WriteResult(
    const wchar_t* path,
    const native_ldac::agent::V1TransportConfigurationResult& result) {
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
    char json[3072] = {};
    const int length = sprintf_s(
        json,
        "{\r\n"
        "  \"schema_version\": 1,\r\n"
        "  \"disposition\": \"%s\",\r\n"
        "  \"disposition_code\": %u,\r\n"
        "  \"primary_disposition_code\": %u,\r\n"
        "  \"stage\": %u,\r\n"
        "  \"protocol_error\": %d,\r\n"
        "  \"backend_error\": %u,\r\n"
        "  \"cleanup_error\": %u,\r\n"
        "  \"remote_seid\": %u,\r\n"
        "  \"sample_rate_hz\": %u,\r\n"
        "  \"channel_mode\": %u,\r\n"
        "  \"open_attempts\": %u,\r\n"
        "  \"signaling_exchanges\": %u,\r\n"
        "  \"incoming_mtu\": %u,\r\n"
        "  \"outgoing_mtu\": %u,\r\n"
        "  \"signaling_opened\": %s,\r\n"
        "  \"set_configuration_accepted\": %s,\r\n"
        "  \"avdtp_open_accepted\": %s,\r\n"
        "  \"media_opened\": %s,\r\n"
        "  \"avdtp_close_accepted\": %s,\r\n"
        "  \"close_attempted\": %s,\r\n"
        "  \"close_succeeded\": %s,\r\n"
        "  \"media_start_commands\": 0,\r\n"
        "  \"media_packets_written\": 0,\r\n"
        "  \"strictly_retryable_open_failure\": %s\r\n"
        "}\r\n",
        DispositionName(result.disposition),
        static_cast<unsigned>(result.disposition),
        static_cast<unsigned>(result.primary_disposition),
        static_cast<unsigned>(result.stage),
        result.protocol_error,
        result.backend_error,
        result.cleanup_error,
        result.remote_seid,
        ldac_sample_rate_to_hz(result.configuration.sample_rate),
        result.configuration.channel_mode,
        result.open_attempts,
        result.signaling_exchanges,
        result.incoming_mtu,
        result.outgoing_mtu,
        result.signaling_opened ? "true" : "false",
        result.set_configuration_accepted ? "true" : "false",
        result.avdtp_open_accepted ? "true" : "false",
        result.media_opened ? "true" : "false",
        result.avdtp_close_accepted ? "true" : "false",
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
#endif

#ifdef V1_TRANSPORT_PCM_WORKER
const char* PcmLimiterAlgorithmName(
    native_ldac::agent::V1TransportPcmLimiterMode mode) {
    using native_ldac::agent::V1TransportPcmLimiterMode;
    if (mode == V1TransportPcmLimiterMode::LinkedStereoBlock) {
        return "linked-stereo-block";
    }
    if (mode ==
        V1TransportPcmLimiterMode::LinkedStereoSamplePeakFidelity) {
        return "linked-stereo-sample-peak";
    }
    return "hard-clip";
}

const char* PcmQualityName(ldac_encoder_quality quality) {
    switch (quality) {
        case LDAC_ENCODER_QUALITY_HQ: return "HQ";
        case LDAC_ENCODER_QUALITY_SQ: return "SQ";
        case LDAC_ENCODER_QUALITY_MQ: return "MQ";
    }
    return "invalid";
}

bool WritePcmResult(
    const wchar_t* path,
    const native_ldac::agent::V1TransportPcmResult& result) {
    if (path == nullptr || path[0] == L'\0') return false;
    const std::wstring temporary = std::wstring(path) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0u, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    char json[16384]{};
    const int length = sprintf_s(
        json,
        "{\r\n"
        "  \"schema_version\": 1,\r\n"
        "  \"disposition\": \"%s\",\r\n"
        "  \"disposition_code\": %u,\r\n"
        "  \"primary_disposition_code\": %u,\r\n"
        "  \"stage\": %u,\r\n"
        "  \"protocol_error\": %d,\r\n"
        "  \"backend_error\": %u,\r\n"
        "  \"open_diagnostic_query_attempts\": %u,\r\n"
        "  \"open_diagnostic_query_error\": %u,\r\n"
        "  \"open_diagnostic_query_bytes\": %u,\r\n"
        "  \"open_diagnostic_available\": %s,\r\n"
        "  \"open_diagnostic_remote_response_valid\": %s,\r\n"
        "  \"open_diagnostic_sequence\": %u,\r\n"
        "  \"open_diagnostic_operation\": %u,\r\n"
        "  \"open_diagnostic_io_status\": %d,\r\n"
        "  \"open_diagnostic_brb_status\": %d,\r\n"
        "  \"open_diagnostic_bluetooth_status\": %u,\r\n"
        "  \"open_diagnostic_remote_bluetooth_address\": %llu,\r\n"
        "  \"open_diagnostic_channel_flags\": %u,\r\n"
        "  \"open_diagnostic_flags\": %u,\r\n"
        "  \"open_diagnostic_psm\": %u,\r\n"
        "  \"open_diagnostic_response\": %u,\r\n"
        "  \"open_diagnostic_response_status\": %u,\r\n"
        "  \"open_diagnostic_remote_no_resources\": %s,\r\n"
        "  \"media_write_diagnostic_query_attempts\": %u,\r\n"
        "  \"media_write_diagnostic_query_error\": %u,\r\n"
        "  \"media_write_diagnostic_query_bytes\": %u,\r\n"
        "  \"media_write_diagnostic_available\": %s,\r\n"
        "  \"media_write_diagnostic_sequence\": %u,\r\n"
        "  \"media_write_diagnostic_operation\": %u,\r\n"
        "  \"media_write_diagnostic_io_status\": %d,\r\n"
        "  \"media_write_diagnostic_brb_status\": %d,\r\n"
        "  \"media_write_diagnostic_bluetooth_status\": %u,\r\n"
        "  \"media_write_diagnostic_requested_bytes\": %u,\r\n"
        "  \"media_write_diagnostic_brb_buffer_size\": %u,\r\n"
        "  \"media_write_diagnostic_remaining_bytes\": %u,\r\n"
        "  \"media_write_diagnostic_transfer_flags\": %u,\r\n"
        "  \"cleanup_error\": %u,\r\n"
        "  \"remote_seid\": %u,\r\n"
        "  \"sample_rate_hz\": %u,\r\n"
        "  \"bits_per_sample\": %u,\r\n"
        "  \"channel_mode\": %u,\r\n"
        "  \"encoder_quality\": \"%s\",\r\n"
        "  \"nominal_ldac_bitrate_kbps\": %u,\r\n"
        "  \"stream_epoch\": %llu,\r\n"
        "  \"open_attempts\": %u,\r\n"
        "  \"signaling_exchanges\": %u,\r\n"
        "  \"peer_signaling_commands_received\": %u,\r\n"
        "  \"peer_discover_commands_accepted\": %u,\r\n"
        "  \"peer_capability_commands_accepted\": %u,\r\n"
        "  \"peer_configuration_commands_rejected\": %u,\r\n"
        "  \"peer_close_commands_accepted\": %u,\r\n"
        "  \"peer_signaling_read_timeouts\": %u,\r\n"
        "  \"last_signaling_response_size\": %u,\r\n"
        "  \"last_signaling_tx_header_available\": %s,\r\n"
        "  \"last_signaling_tx_transaction_label\": %u,\r\n"
        "  \"last_signaling_tx_message_type\": %u,\r\n"
        "  \"last_signaling_tx_signal_id\": %u,\r\n"
        "  \"last_signaling_rx_header_available\": %s,\r\n"
        "  \"last_signaling_rx_transaction_label\": %u,\r\n"
        "  \"last_signaling_rx_message_type\": %u,\r\n"
        "  \"last_signaling_rx_signal_id\": %u,\r\n"
        "  \"incoming_mtu\": %u,\r\n"
        "  \"outgoing_mtu\": %u,\r\n"
        "  \"media_packets_written\": %u,\r\n"
        "  \"media_bytes_written\": %u,\r\n"
        "  \"pcm_frames_read\": %llu,\r\n"
        "  \"pcm_frames_sent\": %llu,\r\n"
        "  \"transport_frames_sent\": %llu,\r\n"
        "  \"pre_start_pcm_frames_discarded\": %llu,\r\n"
        "  \"pcm_prepare_attempts\": %u,\r\n"
        "  \"pcm_epoch_restarts\": %u,\r\n"
        "  \"pcm_stream_stop_count\": %u,\r\n"
        "  \"pcm_stream_stop_detected\": %s,\r\n"
        "  \"pcm_stream_stop_error\": %u,\r\n"
        "  \"pcm_stream_stop_snapshot_valid\": %s,\r\n"
        "  \"pcm_stream_stop_snapshot_error\": %u,\r\n"
        "  \"pcm_stream_stop_elapsed_ms\": %llu,\r\n"
        "  \"pcm_stream_stop_stream_active\": %s,\r\n"
        "  \"pcm_stream_stop_discontinuity\": %s,\r\n"
        "  \"pcm_stream_stop_epoch\": %llu,\r\n"
        "  \"pcm_stream_stop_available_bytes\": %u,\r\n"
        "  \"pcm_stream_stop_capacity_bytes\": %u,\r\n"
        "  \"pcm_stream_stop_total_bytes_written\": %llu,\r\n"
        "  \"pcm_stream_stop_total_bytes_read\": %llu,\r\n"
        "  \"pcm_stream_stop_total_bytes_dropped\": %llu,\r\n"
        "  \"pcm_rebind_attempts\": %u,\r\n"
        "  \"pcm_rebind_successes\": %u,\r\n"
        "  \"pcm_rebind_failures\": %u,\r\n"
        "  \"pcm_rebind_last_error\": %u,\r\n"
        "  \"pcm_rebind_last_timeout_ms\": %u,\r\n"
        "  \"pcm_rebind_last_elapsed_ms\": %llu,\r\n"
        "  \"consumer_lease_acquire_count\": %u,\r\n"
        "  \"consumer_lease_release_count\": %u,\r\n"
        "  \"target_duration_ms\": %u,\r\n"
        "  \"actual_duration_ms\": %u,\r\n"
        "  \"pacing_waits\": %u,\r\n"
        "  \"pcm_transient_timeout_count\": %u,\r\n"
        "  \"pcm_transient_timeout_recovery_count\": %u,\r\n"
        "  \"pcm_transient_timeout_exhausted_count\": %u,\r\n"
        "  \"pcm_transient_timeout_max_streak_ms\": %llu,\r\n"
        "  \"media_write_not_ready_retries\": %u,\r\n"
        "  \"media_write_not_ready_exhaustions\": %u,\r\n"
        "  \"maximum_gain_scalar\": %.8f,\r\n"
        "  \"maximum_output_peak_ceiling\": %.8f,\r\n"
        "  \"maximum_pre_gain_peak\": %.8f,\r\n"
        "  \"maximum_unlimited_post_gain_peak\": %.8f,\r\n"
        "  \"maximum_post_gain_peak\": %.8f,\r\n"
        "  \"limited_output_samples\": %llu,\r\n"
        "  \"limiter_algorithm\": \"%s\",\r\n"
        "  \"limiter_algorithm_version\": 1,\r\n"
        "  \"limiter_release_ms\": %.4f,\r\n"
        "  \"limiter_minimum_gain\": %.8f,\r\n"
        "  \"limiter_last_gain\": %.8f,\r\n"
        "  \"limiter_maximum_gain_step\": %.8f,\r\n"
        "  \"limiter_blocks_processed\": %llu,\r\n"
        "  \"limiter_attack_count\": %llu,\r\n"
        "  \"limiter_gain_reduced_frames\": %llu,\r\n"
        "  \"limiter_gain_reduced_samples\": %llu,\r\n"
        "  \"limiter_fallback_clamp_count\": %llu,\r\n"
        "  \"limiter_sanitized_sample_count\": %llu,\r\n"
        "  \"limiter_pre_over_ceiling_frames\": %llu,\r\n"
        "  \"limiter_pre_over_ceiling_samples\": %llu,\r\n"
        "  \"session_generation\": %llu,\r\n"
        "  \"volume_query_count\": %llu,\r\n"
        "  \"volume_change_count\": %llu,\r\n"
        "  \"volume_scalar_minimum\": %.8f,\r\n"
        "  \"volume_scalar_maximum\": %.8f,\r\n"
        "  \"volume_scalar_last\": %.8f,\r\n"
        "  \"volume_db_minimum\": %.4f,\r\n"
        "  \"volume_db_maximum\": %.4f,\r\n"
        "  \"volume_db_last\": %.4f,\r\n"
        "  \"volume_stable\": %s,\r\n"
        "  \"boundary_envelope_version\": 2,\r\n"
        "  \"startup_silence_ms\": %.4f,\r\n"
        "  \"startup_silence_frames_sent\": %llu,\r\n"
        "  \"startup_silence_packets_written\": %u,\r\n"
        "  \"fade_algorithm\": \"sent-frame-linear-fade\",\r\n"
        "  \"fade_algorithm_version\": 1,\r\n"
        "  \"fade_in_ms\": %.4f,\r\n"
        "  \"fade_duration_frames\": %llu,\r\n"
        "  \"fade_committed_sent_frames\": %llu,\r\n"
        "  \"fade_frames_below_unity\": %llu,\r\n"
        "  \"fade_blocks_prepared\": %llu,\r\n"
        "  \"fade_blocks_committed\": %llu,\r\n"
        "  \"fade_commit_failures\": %llu,\r\n"
        "  \"fade_sanitized_sample_count\": %llu,\r\n"
        "  \"fade_minimum_gain\": %.8f,\r\n"
        "  \"fade_last_gain\": %.8f,\r\n"
        "  \"fade_session_started\": %s,\r\n"
        "  \"boundary_resume_count\": %u,\r\n"
        "  \"boundary_resume_fade_frames\": %llu,\r\n"
        "  \"pause_suspend_count\": %u,\r\n"
        "  \"pause_resume_start_count\": %u,\r\n"
        "  \"pause_wait_prepare_attempts\": %u,\r\n"
        "  \"ceiling_ramp_start\": %.8f,\r\n"
        "  \"ceiling_ramp_ms\": %.4f,\r\n"
        "  \"ceiling_ramp_last\": %.8f,\r\n"
        "  \"output_chain_version\": 1,\r\n"
        "  \"volume_control_available\": %s,\r\n"
        "  \"volume_muted\": %s,\r\n"
        "  \"volume_scalar_at_prepare\": %.8f,\r\n"
        "  \"volume_db_at_prepare\": %.4f,\r\n"
        "  \"pcm_prepared\": %s,\r\n"
        "  \"consumer_lease_acquired\": %s,\r\n"
        "  \"consumer_lease_released\": %s,\r\n"
        "  \"audible_pcm_confirmed_before_open\": %s,\r\n"
        "  \"signaling_opened\": %s,\r\n"
        "  \"set_configuration_accepted\": %s,\r\n"
        "  \"avdtp_open_accepted\": %s,\r\n"
        "  \"media_opened\": %s,\r\n"
        "  \"avdtp_start_accepted\": %s,\r\n"
        "  \"media_started_notified\": %s,\r\n"
        "  \"completed_full_duration\": %s,\r\n"
        "  \"ended_by_graceful_stop\": %s,\r\n"
        "  \"ended_by_peer_close\": %s,\r\n"
        "  \"avdtp_suspend_accepted\": %s,\r\n"
        "  \"avdtp_close_accepted\": %s,\r\n"
        "  \"remote_stream_cleanup_required\": %s,\r\n"
        "  \"close_attempted\": %s,\r\n"
        "  \"close_succeeded\": %s,\r\n"
        "  \"strictly_retryable_open_failure\": %s\r\n"
        "}\r\n",
        DispositionName(result.disposition),
        static_cast<unsigned>(result.disposition),
        static_cast<unsigned>(result.primary_disposition),
        static_cast<unsigned>(result.stage), result.protocol_error,
        result.backend_error,
        result.open_diagnostics.query_attempts,
        result.open_diagnostics.query_error,
        result.open_diagnostics.query_bytes,
        result.open_diagnostics.available ? "true" : "false",
        result.open_diagnostics.remote_response_valid ? "true" : "false",
        result.open_diagnostics.sequence,
        result.open_diagnostics.operation,
        result.open_diagnostics.io_status,
        result.open_diagnostics.brb_status,
        result.open_diagnostics.bluetooth_status,
        static_cast<unsigned long long>(
            result.open_diagnostics.remote_bluetooth_address),
        result.open_diagnostics.channel_flags,
        result.open_diagnostics.flags,
        result.open_diagnostics.psm,
        result.open_diagnostics.response,
        result.open_diagnostics.response_status,
        IsConfirmedRemoteNoResources(result) ? "true" : "false",
        result.media_write_diagnostics.query_attempts,
        result.media_write_diagnostics.query_error,
        result.media_write_diagnostics.query_bytes,
        result.media_write_diagnostics.available ? "true" : "false",
        result.media_write_diagnostics.sequence,
        result.media_write_diagnostics.operation,
        result.media_write_diagnostics.io_status,
        result.media_write_diagnostics.brb_status,
        result.media_write_diagnostics.bluetooth_status,
        result.media_write_diagnostics.requested_bytes,
        result.media_write_diagnostics.brb_buffer_size,
        result.media_write_diagnostics.remaining_bytes,
        result.media_write_diagnostics.transfer_flags,
        result.cleanup_error, result.remote_seid,
        result.pcm_format.sample_rate_hz,
        result.pcm_format.bits_per_sample,
        result.configuration.channel_mode,
        PcmQualityName(result.encoder_quality),
        result.nominal_ldac_bitrate_kbps,
        static_cast<unsigned long long>(result.pcm_format.stream_epoch),
        result.open_attempts, result.signaling_exchanges,
        result.peer_signaling_commands_received,
        result.peer_discover_commands_accepted,
        result.peer_capability_commands_accepted,
        result.peer_configuration_commands_rejected,
        result.peer_close_commands_accepted,
        result.peer_signaling_read_timeouts,
        result.last_signaling_response_size,
        result.last_signaling_tx_header_available ? "true" : "false",
        result.last_signaling_tx_transaction_label,
        result.last_signaling_tx_message_type,
        result.last_signaling_tx_signal_id,
        result.last_signaling_rx_header_available ? "true" : "false",
        result.last_signaling_rx_transaction_label,
        result.last_signaling_rx_message_type,
        result.last_signaling_rx_signal_id,
        result.incoming_mtu, result.outgoing_mtu,
        result.media_packets_written, result.media_bytes_written,
        static_cast<unsigned long long>(result.pcm_frames_read),
        static_cast<unsigned long long>(result.pcm_frames_sent),
        static_cast<unsigned long long>(result.transport_frames_sent),
        static_cast<unsigned long long>(
            result.pre_start_pcm_frames_discarded),
        result.pcm_prepare_attempts, result.pcm_epoch_restarts,
        result.pcm_stream_stop_count,
        result.pcm_stream_stop_detected ? "true" : "false",
        result.pcm_stream_stop_error,
        result.pcm_stream_stop_snapshot_valid ? "true" : "false",
        result.pcm_stream_stop_snapshot_error,
        static_cast<unsigned long long>(
            result.pcm_stream_stop_elapsed_ms),
        result.pcm_stream_stop_snapshot.stream_active ? "true" : "false",
        result.pcm_stream_stop_snapshot.discontinuity ? "true" : "false",
        static_cast<unsigned long long>(
            result.pcm_stream_stop_snapshot.format.stream_epoch),
        result.pcm_stream_stop_snapshot.available_bytes,
        result.pcm_stream_stop_snapshot.capacity_bytes,
        static_cast<unsigned long long>(
            result.pcm_stream_stop_snapshot.total_bytes_written),
        static_cast<unsigned long long>(
            result.pcm_stream_stop_snapshot.total_bytes_read),
        static_cast<unsigned long long>(
            result.pcm_stream_stop_snapshot.total_bytes_dropped),
        result.pcm_rebind_attempts,
        result.pcm_rebind_successes,
        result.pcm_rebind_failures,
        result.pcm_rebind_last_error,
        result.pcm_rebind_last_timeout_ms,
        static_cast<unsigned long long>(
            result.pcm_rebind_last_elapsed_ms),
        result.consumer_lease_acquire_count,
        result.consumer_lease_release_count,
        result.target_duration_ms, result.actual_duration_ms,
        result.pacing_waits,
        result.pcm_transient_timeout_count,
        result.pcm_transient_timeout_recovery_count,
        result.pcm_transient_timeout_exhausted_count,
        static_cast<unsigned long long>(
            result.pcm_transient_timeout_max_streak_ms),
        result.media_write_not_ready_retries,
        result.media_write_not_ready_exhaustions,
        result.maximum_gain_scalar,
        result.maximum_output_peak_ceiling,
        result.maximum_pre_gain_peak,
        result.maximum_unlimited_post_gain_peak,
        result.maximum_post_gain_peak,
        static_cast<unsigned long long>(result.limited_output_samples),
        PcmLimiterAlgorithmName(result.limiter_mode),
        result.limiter_release_ms,
        result.limiter_minimum_gain,
        result.limiter_last_gain,
        result.limiter_maximum_gain_step,
        static_cast<unsigned long long>(result.limiter_blocks_processed),
        static_cast<unsigned long long>(result.limiter_attack_count),
        static_cast<unsigned long long>(
            result.limiter_gain_reduced_frames),
        static_cast<unsigned long long>(
            result.limiter_gain_reduced_samples),
        static_cast<unsigned long long>(
            result.limiter_fallback_clamp_count),
        static_cast<unsigned long long>(
            result.limiter_sanitized_sample_count),
        static_cast<unsigned long long>(
            result.limiter_pre_over_ceiling_frames),
        static_cast<unsigned long long>(
            result.limiter_pre_over_ceiling_samples),
        static_cast<unsigned long long>(result.session_generation),
        static_cast<unsigned long long>(result.volume_query_count),
        static_cast<unsigned long long>(result.volume_change_count),
        result.volume_scalar_minimum,
        result.volume_scalar_maximum,
        result.volume_scalar_last,
        result.volume_db_minimum,
        result.volume_db_maximum,
        result.volume_db_last,
        result.volume_stable ? "true" : "false",
        result.startup_silence_ms,
        static_cast<unsigned long long>(
            result.startup_silence_frames_sent),
        result.startup_silence_packets_written,
        result.fade_in_ms,
        static_cast<unsigned long long>(result.fade_duration_frames),
        static_cast<unsigned long long>(
            result.fade_committed_sent_frames),
        static_cast<unsigned long long>(result.fade_frames_below_unity),
        static_cast<unsigned long long>(result.fade_blocks_prepared),
        static_cast<unsigned long long>(result.fade_blocks_committed),
        static_cast<unsigned long long>(result.fade_commit_failures),
        static_cast<unsigned long long>(
            result.fade_sanitized_sample_count),
        result.fade_minimum_gain,
        result.fade_last_gain,
        result.fade_session_started ? "true" : "false",
        result.boundary_resume_count,
        static_cast<unsigned long long>(
            result.boundary_resume_fade_frames),
        result.pause_suspend_count,
        result.pause_resume_start_count,
        result.pause_wait_prepare_attempts,
        result.ceiling_ramp_start,
        result.ceiling_ramp_ms,
        result.ceiling_ramp_last,
        result.pcm_format.volume_control_available ? "true" : "false",
        result.pcm_format.muted ? "true" : "false",
        result.pcm_format.volume_scalar, result.pcm_format.volume_db,
        result.pcm_prepared ? "true" : "false",
        result.consumer_lease_acquired ? "true" : "false",
        result.consumer_lease_released ? "true" : "false",
        result.audible_pcm_confirmed_before_open ? "true" : "false",
        result.signaling_opened ? "true" : "false",
        result.set_configuration_accepted ? "true" : "false",
        result.avdtp_open_accepted ? "true" : "false",
        result.media_opened ? "true" : "false",
        result.avdtp_start_accepted ? "true" : "false",
        result.media_started_notified ? "true" : "false",
        result.completed_full_duration ? "true" : "false",
        result.ended_by_graceful_stop ? "true" : "false",
        result.ended_by_peer_close ? "true" : "false",
        result.avdtp_suspend_accepted ? "true" : "false",
        result.avdtp_close_accepted ? "true" : "false",
        result.remote_stream_cleanup_required ? "true" : "false",
        result.close_attempted ? "true" : "false",
        result.close_succeeded ? "true" : "false",
        IsStrictlyRetryableOpenFailure(result) ? "true" : "false");
    DWORD written = 0u;
    const bool ok = length > 0 &&
        static_cast<std::size_t>(length) < sizeof(json) &&
        WriteFile(file, json, static_cast<DWORD>(length), &written, nullptr) &&
        written == static_cast<DWORD>(length) && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || !MoveFileExW(temporary.c_str(), path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        (void)DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}
#endif

#ifdef V1_TRANSPORT_SILENCE_WORKER
bool WriteSilenceResult(
    const wchar_t* path,
    const native_ldac::agent::V1TransportSilenceResult& result) {
    if (path == nullptr || path[0] == L'\0') return false;
    const std::wstring temporary = std::wstring(path) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0u, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    char json[3072]{};
    const int length = sprintf_s(
        json,
        "{\r\n"
        "  \"schema_version\": 1,\r\n"
        "  \"disposition\": \"%s\",\r\n"
        "  \"disposition_code\": %u,\r\n"
        "  \"primary_disposition_code\": %u,\r\n"
        "  \"stage\": %u,\r\n"
        "  \"protocol_error\": %d,\r\n"
        "  \"backend_error\": %u,\r\n"
        "  \"open_diagnostic_query_attempts\": %u,\r\n"
        "  \"open_diagnostic_query_error\": %u,\r\n"
        "  \"open_diagnostic_query_bytes\": %u,\r\n"
        "  \"open_diagnostic_available\": %s,\r\n"
        "  \"open_diagnostic_remote_response_valid\": %s,\r\n"
        "  \"open_diagnostic_sequence\": %u,\r\n"
        "  \"open_diagnostic_operation\": %u,\r\n"
        "  \"open_diagnostic_io_status\": %d,\r\n"
        "  \"open_diagnostic_brb_status\": %d,\r\n"
        "  \"open_diagnostic_bluetooth_status\": %u,\r\n"
        "  \"open_diagnostic_remote_bluetooth_address\": %llu,\r\n"
        "  \"open_diagnostic_channel_flags\": %u,\r\n"
        "  \"open_diagnostic_flags\": %u,\r\n"
        "  \"open_diagnostic_psm\": %u,\r\n"
        "  \"open_diagnostic_response\": %u,\r\n"
        "  \"open_diagnostic_response_status\": %u,\r\n"
        "  \"open_diagnostic_remote_no_resources\": %s,\r\n"
        "  \"cleanup_error\": %u,\r\n"
        "  \"remote_seid\": %u,\r\n"
        "  \"sample_rate_hz\": %u,\r\n"
        "  \"channel_mode\": %u,\r\n"
        "  \"open_attempts\": %u,\r\n"
        "  \"signaling_exchanges\": %u,\r\n"
        "  \"incoming_mtu\": %u,\r\n"
        "  \"outgoing_mtu\": %u,\r\n"
        "  \"media_packets_written\": %u,\r\n"
        "  \"media_bytes_written\": %u,\r\n"
        "  \"signaling_opened\": %s,\r\n"
        "  \"set_configuration_accepted\": %s,\r\n"
        "  \"avdtp_open_accepted\": %s,\r\n"
        "  \"media_opened\": %s,\r\n"
        "  \"avdtp_start_accepted\": %s,\r\n"
        "  \"avdtp_suspend_accepted\": %s,\r\n"
        "  \"avdtp_close_accepted\": %s,\r\n"
        "  \"remote_stream_cleanup_required\": %s,\r\n"
        "  \"close_attempted\": %s,\r\n"
        "  \"close_succeeded\": %s,\r\n"
        "  \"strictly_retryable_open_failure\": %s\r\n"
        "}\r\n",
        DispositionName(result.disposition),
        static_cast<unsigned>(result.disposition),
        static_cast<unsigned>(result.primary_disposition),
        static_cast<unsigned>(result.stage), result.protocol_error,
        result.backend_error,
        result.open_diagnostics.query_attempts,
        result.open_diagnostics.query_error,
        result.open_diagnostics.query_bytes,
        result.open_diagnostics.available ? "true" : "false",
        result.open_diagnostics.remote_response_valid ? "true" : "false",
        result.open_diagnostics.sequence,
        result.open_diagnostics.operation,
        result.open_diagnostics.io_status,
        result.open_diagnostics.brb_status,
        result.open_diagnostics.bluetooth_status,
        static_cast<unsigned long long>(
            result.open_diagnostics.remote_bluetooth_address),
        result.open_diagnostics.channel_flags,
        result.open_diagnostics.flags,
        result.open_diagnostics.psm,
        result.open_diagnostics.response,
        result.open_diagnostics.response_status,
        IsConfirmedRemoteNoResources(result) ? "true" : "false",
        result.cleanup_error, result.remote_seid,
        ldac_sample_rate_to_hz(result.configuration.sample_rate),
        result.configuration.channel_mode, result.open_attempts,
        result.signaling_exchanges, result.incoming_mtu,
        result.outgoing_mtu,
        result.media_packets_written, result.media_bytes_written,
        result.signaling_opened ? "true" : "false",
        result.set_configuration_accepted ? "true" : "false",
        result.avdtp_open_accepted ? "true" : "false",
        result.media_opened ? "true" : "false",
        result.avdtp_start_accepted ? "true" : "false",
        result.avdtp_suspend_accepted ? "true" : "false",
        result.avdtp_close_accepted ? "true" : "false",
        result.remote_stream_cleanup_required ? "true" : "false",
        result.close_attempted ? "true" : "false",
        result.close_succeeded ? "true" : "false",
        IsStrictlyRetryableOpenFailure(result) ? "true" : "false");
    DWORD written = 0u;
    const bool ok = length > 0 &&
        static_cast<std::size_t>(length) < sizeof(json) &&
        WriteFile(file, json, static_cast<DWORD>(length), &written, nullptr) &&
        written == static_cast<DWORD>(length) && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || !MoveFileExW(temporary.c_str(), path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        (void)DeleteFileW(temporary.c_str()); return false;
    }
    return true;
}
#endif

#if defined(V1_TRANSPORT_CONFIGURATION_MOCK_BACKEND) || \
    defined(V1_TRANSPORT_SILENCE_MOCK_BACKEND) || \
    defined(V1_TRANSPORT_PCM_MOCK_BACKEND)
class ConfigurationBackend final
#if defined(V1_TRANSPORT_SILENCE_WORKER) || \
    defined(V1_TRANSPORT_PCM_WORKER)
    : public native_ldac::agent::V1TransportSilenceBackend {
#else
    : public native_ldac::agent::V1TransportConfigurationBackend {
#endif
public:
    explicit ConfigurationBackend(HANDLE) {}

    bool OpenSignaling(std::uint32_t, std::uint32_t* error) override {
        *error = ERROR_SUCCESS;
        return true;
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
        } else if (header.signal_id != AVDTP_SIGNAL_SET_CONFIGURATION &&
                   header.signal_id != AVDTP_SIGNAL_OPEN &&
                   header.signal_id != AVDTP_SIGNAL_CLOSE
#if defined(V1_TRANSPORT_SILENCE_WORKER) || \
    defined(V1_TRANSPORT_PCM_WORKER)
                   && header.signal_id != AVDTP_SIGNAL_START
                   && header.signal_id != AVDTP_SIGNAL_SUSPEND
#endif
                   ) {
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

    bool OpenMedia(std::uint32_t,
                   std::uint16_t preferred_mtu,
                   std::uint16_t* incoming_mtu,
                   std::uint16_t* outgoing_mtu,
                   std::uint32_t* error) override {
        *incoming_mtu = preferred_mtu;
        *outgoing_mtu = 895u;
        *error = ERROR_SUCCESS;
        return true;
    }

    bool CloseSignaling(std::uint32_t* error) override {
        *error = ERROR_SUCCESS;
        return true;
    }
#if defined(V1_TRANSPORT_SILENCE_WORKER) || \
    defined(V1_TRANSPORT_PCM_WORKER)
    bool WriteMedia(const std::uint8_t*, std::size_t size,
                    std::uint32_t, std::uint32_t* error) override {
        if (size == 0u || size > 895u) {
            *error = ERROR_INVALID_DATA; return false;
        }
        *error = ERROR_SUCCESS; return true;
    }
    bool BeginPeerSignalingRead(std::uint32_t,
                                std::uint32_t* error) override {
        *error = ERROR_NOT_SUPPORTED; return false;
    }
    native_ldac::agent::V1TransportSignalingReadDisposition
    PollPeerSignalingRead(std::uint8_t*, std::size_t, std::size_t*,
                          std::uint32_t* error) override {
        *error = ERROR_NOT_SUPPORTED;
        return native_ldac::agent::
            V1TransportSignalingReadDisposition::Failure;
    }
    bool SendPeerSignalingResponse(const std::uint8_t*, std::size_t,
                                   std::uint32_t,
                                   std::uint32_t* error) override {
        *error = ERROR_NOT_SUPPORTED; return false;
    }
    bool CancelPeerSignalingRead(std::uint32_t* error) override {
        *error = ERROR_SUCCESS; return true;
    }
#endif
};
#else
#if defined(V1_TRANSPORT_SILENCE_WORKER) || \
    defined(V1_TRANSPORT_PCM_WORKER)
using ConfigurationBackend =
    native_ldac::agent::V1TransportSilenceDriverBackend;
#else
using ConfigurationBackend =
    native_ldac::agent::V1TransportConfigurationDriverBackend;
#endif
#endif

#ifdef V1_TRANSPORT_PCM_WORKER
#ifdef V1_TRANSPORT_PCM_MOCK_BACKEND
class PcmSource final : public native_ldac::agent::V1TransportPcmSource {
public:
    PcmSource(HANDLE, HANDLE, bool = true, HANDLE = nullptr,
              unsigned = 0u, unsigned = 0u) {}
    bool Prepare(native_ldac::agent::V1TransportPcmFormat* format,
                 std::uint32_t,
                 std::uint32_t* error) override {
        format->sample_rate_hz = 48000u;
        format->bits_per_sample = 16u;
        format->stream_epoch = 1u;
        format->volume_control_available = true;
        format->volume_scalar = 1.0f;
        format->volume_db = 0.0f;
        *error = ERROR_SUCCESS;
        return true;
    }
    native_ldac::agent::V1TransportPcmReadDisposition ReadFrames(
        float* pcm, std::size_t frames, std::uint32_t,
        std::size_t* frames_read, std::uint32_t* error) override {
        for (std::size_t index = 0u; index < frames * 2u; ++index) {
            pcm[index] = 0.25f;
        }
        *frames_read = frames;
        *error = ERROR_SUCCESS;
        return native_ldac::agent::V1TransportPcmReadDisposition::Data;
    }
    bool QueryFormat(native_ldac::agent::V1TransportPcmFormat* format,
                     std::uint32_t* error) override {
        format->sample_rate_hz = 48000u;
        format->bits_per_sample = 16u;
        format->stream_epoch = 1u;
        format->volume_control_available = true;
        format->muted = false;
        format->volume_scalar = 1.0f;
        format->volume_db = 0.0f;
        *error = ERROR_SUCCESS;
        return true;
    }
    bool WaitUntilSample(std::uint64_t, unsigned,
                         std::uint32_t* error) override {
        if (!first_wait_completed_) {
            (void)WaitForSingleObject(GetCurrentProcess(), 100u);
            first_wait_completed_ = true;
        }
        *error = ERROR_SUCCESS;
        return true;
    }
    bool Release(std::uint32_t* error) override {
        *error = ERROR_SUCCESS;
        return true;
    }
private:
    bool first_wait_completed_ = false;
};
#else
using PcmSource = native_ldac::agent::V1TransportNativePcmSource;
#endif
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
    HANDLE completed = OpenSignal(options.capabilities_discovered_event);
    HANDLE media_started = OpenSignal(options.media_started_event);
    HANDLE media_stopped = OpenSignal(options.media_stopped_event);
    HANDLE media_failed = OpenSignal(options.media_failed_event);
    HANDLE retryable_open_failure =
        OpenSignal(options.retryable_open_failure_event);
    HANDLE graceful_stop = OpenWait(options.graceful_transport_stop_event);
    HANDLE cancel_transport = OpenWait(options.cancel_transport_event);
    HANDLE single_gain_ready = options.single_gain_ready_event == nullptr
        ? nullptr
        : OpenWait(options.single_gain_ready_event);
    if (ready == nullptr || stop == nullptr || authorize == nullptr ||
        completed == nullptr || media_started == nullptr ||
        media_stopped == nullptr || media_failed == nullptr ||
        retryable_open_failure == nullptr ||
        graceful_stop == nullptr || cancel_transport == nullptr ||
        (options.single_gain_ready_event != nullptr &&
         single_gain_ready == nullptr)) {
        CloseIfValid(single_gain_ready);
        CloseIfValid(cancel_transport);
        CloseIfValid(graceful_stop);
        CloseIfValid(retryable_open_failure);
        CloseIfValid(media_failed);
        CloseIfValid(media_stopped);
        CloseIfValid(media_started);
        CloseIfValid(completed);
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
        goto acknowledge_stop;
    }
    if (initial != WAIT_OBJECT_0 + 1u) {
        result_code = 5;
        goto cleanup;
    }
    transport_authorized = true;

    {
#ifdef V1_TRANSPORT_PCM_WORKER
        ConfigurationBackend backend(cancel_transport);
        PcmSource pcm_source(cancel_transport,
                             graceful_stop,
                             options.apply_endpoint_volume,
                             single_gain_ready,
                             options.sample_rate_hz,
                             options.bits_per_sample);
        PcmStopContext stop_context{graceful_stop, cancel_transport};
        native_ldac::agent::V1TransportPcmOptions run_options;
        run_options.quality = options.quality;
        run_options.channel_mode = options.channel_mode;
        run_options.session_generation = options.session_generation;
#ifdef V1_TRANSPORT_PCM_DURATION_MS
        run_options.duration_ms = V1_TRANSPORT_PCM_DURATION_MS;
#endif
#ifdef V1_TRANSPORT_PCM_MAXIMUM_GAIN_SCALAR
        run_options.maximum_gain_scalar =
            V1_TRANSPORT_PCM_MAXIMUM_GAIN_SCALAR;
#endif
#ifdef V1_TRANSPORT_PCM_MAXIMUM_OUTPUT_PEAK
        run_options.maximum_output_peak =
            V1_TRANSPORT_PCM_MAXIMUM_OUTPUT_PEAK;
#endif
#ifdef V1_TRANSPORT_PCM_MAXIMUM_PACKETS
        run_options.maximum_packets =
            V1_TRANSPORT_PCM_MAXIMUM_PACKETS;
#endif
#ifdef V1_TRANSPORT_PCM_CONTINUOUS_UNTIL_STOP
        run_options.continuous_until_stop = true;
#endif
#ifdef V1_TRANSPORT_PCM_PAUSE_SUSPEND
        run_options.pause_suspend = true;
#endif
#ifdef V1_TRANSPORT_PCM_TIMEOUT_TOLERANCE_MS
        run_options.pcm_timeout_tolerance_ms =
            V1_TRANSPORT_PCM_TIMEOUT_TOLERANCE_MS;
#endif
#ifdef V1_TRANSPORT_PCM_AUDIBLE_PREFLIGHT_TIMEOUT_MS
        run_options.audible_preflight_timeout_ms =
            V1_TRANSPORT_PCM_AUDIBLE_PREFLIGHT_TIMEOUT_MS;
#endif
#ifdef V1_TRANSPORT_PCM_POST_START_STOP_CLASSIFICATION_TIMEOUT_MS
        run_options.post_start_stop_classification_timeout_ms =
            V1_TRANSPORT_PCM_POST_START_STOP_CLASSIFICATION_TIMEOUT_MS;
#endif
#ifdef V1_TRANSPORT_PCM_LINKED_LIMITER
        run_options.limiter_mode = native_ldac::agent::
            V1TransportPcmLimiterMode::LinkedStereoBlock;
#endif
#ifdef V1_TRANSPORT_PCM_LIMITER_RELEASE_MS
        run_options.limiter_release_ms =
            V1_TRANSPORT_PCM_LIMITER_RELEASE_MS;
#endif
#ifdef V1_TRANSPORT_PCM_REQUIRE_STABLE_VOLUME
        // In single-gain mode the Windows endpoint volume is a control/display
        // input only. Do not let its readback change trigger a PCM format or
        // stream-epoch rebind; the XM5 absolute volume is the sole applied
        // loudness gain for that path.
        const bool single_gain_requested =
            !options.apply_endpoint_volume ||
            options.single_gain_ready_event != nullptr;
        run_options.single_gain_mode = single_gain_requested;
        run_options.require_stable_volume = !single_gain_requested;
#endif
#ifdef V1_TRANSPORT_PCM_ALLOW_DYNAMIC_VOLUME
        run_options.allow_dynamic_volume = true;
#endif
#ifdef V1_TRANSPORT_PCM_ALLOW_POST_START_REBIND
        run_options.allow_post_start_pcm_rebind = true;
#endif
#ifdef V1_TRANSPORT_PCM_OBSERVE_PEER_CLOSE
        run_options.observe_peer_close_while_streaming = true;
#endif
#ifdef V1_TRANSPORT_PCM_STARTUP_SILENCE_MS
        run_options.startup_silence_ms =
            V1_TRANSPORT_PCM_STARTUP_SILENCE_MS;
#endif
#ifdef V1_TRANSPORT_PCM_FADE_IN_MS
        run_options.fade_in_ms = V1_TRANSPORT_PCM_FADE_IN_MS;
#endif
#ifdef V1_TRANSPORT_PCM_CEILING_RAMP_START
        run_options.ceiling_ramp_start =
            V1_TRANSPORT_PCM_CEILING_RAMP_START;
#endif
#ifdef V1_TRANSPORT_PCM_CEILING_RAMP_MS
        run_options.ceiling_ramp_ms = V1_TRANSPORT_PCM_CEILING_RAMP_MS;
#endif
#ifdef V1_TRANSPORT_PCM_SAMPLE_PEAK_FIDELITY
        run_options.limiter_mode = native_ldac::agent::
            V1TransportPcmLimiterMode::LinkedStereoSamplePeakFidelity;
#endif
        const auto result = native_ldac::agent::RunV1TransportPcmBurstOnce(
            &backend, &pcm_source, run_options, PcmStopProbe, &stop_context,
            NotifyPcmStarted, media_started);
        const bool result_written =
            WritePcmResult(options.session_result, result);
#else
        ConfigurationBackend backend(stop);
#ifdef V1_TRANSPORT_SILENCE_WORKER
        native_ldac::agent::V1TransportSilenceOptions run_options;
        const auto result =
            native_ldac::agent::RunV1TransportSilenceBurstOnce(
                &backend, run_options, IsCancelled, stop);
        const bool result_written =
            WriteSilenceResult(options.session_result, result);
#else
        native_ldac::agent::V1TransportConfigurationOptions run_options;
        const auto result =
            native_ldac::agent::RunV1TransportConfigurationOnce(
                &backend, run_options, IsCancelled, stop);
        const bool result_written = WriteResult(options.session_result, result);
#endif
#endif
        if (!result_written) {
            result_code = 6;
            (void)SetEvent(media_failed);
            goto wait_for_stop;
        }
        if (result.disposition == native_ldac::agent::
                V1TransportConfigurationDisposition::Succeeded) {
            if (!SetEvent(completed)) {
                result_code = 7;
                (void)SetEvent(media_failed);
                goto wait_for_stop;
            }
        } else if (IsStrictlyRetryableOpenFailure(result)) {
            if (!SetEvent(retryable_open_failure)) {
                result_code = 8;
                goto cleanup;
            }
        } else if (result.disposition != native_ldac::agent::
                       V1TransportConfigurationDisposition::Cancelled) {
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
    CloseIfValid(single_gain_ready);
    CloseHandle(cancel_transport);
    CloseHandle(graceful_stop);
    CloseHandle(retryable_open_failure);
    CloseHandle(media_failed);
    CloseHandle(media_stopped);
    CloseHandle(media_started);
    CloseHandle(completed);
    CloseHandle(authorize);
    CloseHandle(stop);
    CloseHandle(ready);
    return result_code;
}
