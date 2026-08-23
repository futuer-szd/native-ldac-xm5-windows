# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $projectRoot 'tools\v1-volume-sync-topology-common.ps1')
$probeText = Get-Content -LiteralPath (Join-Path $projectRoot `
    'tools\get-v1-volume-sync-topology.ps1') -Raw
foreach ($required in @(
        'status --porcelain',
        'sourceStatus.Count -ne 0',
        '[string]$ProjectRoot',
        'Join-Path $candidateRoot ''avrcp_transport_probe.exe''',
        'Invoke-BoundedReadOnlyProbe',
        "Arguments @('--open')",
        'TimeoutMilliseconds 3000',
        'read_only = $true',
        'write_authorization = $false')) {
    if (-not $probeText.Contains($required)) {
        throw "The volume-sync topology probe policy is missing: $required"
    }
}

function New-Device(
    [string]$InstanceId,
    [string]$Service,
    [string]$ContainerId,
    [string]$Parent) {
    return [pscustomobject]@{
        instance_id = $InstanceId
        service = $Service
        container_id = $ContainerId
        parent = $Parent
    }
}

$container = '{00112233-4455-6677-8899-AABBCCDDEEFF}'
$transport = New-Device 'BTHENUM\A2DP' 'LdacNative' $container 'BTH\RADIO'
$avrcp = New-Device 'BTHENUM\AVRCP' `
    'Microsoft_Bluetooth_AvrcpTransport' $container 'BTH\RADIO'
$root = New-Device 'ROOT\MEDIA\0001' 'NativeLdacAudio' `
    '{00000000-0000-0000-FFFF-FFFFFFFFFFFF}' 'HTREE\ROOT\0'

$decision = Get-V1VolumeSyncTopologyDecision -Transport $transport `
    -Avrcp $avrcp -Endpoint $root `
    -NativeMmDeviceContainerId $container -PrivateAvrcpOpenWin32 5
if (-not $decision.valid -or
    $decision.topology -ne `
        'independent-root-endpoint-with-mmdevice-container-bridge' -or
    -not $decision.endpoint_mmdevice_same_container -or
    $decision.endpoint_owned_by_transport -or
    -not $decision.swd_child_candidate_required -or
    $decision.private_avrcp_accessible -or
    $decision.synchronization_proven -or
    $decision.write_authorization) {
    throw 'The current independent-root topology was not classified safely.'
}

$child = New-Device 'SWD\NativeLdacAudio\1' 'NativeLdacAudio' `
    $container $transport.instance_id
$decision = Get-V1VolumeSyncTopologyDecision -Transport $transport `
    -Avrcp $avrcp -Endpoint $child `
    -NativeMmDeviceContainerId $container -PrivateAvrcpOpenWin32 5
if ($decision.topology -ne 'transport-owned-endpoint' -or
    -not $decision.endpoint_owned_by_transport -or
    $decision.swd_child_candidate_required -or
    $decision.synchronization_proven -or
    $decision.write_authorization -or
    $decision.reason -ne `
        'transport-owned-shape-present-but-avrcp-binding-unproven') {
    throw 'The child topology incorrectly claimed synchronization.'
}

$decision = Get-V1VolumeSyncTopologyDecision -Transport $transport `
    -Avrcp $avrcp -Endpoint $child `
    -NativeMmDeviceContainerId $container -PrivateAvrcpOpenWin32 0
if (-not $decision.private_avrcp_accessible -or
    $decision.synchronization_proven -or
    $decision.write_authorization -or
    $decision.reason -ne `
        'transport-owned-shape-ready-for-observe-only-validation') {
    throw 'Private-interface access bypassed the observe-only safety gate.'
}

$badAvrcp = New-Device 'BTHENUM\AVRCP' `
    'Microsoft_Bluetooth_AvrcpTransport' `
    '{11111111-1111-1111-1111-111111111111}' 'BTH\RADIO'
$decision = Get-V1VolumeSyncTopologyDecision -Transport $transport `
    -Avrcp $badAvrcp -Endpoint $root `
    -NativeMmDeviceContainerId $container -PrivateAvrcpOpenWin32 5
if ($decision.valid -or
    $decision.reason -ne 'xm5-transport-and-avrcp-container-mismatch' -or
    $decision.write_authorization) {
    throw 'A mixed-device topology did not fail closed.'
}

$incomplete = [pscustomobject]@{
    instance_id = 'ROOT\MEDIA\0001'
    service = 'NativeLdacAudio'
    container_id = $container
}
$decision = Get-V1VolumeSyncTopologyDecision -Transport $transport `
    -Avrcp $avrcp -Endpoint $incomplete `
    -NativeMmDeviceContainerId $container
if ($decision.valid -or
    $decision.reason -ne 'required-device-property-missing' -or
    'endpoint.parent' -notin @($decision.missing_properties) -or
    $decision.write_authorization) {
    throw 'An incomplete topology did not fail closed.'
}

Write-Host 'V1 volume-sync topology tests passed.'
