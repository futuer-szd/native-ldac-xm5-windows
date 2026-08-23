# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    Get-Content -LiteralPath (Join-Path $root $RelativePath) -Raw
}
$wrapper = Read-ProjectFile 'agent\v1_transport_recognizable_worker.cpp'
$shared = Read-ProjectFile 'agent\v1_transport_configuration_worker.cpp'
$core = Read-ProjectFile 'agent\v1_transport_pcm_session.cpp'
$common = Read-ProjectFile 'tools\v1-recognizable-burst-common.ps1'
$build = Read-ProjectFile 'tools\build-v1-recognizable-burst-candidate.ps1'
$gate = Read-ProjectFile 'tools\run-v1-recognizable-burst-gate.ps1'
$cmake = Read-ProjectFile 'CMakeLists.txt'

foreach ($required in @(
        '#define V1_TRANSPORT_PCM_WORKER 1',
        '#define V1_TRANSPORT_PCM_DURATION_MS 5000u',
        '#define V1_TRANSPORT_PCM_MAXIMUM_GAIN_SCALAR 1.0f',
        '#define V1_TRANSPORT_PCM_MAXIMUM_OUTPUT_PEAK 0.25f',
        '#define V1_TRANSPORT_PCM_AUDIBLE_PREFLIGHT_TIMEOUT_MS 120000u')) {
    if (-not $wrapper.Contains($required)) {
        throw "The recognizable worker profile is missing: $required"
    }
}
foreach ($required in @(
        'V1_TRANSPORT_PCM_MAXIMUM_OUTPUT_PEAK',
        'run_options.maximum_output_peak',
        'maximum_output_peak_ceiling')) {
    if (-not $shared.Contains($required)) {
        throw "The shared PCM worker does not consume/report: $required"
    }
}
foreach ($required in @(
        'kHardMaximumGainScalar = 1.0f',
        'kHardMaximumOutputPeak = 0.25f',
        'options.maximum_output_peak > kHardMaximumOutputPeak',
        '-output_peak_ceiling',
        'output_peak_ceiling);')) {
    if (-not $core.Contains($required)) {
        throw "The PCM core does not enforce the independent limiter: $required"
    }
}
foreach ($required in @(
        'transport_policy_version -ne 7',
        'verified_policy_v6_faint_audibility_prerequisite',
        'unity_post_volume_gain',
        'independent_hard_limiter_0_25',
        'v1_transport_recognizable_worker.exe')) {
    if (-not (($common + $build).Contains($required))) {
        throw "The recognizable candidate contract is missing: $required"
    }
}
foreach ($required in @(
        'transaction-20260725-155634-355.json',
        'verified-faint-audibility',
        'user-reported-faint-audio',
        'ValidateRange(240,300)',
        'ExpectedDurationMs 5000',
        'ExpectedMaximumGain 1.0',
        'ExpectedMaximumOutputPeak 0.25',
        'RequireOutputPeakField',
        'RequirePretransportRenderGapTolerance',
        'MaximumAttempts 4',
        '(?m)^Stream idle[:,]',
        '15/30/45-second backoff',
        'up to 120 seconds',
        'Pre-START render gaps',
        'No active WaveRT RUN appeared',
        'audibility_observation=''user-report-required''',
        'Packet delivery alone does not prove acoustic quality',
        'no install or reboot is required')) {
    if (-not $gate.Contains($required)) {
        throw "The recognizable hardware gate is missing: $required"
    }
}
foreach ($forbidden in @(
        'pnputil','devcon','Restart-Computer','Disable-PnpDevice',
        'Enable-PnpDevice','Stop-Service','Start-Service','SetDefaultEndpoint')) {
    if ($gate.IndexOf($forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The recognizable runtime gate mutates the baseline: $forbidden"
    }
}
$target = [regex]::Match($cmake,
    '(?s)add_executable\(v1_transport_recognizable_worker.*?(?=add_executable\(v1_transport_pcm_mock_worker)').Value
if ([string]::IsNullOrWhiteSpace($target) -or
    -not $target.Contains('v1_transport_pcm_source_adapter') -or
    -not $target.Contains('v1_transport_silence_driver_backend')) {
    throw 'The recognizable worker target lacks its PCM/transport adapters.'
}
Write-Host 'V1 recognizable-audio policy tests passed.'
