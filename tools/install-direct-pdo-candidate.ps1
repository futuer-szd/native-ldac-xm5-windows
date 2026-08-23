# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [string]$CandidatePath,
    [switch]$ConfirmDirectPdoInstall,
    [switch]$AllowDirtyCandidate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'direct-pdo-install-common.ps1')

function Add-TransactionHistory {
    param(
        [Parameter(Mandatory = $true)][string]$Phase,
        [Parameter(Mandatory = $true)][string]$Message
    )

    $script:transaction.phase = $Phase
    $script:transaction.updated_at = (Get-Date).ToString('o')
    $script:transaction.history += [ordered]@{
        at = $script:transaction.updated_at
        phase = $Phase
        message = $Message
    }
    Write-DirectPdoJsonAtomic -Value $script:transaction `
        -Path $script:transactionPath
}

function Invoke-AutomaticRollback {
    param([Parameter(Mandatory = $true)][string]$Reason)

    $script:transaction.status = 'rolling-back'
    $script:transaction.failure = $Reason
    $script:transaction.rollback.attempted = $true
    $script:transaction.rollback.started_at = (Get-Date).ToString('o')
    Add-TransactionHistory -Phase 'rollback-started' `
        -Message 'Automatic rollback started after an incomplete install.'
    try {
        $removed = @(Remove-DriverPackagesByOriginalInf `
            -InfNames @('NativeLdacDirectPdo.inf') `
            -LogDirectory $script:logDirectory)
        $script:transaction.rollback.removed_packages = $removed
        $null = Invoke-DirectPdoPnpUtil `
            -Arguments @('/scan-devices') `
            -LogPath (Join-Path $script:logDirectory 'rollback-scan.log')

        $restored = $null
        if ($script:transaction.rollback_target.service -eq 'LdacNative') {
            $restored = Wait-Xm5A2dpService `
                -ExpectedService 'LdacNative' `
                -TimeoutSeconds 15
            if ($null -ne $restored -and $restored.problem_code -ne 0) {
                $restored = $null
            }
        }
        if ($null -eq $restored) {
            $legacyRemoved = @(Remove-DriverPackagesByOriginalInf `
                -InfNames @('LdacNative.inf') `
                -LogDirectory $script:logDirectory)
            $script:transaction.rollback.removed_legacy_packages =
                $legacyRemoved
            $backupState = Restore-OriginalA2dpBackup `
                -BackupPath $script:transaction.rollback_target.backup_path `
                -LogDirectory $script:logDirectory
            $restored = Wait-Xm5A2dpService `
                -ExpectedService ([string]$backupState.service) `
                -TimeoutSeconds 30
        }
        if ($null -eq $restored -or $restored.problem_code -ne 0) {
            throw 'Driver packages were restored, but the XM5 PDO did not return healthy before the timeout.'
        }

        $script:transaction.status = 'rolled-back'
        $script:transaction.rollback.status = 'succeeded'
        $script:transaction.rollback.completed_at = (Get-Date).ToString('o')
        $script:transaction.rollback.result_service = $restored.service
        $script:transaction.rollback.result_published_inf =
            $restored.published_inf
        Add-TransactionHistory -Phase 'rolled-back' `
            -Message "Automatic rollback restored service $($restored.service)."
        return $true
    } catch {
        $script:transaction.status = 'rollback-incomplete'
        $script:transaction.rollback.status = 'failed'
        $script:transaction.rollback.completed_at = (Get-Date).ToString('o')
        $script:transaction.rollback.error = $_.Exception.Message
        Add-TransactionHistory -Phase 'rollback-incomplete' `
            -Message $_.Exception.Message
        return $false
    }
}

Assert-DirectPdoAdministrator
if (-not $ConfirmDirectPdoInstall) {
    throw 'Refusing to replace the XM5 A2DP PDO driver. Re-run with -ConfirmDirectPdoInstall.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Assert-DirectPdoHardwareTestsEnabled -ProjectRoot $projectRoot
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $projectRoot 'artifacts\direct-pdo\candidate'
}
$CandidatePath = [System.IO.Path]::GetFullPath($CandidatePath)
$verifyScript = Join-Path $PSScriptRoot 'verify-direct-pdo-candidate.ps1'
$preflightScript = Join-Path $PSScriptRoot `
    'test-direct-pdo-install-readiness.ps1'
$verifyArguments = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $verifyScript,
    '-CandidatePath', $CandidatePath
)
& powershell.exe @verifyArguments
if ($LASTEXITCODE -ne 0) {
    throw "Candidate verification failed with exit code $LASTEXITCODE."
}
$preflightArguments = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $preflightScript,
    '-CandidatePath', $CandidatePath
)
if ($AllowDirtyCandidate) {
    $preflightArguments += '-AllowDirtyCandidate'
}
& powershell.exe @preflightArguments
if ($LASTEXITCODE -ne 0) {
    throw "Install readiness preflight failed with exit code $LASTEXITCODE."
}

$devices = @(Get-Xm5A2dpDevice)
if ($devices.Count -ne 1) {
    throw "Expected one present XM5 A2DP PDO, found $($devices.Count)."
}
$previous = Get-Xm5A2dpSnapshot -Device $devices[0]
$legacyStatePath = Join-Path $projectRoot `
    'artifacts\driver-test\install-state.json'
$latestBackupPath = Join-Path $projectRoot `
    'artifacts\driver-test\latest-backup.txt'
$priorTransactionPath = $null
if ($previous.service -eq 'LdacNative') {
    $legacyState = Get-Content -LiteralPath $legacyStatePath -Raw |
        ConvertFrom-Json
    $backupPath = [System.IO.Path]::GetFullPath(
        [string]$legacyState.backup_path)
    $rollbackService = 'LdacNative'
    $rollbackPublishedInf = [string]$legacyState.published_inf
} elseif ($previous.service -eq 'NativeLdacDirectPdo') {
    $latestTransactionPath = Join-Path $projectRoot `
        'artifacts\direct-pdo\install\latest-transaction.txt'
    $priorTransactionPath = (Get-Content `
        -LiteralPath $latestTransactionPath -Raw).Trim()
    $priorRecord = Read-DirectPdoTransaction -Path $priorTransactionPath
    $rollbackTarget = $priorRecord.rollback_target
    $backupPath = [System.IO.Path]::GetFullPath(
        [string]$rollbackTarget.backup_path)
    $rollbackService = [string]$rollbackTarget.service
    $rollbackPublishedInf = [string]$rollbackTarget.published_inf
} else {
    $backupPath = [System.IO.Path]::GetFullPath(
        (Get-Content -LiteralPath $latestBackupPath -Raw).Trim())
    $rollbackService = $previous.service
    $rollbackPublishedInf = $previous.published_inf
}

$manifest = Get-Content -LiteralPath `
    (Join-Path $CandidatePath 'manifest.json') -Raw | ConvertFrom-Json
$packagePath = Join-Path $CandidatePath 'package'
$infPath = Join-Path $packagePath 'NativeLdacDirectPdo.inf'
$certificatePath = Join-Path $packagePath 'NativeLdacDirectPdo.cer'
$transactionId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$installRoot = Join-Path $projectRoot 'artifacts\direct-pdo\install'
$script:logDirectory = Join-Path $installRoot "logs-$transactionId"
$script:transactionPath = Join-Path $installRoot `
    "transaction-$transactionId.json"
$script:transaction = [ordered]@{
    transaction_version = 2
    transaction_id = $transactionId
    created_at = (Get-Date).ToString('o')
    updated_at = (Get-Date).ToString('o')
    status = 'prepared'
    phase = 'prepared'
    failure = $null
    target = [ordered]@{
        instance_id = $previous.instance_id
        hardware_id = [string]$manifest.hardware_id
    }
    previous = [ordered]@{
        service = $previous.service
        published_inf = $previous.published_inf
        problem_code = $previous.problem_code
        backup_path = $backupPath
    }
    rollback_target = [ordered]@{
        service = $rollbackService
        published_inf = $rollbackPublishedInf
        backup_path = $backupPath
        source_transaction = $priorTransactionPath
    }
    candidate = [ordered]@{
        source_commit = [string]$manifest.source_commit
        source_dirty = [bool]$manifest.source_dirty
        service = [string]$manifest.service_name
        published_inf = $null
        pnputil_exit_code = $null
        reboot_required = $false
        certificate_thumbprint = [string]$manifest.certificate_thumbprint
        certificate_root_preexisting = $null
        certificate_publisher_preexisting = $null
    }
    validation = [ordered]@{
        service_bound = $false
        pnp_problem_code = $null
        direct_media_abi = $false
    }
    rollback = [ordered]@{
        attempted = $false
        status = 'not-needed'
        started_at = $null
        completed_at = $null
        removed_packages = @()
        removed_legacy_packages = @()
        result_service = $null
        result_published_inf = $null
        error = $null
    }
    history = @()
}

$targetDescription = 'Sony WH-1000XM5 A2DP service PDO'
$actionDescription = if ($previous.service -eq 'NativeLdacDirectPdo') {
    'Update and verify NativeLdacDirectPdo with automatic rollback to LdacNative on failure'
} else {
    'Install and verify NativeLdacDirectPdo with automatic rollback on failure'
}
if (-not $PSCmdlet.ShouldProcess($targetDescription, $actionDescription)) {
    return
}

New-Item -ItemType Directory -Path $script:logDirectory -Force | Out-Null
Write-DirectPdoJsonAtomic -Value $script:transaction `
    -Path $script:transactionPath
Write-DirectPdoTextAtomic -Value $script:transactionPath `
    -Path (Join-Path $installRoot 'latest-transaction.txt')

try {
    $certificateState = Import-DirectPdoCertificate `
        -CertificatePath $certificatePath
    $script:transaction.candidate.certificate_root_preexisting =
        $certificateState.root_already_present
    $script:transaction.candidate.certificate_publisher_preexisting =
        $certificateState.publisher_already_present
    Add-TransactionHistory -Phase 'certificate-ready' `
        -Message 'Candidate certificate is trusted for this test boot.'

    $installResult = Invoke-DirectPdoPnpUtil `
        -Arguments @('/add-driver', $infPath, '/install') `
        -LogPath (Join-Path $script:logDirectory 'install-package.log')
    $script:transaction.candidate.pnputil_exit_code =
        $installResult.exit_code
    $script:transaction.candidate.reboot_required =
        $installResult.reboot_required
    Add-TransactionHistory -Phase 'package-added' `
        -Message "PnPUtil accepted the package (exit $($installResult.exit_code))."

    $previousPackage = if ($previous.service -eq 'NativeLdacDirectPdo') {
        $previous.published_inf
    } else {
        $null
    }
    $bound = Wait-Xm5A2dpPackageTransition `
        -ExpectedService 'NativeLdacDirectPdo' `
        -PreviousPublishedInf $previousPackage `
        -TimeoutSeconds 30
    if ($null -eq $bound) {
        throw 'Windows did not bind the new NativeLdacDirectPdo package within 30 seconds.'
    }
    $script:transaction.candidate.published_inf = $bound.published_inf
    $script:transaction.validation.service_bound = $true
    $script:transaction.validation.pnp_problem_code = $bound.problem_code
    if ($bound.problem_code -ne 0) {
        throw "NativeLdacDirectPdo bound with PnP problem code $($bound.problem_code)."
    }
    Add-TransactionHistory -Phase 'driver-bound' `
        -Message "NativeLdacDirectPdo is bound as $($bound.published_inf)."

    $probePath = Join-Path $CandidatePath 'audio_endpoint_probe.exe'
    $deadline = (Get-Date).AddSeconds(30)
    $probeOutput = @()
    $probeTranscript = @()
    $probeExitCode = -1
    $healthySamples = 0
    do {
        $previousPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $probeOutput = @(& $probePath --direct-status 2>&1)
            $probeExitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $previousPreference
        }
        $probeText = $probeOutput -join [Environment]::NewLine
        $sampleHealthy = Test-DirectPdoRuntimeStatusText `
            -StatusText $probeText `
            -ExitCode $probeExitCode
        $probeTranscript += "[$((Get-Date).ToString('o'))] exit $probeExitCode"
        $probeTranscript += $probeOutput
        if ($sampleHealthy) {
            $healthySamples++
        } else {
            $healthySamples = 0
        }
        if ($healthySamples -ge 3) {
            break
        }
        Start-Sleep -Milliseconds 750
    } while ((Get-Date) -lt $deadline)
    $probeTranscript | Set-Content -LiteralPath `
        (Join-Path $script:logDirectory 'direct-status.log') -Encoding UTF8
    if ($healthySamples -lt 3) {
        throw "The bound driver did not sustain a healthy PCM ABI 2 / Direct-PDO Media ABI 3 runtime.`n$probeText"
    }
    $script:transaction.validation.direct_media_abi = $true
    $script:transaction.status = 'committed'
    Add-TransactionHistory -Phase 'committed' `
        -Message 'Driver binding and coordinated runtime ABI validation passed.'

    Write-Host 'NativeLdacDirectPdo candidate installation committed.'
    Write-Host "Bound package: $($bound.published_inf)"
    Write-Host "Transaction: $script:transactionPath"
    Write-Host 'The old root virtual endpoint was intentionally retained for this first hardware trial.'
} catch {
    $installFailure = $_.Exception.Message
    $rollbackSucceeded = Invoke-AutomaticRollback -Reason $installFailure
    if ($rollbackSucceeded) {
        throw "Direct-PDO installation failed and was rolled back safely: $installFailure Transaction: $script:transactionPath"
    }
    throw "Direct-PDO installation failed and automatic rollback is incomplete: $installFailure Transaction: $script:transactionPath"
}
