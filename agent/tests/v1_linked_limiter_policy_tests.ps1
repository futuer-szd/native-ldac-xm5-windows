# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    Get-Content -LiteralPath (Join-Path $root $RelativePath) -Raw
}
$common = Read-ProjectFile 'tools\v1-linked-limiter-common.ps1'
$build = Read-ProjectFile 'tools\build-v1-linked-limiter-candidate.ps1'
$gate = Read-ProjectFile 'tools\run-v1-linked-limiter-gate.ps1'

foreach ($required in @(
        'transport_policy_version -ne 9',
        'linked-stereo-block',
        'limiter_algorithm_version',
        'limiter_release_ms',
        'maximum_gain_scalar - 1.0',
        'maximum_output_peak - 0.25',
        'target_duration_ms -ne 60000',
        'maximum_transport_open_attempts -ne 4',
        'verified_policy_v8_generally_clear_muffled_bass_prerequisite',
        'retry_only_OpenSignaling_Win32_71_zero_exchange',
        'limiter_minimum_gain',
        'limiter_gain_reduced_frames',
        'limiter_gain_reduced_samples',
        'limiter_fallback_clamp_count')) {
    if (-not (($common + $build + $gate).Contains($required))) {
        throw "The linked-limiter policy contract is missing: $required"
    }
}
foreach ($required in @(
        'transaction-20260726-144127-295.json',
        'stability-verified-user-report',
        'user-reported-generally-clear-with-muffled-bass',
        'generally-clear',
        'muffled-bass',
        'Policy v8 was used only as frozen evidence; it was not re-run.',
        '--target v1_transport_linked_limiter_worker v1_presence_agent',
        'v1_transport_linked_limiter_worker.exe')) {
    if (-not (($common + $build).Contains($required))) {
        throw "The linked-limiter candidate prerequisite is missing: $required"
    }
}
foreach ($required in @(
        '[ValidateRange(300,420)][int]$DurationSeconds = 360',
        'Test-V1LinkedLimiterPrerequisite',
        'Test-V1LinkedLimiterEvidence',
        '--exercise-transport-pcm-burst',
        'quality_comparison_observation = ''user-report-required''',
        'transport_policy_version = 9',
        'maximum_output_peak_ceiling',
        'No reboot or rollback is required.')) {
    if (-not $gate.Contains($required)) {
        throw "The linked-limiter hardware gate is missing: $required"
    }
}
foreach ($forbidden in @(
        'pnputil', 'devcon', 'Restart-Computer', 'Disable-PnpDevice',
        'Enable-PnpDevice', 'Stop-Service', 'Start-Service',
        'SetDefaultEndpoint')) {
    if ($gate.IndexOf(
            $forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The linked-limiter gate mutates the baseline: $forbidden"
    }
}
foreach ($relative in @(
        'tools\v1-linked-limiter-common.ps1',
        'tools\build-v1-linked-limiter-candidate.ps1',
        'tools\run-v1-linked-limiter-gate.ps1')) {
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $root $relative), [ref]$tokens, [ref]$errors)
    if (@($errors).Count -ne 0) {
        throw "The linked-limiter PowerShell file does not parse: $relative"
    }
}
Write-Host 'V1 linked-limiter PowerShell policy tests passed.'
