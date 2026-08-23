# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$NamePattern = 'WH-1000XM5',
    [switch]$AsJson
)

$ErrorActionPreference = 'Stop'
$a2dpSinkUuid = '0000110B-0000-1000-8000-00805F9B34FB'

function Get-DevicePropertyValue {
    param(
        [Parameter(Mandatory)]
        [string]$InstanceId,
        [Parameter(Mandatory)]
        [string]$KeyName
    )

    $property = Get-PnpDeviceProperty -InstanceId $InstanceId -KeyName $KeyName `
        -ErrorAction SilentlyContinue
    if ($null -eq $property) {
        return $null
    }
    return $property.Data
}

try {
    $presentDevices = Get-PnpDevice -PresentOnly
} catch {
    throw "Get-PnpDevice 失败。请在管理员 PowerShell 中重试。原始错误：$($_.Exception.Message)"
}

$namedDevices = $presentDevices | Where-Object {
    $_.FriendlyName -like "*$NamePattern*"
}
$a2dpDevices = $namedDevices | Where-Object {
    $_.InstanceId -like "BTHENUM\{$a2dpSinkUuid}*"
}
$a2dpDevice = $a2dpDevices | Select-Object -First 1

$pairedRoot = 'HKLM:\SYSTEM\CurrentControlSet\Services\BthPort\Parameters\Devices'
$pairedOutput = @()
if (Test-Path -LiteralPath $pairedRoot) {
    $pairedKeys = Get-ChildItem -LiteralPath $pairedRoot
    foreach ($pairedKey in $pairedKeys) {
        $values = Get-ItemProperty -LiteralPath $pairedKey.PSPath
        $decodedName = $null
        if ($null -ne $values.Name) {
            $decodedName = [Text.Encoding]::UTF8.GetString($values.Name).Trim([char]0)
        }
        if ($decodedName -like "*$NamePattern*") {
            $pairedOutput += [pscustomobject]@{
                Name = $decodedName
                BluetoothAddress = $pairedKey.PSChildName.ToUpperInvariant()
                VendorId = if ($null -ne $values.VID) { ('0x{0:X4}' -f $values.VID) } else { $null }
                ProductId = if ($null -ne $values.PID) { ('0x{0:X4}' -f $values.PID) } else { $null }
            }
        }
    }
}

$result = [ordered]@{
    ProbeVersion = 1
    NamePattern = $NamePattern
    A2dpSinkUuid = $a2dpSinkUuid
    A2dpServicePdoFound = ($null -ne $a2dpDevice)
    A2dpDevice = $null
    PairedDevices = $pairedOutput
    Conclusion = 'A profile driver can bind to the remote A2DP Sink PDO and reuse Windows BthPort.'
}

if ($null -ne $a2dpDevice) {
    $result.A2dpDevice = [ordered]@{
        FriendlyName = $a2dpDevice.FriendlyName
        Status = [string]$a2dpDevice.Status
        Class = [string]$a2dpDevice.Class
        InstanceId = $a2dpDevice.InstanceId
        Service = Get-DevicePropertyValue -InstanceId $a2dpDevice.InstanceId `
            -KeyName 'DEVPKEY_Device_Service'
        DriverInfPath = Get-DevicePropertyValue -InstanceId $a2dpDevice.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverInfPath'
        DriverProvider = Get-DevicePropertyValue -InstanceId $a2dpDevice.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverProvider'
        DriverVersion = [string](Get-DevicePropertyValue -InstanceId $a2dpDevice.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverVersion')
    }
}

$output = [pscustomobject]$result
if ($AsJson) {
    $output | ConvertTo-Json -Depth 5
} else {
    $output
}

