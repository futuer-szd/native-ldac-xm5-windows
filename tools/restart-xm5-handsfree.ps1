# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmHfpRestart,
    [switch]$InspectOnly,
    [switch]$RestartParent
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

function Get-Xm5BluetoothAddress {
    $pairedRoot =
        'HKLM:\SYSTEM\CurrentControlSet\Services\BthPort\Parameters\Devices'
    $matches = @()
    if (Test-Path -LiteralPath $pairedRoot) {
        foreach ($key in Get-ChildItem -LiteralPath $pairedRoot) {
            $values = Get-ItemProperty -LiteralPath $key.PSPath
            if ($null -eq $values.Name) {
                continue
            }
            $name = [Text.Encoding]::UTF8.GetString($values.Name).
                Trim([char]0)
            if ($name -eq 'WH-1000XM5' -and
                $key.PSChildName -match '^[0-9A-Fa-f]{12}$') {
                $matches += $key.PSChildName.ToUpperInvariant()
            }
        }
    }
    $matches = @($matches | Select-Object -Unique)
    if ($matches.Count -ne 1) {
        throw "Expected one paired WH-1000XM5 address, found $($matches.Count)."
    }
    return $matches[0]
}

$xm5Address = Get-Xm5BluetoothAddress

function Get-Xm5HandsFreeMediaDevice {
    $foundDevices = @()
    $mediaDevices = @(Get-PnpDevice -Class MEDIA -ErrorAction SilentlyContinue)
    foreach ($device in $mediaDevices) {
        $service = Get-PnpDeviceProperty `
            -InstanceId $device.InstanceId `
            -KeyName 'DEVPKEY_Device_Service' `
            -ErrorAction SilentlyContinue
        $hardwareIdProperty = Get-PnpDeviceProperty `
            -InstanceId $device.InstanceId `
            -KeyName 'DEVPKEY_Device_HardwareIds' `
            -ErrorAction SilentlyContinue
        $parent = Get-PnpDeviceProperty `
            -InstanceId $device.InstanceId `
            -KeyName 'DEVPKEY_Device_Parent' `
            -ErrorAction SilentlyContinue
        $hardwareIds = if ($hardwareIdProperty) {
            @($hardwareIdProperty.Data)
        } else {
            @()
        }
        $serviceName = if ($service) { [string]$service.Data } else { '' }
        $parentId = if ($parent) { [string]$parent.Data } else { '' }
        if ($serviceName -eq 'BthHFAud' -and
            $hardwareIds -icontains 'BTHHFENUM\BthHFPAudio' -and
            $parentId -like "*$xm5Address*") {
            $foundDevices += $device
        }
    }
    return $foundDevices
}

function Get-Xm5CaptureEndpoint {
    $endpoints = @(Get-PnpDevice -Class AudioEndpoint -ErrorAction SilentlyContinue)
    return @($endpoints | Where-Object {
        $_.Present -eq $true -and
        $_.InstanceId -like 'SWD\MMDEVAPI\{0.0.1*' -and
        $_.FriendlyName -like '*WH-1000XM5*'
    })
}

function Get-Xm5HandsFreeAgDevice {
    $foundDevices = @()
    $systemDevices = @(Get-PnpDevice -Class System -ErrorAction SilentlyContinue)
    foreach ($device in $systemDevices) {
        if ($device.Service -eq 'BthHFEnum' -and
            $device.InstanceId -like 'BTHENUM\{0000111E-0000-1000-8000-00805F9B34FB}*' -and
            $device.InstanceId -like "*$xm5Address*") {
            $foundDevices += $device
        }
    }
    return $foundDevices
}

function Get-Xm5RestartTargetDevice {
    param(
        [bool]$Parent
    )

    if ($Parent) {
        Get-Xm5HandsFreeAgDevice
    } else {
        Get-Xm5HandsFreeMediaDevice
    }
}

if ($InspectOnly) {
    $inspectionDevices = @(Get-Xm5HandsFreeMediaDevice)
    $inspectionParents = @(Get-Xm5HandsFreeAgDevice)
    $inspectionTargets = @(Get-Xm5RestartTargetDevice -Parent ([bool]$RestartParent))
    $inspectionCapture = @(Get-Xm5CaptureEndpoint)
    Write-Host "Matching XM5 HFP MEDIA devices: $($inspectionDevices.Count)"
    foreach ($inspectionDevice in $inspectionDevices) {
        Write-Host "HFP: $($inspectionDevice.FriendlyName) [$($inspectionDevice.InstanceId)]"
    }
    Write-Host "Matching XM5 Hands-Free AG devices: $($inspectionParents.Count)"
    foreach ($inspectionParent in $inspectionParents) {
        Write-Host "AG: $($inspectionParent.FriendlyName) [$($inspectionParent.InstanceId)]"
    }
    $inspectionTargetKind = if ($RestartParent) { 'Hands-Free AG' } else { 'HFP MEDIA' }
    Write-Host "Selected $inspectionTargetKind restart targets: $($inspectionTargets.Count)"
    Write-Host "Present XM5 capture endpoints: $($inspectionCapture.Count)"
    foreach ($inspectionEndpoint in $inspectionCapture) {
        Write-Host "Capture: $($inspectionEndpoint.FriendlyName) [$($inspectionEndpoint.InstanceId)]"
    }
    return
}

Assert-Administrator
if (-not $ConfirmHfpRestart) {
    throw 'Refusing to restart the XM5 Hands-Free device. Re-run with -ConfirmHfpRestart.'
}

$activeProbe = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('transport_probe.exe', 'audio_endpoint_probe.exe')
})
if ($activeProbe.Count -ne 0) {
    $processSummary = @($activeProbe | ForEach-Object {
        "$($_.Name) (PID $($_.ProcessId))"
    })
    throw "Stop the LDAC/probe session first: $($processSummary -join ', ')"
}

$devices = @(Get-Xm5RestartTargetDevice -Parent ([bool]$RestartParent))
if ($devices.Count -ne 1) {
    $ids = @($devices | ForEach-Object { $_.InstanceId })
    $deviceKind = if ($RestartParent) { 'Hands-Free AG' } else { 'BthHFAud MEDIA' }
    throw "Expected exactly one XM5 $deviceKind device, found $($devices.Count): $($ids -join ', ')"
}

$device = $devices[0]
$target = "$($device.FriendlyName) [$($device.InstanceId)]"
$restartAction = if ($RestartParent) {
    'Restart the XM5 Hands-Free AG parent PnP device'
} else {
    'Restart the XM5 Hands-Free audio PnP device'
}
if (-not $PSCmdlet.ShouldProcess($target, $restartAction)) {
    return
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputRoot = Join-Path $projectRoot 'artifacts\hfp-recovery'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logPrefix = if ($RestartParent) { 'restart-parent' } else { 'restart-audio' }
$logPath = Join-Path $outputRoot "$logPrefix-$timestamp.log"

$logLines = @(
    "Started: $((Get-Date).ToString('o'))",
    "Action: $restartAction",
    "Target: $target",
    "Before capture endpoints: $(@(Get-Xm5CaptureEndpoint).Count)"
)
$restartOutput = @(& pnputil.exe /restart-device $device.InstanceId 2>&1)
$restartExitCode = $LASTEXITCODE
$logLines += "PnPUtil exit code: $restartExitCode"
$logLines += $restartOutput
$logLines | Set-Content -LiteralPath $logPath -Encoding UTF8
if ($restartExitCode -ne 0) {
    throw "PnPUtil could not restart the XM5 Hands-Free device. See $logPath"
}

$captureEndpoints = @()
for ($attempt = 0; $attempt -lt 15; $attempt++) {
    Start-Sleep -Seconds 1
    $captureEndpoints = @(Get-Xm5CaptureEndpoint)
    if ($captureEndpoints.Count -ne 0) {
        break
    }
}

$completionLines = @(
    "Completed: $((Get-Date).ToString('o'))",
    "After capture endpoints: $($captureEndpoints.Count)"
)
foreach ($endpoint in $captureEndpoints) {
    $completionLines += "Capture: $($endpoint.FriendlyName) [$($endpoint.InstanceId)]"
}
$completionLines | Add-Content -LiteralPath $logPath -Encoding UTF8

Write-Host "Restarted: $target"
Write-Host "Log: $logPath"
if ($captureEndpoints.Count -eq 0) {
    $restartedDeviceKind = if ($RestartParent) { 'Hands-Free AG parent' } else { 'HFP MEDIA device' }
    Write-Warning "The XM5 $restartedDeviceKind restarted successfully, but Windows did not recreate the capture endpoint within 15 seconds."
    exit 2
}
Write-Host "Restored microphone endpoint: $($captureEndpoints[0].FriendlyName)"
