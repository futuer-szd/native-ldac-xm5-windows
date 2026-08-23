# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [Guid]$RemoteContainerId = [Guid]::Empty,
    [switch]$ConfirmEndpointInstall
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

Assert-Administrator
if (-not $ConfirmEndpointInstall) {
    throw 'Refusing to install a system audio endpoint. Re-run with -ConfirmEndpointInstall.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildScript = Join-Path $PSScriptRoot 'build-audio-endpoint.ps1'
& $buildScript -Configuration $Configuration -RemoteContainerId $RemoteContainerId

$control = Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control'
if ([string]$control.SystemStartOptions -notmatch '(^|\s)TESTSIGNING(\s|$)') {
    throw 'The current boot is not in TESTSIGNING mode.'
}

$existingDevices = @(Get-NativeLdacAudioDevices)
if ($existingDevices.Count -gt 1) {
    $existingIds = @($existingDevices | ForEach-Object { $_.InstanceId })
    throw "Multiple NativeLdacAudio root devices were found: $($existingIds -join ', ')"
}
$devconAction = if ($existingDevices.Count -eq 0) { 'install' } else { 'update' }
$displayAction = if ($devconAction -eq 'install') {
    'Install Native LDAC test audio endpoint'
} else {
    "Update Native LDAC test audio endpoint on $($existingDevices[0].InstanceId)"
}

$devconPath = Find-DevCon
if (-not $devconPath) {
    throw 'The x64 WDK devcon.exe was not found.'
}

$packageRoot = Join-Path $projectRoot 'artifacts\audio-endpoint\package'
$certificatePath = Join-Path $packageRoot 'NativeLdacAudio.cer'
$infPath = Join-Path $packageRoot 'NativeLdacAudio.inf'
$targetDescription = 'ROOT\NativeLdacAudio virtual render endpoint'
if (-not $PSCmdlet.ShouldProcess($targetDescription, $displayAction)) {
    return
}

$rootCertificate = Import-Certificate -FilePath $certificatePath -CertStoreLocation 'Cert:\LocalMachine\Root'
$publisherCertificate = Import-Certificate -FilePath $certificatePath -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher'

if ($devconAction -eq 'install') {
    $installOutput = & $devconPath install $infPath 'ROOT\NativeLdacAudio' 2>&1
} else {
    $installOutput = & $devconPath update $infPath 'ROOT\NativeLdacAudio' 2>&1
}
$installExitCode = $LASTEXITCODE
$outputRoot = Join-Path $projectRoot 'artifacts\audio-endpoint'
$installLog = Join-Path $outputRoot 'devcon-install.log'
$installOutput | Set-Content -LiteralPath $installLog -Encoding UTF8
if ($installExitCode -notin @(0, 1)) {
    throw "devcon $devconAction failed with exit code $installExitCode. See $installLog"
}

Start-Sleep -Seconds 2
$devices = @(Get-NativeLdacAudioDevices)
if ($devices.Count -eq 0) {
    throw "devcon returned success but no NativeLdacAudio root device was found. See $installLog"
}

$deviceStates = @($devices | ForEach-Object {
    $service = Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_Service' -ErrorAction SilentlyContinue
    $driverInf = Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_DriverInfPath' -ErrorAction SilentlyContinue
    [ordered]@{
        instance_id = $_.InstanceId
        status = [string]$_.Status
        service = if ($service) { [string]$service.Data } else { '' }
        published_inf = if ($driverInf) { [string]$driverInf.Data } else { '' }
    }
})
$installState = [ordered]@{
    completed_at = (Get-Date).ToString('o')
    action = $devconAction
    hardware_id = 'ROOT\NativeLdacAudio'
    endpoint_name = 'Native LDAC - WH-1000XM5'
    devcon_exit_code = $installExitCode
    reboot_required = ($installExitCode -eq 1)
    root_certificate_thumbprint = $rootCertificate.Thumbprint
    trusted_publisher_thumbprint = $publisherCertificate.Thumbprint
    devices = @($deviceStates)
}
$installStatePath = Join-Path $outputRoot 'install-state.json'
$installState | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $installStatePath -Encoding UTF8

Write-Host "Native LDAC virtual audio endpoint $devconAction completed."
Write-Host "State: $installStatePath"
if ($installExitCode -eq 1) {
    Write-Host 'DevCon reports that a reboot is required.'
} else {
    Write-Host 'Open Windows Sound settings and look for Native LDAC - WH-1000XM5.'
}
