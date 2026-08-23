# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1AvrcpFilterResidentInstall,
    [string]$CandidatePath,
    [string]$StatePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-avrcp-filter-gate-common.ps1')

Assert-V1AvrcpFilterAdministrator
if (-not $ConfirmV1AvrcpFilterResidentInstall) {
    throw 'Refusing resident filter install. Re-run with the confirmation switch.'
}
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $projectRoot `
        'artifacts\v1-volume-sync\avrcp-filter-candidate'
}
if ([string]::IsNullOrWhiteSpace($StatePath)) {
    $StatePath = Join-Path $projectRoot `
        'artifacts\v1-volume-sync\filter-gate\install-state.json'
}
$CandidatePath = [IO.Path]::GetFullPath($CandidatePath)
$StatePath = [IO.Path]::GetFullPath($StatePath)
& (Join-Path $PSScriptRoot 'verify-v1-avrcp-filter-candidate.ps1') `
    -CandidatePath $CandidatePath
$manifest = Get-Content -LiteralPath `
    (Join-Path $CandidatePath 'manifest.json') -Raw | ConvertFrom-Json
if ([int]$manifest.policy_version -ne 7) {
    throw 'The resident install requires the volume-write policy 7 filter.'
}
$connectionProbe = Join-Path $CandidatePath `
    'tools\xm5_connection_probe.exe'
$connectionState = @(& $connectionProbe --state 2>&1) -join "`n"
if ($connectionState -notmatch 'disconnected') {
    throw 'XM5 must be off and disconnected before the resident filter install.'
}
if (@(Get-V1AvrcpFilterPackages).Count -ne 0) {
    throw 'A Native AVRCP filter package is already installed.'
}
$target = Get-V1AvrcpFilterTargetDevice
$baseline = Get-V1AvrcpFilterSnapshot -Device $target
if (-not (Test-V1AvrcpFilterMicrosoftBaseline -Snapshot $baseline)) {
    throw 'The exact XM5 AVRCP PDO is not a healthy Microsoft baseline.'
}
$candidateInf = Join-Path $CandidatePath `
    'package\NativeLdacAvrcpIoFilter.inf'
$probePath = Join-Path $CandidatePath `
    'tools\v1_avrcp_filter_probe.exe'
if (-not $PSCmdlet.ShouldProcess(
        'the exact XM5 AVRCP 0x110E PDO',
        'Install the Microsoft-preserving volume upper filter and restart that PDO once')) {
    return
}
$add = Invoke-V1AvrcpFilterPnpUtil -Arguments @(
    '/add-driver', $candidateInf, '/install')
if ($add.exit_code -notin @(0, 259, 3010)) {
    throw "Filter package install failed (exit $($add.exit_code))."
}
$publishedInf = Get-V1AvrcpFilterPublishedInfFromOutput -Lines $add.lines
if (-not (Test-V1AvrcpFilterPublishedInfMatchesCandidate `
        -PublishedInf $publishedInf `
        -CandidateInfPath $candidateInf)) {
    throw 'The published filter INF does not match the candidate.'
}
$restart = Invoke-V1AvrcpFilterPnpUtil -Arguments @(
    '/restart-device', $baseline.instance_id)
if ($restart.exit_code -ne 0) {
    throw "Exact AVRCP PDO restart failed (exit $($restart.exit_code))."
}
$final = Wait-V1AvrcpFilterMicrosoftBaseline `
    -InstanceId $baseline.instance_id -TimeoutSeconds 45
if ($null -eq $final) {
    throw 'Microsoft AVRCP did not return healthy after filter installation.'
}
Start-Sleep -Seconds 2
$control = Test-V1AvrcpFilterControlAbsent -ProbePath $probePath
if (-not $control.healthy) {
    throw 'The resident filter control device is not healthy.'
}
$state = [ordered]@{
    state_version = 2
    installed_at = (Get-Date).ToString('o')
    resident = $true
    candidate_path = $CandidatePath
    published_inf = $publishedInf
    exact_pdo_restart_count = 1
    baseline = $baseline
    final_target = $final
    policy_version = [int]$manifest.policy_version
    source_commit = [string]$manifest.source_commit
    source_dirty = [bool]$manifest.source_dirty
}
Write-V1AvrcpFilterJsonAtomically -Path $StatePath -Value $state
Write-Host 'V1 AVRCP resident volume filter installed.'
Write-Host 'Microsoft AVRCP remains the function driver.'
Write-Host "State: $StatePath"
