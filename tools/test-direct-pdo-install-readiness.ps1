# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$CandidatePath,
    [switch]$AllowDirtyCandidate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'direct-pdo-install-common.ps1')

Assert-DirectPdoAdministrator
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $projectRoot 'artifacts\direct-pdo\candidate'
}
$CandidatePath = [System.IO.Path]::GetFullPath($CandidatePath)
$verifyScript = Join-Path $PSScriptRoot 'verify-direct-pdo-candidate.ps1'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $verifyScript `
    -CandidatePath $CandidatePath
if ($LASTEXITCODE -ne 0) {
    throw "Candidate verification failed with exit code $LASTEXITCODE."
}
$manifest = Get-Content -LiteralPath (Join-Path $CandidatePath 'manifest.json') `
    -Raw | ConvertFrom-Json
if ($manifest.source_dirty -eq $true -and -not $AllowDirtyCandidate) {
    throw 'The candidate was built from a dirty source tree. Rebuild it from a reviewed commit.'
}

$control = Get-ItemProperty -LiteralPath `
    'HKLM:\SYSTEM\CurrentControlSet\Control'
$testSigning = [string]$control.SystemStartOptions -match `
    '(^|\s)TESTSIGNING(\s|$)'
if (-not $testSigning) {
    throw 'The current boot is not in TESTSIGNING mode. No install should be attempted.'
}

$targets = @(Get-PnpDevice -PresentOnly | Where-Object {
    ([string]$_.InstanceId).StartsWith(
        $script:Xm5A2dpInstancePrefix,
        [StringComparison]::OrdinalIgnoreCase)
})
if ($targets.Count -ne 1) {
    throw "Expected one present XM5 A2DP service PDO, found $($targets.Count)."
}
$device = $targets[0]
$service = [string](Get-PnpDeviceProperty -InstanceId $device.InstanceId `
    -KeyName 'DEVPKEY_Device_Service').Data
$publishedInf = [string](Get-PnpDeviceProperty -InstanceId $device.InstanceId `
    -KeyName 'DEVPKEY_Device_DriverInfPath').Data
$problemProperty = Get-PnpDeviceProperty -InstanceId $device.InstanceId `
    -KeyName 'DEVPKEY_Device_ProblemCode' -ErrorAction SilentlyContinue
$problemCode = if ($null -eq $problemProperty) {
    0
} else {
    [int]$problemProperty.Data
}
if ($problemCode -ne 0) {
    throw "The XM5 A2DP service PDO has PnP problem code $problemCode. Repair it before replacing a driver."
}
$containerProperty = Get-PnpDeviceProperty -InstanceId $device.InstanceId `
    -KeyName 'DEVPKEY_Device_ContainerId'
$containerId = ([Guid]::Parse([string]$containerProperty.Data)).ToString('D')
if (-not $containerId.Equals(
        [string]$manifest.remote_container_id,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Candidate Container ID $($manifest.remote_container_id) does not match the present XM5 $containerId."
}

$installStatePath = Join-Path $projectRoot `
    'artifacts\driver-test\install-state.json'
$latestBackupPath = Join-Path $projectRoot `
    'artifacts\driver-test\latest-backup.txt'
if ($service -eq 'LdacNative') {
    if (-not (Test-Path -LiteralPath $installStatePath -PathType Leaf)) {
        throw 'LdacNative is bound but its original rollback state is missing.'
    }
    $installState = Get-Content -LiteralPath $installStatePath -Raw |
        ConvertFrom-Json
    $backupPath = [System.IO.Path]::GetFullPath(
        [string]$installState.backup_path)
    $rollbackService = 'LdacNative'
    $rollbackPublishedInf = [string]$installState.published_inf
} elseif ($service -eq 'NativeLdacDirectPdo') {
    $latestTransactionPath = Join-Path $projectRoot `
        'artifacts\direct-pdo\install\latest-transaction.txt'
    if (-not (Test-Path -LiteralPath $latestTransactionPath -PathType Leaf)) {
        throw 'NativeLdacDirectPdo is bound but its prior install transaction is missing.'
    }
    $priorPath = (Get-Content -LiteralPath $latestTransactionPath -Raw).Trim()
    $priorRecord = Read-DirectPdoTransaction -Path $priorPath
    $prior = $priorRecord.transaction
    if ([string]$prior.status -ne 'committed' -or
        -not ([string]$prior.target.instance_id).Equals(
            [string]$device.InstanceId,
            [StringComparison]::OrdinalIgnoreCase) -or
        -not ([string]$prior.candidate.published_inf).Equals(
            $publishedInf,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The bound Direct-PDO package does not match the latest committed transaction.'
    }
    $rollbackTarget = $priorRecord.rollback_target
    if ([string]$rollbackTarget.service -ne 'LdacNative' -or
        [string]::IsNullOrWhiteSpace(
            [string]$rollbackTarget.published_inf)) {
        throw 'The prior Direct-PDO transaction has no verified LdacNative rollback target.'
    }
    $rollbackService = [string]$rollbackTarget.service
    $rollbackPublishedInf = [string]$rollbackTarget.published_inf
    $backupPath = [System.IO.Path]::GetFullPath(
        [string]$rollbackTarget.backup_path)
    $legacyPackages = @(Get-DriverPackagesByOriginalInf `
        -InfNames @('LdacNative.inf'))
    if ($legacyPackages.Count -eq 0 -or
        $rollbackPublishedInf -notin @($legacyPackages.Driver)) {
        throw "The recorded LdacNative rollback package $rollbackPublishedInf is no longer staged."
    }
} else {
    if (-not (Test-Path -LiteralPath $latestBackupPath -PathType Leaf)) {
        throw 'No verified original A2DP rollback backup is recorded.'
    }
    $backupPath = [System.IO.Path]::GetFullPath(
        (Get-Content -LiteralPath $latestBackupPath -Raw).Trim())
    $rollbackService = $service
    $rollbackPublishedInf = $publishedInf
}
$backupStatePath = Join-Path $backupPath 'state.json'
if (-not (Test-Path -LiteralPath $backupStatePath -PathType Leaf)) {
    throw "Rollback state is missing: $backupStatePath"
}
$backupState = Get-Content -LiteralPath $backupStatePath -Raw |
    ConvertFrom-Json
if ([string]$backupState.service -in @('LdacNative', 'NativeLdacDirectPdo')) {
    throw "Rollback backup service '$($backupState.service)' is not an original driver."
}
$exportedInfFiles = @(Get-ChildItem -LiteralPath $backupPath -Filter '*.inf' `
    -File -Recurse)
if ($exportedInfFiles.Count -eq 0) {
    throw "Rollback backup contains no exported INF: $backupPath"
}

$task = Get-ScheduledTask -TaskName 'Native LDAC Agent' `
    -ErrorAction SilentlyContinue
if ($task) {
    throw 'The Native LDAC login task must be removed before coordinated driver replacement.'
}
$conflictingProcesses = @(Get-CimInstance Win32_Process | Where-Object {
    $isWorkspaceBinary = $_.Name -in @(
            'ldac_agent.exe',
            'ldac_direct_engine.exe',
            'transport_probe.exe',
            'audio_endpoint_probe.exe'
        ) -and ([string]$_.ExecutablePath).StartsWith(
            $projectRoot,
            [StringComparison]::OrdinalIgnoreCase)
    $isWorkspacePython = $_.Name -in @('python.exe', 'pythonw.exe') -and
        ([string]$_.CommandLine).IndexOf(
            $projectRoot,
            [StringComparison]::OrdinalIgnoreCase) -ge 0
    $isWorkspaceBinary -or $isWorkspacePython
})
if ($conflictingProcesses.Count -ne 0) {
    $summary = @($conflictingProcesses | ForEach-Object {
        "$($_.Name) (PID $($_.ProcessId))"
    })
    throw "Stop LDAC workspace processes before replacement: $($summary -join ', ')"
}

Write-Host 'Direct-PDO install readiness preflight passed.'
Write-Host "Target: $($device.InstanceId)"
Write-Host "Current driver: $publishedInf, service $service, problem code $problemCode"
Write-Host "Matching XM5 Container ID: $containerId"
Write-Host "Verified rollback backup: $backupPath ($($backupState.service))"
Write-Host "Immediate rollback target: $rollbackService, $rollbackPublishedInf"
Write-Host 'TESTSIGNING is active; no installed task or LDAC workspace process was found.'
Write-Host 'This preflight was read-only. No certificate, driver, device, process, or system setting was changed.'
