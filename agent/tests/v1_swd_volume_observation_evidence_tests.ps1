# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot `
    '..\..\tools\v1-swd-volume-observation-common.ps1')

function Device(
    [string]$Id, [string]$Service, [string]$Inf,
    [string]$Container, [string]$Parent,
    [object[]]$HardwareIds = @()) {
    [pscustomobject]@{
        instance_id=$Id; present=$true; service=$Service
        published_inf=$Inf; container_id=$Container; parent=$Parent
        problem_code=0; hardware_ids=@($HardwareIds)
    }
}
function Mm([string]$Name, [string]$State, [string]$Id,
    [string]$Container, [string]$Roles, [double]$Percent,
    [int]$Step) {
    [pscustomobject]@{
        name=$Name; state=$State; id=$Id; container_id=$Container
        default_roles=$Roles; volume_available=$true
        volume_percent=$Percent; level_db=0.0; muted=$false
        step_available=$true; step_index=$Step; step_count=51
    }
}

$parent = 'BTHENUM\XM5_A2DP'
$container = '{00112233-4455-6677-8899-AABBCCDDEEFF}'
$transport = Device $parent 'LdacNative' 'oem9103.inf' $container 'BTH\PARENT'
$rootDevice = Device 'ROOT\MEDIA\0001' 'NativeLdacAudio' 'oem9202.inf' `
    '{00000000-0000-0000-FFFF-FFFFFFFFFFFF}' 'HTREE\ROOT\0' `
    @('ROOT\NativeLdacAudio')
$rootBefore = Mm '扬声器 (Native LDAC Speaker Topology)' 'unplugged' `
    '{root-mm}' $container '(none)' 72.1 36
$rootDuring = Mm '扬声器 (Native LDAC Speaker Topology)' 'active' `
    '{root-mm}' $container '(none)' 72.1 36
$rootAfter = Mm '扬声器 (Native LDAC Speaker Topology)' 'unplugged' `
    '{root-mm}' $container '(none)' 72.1 36
$child = Device $script:V1SwdEndpointBindingInstanceId `
    $script:V1SwdEndpointBindingService 'oem9203.inf' $container $parent `
    @($script:V1SwdEndpointBindingHardwareId)
$package = [pscustomobject]@{
    published_inf='oem9203.inf'
    original_file_name=$script:V1SwdEndpointBindingOriginalInf
}
$candidate = Mm '扬声器 (Native LDAC SWD Speaker Topology)' 'active' `
    '{candidate-mm}' $container '(none)' 100.0 50
$candidateGone = Mm '扬声器 (Native LDAC SWD Speaker Topology)' `
    'not-present' '{candidate-mm}' $container '(none)' 100.0 50
$systemDefault = Mm '1 - Display' 'active' '{system-default-mm}' `
    '{DISPLAY-CONTAINER}' 'console, multimedia' 82.0 41

$before = [pscustomobject]@{
    transport=$transport; root_endpoint=$rootDevice; root_mmdevice=$rootBefore
    default_endpoints=@($systemDefault)
    candidate_children=@(); candidate_registered_devices=@()
    candidate_packages=@(); candidate_mmdevices=@()
}
$during = [pscustomobject]@{
    transport=$transport; root_endpoint=$rootDevice; root_mmdevice=$rootDuring
    default_endpoints=@($systemDefault)
    candidate_children=@($child); candidate_registered_devices=@($child)
    candidate_packages=@($package); candidate_mmdevices=@($candidate)
}
$after = [pscustomobject]@{
    transport=$transport; root_endpoint=$rootDevice; root_mmdevice=$rootAfter
    default_endpoints=@($systemDefault)
    candidate_children=@(); candidate_registered_devices=@()
    candidate_packages=@(); candidate_mmdevices=@($candidateGone)
}
$samples = @()
foreach ($index in 0..11) {
    $candidateSample = Mm $candidate.name 'active' $candidate.id $container `
        '(none)' (100.0 - ($index % 3) * 2.0) (50 - ($index % 3))
    $samples += [pscustomobject]@{
        elapsed_ms=$index * 250; root=$rootDuring
        candidate=$candidateSample
    }
}
$classification = Get-V1SwdVolumeObservationClassification `
    -Samples $samples
if ([string]$classification.classification -cne 'candidate-only-changed' -or
    -not $classification.candidate_volume_changed -or
    $classification.root_volume_changed) {
    throw 'The positive volume classification failed.'
}
$emptyClassification = Get-V1SwdVolumeObservationClassification `
    -Samples @()
if ([string]$emptyClassification.classification -cne
        'no-public-endpoint-change' -or
    [int]$emptyClassification.candidate_distinct_values -ne 0 -or
    [int]$emptyClassification.root_distinct_values -ne 0) {
    throw 'The empty volume classification did not fail closed cleanly.'
}
$historicalCandidate = Mm `
    '扬声器 (Native LDAC SWD Speaker Topology)' 'not-present' `
    '{old-candidate-mm}' $container '(none)' 80.0 40
$pair = Get-V1SwdVolumePair `
    -Manifest ([pscustomobject]@{ remote_container_id=$container }) `
    -Endpoints @($rootDuring, $candidate, $historicalCandidate)
if ($pair.root_count -ne 1 -or $pair.candidate_count -ne 1 -or
    [string]$pair.candidate.id -cne '{candidate-mm}') {
    throw 'The public endpoint pair did not ignore historical candidates.'
}
$observation = [pscustomobject]@{
    completed=$true; sample_count=$samples.Count; query_failures=0
    candidate_active_samples=$samples.Count
    candidate_default_role_violations=0
}
$hostProcess = [pscustomobject]@{
    completed=$true; timed_out=$false; forced_termination=$false
    stop_event_observed=$true; exit_code=0
}
$signalingProcess = [pscustomobject]@{
    started=$true; ready=$true; completed=$true; timed_out=$false
    forced_termination=$false; capability_discovery_completed=$true
    hold_started=$true; stop_event_observed=$true; channel_closed=$true
    exit_code=0
}
$acl = [pscustomobject]@{
    connect_observed=$true; disconnect_observed=$false
    disconnect_required=$false
    transport_released_before_power_off=$true
}
$rollback = [pscustomobject]@{
    device_instance_absent=$true; package_remove_succeeded=$true
    published_candidate_endpoint_absent=$true
}
if (-not (Test-V1SwdVolumeObservationEvidence `
        -Before $before -During $during -After $after `
        -Observation $observation -HostProcess $hostProcess `
        -SignalingProcess $signalingProcess -Acl $acl `
        -Rollback $rollback -ExpectedParent $parent `
        -ExpectedContainer $container)) {
    throw 'The positive volume-observation evidence was rejected.'
}

$badDefault = $during | ConvertTo-Json -Depth 8 | ConvertFrom-Json
$badDefault.candidate_mmdevices[0].default_roles = 'multimedia'
if (Test-V1SwdVolumeObservationEvidence `
        -Before $before -During $badDefault -After $after `
        -Observation $observation -HostProcess $hostProcess `
        -SignalingProcess $signalingProcess -Acl $acl `
        -Rollback $rollback -ExpectedParent $parent `
        -ExpectedContainer $container) {
    throw 'A candidate endpoint with a default role was accepted.'
}

$badSystemDefault = $during | ConvertTo-Json -Depth 8 | ConvertFrom-Json
$badSystemDefault.default_endpoints = @($candidate)
$badSystemDefault.default_endpoints[0].default_roles =
    'console, multimedia'
if (Test-V1SwdVolumeObservationEvidence `
        -Before $before -During $badSystemDefault -After $after `
        -Observation $observation -HostProcess $hostProcess `
        -SignalingProcess $signalingProcess -Acl $acl `
        -Rollback $rollback -ExpectedParent $parent `
        -ExpectedContainer $container) {
    throw 'A changed system default endpoint mapping was accepted.'
}

$badObservation = $observation | ConvertTo-Json | ConvertFrom-Json
$badObservation.query_failures = 1
if (Test-V1SwdVolumeObservationEvidence `
        -Before $before -During $during -After $after `
        -Observation $badObservation -HostProcess $hostProcess `
        -SignalingProcess $signalingProcess -Acl $acl `
        -Rollback $rollback -ExpectedParent $parent `
        -ExpectedContainer $container) {
    throw 'A volume observation with a query failure was accepted.'
}

$missingStop = $hostProcess | ConvertTo-Json | ConvertFrom-Json
$missingStop.stop_event_observed = $false
if (Test-V1SwdVolumeObservationEvidence `
        -Before $before -During $during -After $after `
        -Observation $observation -HostProcess $missingStop `
        -SignalingProcess $signalingProcess -Acl $acl `
        -Rollback $rollback -ExpectedParent $parent `
        -ExpectedContainer $container) {
    throw 'A host that did not observe the bounded stop event was accepted.'
}

$missingSignalingStop = $signalingProcess | ConvertTo-Json |
    ConvertFrom-Json
$missingSignalingStop.stop_event_observed = $false
if (Test-V1SwdVolumeObservationEvidence `
        -Before $before -During $during -After $after `
        -Observation $observation -HostProcess $hostProcess `
        -SignalingProcess $missingSignalingStop -Acl $acl `
        -Rollback $rollback -ExpectedParent $parent `
        -ExpectedContainer $container) {
    throw 'A signaling holder without bounded-stop evidence was accepted.'
}

$lateRelease = $acl | ConvertTo-Json | ConvertFrom-Json
$lateRelease.transport_released_before_power_off = $false
if (Test-V1SwdVolumeObservationEvidence `
        -Before $before -During $during -After $after `
        -Observation $observation -HostProcess $hostProcess `
        -SignalingProcess $signalingProcess -Acl $lateRelease `
        -Rollback $rollback -ExpectedParent $parent `
        -ExpectedContainer $container) {
    throw 'A volume observation that held signaling through power-off was accepted.'
}

$parsed = @(ConvertFrom-V1SwdEndpointVolumeText -Lines @(
    'Endpoint: 扬声器 (Native LDAC SWD Speaker Topology)',
    '  state: active',
    '  id: {candidate-mm}',
    "  container: $container",
    '  default roles: (none)',
    '  volume: 72.1%, -4.9688 dB (muted)',
    '  volume range: -96.0000..0.0000 dB, 0.0312 dB increment',
    '  volume step: 36/51'))
if ($parsed.Count -ne 1 -or
    $parsed[0].volume_available -ne $true -or
    [math]::Abs([double]$parsed[0].volume_percent - 72.1) -gt 0.001 -or
    $parsed[0].muted -ne $true -or
    [int]$parsed[0].step_index -ne 36 -or
    [int]$parsed[0].step_count -ne 51) {
    throw 'The extended endpoint-volume parser failed.'
}

Write-Host 'V1 SWD volume observation evidence tests passed.'
