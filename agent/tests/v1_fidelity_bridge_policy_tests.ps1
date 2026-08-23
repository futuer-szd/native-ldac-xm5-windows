# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    Get-Content -LiteralPath (Join-Path $root $RelativePath) -Raw
}
$common = Read-ProjectFile 'tools\v1-fidelity-bridge-common.ps1'
$build = Read-ProjectFile 'tools\build-v1-fidelity-bridge-candidate.ps1'
$gate = Read-ProjectFile 'tools\run-v1-fidelity-bridge-gate.ps1'
$worker = Read-ProjectFile 'agent\v1_transport_fidelity_bridge_worker.cpp'
$pcm = Read-ProjectFile 'agent\v1_transport_pcm_session.cpp'
$shared = Read-ProjectFile 'agent\v1_transport_configuration_worker.cpp'
$hostSource = Read-ProjectFile 'agent\v1_engine_ready_host.cpp'
$cmake = Read-ProjectFile 'CMakeLists.txt'

foreach ($required in @(
        '#define V1_TRANSPORT_PCM_DURATION_MS 10000u',
        '#define V1_TRANSPORT_PCM_MAXIMUM_GAIN_SCALAR 1.0f',
        '#define V1_TRANSPORT_PCM_MAXIMUM_OUTPUT_PEAK 0.89125094f',
        '#define V1_TRANSPORT_PCM_SAMPLE_PEAK_FIDELITY 1',
        '#define V1_TRANSPORT_PCM_REQUIRE_STABLE_VOLUME 1',
        '#define V1_TRANSPORT_PCM_FADE_IN_MS 100.0f',
        '#define V1_TRANSPORT_PCM_CEILING_RAMP_START 0.25f',
        '#define V1_TRANSPORT_PCM_CEILING_RAMP_MS 2000.0f')) {
    if (-not $worker.Contains($required)) {
        throw "The fidelity-bridge worker profile is missing: $required"
    }
}
foreach ($required in @(
        'LinkedStereoSamplePeakFidelity',
        'CheckStableVolume',
        'PrepareV1SentFrameFadeBlock',
        'committed_fade_state',
        'CommitV1SentFrameFadeBlock',
        'pending_fade_frames',
        'CeilingForSentFrame',
        'fade_committed_sent_frames != result.pcm_frames_sent')) {
    if (-not $pcm.Contains($required)) {
        throw "The fidelity-bridge PCM safety chain is missing: $required"
    }
}
foreach ($required in @(
        '--session-generation',
        'run_options.session_generation = options.session_generation',
        'V1_TRANSPORT_PCM_REQUIRE_STABLE_VOLUME',
        'V1_TRANSPORT_PCM_SAMPLE_PEAK_FIDELITY')) {
    if (-not $shared.Contains($required)) {
        throw "The fidelity worker generation/profile plumbing is missing: $required"
    }
}
if (-not $hostSource.Contains('L" --session-generation "') -or
    -not $hostSource.Contains('std::to_wstring(generation)')) {
    throw 'The contained worker does not receive its ACL generation.'
}
if (-not $cmake.Contains(
        'add_executable(v1_transport_fidelity_bridge_worker')) {
    throw 'The fidelity-bridge CMake target is missing.'
}

foreach ($required in @(
        'transaction-20260726-154646-723.json',
        'transport_policy_version -ne 10',
        'transport-verified-quality-not-assessed',
        'not-assessed-by-user',
        'linked-stereo-sample-peak',
        'sent-frame-linear-fade',
        'output_chain_version',
        'digital-sample-peak',
        'peak_unit -ne ''dBFS''',
        'sample_peak_dbfs',
        '0.89125094',
        'maximum_gain_scalar - 1.0',
        'target_duration_ms -ne 10000',
        'fade_in_ms',
        '100.0',
        'ceiling_ramp_start',
        'ceiling_ramp_ms',
        '2000.0',
        'maximum_transport_open_attempts -ne 4',
        'dynamic_volume_mute_format_epoch_lock',
        'retry_only_OpenSignaling_Win32_71_zero_exchange',
        'v1_transport_fidelity_bridge_worker.exe')) {
    if (-not (($common + $build + $gate).Contains($required))) {
        throw "The fidelity-bridge policy contract is missing: $required"
    }
}
foreach ($required in @(
        '--target v1_transport_fidelity_bridge_worker v1_presence_agent',
        'Policy v9 was used only as frozen evidence and was not re-run.',
        'not true-peak',
        'must never be reported as dBTP')) {
    if (-not $build.Contains($required)) {
        throw "The fidelity-bridge candidate contract is missing: $required"
    }
}
foreach ($required in @(
        '[ValidateRange(240,360)][int]$DurationSeconds = 300',
        'Test-V1FidelityBridgePrerequisite',
        'Test-V1FidelityBridgeEvidence',
        '--exercise-transport-pcm-burst',
        'ten seconds after START',
        'session_generation',
        'acl_generation',
        'volume_query_count',
        'volume_change_count',
        'fade_committed_sent_frames',
        'fade_blocks_prepared',
        'fade_blocks_committed',
        'fade_commit_failures',
        'ceiling_ramp_last',
        'limiter_fallback_clamp_count',
        'not true-peak/dBTP',
        'No reboot or rollback is required.')) {
    if (-not $gate.Contains($required)) {
        throw "The fidelity-bridge hardware gate is missing: $required"
    }
}
foreach ($required in @(
        'session_generation -eq [int64]$State.acl_generation',
        'volume_change_count -eq 0',
        'volume_stable -eq $true',
        'fade_committed_sent_frames -eq',
        'pcm_frames_sent',
        'fade_blocks_prepared -eq',
        'fade_blocks_committed',
        'fade_commit_failures -eq 0',
        'ceiling_ramp_last -',
        'limiter_fallback_clamp_count -eq 0')) {
    if (-not $common.Contains($required)) {
        throw "The fidelity-bridge evidence lock is missing: $required"
    }
}
foreach ($forbidden in @(
        'pnputil', 'devcon', 'Restart-Computer', 'Disable-PnpDevice',
        'Enable-PnpDevice', 'Stop-Service', 'Start-Service',
        'SetDefaultEndpoint')) {
    if ($gate.IndexOf(
            $forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The fidelity-bridge gate mutates the baseline: $forbidden"
    }
}
foreach ($relative in @(
        'tools\v1-fidelity-bridge-common.ps1',
        'tools\build-v1-fidelity-bridge-candidate.ps1',
        'tools\run-v1-fidelity-bridge-gate.ps1')) {
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $root $relative), [ref]$tokens, [ref]$errors)
    if (@($errors).Count -ne 0) {
        throw "The fidelity-bridge PowerShell file does not parse: $relative"
    }
}
Write-Host 'V1 fidelity-bridge PowerShell policy tests passed.'
