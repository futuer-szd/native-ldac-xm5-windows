# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$CandidatePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The V1 AVRCP filter verifier requires PowerShell 7.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $projectRoot `
        'artifacts\v1-volume-sync\avrcp-filter-candidate'
}
$CandidatePath = [IO.Path]::GetFullPath($CandidatePath)
$manifestPath = Join-Path $CandidatePath 'manifest.json'
$hashPath = Join-Path $CandidatePath 'manifest.sha256'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $hashPath -PathType Leaf)) {
    throw 'The AVRCP filter candidate manifest or detached hash is missing.'
}
$expectedHash = ((Get-Content -LiteralPath $hashPath -Raw).Trim() `
    -split '\s+')[0]
$actualHash = (Get-FileHash -LiteralPath $manifestPath `
    -Algorithm SHA256).Hash
if ($actualHash -cne $expectedHash) {
    throw 'The AVRCP filter candidate manifest hash does not match.'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw |
    ConvertFrom-Json
$policyVersion = [int]$manifest.policy_version
if ([int]$manifest.manifest_version -ne 1 -or
    $policyVersion -notin @(2, 3, 4, 5, 6, 7) -or
    -not [bool]$manifest.extension_inf -or
    [string]$manifest.filter_position -cne 'upper' -or
    [string]$manifest.filter_service -cne 'NativeLdacAvrcpIoFilter' -or
    [string]$manifest.expected_function_inf -cne `
        'microsoft_bluetooth_avrcptransport.inf' -or
    [string]$manifest.expected_function_service -cne `
        'Microsoft_Bluetooth_AvrcpTransport' -or
    (($policyVersion -lt 7 -and
      [string]$manifest.public_ioctl_access -cne 'read-only') -or
     ($policyVersion -eq 7 -and
      [string]$manifest.public_ioctl_access -cne 'read-write-volume-only')) -or
    [int]$manifest.abi_major -ne 0 -or
    (($policyVersion -lt 7 -and [int]$manifest.abi_minor -ne 1) -or
     ($policyVersion -eq 7 -and [int]$manifest.abi_minor -ne 2)) -or
    (($policyVersion -lt 7 -and -not [bool]$manifest.observe_only) -or
     ($policyVersion -eq 7 -and [bool]$manifest.observe_only)) -or
    -not [bool]$manifest.pass_through_unknown_requests -or
    [int]$manifest.raw_prefix_bytes -ne 32 -or
    (@($manifest.raw_prefix_methods) -join ',') -cne
        'METHOD_BUFFERED,METHOD_IN_DIRECT,METHOD_OUT_DIRECT' -or
    [bool]$manifest.method_neither_raw_capture -or
    -not [bool]$manifest.first_request_arming -or
    -not [bool]$manifest.acl_probe_included -or
    [bool]$manifest.private_bluetooth_imports -or
    [bool]$manifest.function_driver_replacement -or
    [bool]$manifest.class_filter -or
    [bool]$manifest.installation_performed) {
    throw 'The AVRCP filter candidate contract is unsupported.'
}
if ($policyVersion -eq 3 -and (
    -not [bool]$manifest.first_request_timeout_publishes_window_status -or
    [string]$manifest.no_post_connect_request_failure_code -cne `
        'no-post-connect-filter-request' -or
    -not [bool]$manifest.filter_probe_stderr_in_result -or
    -not [bool]$manifest.install_requires_exact_pdo_restart -or
    [int]$manifest.maximum_exact_pdo_restarts -ne 1)) {
    throw 'The AVRCP filter policy 3 candidate contract is incomplete.'
}
if ($policyVersion -eq 4 -and (
    -not [bool]$manifest.first_request_timeout_publishes_window_status -or
    [string]$manifest.no_post_connect_request_failure_code -cne `
        'no-post-connect-filter-request' -or
    -not [bool]$manifest.filter_probe_stderr_in_result -or
    -not [bool]$manifest.install_requires_exact_pdo_restart -or
    [int]$manifest.maximum_exact_pdo_restarts -ne 1 -or
    -not [bool]$manifest.transport_probe_included -or
    -not [bool]$manifest.bounded_ldac_silence_media_prerequisite -or
    [bool]$manifest.audible_playback -or
    [bool]$manifest.probe_overrides_allowed -or
    -not [bool]$manifest.transport_info_preflight)) {
    throw 'The AVRCP filter policy 4 candidate contract is incomplete.'
}
if ($policyVersion -eq 5 -and (
    -not [bool]$manifest.first_request_timeout_publishes_window_status -or
    [string]$manifest.no_post_connect_request_failure_code -cne `
        'no-post-connect-filter-request' -or
    -not [bool]$manifest.filter_probe_stderr_in_result -or
    -not [bool]$manifest.install_requires_exact_pdo_restart -or
    [int]$manifest.maximum_exact_pdo_restarts -ne 1 -or
    -not [bool]$manifest.transport_probe_included -or
    -not [bool]$manifest.bounded_ldac_silence_media_prerequisite -or
    [bool]$manifest.audible_playback -or
    [bool]$manifest.probe_overrides_allowed -or
    -not [bool]$manifest.transport_info_preflight -or
    -not [bool]$manifest.filter_probe_starts_after_acl_connect -or
    -not [bool]$manifest.filter_probe_starts_after_silence_media_ready -or
    -not [bool]$manifest.filter_probe_pre_arm_queue_drain -or
    -not [bool]$manifest.gesture_prompt_after_filter_probe_armed -or
    -not [bool]$manifest.complete_line_live_forwarding -or
    -not [bool]$manifest.high_volume_trace_saved_without_terminal_flood)) {
    throw 'The AVRCP filter policy 5 candidate contract is incomplete.'
}
if ($policyVersion -eq 6 -and (
    -not [bool]$manifest.first_request_timeout_publishes_window_status -or
    [string]$manifest.no_post_connect_request_failure_code -cne `
        'no-post-connect-filter-request' -or
    -not [bool]$manifest.filter_probe_stderr_in_result -or
    -not [bool]$manifest.install_requires_exact_pdo_restart -or
    [int]$manifest.maximum_exact_pdo_restarts -ne 1 -or
    -not [bool]$manifest.transport_probe_included -or
    -not [bool]$manifest.bounded_ldac_silence_media_prerequisite -or
    [bool]$manifest.audible_playback -or
    [bool]$manifest.probe_overrides_allowed -or
    -not [bool]$manifest.transport_info_preflight -or
    -not [bool]$manifest.filter_probe_starts_after_acl_connect -or
    -not [bool]$manifest.filter_probe_starts_after_silence_media_ready -or
    -not [bool]$manifest.filter_probe_pre_arm_queue_drain -or
    -not [bool]$manifest.gesture_prompt_after_filter_probe_armed -or
    -not [bool]$manifest.complete_line_live_forwarding -or
    -not [bool]$manifest.high_volume_trace_saved_without_terminal_flood -or
    -not [bool]$manifest.decoded_volume_evidence_required -or
    -not [bool]$manifest.decoded_pass_through_evidence_required -or
    -not [bool]$manifest.decoded_summary_in_filter_probe)) {
    throw 'The AVRCP filter policy 6 candidate contract is incomplete.'
}
if ($policyVersion -eq 7 -and (
    [string]$manifest.allowed_write -cne
        'SetAbsoluteVolume only, AVRCP value 0..127' -or
    [string]$manifest.microsoft_private_write_layout -cne
        '8-byte header plus AVRCP SetAbsoluteVolume' -or
    [int]$manifest.write_timeout_ms -ne 2000)) {
    throw 'The AVRCP filter policy 7 write contract is incomplete.'
}
foreach ($file in @($manifest.files)) {
    $path = Join-Path $CandidatePath ([string]$file.path)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Candidate file is missing: $path"
    }
    $item = Get-Item -LiteralPath $path
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ([long]$item.Length -ne [long]$file.length -or
        $hash -cne [string]$file.sha256) {
        throw "Candidate file integrity failed: $path"
    }
}

$packageRoot = Join-Path $CandidatePath 'package'
$infPath = Join-Path $packageRoot 'NativeLdacAvrcpIoFilter.inf'
$sysPath = Join-Path $packageRoot 'NativeLdacAvrcpIoFilter.sys'
$certificatePath = Join-Path $packageRoot 'NativeLdacAvrcpIoFilter.cer'
$catPath = Get-ChildItem -LiteralPath $packageRoot `
    -Filter '*.cat' -File | Select-Object -First 1
$probePath = Join-Path $CandidatePath `
    'tools\v1_avrcp_filter_probe.exe'
$aclProbePath = Join-Path $CandidatePath `
    'tools\xm5_connection_probe.exe'
$transportProbePath = Join-Path $CandidatePath `
    'tools\transport_probe.exe'
$importsPath = Join-Path $CandidatePath 'driver-imports.txt'
if (-not (Test-Path -LiteralPath $infPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $sysPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $certificatePath -PathType Leaf) -or
    -not $catPath -or
    -not (Test-Path -LiteralPath $probePath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $aclProbePath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $importsPath -PathType Leaf)) {
    throw 'The AVRCP filter candidate package is incomplete.'
}
if ($policyVersion -ge 4 -and
    -not (Test-Path -LiteralPath $transportProbePath -PathType Leaf)) {
    throw 'The AVRCP filter media-ready candidate package is incomplete.'
}

$inf = Get-Content -LiteralPath $infPath -Raw
$targetId =
    'BTHENUM\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
if ($inf -notmatch '(?im)^Class\s*=\s*Extension\s*$' -or
    $inf -notmatch '(?im)^ExtensionId\s*=\s*\{[0-9A-F-]+\}\s*$' -or
    $inf -notmatch [regex]::Escape($targetId) -or
    $inf -notmatch '(?im)^AddFilter\s*=\s*NativeLdacAvrcpIoFilter,,FilterPosition\s*$' -or
    $inf -notmatch '(?im)^FilterPosition\s*=\s*Upper\s*$' -or
    $inf -match '(?i)UpperFilters|LowerFilters|ClassInstall32|0000110B|ROOT\\MEDIA') {
    throw 'The packaged INF is not an exact-XM5 device upper filter.'
}
$imports = Get-Content -LiteralPath $importsPath -Raw
if ($imports -match '(?i)btampm|BtaMpm|BthmpSetServiceStateEx|bthport') {
    throw 'The packaged filter imports a private Bluetooth/MPM dependency.'
}

Write-Host "V1 AVRCP upper-filter candidate verified: $CandidatePath"
Write-Host "Source: $($manifest.source_commit)"
Write-Host 'The package remains offline and preserves Microsoft AVRCP as the function driver.'
