# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$TransactionPath,
    [string]$CandidatePath,
    [switch]$RequireHealthy
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'direct-pdo-install-common.ps1')

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$installRoot = Join-Path $projectRoot 'artifacts\direct-pdo\install'
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $projectRoot 'artifacts\direct-pdo\candidate'
}
$CandidatePath = [System.IO.Path]::GetFullPath($CandidatePath)

if ([string]::IsNullOrWhiteSpace($TransactionPath)) {
    $latestPath = Join-Path $installRoot 'latest-transaction.txt'
    if (Test-Path -LiteralPath $latestPath -PathType Leaf) {
        $TransactionPath = (Get-Content -LiteralPath $latestPath -Raw).Trim()
    }
}
if (-not [string]::IsNullOrWhiteSpace($TransactionPath)) {
    $TransactionPath = [System.IO.Path]::GetFullPath($TransactionPath)
    if (Test-Path -LiteralPath $TransactionPath -PathType Leaf) {
        $transaction = Get-Content -LiteralPath $TransactionPath -Raw |
            ConvertFrom-Json
        Write-Host "Transaction: $($transaction.status), phase $($transaction.phase)"
        Write-Host "Transaction file: $TransactionPath"
        if (-not [string]::IsNullOrWhiteSpace(
                [string]$transaction.failure)) {
            Write-Host "Recorded failure: $($transaction.failure)"
        }
    } else {
        Write-Warning "Recorded transaction file is missing: $TransactionPath"
    }
} else {
    Write-Host 'Transaction: none recorded.'
}

$devices = @(Get-Xm5A2dpDevice)
if ($devices.Count -eq 0) {
    Write-Host 'XM5 A2DP PDO: not present (the headset may be off).'
    if ($RequireHealthy) {
        throw 'A healthy Direct-PDO binding was required, but the XM5 PDO is not present.'
    }
    return
}
if ($devices.Count -ne 1) {
    throw "Expected at most one present XM5 A2DP PDO, found $($devices.Count)."
}
$snapshot = Get-Xm5A2dpSnapshot -Device $devices[0]
Write-Host "XM5 A2DP PDO: service $($snapshot.service), INF $($snapshot.published_inf), problem code $($snapshot.problem_code)."

$directHealthy = $snapshot.service -eq 'NativeLdacDirectPdo' -and
    $snapshot.problem_code -eq 0
if ($snapshot.service -eq 'NativeLdacDirectPdo') {
    $probePath = Join-Path $CandidatePath 'audio_endpoint_probe.exe'
    if (-not (Test-Path -LiteralPath $probePath -PathType Leaf)) {
        Write-Warning "Direct-PDO probe is missing: $probePath"
        $directHealthy = $false
    } else {
        $probeOutput = @()
        $probeExitCode = -1
        $previousPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $probeOutput = @(& $probePath --direct-status 2>&1)
            $probeExitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $previousPreference
        }
        $probeOutput | ForEach-Object { Write-Host $_ }
        $probeText = $probeOutput -join [Environment]::NewLine
        if (-not (Test-DirectPdoRuntimeStatusText `
                -StatusText $probeText `
                -ExitCode $probeExitCode)) {
            $directHealthy = $false
        }
    }
}

if ($directHealthy) {
    Write-Host 'Direct-PDO installation status: healthy.'
} elseif ($snapshot.service -eq 'NativeLdacDirectPdo') {
    Write-Warning 'Direct-PDO is bound but its PnP/runtime health check failed.'
} else {
    Write-Host 'Direct-PDO installation status: not bound.'
}
if ($RequireHealthy -and -not $directHealthy) {
    throw 'A healthy NativeLdacDirectPdo binding was required.'
}
