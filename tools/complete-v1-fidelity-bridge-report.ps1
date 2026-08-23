# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Medium')]
param(
    [Parameter(Mandatory = $true)]
    [string]$ResultPath,

    [Parameter(Mandatory = $true)]
    [string]$TransactionPath,

    [Parameter(Mandatory = $true)]
    [ValidateSet('overall-normal', 'subtle-differences-not-discernible')]
    [string[]]$Observation,

    [switch]$ConfirmV1FidelityBridgeReport,
    [string]$CandidatePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-fidelity-bridge-common.ps1')

if (-not $ConfirmV1FidelityBridgeReport) {
    throw 'Refusing to record the fidelity-bridge report. Re-run with -ConfirmV1FidelityBridgeReport.'
}
$observations = @($Observation | Select-Object -Unique)
if ($observations.Count -ne 2 -or
    'overall-normal' -notin $observations -or
    'subtle-differences-not-discernible' -notin $observations) {
    throw 'This completion requires exactly: overall-normal, subtle-differences-not-discernible.'
}
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ResultPath = [System.IO.Path]::GetFullPath($ResultPath)
$TransactionPath = [System.IO.Path]::GetFullPath($TransactionPath)
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $root 'artifacts\v1-fidelity-bridge\candidate'
}
if (-not (Test-Path -LiteralPath $ResultPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $TransactionPath -PathType Leaf)) {
    throw 'The explicit fidelity-bridge result or transaction path is missing.'
}

$candidate = Get-V1FidelityBridgeCandidate -CandidatePath $CandidatePath
$transaction = Get-Content -LiteralPath $TransactionPath -Raw |
    ConvertFrom-Json
$result = Get-Content -LiteralPath $ResultPath -Raw | ConvertFrom-Json
$statePath = [System.IO.Path]::GetFullPath([string]$transaction.state)
$sessionPath = [System.IO.Path]::GetFullPath([string]$transaction.session)
if (-not (Test-Path -LiteralPath $statePath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $sessionPath -PathType Leaf)) {
    throw 'The fidelity-bridge state or session evidence is missing.'
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
if (-not (Test-V1FidelityBridgeCompletionEvidence `
        -Transaction $transaction -Result $result `
        -Manifest $candidate.manifest -State $state -Session $session `
        -Attempts $attempts -TransactionPath $TransactionPath `
        -ResultPath $ResultPath)) {
    throw 'The selected policy v10 artifacts are not eligible for report completion.'
}

Write-Host 'Policy v10 transport, fade, PCM lock, generation, lease, limiter, peak, source, and driver-tree evidence passed.'
Write-Host 'This completion records only the user statements: overall normal, ten seconds normal, and subtle differences not discernible.'
Write-Host 'No separate bass, clarity, pumping, noise, speed, or distortion conclusion will be invented.'
Write-Host 'This completion writes only the explicit result and transaction JSON files.'
$action = 'Record the bounded policy v10 user fidelity report'
if (-not $PSCmdlet.ShouldProcess($ResultPath, $action)) {
    return
}

$reportedAt = (Get-Date).ToString('o')
$result.fidelity_observation =
    'user-reported-overall-normal-subtle-differences-not-discernible'
$result | Add-Member -NotePropertyName reported_observations `
    -NotePropertyValue @(
        'overall-normal', 'subtle-differences-not-discernible') -Force
$result | Add-Member -NotePropertyName overall_observation `
    -NotePropertyValue 'normal' -Force
$result | Add-Member -NotePropertyName comparison_observation `
    -NotePropertyValue 'subtle-differences-not-discernible' -Force
$result | Add-Member -NotePropertyName ten_second_playback_observation `
    -NotePropertyValue 'normal' -Force
foreach ($field in @(
        'bass', 'clarity', 'pumping', 'noise', 'speed', 'distortion')) {
    $result | Add-Member -NotePropertyName "${field}_observation" `
        -NotePropertyValue 'not-separately-assessed' -Force
}
$result | Add-Member -NotePropertyName fidelity_reported_at `
    -NotePropertyValue $reportedAt -Force
Write-LegacyJsonAtomic -Value $result -Path $ResultPath

$transaction.status = 'fidelity-verified-user-report'
$transaction.error = $null
$transaction | Add-Member -NotePropertyName user_report `
    -NotePropertyValue ([ordered]@{
        reported_at = $reportedAt
        observations = @(
            'overall-normal', 'subtle-differences-not-discernible')
        ten_second_playback = 'normal'
        bass = 'not-separately-assessed'
        clarity = 'not-separately-assessed'
        pumping = 'not-separately-assessed'
        noise = 'not-separately-assessed'
        speed = 'not-separately-assessed'
        distortion = 'not-separately-assessed'
        result = $ResultPath
    }) -Force
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath

Write-Host 'Policy v10 fidelity report recorded successfully.'
Write-Host 'Reported: overall normal; ten seconds normal; subtle differences not discernible.'
Write-Host 'Bass, clarity, pumping, noise, speed, and distortion remain not-separately-assessed.'
Write-Host 'No driver, Bluetooth radio, endpoint, service, or default-output setting was probed or changed.'
Write-Host "Result: $ResultPath"
Write-Host "Transaction: $TransactionPath"
