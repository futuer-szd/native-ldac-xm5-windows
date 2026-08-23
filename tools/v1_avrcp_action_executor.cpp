// SPDX-License-Identifier: Apache-2.0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <initguid.h>
#include <setupapi.h>

#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "nativeldac_avrcp_observer_ioctl.h"
#include "v1_avrcp_action_executor.h"
#include "v1_media_session_monitor.h"
#include "v1_avrcp_windows_sink.h"

namespace {

using namespace native_ldac::agent;

constexpr wchar_t kOptionReplay[] = L"--replay";
constexpr wchar_t kOptionReplayFilter[] = L"--replay-filter";
constexpr wchar_t kOptionLive[] = L"--live";
constexpr wchar_t kOptionDuration[] = L"--duration-seconds";
constexpr wchar_t kOptionVolumeSync[] = L"--volume-sync";
constexpr wchar_t kOptionNoVolumeSync[] = L"--no-volume-sync";
constexpr wchar_t kOptionMediaRouting[] = L"--route-media-keys";
constexpr wchar_t kOptionNoMediaRouting[] = L"--no-route-media-keys";
constexpr wchar_t kOptionOwnerLease[] = L"--owner-lease";
constexpr wchar_t kOptionInitialVolume[] = L"--initial-volume-percent";
constexpr wchar_t kOptionApply[] = L"--apply";
constexpr wchar_t kOptionDiagnoseMediaKeys[] = L"--diagnose-media-keys";
constexpr wchar_t kOptionHelp[] = L"--help";

void PrintHelp(const wchar_t* program) {
    std::wprintf(
        L"Usage: %ls (--replay PATH | --replay-filter PATH | --live) "
        L"[options]\n"
        L"Feeds XM5 AVRCP observer events through the control mapper.\n"
        L"Without --apply this is a dry run: decisions are printed, no "
        L"volume or media-key write happens.\n"
        L"  --replay PATH               Observer log to replay.\n"
        L"  --replay-filter PATH        v1_avrcp_filter_probe decoded trace "
        L"to replay (Microsoft-preserving upper filter surface).\n"
        L"  --live                      Read events live from the bound "
        L"observer driver interface.\n"
        L"  --duration-seconds N        Live observation seconds (default "
        L"120, max 600).\n"
        L"  --volume-sync / --no-volume-sync   Sync XM5 absolute volume to "
        L"the default render endpoint (default: sync).\n"
        L"  --route-media-keys / --no-route-media-keys  Route pass-through to "
        L"media keys (default: route).\n"
        L"  --owner-lease N             Mapper owner lease id (default 1).\n"
         L"  --initial-volume-percent N  Observed Windows volume before the "
         L"session (default 50).\n"
         L"  --apply                     Actually change volume / inject keys.\n"
         L"  --diagnose-media-keys       Print raw PASS THROUGH, mapper "
         L"eligibility, and SendInput diagnostics in live mode.\n"
         L"  --help                      Show this help.\n",
         program);
}

bool ParseUnsigned64(const wchar_t* text, std::uint64_t* value) {
    if (text == nullptr || value == nullptr || *text == L'\0') return false;
    wchar_t* end = nullptr;
    const unsigned long long parsed = std::wcstoull(text, &end, 10);
    if (end == nullptr || *end != L'\0') return false;
    *value = static_cast<std::uint64_t>(parsed);
    return true;
}

bool ReadTextFile(const std::wstring& path, std::string* text) {
    if (text == nullptr) return false;
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) return false;
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    *text = buffer.str();
    return true;
}

bool GetObserverInterfacePath(std::wstring* path) {
    if (path == nullptr) return false;
    HDEVINFO devices = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_NATIVE_LDAC_AVRCP_OBSERVER,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) return false;
    SP_DEVICE_INTERFACE_DATA interface_data{};
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
    DWORD required = 0u;
    (void)SetupDiGetDeviceInterfaceDetailW(
        devices, &interface_data, nullptr, 0u, &required, nullptr);
    if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
        SetupDiDestroyDeviceInfoList(devices);
        return false;
    }
    std::vector<BYTE> buffer(required);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
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
    SetupDiDestroyDeviceInfoList(devices);
    return true;
}

const char* LiveMediaSessionPlaybackName(
    V1MediaSessionPlayback playback) {
    switch (playback) {
        case V1MediaSessionPlayback::Playing:
            return "playing";
        case V1MediaSessionPlayback::Paused:
            return "paused";
        case V1MediaSessionPlayback::Stopped:
            return "stopped";
        case V1MediaSessionPlayback::Absent:
        default:
            return "absent";
    }
}

bool SameLiveMediaSessionSnapshot(
    const V1MediaSessionSnapshot& left,
    const V1MediaSessionSnapshot& right) {
    return left.acl_generation == right.acl_generation &&
        left.playback == right.playback &&
        left.play_enabled == right.play_enabled &&
        left.pause_enabled == right.pause_enabled &&
        left.next_enabled == right.next_enabled &&
        left.previous_enabled == right.previous_enabled;
}

V1MediaSessionSnapshot GetLiveMediaSessionSnapshot(
    V1MediaSessionMonitor* media_monitor,
    std::uint64_t acl_generation,
    bool* using_fallback) {
    if (media_monitor != nullptr && media_monitor->ready()) {
        if (using_fallback != nullptr) *using_fallback = false;
        return media_monitor->Snapshot(acl_generation);
    }

    // The bounded audible trial has already established a concrete LDAC
    // media session before the live executor starts. GSMTC initialization is
    // asynchronous, so keep the controls usable until the monitor publishes
    // its first real snapshot, matching the daily-host contract.
    if (using_fallback != nullptr) *using_fallback = true;
    V1MediaSessionSnapshot fallback{};
    fallback.acl_generation = acl_generation;
    fallback.playback = V1MediaSessionPlayback::Playing;
    fallback.pause_enabled = true;
    fallback.next_enabled = true;
    fallback.previous_enabled = true;
    return fallback;
}

void PrintLiveMediaSessionSnapshot(
    const V1MediaSessionSnapshot& snapshot,
    bool using_fallback) {
    std::printf(
        "live: media-session source=%s playback=%s play=%s pause=%s "
        "next=%s previous=%s\n",
        using_fallback ? "ldac-fallback" : "gsm-tc",
        LiveMediaSessionPlaybackName(snapshot.playback),
        snapshot.play_enabled ? "yes" : "no",
        snapshot.pause_enabled ? "yes" : "no",
        snapshot.next_enabled ? "yes" : "no",
        snapshot.previous_enabled ? "yes" : "no");
}

const char* LivePassThroughOperationName(std::uint8_t operation) {
    switch (operation) {
        case 0x44u:
            return "PLAY";
        case 0x46u:
            return "PAUSE";
        case 0x4Bu:
            return "NEXT";
        case 0x4Cu:
            return "PREVIOUS";
        default:
            return "UNKNOWN";
    }
}

bool IsAbsoluteVolumeControlReady(
    const NLD_AVRCP_OBSERVER_STATUS& status) {
    constexpr ULONG required =
        NLD_AVRCP_OBSERVER_STATUS_CHANNEL_OPEN |
        NLD_AVRCP_OBSERVER_STATUS_VOLUME_SUPPORTED |
        NLD_AVRCP_OBSERVER_STATUS_OBSERVING;
    constexpr ULONG transient =
        NLD_AVRCP_OBSERVER_STATUS_OPEN_PENDING |
        NLD_AVRCP_OBSERVER_STATUS_REMOTE_DISCONNECTED;
    return (status.Flags & required) == required &&
           (status.Flags & transient) == 0u;
}

class HandleWriter final : public V1AvrcpBluetoothWriter {
public:
    void SetDriverHandle(HANDLE driver) { driver_ = driver; }
    bool WriteAvrcp(ULONG pdu,
                    ULONG response,
                    const UCHAR* parameters,
                    ULONG parameter_size) override {
        if (driver_ == nullptr || driver_ == INVALID_HANDLE_VALUE ||
            parameter_size > 8u ||
            (parameter_size != 0u && parameters == nullptr)) {
            return false;
        }
        NLD_AVRCP_OBSERVER_WRITE_REQUEST request{};
        request.Size = sizeof(request);
        request.PduId = pdu;
        request.Response = response;
        request.ParameterSize = parameter_size;
        for (ULONG index = 0u; index < parameter_size; ++index) {
            request.Parameters[index] = parameters[index];
        }
        DWORD bytes = 0u;
        return DeviceIoControl(
                   driver_,
                   IOCTL_NLD_AVRCP_OBSERVER_SEND_COMMAND,
                   &request,
                   sizeof(request),
                   nullptr,
                   0u,
                   &bytes,
                   nullptr) != FALSE;
    }
private:
    HANDLE driver_ = nullptr;
};

bool RunLive(DWORD duration_seconds,
             HandleWriter* writer,
             V1AvrcpReplayOptions options,
             V1MediaSessionMonitor* media_monitor,
             bool diagnose_media_keys,
             V1AvrcpWindowsSink* sink,
             V1AvrcpReplayStats* stats) {
    std::wstring path;
    if (!GetObserverInterfacePath(&path)) {
        std::fprintf(stderr,
                     "The observer driver interface is not present; bind "
                     "the candidate PDO first.\n");
        return false;
    }
    HANDLE handle = CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0u,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "Open observer interface failed (Win32 %lu).\n",
                     GetLastError());
        return false;
    }
    NLD_AVRCP_OBSERVER_ABI_VERSION version{};
    NLD_AVRCP_OBSERVER_STATUS status{};
    DWORD bytes = 0u;
    if (!DeviceIoControl(handle,
                         IOCTL_NLD_AVRCP_OBSERVER_GET_VERSION,
                         nullptr,
                         0u,
                         &version,
                         sizeof(version),
                         &bytes,
                         nullptr) ||
        bytes != sizeof(version) ||
        version.Major != NLD_AVRCP_OBSERVER_ABI_MAJOR ||
        version.Minor != NLD_AVRCP_OBSERVER_ABI_MINOR) {
        std::fprintf(stderr, "Observer ABI query failed.\n");
        CloseHandle(handle);
        return false;
    }
    if (!DeviceIoControl(handle,
                         IOCTL_NLD_AVRCP_OBSERVER_GET_STATUS,
                         nullptr,
                         0u,
                         &status,
                         sizeof(status),
                         &bytes,
                         nullptr) ||
        bytes != sizeof(status)) {
        std::fprintf(stderr, "Observer status query failed.\n");
        CloseHandle(handle);
        return false;
    }
    std::printf("live: interface=%ls abi=%lu.%lu generation=%llu "
                "queue=%lu flags=0x%08lX\n",
                path.c_str(),
                version.Major,
                version.Minor,
                status.AclGeneration,
                status.QueueDepth,
                status.Flags);
    if ((status.Flags &
            NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUIRED) != 0u) {
        bytes = 0u;
        if (!DeviceIoControl(handle,
                             IOCTL_NLD_AVRCP_OBSERVER_BEGIN_OBSERVATION,
                             nullptr,
                             0u,
                             nullptr,
                             0u,
                             &bytes,
                             nullptr)) {
            std::fprintf(stderr,
                         "Observer observation activation failed (Win32 %lu).\n",
                         GetLastError());
            CloseHandle(handle);
            return false;
        }
        std::printf("live: observation activation accepted; one outbound "
                    "AVCTP OPEN is now in progress\n");
    }
    if (sink != nullptr && writer != nullptr) writer->SetDriverHandle(handle);

    // BEGIN_OBSERVATION only schedules the outbound AVCTP OPEN. Do not expose
    // the manual volume window until the peer has accepted the channel and
    // the absolute-volume notification registration is active.
    constexpr DWORD kControlReadyTimeoutMs = 15000u;
    const ULONGLONG control_ready_deadline =
        GetTickCount64() + kControlReadyTimeoutMs;
    bool control_ready = IsAbsoluteVolumeControlReady(status);
    while (!control_ready && GetTickCount64() < control_ready_deadline) {
        bytes = 0u;
        if (!DeviceIoControl(handle,
                             IOCTL_NLD_AVRCP_OBSERVER_GET_STATUS,
                             nullptr,
                             0u,
                             &status,
                             sizeof(status),
                             &bytes,
                             nullptr) ||
            bytes != sizeof(status)) {
            std::fprintf(stderr,
                         "Observer control status query failed "
                         "while waiting for readiness (Win32 %lu).\n",
                         GetLastError());
            CloseHandle(handle);
            return false;
        }
        control_ready = IsAbsoluteVolumeControlReady(status);
        if (!control_ready) Sleep(100u);
    }
    if (!control_ready) {
        std::fprintf(stderr,
                     "Observer absolute-volume control did not become "
                     "ready within %lu ms (flags=0x%08lX).\n",
                     kControlReadyTimeoutMs,
                     status.Flags);
        CloseHandle(handle);
        return false;
    }
    std::printf(
        "live: control channel ready; absolute-volume notifications are "
        "active (flags=0x%08lX)\n",
        status.Flags);

    V1AvrcpControlMapperState mapper{};
    V1MediaSessionSnapshot last_media_session{};
    bool have_media_session = false;
    bool last_media_session_fallback = false;
    auto refresh_media_session = [&](std::uint64_t acl_generation) {
        if (acl_generation == 0u) return;
        bool using_fallback = false;
        const V1MediaSessionSnapshot snapshot =
            GetLiveMediaSessionSnapshot(
                media_monitor, acl_generation, &using_fallback);
        if (!have_media_session ||
            !SameLiveMediaSessionSnapshot(last_media_session, snapshot) ||
            last_media_session_fallback != using_fallback) {
            PrintLiveMediaSessionSnapshot(snapshot, using_fallback);
            last_media_session = snapshot;
            last_media_session_fallback = using_fallback;
            have_media_session = true;
        }
        options.media_session = snapshot;
        if (mapper.acl_generation_current &&
            mapper.acl_generation == acl_generation) {
            const V1AvrcpActionSet actions =
                V1AvrcpSetMediaSessionSnapshot(&mapper, snapshot);
            V1AvrcpDispatchAuthorizedActions(
                &mapper, actions, sink, stats);
        }
    };
    refresh_media_session(status.AclGeneration);
    if (sink != nullptr) {
        sink->SetMediaKeyDiagnostics(diagnose_media_keys);
    }

    const ULONGLONG deadline =
        GetTickCount64() + static_cast<ULONGLONG>(duration_seconds) * 1000u;
    bool ok = true;
    AvrcpWindowsVolume last_polled{};
    bool last_polled_valid = false;
    ULONGLONG last_poll_tick = 0u;
    while (GetTickCount64() < deadline) {
        if (mapper.acl_generation_current) {
            refresh_media_session(mapper.acl_generation);
        }
        NLD_AVRCP_OBSERVER_EVENT event{};
        bytes = 0u;
        if (DeviceIoControl(handle,
                            IOCTL_NLD_AVRCP_OBSERVER_DEQUEUE_EVENT,
                            nullptr,
                            0u,
                            &event,
                            sizeof(event),
                            &bytes,
                            nullptr)) {
            if (bytes != sizeof(event)) {
                std::fprintf(stderr, "Short observer event.\n");
                ok = false;
                break;
            }
            V1AvrcpObservedEvent observed{};
            observed.generation = event.AclGeneration;
            observed.flags = event.Flags;
            switch (event.Type) {
                case NldAvrcpObserverEventAclConnected:
                    observed.kind =
                        V1AvrcpObservedEvent::Kind::GenerationStarted;
                    break;
                case NldAvrcpObserverEventAclDisconnected:
                    observed.kind =
                        V1AvrcpObservedEvent::Kind::GenerationEnded;
                    break;
                case NldAvrcpObserverEventVolumeCapability:
                    observed.kind =
                        V1AvrcpObservedEvent::Kind::VolumeCapability;
                    observed.value = event.Value0;
                    break;
                case NldAvrcpObserverEventAbsoluteVolume:
                    observed.kind =
                        V1AvrcpObservedEvent::Kind::AbsoluteVolume;
                    observed.value = event.Value0;
                    observed.volume_event =
                        (event.Flags & NLD_AVRCP_EVENT_FLAG_CHANGED) != 0u
                            ? AvrcpXm5VolumeEvent::RemoteNotification
                            : AvrcpXm5VolumeEvent::CommandResponse;
                    break;
                case NldAvrcpObserverEventPassThrough:
                    observed.kind =
                        V1AvrcpObservedEvent::Kind::PassThrough;
                    observed.value = event.Value0;
                    break;
                case NldAvrcpObserverEventWriteResponse:
                    if ((event.Value0 & 0xFFu) == 0x50u) {
                        observed.kind =
                            V1AvrcpObservedEvent::Kind::AbsoluteVolume;
                        observed.value = event.RawPrefixHigh & 0xFFu;
                        observed.volume_event =
                            AvrcpXm5VolumeEvent::CommandResponse;
                        break;
                    }
                    if (stats != nullptr) ++stats->ignored_lines;
                    continue;
                case NldAvrcpObserverEventVendorCommand:
                    if (diagnose_media_keys &&
                        (event.Value0 & 0xFFu) == 0x31u &&
                        (event.RawPrefixHigh & 0xFFu) == 0x01u) {
                        std::printf(
                            "diagnostic: remote register-notification "
                            "pdu=0x31 event=0x01 (playback-status)\n");
                    }
                    if ((event.Value0 & 0xFFu) != 0x50u) {
                        if (stats != nullptr) ++stats->ignored_lines;
                        continue;
                    }
                    observed.kind =
                        V1AvrcpObservedEvent::Kind::SetAbsoluteVolumeCommand;
                    observed.value = event.RawPrefixHigh & 0xFFu;
                    break;
                default:
                    if (stats != nullptr) ++stats->ignored_lines;
                    continue;
            }
            if (observed.kind ==
                V1AvrcpObservedEvent::Kind::GenerationStarted) {
                refresh_media_session(observed.generation);
            }
            if (observed.kind ==
                    V1AvrcpObservedEvent::Kind::AbsoluteVolume ||
                observed.kind ==
                    V1AvrcpObservedEvent::Kind::VolumeCapability) {
                std::printf(
                    "event absolute-volume kind=%s value=%u flags=0x%08lX "
                    "sequence=%llu\n",
                    observed.kind ==
                            V1AvrcpObservedEvent::Kind::AbsoluteVolume
                        ? (observed.volume_event ==
                               AvrcpXm5VolumeEvent::RemoteNotification
                               ? "remote-notification"
                               : "command-response")
                        : "capability",
                    static_cast<unsigned int>(observed.value),
                    static_cast<unsigned long>(observed.flags),
                    static_cast<unsigned long long>(event.Sequence));
            }
            const bool is_pass_through =
                observed.kind == V1AvrcpObservedEvent::Kind::PassThrough;
            const std::uint64_t action_sets_before =
                stats != nullptr ? stats->action_sets : 0u;
            const std::uint64_t accepted_sequence_before =
                mapper.pass_through.accepted_event_sequence;
            const bool held_operation_before =
                mapper.pass_through.held_operation_valid;
            const auto held_operation_value_before =
                mapper.pass_through.held_operation;
            if (diagnose_media_keys && is_pass_through) {
                const auto& eligibility = mapper.media_eligibility;
                std::printf(
                    "diagnostic: pass-through sequence=%llu "
                    "timestamp_100ns=%llu generation=%llu flags=0x%08lX "
                    "response=%s operation=0x%02X (%s) released=%s "
                    "playback=%s session_present=%s route=%s owner_lease=%llu "
                    "eligibility=play:%s pause:%s next:%s previous:%s "
                    "held_before=%s held_operation_before=%u\n",
                    static_cast<unsigned long long>(event.Sequence),
                    static_cast<unsigned long long>(event.Timestamp100ns),
                    static_cast<unsigned long long>(observed.generation),
                    static_cast<unsigned long>(event.Flags),
                    (event.Flags & NLD_AVRCP_EVENT_FLAG_RESPONSE) != 0u
                        ? "yes"
                        : "no",
                    static_cast<unsigned int>(observed.value & 0x7Fu),
                    LivePassThroughOperationName(
                        static_cast<std::uint8_t>(observed.value & 0x7Fu)),
                    (observed.flags & 0x02u) != 0u ? "yes" : "no",
                    V1AvrcpPlaybackStateName(mapper.pc_playback),
                    eligibility.session_present ? "yes" : "no",
                    mapper.media_routing_enabled ? "yes" : "no",
                    static_cast<unsigned long long>(mapper.owner_lease),
                    eligibility.play_eligible ? "yes" : "no",
                    eligibility.pause_eligible ? "yes" : "no",
                    eligibility.next_eligible ? "yes" : "no",
                    eligibility.previous_eligible ? "yes" : "no",
                    held_operation_before ? "yes" : "no",
                    static_cast<unsigned int>(held_operation_value_before));
            }
            // The observer driver resets its event queue when it restarts
            // the generation (remote disconnect), which discards the End
            // event pushed right before the restart. A live observation
            // therefore sees a new GenerationStarted while the mapper still
            // considers the previous generation current. End the current
            // generation first so the mapper's fail-closed contract stays
            // satisfied.
            if (observed.kind ==
                    V1AvrcpObservedEvent::Kind::GenerationStarted &&
                mapper.acl_generation_current) {
                V1AvrcpObservedEvent ended{};
                ended.kind = V1AvrcpObservedEvent::Kind::GenerationEnded;
                ended.generation = mapper.acl_generation;
                (void)V1AvrcpFeedEvent(
                    &mapper, ended, options, sink, stats);
            }
            const bool feed_ok = V1AvrcpFeedEvent(
                &mapper, observed, options, sink, stats);
            if (diagnose_media_keys && is_pass_through) {
                const std::uint64_t action_sets_after =
                    stats != nullptr ? stats->action_sets : 0u;
                std::printf(
                    "diagnostic: pass-through result action_sets_delta=%llu "
                    "accepted_sequence_delta=%llu mapper_playback=%s "
                    "held_after=%s held_operation_after=%u "
                    "feed_ok=%s\n",
                    static_cast<unsigned long long>(
                        action_sets_after - action_sets_before),
                    static_cast<unsigned long long>(
                        mapper.pass_through.accepted_event_sequence -
                        accepted_sequence_before),
                     V1AvrcpPlaybackStateName(mapper.pc_playback),
                    mapper.pass_through.held_operation_valid ? "yes" : "no",
                    static_cast<unsigned int>(
                        mapper.pass_through.held_operation),
                    feed_ok ? "yes" : "no");
            }
            if (!feed_ok) {
                ok = false;
                break;
            }
            continue;
        }
        const DWORD error = GetLastError();
        if (error != ERROR_NO_MORE_ITEMS) {
            std::fprintf(stderr,
                         "Observer dequeue failed (Win32 %lu).\n", error);
            ok = false;
            break;
        }
        if (sink != nullptr && mapper.acl_generation_current &&
            GetTickCount64() - last_poll_tick >= 200u) {
            last_poll_tick = GetTickCount64();
            AvrcpWindowsVolume current{};
            if (sink->QueryWindowsVolume(&current)) {
                if (!last_polled_valid ||
                    last_polled.percent != current.percent ||
                    last_polled.muted != current.muted) {
                    last_polled = current;
                    last_polled_valid = true;
                    const V1AvrcpActionSet poll_actions =
                        V1AvrcpObserveWindowsVolume(
                            &mapper, mapper.acl_generation, current);
                    V1AvrcpDispatchAuthorizedActions(
                        &mapper, poll_actions, sink, stats);
                } else {
                    last_polled_valid = true;
                }
            }
        }
        Sleep(50u);
    }
    if (ok && mapper.acl_generation_current) {
        V1AvrcpObservedEvent ended;
        ended.kind = V1AvrcpObservedEvent::Kind::GenerationEnded;
        ended.generation = mapper.acl_generation;
        (void)V1AvrcpFeedEvent(&mapper, ended, options, sink, stats);
    }
    CloseHandle(handle);
    return ok;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    (void)setvbuf(stdout, nullptr, _IONBF, 0u);
    (void)setvbuf(stderr, nullptr, _IONBF, 0u);
    std::wstring replay_path;
    std::wstring filter_replay_path;
    V1AvrcpReplayOptions options;
    bool apply = false;
    bool live = false;
    bool diagnose_media_keys = false;
    DWORD duration_seconds = 120u;
    bool show_help = false;
    for (int index = 1; index < argc; ++index) {
        if (std::wcscmp(argv[index], kOptionHelp) == 0 ||
            std::wcscmp(argv[index], L"-h") == 0) {
            show_help = true;
            break;
        }
        if (std::wcscmp(argv[index], kOptionReplay) == 0 &&
            index + 1 < argc) {
            replay_path = argv[++index];
            continue;
        }
        if (std::wcscmp(argv[index], kOptionReplayFilter) == 0 &&
            index + 1 < argc) {
            filter_replay_path = argv[++index];
            continue;
        }
        if (std::wcscmp(argv[index], kOptionLive) == 0) {
            live = true;
            continue;
        }
        if (std::wcscmp(argv[index], kOptionDuration) == 0 &&
            index + 1 < argc) {
            std::uint64_t parsed = 0u;
            if (!ParseUnsigned64(argv[++index], &parsed) ||
                parsed == 0u || parsed > 600u) {
                std::fprintf(stderr,
                             "Invalid --duration-seconds value.\n");
                return 2;
            }
            duration_seconds = static_cast<DWORD>(parsed);
            continue;
        }
        if (std::wcscmp(argv[index], kOptionVolumeSync) == 0) {
            options.volume_sync = true;
            continue;
        }
        if (std::wcscmp(argv[index], kOptionNoVolumeSync) == 0) {
            options.volume_sync = false;
            continue;
        }
        if (std::wcscmp(argv[index], kOptionMediaRouting) == 0) {
            options.media_routing = true;
            continue;
        }
        if (std::wcscmp(argv[index], kOptionNoMediaRouting) == 0) {
            options.media_routing = false;
            continue;
        }
        if (std::wcscmp(argv[index], kOptionOwnerLease) == 0 &&
            index + 1 < argc) {
            std::uint64_t lease = 0u;
            if (!ParseUnsigned64(argv[++index], &lease) || lease == 0u) {
                std::fprintf(stderr, "Invalid --owner-lease value.\n");
                return 2;
            }
            options.owner_lease = lease;
            continue;
        }
        if (std::wcscmp(argv[index], kOptionInitialVolume) == 0 &&
            index + 1 < argc) {
            std::uint64_t percent = 0u;
            if (!ParseUnsigned64(argv[++index], &percent) || percent > 100u) {
                std::fprintf(stderr,
                             "Invalid --initial-volume-percent value.\n");
                return 2;
            }
            options.initial_windows_volume.percent =
                static_cast<std::uint8_t>(percent);
            continue;
        }
        if (std::wcscmp(argv[index], kOptionApply) == 0) {
            apply = true;
            continue;
        }
        if (std::wcscmp(argv[index], kOptionDiagnoseMediaKeys) == 0) {
            diagnose_media_keys = true;
            continue;
        }
        std::fprintf(stderr, "Unknown argument: %ls\n", argv[index]);
        return 2;
    }
    if (show_help) {
        PrintHelp(argv[0]);
        return 0;
    }
    const int mode_count =
        (live ? 1 : 0) + (replay_path.empty() ? 0 : 1) +
        (filter_replay_path.empty() ? 0 : 1);
    if (mode_count != 1) {
        PrintHelp(argv[0]);
        return 2;
    }

    const HRESULT initialize_hr = CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialize_hr) &&
        initialize_hr != RPC_E_CHANGED_MODE) {
        std::fprintf(stderr, "CoInitializeEx failed 0x%08lX\n",
                     initialize_hr);
        return 4;
    }

    bool ok = false;
    {
        HandleWriter writer;
        V1AvrcpWindowsSink sink(apply, &writer);
        sink.SetMediaKeyDiagnostics(diagnose_media_keys);
        V1AvrcpReplayStats stats{};
        V1MediaSessionMonitor media_session_monitor;
        DWORD media_monitor_error = ERROR_SUCCESS;
        if (live && !media_session_monitor.Start(&media_monitor_error)) {
            std::fprintf(
                stderr,
                "GSMTC media-session monitor unavailable (Win32 %lu); "
                "the bounded LDAC-session fallback will be used.\n",
                media_monitor_error);
        }
        if (live) {
            ok = RunLive(
                duration_seconds,
                &writer,
                options,
                &media_session_monitor,
                diagnose_media_keys,
                &sink,
                &stats);
        } else if (!filter_replay_path.empty()) {
            std::string log_text;
            if (!ReadTextFile(filter_replay_path, &log_text)) {
                std::fprintf(
                    stderr, "Could not read filter replay file: %ls\n",
                    filter_replay_path.c_str());
                CoUninitialize();
                return 3;
            }
            ok = V1AvrcpRunFilterReplay(log_text, options, &sink, &stats);
        } else {
            std::string log_text;
            if (!ReadTextFile(replay_path, &log_text)) {
                std::fprintf(stderr, "Could not read replay file: %ls\n",
                             replay_path.c_str());
                CoUninitialize();
                return 3;
            }
            ok = V1AvrcpRunReplay(log_text, options, &sink, &stats);
        }
        media_session_monitor.Stop();
        std::printf(
            "executor: events=%llu ignored=%llu action_sets=%llu "
            "sink_accepted=%llu generation_ended=%llu errors=%llu "
            "apply=%s\n",
            static_cast<unsigned long long>(stats.recognized_events),
            static_cast<unsigned long long>(stats.ignored_lines),
            static_cast<unsigned long long>(stats.action_sets),
            static_cast<unsigned long long>(stats.sink_accepted),
            static_cast<unsigned long long>(stats.generation_ended),
            static_cast<unsigned long long>(stats.errors),
            apply ? "yes" : "no");
    }
    CoUninitialize();
    return ok ? 0 : 1;
}
