# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [string]$StatePath,
    [switch]$ConfirmV1AvrcpFilterRollback,
    [switch]$AllowExactPdoRestart
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-avrcp-filter-gate-common.ps1')

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The V1 AVRCP filter rollback requires PowerShell 7.'
}
Assert-V1AvrcpFilterAdministrator
if (-not $ConfirmV1AvrcpFilterRollback) {
    throw 'Refusing filter rollback. Re-run with -ConfirmV1AvrcpFilterRollback.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($StatePath)) {
    $StatePath = Join-Path $projectRoot `
        'artifacts\v1-volume-sync\filter-gate\install-state.json'
}
$StatePath = [IO.Path]::GetFullPath($StatePath)
if (-not (Test-Path -LiteralPath $StatePath -PathType Leaf)) {
    throw "The filter gate state is missing: $StatePath"
}
$state = Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json
$publishedInf = Assert-V1AvrcpFilterPublishedInf `
    -PublishedInf ([string]$state.published_inf)
$instanceId = [string]$state.baseline.instance_id
$candidatePath = [IO.Path]::GetFullPath([string]$state.candidate_path)
$expectedCandidateRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot `
    'artifacts\v1-volume-sync')) + [IO.Path]::DirectorySeparatorChar
if (-not $candidatePath.StartsWith(
        $expectedCandidateRoot,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The filter gate state points outside the V1 volume-sync artifact root.'
}
& (Join-Path $PSScriptRoot 'verify-v1-avrcp-filter-candidate.ps1') `
    -CandidatePath $candidatePath
$probePath = Join-Path $candidatePath `
    'tools\v1_avrcp_filter_probe.exe'
$candidateInfPath = Join-Path $candidatePath `
    'package\NativeLdacAvrcpIoFilter.inf'
if (-not (Test-Path -LiteralPath $probePath -PathType Leaf)) {
    throw "The filter probe is missing: $probePath"
}
$matchingPackages = @(Get-V1AvrcpFilterPackages | Where-Object {
    $_.published_inf -ieq $publishedInf
})
if ($matchingPackages.Count -eq 1 -and
    -not (Test-V1AvrcpFilterPublishedInfMatchesCandidate `
        -PublishedInf $publishedInf `
        -CandidateInfPath $candidateInfPath)) {
    throw 'The managed Driver Store INF no longer matches the verified filter candidate.'
}
$target = Get-PnpDevice -InstanceId $instanceId -ErrorAction SilentlyContinue
if ($null -eq $target) {
    throw 'The exact XM5 AVRCP PDO from the install state is no longer present.'
}
$baseline = Get-V1AvrcpFilterSnapshot -Device $target
if ($baseline.inf -ine $script:V1AvrcpFilterBaselineInf -or
    $baseline.service -ine $script:V1AvrcpFilterBaselineService) {
    throw 'Rollback requires the exact PDO function owner to remain Microsoft AVRCP.'
}
$restartCount = [int]$state.exact_pdo_restart_count
if ($AllowExactPdoRestart -and $restartCount -ne 0) {
    throw 'The bounded filter gate has already used its one exact PDO restart.'
}

$stateRoot = Split-Path -Parent $StatePath
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$logDirectory = Join-Path $stateRoot "rollback-$timestamp"
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
if (-not $PSCmdlet.ShouldProcess(
        'the exact XM5 AVRCP 0x110E upper-filter package',
        'Remove the filter package, rescan the exact PDO, and verify Microsoft AVRCP')) {
    return
}

$rollback = Invoke-V1AvrcpFilterRollback `
    -PublishedInf $publishedInf `
    -InstanceId $instanceId `
    -ProbePath $probePath `
    -LogDirectory $logDirectory `
    -AllowExactPdoRestart ([bool]$AllowExactPdoRestart) `
    -ExistingExactPdoRestartCount $restartCount
$rollbackPath = Join-Path $stateRoot 'rollback-state.json'
$rollbackState = [ordered]@{
    state_version = 1
    completed_at = (Get-Date).ToString('o')
    passed = [bool]$rollback.passed
    published_inf = $publishedInf
    exact_pdo_restart_count = $restartCount + @($rollback.steps | Where-Object {
        $_.action -eq 'restart-exact-avrcp-pdo-for-rollback'
    }).Count
    baseline = $baseline
    rollback = $rollback
    install_state = $StatePath
}
Write-V1AvrcpFilterJsonAtomically -Path $rollbackPath -Value $rollbackState

if (-not [bool]$rollback.passed) {
    throw "V1 AVRCP filter rollback incomplete. Result: $rollbackPath"
}
Write-Host 'V1 AVRCP upper-filter rollback passed.'
Write-Host 'Microsoft AVRCP remains the exact PDO function driver; no filter package remains staged.'
Write-Host "Result: $rollbackPath"
