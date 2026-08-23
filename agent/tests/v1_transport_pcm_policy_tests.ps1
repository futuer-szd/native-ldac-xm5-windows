# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    return Get-Content -LiteralPath `
        (Join-Path $projectRoot $RelativePath) -Raw
}

$header = Read-ProjectFile 'agent\v1_transport_pcm_session.h'
$core = Read-ProjectFile 'agent\v1_transport_pcm_session.cpp'
$adapter = Read-ProjectFile `
    'agent\v1_transport_pcm_source_adapter.cpp'
$wrapper = Read-ProjectFile 'agent\v1_transport_pcm_worker.cpp'
$sharedWorker = Read-ProjectFile `
    'agent\v1_transport_configuration_worker.cpp'
$agent = Read-ProjectFile 'agent\v1_presence_agent.cpp'
$common = Read-ProjectFile 'tools\v1-pcm-burst-common.ps1'
$build = Read-ProjectFile 'tools\build-v1-pcm-burst-candidate.ps1'
$gate = Read-ProjectFile 'tools\run-v1-pcm-burst-gate.ps1'
$tests = Read-ProjectFile `
    'agent\tests\v1_transport_pcm_session_tests.cpp'
$cmake = Read-ProjectFile 'CMakeLists.txt'

foreach ($required in @(
        'duration_ms = 10000u',
        'kMaximumDurationMs = 60000u',
        'kMaximumPacketCount = 32768u',
        'kMaximumPreflightEpochRestarts = 32u',
        'kMaximumPcmTimeoutToleranceMs = 5000u',
        'kHardMaximumGainScalar = 1.0f',
        'kHardMaximumOutputPeak = 0.25f',
        'audible_pcm_confirmed_before_open',
        'PrepareAudiblePcm',
        'pcm_epoch_restarts',
        'consumer_lease_acquire_count',
        'consumer_lease_release_count',
        'maximum_gain_scalar',
        'maximum_output_peak',
        'WaitUntilSample',
        'ResetPacing',
        'options.maximum_packets > kMaximumPacketCount',
        'maximum_unlimited_post_gain_peak',
        'limited_output_samples',
        'ldac_encoder_encode_f32',
        'ldac_rtp_build_unfragmented',
        'avdtp_source_start',
        'avdtp_source_suspend',
        'avdtp_source_close',
        'remote_stream_cleanup_required')) {
    if (-not ($header.Contains($required) -or $core.Contains($required))) {
        throw "V1 PCM transport core is missing: $required"
    }
}
$prepare = $core.LastIndexOf('PrepareAudiblePcm')
$open = $core.IndexOf('backend->OpenSignaling', $prepare)
$start = $core.IndexOf('avdtp_source_start', $open)
$pace = $core.LastIndexOf('source->WaitUntilSample')
$write = $core.LastIndexOf('WriteMediaWithTransientNotReadyRetry')
$suspend = $core.LastIndexOf('avdtp_source_suspend')
$close = $core.LastIndexOf('avdtp_source_close')
if ($prepare -lt 0 -or $open -lt 0 -or $start -lt 0 -or $pace -lt 0 -or
    $write -lt 0 -or $suspend -lt 0 -or $close -lt 0 -or
    $prepare -ge $open -or $open -ge $start -or $start -ge $pace -or
    $pace -ge $write -or $write -ge $suspend -or $suspend -ge $close) {
    throw 'The PCM sequence is not audible-preflight/OPEN/START/pace/write/SUSPEND/CLOSE.'
}
foreach ($forbidden in @(
        'native_pcm_source_report_link_state',
        'NativeLdacPcmPropertyLinkState',
        'SetDefaultEndpoint',
        'Sleep(')) {
    if (($core + $adapter).IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "V1 PCM transport exceeds its authority: $forbidden"
    }
}
foreach ($required in @(
        'native_pcm_source_acquire_consumer',
        'native_pcm_source_release_consumer',
        'native_pcm_source_read_f32_stereo',
        'WaitForSingleObject',
        'QueryPerformanceCounter')) {
    if (-not $adapter.Contains($required)) {
        throw "V1 PCM source adapter is missing: $required"
    }
}
if (-not $wrapper.Contains('#define V1_TRANSPORT_PCM_WORKER 1') -or
    -not $wrapper.Contains(
        '#define V1_TRANSPORT_PCM_MAXIMUM_OUTPUT_PEAK 0.01f') -or
    -not $wrapper.Contains(
        '#include "v1_transport_configuration_worker.cpp"')) {
    throw 'The PCM worker does not own its compile-time switch.'
}
foreach ($required in @(
        'RunV1TransportPcmBurstOnce',
        'WritePcmResult',
        'NotifyPcmStarted',
        'PcmStopProbe',
        'V1TransportNativePcmSource')) {
    if (-not $sharedWorker.Contains($required)) {
        throw "V1 PCM contained worker is missing: $required"
    }
}
foreach ($required in @(
        '--exercise-transport-pcm-burst',
        'transport-pcm-burst-exercise',
        'pcm_burst_sessions_completed',
        'V1LifecycleEvent::MediaStopped',
        'bounded PCM authorization')) {
    if (-not $agent.Contains($required)) {
        throw "V1 PCM agent mode is missing: $required"
    }
}
foreach ($required in @(
        'audible_PCM_before_Bluetooth_OPEN',
        'maximum_fixed_gain_0_01',
        'bounded_10000_ms_PCM_clock_pacing',
        'consumer_lease_release_required',
        'no_LinkState_write',
        'no_driver_install',
        'no_reboot')) {
    if (-not $build.Contains($required)) {
        throw "V1 PCM candidate contract is missing: $required"
    }
}
foreach ($required in @(
        'Test-V1PcmBurstEvidence',
        'maximum_gain_scalar',
        'target_duration_ms -eq $ExpectedDurationMs',
        'consumer_lease_released -eq $true',
        'media_started_events -eq 1')) {
    if (-not $common.Contains($required)) {
        throw "V1 PCM evidence is missing: $required"
    }
}
foreach ($required in @(
        'transaction-20260725-111708-750.json',
        '--exercise-transport-pcm-burst',
        'v1_transport_pcm_worker.exe',
        'fixed -40 dB ceiling',
        'PCM consumer lease released: generation 0',
        'no install or reboot is required')) {
    if (-not $gate.Contains($required)) {
        throw "V1 PCM hardware gate is missing: $required"
    }
}
foreach ($forbidden in @(
        'pnputil',
        'devcon',
        'Restart-Computer',
        'Disable-PnpDevice',
        'Enable-PnpDevice',
        'Stop-Service',
        'Start-Service',
        'SetDefaultEndpoint')) {
    if ($gate.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "V1 PCM runtime gate mutates the baseline: $forbidden"
    }
}
foreach ($required in @(
        'QuietPcmNeverOpensBluetooth',
        'RetryableOpenFailureReleasesConsumer',
        'WriteFailureStopsLocally',
        'GracefulStopSuspendsAndCloses',
        'CancelStopsWithoutBlindSignaling',
        'InvalidGainNeverPrepares',
        'InvalidOutputPeakNeverPrepares',
        'InvalidPreflightBoundNeverPrepares',
        'InvalidLongProfileBoundsNeverPrepare',
        'LongProfileBoundsAreAccepted',
        'PreflightReacquiresAcrossEpoch',
        'AudibleProfileIsStillHardBounded',
        'UnityGainPreservesLowInput',
        'UnityGainLimiterCapsFullScaleAndSanitizesNonFinite')) {
    if (-not $tests.Contains($required)) {
        throw "V1 PCM fault coverage is missing: $required"
    }
}
$pcmTarget = [regex]::Match(
    $cmake,
    '(?s)add_executable\(v1_transport_pcm_worker.*?(?=add_executable\(v1_transport_pcm_mock_worker)').Value
if ([string]::IsNullOrWhiteSpace($pcmTarget) -or
    -not $pcmTarget.Contains('v1_transport_pcm_source_adapter') -or
    -not $pcmTarget.Contains('v1_transport_silence_driver_backend')) {
    throw 'The production PCM worker target lacks its bounded source/transport adapters.'
}

Write-Host 'V1 bounded low-gain PCM transport policy tests passed.'
