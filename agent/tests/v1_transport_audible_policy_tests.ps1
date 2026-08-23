# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    Get-Content -LiteralPath (Join-Path $root $RelativePath) -Raw
}
$wrapper = Read-ProjectFile 'agent\v1_transport_audible_worker.cpp'
$shared = Read-ProjectFile 'agent\v1_transport_configuration_worker.cpp'
$core = Read-ProjectFile 'agent\v1_transport_pcm_session.cpp'
$adapter = Read-ProjectFile 'agent\v1_transport_pcm_source_adapter.cpp'
$agent = Read-ProjectFile 'agent\v1_presence_agent.cpp'
$common = Read-ProjectFile 'tools\v1-audible-burst-common.ps1'
$build = Read-ProjectFile 'tools\build-v1-audible-burst-candidate.ps1'
$gate = Read-ProjectFile 'tools\run-v1-audible-burst-gate.ps1'
$cmake = Read-ProjectFile 'CMakeLists.txt'

foreach ($required in @(
        '#define V1_TRANSPORT_PCM_WORKER 1',
        '#define V1_TRANSPORT_PCM_DURATION_MS 5000u',
        '#define V1_TRANSPORT_PCM_MAXIMUM_GAIN_SCALAR 0.25f',
        '#define V1_TRANSPORT_PCM_MAXIMUM_OUTPUT_PEAK 0.25f',
        '#define V1_TRANSPORT_PCM_AUDIBLE_PREFLIGHT_TIMEOUT_MS 120000u')) {
    if (-not $wrapper.Contains($required)) {
        throw "The audible worker profile is missing: $required"
    }
}
foreach ($required in @(
        'V1_TRANSPORT_PCM_DURATION_MS',
        'V1_TRANSPORT_PCM_MAXIMUM_GAIN_SCALAR',
        'V1_TRANSPORT_PCM_AUDIBLE_PREFLIGHT_TIMEOUT_MS')) {
    if (-not $shared.Contains($required)) {
        throw "The shared PCM worker does not consume: $required"
    }
}
if (-not $core.Contains('kHardMaximumGainScalar = 1.0f') -or
    -not $core.Contains('kHardMaximumOutputPeak = 0.25f') -or
    -not $core.Contains('options.maximum_gain_scalar > kHardMaximumGainScalar') -or
    -not $core.Contains(
        'kMaximumAudiblePreflightTimeoutMs = 120000u')) {
    throw 'The PCM core does not enforce the policy v6 hard gain bound.'
}
foreach ($required in @(
        'if (snapshot.stream_active &&',
        'snapshot.sample_rate_hz == requested_sample_rate_hz_',
        'WaitForMultipleObjects',
        'StoreError(WAIT_TIMEOUT, error)')) {
    if (-not $adapter.Contains($required)) {
        throw "The PCM source does not tolerate the transient RUN race: $required"
    }
}
if (-not $agent.Contains(
        'transport attempt ended with bounded') -or
    $agent.Contains('ERROR_CONNECTION_ABORTED')) {
    throw 'The parent agent does not preserve bounded worker failure evidence.'
}
foreach ($required in @(
        'transport_policy_version -ne 6',
        'maximum_fixed_gain_0_25',
        'wait_for_active_WaveRT_before_consumer_lease',
        'bounded_120000_ms_pretransport_PCM_wait',
        'bounded_5000_ms_PCM_clock_pacing',
        'maximum_four_zero_exchange_open_attempts',
        'v1_transport_audible_worker.exe')) {
    if (-not (($common + $build).Contains($required))) {
        throw "The audible candidate contract is missing: $required"
    }
}
foreach ($required in @(
        'transaction-20260725-122007-581.json',
        'ValidateRange(240,300)',
        'ExpectedDurationMs 5000',
        'ExpectedMaximumGain 0.25',
        'MaximumAttempts 4',
        '15/30/45-second backoff',
        'remains armed for up to 120 seconds',
        'tolerates an initial idle WaveRT transition',
        'No active WaveRT RUN appeared',
        'Bluetooth OPEN and media packets remained zero',
        'audibility_observation=''user-report-required''',
        'does not infer acoustic success',
        'no install or reboot is required')) {
    if (-not $gate.Contains($required)) {
        throw "The audible hardware gate is missing: $required"
    }
}
foreach ($forbidden in @(
        'pnputil','devcon','Restart-Computer','Disable-PnpDevice',
        'Enable-PnpDevice','Stop-Service','Start-Service','SetDefaultEndpoint')) {
    if ($gate.IndexOf($forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The audible runtime gate mutates the baseline: $forbidden"
    }
}
$target = [regex]::Match($cmake,
    '(?s)add_executable\(v1_transport_audible_worker.*?(?=add_executable\(v1_transport_pcm_mock_worker)').Value
if ([string]::IsNullOrWhiteSpace($target) -or
    -not $target.Contains('v1_transport_pcm_source_adapter') -or
    -not $target.Contains('v1_transport_silence_driver_backend')) {
    throw 'The audible worker target lacks its PCM/transport adapters.'
}
Write-Host 'V1 five-second audibility policy tests passed.'
