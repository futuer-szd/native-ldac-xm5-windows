# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$CandidatePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The V1 AVRCP observer verifier requires PowerShell 7.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $projectRoot `
        'artifacts\v1-volume-sync\avrcp-observer-candidate'
} else {
    $CandidatePath = $ExecutionContext.SessionState.Path.
        GetUnresolvedProviderPathFromPSPath($CandidatePath)
}
$CandidatePath = [IO.Path]::GetFullPath($CandidatePath)
$manifestPath = Join-Path $CandidatePath 'manifest.json'
$hashPath = Join-Path $CandidatePath 'manifest.sha256'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $hashPath -PathType Leaf)) {
    throw 'The AVRCP candidate manifest or detached hash is missing.'
}
$expectedHash = ((Get-Content -LiteralPath $hashPath -Raw).Trim() `
    -split '\s+')[0]
$actualHash = (Get-FileHash -LiteralPath $manifestPath `
    -Algorithm SHA256).Hash
if ($actualHash -cne $expectedHash) {
    throw 'The AVRCP candidate manifest hash does not match.'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw |
    ConvertFrom-Json
if ([int]$manifest.manifest_version -ne 1 -or
    [int]$manifest.policy_version -ne 3 -or
    [bool]$manifest.observe_only -or
    [string]$manifest.public_ioctl_access -cne 'read-write' -or
    [int]$manifest.avctp_control_psm -ne 0x0017 -or
    [string]$manifest.avctp_control_direction -cne 'outbound' -or
    [bool]$manifest.fixed_psm_listener -or
    -not [bool]$manifest.outbound_open -or
    [int]$manifest.outbound_open_attempts -ne 1 -or
    [int]$manifest.abi_minor -ne 11 -or
    -not [bool]$manifest.avdtp_capability_hold_prerequisite -or
    -not [bool]$manifest.avdtp_media_session_prerequisite -or
    [string]$manifest.observation_activation -cne
        'explicit read-only BEGIN_OBSERVATION after the media session is ready' -or
    [string]$manifest.profile_acquisition -cne
        'deferred to the active observation session and current physical ACL' -or
    [string]$manifest.expected_previous_inf -cne `
        'microsoft_bluetooth_avrcptransport.inf' -or
    [string]$manifest.expected_previous_service -cne `
        'Microsoft_Bluetooth_AvrcpTransport') {
    throw 'The AVRCP candidate contract is unsupported.'
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
$infPath = Join-Path $CandidatePath `
    'package\NativeLdacAvrcpObserver.inf'
$sysPath = Join-Path $CandidatePath `
    'package\NativeLdacAvrcpObserver.sys'
$catPath = Get-ChildItem -LiteralPath (Join-Path $CandidatePath 'package') `
    -Filter '*.cat' -File | Select-Object -First 1
$probePath = Join-Path $CandidatePath `
    'tools\v1_avrcp_observer_probe.exe'
$transportProbePath = Join-Path $CandidatePath `
    'tools\transport_probe.exe'
if (-not (Test-Path -LiteralPath $infPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $sysPath -PathType Leaf) -or
    -not $catPath -or
    -not (Test-Path -LiteralPath $probePath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $transportProbePath -PathType Leaf)) {
    throw 'The AVRCP candidate package is incomplete.'
}
$inf = Get-Content -LiteralPath $infPath -Raw
if ($inf -notmatch [regex]::Escape(
        'BTHENUM\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0') -or
    $inf -match '0000110B|ROOT\\MEDIA|NativeLdacAudio') {
    throw 'The staged INF target is not isolated to XM5 AVRCP 0x110E.'
}

Write-Host "V1 AVRCP observer candidate verified: $CandidatePath"
Write-Host "Source: $($manifest.source_commit)"
Write-Host 'The candidate remains uninstalled; activation acquires a fresh current-ACL BTH profile after the media session is ready, and keeps the authorized AVRCP write surface unchanged.'
