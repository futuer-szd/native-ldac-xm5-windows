# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    Get-Content -LiteralPath (Join-Path $root $RelativePath) -Raw
}
$wrapper = Read-ProjectFile 'agent\v1_transport_stability_worker.cpp'
$shared = Read-ProjectFile 'agent\v1_transport_configuration_worker.cpp'
$core = Read-ProjectFile 'agent\v1_transport_pcm_session.cpp'
$common = Read-ProjectFile 'tools\v1-stability-burst-common.ps1'
$build = Read-ProjectFile 'tools\build-v1-stability-burst-candidate.ps1'
$gate = Read-ProjectFile 'tools\run-v1-stability-burst-gate.ps1'
$cmake = Read-ProjectFile 'CMakeLists.txt'

foreach ($required in @(
        '#define V1_TRANSPORT_PCM_WORKER 1',
        '#define V1_TRANSPORT_PCM_DURATION_MS 60000u',
        '#define V1_TRANSPORT_PCM_MAXIMUM_GAIN_SCALAR 1.0f',
        '#define V1_TRANSPORT_PCM_MAXIMUM_OUTPUT_PEAK 0.25f',
        '#define V1_TRANSPORT_PCM_MAXIMUM_PACKETS 32768u',
        '#define V1_TRANSPORT_PCM_AUDIBLE_PREFLIGHT_TIMEOUT_MS 120000u')) {
    if (-not $wrapper.Contains($required)) {
        throw "The stability worker profile is missing: $required"
    }
}
foreach ($required in @(
        'V1_TRANSPORT_PCM_MAXIMUM_PACKETS',
        'run_options.maximum_packets',
        'maximum_unlimited_post_gain_peak',
        'limited_output_samples')) {
    if (-not $shared.Contains($required)) {
        throw "The shared PCM worker does not consume/report: $required"
    }
}
foreach ($required in @(
        'kMaximumDurationMs = 60000u',
        'kMaximumPacketCount = 32768u',
        'kHardMaximumOutputPeak = 0.25f',
        'maximum_unlimited_post_gain_peak',
        'limited_output_samples')) {
    if (-not $core.Contains($required)) {
        throw "The PCM core lacks its stability bound/telemetry: $required"
    }
}
foreach ($required in @(
        'transport_policy_version -ne 8',
        'verified_policy_v7_recognizable_audio_prerequisite',
        'bounded_60000_ms_PCM_clock_pacing',
        'limiter_engagement_telemetry',
        'v1_transport_stability_worker.exe')) {
    if (-not (($common + $build).Contains($required))) {
        throw "The stability candidate contract is missing: $required"
    }
}
foreach ($required in @(
        'transaction-20260725-222235-095.json',
        'verified-recognizable-audio',
        'user-reported-recognizable-audio',
        'ValidateRange(300,420)',
        'ExpectedDurationMs 60000',
        'ExpectedMaximumPackets 32768',
        'RequireLimiterTelemetry',
        'RequireEpochReacquireTelemetry',
        'pcm_epoch_restarts',
        'consumer_lease_acquire_count',
        'maximum_unlimited_post_gain_peak',
        'limited_output_samples',
        'listen for the full sixty seconds',
        'does not prove acoustic quality',
        'no install or reboot is required')) {
    if (-not $gate.Contains($required)) {
        throw "The stability hardware gate is missing: $required"
    }
}
foreach ($forbidden in @(
        'pnputil','devcon','Restart-Computer','Disable-PnpDevice',
        'Enable-PnpDevice','Stop-Service','Start-Service','SetDefaultEndpoint')) {
    if ($gate.IndexOf($forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The stability runtime gate mutates the baseline: $forbidden"
    }
}
$target = [regex]::Match($cmake,
    '(?s)add_executable\(v1_transport_stability_worker.*?(?=add_executable\(v1_transport_pcm_mock_worker)').Value
if ([string]::IsNullOrWhiteSpace($target) -or
    -not $target.Contains('v1_transport_pcm_source_adapter') -or
    -not $target.Contains('v1_transport_silence_driver_backend')) {
    throw 'The stability worker target lacks its PCM/transport adapters.'
}
Write-Host 'V1 sixty-second stability policy tests passed.'
