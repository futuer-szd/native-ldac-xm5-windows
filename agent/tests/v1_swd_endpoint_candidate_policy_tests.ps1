# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-File([string]$Path) {
    Get-Content -LiteralPath (Join-Path $projectRoot $Path) -Raw
}

$hostSource = Read-File 'tools\v1_swd_endpoint_host.cpp'
$inf = Read-File 'audio-endpoint\Source\Main\NativeLdacSwdAudio.inx'
$project = Read-File 'audio-endpoint\Source\Main\Main.vcxproj'
$build = Read-File 'tools\build-v1-swd-endpoint-candidate.ps1'
$verify = Read-File 'tools\verify-v1-swd-endpoint-candidate.ps1'
$common = Read-File 'tools\v1-swd-endpoint-candidate-common.ps1'
$cmake = Read-File 'CMakeLists.txt'

foreach ($required in @(
        'SWDeviceCapabilitiesDriverRequired',
        'SWDeviceLifetimeHandle',
        'SWD\\NativeLdacAudioXm5\0',
        '--confirm-endpoint-binding-probe',
        '--publish-presence',
        '--confirm-volume-observation',
        '--stop-event',
        'OpenForInstanceId',
        'The default/--plan path is read-only')) {
    if (-not $hostSource.Contains($required)) {
        throw "The SWD endpoint host policy is missing: $required"
    }
}
foreach ($required in @(
        'Class       = MEDIA',
        'SWD\NativeLdacAudioXm5',
        'AddService=NativeLdacSwdAudio',
        'NativeLdacSwdAudio.sys',
        'PKEY_AudioDevice_NeverSetAsDefaultEndpoint',
        '0x00000107',
        '{F3E80BEF-1723-4FF2-BCC4-7F83DC5E46D4},3')) {
    if (-not $inf.Contains($required)) {
        throw "The isolated SWD endpoint INF is missing: $required"
    }
}
if ($inf.Contains('ROOT\NativeLdacAudio') -or
    $inf.Contains('BTHENUM\{0000110B')) {
    throw 'The isolated SWD endpoint INF targets an existing device identity.'
}
foreach ($required in @(
        'NativeLdacSwdEndpointCandidate',
        '<TargetName>NativeLdacSwdAudio</TargetName>',
        '<Inf Include="NativeLdacSwdAudio.inx"',
        'ValidateNativeLdacSwdEndpointCandidateSelection',
        'cannot be combined with a Direct PDO build variant')) {
    if (-not $project.Contains($required)) {
        throw "The endpoint project variant is missing: $required"
    }
}
foreach ($required in @(
        'requires clean Git source',
        'install_script_included = $false',
        'device_creation_default = $false',
        'current_root_endpoint_preserved = $true',
        'volume_observation_presence_supported = $true',
        'stop_event_supported = $true',
        'xm5_connection_probe_included = $true',
        'capability_only_signaling_hold_supported = $true',
        'never_default_render_role_mask = 0x00000107',
        'PowerShell verification failures propagate as terminating errors',
        'No driver, certificate, device, endpoint, Bluetooth state, or audio path was changed')) {
    if (-not $build.Contains($required)) {
        throw "The endpoint candidate build policy is missing: $required"
    }
}
foreach ($required in @(
        'candidate verifier requires PowerShell 7',
        'PKEY_AudioDevice_NeverSetAsDefaultEndpoint',
        '0x00000107')) {
    if (-not $verify.Contains($required)) {
        throw "The endpoint candidate verifier is missing: $required"
    }
}
if (-not $verify.Contains('$hostPath = Join-Path') -or
    $verify.Contains('$host = Join-Path')) {
    throw 'The endpoint candidate verifier conflicts with the PowerShell Host variable.'
}
foreach ($forbidden in @(
        'pnputil.exe',
        'devcon.exe',
        'Import-Certificate',
        'Disable-PnpDevice',
        'Enable-PnpDevice',
        'BluetoothSetServiceState')) {
    if (($build + $verify).IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The endpoint candidate path contains a forbidden mutation: $forbidden"
    }
}
foreach ($forbiddenPackagedFile in @(
        "'v1-swd-endpoint-binding-common.ps1' =",
        "'run-v1-swd-endpoint-binding-gate.ps1' =",
        "'v1-swd-endpoint-system-common.ps1' =",
        "'v1-swd-volume-observation-common.ps1' =",
        "'run-v1-swd-volume-observation-gate.ps1' =")) {
    if ($build.Contains($forbiddenPackagedFile)) {
        throw "The staged-only candidate includes a system gate: $forbiddenPackagedFile"
    }
}
foreach ($required in @(
        '$script:V1SwdEndpointCandidatePolicyVersion = 4',
        'SWD\NativeLdacAudioXm5',
        'NativeLdacSwdAudio',
        'volume_observation_presence_supported',
        'capability_only_signaling_hold_supported')) {
    if (-not $common.Contains($required)) {
        throw "The endpoint candidate common contract is missing: $required"
    }
}
foreach ($required in @(
        'v1_swd_endpoint_host',
        'v1_swd_endpoint_candidate_policy')) {
    if (-not $cmake.Contains($required)) {
        throw "The endpoint candidate CMake contract is missing: $required"
    }
}

Write-Host 'V1 SWD endpoint candidate policy tests passed.'
