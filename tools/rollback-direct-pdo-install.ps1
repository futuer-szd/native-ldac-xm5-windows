# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [string]$TransactionPath,
    [switch]$ConfirmDirectPdoRollback,
    [switch]$RestoreOriginalA2dp
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'direct-pdo-install-common.ps1')

Assert-DirectPdoAdministrator
if (-not $ConfirmDirectPdoRollback) {
    throw 'Refusing to change the XM5 PDO driver. Re-run with -ConfirmDirectPdoRollback.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$installRoot = Join-Path $projectRoot 'artifacts\direct-pdo\install'
if ([string]::IsNullOrWhiteSpace($TransactionPath)) {
    $latestPath = Join-Path $installRoot 'latest-transaction.txt'
    if (-not (Test-Path -LiteralPath $latestPath -PathType Leaf)) {
        throw 'No Direct-PDO transaction is recorded. Provide -TransactionPath.'
    }
    $TransactionPath = (Get-Content -LiteralPath $latestPath -Raw).Trim()
}
$TransactionPath = [System.IO.Path]::GetFullPath($TransactionPath)
if (-not (Test-Path -LiteralPath $TransactionPath -PathType Leaf)) {
    throw "Transaction file was not found: $TransactionPath"
}
$transactionRecord = Read-DirectPdoTransaction -Path $TransactionPath
$transaction = $transactionRecord.transaction
$rollbackTarget = $transactionRecord.rollback_target
if ([string]$transaction.status -eq 'rolled-back' -and
    -not $RestoreOriginalA2dp) {
    Write-Host 'This transaction is already rolled back; no change was made.'
    Write-Host "Transaction: $TransactionPath"
    return
}

$target = if ($RestoreOriginalA2dp) {
    'the verified original A2DP backup'
} else {
    "the rollback service $($rollbackTarget.service)"
}
if (-not $PSCmdlet.ShouldProcess(
        'Sony WH-1000XM5 A2DP service PDO',
        "Remove NativeLdacDirectPdo and restore $target")) {
    return
}

$rollbackId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$logDirectory = Join-Path $installRoot "manual-rollback-$rollbackId"
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$transaction.status = 'rolling-back'
$transaction.phase = 'manual-rollback-started'
$transaction.updated_at = (Get-Date).ToString('o')
$transaction.rollback.attempted = $true
$transaction.rollback.status = 'running'
$transaction.rollback.started_at = $transaction.updated_at
$transaction.history = @($transaction.history) + [pscustomobject]@{
    at = $transaction.updated_at
    phase = $transaction.phase
    message = "Manual rollback requested to $target."
}
Write-DirectPdoJsonAtomic -Value $transaction -Path $TransactionPath

try {
    $removed = @(Remove-DriverPackagesByOriginalInf `
        -InfNames @('NativeLdacDirectPdo.inf') `
        -LogDirectory $logDirectory)
    $transaction.rollback.removed_packages = @(
        @($transaction.rollback.removed_packages) + $removed)

    $restored = $null
    if (-not $RestoreOriginalA2dp -and
        [string]$rollbackTarget.service -eq 'LdacNative') {
        $null = Invoke-DirectPdoPnpUtil `
            -Arguments @('/scan-devices') `
            -LogPath (Join-Path $logDirectory 'scan-previous.log')
        $restored = Wait-Xm5A2dpService `
            -ExpectedService 'LdacNative' `
            -TimeoutSeconds 20
        if ($null -ne $restored -and $restored.problem_code -ne 0) {
            $restored = $null
        }
    }

    if ($null -eq $restored) {
        $legacyRemoved = @(Remove-DriverPackagesByOriginalInf `
            -InfNames @('LdacNative.inf') `
            -LogDirectory $logDirectory)
        $transaction.rollback.removed_legacy_packages = @(
            @($transaction.rollback.removed_legacy_packages) +
            $legacyRemoved)
        $backupState = Restore-OriginalA2dpBackup `
            -BackupPath ([string]$rollbackTarget.backup_path) `
            -LogDirectory $logDirectory
        $presentTargets = @(Get-Xm5A2dpDevice)
        if ($presentTargets.Count -eq 0 -and $RestoreOriginalA2dp) {
            $testPackages = @(Get-DriverPackagesByOriginalInf `
                -InfNames @(
                    'LdacNative.inf',
                    'NativeLdacDirectPdo.inf'))
            $originalPackages = @(Get-DriverPackagesByOriginalInf `
                -InfNames @('alta2dp.inf'))
            if ($testPackages.Count -ne 0) {
                throw 'The XM5 PDO is absent, but one or more LDAC test packages remain staged.'
            }
            if ($originalPackages.Count -eq 0) {
                throw 'The XM5 PDO is absent and the original AltA2DP package is not staged.'
            }
            $restored = [pscustomobject][ordered]@{
                service = [string]$backupState.service
                published_inf = [string]$originalPackages[0].Driver
                problem_code = 0
            }
            Write-Host 'XM5 is off and its PDO is absent; the verified original package is staged for the next connection.'
        } else {
            $restored = Wait-Xm5A2dpService `
                -ExpectedService ([string]$backupState.service) `
                -TimeoutSeconds 30
        }
    }
    if ($null -eq $restored -or $restored.problem_code -ne 0) {
        throw 'Packages were restored, but the XM5 PDO did not return healthy before the timeout.'
    }

    $transaction.status = 'rolled-back'
    $transaction.phase = 'rolled-back'
    $transaction.updated_at = (Get-Date).ToString('o')
    $transaction.rollback.status = 'succeeded'
    $transaction.rollback.completed_at = $transaction.updated_at
    $transaction.rollback.result_service = $restored.service
    $transaction.rollback.result_published_inf = $restored.published_inf
    $transaction.rollback.error = $null
    $transaction.history = @($transaction.history) + [pscustomobject]@{
        at = $transaction.updated_at
        phase = $transaction.phase
        message = "Manual rollback restored service $($restored.service)."
    }
    Write-DirectPdoJsonAtomic -Value $transaction -Path $TransactionPath

    Write-Host "Rollback completed: $($restored.service), $($restored.published_inf)."
    Write-Host "Transaction: $TransactionPath"
    Write-Host 'The shared test certificate was retained because the legacy test driver may still use it.'
} catch {
    $transaction.status = 'rollback-incomplete'
    $transaction.phase = 'rollback-incomplete'
    $transaction.updated_at = (Get-Date).ToString('o')
    $transaction.rollback.status = 'failed'
    $transaction.rollback.completed_at = $transaction.updated_at
    $transaction.rollback.error = $_.Exception.Message
    $transaction.history = @($transaction.history) + [pscustomobject]@{
        at = $transaction.updated_at
        phase = $transaction.phase
        message = $_.Exception.Message
    }
    Write-DirectPdoJsonAtomic -Value $transaction -Path $TransactionPath
    throw "Rollback is incomplete: $($_.Exception.Message) Transaction: $TransactionPath"
}
