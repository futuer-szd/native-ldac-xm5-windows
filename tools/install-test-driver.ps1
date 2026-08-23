# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$ConfirmDriverReplacement
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script from an elevated Windows PowerShell.'
    }
}

Assert-Administrator
if (-not $ConfirmDriverReplacement) {
    throw 'Refusing to replace the XM5 A2DP profile driver. Re-run with -ConfirmDriverReplacement.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$stageScript = Join-Path $PSScriptRoot 'stage-test-package.ps1'
$backupScript = Join-Path $PSScriptRoot 'backup-current-a2dp.ps1'
& $stageScript -Configuration $Configuration

$control = Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control'
if ([string]$control.SystemStartOptions -notmatch '(^|\s)TESTSIGNING(\s|$)') {
    throw 'The current boot is not in TESTSIGNING mode. Enable it and restart first.'
}

$latestBackupFile = Join-Path $projectRoot 'artifacts\driver-test\latest-backup.txt'
$instancePrefix = 'BTHENUM\{0000110B-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
$presentDevices = Get-PnpDevice -PresentOnly
$device = $presentDevices |
    Where-Object { $_.InstanceId.StartsWith($instancePrefix, [StringComparison]::OrdinalIgnoreCase) } |
    Select-Object -First 1
if ($null -eq $device) {
    throw 'The WH-1000XM5 A2DP Sink service PDO is not present.'
}
$currentService = [string](Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_Service').Data

if ($currentService -eq 'LdacNative') {
    $installStatePath = Join-Path $projectRoot 'artifacts\driver-test\install-state.json'
    if (-not (Test-Path -LiteralPath $installStatePath -PathType Leaf)) {
        throw 'LdacNative is already bound, but install-state.json is missing. Restore AltA2DP before updating.'
    }
    $previousInstallState = Get-Content -LiteralPath $installStatePath -Raw | ConvertFrom-Json
    $backupPath = [System.IO.Path]::GetFullPath([string]$previousInstallState.backup_path)
    $backupStatePath = Join-Path $backupPath 'state.json'
    if (-not (Test-Path -LiteralPath $backupStatePath -PathType Leaf)) {
        throw "The original A2DP backup is missing: $backupPath"
    }
    $backupState = Get-Content -LiteralPath $backupStatePath -Raw | ConvertFrom-Json
    if ([string]$backupState.service -eq 'LdacNative') {
        throw "Refusing to use an LdacNative package as the rollback backup: $backupPath"
    }
    Write-Host "Updating LdacNative; preserving original rollback backup: $backupPath"
} else {
    & $backupScript
    $backupPath = (Get-Content -LiteralPath $latestBackupFile -Raw).Trim()
    if (-not (Test-Path -LiteralPath (Join-Path $backupPath 'state.json'))) {
        throw 'The current A2DP backup did not complete.'
    }
}

$packagePath = Join-Path $projectRoot 'artifacts\driver-test\package'
$certificatePath = Join-Path $packagePath 'LdacNative.cer'
$infPath = Join-Path $packagePath 'LdacNative.inf'
$targetDescription = 'Sony WH-1000XM5 A2DP Sink profile driver'
if (-not $PSCmdlet.ShouldProcess($targetDescription, 'Install and bind LdacNative test driver')) {
    return
}

$rootCertificate = Import-Certificate -FilePath $certificatePath -CertStoreLocation 'Cert:\LocalMachine\Root'
$publisherCertificate = Import-Certificate -FilePath $certificatePath -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher'

$installOutput = & pnputil.exe /add-driver $infPath /install 2>&1
$installExitCode = $LASTEXITCODE
$installLog = Join-Path $projectRoot 'artifacts\driver-test\pnputil-install.log'
$installOutput | Set-Content -LiteralPath $installLog -Encoding UTF8
if ($installExitCode -ne 0) {
    throw "pnputil install failed with exit code $installExitCode. Use restore-a2dp.ps1 with backup $backupPath."
}

Start-Sleep -Seconds 2
$presentDevices = Get-PnpDevice -PresentOnly
$device = $presentDevices |
    Where-Object { $_.InstanceId.StartsWith($instancePrefix, [StringComparison]::OrdinalIgnoreCase) } |
    Select-Object -First 1
if ($null -eq $device) {
    throw "The XM5 service PDO disappeared. Use restore-a2dp.ps1 with backup $backupPath."
}
$service = (Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_Service').Data
$publishedInf = (Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_DriverInfPath').Data
$problemCodeProperty = Get-PnpDeviceProperty `
    -InstanceId $device.InstanceId `
    -KeyName 'DEVPKEY_Device_ProblemCode' `
    -ErrorAction SilentlyContinue
$problemCode = if ($null -eq $problemCodeProperty) {
    0
} else {
    [int]$problemCodeProperty.Data
}

$installState = [ordered]@{
    installed_at = (Get-Date).ToString('o')
    instance_id = $device.InstanceId
    service = [string]$service
    published_inf = [string]$publishedInf
    backup_path = $backupPath
    certificate_thumbprint = $rootCertificate.Thumbprint
    trusted_publisher_thumbprint = $publisherCertificate.Thumbprint
}
$installState | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $projectRoot 'artifacts\driver-test\install-state.json') -Encoding UTF8

if ([string]$service -ne 'LdacNative') {
    throw "The package was added but Windows kept service '$service'. No forced rank override was attempted; restore is still available at $backupPath."
}
if ($problemCode -eq 38) {
    throw 'LdacNative is bound, but Windows reports device problem code 38 (the previous driver instance did not unload). Restart Windows once before testing; do not repeatedly restart the XM5 device.'
}
if ($problemCode -ne 0) {
    throw "LdacNative is bound, but the XM5 A2DP transport reports PnP problem code $problemCode. Do not start a media trial until the device is healthy."
}

Write-Host "LdacNative is bound to the XM5 service PDO as $publishedInf."
Write-Host "Run: $projectRoot\artifacts\driver-test\transport_probe.exe --info"
Write-Host "Then: $projectRoot\artifacts\driver-test\transport_probe.exe --discover"
Write-Host "Rollback backup: $backupPath"
