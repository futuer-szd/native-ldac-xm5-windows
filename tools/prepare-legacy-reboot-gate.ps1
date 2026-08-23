# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmLegacyRebootPreparation,
    [switch]$ConfirmPinImpactAndReboot,
    [string]$CandidatePath,
    [string]$BackupPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'native-ldac-baseline-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmLegacyRebootPreparation -or
    -not $ConfirmPinImpactAndReboot) {
    throw 'This gate requires both -ConfirmLegacyRebootPreparation and -ConfirmPinImpactAndReboot because rebooting this machine can invalidate Windows Hello PIN state.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $projectRoot 'artifacts\legacy-candidate'
}
$CandidatePath = [System.IO.Path]::GetFullPath($CandidatePath)
$verifyScript = Join-Path $PSScriptRoot 'verify-legacy-candidate.ps1'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $verifyScript `
    -CandidatePath $CandidatePath
if ($LASTEXITCODE -ne 0) {
    throw "Legacy candidate verification failed with exit code $LASTEXITCODE."
}
$manifest = Get-Content -LiteralPath `
    (Join-Path $CandidatePath 'manifest.json') -Raw | ConvertFrom-Json
if ($manifest.policy.requires_reboot_before_avdtp -ne $true -or
    $manifest.policy.hot_swap_playback_forbidden -ne $true -or
    $manifest.policy.open_failure_telemetry_required -ne $true -or
    $manifest.policy.requires_acl_connect_event -ne $true -or
    $manifest.policy.requires_operator_power_confirmation -ne $true) {
    throw 'The candidate does not enforce the post-install reboot boundary.'
}

$failedTransactions = @(Get-ChildItem -LiteralPath `
        (Join-Path $projectRoot 'artifacts\legacy-reboot') `
        -Filter 'transport-*.json' -File -ErrorAction SilentlyContinue)
foreach ($failedTransactionPath in $failedTransactions) {
    $failedTransaction = Get-Content -LiteralPath `
        $failedTransactionPath.FullName -Raw | ConvertFrom-Json
    if ([string]$failedTransaction.status -notin @(
            'transport_failed_rollback_required',
            'rollback_awaiting_reboot',
            'rollback_verified') -or
        [string]::IsNullOrWhiteSpace(
            [string]$failedTransaction.candidate_source_commit)) {
        continue
    }
    $failedDriverTree = @(& git.exe -C $projectRoot rev-parse `
        (([string]$failedTransaction.candidate_source_commit) + ':driver') `
        2>$null)
    if ($LASTEXITCODE -eq 0 -and $failedDriverTree.Count -eq 1 -and
        ([string]$failedDriverTree[0]).Trim() -eq
            [string]$manifest.current_driver_tree) {
        $hasValidPowerCycleEvidence =
            Test-NativeLdacPhysicalPowerOnEvidence `
                -Transaction $failedTransaction
        if ($hasValidPowerCycleEvidence) {
            throw "This exact LdacNative driver tree already failed after a real ACL event and explicit physical power-on confirmation in $($failedTransactionPath.FullName). Change and review the driver contract before another reboot trial."
        }
    }
}

$headCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$gitStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or $gitStatus.Count -ne 0 -or
    $headCommit -ne [string]$manifest.source_commit) {
    throw 'The candidate must match the current clean Git HEAD.'
}

if ([string]::IsNullOrWhiteSpace($BackupPath)) {
    $latestBackupPath = Join-Path $projectRoot `
        'artifacts\driver-test\latest-backup.txt'
    if (-not (Test-Path -LiteralPath $latestBackupPath -PathType Leaf)) {
        throw 'Original A2DP latest-backup.txt is missing.'
    }
    $BackupPath = (Get-Content -LiteralPath $latestBackupPath -Raw).Trim()
}
$BackupPath = [System.IO.Path]::GetFullPath($BackupPath)
$before = Get-NativeLdacBaselineSnapshot -BackupPath $BackupPath
if (-not $before.clean_original_a2dp) {
    throw 'A clean original-A2DP baseline is required before preparing the reboot gate.'
}
if (-not $before.original_binding_healthy) {
    throw 'Turn on and connect the XM5 under the original A2DP driver before preparing the reboot gate.'
}
if (-not $before.test_signing_active) {
    throw 'The current boot is not in TESTSIGNING mode.'
}

$connectionProbePath = Join-Path $CandidatePath `
    'xm5_connection_probe.exe'
if ((Get-NativeLdacXm5BluetoothState -ProbePath $connectionProbePath `
        -ExpectedSourceCommit $headCommit) -ne
    'connected') {
    throw 'The XM5 must be physically connected under the healthy original A2DP driver before preparation.'
}

$altService = $before.original_a2dp_user_service
if ($null -eq $altService -or $altService.state -ne 'Running' -or
    $altService.start_mode -ne 'Auto' -or $altService.process_id -le 0) {
    throw 'Alternative A2DP Service must be in its original Automatic/Running state.'
}

$transactionRoot = Join-Path $projectRoot 'artifacts\legacy-reboot'
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$transactionPath = Join-Path $transactionRoot `
    "transport-$timestamp.json"
$logDirectory = Join-Path $transactionRoot "transport-$timestamp"
$latestTransactionPath = Join-Path $transactionRoot 'latest-transaction.txt'
$transaction = [ordered]@{
    transaction_version = 1
    started_at = (Get-Date).ToString('o')
    completed_at = $null
    status = 'prepared'
    phase = 'preflight'
    prepared_boot_time_utc = [string]$before.boot_time_utc
    candidate_source_commit = [string]$manifest.source_commit
    candidate_path = $CandidatePath
    backup_path = $BackupPath
    before = $before
    original_a2dp_user_service = $altService
    installed = $null
    install_reboot_reported = $false
    post_reboot = $null
    discovery = $null
    rollback = $null
    error = $null
}
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath

$target = 'Sony WH-1000XM5 A2DP service PDO across one explicit Windows reboot'
$action = 'Quiesce Alternative A2DP Service, install a newly reviewed LdacNative driver tree without opening Bluetooth, then perform one explicitly accepted reboot that may require Windows Hello PIN recovery'
if (-not $PSCmdlet.ShouldProcess($target, $action)) {
    return
}

$installationCommitted = $false
try {
    $transaction.status = 'running'
    $transaction.phase = 'quiesce_original_service'
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    Stop-Service -Name 'AltA2dpSVC' -ErrorAction Stop
    $controller = Get-Service -Name 'AltA2dpSVC' -ErrorAction Stop
    $controller.WaitForStatus(
        [System.ServiceProcess.ServiceControllerStatus]::Stopped,
        [timespan]::FromSeconds(15))
    Set-Service -Name 'AltA2dpSVC' -StartupType Manual
    $quiesced = Get-NativeLdacAltA2dpUserService
    if ($null -eq $quiesced -or $quiesced.state -ne 'Stopped' -or
        $quiesced.start_mode -ne 'Manual' -or
        $quiesced.process_id -ne 0) {
        throw 'Alternative A2DP Service did not enter Manual/Stopped.'
    }

    $transaction.phase = 'certificate'
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    $certificatePath = Join-Path $CandidatePath `
        'package\LdacNative.cer'
    $certificate = Get-PfxCertificate -FilePath $certificatePath
    foreach ($storePath in @(
            'Cert:\LocalMachine\Root',
            'Cert:\LocalMachine\TrustedPublisher')) {
        $targetPath = Join-Path $storePath $certificate.Thumbprint
        if (-not (Test-Path -LiteralPath $targetPath)) {
            $null = Import-Certificate -FilePath $certificatePath `
                -CertStoreLocation $storePath
        }
    }

    $transaction.phase = 'install'
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    $installResult = Invoke-LegacyPnpUtil -Arguments @(
            '/add-driver',
            (Join-Path $CandidatePath 'package\LdacNative.inf'),
            '/install') `
        -LogPath (Join-Path $logDirectory 'install.log') `
        -AcceptedExitCodes @(0, 3010)
    $transaction.install_reboot_reported = $installResult.reboot_required
    $installed = Wait-LegacyXm5A2dpService -ExpectedService 'LdacNative' `
        -TimeoutSeconds 30
    if ($null -eq $installed -or
        $installed.problem_code -notin @(0, 38)) {
        throw 'LdacNative did not bind in an expected healthy-or-reboot-pending state.'
    }
    $transaction.installed = $installed
    $transaction.status = 'awaiting_reboot'
    $transaction.phase = 'installed_awaiting_reboot'
    $installationCommitted = $true
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    New-Item -ItemType Directory -Path $transactionRoot -Force |
        Out-Null
    Set-Content -LiteralPath $latestTransactionPath `
        -Value $transactionPath -Encoding UTF8
} catch {
    $transaction.error = $_.Exception.Message
    if (-not $installationCommitted) {
        try {
            $null = Remove-LegacyTestDriverPackages `
                -LogDirectory $logDirectory
            $null = Restore-LegacyOriginalA2dp -BackupPath $BackupPath `
                -LogDirectory $logDirectory
            Set-Service -Name 'AltA2dpSVC' -StartupType Automatic
            Start-Service -Name 'AltA2dpSVC' -ErrorAction Stop
            $transaction.status = 'rolled_back_after_prepare_failure'
            $transaction.phase = 'complete'
        } catch {
            $transaction.status = 'rollback_failed'
            $transaction.phase = 'rollback_failed'
            $transaction.error = $transaction.error + ' | Rollback: ' +
                $_.Exception.Message
        }
    }
} finally {
    $transaction.completed_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
}

if ($transaction.status -ne 'awaiting_reboot') {
    throw "Legacy reboot preparation failed: $($transaction.error) Transaction: $transactionPath"
}

Write-Host 'Legacy reboot preparation completed without opening AVDTP signaling.'
Write-Host 'Turn the XM5 off normally now, wait at least 10 seconds, and reboot Windows once.'
Write-Host 'After signing in, do not start audio; run the post-reboot transport gate shown below.'
Write-Host 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\run-legacy-post-reboot-transport-gate.ps1 -ConfirmLegacyPostRebootTransport'
Write-Host "Transaction: $transactionPath"
