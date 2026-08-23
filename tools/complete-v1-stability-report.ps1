# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Medium')]
param(
    [Parameter(Mandatory = $true)]
    [string]$ResultPath,

    [Parameter(Mandatory = $true)]
    [string]$TransactionPath,

    [Parameter(Mandatory = $true)]
    [ValidateSet('generally-clear', 'muffled-bass')]
    [string[]]$Observation,

    [switch]$ConfirmV1StabilityReport,
    [string]$CandidatePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-stability-burst-common.ps1')

if (-not $ConfirmV1StabilityReport) {
    throw 'Refusing to record the stability report. Re-run with -ConfirmV1StabilityReport.'
}
$observations = @($Observation | Select-Object -Unique)
if ($observations.Count -ne 2 -or
    'generally-clear' -notin $observations -or
    'muffled-bass' -notin $observations) {
    throw 'This completion requires exactly: generally-clear, muffled-bass.'
}

$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ResultPath = [System.IO.Path]::GetFullPath($ResultPath)
$TransactionPath = [System.IO.Path]::GetFullPath($TransactionPath)
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $root 'artifacts\v1-stability-burst\candidate'
}
if (-not (Test-Path -LiteralPath $ResultPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $TransactionPath -PathType Leaf)) {
    throw 'The explicit stability result or transaction path is missing.'
}

$candidate = Get-V1StabilityBurstCandidate -CandidatePath $CandidatePath
$transaction = Get-Content -LiteralPath $TransactionPath -Raw |
    ConvertFrom-Json
$result = Get-Content -LiteralPath $ResultPath -Raw | ConvertFrom-Json
$statePath = [System.IO.Path]::GetFullPath([string]$transaction.state)
$sessionPath = [System.IO.Path]::GetFullPath([string]$transaction.session)
if (-not (Test-Path -LiteralPath $statePath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $sessionPath -PathType Leaf)) {
    throw 'The stability state or session evidence is missing.'
}
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$session = Get-Content -LiteralPath $sessionPath -Raw | ConvertFrom-Json
$state | Add-Member -NotePropertyName __evidence_path `
    -NotePropertyValue $statePath -Force
$session | Add-Member -NotePropertyName __evidence_path `
    -NotePropertyValue $sessionPath -Force
$attemptFiles = @(Get-ChildItem -LiteralPath ([string]$transaction.directory) `
    -Filter 'session.json.attempt-*.json' -File | Sort-Object Name)
$attempts = @($attemptFiles | ForEach-Object {
    Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
})
if (-not (Test-V1StabilityCompletionEvidence `
        -Transaction $transaction -Result $result `
        -Manifest $candidate.manifest -State $state -Session $session `
        -Attempts $attempts -TransactionPath $TransactionPath `
        -ResultPath $ResultPath)) {
    throw 'The selected policy v8 artifacts are not eligible for user-report completion.'
}

Write-Host 'Policy v8 transport, duration, ConsumerLease, SUSPEND/CLOSE, source, and driver-tree evidence passed.'
Write-Host 'This completion writes only the explicit result and transaction JSON files.'
$action = 'Record the enumerated generally-clear and muffled-bass user report'
if (-not $PSCmdlet.ShouldProcess($ResultPath, $action)) {
    return
}

$reportedAt = (Get-Date).ToString('o')
$result.stability_observation =
    'user-reported-generally-clear-with-muffled-bass'
$result | Add-Member -NotePropertyName stability_reported `
    -NotePropertyValue $true -Force
$result | Add-Member -NotePropertyName reported_observations `
    -NotePropertyValue @('generally-clear', 'muffled-bass') -Force
$result | Add-Member -NotePropertyName clarity_observation `
    -NotePropertyValue 'generally-clear' -Force
$result | Add-Member -NotePropertyName bass_observation `
    -NotePropertyValue 'muffled-bass' -Force
foreach ($field in @('dropouts', 'speed', 'noise', 'distortion')) {
    $result | Add-Member -NotePropertyName "${field}_observation" `
        -NotePropertyValue 'not-reported' -Force
}
$result | Add-Member -NotePropertyName stability_reported_at `
    -NotePropertyValue $reportedAt -Force
Write-LegacyJsonAtomic -Value $result -Path $ResultPath

$transaction.status = 'stability-verified-user-report'
$transaction.error = $null
$transaction | Add-Member -NotePropertyName user_report `
    -NotePropertyValue ([ordered]@{
        reported_at = $reportedAt
        observations = @('generally-clear', 'muffled-bass')
        dropouts = 'not-reported'
        speed = 'not-reported'
        noise = 'not-reported'
        distortion = 'not-reported'
        result = $ResultPath
    }) -Force
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath

Write-Host 'Policy v8 user stability report recorded successfully.'
Write-Host 'Reported: generally-clear; muffled-bass.'
Write-Host 'Dropouts, speed, noise, and distortion remain not-reported.'
Write-Host 'No driver, Bluetooth radio, endpoint, service, or default-output setting was changed.'
Write-Host "Result: $ResultPath"
Write-Host "Transaction: $TransactionPath"
