# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1DiscoveryRollback,
    [string]$TransactionPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'legacy-install-common.ps1')
. (Join-Path $PSScriptRoot 'native-ldac-baseline-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1DiscoveryRollback) {
    throw 'Refusing to change the XM5 A2DP function driver. Re-run with -ConfirmV1DiscoveryRollback.'
}
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$trialRoot = Join-Path $projectRoot `
    'artifacts\v1-discovery-handoff\trial'
if ([string]::IsNullOrWhiteSpace($TransactionPath)) {
    $latestPath = Join-Path $trialRoot 'latest-transaction.txt'
    if (-not (Test-Path -LiteralPath $latestPath -PathType Leaf)) {
        throw 'No V1 discovery handoff transaction is recorded.'
    }
    $TransactionPath = (Get-Content -LiteralPath $latestPath -Raw).Trim()
}
$TransactionPath = [System.IO.Path]::GetFullPath($TransactionPath)
if (-not (Test-Path -LiteralPath $TransactionPath -PathType Leaf)) {
    throw "V1 discovery transaction was not found: $TransactionPath"
}
$transaction = Get-Content -LiteralPath $TransactionPath -Raw |
    ConvertFrom-Json
$backupPath = [System.IO.Path]::GetFullPath(
    [string]$transaction.backup_path)
if (-not (Test-Path -LiteralPath `
        (Join-Path $backupPath 'state.json') -PathType Leaf)) {
    throw "Original A2DP backup is missing: $backupPath"
}
if (-not $PSCmdlet.ShouldProcess(
        'Sony WH-1000XM5 A2DP Sink service PDO',
        'Stop contained V1 discovery processes, remove LdacNative, restore the recorded original A2DP package, and restore Alternative A2DP Service')) {
    return
}

$rollbackId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$logDirectory = Join-Path $trialRoot "manual-rollback-$rollbackId"
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$workspaceProcesses = @(Get-NativeLdacWorkspaceProcesses)
foreach ($process in $workspaceProcesses) {
    $executablePath = [string]$process.executable_path
    if (-not [string]::IsNullOrWhiteSpace($executablePath) -and
        $executablePath.StartsWith(
            $projectRoot.TrimEnd('\') + '\',
            [StringComparison]::OrdinalIgnoreCase)) {
        Stop-Process -Id ([int]$process.process_id) -Force `
            -ErrorAction SilentlyContinue
    }
}
$null = Remove-LegacyTestDriverPackages -LogDirectory $logDirectory
$restoredState = Restore-LegacyOriginalA2dp `
    -BackupPath $backupPath -LogDirectory $logDirectory
$restored = Wait-LegacyXm5A2dpService `
    -ExpectedService ([string]$restoredState.service) -TimeoutSeconds 30
if ($null -eq $restored -or [int]$restored.problem_code -ne 0) {
    throw 'The original A2DP function driver did not return healthy.'
}
Set-Service -Name 'AltA2dpSVC' -StartupType Automatic
Start-Service -Name 'AltA2dpSVC' -ErrorAction Stop
$baseline = Get-NativeLdacBaselineSnapshot -BackupPath $backupPath
if (-not $baseline.safe_original_a2dp -or
    -not $baseline.original_binding_healthy) {
    throw 'Manual rollback completed its commands, but the original baseline is not healthy.'
}

$transaction.status = 'manually-rolled-back'
$transaction.phase = 'manually-rolled-back'
$transaction.updated_at = (Get-Date).ToString('o')
$transaction.rollback.attempted = $true
$transaction.rollback.succeeded = $true
$transaction.rollback.service = [string]$restored.service
$transaction.rollback.published_inf = [string]$restored.published_inf
$transaction.rollback.error = $null
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath

Write-Host "Original A2DP restored: $($restored.service), $($restored.published_inf)."
Write-Host 'Alternative A2DP Service is Automatic/Running.'
Write-Host 'No reboot or Windows Bluetooth toggle was performed.'
Write-Host "Transaction: $TransactionPath"
