# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [Parameter(Mandatory = $true)]
    [string]$ResultPath,
    [switch]$ConfirmV1AvrcpObserverRollback
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'The AVRCP observer rollback requires an elevated PowerShell 7 terminal.'
    }
}
function Get-PropertyData([string]$InstanceId, [string]$KeyName) {
    $property = Get-PnpDeviceProperty -InstanceId $InstanceId `
        -KeyName $KeyName -ErrorAction SilentlyContinue
    if ($null -eq $property) { return $null }
    return $property.Data
}
function Get-CandidatePackages {
    return @(Get-WindowsDriver -Online -All | Where-Object {
        (Split-Path -Leaf ([string]$_.OriginalFileName)) -ieq
            'NativeLdacAvrcpObserver.inf'
    })
}
function Invoke-PnpUtil([string[]]$Arguments) {
    $lines = @(& pnputil.exe @Arguments 2>&1)
    if ($LASTEXITCODE -notin @(0, 259)) {
        throw "PnPUtil failed ($LASTEXITCODE): $($lines -join ' ')"
    }
    return @($lines)
}

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The AVRCP observer rollback requires PowerShell 7.'
}
Assert-Administrator
if (-not $ConfirmV1AvrcpObserverRollback) {
    throw 'Refusing rollback without -ConfirmV1AvrcpObserverRollback.'
}
$ResultPath = [IO.Path]::GetFullPath($ResultPath)
if (-not (Test-Path -LiteralPath $ResultPath -PathType Leaf)) {
    throw "The gate result does not exist: $ResultPath"
}
$failed = Get-Content -LiteralPath $ResultPath -Raw | ConvertFrom-Json
$instanceId = [string]$failed.baseline.instance_id
if ($instanceId -notmatch '^BTHENUM\\\{0000110E-') {
    throw 'The result does not describe an exact AVRCP observer transaction.'
}
$packages = @(Get-CandidatePackages)
if ($packages.Count -gt 1) {
    throw 'More than one AVRCP observer package exists; manual review is required.'
}
Write-Host 'V1 AVRCP observer rollback preflight passed.'
Write-Host 'Keep XM5 off. Only the candidate package and exact 0x110E PDO binding will be changed.'
if (-not $PSCmdlet.ShouldProcess(
        $instanceId,
        'Delete the exact candidate package, rescan devices, restart only the exact PDO if present, and verify the Microsoft binding')) {
    return
}

if ($packages.Count -eq 1) {
    $published = [string]$packages[0].Driver
    [void](Invoke-PnpUtil -Arguments @('/delete-driver', $published,
        '/uninstall', '/force'))
}
[void](Invoke-PnpUtil -Arguments @('/scan-devices'))
Start-Sleep -Seconds 2
$device = Get-PnpDevice -InstanceId $instanceId -ErrorAction SilentlyContinue
if ($null -ne $device -and [bool]$device.Present) {
    [void](Invoke-PnpUtil -Arguments @('/restart-device', $instanceId))
    Start-Sleep -Seconds 2
}
$remaining = @(Get-CandidatePackages)
$device = Get-PnpDevice -InstanceId $instanceId -ErrorAction Stop
$inf = [string](Get-PropertyData $instanceId `
    'DEVPKEY_Device_DriverInfPath')
$service = [string](Get-PropertyData $instanceId `
    'DEVPKEY_Device_Service')
$problem = [int](Get-PropertyData $instanceId `
    'DEVPKEY_Device_ProblemCode')
if ($remaining.Count -ne 0 -or
    $inf -ine 'microsoft_bluetooth_avrcptransport.inf' -or
    $service -ine 'Microsoft_Bluetooth_AvrcpTransport' -or
    $problem -ne 0) {
    throw 'The Microsoft AVRCP binding did not restore cleanly.'
}
$rollbackResult = [ordered]@{
    created_at = (Get-Date).ToString('o')
    passed = $true
    instance_id = $instanceId
    inf = $inf
    service = $service
    problem_code = $problem
    candidate_packages = 0
}
$rollbackPath = Join-Path (Split-Path $ResultPath -Parent) `
    'rollback-result.json'
$rollbackResult | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $rollbackPath -Encoding utf8
Write-Host 'V1 AVRCP observer rollback passed.'
Write-Host 'The Microsoft AVRCP transport binding is restored; LdacNative and ROOT audio were untouched.'
Write-Host "Result: $rollbackPath"
