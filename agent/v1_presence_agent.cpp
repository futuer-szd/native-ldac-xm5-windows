#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <limits>
#include <string>

#include "v1_daily_instance.h"
#include "v1_daily_config_ipc.h"
#include "v1_avrcp_handoff_ipc.h"
#include "v1_avrcp_filter_host.h"
#include "v1_avrcp_observer_host.h"
#include "v1_engine_ready_host.h"
#include "v1_endpoint_presence_sink.h"
#include "v1_hfp_capture_monitor.h"
#include "v1_hfp_lifecycle_adapter.h"
#include "v1_hfp_render_endpoint_monitor.h"
#include "v1_hfp_shadow_state.h"
#include "v1_lifecycle.h"
#include "v1_media_session_monitor.h"
#include "v1_render_demand_tracker.h"
#include "v1_transport_open_stability.h"
#include "xm5_acl_watcher.h"

namespace {

struct Options {
    DWORD run_for_ms = 0u;
    bool run_for_ms_set = false;
    DWORD render_start_timeout_ms = 0u;
    DWORD transport_open_render_stability_ms = 0u;
    std::wstring state_path;
    bool endpoint_presence = false;
    bool observe_render_demand = false;
    bool observe_engine_ready = false;
    bool exercise_transport_worker = false;
    bool exercise_transport_discovery = false;
    bool exercise_transport_configuration = false;
    bool exercise_transport_silence = false;
    bool exercise_transport_pcm_burst = false;
    bool pcm_fast_signaling_acquisition = false;
    bool await_playback_disconnect = false;
    bool await_playback_reconnect = false;
    DWORD playback_reconnect_generations = 2u;
    bool playback_reconnect_generations_set = false;
    bool daily_mode = false;
    std::wstring daily_quality = L"hq";
    bool daily_quality_set = false;
    std::wstring daily_channel_mode = L"stereo";
    bool daily_channel_mode_set = false;
    unsigned daily_sample_rate_hz = 48000u;
    unsigned daily_bits_per_sample = 16u;
    bool daily_sample_rate_set = false;
    bool daily_bits_set = false;
    bool volume_sync = false;
    bool handoff = false;
    bool hfp_transport_switch = false;
    bool stop_daily = false;
    bool instance_suffix_set = false;
    std::wstring instance_suffix = L"default";
    std::wstring engine_stub_path;
    std::wstring transport_result_path;
};

struct TrialState {
    native_ldac::agent::V1LifecycleState lifecycle;
    unsigned int connected_events = 0u;
    unsigned int disconnected_events = 0u;
    unsigned int publish_present_actions = 0u;
    unsigned int publish_absent_actions = 0u;
    unsigned int fail_mute_actions = 0u;
    unsigned int transport_open_actions = 0u;
    unsigned int transport_open_executed = 0u;
    unsigned int transport_open_stability_waits = 0u;
    unsigned int transport_open_stability_resets = 0u;
    unsigned int transport_open_stable_authorizations = 0u;
    unsigned int transport_retryable_failures = 0u;
    unsigned int transport_retries_scheduled = 0u;
    unsigned int transport_retry_budget_exhausted = 0u;
    unsigned int capabilities_discovered_events = 0u;
    unsigned int discovery_sessions_completed = 0u;
    unsigned int configuration_sessions_completed = 0u;
    unsigned int silence_sessions_completed = 0u;
    unsigned int pcm_burst_sessions_completed = 0u;
    unsigned int transport_graceful_stop_actions = 0u;
    unsigned int transport_cancel_actions = 0u;
    unsigned int media_started_events = 0u;
    unsigned int media_stopped_events = 0u;
    unsigned int media_failed_events = 0u;
    unsigned int avrcp_observer_activation_attempts = 0u;
    unsigned int avrcp_observer_activation_failures = 0u;
    std::uint64_t avrcp_observer_failed_generation = 0u;
    unsigned int avrcp_observer_poll_failures = 0u;
    unsigned int hfp_capture_samples = 0u;
    unsigned int hfp_shadow_stop_requests = 0u;
    unsigned int hfp_shadow_enter_requests = 0u;
    unsigned int hfp_shadow_exit_requests = 0u;
    unsigned int hfp_shadow_resume_requests = 0u;
    unsigned int hfp_lifecycle_suspend_plans = 0u;
    unsigned int hfp_lifecycle_resume_plans = 0u;
    unsigned int hfp_lifecycle_output_enter_plans = 0u;
    unsigned int hfp_lifecycle_output_exit_plans = 0u;
    unsigned int hfp_lifecycle_stale_plans = 0u;
    unsigned int hfp_lifecycle_invalid_plans = 0u;
    unsigned int hfp_lifecycle_executions = 0u;
    unsigned int hfp_lifecycle_execution_failures = 0u;
    std::uint64_t hfp_capture_sequence = 0u;
    bool hfp_capture_monitor_ready = false;
    bool hfp_capture_matched = false;
    bool hfp_capture_active = false;
    bool hfp_render_monitor_ready = false;
    bool hfp_render_endpoint_present = false;
    bool hfp_render_endpoint_matched = false;
    bool hfp_render_bridge_ready = false;
    std::uint64_t hfp_render_sequence = 0u;
    DWORD hfp_render_last_error = ERROR_SUCCESS;
    bool hfp_transport_switch_enabled = false;
    unsigned int transport_stop_acknowledgements = 0u;
    unsigned int child_processes_started = 0u;
    unsigned int endpoint_presence_updates = 0u;
    unsigned int endpoint_presence_failures = 0u;
    unsigned int endpoint_presence_rebinds = 0u;
    unsigned int render_query_count = 0u;
    unsigned int render_query_failures = 0u;
    DWORD render_query_last_error = ERROR_SUCCESS;
    ULONGLONG render_query_recovery_next_tick = 0u;
    unsigned int render_started_events = 0u;
    unsigned int render_stopped_events = 0u;
    unsigned int pre_media_render_stop_events = 0u;
    unsigned int render_stop_deferred_events = 0u;
    unsigned int render_stop_resumed_events = 0u;
    unsigned int render_stop_timeout_events = 0u;
    unsigned int render_stop_acl_cancelled_events = 0u;
    unsigned int engine_start_requests = 0u;
    unsigned int engine_stop_requests = 0u;
    unsigned int engine_ready_events = 0u;
    unsigned int engine_exit_events = 0u;
    unsigned int engine_start_failures = 0u;
    unsigned int engine_ready_timeouts = 0u;
    unsigned int engine_stop_failures = 0u;
    unsigned int engine_graceful_stops = 0u;
    unsigned int engine_unexpected_exits = 0u;
    std::uint64_t transport_worker_sequence = 0u;
    DWORD last_engine_exit_code = 0u;
    std::uint64_t last_stream_epoch = 0u;
    bool endpoint_sink_enabled = false;
    bool render_observer_enabled = false;
    bool engine_ready_observer_enabled = false;
    bool transport_worker_exercise_enabled = false;
    bool transport_discovery_exercise_enabled = false;
    bool transport_configuration_exercise_enabled = false;
    bool transport_silence_exercise_enabled = false;
    bool transport_pcm_burst_exercise_enabled = false;
    bool playback_disconnect_wait_enabled = false;
    bool playback_reconnect_wait_enabled = false;
    unsigned int playback_reconnect_target_generations = 1u;
    bool playback_disconnect_fail_closed_release = false;
    bool render_start_timed_out = false;
    bool render_stop_pending = false;
    bool daily_mode = false;
    bool avrcp_volume_sync_enabled = false;
    bool avrcp_handoff_enabled = false;
    bool avrcp_observer_enabled = false;
    bool avrcp_observer_active = false;
    ULONGLONG avrcp_control_ready_deadline = 0u;
    unsigned int avrcp_handoff_requests = 0u;
    unsigned int avrcp_handoff_errors = 0u;
    unsigned int avrcp_handoff_restores = 0u;
    unsigned int avrcp_handoff_restore_errors = 0u;
    std::uint64_t avrcp_handoff_failed_generation = 0u;
    // The elevated owner switch is performed only after the real A2DP
    // MediaStarted edge. This preserves the Microsoft bootstrap state that
    // the XM5 requires before accepting Native AVCTP PSM 0x0017.
    std::uint64_t avrcp_handoff_ready_generation = 0u;
    native_ldac::agent::V1MediaSessionPlayback avrcp_pc_playback =
        native_ldac::agent::V1MediaSessionPlayback::Absent;
    bool avrcp_pc_playback_valid = false;
    std::uint64_t avrcp_pc_playback_generation = 0u;
    unsigned int avrcp_pc_playback_snapshot_changes = 0u;
    bool avrcp_pc_play_enabled = false;
    bool avrcp_pc_pause_enabled = false;
    bool avrcp_pc_next_enabled = false;
    bool avrcp_pc_previous_enabled = false;
    std::wstring config_pipe;
    native_ldac::agent::V1DailyQuality requested_quality =
        native_ldac::agent::V1DailyQuality::Hq;
    native_ldac::agent::V1DailyQuality applied_quality =
        native_ldac::agent::V1DailyQuality::Hq;
    std::uint64_t requested_config_revision = 0u;
    std::uint64_t applied_config_revision = 0u;
    unsigned int config_rejected_count = 0u;
    DWORD config_last_error = ERROR_SUCCESS;
    native_ldac::agent::V1RenderDemandTracker render_tracker;
    native_ldac::agent::V1TransportOpenStabilityGate
        transport_open_stability;
};

bool ParseDword(const wchar_t* text, DWORD* value) {
    if (text == nullptr || value == nullptr || text[0] == L'\0') {
        return false;
    }
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(text, &end, 10);
    if (end == text || end == nullptr || *end != L'\0' ||
        parsed > MAXDWORD) {
        return false;
    }
    *value = static_cast<DWORD>(parsed);
    return true;
}

bool ParseUnsigned(const wchar_t* text, unsigned* value) {
    if (text == nullptr || value == nullptr || text[0] == L'\0') {
        return false;
    }
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(text, &end, 10);
    if (end == text || end == nullptr || *end != L'\0' ||
        parsed > std::numeric_limits<unsigned>::max()) {
        return false;
    }
    *value = static_cast<unsigned>(parsed);
    return true;
}

bool HasExecutableFileName(const std::wstring& path,
                           const wchar_t* expected_name) {
    if (path.empty() || expected_name == nullptr) {
        return false;
    }
    const std::size_t separator = path.find_last_of(L"\\/");
    const wchar_t* file_name = path.c_str() +
        (separator == std::wstring::npos ? 0u : separator + 1u);
    return _wcsicmp(file_name, expected_name) == 0;
}

bool ParseOptions(int argc, wchar_t** argv, Options* options) {
    if (options == nullptr) {
        return false;
    }
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument(argv[index]);
        if (argument == L"--run-for-ms") {
            if (index + 1 >= argc ||
                !ParseDword(argv[++index], &options->run_for_ms)) {
                return false;
            }
            options->run_for_ms_set = true;
        } else if (argument == L"--render-start-timeout-ms") {
            if (index + 1 >= argc ||
                !ParseDword(argv[++index],
                            &options->render_start_timeout_ms)) {
                return false;
            }
        } else if (argument ==
                   L"--transport-open-render-stability-ms") {
            if (index + 1 >= argc ||
                !ParseDword(
                    argv[++index],
                    &options->transport_open_render_stability_ms)) {
                return false;
            }
        } else if (argument == L"--state") {
            if (index + 1 >= argc || argv[index + 1][0] == L'\0') {
                return false;
            }
            options->state_path = argv[++index];
        } else if (argument == L"--endpoint-presence") {
            options->endpoint_presence = true;
        } else if (argument == L"--observe-render-demand") {
            options->observe_render_demand = true;
        } else if (argument == L"--observe-engine-ready") {
            options->observe_engine_ready = true;
        } else if (argument == L"--exercise-transport-worker") {
            options->exercise_transport_worker = true;
        } else if (argument == L"--exercise-transport-discovery") {
            options->exercise_transport_discovery = true;
        } else if (argument == L"--exercise-transport-configuration") {
            options->exercise_transport_configuration = true;
        } else if (argument == L"--exercise-transport-silence") {
            options->exercise_transport_silence = true;
        } else if (argument == L"--exercise-transport-pcm-burst") {
            options->exercise_transport_pcm_burst = true;
        } else if (argument == L"--pcm-fast-signaling-acquisition") {
            options->pcm_fast_signaling_acquisition = true;
        } else if (argument == L"--await-playback-disconnect") {
            options->await_playback_disconnect = true;
        } else if (argument == L"--await-playback-reconnect") {
            options->await_playback_reconnect = true;
        } else if (argument == L"--daily") {
            options->daily_mode = true;
        } else if (argument == L"--quality") {
            if (index + 1 >= argc || argv[index + 1][0] == L'\0') {
                return false;
            }
            options->daily_quality = argv[++index];
            options->daily_quality_set = true;
        } else if (argument == L"--channel-mode") {
            if (index + 1 >= argc || argv[index + 1][0] == L'\0') {
                return false;
            }
            options->daily_channel_mode = argv[++index];
            options->daily_channel_mode_set = true;
        } else if (argument == L"--sample-rate") {
            if (index + 1 >= argc ||
                !ParseUnsigned(argv[++index], &options->daily_sample_rate_hz) ||
                (options->daily_sample_rate_hz != 44100u &&
                 options->daily_sample_rate_hz != 48000u &&
                 options->daily_sample_rate_hz != 88200u &&
                 options->daily_sample_rate_hz != 96000u)) {
                return false;
            }
            options->daily_sample_rate_set = true;
        } else if (argument == L"--bits") {
            if (index + 1 >= argc ||
                !ParseUnsigned(argv[++index], &options->daily_bits_per_sample) ||
                (options->daily_bits_per_sample != 16u &&
                 options->daily_bits_per_sample != 24u)) {
                return false;
            }
            options->daily_bits_set = true;
        } else if (argument == L"--volume-sync") {
            options->volume_sync = true;
        } else if (argument == L"--handoff") {
            return false;
        } else if (argument == L"--hfp-transport-switch") {
            options->hfp_transport_switch = true;
        } else if (argument == L"--stop-daily") {
            options->stop_daily = true;
        } else if (argument == L"--instance-suffix") {
            if (index + 1 >= argc || argv[index + 1][0] == L'\0') {
                return false;
            }
            options->instance_suffix = argv[++index];
            options->instance_suffix_set = true;
        } else if (argument == L"--playback-reconnect-generations") {
            if (index + 1 >= argc ||
                !ParseDword(argv[++index],
                            &options->playback_reconnect_generations)) {
                return false;
            }
            options->playback_reconnect_generations_set = true;
        } else if (argument == L"--transport-result") {
            if (index + 1 >= argc || argv[index + 1][0] == L'\0') {
                return false;
            }
            options->transport_result_path = argv[++index];
        } else if (argument == L"--engine-stub" ||
                   argument == L"--engine-executable") {
            if (index + 1 >= argc || argv[index + 1][0] == L'\0') {
                return false;
            }
            options->engine_stub_path = argv[++index];
        } else {
            return false;
        }
    }
    const bool await_playback_disconnect =
        options->await_playback_disconnect ||
        options->await_playback_reconnect;
    const unsigned int transport_modes =
        static_cast<unsigned>(options->exercise_transport_worker) +
        static_cast<unsigned>(options->exercise_transport_discovery) +
        static_cast<unsigned>(options->exercise_transport_configuration) +
        static_cast<unsigned>(options->exercise_transport_silence) +
        static_cast<unsigned>(options->exercise_transport_pcm_burst);
    native_ldac::agent::V1DailyQuality parsed_daily_quality{};
    const bool daily_quality_valid =
        native_ldac::agent::ParseV1DailyQuality(
            options->daily_quality, &parsed_daily_quality);
    const bool daily_channel_mode_valid =
        _wcsicmp(options->daily_channel_mode.c_str(), L"stereo") == 0 ||
        _wcsicmp(options->daily_channel_mode.c_str(), L"dual") == 0 ||
        _wcsicmp(options->daily_channel_mode.c_str(), L"mono") == 0;
    if (options->stop_daily) {
        return !options->daily_mode &&
            !options->run_for_ms_set &&
            options->render_start_timeout_ms == 0u &&
            options->transport_open_render_stability_ms == 0u &&
            options->state_path.empty() &&
            !options->endpoint_presence &&
            !options->observe_render_demand &&
            !options->observe_engine_ready &&
            transport_modes == 0u &&
            !options->pcm_fast_signaling_acquisition &&
            !await_playback_disconnect &&
            !options->playback_reconnect_generations_set &&
            options->engine_stub_path.empty() &&
            options->transport_result_path.empty() &&
            !options->hfp_transport_switch &&
            !options->daily_quality_set &&
            !options->daily_channel_mode_set &&
            native_ldac::agent::IsValidV1DailyInstanceSuffix(
                options->instance_suffix);
    }
    if (options->daily_mode) {
        if (options->run_for_ms_set ||
            options->render_start_timeout_ms != 0u ||
            options->transport_open_render_stability_ms != 0u ||
            options->state_path.empty() ||
            options->endpoint_presence ||
            options->observe_render_demand ||
            options->observe_engine_ready ||
            transport_modes != 0u ||
            options->pcm_fast_signaling_acquisition ||
            await_playback_disconnect ||
            options->playback_reconnect_generations_set ||
            options->engine_stub_path.empty() ||
            !HasExecutableFileName(
                options->engine_stub_path,
                L"v1_transport_daily_worker.exe") ||
            options->transport_result_path.empty() ||
            !native_ldac::agent::IsValidV1DailyInstanceSuffix(
                options->instance_suffix) ||
            !daily_quality_valid || !daily_channel_mode_valid) {
            return false;
        }
        if (options->daily_sample_rate_set != options->daily_bits_set) {
            return false;
        }
        options->endpoint_presence = true;
        options->observe_render_demand = true;
        options->observe_engine_ready = true;
        options->exercise_transport_pcm_burst = true;
        options->pcm_fast_signaling_acquisition = true;
        options->transport_open_render_stability_ms = 1000u;
        return true;
    }
    return !options->instance_suffix_set &&
           !options->daily_quality_set &&
           !options->daily_channel_mode_set &&
           !options->hfp_transport_switch &&
           options->run_for_ms >= 1000u &&
           options->run_for_ms <= 600000u &&
           !options->state_path.empty() &&
           (!options->observe_render_demand || options->endpoint_presence) &&
           (!options->observe_engine_ready ||
            (options->observe_render_demand &&
             !options->engine_stub_path.empty())) &&
           (!options->exercise_transport_worker ||
            options->observe_engine_ready) &&
           (!options->exercise_transport_discovery ||
            (options->observe_engine_ready &&
             !options->transport_result_path.empty())) &&
           (!options->exercise_transport_configuration ||
            (options->observe_engine_ready &&
             !options->transport_result_path.empty())) &&
           (!options->exercise_transport_silence ||
            (options->observe_engine_ready &&
             !options->transport_result_path.empty())) &&
           (!options->exercise_transport_pcm_burst ||
            (options->observe_engine_ready &&
             !options->transport_result_path.empty())) &&
           (!options->pcm_fast_signaling_acquisition ||
            options->exercise_transport_pcm_burst) &&
           (!await_playback_disconnect ||
            options->exercise_transport_pcm_burst) &&
           !(options->await_playback_disconnect &&
             options->await_playback_reconnect) &&
           (!options->playback_reconnect_generations_set ||
            options->await_playback_reconnect) &&
           (!options->await_playback_reconnect ||
            (options->playback_reconnect_generations >= 2u &&
             options->playback_reconnect_generations <= 3u)) &&
           (options->render_start_timeout_ms == 0u ||
            (await_playback_disconnect &&
             options->render_start_timeout_ms >= 5000u &&
             options->render_start_timeout_ms <= 120000u)) &&
           (options->transport_open_render_stability_ms == 0u ||
            (options->exercise_transport_pcm_burst &&
             options->transport_open_render_stability_ms >= 250u &&
             options->transport_open_render_stability_ms <= 5000u)) &&
           transport_modes <= 1u &&
           (options->exercise_transport_discovery ||
            options->exercise_transport_configuration ||
            options->exercise_transport_silence ||
            options->exercise_transport_pcm_burst ||
            options->transport_result_path.empty()) &&
           (options->observe_engine_ready ||
            options->engine_stub_path.empty());
}

void PrintUsage() {
    std::wprintf(
        L"Usage: v1_presence_agent.exe --run-for-ms <1000..600000> "
        L"--state <path> [--endpoint-presence "
        L"[--observe-render-demand [--observe-engine-ready "
        L"--engine-executable <path> "
        L"[--exercise-transport-worker | "
        L"--exercise-transport-discovery --transport-result <path> | "
        L"--exercise-transport-configuration "
        L"--transport-result <path> | --exercise-transport-silence "
        L"--transport-result <path> | --exercise-transport-pcm-burst "
        L"--transport-result <path> "
        L"[--pcm-fast-signaling-acquisition] "
        L"[--transport-open-render-stability-ms <250..5000>] "
        L"[--await-playback-disconnect | --await-playback-reconnect "
        L"[--playback-reconnect-generations <2..3>] "
        L"[--render-start-timeout-ms <5000..120000>]]]]]\n"
        L"Observes exact XM5 ACL events and reduces presence state only.\n"
        L"The optional endpoint sink publishes only the physical-presence "
        L"lease.\n"
        L"The demand observer only GETs PCM Info while XM5 is present.\n"
        L"The engine-ready observer starts one contained no-media stub; "
        L"transport OPEN actions remain suppressed unless the explicit "
        L"event-only transport worker exercise is selected.\n"
        L"Daily use: v1_presence_agent.exe --daily --state <path> "
        L"--engine-executable <v1_transport_daily_worker.exe> "
        L"--transport-result <path> [--instance-suffix <safe-name>] "
        L"[--quality hq|sq|mq] [--volume-sync] "
        L"[--channel-mode stereo|dual|mono] "
        L"[--sample-rate 44100|48000|88200|96000] "
        L"[--bits 16|24] "
        L"[--hfp-transport-switch]\n"
        L"--volume-sync enables the B2 write mode for the media-scoped "
        L"AVRCP session (mapper volume sync + media routing + "
        L"SEND_COMMAND); default is ObserveOnly.\n"
        L"--hfp-transport-switch enables the experimental transport-only "
        L"HFP suspend/resume executor; it does not yet bridge Native LDAC "
        L"render to the Windows Hands-Free output. Default is off.\n"
        L"Stop daily use: v1_presence_agent.exe --stop-daily "
        L"[--instance-suffix <safe-name>]\n");
}

const wchar_t* PresenceName(
    native_ldac::agent::V1PhysicalPresence presence) {
    return presence == native_ldac::agent::V1PhysicalPresence::Present
        ? L"present"
        : L"absent";
}

const wchar_t* RenderDemandName(
    native_ldac::agent::V1RenderDemand demand) {
    return demand == native_ldac::agent::V1RenderDemand::Running
        ? L"running"
        : L"idle";
}

const wchar_t* MediaPlaybackName(
    native_ldac::agent::V1MediaSessionPlayback playback) {
    switch (playback) {
        case native_ldac::agent::V1MediaSessionPlayback::Playing:
            return L"playing";
        case native_ldac::agent::V1MediaSessionPlayback::Paused:
            return L"paused";
        case native_ldac::agent::V1MediaSessionPlayback::Stopped:
            return L"stopped";
        case native_ldac::agent::V1MediaSessionPlayback::Absent:
        default:
            return L"absent";
    }
}

const wchar_t* ModeName(const TrialState& state) {
    if (state.daily_mode) {
        return L"daily";
    }
    if (state.transport_pcm_burst_exercise_enabled) {
        return L"transport-pcm-burst-exercise";
    }
    if (state.transport_silence_exercise_enabled) {
        return L"transport-silence-exercise";
    }
    if (state.transport_configuration_exercise_enabled) {
        return L"transport-configuration-exercise";
    }
    if (state.transport_discovery_exercise_enabled) {
        return L"transport-discovery-exercise";
    }
    if (state.transport_worker_exercise_enabled) {
        return L"transport-worker-exercise";
    }
    if (state.engine_ready_observer_enabled) {
        return L"engine-ready-observer";
    }
    if (state.render_observer_enabled) {
        return L"render-demand-observer";
    }
    return state.endpoint_sink_enabled ? L"endpoint-presence"
                                       : L"presence-only";
}

void RecordAvrcpMediaSnapshot(
    TrialState* state,
    const native_ldac::agent::V1MediaSessionSnapshot& snapshot) {
    if (state == nullptr) return;
    const bool changed = !state->avrcp_pc_playback_valid ||
        state->avrcp_pc_playback_generation != snapshot.acl_generation ||
        state->avrcp_pc_playback != snapshot.playback ||
        state->avrcp_pc_play_enabled != snapshot.play_enabled ||
        state->avrcp_pc_pause_enabled != snapshot.pause_enabled ||
        state->avrcp_pc_next_enabled != snapshot.next_enabled ||
        state->avrcp_pc_previous_enabled != snapshot.previous_enabled;
    if (!changed) return;
    state->avrcp_pc_playback = snapshot.playback;
    state->avrcp_pc_playback_valid = true;
    state->avrcp_pc_playback_generation = snapshot.acl_generation;
    state->avrcp_pc_playback_snapshot_changes++;
    state->avrcp_pc_play_enabled = snapshot.play_enabled;
    state->avrcp_pc_pause_enabled = snapshot.pause_enabled;
    state->avrcp_pc_next_enabled = snapshot.next_enabled;
    state->avrcp_pc_previous_enabled = snapshot.previous_enabled;
    std::wprintf(
        L"V1 daily GSMTC snapshot playback=%ls play=%ls pause=%ls "
        L"next=%ls previous=%ls generation=%llu.\n",
        MediaPlaybackName(snapshot.playback),
        snapshot.play_enabled ? L"yes" : L"no",
        snapshot.pause_enabled ? L"yes" : L"no",
        snapshot.next_enabled ? L"yes" : L"no",
        snapshot.previous_enabled ? L"yes" : L"no",
        static_cast<unsigned long long>(snapshot.acl_generation));
}

bool WriteStateAtomically(const std::wstring& path,
                          const wchar_t* state_name,
                          const TrialState& state) {
    const std::wstring temporary = path + L".tmp";
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
    wchar_t json[12288]{};
    const int length = swprintf_s(
        json,
        L"{\r\n"
        L"  \"schema_version\": 1,\r\n"
        L"  \"mode\": \"%ls\",\r\n"
        L"  \"daily_mode\": %ls,\r\n"
        L"  \"state\": \"%ls\",\r\n"
        L"  \"host_process_id\": %lu,\r\n"
        L"  \"config_schema_version\": 1,\r\n"
        L"  \"config_pipe\": \"%ls\",\r\n"
        L"  \"requested_quality\": \"%ls\",\r\n"
        L"  \"applied_quality\": \"%ls\",\r\n"
        L"  \"requested_config_revision\": %llu,\r\n"
        L"  \"applied_config_revision\": %llu,\r\n"
        L"  \"config_rejected_count\": %u,\r\n"
        L"  \"config_last_error\": %lu,\r\n"
        L"  \"physical_presence\": \"%ls\",\r\n"
        L"  \"render_demand\": \"%ls\",\r\n"
        L"  \"acl_generation\": %llu,\r\n"
        L"  \"connected_events\": %u,\r\n"
        L"  \"disconnected_events\": %u,\r\n"
        L"  \"publish_present_actions\": %u,\r\n"
        L"  \"publish_absent_actions\": %u,\r\n"
        L"  \"fail_mute_actions\": %u,\r\n"
        L"  \"transport_open_actions\": %u,\r\n"
        L"  \"transport_open_executed\": %u,\r\n"
        L"  \"transport_open_render_stability_ms\": %lu,\r\n"
        L"  \"transport_open_stability_waits\": %u,\r\n"
        L"  \"transport_open_stability_resets\": %u,\r\n"
        L"  \"transport_open_stable_authorizations\": %u,\r\n"
        L"  \"transport_open_stability_pending\": %ls,\r\n"
        L"  \"transport_open_attempts_for_generation\": %u,\r\n"
        L"  \"completed_media_sessions_for_generation\": %u,\r\n"
        L"  \"maximum_transport_open_attempts\": %u,\r\n"
        L"  \"pretransport_render_gap_tolerance\": %ls,\r\n"
        L"  \"multiple_media_sessions_enabled\": %ls,\r\n"
        L"  \"transport_retryable_failures\": %u,\r\n"
        L"  \"transport_retries_scheduled\": %u,\r\n"
        L"  \"transport_retry_budget_exhausted\": %u,\r\n"
        L"  \"capabilities_discovered_events\": %u,\r\n"
        L"  \"discovery_sessions_completed\": %u,\r\n"
        L"  \"configuration_sessions_completed\": %u,\r\n"
        L"  \"silence_sessions_completed\": %u,\r\n"
        L"  \"pcm_burst_sessions_completed\": %u,\r\n"
        L"  \"transport_graceful_stop_actions\": %u,\r\n"
        L"  \"transport_cancel_actions\": %u,\r\n"
        L"  \"media_started_events\": %u,\r\n"
        L"  \"media_stopped_events\": %u,\r\n"
        L"  \"media_failed_events\": %u,\r\n"
        L"  \"avrcp_volume_sync_enabled\": %ls,\r\n"
        L"  \"avrcp_handoff_enabled\": %ls,\r\n"
        L"  \"avrcp_handoff_requests\": %u,\r\n"
        L"  \"avrcp_handoff_errors\": %u,\r\n"
        L"  \"avrcp_handoff_restores\": %u,\r\n"
        L"  \"avrcp_handoff_restore_errors\": %u,\r\n"
        L"  \"avrcp_handoff_failed_generation\": %llu,\r\n"
        L"  \"avrcp_handoff_ready_generation\": %llu,\r\n"
        L"  \"avrcp_observer_enabled\": %ls,\r\n"
        L"  \"avrcp_observer_active\": %ls,\r\n"
         L"  \"avrcp_observer_activation_attempts\": %u,\r\n"
         L"  \"avrcp_observer_activation_failures\": %u,\r\n"
         L"  \"avrcp_observer_failed_generation\": %llu,\r\n"
         L"  \"avrcp_observer_poll_failures\": %u,\r\n"
         L"  \"avrcp_pc_playback\": \"%ls\",\r\n"
         L"  \"avrcp_pc_playback_generation\": %llu,\r\n"
         L"  \"avrcp_pc_playback_snapshot_changes\": %u,\r\n"
         L"  \"avrcp_pc_play_enabled\": %ls,\r\n"
         L"  \"avrcp_pc_pause_enabled\": %ls,\r\n"
         L"  \"avrcp_pc_next_enabled\": %ls,\r\n"
         L"  \"avrcp_pc_previous_enabled\": %ls,\r\n"
         L"  \"hfp_capture_monitor_ready\": %ls,\r\n"
        L"  \"hfp_capture_matched\": %ls,\r\n"
        L"  \"hfp_capture_active\": %ls,\r\n"
        L"  \"hfp_transport_switch_enabled\": %ls,\r\n"
        L"  \"hfp_suspended\": %ls,\r\n"
        L"  \"hfp_capture_samples\": %u,\r\n"
        L"  \"hfp_capture_sequence\": %llu,\r\n"
        L"  \"hfp_render_monitor_ready\": %ls,\r\n"
        L"  \"hfp_render_endpoint_present\": %ls,\r\n"
        L"  \"hfp_render_endpoint_matched\": %ls,\r\n"
        L"  \"hfp_render_bridge_ready\": %ls,\r\n"
        L"  \"hfp_render_sequence\": %llu,\r\n"
        L"  \"hfp_render_last_error\": %lu,\r\n"
        L"  \"hfp_shadow_stop_requests\": %u,\r\n"
        L"  \"hfp_shadow_enter_requests\": %u,\r\n"
        L"  \"hfp_shadow_exit_requests\": %u,\r\n"
        L"  \"hfp_shadow_resume_requests\": %u,\r\n"
        L"  \"hfp_lifecycle_suspend_plans\": %u,\r\n"
        L"  \"hfp_lifecycle_resume_plans\": %u,\r\n"
        L"  \"hfp_lifecycle_output_enter_plans\": %u,\r\n"
        L"  \"hfp_lifecycle_output_exit_plans\": %u,\r\n"
        L"  \"hfp_lifecycle_stale_plans\": %u,\r\n"
        L"  \"hfp_lifecycle_invalid_plans\": %u,\r\n"
        L"  \"hfp_lifecycle_executions\": %u,\r\n"
        L"  \"hfp_lifecycle_execution_failures\": %u,\r\n"
        L"  \"transport_stop_acknowledgements\": %u,\r\n"
        L"  \"child_processes_started\": %u,\r\n"
        L"  \"endpoint_sink_enabled\": %ls,\r\n"
        L"  \"endpoint_presence_updates\": %u,\r\n"
        L"  \"endpoint_presence_failures\": %u,\r\n"
        L"  \"endpoint_presence_rebinds\": %u,\r\n"
        L"  \"render_observer_enabled\": %ls,\r\n"
        L"  \"engine_ready_observer_enabled\": %ls,\r\n"
        L"  \"transport_worker_exercise_enabled\": %ls,\r\n"
        L"  \"transport_discovery_exercise_enabled\": %ls,\r\n"
        L"  \"transport_configuration_exercise_enabled\": %ls,\r\n"
        L"  \"transport_silence_exercise_enabled\": %ls,\r\n"
        L"  \"transport_pcm_burst_exercise_enabled\": %ls,\r\n"
        L"  \"playback_disconnect_wait_enabled\": %ls,\r\n"
        L"  \"playback_reconnect_wait_enabled\": %ls,\r\n"
        L"  \"playback_reconnect_target_generations\": %u,\r\n"
        L"  \"playback_disconnect_fail_closed_release\": %ls,\r\n"
        L"  \"render_start_timed_out\": %ls,\r\n"
        L"  \"render_query_count\": %u,\r\n"
        L"  \"render_query_failures\": %u,\r\n"
        L"  \"render_query_last_error\": %lu,\r\n"
        L"  \"render_started_events\": %u,\r\n"
        L"  \"render_stopped_events\": %u,\r\n"
        L"  \"pre_media_render_stop_events\": %u,\r\n"
        L"  \"render_stop_deferred_events\": %u,\r\n"
        L"  \"render_stop_resumed_events\": %u,\r\n"
        L"  \"render_stop_timeout_events\": %u,\r\n"
        L"  \"render_stop_acl_cancelled_events\": %u,\r\n"
        L"  \"render_stop_pending\": %ls,\r\n"
        L"  \"engine_start_requests\": %u,\r\n"
        L"  \"engine_stop_requests\": %u,\r\n"
        L"  \"engine_ready_events\": %u,\r\n"
        L"  \"engine_exit_events\": %u,\r\n"
        L"  \"engine_start_failures\": %u,\r\n"
        L"  \"engine_ready_timeouts\": %u,\r\n"
        L"  \"engine_stop_failures\": %u,\r\n"
        L"  \"engine_graceful_stops\": %u,\r\n"
        L"  \"engine_unexpected_exits\": %u,\r\n"
        L"  \"transport_worker_sequence\": %llu,\r\n"
        L"  \"last_engine_exit_code\": %lu,\r\n"
        L"  \"last_stream_epoch\": %llu\r\n"
        L"}\r\n",
        ModeName(state),
        state.daily_mode ? L"true" : L"false",
        state_name,
        static_cast<unsigned long>(GetCurrentProcessId()),
        state.config_pipe.c_str(),
        native_ldac::agent::V1DailyQualityName(state.requested_quality),
        native_ldac::agent::V1DailyQualityName(state.applied_quality),
        static_cast<unsigned long long>(state.requested_config_revision),
        static_cast<unsigned long long>(state.applied_config_revision),
        state.config_rejected_count,
        static_cast<unsigned long>(state.config_last_error),
        PresenceName(state.lifecycle.physical_presence),
        RenderDemandName(state.lifecycle.render_demand),
        static_cast<unsigned long long>(state.lifecycle.acl_generation),
        state.connected_events,
        state.disconnected_events,
        state.publish_present_actions,
        state.publish_absent_actions,
        state.fail_mute_actions,
        state.transport_open_actions,
        state.transport_open_executed,
        static_cast<unsigned long>(
            state.transport_open_stability.required_ms),
        state.transport_open_stability_waits,
        state.transport_open_stability_resets,
        state.transport_open_stable_authorizations,
        state.transport_open_stability.pending ? L"true" : L"false",
        state.lifecycle.open_attempts_for_generation,
        state.lifecycle.completed_media_sessions_for_generation,
        state.lifecycle.maximum_open_attempts,
        state.lifecycle.tolerate_pretransport_render_gaps
            ? L"true" : L"false",
        state.lifecycle.allow_multiple_media_sessions
            ? L"true" : L"false",
        state.transport_retryable_failures,
        state.transport_retries_scheduled,
        state.transport_retry_budget_exhausted,
        state.capabilities_discovered_events,
        state.discovery_sessions_completed,
        state.configuration_sessions_completed,
        state.silence_sessions_completed,
        state.pcm_burst_sessions_completed,
        state.transport_graceful_stop_actions,
        state.transport_cancel_actions,
        state.media_started_events,
        state.media_stopped_events,
        state.media_failed_events,
        state.avrcp_volume_sync_enabled ? L"true" : L"false",
        state.avrcp_handoff_enabled ? L"true" : L"false",
        state.avrcp_handoff_requests,
        state.avrcp_handoff_errors,
        state.avrcp_handoff_restores,
        state.avrcp_handoff_restore_errors,
        static_cast<unsigned long long>(
            state.avrcp_handoff_failed_generation),
        static_cast<unsigned long long>(
            state.avrcp_handoff_ready_generation),
        state.avrcp_observer_enabled ? L"true" : L"false",
         state.avrcp_observer_active ? L"true" : L"false",
         state.avrcp_observer_activation_attempts,
         state.avrcp_observer_activation_failures,
         static_cast<unsigned long long>(
             state.avrcp_observer_failed_generation),
         state.avrcp_observer_poll_failures,
         MediaPlaybackName(state.avrcp_pc_playback),
         static_cast<unsigned long long>(
             state.avrcp_pc_playback_generation),
         state.avrcp_pc_playback_snapshot_changes,
         state.avrcp_pc_play_enabled ? L"true" : L"false",
         state.avrcp_pc_pause_enabled ? L"true" : L"false",
         state.avrcp_pc_next_enabled ? L"true" : L"false",
         state.avrcp_pc_previous_enabled ? L"true" : L"false",
         state.hfp_capture_monitor_ready ? L"true" : L"false",
        state.hfp_capture_matched ? L"true" : L"false",
        state.hfp_capture_active ? L"true" : L"false",
        state.hfp_transport_switch_enabled ? L"true" : L"false",
        state.lifecycle.hfp_suspended ? L"true" : L"false",
        state.hfp_capture_samples,
        static_cast<unsigned long long>(state.hfp_capture_sequence),
        state.hfp_render_monitor_ready ? L"true" : L"false",
        state.hfp_render_endpoint_present ? L"true" : L"false",
        state.hfp_render_endpoint_matched ? L"true" : L"false",
        state.hfp_render_bridge_ready ? L"true" : L"false",
        static_cast<unsigned long long>(state.hfp_render_sequence),
        static_cast<unsigned long>(state.hfp_render_last_error),
        state.hfp_shadow_stop_requests,
        state.hfp_shadow_enter_requests,
        state.hfp_shadow_exit_requests,
        state.hfp_shadow_resume_requests,
        state.hfp_lifecycle_suspend_plans,
        state.hfp_lifecycle_resume_plans,
        state.hfp_lifecycle_output_enter_plans,
        state.hfp_lifecycle_output_exit_plans,
        state.hfp_lifecycle_stale_plans,
        state.hfp_lifecycle_invalid_plans,
        state.hfp_lifecycle_executions,
        state.hfp_lifecycle_execution_failures,
        state.transport_stop_acknowledgements,
        state.child_processes_started,
        state.endpoint_sink_enabled ? L"true" : L"false",
        state.endpoint_presence_updates,
        state.endpoint_presence_failures,
        state.endpoint_presence_rebinds,
        state.render_observer_enabled ? L"true" : L"false",
        state.engine_ready_observer_enabled ? L"true" : L"false",
        state.transport_worker_exercise_enabled ? L"true" : L"false",
        state.transport_discovery_exercise_enabled ? L"true" : L"false",
        state.transport_configuration_exercise_enabled ? L"true" : L"false",
        state.transport_silence_exercise_enabled ? L"true" : L"false",
        state.transport_pcm_burst_exercise_enabled ? L"true" : L"false",
        state.playback_disconnect_wait_enabled ? L"true" : L"false",
        state.playback_reconnect_wait_enabled ? L"true" : L"false",
        state.playback_reconnect_target_generations,
        state.playback_disconnect_fail_closed_release ? L"true" : L"false",
        state.render_start_timed_out ? L"true" : L"false",
        state.render_query_count,
        state.render_query_failures,
        state.render_query_last_error,
        state.render_started_events,
        state.render_stopped_events,
        state.pre_media_render_stop_events,
        state.render_stop_deferred_events,
        state.render_stop_resumed_events,
        state.render_stop_timeout_events,
        state.render_stop_acl_cancelled_events,
        state.render_stop_pending ? L"true" : L"false",
        state.engine_start_requests,
        state.engine_stop_requests,
        state.engine_ready_events,
        state.engine_exit_events,
        state.engine_start_failures,
        state.engine_ready_timeouts,
        state.engine_stop_failures,
        state.engine_graceful_stops,
        state.engine_unexpected_exits,
        static_cast<unsigned long long>(
            state.transport_worker_sequence),
        state.last_engine_exit_code,
        static_cast<unsigned long long>(state.last_stream_epoch));
    if (length <= 0) {
        CloseHandle(file);
        (void)DeleteFileW(temporary.c_str());
        return false;
    }
    const int bytes_required = WideCharToMultiByte(CP_UTF8,
                                                    0,
                                                    json,
                                                    length,
                                                    nullptr,
                                                    0,
                                                    nullptr,
                                                    nullptr);
    if (bytes_required <= 0) {
        CloseHandle(file);
        (void)DeleteFileW(temporary.c_str());
        return false;
    }
    std::string utf8(static_cast<size_t>(bytes_required), '\0');
    if (WideCharToMultiByte(CP_UTF8,
                            0,
                            json,
                            length,
                            utf8.data(),
                            bytes_required,
                            nullptr,
                            nullptr) != bytes_required) {
        CloseHandle(file);
        (void)DeleteFileW(temporary.c_str());
        return false;
    }
    DWORD written = 0u;
    const bool wrote = WriteFile(file,
                                 utf8.data(),
                                 static_cast<DWORD>(utf8.size()),
                                 &written,
                                 nullptr) != FALSE &&
                       written == static_cast<DWORD>(utf8.size()) &&
                       FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (!wrote ||
        !MoveFileExW(temporary.c_str(),
                     path.c_str(),
                     MOVEFILE_REPLACE_EXISTING |
                         MOVEFILE_WRITE_THROUGH)) {
        (void)DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

void CountActions(std::uint32_t actions, TrialState* state) {
    using native_ldac::agent::HasV1LifecycleAction;
    using native_ldac::agent::V1ActionFailMute;
    using native_ldac::agent::V1ActionCancelTransport;
    using native_ldac::agent::V1ActionGracefulStopTransport;
    using native_ldac::agent::V1ActionOpenTransport;
    using native_ldac::agent::V1ActionPublishEndpointAbsent;
    using native_ldac::agent::V1ActionPublishEndpointPresent;
    using native_ldac::agent::V1ActionScheduleTransportRetry;
    using native_ldac::agent::V1ActionStartEngine;
    using native_ldac::agent::V1ActionStopEngine;
    if (HasV1LifecycleAction(actions, V1ActionPublishEndpointPresent)) {
        ++state->publish_present_actions;
    }
    if (HasV1LifecycleAction(actions, V1ActionPublishEndpointAbsent)) {
        ++state->publish_absent_actions;
    }
    if (HasV1LifecycleAction(actions, V1ActionFailMute)) {
        ++state->fail_mute_actions;
    }
    if (HasV1LifecycleAction(actions, V1ActionOpenTransport)) {
        ++state->transport_open_actions;
    }
    if (HasV1LifecycleAction(actions, V1ActionGracefulStopTransport)) {
        ++state->transport_graceful_stop_actions;
    }
    if (HasV1LifecycleAction(actions, V1ActionCancelTransport)) {
        ++state->transport_cancel_actions;
    }
    if (HasV1LifecycleAction(actions, V1ActionStartEngine)) {
        ++state->engine_start_requests;
    }
    if (HasV1LifecycleAction(actions, V1ActionStopEngine)) {
        ++state->engine_stop_requests;
    }
    if (HasV1LifecycleAction(actions, V1ActionScheduleTransportRetry)) {
        ++state->transport_retries_scheduled;
    }
}

bool ArchiveTransportAttemptResultAt(const Options& options,
                                     const TrialState& state,
                                     std::uint32_t attempt,
                                     DWORD* error) {
    if (!state.transport_discovery_exercise_enabled ||
        options.transport_result_path.empty() ||
        attempt == 0u) {
        if (error != nullptr) {
            *error = ERROR_SUCCESS;
        }
        return true;
    }
    if (state.daily_mode) {
        if (state.transport_worker_sequence == 0u) {
            if (error != nullptr) {
                *error = ERROR_INVALID_STATE;
            }
            return false;
        }
        const std::wstring daily_archive =
            options.transport_result_path + L".generation-" +
            std::to_wstring(state.lifecycle.acl_generation) +
            L".worker-" +
            std::to_wstring(state.transport_worker_sequence) +
            L".attempt-" + std::to_wstring(attempt) + L".json";
        if (!CopyFileW(options.transport_result_path.c_str(),
                       daily_archive.c_str(),
                       FALSE)) {
            if (error != nullptr) {
                *error = GetLastError();
            }
            return false;
        }
        if (error != nullptr) {
            *error = ERROR_SUCCESS;
        }
        return true;
    }
    const std::wstring archive = options.transport_result_path +
        L".attempt-" +
        std::to_wstring(attempt) +
        L".json";
    if (!CopyFileW(options.transport_result_path.c_str(),
                   archive.c_str(),
                   FALSE)) {
        if (error != nullptr) {
            *error = GetLastError();
        }
        return false;
    }
    const std::wstring generation_archive =
        options.transport_result_path + L".generation-" +
        std::to_wstring(state.lifecycle.acl_generation) +
        L".attempt-" + std::to_wstring(attempt) + L".json";
    if (!CopyFileW(options.transport_result_path.c_str(),
                   generation_archive.c_str(),
                   FALSE)) {
        if (error != nullptr) {
            *error = GetLastError();
        }
        return false;
    }
    if (error != nullptr) {
        *error = ERROR_SUCCESS;
    }
    return true;
}

bool ArchiveGenerationState(const Options& options,
                            const TrialState& state,
                            DWORD* error) {
    if (!state.playback_reconnect_wait_enabled ||
        state.lifecycle.acl_generation == 0u) {
        if (error != nullptr) {
            *error = ERROR_SUCCESS;
        }
        return true;
    }
    const std::wstring archive = options.state_path + L".generation-" +
        std::to_wstring(state.lifecycle.acl_generation) + L".json";
    if (!CopyFileW(options.state_path.c_str(), archive.c_str(), FALSE)) {
        if (error != nullptr) {
            *error = GetLastError();
        }
        return false;
    }
    if (error != nullptr) {
        *error = ERROR_SUCCESS;
    }
    return true;
}

bool ArchiveTransportAttemptResult(const Options& options,
                                   const TrialState& state,
                                   DWORD* error) {
    return ArchiveTransportAttemptResultAt(
        options,
        state,
        state.lifecycle.open_attempts_for_generation,
        error);
}

bool ApplyEndpointPresence(
    std::uint32_t actions,
    native_ldac::agent::V1EndpointPresenceSink* sink,
    TrialState* state,
    DWORD* error) {
    using native_ldac::agent::HasV1LifecycleAction;
    using native_ldac::agent::V1ActionPublishEndpointAbsent;
    using native_ldac::agent::V1ActionPublishEndpointPresent;
    if (!state->endpoint_sink_enabled) {
        return true;
    }
    const bool publish_present = HasV1LifecycleAction(
        actions, V1ActionPublishEndpointPresent);
    const bool publish_absent = HasV1LifecycleAction(
        actions, V1ActionPublishEndpointAbsent);
    if (!publish_present && !publish_absent) {
        return true;
    }
    if (!sink->Set(publish_present,
                   state->lifecycle.acl_generation,
                   error)) {
        ++state->endpoint_presence_failures;
        return false;
    }
    ++state->endpoint_presence_updates;
    return true;
}

bool ReconcileHfpShadow(
    native_ldac::agent::V1HfpCaptureMonitor* monitor,
    native_ldac::agent::V1HfpShadowState* shadow_state,
    TrialState* state,
    ULONGLONG now,
    bool transport_switch_enabled,
    native_ldac::agent::V1HfpLifecyclePlan* lifecycle_plan) {
    if (monitor == nullptr || shadow_state == nullptr || state == nullptr ||
        !state->daily_mode) {
        if (lifecycle_plan != nullptr) *lifecycle_plan = {};
        return false;
    }
    const auto capture = monitor->Snapshot();
    native_ldac::agent::V1HfpShadowInput input;
    input.capture = capture;
    input.acl_generation = state->lifecycle.acl_generation;
    input.now_ms = now;
    input.monitor_ready = monitor->ready();
    input.physical_connected =
        state->lifecycle.physical_presence ==
        native_ldac::agent::V1PhysicalPresence::Present;
    input.ldac_path_active =
        state->lifecycle.engine_lease !=
            native_ldac::agent::V1EngineLease::Absent ||
        state->lifecycle.media_session ==
            native_ldac::agent::V1MediaSession::Opening ||
        state->lifecycle.media_session ==
            native_ldac::agent::V1MediaSession::Streaming ||
        state->lifecycle.media_session ==
            native_ldac::agent::V1MediaSession::Stopping;
    input.render_demand =
        state->lifecycle.render_demand ==
        native_ldac::agent::V1RenderDemand::Running;
    const auto shadow = shadow_state->Step(input);
    state->hfp_capture_sequence = shadow.capture_sequence;
    state->hfp_capture_monitor_ready = shadow.monitor_ready;
    state->hfp_capture_matched = shadow.endpoint_matched;
    state->hfp_capture_active = shadow.capture_active;
    ++state->hfp_capture_samples;
    const auto& decision = shadow.switch_decision;
    using native_ldac::agent::HasV1HfpSwitchAction;
    if (HasV1HfpSwitchAction(
            decision, native_ldac::agent::V1HfpActionStopLdac)) {
        ++state->hfp_shadow_stop_requests;
    }
    if (HasV1HfpSwitchAction(
            decision, native_ldac::agent::V1HfpActionEnterHfpOutput)) {
        ++state->hfp_shadow_enter_requests;
    }
    if (HasV1HfpSwitchAction(
            decision, native_ldac::agent::V1HfpActionExitHfpOutput)) {
        ++state->hfp_shadow_exit_requests;
    }
    if (HasV1HfpSwitchAction(
            decision, native_ldac::agent::V1HfpActionResumeLdac)) {
        ++state->hfp_shadow_resume_requests;
    }
    const auto plan = native_ldac::agent::PlanV1HfpLifecycle(
        decision,
        state->lifecycle.acl_generation,
        transport_switch_enabled);
    if (lifecycle_plan != nullptr) *lifecycle_plan = plan;
    if (plan.enter_hfp_output_requested) {
        ++state->hfp_lifecycle_output_enter_plans;
    }
    if (plan.exit_hfp_output_requested) {
        ++state->hfp_lifecycle_output_exit_plans;
    }
    if (plan.stale) {
        ++state->hfp_lifecycle_stale_plans;
    }
    if (plan.invalid) {
        ++state->hfp_lifecycle_invalid_plans;
    }
    if (plan.proposed_command == native_ldac::agent::V1HfpLifecycleCommand::
            SuspendLdac) {
        ++state->hfp_lifecycle_suspend_plans;
    } else if (plan.proposed_command ==
                   native_ldac::agent::V1HfpLifecycleCommand::
                   ResumeLdac) {
        ++state->hfp_lifecycle_resume_plans;
    }
    return shadow.snapshot_changed || decision.actions !=
        native_ldac::agent::V1HfpActionNone;
}

bool ReconcileHfpRenderEndpoint(
    native_ldac::agent::V1HfpRenderEndpointMonitor* monitor,
    TrialState* state) {
    if (monitor == nullptr || state == nullptr || !state->daily_mode) {
        return false;
    }
    const auto snapshot = monitor->Snapshot();
    const bool monitor_ready = monitor->ready();
    const DWORD last_error = monitor->last_error();
    const bool changed =
        state->hfp_render_monitor_ready != monitor_ready ||
        state->hfp_render_endpoint_present != snapshot.endpoint_present ||
        state->hfp_render_endpoint_matched != snapshot.endpoint_matched ||
        state->hfp_render_bridge_ready != snapshot.output_bridge_ready ||
        state->hfp_render_sequence != snapshot.sequence ||
        state->hfp_render_last_error != last_error;
    state->hfp_render_monitor_ready = monitor_ready;
    state->hfp_render_endpoint_present = snapshot.endpoint_present;
    state->hfp_render_endpoint_matched = snapshot.endpoint_matched;
    state->hfp_render_bridge_ready = snapshot.output_bridge_ready;
    state->hfp_render_sequence = snapshot.sequence;
    state->hfp_render_last_error = last_error;
    return changed;
}

bool RequestAvrcpHandoff(
    TrialState* state,
    native_ldac::agent::V1AvrcpHandoffIpc* handoff_ipc,
    DWORD* error) {
    if (state == nullptr || handoff_ipc == nullptr ||
        !handoff_ipc->enabled()) {
        if (error != nullptr) *error = ERROR_SUCCESS;
        return true;
    }
    const std::uint64_t generation = state->lifecycle.acl_generation;
    if (generation == 0u ||
        state->avrcp_handoff_ready_generation == generation) {
        if (error != nullptr) *error = ERROR_SUCCESS;
        return true;
    }

    ++state->avrcp_handoff_requests;
    DWORD handoff_error = ERROR_SUCCESS;
    if (!handoff_ipc->RequestHandoff(generation, 0u, &handoff_error) ||
        handoff_ipc->WaitHandoffDone(
            30000u, &handoff_error) !=
            native_ldac::agent::V1AvrcpHandoffWaitResult::Ok) {
        ++state->avrcp_handoff_errors;
        state->avrcp_observer_active = false;
        state->avrcp_handoff_failed_generation = generation;
        state->avrcp_handoff_ready_generation = 0u;
        ++state->avrcp_handoff_restores;
        DWORD restore_error = ERROR_SUCCESS;
        const bool restored = handoff_ipc->RequestRestore(
                generation, 0u, &restore_error) &&
            handoff_ipc->WaitRestoreDone(
                30000u, &restore_error) ==
                native_ldac::agent::V1AvrcpHandoffWaitResult::Ok;
        if (!restored) ++state->avrcp_handoff_restore_errors;
        std::wprintf(
            L"V1 daily AVRCP handoff request failed (Win32 %lu); "
            L"Microsoft restore %ls and this ACL generation is fail-closed.\n",
            handoff_error,
            restored ? L"completed" : L"failed");
        std::fflush(stdout);
        if (error != nullptr) *error = ERROR_SUCCESS;
        return true;
    }

    state->avrcp_handoff_ready_generation = generation;
    std::wprintf(
        L"V1 daily AVRCP handoff completed after MediaStarted; the "
        L"observer lease can activate now.\n");
    std::fflush(stdout);
    if (error != nullptr) *error = ERROR_SUCCESS;
    return true;
}

bool RebindEndpointPresenceLease(
    native_ldac::agent::V1EndpointPresenceSink* endpoint_sink,
    TrialState* state,
    DWORD* error) {
    if (endpoint_sink == nullptr || state == nullptr ||
        state->lifecycle.acl_generation == 0u) {
        if (error != nullptr) *error = ERROR_INVALID_PARAMETER;
        return false;
    }

    // The handoff host restarts the exact AVRCP PDO. Do not retain a KS
    // handle opened before that PnP transition: it can block the next PCM
    // info query while Windows is re-enumerating the Bluetooth topology.
    // Reopen the current audio interface and renew the independent
    // 15-second physical-presence lease before PCM starts.
    endpoint_sink->Close();
    constexpr unsigned int kMaximumRebindAttempts = 30u;
    DWORD last_error = ERROR_DEVICE_NOT_CONNECTED;
    for (unsigned int attempt = 0u;
         attempt < kMaximumRebindAttempts;
         ++attempt) {
        if (endpoint_sink->Open(&last_error) &&
            endpoint_sink->Set(
                true, state->lifecycle.acl_generation, &last_error)) {
            ++state->endpoint_presence_updates;
            ++state->endpoint_presence_rebinds;
            if (error != nullptr) *error = ERROR_SUCCESS;
            std::wprintf(
                L"V1 Native LDAC audio endpoint rebound; the "
                L"physical-presence lease was renewed.\n");
            std::fflush(stdout);
            return true;
        }
        endpoint_sink->Close();
        if (attempt + 1u < kMaximumRebindAttempts) Sleep(100u);
    }
    ++state->endpoint_presence_failures;
    if (error != nullptr) *error = last_error;
    return false;
}

bool RequestAvrcpHandoffWithEndpointQuiesce(
    TrialState* state,
    native_ldac::agent::V1AvrcpHandoffIpc* handoff_ipc,
    native_ldac::agent::V1EndpointPresenceSink* endpoint_sink,
    DWORD* error) {
    if (endpoint_sink == nullptr) {
        return RequestAvrcpHandoff(state, handoff_ipc, error);
    }
    endpoint_sink->Close();
    const bool handoff_ok =
        RequestAvrcpHandoff(state, handoff_ipc, error);
    DWORD rebind_error = ERROR_SUCCESS;
    const bool rebound = RebindEndpointPresenceLease(
        endpoint_sink, state, &rebind_error);
    if (!rebound) {
        if (error != nullptr) *error = rebind_error;
        return false;
    }
    if (error != nullptr) *error = ERROR_SUCCESS;
    return handoff_ok;
}

bool ReconcileAvrcpObserver(
    native_ldac::agent::V1AvrcpObserverHost* observer,
    native_ldac::agent::V1AvrcpActionSink* sink,
    native_ldac::agent::V1EngineReadyHost* engine,
    native_ldac::agent::V1EndpointPresenceSink* endpoint_sink,
    TrialState* state,
    ULONGLONG now,
    ULONGLONG* next_attempt_tick,
    native_ldac::agent::V1MediaSessionMonitor* media_monitor,
    native_ldac::agent::V1AvrcpHandoffIpc* handoff_ipc,
    DWORD* error) {
    if (observer == nullptr || state == nullptr || next_attempt_tick == nullptr) {
        if (error != nullptr) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return false;
    }
    if (!state->avrcp_observer_enabled) {
        if (error != nullptr) {
            *error = ERROR_SUCCESS;
        }
        return true;
    }
    const bool media_streaming =
        state->lifecycle.media_session ==
        native_ldac::agent::V1MediaSession::Streaming;
    native_ldac::agent::V1MediaSessionSnapshot media_snapshot{};
    if (media_monitor != nullptr && media_monitor->ready()) {
        media_snapshot =
            media_monitor->Snapshot(state->lifecycle.acl_generation);
    } else {
        media_snapshot.acl_generation = state->lifecycle.acl_generation;
        if (media_streaming) {
            // GSMTC initialization is asynchronous. The LDAC lifecycle is
            // already a concrete media session, so keep controls usable until
            // the read-only Windows session monitor publishes its first
            // capability snapshot.
            media_snapshot.playback =
                native_ldac::agent::V1MediaSessionPlayback::Playing;
            media_snapshot.pause_enabled = true;
            media_snapshot.next_enabled = true;
            media_snapshot.previous_enabled = true;
        }
    }
    const auto media_eligibility =
        native_ldac::agent::EvaluateV1MediaSessionEligibility(media_snapshot);
    RecordAvrcpMediaSnapshot(state, media_snapshot);
    const bool keep_media_controls = !state->lifecycle.hfp_suspended &&
        observer->media_session_active() &&
        media_eligibility.session_present;
    if (state->lifecycle.hfp_suspended ||
        (!media_streaming && !keep_media_controls)) {
        if (engine != nullptr && engine->active() &&
            engine->single_gain_fail_safe_enabled()) {
            DWORD gain_error = ERROR_SUCCESS;
            if (!engine->SetSingleGainReady(false, &gain_error)) {
                if (error != nullptr) *error = gain_error;
                return false;
            }
        }
        if (observer->media_session_active()) {
            observer->EndMediaSession();
            state->avrcp_observer_active = false;
            state->avrcp_control_ready_deadline = 0u;
            std::wprintf(
                L"V1 daily AVRCP observer lease ended with the media session.\n");
            if (handoff_ipc != nullptr && handoff_ipc->enabled()) {
                ++state->avrcp_handoff_restores;
                DWORD handoff_error = ERROR_SUCCESS;
                if (!handoff_ipc->RequestRestore(
                        state->lifecycle.acl_generation,
                        0u,
                        &handoff_error) ||
                    handoff_ipc->WaitRestoreDone(
                        30000u,
                        &handoff_error) !=
                        native_ldac::agent::V1AvrcpHandoffWaitResult::Ok) {
                    ++state->avrcp_handoff_restore_errors;
                    std::wprintf(
                        L"V1 daily AVRCP restore request failed (Win32 %lu); "
                        L"Microsoft restoration is left to the handoff "
                        L"host or rollback tooling.\n",
                        handoff_error);
                } else {
                    std::wprintf(
                        L"V1 daily AVRCP restore completed; Microsoft AVRCP "
                        L"is the owner again.\n");
                }
                observer->ReleaseTransport();
                state->avrcp_handoff_ready_generation = 0u;
                std::fflush(stdout);
            }
            std::fflush(stdout);
        }
        if (!observer->media_session_active() &&
            handoff_ipc != nullptr && handoff_ipc->enabled() &&
            state->avrcp_handoff_ready_generation ==
                state->lifecycle.acl_generation) {
            ++state->avrcp_handoff_restores;
            DWORD restore_error = ERROR_SUCCESS;
            const bool restored = handoff_ipc->RequestRestore(
                    state->lifecycle.acl_generation,
                    0u,
                    &restore_error) &&
                handoff_ipc->WaitRestoreDone(
                    30000u, &restore_error) ==
                    native_ldac::agent::V1AvrcpHandoffWaitResult::Ok;
            if (!restored) {
                ++state->avrcp_handoff_restore_errors;
            } else {
                std::wprintf(
                    L"V1 daily AVRCP pre-media handoff restored Microsoft "
                    L"before the media session started.\n");
            }
            observer->ReleaseTransport();
            state->avrcp_handoff_ready_generation = 0u;
        }
        if (error != nullptr) {
            *error = ERROR_SUCCESS;
        }
        return true;
    }
    if (observer->media_session_active()) {
        observer->SetMediaSessionSnapshot(media_snapshot);
    }
    // Native owner handoff is not safe while this process is the real PCM
    // render owner: it can tear down the audio endpoint and the subsequent
    // Native AVCTP OPEN is rejected by the XM5. Keep the Microsoft owner and
    // the normal PC endpoint gain until a supported shared-owner path exists.
    if (state->avrcp_volume_sync_enabled &&
        state->avrcp_handoff_enabled &&
        !observer->media_session_active() &&
        state->avrcp_handoff_failed_generation !=
            state->lifecycle.acl_generation) {
        state->avrcp_handoff_failed_generation =
            state->lifecycle.acl_generation;
        std::wprintf(
            L"V1 daily volume sync is fail-safe disabled for the "
            L"Native audio path: Microsoft AVRCP owner handoff is "
            L"not supported during real PCM playback; PC endpoint gain "
            L"remains active.\n");
        std::fflush(stdout);
        if (error != nullptr) *error = ERROR_NOT_SUPPORTED;
        return true;
    }
    if (state->avrcp_handoff_enabled &&
        state->avrcp_handoff_failed_generation ==
            state->lifecycle.acl_generation) {
        if (error != nullptr) {
            *error = ERROR_SUCCESS;
        }
        return true;
    }
    if (!observer->media_session_active() &&
        state->lifecycle.acl_generation != 0u &&
        state->avrcp_observer_failed_generation ==
            state->lifecycle.acl_generation) {
        if (error != nullptr) {
            *error = ERROR_SUCCESS;
        }
        return true;
    }
    if (!observer->media_session_active() &&
        !state->lifecycle.hfp_suspended) {
        if (*next_attempt_tick != 0u && now < *next_attempt_tick) {
            if (error != nullptr) {
                *error = ERROR_SUCCESS;
            }
            return true;
        }
        if (handoff_ipc != nullptr && handoff_ipc->enabled() &&
            state->avrcp_handoff_ready_generation !=
                state->lifecycle.acl_generation) {
            if (!RequestAvrcpHandoffWithEndpointQuiesce(
                    state, handoff_ipc, endpoint_sink, error)) {
                return false;
            }
        }
        ++state->avrcp_observer_activation_attempts;
        native_ldac::agent::V1AvrcpReplayOptions options;
        options.acl_generation = state->lifecycle.acl_generation;
        // B2 write mode is opt-in via --volume-sync (default off). The
        // mapper stays ObserveOnly unless the operator enabled the sync;
        // authorized Windows/AVRCP writes then flow through the shared sink.
        options.volume_sync = state->avrcp_volume_sync_enabled;
        options.media_routing = state->avrcp_volume_sync_enabled;
        options.headset_preferred = true;
        options.media_session = media_snapshot;
        DWORD observer_error = ERROR_SUCCESS;
        const auto result = observer->BeginMediaSession(
            options, sink, &observer_error);
        if (result != native_ldac::agent::V1AvrcpObserverActivationResult::
                          Active) {
            ++state->avrcp_observer_activation_failures;
            state->avrcp_observer_active = false;
            state->avrcp_control_ready_deadline = 0u;
            observer->EndMediaSession();
            const bool retry_activation =
                result == native_ldac::agent::
                              V1AvrcpObserverActivationResult::Pending;
            if (!retry_activation) {
                state->avrcp_observer_failed_generation =
                    state->lifecycle.acl_generation;
            }
            if (handoff_ipc != nullptr && handoff_ipc->enabled()) {
                state->avrcp_handoff_failed_generation =
                    state->lifecycle.acl_generation;
                ++state->avrcp_handoff_restores;
                DWORD restore_error = ERROR_SUCCESS;
                const bool restored = handoff_ipc->RequestRestore(
                        state->lifecycle.acl_generation,
                        0u,
                        &restore_error) &&
                    handoff_ipc->WaitRestoreDone(
                        30000u,
                        &restore_error) ==
                        native_ldac::agent::V1AvrcpHandoffWaitResult::Ok;
                if (!restored) {
                    ++state->avrcp_handoff_restore_errors;
                }
                std::wprintf(
                    L"V1 daily AVRCP activation rollback %ls (Win32 %lu); "
                    L"this ACL generation is fail-closed.\n",
                    restored ? L"completed" : L"failed",
                    restore_error);
                *next_attempt_tick = 0u;
                observer->ReleaseTransport();
                state->avrcp_handoff_ready_generation = 0u;
            } else {
                *next_attempt_tick = retry_activation ? now + 1000u : 0u;
            }
            const wchar_t* reason =
                result == native_ldac::agent::V1AvrcpObserverActivationResult::
                              Pending
                    ? L"activation not ready"
                    : result == native_ldac::agent::
                                    V1AvrcpObserverActivationResult::
                                        Unavailable
                    ? L"interface unavailable"
                    : result == native_ldac::agent::
                                    V1AvrcpObserverActivationResult::
                                        Incompatible
                    ? L"ABI incompatible"
                    : L"activation failed";
            std::wprintf(
                L"V1 daily AVRCP observer %ls (Win32 %lu); media continues "
                L"without a duplicate activation attempt in this PDO "
                L"generation.\n",
                reason,
                observer_error);
            std::fflush(stdout);
            if (error != nullptr) {
                *error = ERROR_SUCCESS;
            }
            return true;
        }
        state->avrcp_observer_active = false;
        state->avrcp_control_ready_deadline = now + 10000u;
        *next_attempt_tick = 0u;
        std::wprintf(
            L"V1 daily AVRCP activation requested after MediaStarted; one "
            L"post-media OPEN is in progress and endpoint gain remains "
            L"fail-safe until the control channel is ready (%ls).\n",
            state->avrcp_volume_sync_enabled
                ? L"in write mode"
                : L"ObserveOnly");
        std::fflush(stdout);
    }
    DWORD poll_error = ERROR_SUCCESS;
    if (!observer->Poll(&poll_error)) {
        ++state->avrcp_observer_poll_failures;
        state->avrcp_observer_active = false;
        state->avrcp_control_ready_deadline = 0u;
        if (engine != nullptr && engine->active() &&
            engine->single_gain_fail_safe_enabled()) {
            DWORD gain_error = ERROR_SUCCESS;
            if (!engine->SetSingleGainReady(false, &gain_error)) {
                if (error != nullptr) *error = gain_error;
                return false;
            }
        }
        std::wprintf(
            L"V1 daily AVRCP observer poll failed (Win32 %lu); media "
            L"continues and no duplicate activation is issued.\n",
            poll_error);
        std::fflush(stdout);
    } else {
        const bool control_ready = observer->single_gain_ready();
        if (!control_ready && state->avrcp_control_ready_deadline != 0u &&
            now >= state->avrcp_control_ready_deadline) {
            const ULONG flags = observer->status_flags();
            const LONG protocol_status = observer->last_protocol_status();
            const LONG open_status = observer->last_open_status();
            state->avrcp_control_ready_deadline = 0u;
            state->avrcp_observer_active = false;
            state->avrcp_observer_failed_generation =
                state->lifecycle.acl_generation;
            observer->EndMediaSession();
            if (engine != nullptr && engine->active() &&
                engine->single_gain_fail_safe_enabled()) {
                DWORD gain_error = ERROR_SUCCESS;
                if (!engine->SetSingleGainReady(false, &gain_error)) {
                    if (error != nullptr) *error = gain_error;
                    return false;
                }
            }
            if (handoff_ipc != nullptr && handoff_ipc->enabled()) {
                ++state->avrcp_handoff_restores;
                DWORD restore_error = ERROR_SUCCESS;
                const bool restored = handoff_ipc->RequestRestore(
                        state->lifecycle.acl_generation,
                        0u,
                        &restore_error) &&
                    handoff_ipc->WaitRestoreDone(
                        30000u,
                        &restore_error) ==
                        native_ldac::agent::V1AvrcpHandoffWaitResult::Ok;
                if (!restored) ++state->avrcp_handoff_restore_errors;
                observer->ReleaseTransport();
                state->avrcp_handoff_ready_generation = 0u;
                state->avrcp_handoff_failed_generation =
                    state->lifecycle.acl_generation;
                std::wprintf(
                    L"V1 daily AVRCP control readiness timed out "
                    L"(flags=0x%08lX protocol=0x%08lX open=0x%08lX); "
                    L"endpoint gain stayed active and Microsoft restore "
                    L"%ls.\n",
                    static_cast<unsigned long>(flags),
                    static_cast<unsigned long>(protocol_status),
                    static_cast<unsigned long>(open_status),
                    restored ? L"completed" : L"failed");
                std::fflush(stdout);
            }
            if (error != nullptr) *error = ERROR_SUCCESS;
            return true;
        }
        if (control_ready) {
            state->avrcp_control_ready_deadline = 0u;
        }
        if (engine != nullptr && engine->active() &&
            engine->single_gain_fail_safe_enabled()) {
            DWORD gain_error = ERROR_SUCCESS;
            if (!engine->SetSingleGainReady(control_ready, &gain_error)) {
                if (error != nullptr) *error = gain_error;
                return false;
            }
        }
        if (control_ready != state->avrcp_observer_active) {
            state->avrcp_observer_active = control_ready;
            std::wprintf(
                control_ready
                    ? L"V1 daily AVRCP control channel ready "
                      L"(flags=0x%08lX); XM5 initial volume adopted and "
                      L"single-gain mode enabled.\n"
                    : L"V1 daily AVRCP control readiness lost "
                      L"(flags=0x%08lX); endpoint gain restored.\n",
                static_cast<unsigned long>(observer->status_flags()));
            std::fflush(stdout);
        }
    }
    if (error != nullptr) {
        *error = ERROR_SUCCESS;
    }
    return true;
}

bool ReconcileAvrcpFilterHost(
    native_ldac::agent::V1AvrcpFilterHost* filter_host,
    native_ldac::agent::V1AvrcpWindowsSink* sink,
    native_ldac::agent::V1EngineReadyHost* engine,
    TrialState* state,
    native_ldac::agent::V1MediaSessionMonitor* media_monitor,
    DWORD* error) {
    if (filter_host == nullptr || sink == nullptr || state == nullptr) {
        if (error != nullptr) *error = ERROR_INVALID_PARAMETER;
        return false;
    }
    const bool streaming = state->lifecycle.media_session ==
        native_ldac::agent::V1MediaSession::Streaming;
    const bool physically_connected =
        state->lifecycle.physical_presence ==
        native_ldac::agent::V1PhysicalPresence::Present;
    native_ldac::agent::V1MediaSessionSnapshot media{};
    if (media_monitor != nullptr && media_monitor->ready()) {
        media = media_monitor->Snapshot(state->lifecycle.acl_generation);
    } else {
        media.acl_generation = state->lifecycle.acl_generation;
        if (streaming) {
            media.playback = native_ldac::agent::
                V1MediaSessionPlayback::Playing;
            media.pause_enabled = true;
            media.next_enabled = true;
            media.previous_enabled = true;
        }
    }
    RecordAvrcpMediaSnapshot(state, media);
    if (!state->avrcp_volume_sync_enabled || !physically_connected ||
        state->lifecycle.hfp_suspended) {
        filter_host->EndSession();
        if (engine != nullptr && engine->active() &&
            engine->single_gain_fail_safe_enabled()) {
            DWORD gain_error = ERROR_SUCCESS;
            if (!engine->SetSingleGainReady(false, &gain_error)) {
                if (error != nullptr) *error = gain_error;
                return false;
            }
        }
        state->avrcp_observer_active = false;
        if (error != nullptr) *error = ERROR_SUCCESS;
        return true;
    }
    if (!filter_host->session_active()) {
        if (!filter_host->BeginSession(
                state->lifecycle.acl_generation, media, sink, error)) {
            state->avrcp_observer_active = false;
            return true;
        }
        std::wprintf(
            L"V1 daily Microsoft-preserving AVRCP volume bridge started; "
            L"no driver handoff is performed.\n");
        std::fflush(stdout);
    } else {
        filter_host->SetMediaSessionSnapshot(media);
    }
    DWORD poll_error = ERROR_SUCCESS;
    if (!filter_host->Poll(&poll_error)) {
        filter_host->EndSession();
        state->avrcp_observer_active = false;
        if (error != nullptr) *error = poll_error;
        return true;
    }
    const bool endpoint_ready = sink->WindowsVolumeEndpointReady();
    const bool ready = filter_host->initial_volume_seen() && endpoint_ready;
    if (engine != nullptr && engine->active() &&
        engine->single_gain_fail_safe_enabled()) {
        DWORD gain_error = ERROR_SUCCESS;
        if (!engine->SetSingleGainReady(ready, &gain_error)) {
            if (error != nullptr) *error = gain_error;
            return false;
        }
    }
    if (ready != state->avrcp_observer_active) {
        state->avrcp_observer_active = ready;
        std::wprintf(
            ready
                ? L"V1 daily Microsoft-preserving AVRCP volume bridge ready; "
                  L"exact Native LDAC endpoint bound and single-gain mode "
                  L"enabled.\n"
                : L"V1 daily Microsoft-preserving AVRCP volume bridge lost "
                  L"readiness; exact Native LDAC endpoint binding is "
                  L"unavailable and endpoint gain was restored.\n");
        std::fflush(stdout);
    }
    if (error != nullptr) *error = ERROR_SUCCESS;
    return true;
}

bool ReconcileAvrcpControl(
    native_ldac::agent::V1AvrcpFilterHost* filter_host,
    native_ldac::agent::V1AvrcpObserverHost* observer,
    native_ldac::agent::V1AvrcpWindowsSink* sink,
    native_ldac::agent::V1EngineReadyHost* engine,
    native_ldac::agent::V1EndpointPresenceSink* endpoint_sink,
    TrialState* state,
    ULONGLONG now,
    ULONGLONG* next_attempt_tick,
    native_ldac::agent::V1MediaSessionMonitor* media_monitor,
    native_ldac::agent::V1AvrcpHandoffIpc* handoff_ipc,
    DWORD* error) {
    if (state != nullptr && state->avrcp_volume_sync_enabled) {
        return ReconcileAvrcpFilterHost(
            filter_host, sink, engine, state, media_monitor, error);
    }
    return ReconcileAvrcpObserver(
        observer, sink, engine, endpoint_sink, state, now,
        next_attempt_tick, media_monitor, handoff_ipc, error);
}

struct RenderPollResult {
    bool transitioned = false;
    bool started = false;
    std::uint32_t actions = 0u;
};

bool PollRenderDemand(
    native_ldac::agent::V1EndpointPresenceSink* sink,
    TrialState* state,
    ULONGLONG now,
    ULONGLONG* render_stop_deadline,
    native_ldac::agent::V1MediaSessionMonitor* media_monitor,
    RenderPollResult* result,
    DWORD* error) {
    using native_ldac::agent::ObserveV1RenderDemand;
    using native_ldac::agent::ReduceV1Lifecycle;
    using native_ldac::agent::V1LifecycleEvent;
    using native_ldac::agent::V1RenderDemandTransition;

    *result = {};
    bool stream_active = false;
    std::uint64_t stream_epoch = 0u;
    if (!sink->QueryRenderActive(&stream_active, &stream_epoch, error)) {
        ++state->render_query_failures;
        state->render_query_last_error =
            error != nullptr ? *error : GetLastError();
        const DWORD query_error = state->render_query_last_error;
        const bool transient =
            query_error == ERROR_TIMEOUT ||
            query_error == WAIT_TIMEOUT ||
            query_error == ERROR_OPERATION_ABORTED ||
            query_error == ERROR_DEVICE_NOT_CONNECTED ||
            query_error == ERROR_INVALID_HANDLE ||
            query_error == ERROR_NOT_READY ||
            query_error == ERROR_FILE_NOT_FOUND ||
            query_error == ERROR_PATH_NOT_FOUND;
        if (!transient) return false;

        // A PnP owner switch can leave one synchronous KS property request
        // pending. Treat that sample as unknown instead of freezing the
        // daily host and starving the physical-presence heartbeat. While no
        // engine/media session owns the endpoint, periodically reopen the
        // current interface so a transient topology transition can recover.
        if (state->lifecycle.engine_lease ==
                native_ldac::agent::V1EngineLease::Absent &&
            state->lifecycle.media_session ==
                native_ldac::agent::V1MediaSession::Stopped &&
            now >= state->render_query_recovery_next_tick) {
            state->render_query_recovery_next_tick = now + 1000u;
            DWORD recovery_error = ERROR_SUCCESS;
            (void)RebindEndpointPresenceLease(
                sink, state, &recovery_error);
        }
        if (error != nullptr) *error = ERROR_SUCCESS;
        return true;
    }
    state->render_query_last_error = ERROR_SUCCESS;
    ++state->render_query_count;
    state->last_stream_epoch = stream_epoch;
    const V1RenderDemandTransition transition = ObserveV1RenderDemand(
        &state->render_tracker,
        stream_active);
    if (transition == V1RenderDemandTransition::None) {
        return true;
    }

    const bool started = transition == V1RenderDemandTransition::Started;
    if (started) {
        ++state->render_started_events;
        if (state->render_stop_pending) {
            state->render_stop_pending = false;
            ++state->render_stop_resumed_events;
            *render_stop_deadline = 0u;
            result->transitioned = true;
            result->started = true;
            std::wprintf(
                L"V1 render resumed within the bounded transition window: "
                L"demand=%ls, epoch=%llu; the current transport remains "
                L"open.\n",
                RenderDemandName(state->lifecycle.render_demand),
                static_cast<unsigned long long>(stream_epoch));
            std::fflush(stdout);
            return true;
        }
    } else {
        ++state->render_stopped_events;
        if (state->media_started_events == 0u) {
            ++state->pre_media_render_stop_events;
        }
        bool paused_media = false;
        if (media_monitor != nullptr && media_monitor->ready()) {
            const auto media = media_monitor->Snapshot(
                state->lifecycle.acl_generation);
            paused_media =
                media.playback == native_ldac::agent::
                    V1MediaSessionPlayback::Paused &&
                media.play_enabled;
        }
        if (paused_media &&
            state->transport_pcm_burst_exercise_enabled &&
            state->lifecycle.media_session ==
                native_ldac::agent::V1MediaSession::Streaming) {
            // GSMTC Paused is a media-session state, not a terminal stop.
            // Keep the engine and AVDTP session alive; the PCM worker fills
            // silence until Render RUN returns.
            result->transitioned = true;
            result->started = false;
            std::wprintf(
                L"V1 render paused with GSMTC Paused; suspending LDAC "
                L"media packets while keeping the transport session open.\n");
            std::fflush(stdout);
            return true;
        }
        if (state->transport_pcm_burst_exercise_enabled &&
            state->lifecycle.media_session ==
                native_ldac::agent::V1MediaSession::Streaming) {
            constexpr ULONGLONG kRenderStopClassificationMs = 2000u;
            state->render_stop_pending = true;
            ++state->render_stop_deferred_events;
            *render_stop_deadline = now + kRenderStopClassificationMs;
            result->transitioned = true;
            result->started = false;
            std::wprintf(
                L"V1 render stopped transiently: epoch=%llu; allowing "
                L"2000 ms for the same playback session to resume before "
                L"SUSPEND/CLOSE.\n",
                static_cast<unsigned long long>(stream_epoch));
            std::fflush(stdout);
            return true;
        }
    }
    const std::uint32_t actions = ReduceV1Lifecycle(
        &state->lifecycle,
        started ? V1LifecycleEvent::RenderStarted
                : V1LifecycleEvent::RenderStopped);
    CountActions(actions, state);
    result->transitioned = true;
    result->started = started;
    result->actions = actions;
    std::wprintf(
        L"V1 render %ls: demand=%ls, epoch=%llu.\n",
        started ? L"started" : L"stopped",
        RenderDemandName(state->lifecycle.render_demand),
        static_cast<unsigned long long>(stream_epoch));
    std::fflush(stdout);
    return true;
}

bool StopContainedEngine(
    native_ldac::agent::V1EngineReadyHost* engine,
    std::uint32_t actions,
    TrialState* state,
    ULONGLONG* ready_deadline,
    DWORD* error) {
    native_ldac::agent::CancelV1TransportOpenStability(
        &state->transport_open_stability);
    if (!engine->active()) {
        *ready_deadline = 0u;
        return true;
    }
    native_ldac::agent::V1EngineStopMode stop_mode =
        native_ldac::agent::V1EngineStopMode::LocalOnly;
    if (state->transport_worker_exercise_enabled &&
        native_ldac::agent::HasV1LifecycleAction(
            actions,
            native_ldac::agent::V1ActionCancelTransport)) {
        stop_mode = native_ldac::agent::V1EngineStopMode::CancelTransport;
    } else if (state->transport_worker_exercise_enabled &&
               native_ldac::agent::HasV1LifecycleAction(
                   actions,
                   native_ldac::agent::V1ActionGracefulStopTransport)) {
        stop_mode =
            native_ldac::agent::V1EngineStopMode::GracefulTransport;
    }
    DWORD exit_code = 0u;
    if (!engine->Stop(stop_mode, 3000u, &exit_code, error)) {
        ++state->engine_stop_failures;
        *ready_deadline = 0u;
        return false;
    }
    ++state->engine_graceful_stops;
    ++state->engine_exit_events;
    state->last_engine_exit_code = exit_code;
    if (stop_mode ==
        native_ldac::agent::V1EngineStopMode::GracefulTransport) {
        ++state->media_stopped_events;
        ++state->transport_stop_acknowledgements;
        if (state->daily_mode &&
            state->transport_pcm_burst_exercise_enabled) {
            ++state->pcm_burst_sessions_completed;
        }
        (void)native_ldac::agent::ReduceV1Lifecycle(
            &state->lifecycle,
            native_ldac::agent::V1LifecycleEvent::MediaStopped);
    } else if (stop_mode ==
               native_ldac::agent::V1EngineStopMode::CancelTransport) {
        ++state->transport_stop_acknowledgements;
    } else {
        const std::uint32_t exit_actions =
            native_ldac::agent::ReduceV1Lifecycle(
                &state->lifecycle,
                native_ldac::agent::V1LifecycleEvent::EngineExited);
        CountActions(exit_actions, state);
    }
    *ready_deadline = 0u;
    std::wprintf(L"V1 contained engine stopped cleanly (exit %lu).\n",
                 exit_code);
    std::fflush(stdout);
    return true;
}

bool ExecuteEngineActions(
    std::uint32_t actions,
    const Options& options,
    native_ldac::agent::V1EngineReadyHost* engine,
    TrialState* state,
    ULONGLONG* ready_deadline,
    DWORD* error) {
    using native_ldac::agent::HasV1LifecycleAction;
    using native_ldac::agent::V1ActionStartEngine;
    using native_ldac::agent::V1ActionStopEngine;
    if (!state->engine_ready_observer_enabled) {
        if (HasV1LifecycleAction(actions, V1ActionStopEngine)) {
            (void)native_ldac::agent::ReduceV1Lifecycle(
                &state->lifecycle,
                native_ldac::agent::V1LifecycleEvent::EngineExited);
        }
        return true;
    }
    if (HasV1LifecycleAction(actions, V1ActionStartEngine)) {
        if (engine->active()) {
            ++state->engine_start_failures;
            if (error != nullptr) {
                *error = ERROR_BUSY;
            }
            return false;
        }
        if (state->transport_worker_sequence !=
            std::numeric_limits<std::uint64_t>::max()) {
            ++state->transport_worker_sequence;
        }
        // Single-gain audio: with volume sync enabled the worker must not
        // apply the PC endpoint volume to the PCM; actual loudness is
        // decided only by the XM5 absolute volume.
        const bool apply_endpoint_volume = !options.volume_sync;
        const std::wstring quality =
            state->applied_quality == native_ldac::agent::V1DailyQuality::Sq
                ? L"sq"
                : state->applied_quality ==
                          native_ldac::agent::V1DailyQuality::Mq
                ? L"mq"
                : L"hq";
        const bool started =
            state->transport_discovery_exercise_enabled
                ? engine->StartTransportDiscoveryWorker(
                      options.engine_stub_path,
                      state->lifecycle.acl_generation,
                      options.transport_result_path,
                      error,
                      apply_endpoint_volume,
                      quality,
                      options.daily_channel_mode,
                      options.daily_sample_rate_hz,
                      options.daily_bits_per_sample)
                : state->transport_worker_exercise_enabled
                ? engine->StartTransportWorker(
                      options.engine_stub_path,
                      state->lifecycle.acl_generation,
                      false,
                      error,
                      apply_endpoint_volume,
                      quality,
                      options.daily_channel_mode,
                      options.daily_sample_rate_hz,
                      options.daily_bits_per_sample)
                : engine->Start(options.engine_stub_path,
                                state->lifecycle.acl_generation,
                                false,
                                error);
        if (!started) {
            ++state->engine_start_failures;
            return false;
        }
        ++state->child_processes_started;
        *ready_deadline = GetTickCount64() + 3000u;
        const wchar_t* child_kind =
            state->daily_mode
                ? L"continuous PCM worker"
                : state->transport_pcm_burst_exercise_enabled
                ? L"bounded PCM worker"
                : state->transport_silence_exercise_enabled
                ? L"four-packet digital-zero worker"
                : state->transport_configuration_exercise_enabled
                ? L"zero-packet configuration worker"
                : state->transport_discovery_exercise_enabled
                ? L"capability-only worker"
                : L"engine";
        std::wprintf(
            L"V1 contained %ls started: PID %lu, generation %llu.\n",
            child_kind,
            engine->process_id(),
            static_cast<unsigned long long>(
                state->lifecycle.acl_generation));
        std::fflush(stdout);
    }
    if (HasV1LifecycleAction(actions, V1ActionStopEngine)) {
        if (!StopContainedEngine(engine,
                                 actions,
                                 state,
                                 ready_deadline,
                                 error)) {
            return false;
        }
        if (state->transport_discovery_exercise_enabled &&
            HasV1LifecycleAction(
                actions,
                native_ldac::agent::V1ActionGracefulStopTransport) &&
            !ArchiveTransportAttemptResult(options, *state, error)) {
            return false;
        }
    }
    return true;
}

bool DeliverTransportOpenAuthorization(
    native_ldac::agent::V1EngineReadyHost* engine,
    TrialState* state,
    bool stable_render_authorization,
    DWORD* error) {
    if (!engine->AuthorizeTransportOpen(error)) {
        return false;
    }
    ++state->transport_open_executed;
    if (stable_render_authorization) {
        ++state->transport_open_stable_authorizations;
    }
    std::wprintf(
        state->daily_mode
            ? L"V1 engine ready; one daily-session PCM authorization was "
              L"delivered after stable Render RUN.\n"
        : state->transport_pcm_burst_exercise_enabled
            ? L"V1 engine ready; one generation-bound bounded PCM authorization "
              L"was delivered to the contained worker.\n"
        : state->transport_silence_exercise_enabled
            ? L"V1 engine ready; one generation-bound four-packet "
              L"silence authorization was delivered to the contained "
              L"worker.\n"
        : state->transport_configuration_exercise_enabled
            ? L"V1 engine ready; one generation-bound zero-packet "
              L"configuration authorization was delivered to the "
              L"contained worker.\n"
        : state->transport_discovery_exercise_enabled
            ? L"V1 engine ready; one generation-bound DISCOVER "
              L"authorization was delivered to the contained worker.\n"
            : L"V1 engine ready; one generation-bound transport OPEN "
              L"authorization was delivered to the event-only worker.\n");
    std::fflush(stdout);
    return true;
}

bool PollContainedEngine(
    native_ldac::agent::V1EngineReadyHost* engine,
    const Options& options,
    TrialState* state,
    ULONGLONG* ready_deadline,
    ULONGLONG* transport_retry_deadline,
    bool* state_changed,
    DWORD* error) {
    using native_ldac::agent::HasV1LifecycleAction;
    using native_ldac::agent::V1ActionOpenTransport;
    if (!engine->active()) {
        return true;
    }
    if (!engine->ready_observed()) {
        bool ready = false;
        if (!engine->PollReady(&ready, error)) {
            return false;
        }
        if (ready) {
            ++state->engine_ready_events;
            const std::uint32_t ready_actions =
                native_ldac::agent::ReduceV1Lifecycle(
                    &state->lifecycle,
                    native_ldac::agent::V1LifecycleEvent::EngineReady);
            CountActions(ready_actions, state);
            if (HasV1LifecycleAction(ready_actions,
                                     V1ActionOpenTransport)) {
                if (state->transport_worker_exercise_enabled) {
                    if (options.transport_open_render_stability_ms != 0u) {
                        native_ldac::agent::ArmV1TransportOpenStability(
                            &state->transport_open_stability,
                            options.transport_open_render_stability_ms,
                            state->lifecycle.acl_generation,
                            state->lifecycle.render_demand ==
                                native_ldac::agent::V1RenderDemand::Running,
                            state->last_stream_epoch,
                            GetTickCount64());
                        ++state->transport_open_stability_waits;
                        std::wprintf(
                            L"V1 engine ready; transport OPEN is waiting "
                            L"for %lu ms of continuous Render RUN in one "
                            L"epoch.\n",
                            options.transport_open_render_stability_ms);
                    } else if (!DeliverTransportOpenAuthorization(
                                   engine, state, false, error)) {
                        return false;
                    }
                } else {
                    (void)native_ldac::agent::ReduceV1Lifecycle(
                        &state->lifecycle,
                        native_ldac::agent::V1LifecycleEvent::
                            TransportOpenSuppressed);
                    std::wprintf(
                        L"V1 engine ready; transport OPEN was requested by "
                        L"the reducer and suppressed by this observer gate.\n");
                }
                std::fflush(stdout);
            }
            *ready_deadline = 0u;
            *state_changed = true;
        } else if (*ready_deadline != 0u &&
                   GetTickCount64() >= *ready_deadline) {
            ++state->engine_ready_timeouts;
            if (error != nullptr) {
                *error = WAIT_TIMEOUT;
            }
            return false;
        }
    }

    if (state->transport_open_stability.pending) {
        const auto stability =
            native_ldac::agent::ObserveV1TransportOpenStability(
                &state->transport_open_stability,
                state->lifecycle.acl_generation,
                state->lifecycle.render_demand ==
                    native_ldac::agent::V1RenderDemand::Running,
                state->last_stream_epoch,
                GetTickCount64());
        if (stability == native_ldac::agent::
                             V1TransportOpenStabilityObservation::Reset) {
            ++state->transport_open_stability_resets;
            *state_changed = true;
            std::wprintf(
                L"V1 transport OPEN stability window reset by Render "
                L"STOP or epoch change; no Bluetooth attempt was "
                L"consumed.\n");
            std::fflush(stdout);
        } else if (stability == native_ldac::agent::
                                    V1TransportOpenStabilityObservation::
                                        Ready) {
            std::wprintf(
                L"V1 Render RUN remained stable for %lu ms in epoch "
                L"%llu; authorizing transport OPEN.\n",
                options.transport_open_render_stability_ms,
                static_cast<unsigned long long>(
                    state->last_stream_epoch));
            std::fflush(stdout);
            if (!DeliverTransportOpenAuthorization(
                    engine, state, true, error)) {
                return false;
            }
            *state_changed = true;
        } else if (stability == native_ldac::agent::
                                    V1TransportOpenStabilityObservation::
                                        Cancelled) {
            *state_changed = true;
        }
    }

    if (state->transport_worker_exercise_enabled && engine->active()) {
        native_ldac::agent::V1TransportWorkerEvent transport_event =
            native_ldac::agent::V1TransportWorkerEvent::None;
        if (!engine->PollTransportEvent(&transport_event, error)) {
            return false;
        }
        if (transport_event ==
            native_ldac::agent::V1TransportWorkerEvent::
                CapabilitiesDiscovered) {
            if (!ArchiveTransportAttemptResult(options, *state, error)) {
                return false;
            }
            ++state->capabilities_discovered_events;
            *state_changed = true;
            if (state->transport_discovery_exercise_enabled) {
                (void)native_ldac::agent::ReduceV1Lifecycle(
                    &state->lifecycle,
                    state->transport_pcm_burst_exercise_enabled
                        ? native_ldac::agent::V1LifecycleEvent::MediaStopped
                        : native_ldac::agent::V1LifecycleEvent::
                            TransportOpenSuppressed);
                DWORD exit_code = 0u;
                if (!engine->Stop(
                        state->transport_pcm_burst_exercise_enabled
                            ? native_ldac::agent::V1EngineStopMode::
                                GracefulTransport
                            : native_ldac::agent::V1EngineStopMode::
                                CancelTransport,
                        3000u,
                        &exit_code,
                        error)) {
                    ++state->engine_stop_failures;
                    return false;
                }
                ++state->media_stopped_events;
                ++state->transport_stop_acknowledgements;
                ++state->engine_graceful_stops;
                ++state->engine_exit_events;
                if (state->transport_pcm_burst_exercise_enabled) {
                    ++state->pcm_burst_sessions_completed;
                } else if (state->transport_silence_exercise_enabled) {
                    ++state->silence_sessions_completed;
                } else if (state->transport_configuration_exercise_enabled) {
                    ++state->configuration_sessions_completed;
                } else {
                    ++state->discovery_sessions_completed;
                }
                state->last_engine_exit_code = exit_code;
                const std::uint32_t exit_actions =
                    native_ldac::agent::ReduceV1Lifecycle(
                        &state->lifecycle,
                        native_ldac::agent::V1LifecycleEvent::EngineExited);
                CountActions(exit_actions, state);
                *ready_deadline = 0u;
                std::wprintf(
                    state->transport_pcm_burst_exercise_enabled
                        ? L"V1 bounded PCM/LDAC "
                          L"session completed with SUSPEND and CLOSE. "
                          L"Stop audio, then turn off XM5.\n"
                    : state->transport_silence_exercise_enabled
                        ? L"V1 AVDTP START, four digital-zero LDAC/RTP "
                          L"packets, SUSPEND, and CLOSE completed. Stop "
                          L"audio, then turn off XM5.\n"
                    : state->transport_configuration_exercise_enabled
                        ? L"V1 LDAC configuration, AVDTP OPEN, Media L2CAP "
                          L"OPEN, and immediate AVDTP CLOSE completed; no "
                          L"START or media packet was sent. Stop audio, "
                          L"then turn off XM5.\n"
                        : L"V1 capability DISCOVER completed and the "
                          L"contained worker closed signaling cleanly. "
                          L"Stop audio, then turn off XM5.\n");
                std::fflush(stdout);
                return true;
            }
            std::wprintf(
                L"V1 event-only worker reported capability discovery.\n");
            std::fflush(stdout);
        } else if (transport_event ==
                   native_ldac::agent::V1TransportWorkerEvent::
                       RetryableOpenFailure) {
            ++state->transport_retryable_failures;
            if (!ArchiveTransportAttemptResult(options, *state, error)) {
                return false;
            }
            const std::uint32_t retry_actions =
                native_ldac::agent::ReduceV1Lifecycle(
                    &state->lifecycle,
                    native_ldac::agent::V1LifecycleEvent::
                        TransportRetryableFailure);
            CountActions(retry_actions, state);
            if (!ExecuteEngineActions(retry_actions,
                                      options,
                                      engine,
                                      state,
                                      ready_deadline,
                                      error)) {
                return false;
            }
            *state_changed = true;
            if (HasV1LifecycleAction(
                    retry_actions,
                    native_ldac::agent::
                        V1ActionScheduleTransportRetry)) {
                const ULONGLONG delay_ms =
                    options.pcm_fast_signaling_acquisition
                    ? native_ldac::agent::GetV1PcmTransportRetryDelayMs(
                        state->lifecycle.open_attempts_for_generation)
                    : native_ldac::agent::GetV1TransportRetryDelayMs(
                        state->lifecycle.open_attempts_for_generation);
                *transport_retry_deadline =
                    GetTickCount64() + delay_ms;
                const wchar_t* retry_message =
                    options.pcm_fast_signaling_acquisition
                    ? L"V1 signaling OPEN was rejected because the remote "
                      L"reported no resources; attempt %u/%u closed "
                      L"locally. One same-generation retry is armed in "
                      L"%llu ms while ACL and render demand remain valid.\n"
                    : L"V1 signaling OPEN was temporarily rejected; "
                      L"attempt %u/%u closed locally. One same-generation "
                      L"retry is armed in %llu ms while ACL and render "
                      L"demand remain valid.\n";
                std::wprintf(
                    retry_message,
                    state->lifecycle.open_attempts_for_generation,
                    state->lifecycle.maximum_open_attempts,
                    static_cast<unsigned long long>(delay_ms));
                std::fflush(stdout);
                return true;
            }
            ++state->transport_retry_budget_exhausted;
            std::wprintf(
                options.pcm_fast_signaling_acquisition
                    ? L"V1 signaling acquisition exhausted after %u "
                      L"diagnostic-confirmed remote no-resources "
                      L"attempts.\n"
                    : L"V1 signaling OPEN Win32 71 budget exhausted after "
                      L"%u bounded zero-exchange attempts.\n",
                state->lifecycle.open_attempts_for_generation);
            std::fflush(stdout);
            return true;
        } else if (transport_event ==
                   native_ldac::agent::V1TransportWorkerEvent::MediaStarted) {
            ++state->media_started_events;
            (void)native_ldac::agent::ReduceV1Lifecycle(
                &state->lifecycle,
                native_ldac::agent::V1LifecycleEvent::MediaStarted);
            *state_changed = true;
            std::wprintf(
                state->daily_mode
                    ? L"V1 daily PCM media started; the frozen transparent "
                      L"audio policy is active.\n"
                : state->transport_pcm_burst_exercise_enabled
                    ? L"V1 bounded PCM media started; the candidate's "
                      L"fixed digital output policy is active.\n"
                    : L"V1 event-only transport worker reported media "
                      L"started.\n");
            std::fflush(stdout);
        } else if (transport_event ==
                   native_ldac::agent::V1TransportWorkerEvent::MediaStopped) {
            ++state->media_stopped_events;
            (void)native_ldac::agent::ReduceV1Lifecycle(
                &state->lifecycle,
                native_ldac::agent::V1LifecycleEvent::MediaStopped);
            *state_changed = true;
        } else if (transport_event ==
                   native_ldac::agent::V1TransportWorkerEvent::MediaFailed) {
            ++state->media_failed_events;
            if (!ArchiveTransportAttemptResult(options, *state, error)) {
                return false;
            }
            const std::uint32_t failure_actions =
                native_ldac::agent::ReduceV1Lifecycle(
                    &state->lifecycle,
                    native_ldac::agent::V1LifecycleEvent::MediaFailed);
            CountActions(failure_actions, state);
            if (!ExecuteEngineActions(failure_actions,
                                      options,
                                      engine,
                                      state,
                                      ready_deadline,
                                      error)) {
                return false;
            }
            *state_changed = true;
            std::wprintf(
                L"V1 contained transport attempt ended with bounded "
                L"failure; the session result contains the exact stage "
                L"and cause.\n");
            std::fflush(stdout);
            return true;
        }
    }

    bool exited = false;
    DWORD exit_code = 0u;
    if (!engine->PollExited(&exited, &exit_code, error)) {
        return false;
    }
    if (exited) {
        native_ldac::agent::CancelV1TransportOpenStability(
            &state->transport_open_stability);
        ++state->engine_exit_events;
        ++state->engine_unexpected_exits;
        state->last_engine_exit_code = exit_code;
        const std::uint32_t exit_actions =
            native_ldac::agent::ReduceV1Lifecycle(
                &state->lifecycle,
                native_ldac::agent::V1LifecycleEvent::EngineExited);
        CountActions(exit_actions, state);
        engine->Close();
        std::fwprintf(stderr,
                      L"V1 contained engine exited unexpectedly "
                      L"(exit %lu).\n",
                      exit_code);
        if (error != nullptr) {
            *error = ERROR_PROCESS_ABORTED;
        }
        return false;
    }
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    // Daily mode redirects the native streams into a PowerShell pipeline.
    // Disable CRT buffering so action lines retain their real event timing;
    // the runner performs its own PC-slider display coalescing downstream.
    (void)setvbuf(stdout, nullptr, _IONBF, 0u);
    (void)setvbuf(stderr, nullptr, _IONBF, 0u);
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

    if (options.stop_daily) {
        DWORD stop_error = ERROR_SUCCESS;
        if (!native_ldac::agent::V1DailyInstance::SignalStop(
                options.instance_suffix,
                &stop_error)) {
            std::fwprintf(stderr,
                          L"V1 daily instance stop signal failed "
                          L"(Win32 %lu).\n",
                          stop_error);
            return 15;
        }
        std::wprintf(L"V1 daily instance stop requested: %ls.\n",
                     options.instance_suffix.c_str());
        return 0;
    }

    const HRESULT initialize_hr = CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialize_hr) && initialize_hr != RPC_E_CHANGED_MODE) {
        std::fwprintf(stderr, L"CoInitializeEx failed 0x%08lX\n",
                      initialize_hr);
        return 4;
    }

    native_ldac::agent::V1DailyInstance daily_instance;
    if (options.daily_mode) {
        DWORD instance_error = ERROR_SUCCESS;
        if (!daily_instance.Acquire(options.instance_suffix,
                                    &instance_error)) {
            std::fwprintf(stderr,
                          L"V1 daily instance acquisition failed "
                          L"(Win32 %lu).\n",
                          instance_error);
            return 15;
        }
    }

    native_ldac::agent::V1DailyConfigServer config_server;
    if (options.daily_mode) {
        const std::filesystem::path state_path(options.state_path);
        const std::wstring persistence_path =
            (state_path.parent_path() / L"daily-config.bin").wstring();
        DWORD config_error = ERROR_SUCCESS;
        native_ldac::agent::V1DailyQuality initial_quality{};
        if (!native_ldac::agent::ParseV1DailyQuality(
                options.daily_quality, &initial_quality)) {
            return 2;
        }
        const std::wstring config_pipe =
            L"NativeLdac.V1.Config." + options.instance_suffix;
        if (!config_server.Start(
                config_pipe,
                persistence_path,
                initial_quality,
                0u,
                &config_error)) {
            std::fwprintf(stderr,
                          L"V1 daily config IPC start failed (Win32 %lu).\n",
                          config_error);
            return 16;
        }
    }

    native_ldac::agent::Xm5AclWatcher watcher(0);
    if (!watcher.Start()) {
        std::fwprintf(stderr,
                      L"V1 ACL watcher start failed (Win32 %lu).\n",
                      watcher.start_error());
        return 11;
    }

    TrialState state;
    state.daily_mode = options.daily_mode;
    if (options.daily_mode) {
        state.config_pipe = config_server.pipe_name();
        state.requested_quality = config_server.requested_quality();
        state.applied_quality = config_server.applied_quality();
        state.requested_config_revision =
            config_server.requested_revision();
        state.applied_config_revision = config_server.applied_revision();
        state.config_rejected_count = config_server.rejected_count();
        state.config_last_error = config_server.last_error();
    }
    state.avrcp_observer_enabled = options.daily_mode;
    state.avrcp_volume_sync_enabled = options.volume_sync;
    state.avrcp_handoff_enabled = options.handoff;
    state.hfp_transport_switch_enabled = options.hfp_transport_switch;
    state.endpoint_sink_enabled = options.endpoint_presence;
    state.render_observer_enabled = options.observe_render_demand;
    state.engine_ready_observer_enabled = options.observe_engine_ready;
    state.transport_worker_exercise_enabled =
        options.exercise_transport_worker ||
        options.exercise_transport_discovery ||
        options.exercise_transport_configuration ||
        options.exercise_transport_silence ||
        options.exercise_transport_pcm_burst;
    state.transport_discovery_exercise_enabled =
        options.exercise_transport_discovery ||
        options.exercise_transport_configuration ||
        options.exercise_transport_silence ||
        options.exercise_transport_pcm_burst;
    state.transport_configuration_exercise_enabled =
        options.exercise_transport_configuration ||
        options.exercise_transport_silence ||
        options.exercise_transport_pcm_burst;
    state.transport_silence_exercise_enabled =
        options.exercise_transport_silence;
    state.transport_pcm_burst_exercise_enabled =
        options.exercise_transport_pcm_burst;
    state.playback_disconnect_wait_enabled =
        options.await_playback_disconnect ||
        options.await_playback_reconnect;
    state.playback_reconnect_wait_enabled =
        options.await_playback_reconnect;
    state.playback_reconnect_target_generations =
        options.await_playback_reconnect
            ? static_cast<unsigned int>(
                  options.playback_reconnect_generations)
            : 1u;
    state.transport_open_stability.required_ms =
        options.transport_open_render_stability_ms;
    if (options.exercise_transport_pcm_burst) {
        state.lifecycle.maximum_open_attempts =
            native_ldac::agent::kV1MaximumPcmTransportOpenAttempts;
        state.lifecycle.tolerate_pretransport_render_gaps = true;
    }
    if (options.daily_mode) {
        state.lifecycle.allow_multiple_media_sessions = true;
    }
    native_ldac::agent::V1EndpointPresenceSink endpoint_sink;
    native_ldac::agent::V1EngineReadyHost engine_host;
    native_ldac::agent::V1AvrcpObserverWin32Io avrcp_observer_io;
    native_ldac::agent::V1AvrcpObserverHost avrcp_observer(
        &avrcp_observer_io, options.volume_sync);
    native_ldac::agent::V1AvrcpFilterHost avrcp_filter_host;
    native_ldac::agent::V1AvrcpHandoffIpc avrcp_handoff_ipc(
        options.handoff);
    native_ldac::agent::V1MediaSessionMonitor media_session_monitor;
    native_ldac::agent::V1HfpCaptureMonitor hfp_capture_monitor;
    native_ldac::agent::V1HfpRenderEndpointMonitor
        hfp_render_endpoint_monitor;
    native_ldac::agent::V1HfpShadowState hfp_shadow_state;
    native_ldac::agent::V1AvrcpWindowsSink avrcp_windows_sink(
        options.volume_sync,
        options.volume_sync
            ? static_cast<native_ldac::agent::V1AvrcpBluetoothWriter*>(
                  &avrcp_filter_host)
            : avrcp_observer.writer(),
        options.daily_mode && options.volume_sync);
    DWORD media_monitor_error = ERROR_SUCCESS;
    if (options.daily_mode &&
        !media_session_monitor.Start(&media_monitor_error)) {
        std::wprintf(
            L"V1 media-session monitor unavailable (Win32 %lu); media "
            L"gestures remain fail-closed while volume sync continues.\n",
            media_monitor_error);
    }
    DWORD hfp_capture_monitor_error = ERROR_SUCCESS;
    if (options.daily_mode &&
        !hfp_capture_monitor.Start(&hfp_capture_monitor_error)) {
        std::wprintf(
            L"V1 HFP capture monitor unavailable (Win32 %lu); automatic "
            L"HFP switching remains fail-closed.\n",
            hfp_capture_monitor_error);
    }
    DWORD hfp_render_monitor_error = ERROR_SUCCESS;
    if (options.daily_mode &&
        !hfp_render_endpoint_monitor.Start(&hfp_render_monitor_error)) {
        std::wprintf(
            L"V1 HFP render endpoint monitor unavailable (Win32 %lu); "
            L"HFP output bridge remains fail-closed.\n",
            hfp_render_monitor_error);
    }
    if (options.endpoint_presence) {
        DWORD sink_error = ERROR_SUCCESS;
        if (!endpoint_sink.Open(&sink_error)) {
            std::fwprintf(stderr,
                          L"V1 endpoint presence sink open failed "
                          L"(Win32 %lu).\n",
                          sink_error);
            return 14;
        }
    }
    native_ldac::agent::ResetV1RenderDemandTracker(
        &state.render_tracker);
    if (!WriteStateAtomically(options.state_path, L"armed", state)) {
        std::fwprintf(stderr,
                      L"V1 presence state write failed (Win32 %lu).\n",
                      GetLastError());
        return 12;
    }
    if (options.daily_mode) {
        std::wprintf(
            L"V1 daily presence agent armed until an explicit stop request "
            L"for instance %ls.\n",
            options.instance_suffix.c_str());
    } else {
        std::wprintf(L"V1 presence agent armed for %lu ms.\n",
                     options.run_for_ms);
    }
    if (options.observe_engine_ready) {
        if (options.exercise_transport_pcm_burst) {
            std::wprintf(options.daily_mode
                ? L"One Job Object-contained continuous PCM worker may run "
                  L"per playback session; each normal STOP must "
                  L"SUSPEND/CLOSE before the same ACL can start another "
                  L"session.\n"
                : L"One Job Object-contained bounded PCM worker may start; "
                  L"it must confirm non-silent PCM before Bluetooth OPEN, "
                  L"apply its fixed candidate gain ceiling, and "
                  L"SUSPEND/CLOSE at the bounded profile limit.\n");
        } else if (options.exercise_transport_silence) {
            std::wprintf(
                L"One Job Object-contained four-packet digital-zero "
                L"worker may start; it must SUSPEND/CLOSE before "
                L"reporting completion.\n");
        } else if (options.exercise_transport_configuration) {
            std::wprintf(
                L"One Job Object-contained zero-packet configuration "
                L"worker may start; it closes AVDTP and Media L2CAP "
                L"before reporting completion.\n");
        } else if (options.exercise_transport_discovery) {
            std::wprintf(
                L"One Job Object-contained capability-only DISCOVER worker "
                L"may start; it cannot configure or open media.\n");
        } else if (options.exercise_transport_worker) {
            std::wprintf(
                L"One Job Object-contained event-only transport worker "
                L"may start; it has no Bluetooth or PCM implementation.\n");
        } else {
            std::wprintf(
                L"One Job Object-contained no-media engine may start on "
                L"render demand; Bluetooth transport execution is "
                L"disabled.\n");
        }
    } else {
        std::wprintf(
            L"No child process or Bluetooth transport will be started.\n");
    }
    if (options.endpoint_presence) {
        std::wprintf(
            L"Physical-presence lease sink is enabled; media link state "
            L"remains untouched.\n");
    }
    if (options.observe_render_demand) {
        if (options.observe_engine_ready) {
            if (options.exercise_transport_pcm_burst) {
                std::wprintf(
                    options.daily_mode
                        ? L"Daily engine readiness requires 1000 ms of "
                          L"continuous Render RUN and may use at most four "
                          L"diagnostic-confirmed 1/2/4-second signaling "
                          L"acquisition attempts per playback session.\n"
                    : options.pcm_fast_signaling_acquisition
                        ? L"Engine readiness may authorize at most four "
                          L"bounded PCM attempts; only a diagnostic-"
                          L"confirmed remote no-resources response at "
                          L"OpenSignaling is retryable with 1/2/4-second "
                          L"acquisition delays. Pre-START render gaps keep "
                          L"the bounded local PCM wait alive.\n"
                        : L"Engine readiness may authorize at most four "
                          L"bounded PCM attempts; only Win32 71 at "
                          L"OpenSignaling is retryable. Pre-START render "
                          L"gaps keep the bounded local PCM wait alive.\n");
            } else if (options.exercise_transport_silence) {
                std::wprintf(
                    L"Engine readiness may authorize at most three "
                    L"four-packet silence attempts; only Win32 71 at "
                    L"OpenSignaling is retryable.\n");
            } else if (options.exercise_transport_configuration) {
                std::wprintf(
                    L"Engine readiness may authorize at most three "
                    L"configuration attempts for the current ACL "
                    L"generation; only Win32 71 at OpenSignaling is "
                    L"retryable.\n");
            } else if (options.exercise_transport_discovery) {
                std::wprintf(
                    L"Engine readiness may authorize at most three "
                    L"capability-only DISCOVER attempts for the current "
                    L"ACL generation; only Win32 71 at OpenSignaling is "
                    L"retryable.\n");
            } else if (options.exercise_transport_worker) {
                std::wprintf(
                    L"Engine readiness may authorize exactly one event-only "
                    L"OPEN for the current ACL generation.\n");
            } else {
                std::wprintf(
                    L"Engine readiness is observed; every reducer transport "
                    L"OPEN action remains suppressed.\n");
            }
        } else {
            std::wprintf(
                L"Render-demand observer is enabled only while XM5 is "
                L"present; engine actions are recorded but suppressed.\n");
        }
    }
    if (options.transport_open_render_stability_ms != 0u) {
        std::wprintf(
            L"Transport OPEN requires %lu ms of continuous Render RUN "
            L"in one epoch; pre-media STOP or epoch changes reset this "
            L"local wait without consuming a Bluetooth attempt.\n",
            options.transport_open_render_stability_ms);
    }
    std::fflush(stdout);

    const ULONGLONG deadline = options.daily_mode
        ? std::numeric_limits<ULONGLONG>::max()
        : GetTickCount64() + options.run_for_ms;
    constexpr ULONGLONG kPresenceHeartbeatMs = 5000u;
    constexpr ULONGLONG kRenderPollMs = 250u;
    constexpr ULONGLONG kHfpShadowPollMs = 250u;
    constexpr ULONGLONG kAvrcpActivePollMs = 25u;
    ULONGLONG next_presence_heartbeat = 0u;
    ULONGLONG next_render_poll = 0u;
    ULONGLONG next_hfp_shadow_poll = options.daily_mode
        ? GetTickCount64() + kHfpShadowPollMs
        : 0u;
    ULONGLONG engine_ready_deadline = 0u;
    ULONGLONG transport_retry_deadline = 0u;
    ULONGLONG playback_disconnect_failure_deadline = 0u;
    ULONGLONG render_start_deadline = 0u;
    unsigned int render_started_events_at_connect = 0u;
    ULONGLONG render_stop_deadline = 0u;
    ULONGLONG avrcp_observer_next_attempt = 0u;
    ULONGLONG next_avrcp_poll = 0u;
    bool wait_failed = false;
    const unsigned int playback_disconnect_target =
        state.playback_reconnect_target_generations;
    for (;;) {
        const ULONGLONG now = GetTickCount64();
        bool config_state_changed = false;
        if (options.daily_mode) {
            native_ldac::agent::V1DailyConfigRequest accepted{};
            if (config_server.TakeAccepted(&accepted)) {
                state.requested_quality =
                    static_cast<native_ldac::agent::V1DailyQuality>(
                        accepted.quality);
                state.requested_config_revision = accepted.revision;
                config_state_changed = true;
                std::wprintf(
                    L"V1 daily quality %ls revision %llu accepted; it will "
                    L"apply at the next safe media-session start.\n",
                    native_ldac::agent::V1DailyQualityName(
                        state.requested_quality),
                    static_cast<unsigned long long>(
                        state.requested_config_revision));
                std::fflush(stdout);
            }
            if (!engine_host.active() &&
                state.applied_config_revision !=
                    state.requested_config_revision) {
                DWORD config_error = ERROR_SUCCESS;
                if (config_server.MarkApplied(
                        state.requested_config_revision, &config_error)) {
                    state.applied_quality = state.requested_quality;
                    state.applied_config_revision =
                        state.requested_config_revision;
                    config_state_changed = true;
                } else {
                    state.config_last_error = config_error;
                }
            }
            const unsigned int rejected_count =
                config_server.rejected_count();
            const DWORD config_last_error = config_server.last_error();
            if (state.config_rejected_count != rejected_count ||
                state.config_last_error != config_last_error) {
                state.config_rejected_count = rejected_count;
                state.config_last_error = config_last_error;
                config_state_changed = true;
            }
            if (config_state_changed &&
                !WriteStateAtomically(
                    options.state_path,
                    state.lifecycle.physical_presence ==
                            native_ldac::agent::V1PhysicalPresence::Present
                        ? L"present"
                        : L"absent",
                    state)) {
                wait_failed = true;
                break;
            }
        }
        const bool resident_filter_poll =
            state.avrcp_volume_sync_enabled &&
            state.lifecycle.physical_presence ==
                native_ldac::agent::V1PhysicalPresence::Present &&
            !state.lifecycle.hfp_suspended;
        if (resident_filter_poll || avrcp_filter_host.session_active() ||
            avrcp_observer.media_session_active()) {
            if (next_avrcp_poll == 0u) next_avrcp_poll = now;
        } else {
            next_avrcp_poll = 0u;
        }
        if (state.playback_disconnect_wait_enabled &&
            state.media_failed_events == 1u &&
            playback_disconnect_failure_deadline == 0u) {
            playback_disconnect_failure_deadline = now + 30000u;
        }
        if (!options.daily_mode &&
            state.transport_pcm_burst_exercise_enabled &&
            state.media_started_events == playback_disconnect_target &&
            state.media_failed_events == 0u &&
            state.connected_events == playback_disconnect_target &&
            state.disconnected_events == playback_disconnect_target &&
            state.transport_stop_acknowledgements ==
                state.transport_open_executed &&
            state.engine_exit_events == state.child_processes_started &&
            state.lifecycle.physical_presence ==
                native_ldac::agent::V1PhysicalPresence::Absent &&
            state.lifecycle.render_demand ==
                native_ldac::agent::V1RenderDemand::Idle &&
            state.lifecycle.open_attempts_for_generation == 0u &&
            !engine_host.active()) {
            break;
        }
        if (!options.daily_mode &&
            state.transport_pcm_burst_exercise_enabled &&
            state.pcm_burst_sessions_completed == 1u &&
            !engine_host.active()) {
            break;
        }
        if (!options.daily_mode &&
            state.transport_worker_exercise_enabled &&
            state.media_failed_events == 1u &&
            !engine_host.active() &&
            (!state.playback_disconnect_wait_enabled ||
             state.disconnected_events == 1u ||
             state.render_stopped_events != 0u ||
             now >= playback_disconnect_failure_deadline)) {
            break;
        }
        if (!options.daily_mode &&
            state.transport_retry_budget_exhausted == 1u &&
            !engine_host.active()) {
            break;
        }
        if (!options.daily_mode &&
            state.transport_silence_exercise_enabled &&
            state.silence_sessions_completed == 1u &&
            !engine_host.active()) {
            break;
        }
        if (!options.daily_mode &&
            state.transport_discovery_exercise_enabled &&
            state.discovery_sessions_completed +
                    state.configuration_sessions_completed +
                    state.silence_sessions_completed +
                    state.pcm_burst_sessions_completed ==
                1u &&
            state.disconnected_events == 1u &&
            !engine_host.active()) {
            break;
        }
        if (!options.daily_mode && now >= deadline) {
            break;
        }
        ULONGLONG wake_tick = deadline;
        if (next_presence_heartbeat != 0u &&
            next_presence_heartbeat < wake_tick) {
            wake_tick = next_presence_heartbeat;
        }
        if (next_render_poll != 0u && next_render_poll < wake_tick) {
            wake_tick = next_render_poll;
        }
        if (next_hfp_shadow_poll != 0u &&
            next_hfp_shadow_poll < wake_tick) {
            wake_tick = next_hfp_shadow_poll;
        }
        if (next_avrcp_poll != 0u && next_avrcp_poll < wake_tick) {
            wake_tick = next_avrcp_poll;
        }
        if (transport_retry_deadline != 0u &&
            transport_retry_deadline < wake_tick) {
            wake_tick = transport_retry_deadline;
        }
        if (playback_disconnect_failure_deadline != 0u &&
            playback_disconnect_failure_deadline < wake_tick) {
            wake_tick = playback_disconnect_failure_deadline;
        }
        if (render_stop_deadline != 0u &&
            render_stop_deadline < wake_tick) {
            wake_tick = render_stop_deadline;
        }
        if (render_start_deadline != 0u &&
            render_start_deadline < wake_tick) {
            wake_tick = render_start_deadline;
        }
        const ULONGLONG remaining = wake_tick > now
                                        ? wake_tick - now
                                        : 0u;
        const DWORD timeout = remaining > MAXDWORD
                                  ? MAXDWORD
                                  : static_cast<DWORD>(remaining);
        HANDLE wait_handles[3] = {
            watcher.change_event(),
            daily_instance.stop_event(),
            avrcp_windows_sink.volume_change_event(),
        };
        const DWORD wait_handle_count =
            options.daily_mode && wait_handles[2] != nullptr ? 3u : 2u;
        const DWORD wait = options.daily_mode
            ? WaitForMultipleObjects(
                  wait_handle_count, wait_handles, FALSE, timeout)
            : WaitForSingleObject(watcher.change_event(), timeout);
        if (wait == WAIT_TIMEOUT) {
            const ULONGLONG timeout_now = GetTickCount64();
            if (!options.daily_mode && timeout_now >= deadline) {
                break;
            }
            bool state_changed = false;
            if (next_avrcp_poll != 0u &&
                timeout_now >= next_avrcp_poll) {
                DWORD avrcp_error = ERROR_SUCCESS;
                if (!ReconcileAvrcpControl(
                        &avrcp_filter_host,
                        &avrcp_observer,
                        &avrcp_windows_sink,
                        &engine_host,
                        &endpoint_sink,
                        &state,
                        timeout_now,
                        &avrcp_observer_next_attempt,
                        &media_session_monitor,
                        &avrcp_handoff_ipc,
                        &avrcp_error)) {
                    std::fwprintf(
                        stderr,
                        L"V1 active AVRCP reconciliation failed "
                        L"(Win32 %lu).\n",
                        avrcp_error);
                    wait_failed = true;
                    break;
                }
                next_avrcp_poll =
                    ((state.avrcp_volume_sync_enabled &&
                      state.lifecycle.physical_presence ==
                          native_ldac::agent::V1PhysicalPresence::Present &&
                      !state.lifecycle.hfp_suspended) ||
                     avrcp_filter_host.session_active() ||
                     avrcp_observer.media_session_active())
                    ? GetTickCount64() + kAvrcpActivePollMs
                    : 0u;
            }
            if (options.endpoint_presence &&
                next_presence_heartbeat != 0u &&
                timeout_now >= next_presence_heartbeat &&
                state.lifecycle.physical_presence ==
                    native_ldac::agent::V1PhysicalPresence::Present) {
                DWORD sink_error = ERROR_SUCCESS;
                if (!endpoint_sink.Set(
                        true,
                        state.lifecycle.acl_generation,
                        &sink_error)) {
                    ++state.endpoint_presence_failures;
                    std::fwprintf(
                        stderr,
                        L"V1 physical-presence heartbeat failed "
                        L"(Win32 %lu).\n",
                        sink_error);
                    wait_failed = true;
                    break;
                }
                ++state.endpoint_presence_updates;
                next_presence_heartbeat =
                    timeout_now + kPresenceHeartbeatMs;
                state_changed = true;
            }
            if (options.observe_render_demand &&
                next_render_poll != 0u &&
                timeout_now >= next_render_poll &&
                state.lifecycle.physical_presence ==
                    native_ldac::agent::V1PhysicalPresence::Present) {
                DWORD query_error = ERROR_SUCCESS;
                RenderPollResult render_result;
                if (!PollRenderDemand(&endpoint_sink,
                                      &state,
                                      timeout_now,
                                      &render_stop_deadline,
                                      &media_session_monitor,
                                      &render_result,
                                      &query_error)) {
                    std::fwprintf(
                        stderr,
                        L"V1 render-demand query failed (Win32 %lu).\n",
                        query_error);
                    wait_failed = true;
                    break;
                }
                // A Bluetooth PDO owner switch can make Windows report the
                // Native LDAC render endpoint as unplugged. Give the handoff
                // a chance to finish before starting or authorizing the PCM
                // worker for this RenderStarted edge. The later reconciliation
                // remains necessary for MediaStarted and observer polling.
                if (render_result.transitioned && render_result.started &&
                    state.avrcp_handoff_enabled &&
                    !ReconcileAvrcpControl(
                        &avrcp_filter_host,
                        &avrcp_observer,
                        &avrcp_windows_sink,
                        &engine_host,
                        &endpoint_sink,
                        &state,
                        timeout_now,
                        &avrcp_observer_next_attempt,
                        &media_session_monitor,
                        &avrcp_handoff_ipc,
                        &query_error)) {
                    std::fwprintf(
                        stderr,
                        L"V1 daily AVRCP pre-media reconciliation failed "
                        L"(Win32 %lu).\n",
                        query_error);
                    wait_failed = true;
                    break;
                }
                if (render_result.transitioned &&
                    !ExecuteEngineActions(render_result.actions,
                                          options,
                                          &engine_host,
                                          &state,
                                          &engine_ready_deadline,
                                          &query_error)) {
                    std::fwprintf(
                        stderr,
                        L"V1 contained engine action failed "
                        L"(Win32 %lu).\n",
                        query_error);
                    wait_failed = true;
                    break;
                }
                if (render_result.transitioned &&
                    !render_result.started) {
                    transport_retry_deadline = 0u;
                }
                if (render_result.transitioned &&
                    render_result.started) {
                    render_start_deadline = 0u;
                }
                next_render_poll = timeout_now + kRenderPollMs;
                state_changed = state_changed ||
                                render_result.transitioned;
                const unsigned int media_failed_before =
                    state.media_failed_events;
                if (options.observe_engine_ready &&
                    !PollContainedEngine(&engine_host,
                                         options,
                                         &state,
                                         &engine_ready_deadline,
                                         &transport_retry_deadline,
                                         &state_changed,
                                         &query_error)) {
                    std::fwprintf(
                        stderr,
                        L"V1 contained engine observation failed "
                        L"(Win32 %lu).\n",
                        query_error);
                    wait_failed = true;
                    break;
                }
                if (!ReconcileAvrcpControl(
                        &avrcp_filter_host,
                        &avrcp_observer,
                        &avrcp_windows_sink,
                        &engine_host,
                        &endpoint_sink,
                        &state,
                        timeout_now,
                        &avrcp_observer_next_attempt,
                        &media_session_monitor,
                        &avrcp_handoff_ipc,
                        &query_error)) {
                    std::fwprintf(
                        stderr,
                        L"V1 daily AVRCP observer reconciliation failed "
                        L"(Win32 %lu).\n",
                        query_error);
                    wait_failed = true;
                    break;
                }
                if (state.playback_disconnect_wait_enabled &&
                    media_failed_before == 0u &&
                    state.media_failed_events == 1u) {
                    if (!endpoint_sink.Set(
                            false,
                            state.lifecycle.acl_generation,
                            &query_error)) {
                        ++state.endpoint_presence_failures;
                        std::fwprintf(
                            stderr,
                            L"V1 playback-disconnect fail-closed lease "
                            L"release failed (Win32 %lu).\n",
                            query_error);
                        wait_failed = true;
                        break;
                    }
                    ++state.endpoint_presence_updates;
                    state.playback_disconnect_fail_closed_release = true;
                    next_presence_heartbeat = 0u;
                    next_render_poll = 0u;
                    std::wprintf(
                        L"V1 playback-disconnect fallback released the "
                        L"endpoint lease while awaiting physical ACL "
                        L"disconnect.\n");
                    std::fflush(stdout);
                }
            }
            if (next_hfp_shadow_poll != 0u &&
                timeout_now >= next_hfp_shadow_poll) {
                native_ldac::agent::V1HfpLifecyclePlan hfp_plan;
                state_changed = ReconcileHfpShadow(
                    &hfp_capture_monitor,
                    &hfp_shadow_state,
                    &state,
                    timeout_now,
                    options.hfp_transport_switch,
                    &hfp_plan) || state_changed;
                state_changed = ReconcileHfpRenderEndpoint(
                    &hfp_render_endpoint_monitor,
                    &state) || state_changed;
                if (options.hfp_transport_switch &&
                    !hfp_plan.shadow_only &&
                    !hfp_plan.stale && !hfp_plan.invalid &&
                    hfp_plan.command !=
                        native_ldac::agent::V1HfpLifecycleCommand::None) {
                    const auto lifecycle_event =
                        native_ldac::agent::V1HfpLifecycleEventForCommand(
                            hfp_plan.command);
                    const std::uint32_t lifecycle_actions =
                        native_ldac::agent::ReduceV1Lifecycle(
                            &state.lifecycle, lifecycle_event);
                    CountActions(lifecycle_actions, &state);
                    DWORD hfp_error = ERROR_SUCCESS;
                    if (!ExecuteEngineActions(
                            lifecycle_actions,
                            options,
                            &engine_host,
                            &state,
                            &engine_ready_deadline,
                            &hfp_error)) {
                        ++state.hfp_lifecycle_execution_failures;
                        std::fwprintf(
                            stderr,
                            L"V1 HFP transport lifecycle execution failed "
                            L"(Win32 %lu).\n",
                            hfp_error);
                        wait_failed = true;
                        break;
                    }
                    ++state.hfp_lifecycle_executions;
                    state_changed = true;
                }
                next_hfp_shadow_poll =
                    timeout_now + kHfpShadowPollMs;
            }
            // Observe a last-moment RUN edge before classifying the pending
            // STOP. Both deadlines can become due in the same wait cycle.
            if (render_stop_deadline != 0u &&
                timeout_now >= render_stop_deadline &&
                state.render_stop_pending) {
                state.render_stop_pending = false;
                render_stop_deadline = 0u;
                ++state.render_stop_timeout_events;
                const std::uint32_t stop_actions =
                    native_ldac::agent::ReduceV1Lifecycle(
                        &state.lifecycle,
                        native_ldac::agent::V1LifecycleEvent::RenderStopped);
                CountActions(stop_actions, &state);
                DWORD stop_error = ERROR_SUCCESS;
                if (!ExecuteEngineActions(stop_actions,
                                          options,
                                          &engine_host,
                                          &state,
                                          &engine_ready_deadline,
                                          &stop_error)) {
                    std::fwprintf(
                        stderr,
                        L"V1 classified Render STOP action failed "
                        L"(Win32 %lu).\n",
                        stop_error);
                    wait_failed = true;
                    break;
                }
                transport_retry_deadline = 0u;
                state_changed = true;
                std::wprintf(
                    L"V1 render stop persisted for 2000 ms; it is now "
                    L"classified as a normal playback stop.\n");
                std::fflush(stdout);
            }
            if (render_start_deadline != 0u &&
                timeout_now >= render_start_deadline &&
                state.render_started_events ==
                    render_started_events_at_connect) {
                render_start_deadline = 0u;
                state.render_start_timed_out = true;
                std::wprintf(
                    L"V1 render start did not arrive within %lu ms of "
                    L"physical ACL connect; ending this bounded trial.\n",
                    options.render_start_timeout_ms);
                std::fflush(stdout);
                if (!WriteStateAtomically(options.state_path,
                                          L"present",
                                          state)) {
                    wait_failed = true;
                }
                break;
            }
            if (transport_retry_deadline != 0u &&
                timeout_now >= transport_retry_deadline) {
                transport_retry_deadline = 0u;
                const std::uint32_t retry_actions =
                    native_ldac::agent::ReduceV1Lifecycle(
                        &state.lifecycle,
                        native_ldac::agent::V1LifecycleEvent::
                            TransportRetryDue);
                CountActions(retry_actions, &state);
                DWORD retry_error = ERROR_SUCCESS;
                if (!ExecuteEngineActions(retry_actions,
                                          options,
                                          &engine_host,
                                          &state,
                                          &engine_ready_deadline,
                                          &retry_error)) {
                    std::fwprintf(
                        stderr,
                        L"V1 bounded transport retry start failed "
                        L"(Win32 %lu).\n",
                        retry_error);
                    wait_failed = true;
                    break;
                }
                state_changed = true;
            }
            if (state_changed &&
                !WriteStateAtomically(options.state_path,
                                      L"present",
                                      state)) {
                wait_failed = true;
                break;
            }
            continue;
        }
        if (options.daily_mode && wait == WAIT_OBJECT_0 + 1u) {
            std::wprintf(
                L"V1 daily stop request received; closing the active "
                L"transport gracefully when possible.\n");
            std::fflush(stdout);
            if (state.render_stop_pending ||
                state.lifecycle.render_demand ==
                    native_ldac::agent::V1RenderDemand::Running) {
                state.render_stop_pending = false;
                render_stop_deadline = 0u;
                const std::uint32_t stop_actions =
                    native_ldac::agent::ReduceV1Lifecycle(
                        &state.lifecycle,
                        native_ldac::agent::V1LifecycleEvent::RenderStopped);
                CountActions(stop_actions, &state);
                DWORD stop_error = ERROR_SUCCESS;
                if (!ExecuteEngineActions(stop_actions,
                                          options,
                                          &engine_host,
                                          &state,
                                          &engine_ready_deadline,
                                          &stop_error)) {
                    std::fwprintf(
                        stderr,
                        L"V1 daily transport shutdown failed "
                        L"(Win32 %lu).\n",
                        stop_error);
                    wait_failed = true;
                }
                transport_retry_deadline = 0u;
            }
            break;
        }
        if (options.daily_mode && wait_handle_count == 3u &&
            wait == WAIT_OBJECT_0 + 2u) {
            DWORD avrcp_error = ERROR_SUCCESS;
            if (!ReconcileAvrcpControl(
                    &avrcp_filter_host,
                    &avrcp_observer,
                    &avrcp_windows_sink,
                    &engine_host,
                    &endpoint_sink,
                    &state,
                    GetTickCount64(),
                    &avrcp_observer_next_attempt,
                    &media_session_monitor,
                    &avrcp_handoff_ipc,
                    &avrcp_error)) {
                std::fwprintf(
                    stderr,
                    L"V1 event-driven AVRCP volume reconciliation failed "
                    L"(Win32 %lu).\n",
                    avrcp_error);
                wait_failed = true;
                break;
            }
            continue;
        }
        if (wait != WAIT_OBJECT_0) {
            wait_failed = true;
            break;
        }

        native_ldac::agent::Xm5AclEvent acl_event;
        while (watcher.TryPop(&acl_event)) {
            const bool connected =
                acl_event == native_ldac::agent::Xm5AclEvent::Connected;
            const bool engine_was_active = engine_host.active();
            const std::uint32_t ending_transport_attempt =
                state.lifecycle.open_attempts_for_generation;
            if (connected) {
                ++state.connected_events;
                render_started_events_at_connect =
                    state.render_started_events;
                if (options.render_start_timeout_ms != 0u &&
                    (state.render_started_events == 0u ||
                     state.playback_reconnect_wait_enabled)) {
                    render_start_deadline =
                        GetTickCount64() + options.render_start_timeout_ms;
                }
            } else {
                ++state.disconnected_events;
            }
            const std::uint32_t actions =
                native_ldac::agent::ReduceV1Lifecycle(
                    &state.lifecycle,
                    connected
                        ? native_ldac::agent::V1LifecycleEvent::AclConnected
                        : native_ldac::agent::V1LifecycleEvent::AclDisconnected);
            CountActions(actions, &state);
            DWORD engine_error = ERROR_SUCCESS;
            if (!ExecuteEngineActions(actions,
                                      options,
                                      &engine_host,
                                      &state,
                                      &engine_ready_deadline,
                                      &engine_error)) {
                std::fwprintf(stderr,
                              L"V1 contained engine ACL action failed "
                              L"(Win32 %lu).\n",
                              engine_error);
                wait_failed = true;
                break;
            }
            if (!connected && engine_was_active &&
                native_ldac::agent::HasV1LifecycleAction(
                    actions,
                    native_ldac::agent::V1ActionCancelTransport)) {
                DWORD archive_error = ERROR_SUCCESS;
                if (!ArchiveTransportAttemptResultAt(
                        options,
                        state,
                        ending_transport_attempt,
                        &archive_error)) {
                    std::fwprintf(
                        stderr,
                        L"V1 cancelled transport result archive failed "
                        L"(Win32 %lu).\n",
                        archive_error);
                    wait_failed = true;
                    break;
                }
            }
            DWORD sink_error = ERROR_SUCCESS;
            if (!ApplyEndpointPresence(actions,
                                       &endpoint_sink,
                                       &state,
                                       &sink_error)) {
                std::fwprintf(stderr,
                              L"V1 physical-presence publish failed "
                              L"(Win32 %lu).\n",
                              sink_error);
                wait_failed = true;
                break;
            }
            next_presence_heartbeat =
                options.endpoint_presence && connected
                    ? GetTickCount64() + kPresenceHeartbeatMs
                    : 0u;
            next_render_poll =
                options.observe_render_demand && connected
                    ? GetTickCount64()
                    : 0u;
            if (connected) {
                // A failed media session can leave Render RUN active while
                // the ACL reconnects. Re-arm the edge detector so the new
                // ACL generation receives a fresh engine start.
                native_ldac::agent::ResetV1RenderDemandTracker(
                    &state.render_tracker);
            }
            if (!connected) {
                avrcp_filter_host.EndSession();
                if (avrcp_observer.media_session_active()) {
                    avrcp_observer.EndMediaSession();
                    if (avrcp_handoff_ipc.enabled()) {
                        ++state.avrcp_handoff_restores;
                        DWORD restore_error = ERROR_SUCCESS;
                        if (!avrcp_handoff_ipc.RequestRestore(
                                state.lifecycle.acl_generation,
                                0u,
                                &restore_error) ||
                            avrcp_handoff_ipc.WaitRestoreDone(
                                30000u,
                                &restore_error) !=
                                native_ldac::agent::
                                    V1AvrcpHandoffWaitResult::Ok) {
                            ++state.avrcp_handoff_restore_errors;
                            std::wprintf(
                                L"V1 ACL disconnect AVRCP restore failed "
                                L"(Win32 %lu).\n",
                                restore_error);
                        } else {
                            std::wprintf(
                                L"V1 ACL disconnect restored Microsoft "
                                L"AVRCP before releasing the generation.\n");
                        }
                        std::fflush(stdout);
                    }
                }
                if (!avrcp_observer.media_session_active() &&
                    avrcp_handoff_ipc.enabled() &&
                    state.avrcp_handoff_ready_generation ==
                        state.lifecycle.acl_generation) {
                    ++state.avrcp_handoff_restores;
                    DWORD restore_error = ERROR_SUCCESS;
                    const bool restored = avrcp_handoff_ipc.RequestRestore(
                            state.lifecycle.acl_generation,
                            0u,
                            &restore_error) &&
                        avrcp_handoff_ipc.WaitRestoreDone(
                            30000u, &restore_error) ==
                            native_ldac::agent::V1AvrcpHandoffWaitResult::Ok;
                    if (!restored) ++state.avrcp_handoff_restore_errors;
                    state.avrcp_handoff_ready_generation = 0u;
                }
                avrcp_observer.Close();
                state.avrcp_observer_active = false;
                state.avrcp_control_ready_deadline = 0u;
                avrcp_observer_next_attempt = 0u;
                next_avrcp_poll = 0u;
                state.avrcp_handoff_failed_generation = 0u;
                state.avrcp_handoff_ready_generation = 0u;
                transport_retry_deadline = 0u;
                render_start_deadline = 0u;
                if (state.render_stop_pending) {
                    state.render_stop_pending = false;
                    render_stop_deadline = 0u;
                    ++state.render_stop_acl_cancelled_events;
                }
                native_ldac::agent::ResetV1RenderDemandTracker(
                    &state.render_tracker);
            }
            std::wprintf(L"V1 ACL %ls: presence=%ls, generation=%llu.\n",
                         connected ? L"connected" : L"disconnected",
                         PresenceName(state.lifecycle.physical_presence),
                         static_cast<unsigned long long>(
                             state.lifecycle.acl_generation));
            std::fflush(stdout);
            if (!WriteStateAtomically(options.state_path,
                                      connected ? L"present" : L"absent",
                                      state)) {
                std::fwprintf(stderr,
                              L"V1 presence state update failed "
                              L"(Win32 %lu).\n",
                              GetLastError());
                wait_failed = true;
                break;
            }
            if (connected && state.avrcp_volume_sync_enabled) {
                DWORD avrcp_error = ERROR_SUCCESS;
                if (!ReconcileAvrcpControl(
                        &avrcp_filter_host,
                        &avrcp_observer,
                        &avrcp_windows_sink,
                        &engine_host,
                        &endpoint_sink,
                        &state,
                        GetTickCount64(),
                        &avrcp_observer_next_attempt,
                        &media_session_monitor,
                        &avrcp_handoff_ipc,
                        &avrcp_error)) {
                    std::fwprintf(
                        stderr,
                        L"V1 ACL connection control hook failed "
                        L"(Win32 %lu).\n",
                        avrcp_error);
                    wait_failed = true;
                    break;
                }
                next_avrcp_poll =
                    GetTickCount64() + kAvrcpActivePollMs;
            }
            if (!connected && state.playback_reconnect_wait_enabled) {
                DWORD archive_error = ERROR_SUCCESS;
                if (!ArchiveGenerationState(options,
                                            state,
                                            &archive_error)) {
                    std::fwprintf(
                        stderr,
                        L"V1 generation state archive failed "
                        L"(Win32 %lu).\n",
                        archive_error);
                    wait_failed = true;
                    break;
                }
                if (state.disconnected_events <
                    state.playback_reconnect_target_generations) {
                    std::wprintf(
                        L"V1 reconnect checkpoint reached. Wait for "
                        L"Windows public XM5 state and Native LDAC to "
                        L"become disconnected, then turn on XM5 for "
                        L"generation %u.\n",
                        state.disconnected_events + 1u);
                    std::fflush(stdout);
                }
            }
        }
        if (wait_failed) {
            break;
        }
    }

    if (options.endpoint_presence &&
        state.lifecycle.physical_presence ==
            native_ldac::agent::V1PhysicalPresence::Present) {
        const std::uint32_t cleanup_actions =
            native_ldac::agent::ReduceV1Lifecycle(
                &state.lifecycle,
                native_ldac::agent::V1LifecycleEvent::WatcherLeaseExpired);
        CountActions(cleanup_actions, &state);
        DWORD engine_error = ERROR_SUCCESS;
        if (!ExecuteEngineActions(cleanup_actions,
                                  options,
                                  &engine_host,
                                  &state,
                                  &engine_ready_deadline,
                                  &engine_error)) {
            std::fwprintf(stderr,
                          L"V1 contained engine cleanup failed "
                          L"(Win32 %lu).\n",
                          engine_error);
            wait_failed = true;
        }
        DWORD sink_error = ERROR_SUCCESS;
        if (!ApplyEndpointPresence(cleanup_actions,
                                   &endpoint_sink,
                                   &state,
                                   &sink_error)) {
            std::fwprintf(stderr,
                          L"V1 physical-presence cleanup failed "
                          L"(Win32 %lu).\n",
                          sink_error);
            wait_failed = true;
        }
    }
    if (avrcp_observer.media_session_active()) {
        avrcp_observer.EndMediaSession();
        if (avrcp_handoff_ipc.enabled()) {
            ++state.avrcp_handoff_restores;
            DWORD restore_error = ERROR_SUCCESS;
            if (!avrcp_handoff_ipc.RequestRestore(
                    state.lifecycle.acl_generation,
                    0u,
                    &restore_error) ||
                avrcp_handoff_ipc.WaitRestoreDone(
                    30000u,
                    &restore_error) !=
                    native_ldac::agent::V1AvrcpHandoffWaitResult::Ok) {
                ++state.avrcp_handoff_restore_errors;
                std::wprintf(
                    L"V1 shutdown AVRCP restore failed (Win32 %lu).\n",
                    restore_error);
            } else {
                std::wprintf(
                    L"V1 shutdown restored Microsoft AVRCP before exit.\n");
            }
            std::fflush(stdout);
        }
    }
    if (!avrcp_observer.media_session_active() &&
        avrcp_handoff_ipc.enabled() &&
        state.avrcp_handoff_ready_generation ==
            state.lifecycle.acl_generation) {
        ++state.avrcp_handoff_restores;
        DWORD restore_error = ERROR_SUCCESS;
        const bool restored = avrcp_handoff_ipc.RequestRestore(
                state.lifecycle.acl_generation,
                0u,
                &restore_error) &&
            avrcp_handoff_ipc.WaitRestoreDone(
                30000u, &restore_error) ==
                native_ldac::agent::V1AvrcpHandoffWaitResult::Ok;
        if (!restored) ++state.avrcp_handoff_restore_errors;
        state.avrcp_handoff_ready_generation = 0u;
    }
    avrcp_observer.Close();
    avrcp_filter_host.EndSession();
    avrcp_filter_host.Close();
    hfp_render_endpoint_monitor.Stop();
    hfp_capture_monitor.Stop();
    media_session_monitor.Stop();
    state.avrcp_observer_active = false;
    state.avrcp_control_ready_deadline = 0u;
    state.avrcp_handoff_ready_generation = 0u;
    engine_host.Close();
    watcher.Stop();
    if (!WriteStateAtomically(options.state_path, L"stopped", state)) {
        std::fwprintf(stderr,
                      L"V1 final state write failed (Win32 %lu).\n",
                      GetLastError());
        return 12;
    }
    std::wprintf(
        L"V1 presence agent stopped: %u connect, %u disconnect, "
        L"%u transport OPEN, %u child process, %u endpoint update, "
        L"%u endpoint failure, %u render start, %u render stop, "
        L"%u render query failure, %u engine ready, %u clean engine "
        L"stop, %u AVRCP activation attempt, %u AVRCP activation failure, "
        L"%u AVRCP poll failure, %u transport OPEN executed, %u capability discovery, "
        L"%u discovery session complete, %u configuration session "
        L"complete, %u silence session complete, %u PCM burst session "
        L"complete.\n",
        state.connected_events,
        state.disconnected_events,
        state.transport_open_actions,
        state.child_processes_started,
        state.endpoint_presence_updates,
        state.endpoint_presence_failures,
        state.render_started_events,
        state.render_stopped_events,
        state.render_query_failures,
        state.engine_ready_events,
        state.engine_graceful_stops,
        state.avrcp_observer_activation_attempts,
        state.avrcp_observer_activation_failures,
        state.avrcp_observer_poll_failures,
        state.transport_open_executed,
        state.capabilities_discovered_events,
        state.discovery_sessions_completed,
        state.configuration_sessions_completed,
        state.silence_sessions_completed,
        state.pcm_burst_sessions_completed);
    std::fflush(stdout);
    CoUninitialize();
    return wait_failed ? 13 : 0;
}
