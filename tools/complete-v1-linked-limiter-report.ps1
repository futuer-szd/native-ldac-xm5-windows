# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Medium')]
param(
    [Parameter(Mandatory = $true)]
    [string]$ResultPath,

    [Parameter(Mandatory = $true)]
    [string]$TransactionPath,

    [Parameter(Mandatory = $true)]
    [ValidateSet('not-assessed-by-user')]
    [string]$Observation,

    [switch]$ConfirmV1LinkedLimiterReport,
    [string]$CandidatePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-linked-limiter-common.ps1')

if (-not $ConfirmV1LinkedLimiterReport) {
    throw 'Refusing to record the linked-limiter report. Re-run with -ConfirmV1LinkedLimiterReport.'
}
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ResultPath = [System.IO.Path]::GetFullPath($ResultPath)
$TransactionPath = [System.IO.Path]::GetFullPath($TransactionPath)
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $root 'artifacts\v1-linked-limiter\candidate'
}
if (-not (Test-Path -LiteralPath $ResultPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $TransactionPath -PathType Leaf)) {
    throw 'The explicit linked-limiter result or transaction path is missing.'
}

$candidate = Get-V1LinkedLimiterCandidate -CandidatePath $CandidatePath
$transaction = Get-Content -LiteralPath $TransactionPath -Raw |
    ConvertFrom-Json
$result = Get-Content -LiteralPath $ResultPath -Raw | ConvertFrom-Json
$statePath = [System.IO.Path]::GetFullPath([string]$transaction.state)
$sessionPath = [System.IO.Path]::GetFullPath([string]$transaction.session)
if (-not (Test-Path -LiteralPath $statePath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $sessionPath -PathType Leaf)) {
    throw 'The linked-limiter state or session evidence is missing.'
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
if (-not (Test-V1LinkedLimiterCompletionEvidence `
        -Transaction $transaction -Result $result `
        -Manifest $candidate.manifest -State $state -Session $session `
        -Attempts $attempts -TransactionPath $TransactionPath `
        -ResultPath $ResultPath)) {
    throw 'The selected policy v9 artifacts are not eligible for report completion.'
}

Write-Host 'Policy v9 transport, linked-limiter telemetry, source, and driver-tree evidence passed.'
Write-Host 'The user did not listen carefully, so no acoustic quality conclusion will be recorded.'
Write-Host 'This completion writes only the explicit result and transaction JSON files.'
$action = 'Record that policy v9 audio quality was not assessed by the user'
if (-not $PSCmdlet.ShouldProcess($ResultPath, $action)) {
    return
}

$reportedAt = (Get-Date).ToString('o')
$result.quality_comparison_observation = $Observation
$result | Add-Member -NotePropertyName quality_assessed_by_user `
    -NotePropertyValue $false -Force
$result | Add-Member -NotePropertyName careful_listening_reported `
    -NotePropertyValue $false -Force
foreach ($field in @(
        'bass', 'clarity', 'pumping', 'noise', 'speed', 'distortion')) {
    $result | Add-Member -NotePropertyName "${field}_observation" `
        -NotePropertyValue 'not-assessed' -Force
}
$result | Add-Member -NotePropertyName quality_reported_at `
    -NotePropertyValue $reportedAt -Force
Write-LegacyJsonAtomic -Value $result -Path $ResultPath

$transaction.status = 'transport-verified-quality-not-assessed'
$transaction.error = $null
$transaction | Add-Member -NotePropertyName user_report `
    -NotePropertyValue ([ordered]@{
        reported_at = $reportedAt
        observation = 'not-assessed-by-user'
        careful_listening = $false
        bass = 'not-assessed'
        clarity = 'not-assessed'
        pumping = 'not-assessed'
        noise = 'not-assessed'
        speed = 'not-assessed'
        distortion = 'not-assessed'
        result = $ResultPath
    }) -Force
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath

Write-Host 'Policy v9 user report finalized without an acoustic assessment.'
Write-Host 'Bass, clarity, pumping, noise, speed, and distortion remain not-assessed.'
Write-Host 'No driver, Bluetooth radio, endpoint, service, or default-output setting was probed or changed.'
Write-Host "Result: $ResultPath"
Write-Host "Transaction: $TransactionPath"
