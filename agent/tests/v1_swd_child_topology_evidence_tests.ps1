# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $projectRoot 'tools\v1-swd-child-topology-common.ps1')

function New-Device([string]$Instance, [string]$Service,
    [string]$Inf, [string]$Container, [string]$Parent) {
    return [pscustomobject]@{
        instance_id = $Instance
        friendly_name = $Instance
        service = $Service
        published_inf = $Inf
        container_id = $Container
        parent = $Parent
        problem_code = 0
        hardware_ids = @('placeholder')
    }
}

$container = '{00112233-4455-6677-8899-AABBCCDDEEFF}'
$parent = 'BTHENUM\XM5_A2DP'
$transport = New-Device $parent 'LdacNative' 'oem9103.inf' `
    $container 'BTH\RADIO'
$endpoint = New-Device 'ROOT\MEDIA\0001' 'NativeLdacAudio' `
    'oem9202.inf' '{00000000-0000-0000-FFFF-FFFFFFFFFFFF}' `
    'HTREE\ROOT\0'
$child = [pscustomobject]@{
    instance_id = $script:V1SwdChildInstanceId
    friendly_name = $script:V1SwdChildFriendlyName
    present = $true
    service = ''
    published_inf = $script:V1SwdChildInboxInf
    container_id = $container
    parent = $parent
    problem_code = 0
    hardware_ids = @($null)
}
$before = [pscustomobject]@{
    transport = $transport
    endpoint = $endpoint
    child_devices = @()
    driver_packages = @('oem126|ldacnative.inf|1',
        'oem122|nativeldacaudio.inf|1')
    endpoint_volume_sha256 = 'ABC'
}
$during = [pscustomobject]@{
    transport = $transport
    endpoint = $endpoint
    child_devices = @($child)
    driver_packages = @('oem122|nativeldacaudio.inf|1',
        'oem126|ldacnative.inf|1')
    endpoint_volume_sha256 = 'ABC'
}
$after = [pscustomobject]@{
    transport = $transport
    endpoint = $endpoint
    child_devices = @()
    driver_packages = @('oem126|ldacnative.inf|1',
        'oem122|nativeldacaudio.inf|1')
    endpoint_volume_sha256 = 'ABC'
}
$probe = [pscustomobject]@{
    exit_code = 0
    completed = $true
    timed_out = $false
    forced_termination = $false
}
if (-not (Test-V1SwdChildLifecycleEvidence -Before $before `
        -During $during -After $after -Probe $probe `
        -ExpectedParent $parent -ExpectedContainer $container)) {
    throw 'Valid driverless SWD lifecycle evidence was rejected.'
}

$badChild = $child.PSObject.Copy()
$badChild.service = 'NativeLdacAudio'
$badDuring = $during.PSObject.Copy()
$badDuring.child_devices = @($badChild)
if (Test-V1SwdChildLifecycleEvidence -Before $before `
        -During $badDuring -After $after -Probe $probe `
        -ExpectedParent $parent -ExpectedContainer $container) {
    throw 'A driver-bound SWD child was accepted.'
}

$badChild = $child.PSObject.Copy()
$badChild.published_inf = 'oem9999.inf'
$badDuring = $during.PSObject.Copy()
$badDuring.child_devices = @($badChild)
if (Test-V1SwdChildLifecycleEvidence -Before $before `
        -During $badDuring -After $after -Probe $probe `
        -ExpectedParent $parent -ExpectedContainer $container) {
    throw 'A custom-INF SWD child was accepted.'
}

$badChild = $child.PSObject.Copy()
$badChild.present = $false
$badDuring = $during.PSObject.Copy()
$badDuring.child_devices = @($badChild)
if (Test-V1SwdChildLifecycleEvidence -Before $before `
        -During $badDuring -After $after -Probe $probe `
        -ExpectedParent $parent -ExpectedContainer $container) {
    throw 'A non-present SWD child was accepted as live evidence.'
}

$badChild = $child.PSObject.Copy()
$badChild.parent = 'HTREE\ROOT\0'
$badDuring = $during.PSObject.Copy()
$badDuring.child_devices = @($badChild)
if (Test-V1SwdChildLifecycleEvidence -Before $before `
        -During $badDuring -After $after -Probe $probe `
        -ExpectedParent $parent -ExpectedContainer $container) {
    throw 'A child with the wrong PnP parent was accepted.'
}

$badAfter = $after.PSObject.Copy()
$badAfter.child_devices = @($child)
if (Test-V1SwdChildLifecycleEvidence -Before $before `
        -During $during -After $badAfter -Probe $probe `
        -ExpectedParent $parent -ExpectedContainer $container) {
    throw 'A residual SWD child was accepted.'
}

$badAfter = $after.PSObject.Copy()
$badAfter.endpoint_volume_sha256 = 'DEF'
if (Test-V1SwdChildLifecycleEvidence -Before $before `
        -During $during -After $badAfter -Probe $probe `
        -ExpectedParent $parent -ExpectedContainer $container) {
    throw 'An endpoint volume mutation was accepted.'
}

$badAfter = $after.PSObject.Copy()
$badAfter.driver_packages = @('oem126|ldacnative.inf|1')
if (Test-V1SwdChildLifecycleEvidence -Before $before `
        -During $during -After $badAfter -Probe $probe `
        -ExpectedParent $parent -ExpectedContainer $container) {
    throw 'A driver package inventory change was accepted.'
}

$badProbe = $probe.PSObject.Copy()
$badProbe.forced_termination = $true
if (Test-V1SwdChildLifecycleEvidence -Before $before `
        -During $during -After $after -Probe $badProbe `
        -ExpectedParent $parent -ExpectedContainer $container) {
    throw 'A forcibly terminated probe was accepted.'
}

Write-Host 'V1 SWD child topology evidence tests passed.'
