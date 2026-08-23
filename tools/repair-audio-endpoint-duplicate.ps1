# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmDuplicateRemoval
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

function Find-DevCon {
    $candidates = @(
        'C:\Program Files (x86)\Windows Kits\10\Tools\10.0.26100.0\x64\devcon.exe',
        'C:\Program Files (x86)\Windows Kits\10\Tools\x64\devcon.exe'
    )
    return $candidates |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
}

function Get-NativeLdacAudioDevices {
    $matches = @()
    $mediaDevices = @(Get-PnpDevice -Class MEDIA -ErrorAction SilentlyContinue)
    foreach ($device in $mediaDevices) {
        $hardwareIdProperty = Get-PnpDeviceProperty `
            -InstanceId $device.InstanceId `
            -KeyName 'DEVPKEY_Device_HardwareIds' `
            -ErrorAction SilentlyContinue
        if ($null -ne $hardwareIdProperty) {
            $hardwareIds = @($hardwareIdProperty.Data)
            if ($hardwareIds -icontains 'ROOT\NativeLdacAudio') {
                $matches += $device
            }
        }
    }
    return $matches
}

function Get-ProblemCode {
    param([Parameter(Mandatory)][string]$InstanceId)

    $property = Get-PnpDeviceProperty `
        -InstanceId $InstanceId `
        -KeyName 'DEVPKEY_Device_ProblemCode' `
        -ErrorAction SilentlyContinue
    if ($null -eq $property -or $null -eq $property.Data) {
        return [uint32]0xFFFFFFFF
    }
    return [uint32]$property.Data
}

Assert-Administrator
if (-not $ConfirmDuplicateRemoval) {
    throw 'Refusing to remove a device instance. Re-run with -ConfirmDuplicateRemoval.'
}

$devconPath = Find-DevCon
if (-not $devconPath) {
    throw 'The x64 WDK devcon.exe was not found.'
}

$devices = @(Get-NativeLdacAudioDevices)
if ($devices.Count -lt 2) {
    throw "Duplicate repair expected at least two NativeLdacAudio devices, but found $($devices.Count). No change was made."
}

$deviceStates = @()
foreach ($device in $devices) {
    $problemCode = Get-ProblemCode -InstanceId $device.InstanceId
    $deviceStates += [pscustomobject]@{
        Device = $device
        InstanceId = [string]$device.InstanceId
        ProblemCode = $problemCode
    }
}

$healthyDevices = @($deviceStates | Where-Object { $_.ProblemCode -eq 0 })
$failedDevices = @($deviceStates | Where-Object { $_.ProblemCode -ne 0 })
if ($healthyDevices.Count -ne 1 -or $failedDevices.Count -eq 0) {
    $summary = @($deviceStates | ForEach-Object { "$($_.InstanceId) (problem $($_.ProblemCode))" })
    throw "Cannot choose a duplicate safely: $($summary -join ', '). No change was made."
}

$failedIds = @($failedDevices | ForEach-Object { $_.InstanceId })
$target = $failedIds -join ', '
if (-not $PSCmdlet.ShouldProcess($target, 'Remove failed duplicate NativeLdacAudio device instance')) {
    return
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputRoot = Join-Path $projectRoot 'artifacts\audio-endpoint'
$repairLog = Join-Path $outputRoot 'duplicate-repair.log'
$logLines = @()
$rebootRequired = $false
foreach ($failedDevice in $failedDevices) {
    $instanceArgument = "@$($failedDevice.InstanceId)"
    $removeOutput = @(& $devconPath remove $instanceArgument 2>&1)
    $removeExitCode = $LASTEXITCODE
    $logLines += "[$($failedDevice.InstanceId)] exit code $removeExitCode"
    $logLines += $removeOutput
    if ($removeExitCode -eq 1) {
        $rebootRequired = $true
    } elseif ($removeExitCode -ne 0) {
        $logLines | Set-Content -LiteralPath $repairLog -Encoding UTF8
        throw "DevCon failed to remove $($failedDevice.InstanceId) with exit code $removeExitCode. See $repairLog"
    }
}
$logLines | Set-Content -LiteralPath $repairLog -Encoding UTF8

Start-Sleep -Seconds 2
$remainingDevices = @(Get-NativeLdacAudioDevices)
if ($remainingDevices.Count -ne 1) {
    $remainingIds = @($remainingDevices | ForEach-Object { $_.InstanceId })
    throw "Duplicate removal completed, but $($remainingDevices.Count) device instances remain: $($remainingIds -join ', '). See $repairLog"
}

$remainingProblemCode = Get-ProblemCode -InstanceId $remainingDevices[0].InstanceId
if ($remainingProblemCode -ne 0) {
    throw "The remaining device $($remainingDevices[0].InstanceId) has problem code $remainingProblemCode. See $repairLog"
}

Write-Host "Removed failed duplicate: $target"
Write-Host "Preserved healthy endpoint: $($remainingDevices[0].InstanceId)"
Write-Host "Log: $repairLog"
if ($rebootRequired) {
    Write-Host 'DevCon also reports that a reboot is required.'
} else {
    Write-Host 'The earlier driver update still requested a reboot; restart Windows once now.'
}
