# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\..\tools\v1-swd-endpoint-binding-common.ps1')

function Device(
    [string]$Id,
    [string]$Service,
    [string]$Inf,
    [string]$Container,
    [string]$Parent,
    [object[]]$HardwareIds = @()) {
    [pscustomobject]@{
        instance_id = $Id
        present = $true
        service = $Service
        published_inf = $Inf
        container_id = $Container
        parent = $Parent
        problem_code = 0
        hardware_ids = @($HardwareIds)
    }
}

function Mm([string]$Name, [string]$State, [string]$Id,
    [string]$Container, [string]$Roles) {
    [pscustomobject]@{
        name = $Name
        state = $State
        id = $Id
        container_id = $Container
        default_roles = $Roles
    }
}

$parent = 'BTHENUM\XM5_A2DP'
$container = '{00112233-4455-6677-8899-AABBCCDDEEFF}'
$transport = Device $parent 'LdacNative' 'oem9103.inf' $container 'BTHENUM\PARENT'
$rootDevice = Device 'ROOT\NativeLdacAudio\0000' 'NativeLdacAudio' `
    'oem9201.inf' $container 'HTREE\ROOT\0' @('ROOT\NativeLdacAudio')
$rootMm = Mm '扬声器 (Native LDAC Speaker Topology)' 'unplugged' '{root-mm}' `
    $container 'console, multimedia'
$child = Device $script:V1SwdEndpointBindingInstanceId `
    $script:V1SwdEndpointBindingService 'oem9203.inf' $container $parent `
    @($script:V1SwdEndpointBindingHardwareId)
$package = [pscustomobject]@{
    published_inf = 'oem9203.inf'
    original_file_name = $script:V1SwdEndpointBindingOriginalInf
}
$candidateMm = Mm '扬声器 (Native LDAC SWD Speaker Topology)' `
    'unplugged' `
    '{candidate-mm}' $container '(none)'
$candidateGone = Mm `
    '扬声器 (Native LDAC SWD Speaker Topology)' 'not-present' `
    '{candidate-mm}' $container '(none)'

$before = [pscustomobject]@{
    transport = $transport
    root_endpoint = $rootDevice
    root_mmdevice = $rootMm
    candidate_children = @()
    candidate_packages = @()
    candidate_mmdevices = @()
}
$during = [pscustomobject]@{
    transport = $transport
    root_endpoint = $rootDevice
    root_mmdevice = $rootMm
    candidate_children = @($child)
    candidate_packages = @($package)
    candidate_mmdevices = @($candidateMm)
}
$after = [pscustomobject]@{
    transport = $transport
    root_endpoint = $rootDevice
    root_mmdevice = $rootMm
    candidate_children = @()
    candidate_packages = @()
    candidate_mmdevices = @($candidateGone)
}
$hostProcess = [pscustomobject]@{
    completed = $true
    timed_out = $false
    forced_termination = $false
    exit_code = 0
}
$rollback = [pscustomobject]@{
    device_instance_absent = $true
    package_remove_attempted = $true
    package_remove_succeeded = $true
    child_absent = $true
    published_candidate_endpoint_absent = $true
}

if (-not (Test-V1SwdEndpointBindingEvidence -Before $before `
        -During $during -After $after -HostProcess $hostProcess `
        -Rollback $rollback -ExpectedParent $parent `
        -ExpectedContainer $container)) {
    throw 'The positive endpoint-binding evidence was rejected.'
}

$badDefault = $during | ConvertTo-Json -Depth 8 | ConvertFrom-Json
$badDefault.candidate_mmdevices[0].default_roles = 'multimedia'
if (Test-V1SwdEndpointBindingEvidence -Before $before `
        -During $badDefault -After $after -HostProcess $hostProcess `
        -Rollback $rollback -ExpectedParent $parent `
        -ExpectedContainer $container) {
    throw 'A candidate endpoint that became default was accepted.'
}

$leakedPackage = $after | ConvertTo-Json -Depth 8 | ConvertFrom-Json
$leakedPackage.candidate_packages = @($package)
if (Test-V1SwdEndpointBindingEvidence -Before $before `
        -During $during -After $leakedPackage -HostProcess $hostProcess `
        -Rollback $rollback -ExpectedParent $parent `
        -ExpectedContainer $container) {
    throw 'A leaked isolated package was accepted.'
}

$wrongParent = $during | ConvertTo-Json -Depth 8 | ConvertFrom-Json
$wrongParent.candidate_children[0].parent = 'HTREE\ROOT\0'
if (Test-V1SwdEndpointBindingEvidence -Before $before `
        -During $wrongParent -After $after -HostProcess $hostProcess `
        -Rollback $rollback -ExpectedParent $parent `
        -ExpectedContainer $container) {
    throw 'A candidate child under the wrong parent was accepted.'
}

$parsed = @(ConvertFrom-V1SwdEndpointVolumeText -Lines @(
    'Endpoint: 扬声器 (Native LDAC Speaker Topology)',
    '  state: unplugged',
    '  id: {root-mm}',
    "  container: $container",
    '  default roles: console, multimedia',
    'Endpoint: Native LDAC SWD Speaker Topology',
    '  state: active',
    '  id: {candidate-mm}',
    "  container: $container",
    '  default roles: (none)'))
if ($parsed.Count -ne 2 -or
    -not (Test-V1SwdEndpointNameContains `
        -Name ([string]$parsed[1].name) `
        -Marker $script:V1SwdEndpointBindingNameMarker) -or
    [string]$parsed[1].state -cne 'active') {
    throw 'The endpoint-volume text parser failed.'
}

Write-Host 'V1 SWD endpoint binding evidence tests passed.'
