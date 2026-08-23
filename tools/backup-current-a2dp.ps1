# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$OutputRoot
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
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $projectRoot 'artifacts\driver-test\backup'
}
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backupPath = Join-Path $OutputRoot $timestamp
New-Item -ItemType Directory -Path $backupPath -Force | Out-Null

$instancePrefix = 'BTHENUM\{0000110B-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
$presentDevices = Get-PnpDevice -PresentOnly
$device = $presentDevices |
    Where-Object { $_.InstanceId.StartsWith($instancePrefix, [StringComparison]::OrdinalIgnoreCase) } |
    Select-Object -First 1
if ($null -eq $device) {
    throw 'The WH-1000XM5 A2DP Sink service PDO is not present.'
}

$infProperty = Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_DriverInfPath'
$serviceProperty = Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_Service'
$providerProperty = Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_DriverProvider'
$versionProperty = Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_DriverVersion'
$publishedInf = [string]$infProperty.Data
if ([string]::IsNullOrWhiteSpace($publishedInf)) {
    throw 'The current A2DP driver INF could not be determined.'
}

$pnputilOutput = & pnputil.exe /export-driver $publishedInf $backupPath 2>&1
$pnputilExitCode = $LASTEXITCODE
$pnputilOutput | Set-Content -LiteralPath (Join-Path $backupPath 'pnputil-export.log') -Encoding UTF8
if ($pnputilExitCode -ne 0) {
    throw "pnputil failed to export $publishedInf (exit $pnputilExitCode)."
}

$state = [ordered]@{
    captured_at = (Get-Date).ToString('o')
    instance_id = $device.InstanceId
    friendly_name = $device.FriendlyName
    status = [string]$device.Status
    published_inf = $publishedInf
    service = [string]$serviceProperty.Data
    provider = [string]$providerProperty.Data
    driver_version = [string]$versionProperty.Data
    backup_path = $backupPath
}
$state | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backupPath 'state.json') -Encoding UTF8

$latestPath = Join-Path $projectRoot 'artifacts\driver-test\latest-backup.txt'
New-Item -ItemType Directory -Path (Split-Path -Parent $latestPath) -Force | Out-Null
Set-Content -LiteralPath $latestPath -Value $backupPath -Encoding UTF8

Write-Host "Backed up $publishedInf ($($state.provider), service $($state.service))."
Write-Host "Backup path: $backupPath"
