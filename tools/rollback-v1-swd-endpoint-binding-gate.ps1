# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1SwdEndpointBindingRollback,
    [Parameter(Mandatory = $true)][string]$ResultPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-swd-endpoint-binding-common.ps1')

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this rollback from an elevated PowerShell 7 terminal.'
    }
}

function Get-DevicePropertyData {
    param(
        [Parameter(Mandatory = $true)][string]$InstanceId,
        [Parameter(Mandatory = $true)][string]$KeyName
    )
    $property = Get-PnpDeviceProperty -InstanceId $InstanceId `
        -KeyName $KeyName -ErrorAction SilentlyContinue
    if ($null -eq $property -or
        $null -eq $property.PSObject.Properties['Data']) {
        return $null
    }
    return $property.Data
}

function Get-IsolatedDevice {
    $device = Get-PnpDevice `
        -InstanceId $script:V1SwdEndpointBindingInstanceId `
        -ErrorAction SilentlyContinue
    if ($null -eq $device) {
        return $null
    }
    return [pscustomobject][ordered]@{
        instance_id = [string]$device.InstanceId
        present = [bool]$device.Present
        service = [string](Get-DevicePropertyData `
            -InstanceId $device.InstanceId `
            -KeyName 'DEVPKEY_Device_Service')
        published_inf = [string](Get-DevicePropertyData `
            -InstanceId $device.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverInfPath')
        container_id = [string](Get-DevicePropertyData `
            -InstanceId $device.InstanceId `
            -KeyName 'DEVPKEY_Device_ContainerId')
        parent = [string](Get-DevicePropertyData `
            -InstanceId $device.InstanceId `
            -KeyName 'DEVPKEY_Device_Parent')
        hardware_ids = @(Get-DevicePropertyData `
            -InstanceId $device.InstanceId `
            -KeyName 'DEVPKEY_Device_HardwareIds' | Where-Object {
                -not [string]::IsNullOrWhiteSpace([string]$_)
            })
    }
}

function Get-IsolatedPackages {
    return @(Get-WindowsDriver -Online -All | Where-Object {
        (Split-Path -Leaf ([string]$_.OriginalFileName)) -ieq
            $script:V1SwdEndpointBindingOriginalInf
    } | ForEach-Object {
        [pscustomobject][ordered]@{
            published_inf = [string]$_.Driver
            original_file_name =
                (Split-Path -Leaf ([string]$_.OriginalFileName))
            provider_name = [string]$_.ProviderName
            class_name = [string]$_.ClassName
            version = [string]$_.Version
        }
    })
}

function Invoke-PnpUtil {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $text = @(& pnputil.exe @Arguments 2>&1)
    return [pscustomobject][ordered]@{
        exit_code = $LASTEXITCODE
        text = @($text)
    }
}

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The SWD endpoint binding rollback requires PowerShell 7.'
}
Assert-Administrator
if (-not $ConfirmV1SwdEndpointBindingRollback) {
    throw 'Refusing to remove isolated endpoint state without -ConfirmV1SwdEndpointBindingRollback.'
}

$ResultPath = [IO.Path]::GetFullPath($ResultPath)
if (-not (Test-Path -LiteralPath $ResultPath -PathType Leaf)) {
    throw "The failed endpoint-binding result does not exist: $ResultPath"
}
$failed = Get-Content -LiteralPath $ResultPath -Raw | ConvertFrom-Json
if ($failed.passed -ne $false -or
    [string]$failed.staged_published_inf -notmatch '^oem\d+\.inf$' -or
    [string]$failed.expected_parent -notmatch '^BTHENUM\\' -or
    [string]$failed.expected_container -notmatch '^\{[0-9A-Fa-f-]{36}\}$') {
    throw 'The supplied result is not a bounded failed endpoint-binding transaction.'
}
$publishedInf = [string]$failed.staged_published_inf
$device = Get-IsolatedDevice
$packages = @(Get-IsolatedPackages)
if ($packages.Count -ne 1 -or
    [string]$packages[0].published_inf -ine $publishedInf) {
    throw 'The current isolated package does not match the failed transaction.'
}
if ($null -ne $device) {
    $hardwareIds = @($device.hardware_ids)
    if ($device.present -eq $true -or
        [string]$device.instance_id -ine
            $script:V1SwdEndpointBindingInstanceId -or
        [string]$device.service -cne $script:V1SwdEndpointBindingService -or
        [string]$device.published_inf -ine $publishedInf -or
        [string]$device.parent -ine [string]$failed.expected_parent -or
        [string]$device.container_id -ine
            [string]$failed.expected_container -or
        $hardwareIds.Count -ne 1 -or
        [string]$hardwareIds[0] -ine
            $script:V1SwdEndpointBindingHardwareId) {
        throw 'The registered SWD device is active or does not match the failed transaction.'
    }
}

$directory = Split-Path -Parent $ResultPath
$rollbackPath = Join-Path $directory 'rollback-result.json'
$rollback = [ordered]@{
    schema_version = 1
    policy_version = $script:V1SwdEndpointBindingPolicyVersion
    source_result = $ResultPath
    started_at = (Get-Date).ToString('o')
    completed_at = $null
    status = 'prepared'
    device_before = $device
    package_before = $packages[0]
    device_remove = $null
    package_remove = $null
    device_absent = $false
    package_absent = $false
    safety = [ordered]@{
        exact_swd_instance_only = $true
        exact_failed_oem_inf_only = $true
        current_root_endpoint_touched = $false
        transport_driver_touched = $false
        pnp_restarted = $false
        bluetooth_toggled = $false
        endpoint_written = $false
        audio_playback_started = $false
    }
    error = $null
}

Write-Host 'V1 isolated endpoint rollback preflight passed.'
Write-Host "Failed transaction package: $publishedInf"
Write-Host 'Only the exact non-present SWD candidate instance and its isolated package will be removed.'
if (-not $PSCmdlet.ShouldProcess(
        "$($script:V1SwdEndpointBindingInstanceId) / $publishedInf",
        'Remove the failed isolated SWD device instance and its exact driver package')) {
    return
}

try {
    $rollback.status = 'running'
    if ($null -ne $device) {
        $deviceRemove = Invoke-PnpUtil -Arguments @(
            '/remove-device',
            $script:V1SwdEndpointBindingInstanceId)
        $deviceRemove.text | Set-Content -LiteralPath (
            Join-Path $directory 'rollback-remove-device.log') `
            -Encoding utf8NoBOM
        $rollback.device_remove = $deviceRemove
        if ([int]$deviceRemove.exit_code -ne 0) {
            throw "The exact SWD device removal failed with exit $($deviceRemove.exit_code)."
        }
    }
    $deviceDeadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([DateTime]::UtcNow -lt $deviceDeadline -and
        $null -ne (Get-IsolatedDevice)) {
        Start-Sleep -Milliseconds 200
    }
    $rollback.device_absent = $null -eq (Get-IsolatedDevice)
    if (-not $rollback.device_absent) {
        throw 'The exact SWD device instance remained registered after removal.'
    }

    $packageRemove = Invoke-PnpUtil -Arguments @(
        '/delete-driver', $publishedInf, '/force')
    $packageRemove.text | Set-Content -LiteralPath (
        Join-Path $directory 'rollback-remove-package.log') `
        -Encoding utf8NoBOM
    $rollback.package_remove = $packageRemove
    if ([int]$packageRemove.exit_code -ne 0) {
        throw "The exact isolated package removal failed with exit $($packageRemove.exit_code)."
    }
    $packageDeadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([DateTime]::UtcNow -lt $packageDeadline -and
        @(Get-IsolatedPackages).Count -ne 0) {
        Start-Sleep -Milliseconds 200
    }
    $rollback.package_absent = @(Get-IsolatedPackages).Count -eq 0
    if (-not $rollback.package_absent) {
        throw 'The isolated package remained in the Driver Store.'
    }
    $rollback.status = 'passed'
} catch {
    $rollback.status = 'failed'
    $rollback.error = $_.Exception.Message
} finally {
    $rollback.completed_at = (Get-Date).ToString('o')
    $rollback | ConvertTo-Json -Depth 10 | Set-Content `
        -LiteralPath $rollbackPath -Encoding utf8NoBOM
}

if ($rollback.status -ne 'passed') {
    throw "V1 isolated endpoint rollback failed: $($rollback.error) Result: $rollbackPath"
}
Write-Host 'V1 isolated endpoint rollback passed.'
Write-Host 'The exact failed SWD device instance and isolated package are absent.'
Write-Host 'The current ROOT endpoint, LdacNative transport, Bluetooth, and audio settings were untouched.'
Write-Host "Result: $rollbackPath"
