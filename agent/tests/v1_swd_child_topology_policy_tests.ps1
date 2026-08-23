# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-File([string]$Path) {
    return Get-Content -LiteralPath (Join-Path $projectRoot $Path) -Raw
}
$build = Read-File 'tools\build-v1-swd-child-topology-candidate.ps1'
$gate = Read-File 'tools\run-v1-swd-child-topology-gate.ps1'
$common = Read-File 'tools\v1-swd-child-topology-common.ps1'
$topology = Read-File 'tools\get-v1-volume-sync-topology.ps1'
$cmake = Read-File 'CMakeLists.txt'

foreach ($required in @(
        'sourceStatus.Count -ne 0',
        'v1_swd_child_probe endpoint_volume_probe avrcp_transport_probe',
        'driverless = $true',
        'inbox_null_driver_inf = $script:V1SwdChildInboxInf',
        'custom_driver_binding = $false',
        'driver_install = $false',
        'audio_endpoint_creation = $false',
        'write_authorization = $false',
        'No software device was created')) {
    if (-not $build.Contains($required)) {
        throw "The SWD topology candidate policy is missing: $required"
    }
}
foreach ($required in @(
        'SupportsShouldProcess',
        'ConfirmV1SwdChildTopology',
        'verify-v1-golden-checkpoint.ps1',
        'Get-V1SwdChildTopologyCandidate',
        '--confirm-driverless-probe',
        'Get-ChildProbeDevices',
        '-InstanceId $script:V1SwdChildInstanceId',
        '-ChildDevices $liveChildren',
        '[bool]$_.Present',
        '-ProjectRoot $projectRoot',
        'Test-V1SwdChildLifecycleEvidence',
        'driver_installed = $false',
        'audio_endpoint_created = $false')) {
    if (-not $gate.Contains($required)) {
        throw "The SWD topology gate policy is missing: $required"
    }
}
if ($gate.Contains('Get-PnpDevice -ErrorAction SilentlyContinue')) {
    throw 'The SWD topology gate still uses an unbounded full PnP enumeration.'
}
foreach ($forbidden in @(
        'pnputil.exe /add-driver',
        'pnputil.exe /delete-driver',
        'devcon',
        'Disable-PnpDevice',
        'Enable-PnpDevice',
        'SetDefaultEndpoint',
        'BluetoothSetServiceState')) {
    if (($build + $gate).IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The SWD topology path contains forbidden mutation: $forbidden"
    }
}
foreach ($required in @(
        'V1SwdChildInstanceId',
        'V1SwdChildInboxInf',
        'custom_driver_binding',
        '$hardwareIds.Count -ne 0',
        'endpoint_volume_sha256',
        'driver_packages')) {
    if (-not $common.Contains($required)) {
        throw "The SWD topology evidence contract is missing: $required"
    }
}
foreach ($required in @(
        '[string]$ProbeRoot',
        '[string]$ProjectRoot',
        'Join-Path $candidateRoot ''avrcp_transport_probe.exe''')) {
    if (-not $topology.Contains($required)) {
        throw "The topology probe candidate-root contract is missing: $required"
    }
}
foreach ($required in @(
        'v1_swd_child_topology_policy',
        'v1_swd_child_topology_evidence')) {
    if (-not $cmake.Contains($required)) {
        throw "The SWD topology CTest registration is missing: $required"
    }
}

Write-Host 'V1 SWD child topology gate policy tests passed.'
