# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmHfpReenumeration,
    [switch]$InspectOnly
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

function Get-Xm5CaptureEndpoint {
    param(
        [switch]$IncludeNotPresent
    )

    $endpoints = @(Get-PnpDevice -Class AudioEndpoint -ErrorAction SilentlyContinue)
    return @($endpoints | Where-Object {
        ($IncludeNotPresent -or $_.Present -eq $true) -and
        $_.InstanceId -like 'SWD\MMDEVAPI\{0.0.1*' -and
        $_.FriendlyName -like '*WH-1000XM5*'
    })
}

function Get-PresentDeviceByInstanceId {
    param(
        [string]$InstanceId
    )

    $device = Get-PnpDevice -InstanceId $InstanceId -ErrorAction SilentlyContinue
    if ($device -and $device.Present -eq $true) {
        return $device
    }
    return $null
}

$mediaDevices = @(Get-Xm5HandsFreeMediaDevice)
$parentDevices = @(Get-Xm5HandsFreeAgDevice)
$captureEndpoints = @(Get-Xm5CaptureEndpoint)
$knownCaptureEndpoints = @(Get-Xm5CaptureEndpoint -IncludeNotPresent)

if ($InspectOnly) {
    Write-Host "Matching XM5 HFP MEDIA devices: $($mediaDevices.Count)"
    foreach ($device in $mediaDevices) {
        Write-Host "HFP: $($device.FriendlyName) [$($device.InstanceId)], present=$($device.Present), status=$($device.Status)"
    }
    Write-Host "Matching XM5 Hands-Free AG devices: $($parentDevices.Count)"
    foreach ($device in $parentDevices) {
        Write-Host "AG: $($device.FriendlyName) [$($device.InstanceId)], present=$($device.Present), status=$($device.Status)"
    }
    Write-Host "Known XM5 capture endpoints: $($knownCaptureEndpoints.Count)"
    foreach ($endpoint in $knownCaptureEndpoints) {
        Write-Host "Capture: $($endpoint.FriendlyName) [$($endpoint.InstanceId)], present=$($endpoint.Present), status=$($endpoint.Status)"
    }
    Write-Host "Present XM5 capture endpoints: $($captureEndpoints.Count)"
    return
}

Assert-Administrator
if (-not $ConfirmHfpReenumeration) {
    throw 'Refusing to remove and re-enumerate the XM5 Hands-Free child device. Re-run with -ConfirmHfpReenumeration.'
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

if ($mediaDevices.Count -ne 1) {
    $ids = @($mediaDevices | ForEach-Object { $_.InstanceId })
    throw "Expected exactly one XM5 BthHFAud MEDIA device, found $($mediaDevices.Count): $($ids -join ', ')"
}
if ($parentDevices.Count -ne 1) {
    $ids = @($parentDevices | ForEach-Object { $_.InstanceId })
    throw "Expected exactly one XM5 Hands-Free AG parent, found $($parentDevices.Count): $($ids -join ', ')"
}

$mediaDevice = $mediaDevices[0]
$parentDevice = $parentDevices[0]
$target = "$($mediaDevice.FriendlyName) [$($mediaDevice.InstanceId)]"
$action = 'Remove and re-enumerate only the XM5 BthHFAud MEDIA child device'
if (-not $PSCmdlet.ShouldProcess($target, $action)) {
    return
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputRoot = Join-Path $projectRoot 'artifacts\hfp-recovery'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logPath = Join-Path $outputRoot "reenumerate-hfp-$timestamp.log"
$logLines = @(
    "Started: $((Get-Date).ToString('o'))",
    "Target child: $target",
    "Parent: $($parentDevice.FriendlyName) [$($parentDevice.InstanceId)]",
    "Before present capture endpoints: $($captureEndpoints.Count)",
    "Before known capture endpoints: $($knownCaptureEndpoints.Count)"
)

$stackOutput = @(& pnputil.exe /enum-devices /instanceid $mediaDevice.InstanceId /drivers /services /stack 2>&1)
$logLines += '--- Child device stack before removal ---'
$logLines += $stackOutput

$removeOutput = @(& pnputil.exe /remove-device $mediaDevice.InstanceId 2>&1)
$removeExitCode = $LASTEXITCODE
$logLines += "Remove exit code: $removeExitCode"
$logLines += $removeOutput
if ($removeExitCode -ne 0) {
    $logLines | Set-Content -LiteralPath $logPath -Encoding UTF8
    throw "PnPUtil could not remove the XM5 Hands-Free child device. See $logPath"
}

$scanOutput = @(& pnputil.exe /scan-devices 2>&1)
$scanExitCode = $LASTEXITCODE
$logLines += "Initial scan exit code: $scanExitCode"
$logLines += $scanOutput
if ($scanExitCode -ne 0) {
    $logLines | Set-Content -LiteralPath $logPath -Encoding UTF8
    throw "PnPUtil could not scan for devices after removing the HFP child. See $logPath"
}

$restoredMediaDevice = $null
for ($attempt = 0; $attempt -lt 10; $attempt++) {
    Start-Sleep -Seconds 1
    $restoredMediaDevice = Get-PresentDeviceByInstanceId -InstanceId $mediaDevice.InstanceId
    if ($restoredMediaDevice) {
        break
    }
}

if (-not $restoredMediaDevice) {
    $logLines += 'The child did not return after the initial scan; restarting the exact Hands-Free AG parent.'
    $parentRestartOutput = @(& pnputil.exe /restart-device $parentDevice.InstanceId 2>&1)
    $parentRestartExitCode = $LASTEXITCODE
    $logLines += "Parent restart exit code: $parentRestartExitCode"
    $logLines += $parentRestartOutput
    if ($parentRestartExitCode -ne 0) {
        $logLines | Set-Content -LiteralPath $logPath -Encoding UTF8
        throw "The HFP child was removed, but the Hands-Free AG parent could not be restarted. See $logPath"
    }

    $secondScanOutput = @(& pnputil.exe /scan-devices 2>&1)
    $secondScanExitCode = $LASTEXITCODE
    $logLines += "Second scan exit code: $secondScanExitCode"
    $logLines += $secondScanOutput
    if ($secondScanExitCode -ne 0) {
        $logLines | Set-Content -LiteralPath $logPath -Encoding UTF8
        throw "The HFP parent restarted, but the second device scan failed. See $logPath"
    }

    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        Start-Sleep -Seconds 1
        $restoredMediaDevice = Get-PresentDeviceByInstanceId -InstanceId $mediaDevice.InstanceId
        if ($restoredMediaDevice) {
            break
        }
    }
}

if (-not $restoredMediaDevice) {
    $matchingMediaDevices = @(Get-Xm5HandsFreeMediaDevice)
    if ($matchingMediaDevices.Count -eq 1 -and $matchingMediaDevices[0].Present -eq $true) {
        $restoredMediaDevice = $matchingMediaDevices[0]
    }
}

$restoredCaptureEndpoints = @()
if ($restoredMediaDevice) {
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        Start-Sleep -Seconds 1
        foreach ($knownEndpoint in $knownCaptureEndpoints) {
            $candidate = Get-PresentDeviceByInstanceId -InstanceId $knownEndpoint.InstanceId
            if ($candidate) {
                $restoredCaptureEndpoints = @($candidate)
                break
            }
        }
        if ($restoredCaptureEndpoints.Count -eq 0 -and ($attempt % 5) -eq 4) {
            $restoredCaptureEndpoints = @(Get-Xm5CaptureEndpoint)
        }
        if ($restoredCaptureEndpoints.Count -ne 0) {
            break
        }
    }
}

$logLines += "Completed: $((Get-Date).ToString('o'))"
$logLines += "Restored HFP MEDIA child: $([bool]$restoredMediaDevice)"
if ($restoredMediaDevice) {
    $logLines += "Restored child: $($restoredMediaDevice.FriendlyName) [$($restoredMediaDevice.InstanceId)]"
}
$logLines += "After present capture endpoints: $($restoredCaptureEndpoints.Count)"
foreach ($endpoint in $restoredCaptureEndpoints) {
    $logLines += "Capture: $($endpoint.FriendlyName) [$($endpoint.InstanceId)]"
}
$logLines | Set-Content -LiteralPath $logPath -Encoding UTF8

Write-Host "Log: $logPath"
if (-not $restoredMediaDevice) {
    Write-Warning 'The XM5 HFP child was removed, but Windows did not re-enumerate it. The Bluetooth pairing was not deleted.'
    exit 2
}
Write-Host "Re-enumerated HFP child: $($restoredMediaDevice.FriendlyName) [$($restoredMediaDevice.InstanceId)]"
if ($restoredCaptureEndpoints.Count -eq 0) {
    Write-Warning 'The XM5 HFP child returned, but Windows still did not create a present capture endpoint.'
    exit 2
}
Write-Host "Restored microphone endpoint: $($restoredCaptureEndpoints[0].FriendlyName)"
