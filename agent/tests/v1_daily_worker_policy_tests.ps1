# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    return Get-Content -LiteralPath `
        (Join-Path $projectRoot $RelativePath) -Raw
}

$daily = Read-ProjectFile 'agent\v1_transport_daily_worker.cpp'
$normal = Read-ProjectFile 'agent\v1_transport_normal_stop_worker.cpp'
$header = Read-ProjectFile 'agent\v1_transport_pcm_session.h'
$core = Read-ProjectFile 'agent\v1_transport_pcm_session.cpp'
$shared = Read-ProjectFile 'agent\v1_transport_configuration_worker.cpp'
$tests = Read-ProjectFile `
    'agent\tests\v1_transport_pcm_session_tests.cpp'
$cmake = Read-ProjectFile 'CMakeLists.txt'

foreach ($required in @(
        '#define V1_TRANSPORT_PCM_WORKER 1',
        '#define V1_TRANSPORT_PCM_DURATION_MS 0u',
        '#define V1_TRANSPORT_PCM_MAXIMUM_GAIN_SCALAR 1.0f',
        '#define V1_TRANSPORT_PCM_MAXIMUM_OUTPUT_PEAK 1.0f',
        '#define V1_TRANSPORT_PCM_MAXIMUM_PACKETS 0u',
        '#define V1_TRANSPORT_PCM_CONTINUOUS_UNTIL_STOP 1',
        '#define V1_TRANSPORT_PCM_PAUSE_SUSPEND 1',
        '#define V1_TRANSPORT_PCM_TIMEOUT_TOLERANCE_MS 2000u',
        '#define V1_TRANSPORT_PCM_AUDIBLE_PREFLIGHT_TIMEOUT_MS 120000u',
        '#define V1_TRANSPORT_PCM_POST_START_STOP_CLASSIFICATION_TIMEOUT_MS 30000u',
        '#define V1_TRANSPORT_PCM_SAMPLE_PEAK_FIDELITY 1',
        '#define V1_TRANSPORT_PCM_REQUIRE_STABLE_VOLUME 1',
        '#define V1_TRANSPORT_PCM_ALLOW_DYNAMIC_VOLUME 1',
        '#define V1_TRANSPORT_PCM_ALLOW_POST_START_REBIND 1',
        '#define V1_TRANSPORT_PCM_OBSERVE_PEER_CLOSE 1',
        '#define V1_TRANSPORT_PCM_STARTUP_SILENCE_MS 20.0f',
        '#define V1_TRANSPORT_PCM_FADE_IN_MS 500.0f',
        '#define V1_TRANSPORT_PCM_CEILING_RAMP_START 1.0f',
        '#define V1_TRANSPORT_PCM_CEILING_RAMP_MS 0.0f',
        '#include "v1_transport_configuration_worker.cpp"')) {
    if (-not $daily.Contains($required)) {
        throw "The daily worker profile is missing: $required"
    }
}

foreach ($required in @(
        '#define V1_TRANSPORT_PCM_DURATION_MS 60000u',
        '#define V1_TRANSPORT_PCM_MAXIMUM_PACKETS 32768u',
        '#define V1_TRANSPORT_PCM_MAXIMUM_GAIN_SCALAR 1.0f',
        '#define V1_TRANSPORT_PCM_MAXIMUM_OUTPUT_PEAK 1.0f',
        '#define V1_TRANSPORT_PCM_SAMPLE_PEAK_FIDELITY 1',
        '#define V1_TRANSPORT_PCM_ALLOW_DYNAMIC_VOLUME 1',
        '#define V1_TRANSPORT_PCM_ALLOW_POST_START_REBIND 1',
        '#define V1_TRANSPORT_PCM_OBSERVE_PEER_CLOSE 1',
        '#define V1_TRANSPORT_PCM_STARTUP_SILENCE_MS 20.0f',
        '#define V1_TRANSPORT_PCM_FADE_IN_MS 100.0f',
        '#define V1_TRANSPORT_PCM_CEILING_RAMP_START 1.0f',
        '#define V1_TRANSPORT_PCM_CEILING_RAMP_MS 0.0f')) {
    if (-not $normal.Contains($required)) {
        throw "The frozen normal-stop profile changed: $required"
    }
}
if ($normal.Contains('V1_TRANSPORT_PCM_CONTINUOUS_UNTIL_STOP')) {
    throw 'The bounded normal-stop worker enabled continuous mode.'
}

foreach ($required in @(
        'bool continuous_until_stop = false',
        'options.continuous_until_stop',
        'options.duration_ms != 0u',
        'options.maximum_packets != 0u',
        'stop_probe == nullptr',
        'std::numeric_limits<std::uint64_t>::max()',
        'options.maximum_packets != 0u &&',
        'result->target_duration_ms != 0u')) {
    if (-not ($header.Contains($required) -or $core.Contains($required))) {
        throw "The continuous PCM contract is missing: $required"
    }
}
foreach ($required in @(
        'struct V1TransportPcmSnapshot',
        'QuerySnapshot',
        'single_gain_mode',
        'pcm_stream_stop_count',
        'pcm_stream_stop_snapshot_valid',
        'pcm_rebind_last_error',
        'PcmElapsedMilliseconds',
        'RecordPcmStreamStop',
        'pause_suspend',
        'pause_suspend_count',
        'pause_resume_start_count',
        'pause_wait_prepare_attempts',
        'pcm_timeout_tolerance_ms',
        'pcm_transient_timeout_count',
        'pcm_transient_timeout_recovery_count')) {
    if (-not ($header.Contains($required) -or
              $core.Contains($required) -or
              $shared.Contains($required))) {
        throw "The PCM lifecycle diagnostic contract is missing: $required"
    }
}
if (-not $shared.Contains(
        '#ifdef V1_TRANSPORT_PCM_CONTINUOUS_UNTIL_STOP') -or
    -not $shared.Contains('run_options.continuous_until_stop = true')) {
    throw 'The shared contained worker does not map continuous mode.'
}
foreach ($required in @(
        '#ifdef V1_TRANSPORT_PCM_STARTUP_SILENCE_MS',
        'run_options.startup_silence_ms',
        '#ifdef V1_TRANSPORT_PCM_FADE_IN_MS',
        'run_options.fade_in_ms')) {
    if (-not $shared.Contains($required)) {
        throw "The shared worker boundary policy is missing: $required"
    }
}
foreach ($required in @(
        'pcm_stream_stop_detected',
        'pcm_stream_stop_snapshot_valid',
        'pcm_stream_stop_total_bytes_dropped',
        'pcm_rebind_last_error',
        'pcm_rebind_last_timeout_ms',
        'pause_suspend_count',
        'pause_resume_start_count',
        'pause_wait_prepare_attempts',
        'pcm_transient_timeout_count',
        'pcm_transient_timeout_recovery_count',
        'pcm_transient_timeout_exhausted_count',
        'pcm_transient_timeout_max_streak_ms')) {
    if (-not $shared.Contains($required)) {
        throw "The PCM result serializer is missing diagnostic evidence: $required"
    }
}

foreach ($required in @(
        'InvalidContinuousProfileBoundsNeverPrepare',
        'ContinuousGracefulStopSuspendsAndCloses',
        'ContinuousCancelStopsWithoutBlindSignaling',
        'ContinuousStartupSilenceIsTransportOnly',
        'DailyExtendedFadeInIsSupported',
        'TransientBoundaryResumeThenGracefulStopUsesResumeFade',
        'PauseSuspendResumesSameTransportWithoutMediaPackets',
        'PauseSuspendThenGracefulStopClosesWithoutResumeStart',
        'RepeatedPauseSuspendCyclesReuseOneTransport',
        'PauseSuspendThenCancelDoesNotSignalRemoteClose',
        'TransientPcmTimeoutRecoversWithFreshPacingBoundary',
        'PcmTimeoutToleranceExhaustsBounded',
        'TimeoutThenStreamStopUsesPauseSuspend',
        'InvalidPcmTimeoutToleranceNeverPrepares',
        'SingleGainFidelityProfileIsValid',
        'PostStartRebindFailureRecordsPcmEvidence',
        'pcm_stream_stop_error == 232u',
        'pcm_rebind_last_error == 258u',
        'result.target_duration_ms == 0u',
        'result.ended_by_graceful_stop',
        'result.remote_stream_cleanup_required')) {
    if (-not $tests.Contains($required)) {
        throw "The continuous PCM unit coverage is missing: $required"
    }
}

foreach ($required in @(
        'add_executable(v1_transport_daily_worker',
        'v1_transport_daily_worker_help',
        'v1_daily_worker_policy',
        'agent/tests/v1_daily_worker_policy_tests.ps1')) {
    if (-not $cmake.Contains($required)) {
        throw "The daily worker build/test target is missing: $required"
    }
}

Write-Host 'V1 continuous daily worker policy tests passed.'
