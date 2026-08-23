// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "ldac_native/ldac_encoder.h"
#include "v1_transport_silence_session.h"

namespace native_ldac::agent {

enum class V1TransportPcmReadDisposition : std::uint32_t {
    Data = 0,
    Timeout,
    StreamStopped,
    Failure,
};

enum class V1TransportPcmStopDisposition : std::uint32_t {
    None = 0,
    Graceful,
    Cancel,
};

enum class V1TransportPcmLimiterMode : std::uint32_t {
    HardClip = 0,
    LinkedStereoBlock = 1,
    LinkedStereoSamplePeakFidelity = 2,
};

struct V1TransportPcmFormat {
    unsigned sample_rate_hz = 0u;
    unsigned bits_per_sample = 0u;
    std::uint64_t stream_epoch = 0u;
    bool volume_control_available = false;
    bool muted = false;
    float volume_scalar = 0.0f;
    float volume_db = 0.0f;
};

// A point-in-time snapshot from the Native LDAC WaveRT PCM ring. The format
// fields remain separate because the transport policy compares them for
// rebind safety; the counters and lifecycle flags are diagnostic evidence.
struct V1TransportPcmSnapshot {
    V1TransportPcmFormat format = {};
    unsigned available_bytes = 0u;
    unsigned capacity_bytes = 0u;
    bool stream_active = false;
    bool discontinuity = false;
    std::uint64_t total_bytes_written = 0u;
    std::uint64_t total_bytes_read = 0u;
    std::uint64_t total_bytes_dropped = 0u;
};

class V1TransportPcmSource {
public:
    virtual ~V1TransportPcmSource() = default;
    virtual bool Prepare(V1TransportPcmFormat* format,
                         std::uint32_t timeout_ms,
                         std::uint32_t* error) = 0;
    virtual V1TransportPcmReadDisposition ReadFrames(
        float* interleaved_stereo,
        std::size_t requested_frames,
        std::uint32_t timeout_ms,
        std::size_t* frames_read,
        std::uint32_t* error) = 0;
    virtual bool QueryFormat(V1TransportPcmFormat* format,
                             std::uint32_t* error) = 0;
    // Optional richer snapshot. Existing test sources only implement
    // QueryFormat; their format remains valid and the diagnostic counters
    // stay zero until a native source supplies the snapshot.
    virtual bool QuerySnapshot(V1TransportPcmSnapshot* snapshot,
                               std::uint32_t* error) {
        if (snapshot == nullptr) return false;
        return QueryFormat(&snapshot->format, error);
    }
    virtual bool WaitUntilSample(std::uint64_t sample_offset,
                                 unsigned sample_rate_hz,
                                 std::uint32_t* error) = 0;
    virtual bool ResetPacing(std::uint32_t* error) {
        if (error != nullptr) *error = 0u;
        return true;
    }
    virtual bool Release(std::uint32_t* error) = 0;
};

using V1TransportPcmStopProbe = V1TransportPcmStopDisposition (*)(
    void* context);
using V1TransportPcmStartedNotifier = bool (*)(void* context,
                                                std::uint32_t* error);

struct V1TransportPcmOptions {
    std::uint32_t open_timeout_ms = 10000u;
    std::uint32_t exchange_timeout_ms = 5000u;
    std::uint32_t media_timeout_ms = 5000u;
    std::uint32_t pcm_read_timeout_ms = 250u;
    std::uint32_t pcm_timeout_tolerance_ms = 0u;
    std::uint32_t post_start_stop_classification_timeout_ms = 0u;
    std::uint32_t audible_preflight_timeout_ms = 3000u;
    std::uint32_t duration_ms = 10000u;
    std::uint32_t maximum_packets = 4096u;
    std::uint16_t preferred_media_mtu = 1000u;
    float maximum_gain_scalar = 0.01f;
    float maximum_output_peak = 0.25f;
    float audible_peak_threshold = 0.0001f;
    V1TransportPcmLimiterMode limiter_mode =
        V1TransportPcmLimiterMode::HardClip;
    float limiter_release_ms = 50.0f;
    std::uint64_t session_generation = 0u;
    bool continuous_until_stop = false;
    // Pause without media packets: send AVDTP SUSPEND, release the PCM
    // consumer, then START the same open transport when Render returns.
    bool pause_suspend = false;
    // Single-gain mode deliberately disables endpoint-volume stability
    // polling; XM5 absolute volume is the only applied loudness gain.
    bool single_gain_mode = false;
    bool require_stable_volume = false;
    bool allow_dynamic_volume = false;
    bool allow_post_start_pcm_rebind = false;
    bool observe_peer_close_while_streaming = false;
    float startup_silence_ms = 0.0f;
    float fade_in_ms = 0.0f;
    float ceiling_ramp_start = 0.25f;
    float ceiling_ramp_ms = 0.0f;
    ldac_encoder_quality quality = LDAC_ENCODER_QUALITY_HQ;
    ldac_encoder_channel_mode channel_mode =
        LDAC_ENCODER_CHANNEL_STEREO;
};

struct V1TransportPcmResult : V1TransportSilenceResult {
    ldac_encoder_quality encoder_quality = LDAC_ENCODER_QUALITY_HQ;
    unsigned nominal_ldac_bitrate_kbps = 0u;
    V1TransportPcmFormat pcm_format = {};
    std::uint64_t pcm_frames_read = 0u;
    std::uint64_t pcm_frames_sent = 0u;
    std::uint64_t transport_frames_sent = 0u;
    std::uint64_t pre_start_pcm_frames_discarded = 0u;
    std::uint32_t pcm_prepare_attempts = 0u;
    std::uint32_t pcm_epoch_restarts = 0u;
    std::uint32_t pcm_stream_stop_count = 0u;
    bool pcm_stream_stop_detected = false;
    std::uint32_t pcm_stream_stop_error = 0u;
    bool pcm_stream_stop_snapshot_valid = false;
    std::uint32_t pcm_stream_stop_snapshot_error = 0u;
    std::uint64_t pcm_stream_stop_elapsed_ms = 0u;
    V1TransportPcmSnapshot pcm_stream_stop_snapshot = {};
    std::uint32_t pcm_rebind_attempts = 0u;
    std::uint32_t pcm_rebind_successes = 0u;
    std::uint32_t pcm_rebind_failures = 0u;
    std::uint32_t pcm_rebind_last_error = 0u;
    std::uint32_t pcm_rebind_last_timeout_ms = 0u;
    std::uint64_t pcm_rebind_last_elapsed_ms = 0u;
    std::uint32_t consumer_lease_acquire_count = 0u;
    std::uint32_t consumer_lease_release_count = 0u;
    std::uint32_t target_duration_ms = 0u;
    std::uint32_t actual_duration_ms = 0u;
    std::uint32_t pacing_waits = 0u;
    std::uint32_t pcm_transient_timeout_count = 0u;
    std::uint32_t pcm_transient_timeout_recovery_count = 0u;
    std::uint32_t pcm_transient_timeout_exhausted_count = 0u;
    std::uint64_t pcm_transient_timeout_max_streak_ms = 0u;
    std::uint32_t media_write_not_ready_retries = 0u;
    std::uint32_t media_write_not_ready_exhaustions = 0u;
    float maximum_gain_scalar = 0.0f;
    float maximum_output_peak_ceiling = 0.0f;
    float maximum_pre_gain_peak = 0.0f;
    float maximum_unlimited_post_gain_peak = 0.0f;
    float maximum_post_gain_peak = 0.0f;
    std::uint64_t limited_output_samples = 0u;
    V1TransportPcmLimiterMode limiter_mode =
        V1TransportPcmLimiterMode::HardClip;
    float limiter_release_ms = 0.0f;
    float limiter_minimum_gain = 1.0f;
    float limiter_last_gain = 1.0f;
    float limiter_maximum_gain_step = 0.0f;
    std::uint64_t limiter_blocks_processed = 0u;
    std::uint64_t limiter_attack_count = 0u;
    std::uint64_t limiter_gain_reduced_frames = 0u;
    std::uint64_t limiter_gain_reduced_samples = 0u;
    std::uint64_t limiter_fallback_clamp_count = 0u;
    std::uint64_t limiter_sanitized_sample_count = 0u;
    std::uint64_t limiter_pre_over_ceiling_frames = 0u;
    std::uint64_t limiter_pre_over_ceiling_samples = 0u;
    std::uint64_t session_generation = 0u;
    std::uint64_t volume_query_count = 0u;
    std::uint64_t volume_change_count = 0u;
    float volume_scalar_minimum = 0.0f;
    float volume_scalar_maximum = 0.0f;
    float volume_scalar_last = 0.0f;
    float volume_db_minimum = 0.0f;
    float volume_db_maximum = 0.0f;
    float volume_db_last = 0.0f;
    float startup_silence_ms = 0.0f;
    std::uint64_t startup_silence_frames_sent = 0u;
    std::uint32_t startup_silence_packets_written = 0u;
    float fade_in_ms = 0.0f;
    std::uint64_t fade_duration_frames = 0u;
    std::uint64_t fade_committed_sent_frames = 0u;
    std::uint64_t fade_frames_below_unity = 0u;
    std::uint64_t fade_blocks_prepared = 0u;
    std::uint64_t fade_blocks_committed = 0u;
    std::uint64_t fade_commit_failures = 0u;
    std::uint64_t fade_sanitized_sample_count = 0u;
    float fade_minimum_gain = 1.0f;
    float fade_last_gain = 1.0f;
    std::uint32_t boundary_resume_count = 0u;
    std::uint64_t boundary_resume_fade_frames = 0u;
    std::uint32_t pause_suspend_count = 0u;
    std::uint32_t pause_resume_start_count = 0u;
    std::uint32_t pause_wait_prepare_attempts = 0u;
    float ceiling_ramp_start = 0.0f;
    float ceiling_ramp_ms = 0.0f;
    float ceiling_ramp_last = 0.0f;
    bool volume_stable = false;
    bool fade_session_started = false;
    bool pcm_prepared = false;
    bool consumer_lease_acquired = false;
    bool consumer_lease_released = false;
    bool consumer_lease_held = false;
    bool audible_pcm_confirmed_before_open = false;
    bool media_started_notified = false;
    bool completed_full_duration = false;
    bool ended_by_graceful_stop = false;
};

V1TransportPcmResult RunV1TransportPcmBurstOnce(
    V1TransportSilenceBackend* backend,
    V1TransportPcmSource* source,
    const V1TransportPcmOptions& options,
    V1TransportPcmStopProbe stop_probe = nullptr,
    void* stop_context = nullptr,
    V1TransportPcmStartedNotifier started_notifier = nullptr,
    void* started_context = nullptr);

}  // namespace native_ldac::agent
