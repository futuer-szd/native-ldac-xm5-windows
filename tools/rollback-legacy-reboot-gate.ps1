# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmLegacyRebootRollback,
    [string]$TransactionPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'native-ldac-baseline-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmLegacyRebootRollback) {
    throw 'Refusing to restore the original A2DP driver. Re-run with -ConfirmLegacyRebootRollback.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($TransactionPath)) {
    $latestPath = Join-Path $projectRoot `
        'artifacts\legacy-reboot\latest-transaction.txt'
    if (-not (Test-Path -LiteralPath $latestPath -PathType Leaf)) {
        throw 'No legacy reboot transaction was found.'
    }
    $TransactionPath = (Get-Content -LiteralPath $latestPath -Raw).Trim()
}
$TransactionPath = [System.IO.Path]::GetFullPath($TransactionPath)
$transaction = Get-Content -LiteralPath $TransactionPath -Raw |
    ConvertFrom-Json
if ([int]$transaction.transaction_version -ne 1 -or
    [string]$transaction.status -notin @(
        'awaiting_reboot',
        'running_post_reboot',
        'transport_failed_rollback_required',
        'transport_verified')) {
    throw 'The selected transaction is not in a rollback-eligible state.'
}

$connectionProbePath = Join-Path $projectRoot `
    'artifacts\diagnostics\xm5_connection_probe.exe'
if ((Get-NativeLdacXm5BluetoothState -ProbePath $connectionProbePath `
        -ExpectedSourceCommit `
            ([string]$transaction.candidate_source_commit)) -ne
    'disconnected') {
    throw 'Turn off the XM5 and wait for physical Bluetooth disconnection before rollback.'
}
$processes = @(Get-NativeLdacWorkspaceProcesses)
if ($processes.Count -ne 0) {
    throw 'Stop every LDAC media process before rollback.'
}

$target = 'Sony WH-1000XM5 A2DP service PDO'
$action = 'Remove LdacNative, restore the recorded original A2DP backup, restore Alternative A2DP Service to Automatic/Running, and verify whether a reboot is actually needed'
if (-not $PSCmdlet.ShouldProcess($target, $action)) {
    return
}

$logDirectory = Join-Path (Split-Path -Parent $TransactionPath) `
    ([System.IO.Path]::GetFileNameWithoutExtension($TransactionPath))
try {
    $transaction.phase = 'rollback_remove_test'
    Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
    $removed = @(Remove-LegacyTestDriverPackages `
        -LogDirectory $logDirectory)
    $transaction.phase = 'rollback_restore_original'
    Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
    $null = Restore-LegacyOriginalA2dp `
        -BackupPath ([string]$transaction.backup_path) `
        -LogDirectory $logDirectory
    $backupState = Get-Content -LiteralPath `
        (Join-Path ([string]$transaction.backup_path) 'state.json') `
        -Raw | ConvertFrom-Json
    $restored = Wait-LegacyXm5A2dpService `
        -ExpectedService ([string]$backupState.service) `
        -TimeoutSeconds 30
    if ($null -eq $restored -or $restored.problem_code -ne 0 -or
        -not $restored.published_inf.Equals(
            [string]$backupState.published_inf,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The original A2DP binding was not restored exactly.'
    }
    Set-Service -Name 'AltA2dpSVC' -StartupType Automatic
    Start-Service -Name 'AltA2dpSVC' -ErrorAction Stop
    $controller = Get-Service -Name 'AltA2dpSVC' -ErrorAction Stop
    $controller.WaitForStatus(
        [System.ServiceProcess.ServiceControllerStatus]::Running,
        [timespan]::FromSeconds(15))
    $service = Get-NativeLdacAltA2dpUserService
    if ($null -eq $service -or $service.state -ne 'Running' -or
        $service.start_mode -ne 'Auto' -or $service.process_id -le 0) {
        throw 'Alternative A2DP Service was not restored to Automatic/Running.'
    }
    $remaining = @(Get-LegacyDriverPackages `
        -OriginalInfNames @('LdacNative.inf'))
    if ($remaining.Count -ne 0) {
        throw 'An LdacNative package remains after rollback.'
    }
    $after = Get-NativeLdacBaselineSnapshot `
        -BackupPath ([string]$transaction.backup_path)
    if (-not $after.safe_original_a2dp) {
        throw 'The original A2DP baseline is not safe after rollback.'
    }
    $transaction.rollback = [ordered]@{
        completed_at = (Get-Date).ToString('o')
        removed_test_packages = $removed
        restored = $restored
        original_service = $service
        baseline = $after
        reboot_required_by_policy = $false
    }
    $transaction.status = 'rollback_verified'
    $transaction.phase = 'complete'
    $transaction.error = $null
} catch {
    $transaction.status = 'rollback_failed'
    $transaction.phase = 'rollback_failed'
    $transaction.error = $_.Exception.Message
} finally {
    $transaction.completed_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
}

if ($transaction.status -eq 'rollback_failed') {
    throw "Legacy reboot rollback failed: $($transaction.error) Transaction: $TransactionPath"
}

Write-Host 'Original AltA2DP binding and service were restored.'
Write-Host 'The safe original-A2DP baseline is verified; this gate does not require a reboot.'
Write-Host 'Turn on the XM5 and confirm normal Alternative A2DP use. Do not reboot solely for this rollback.'
Write-Host "Transaction: $TransactionPath"
