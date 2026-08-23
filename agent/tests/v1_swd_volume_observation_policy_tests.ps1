# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-File([string]$Path) {
    Get-Content -LiteralPath (Join-Path $projectRoot $Path) -Raw
}

$gate = Read-File 'tools\run-v1-swd-volume-observation-gate.ps1'
$common = Read-File 'tools\v1-swd-volume-observation-common.ps1'
$system = Read-File 'tools\v1-swd-endpoint-system-common.ps1'
$hostSource = Read-File 'tools\v1_swd_endpoint_host.cpp'
$transportSource = Read-File 'tools\transport_probe.c'
$sink = Read-File 'agent\v1_endpoint_presence_sink.cpp'
$candidateBuild = Read-File 'tools\build-v1-swd-endpoint-candidate.ps1'
$cmake = Read-File 'CMakeLists.txt'

foreach ($required in @(
        "SupportsShouldProcess, ConfirmImpact = 'High'",
        'ConfirmV1SwdVolumeObservation',
        'requires PowerShell 7',
        'Assert-V1SwdAdministrator',
        'volume_observation_presence_supported',
        'binding_prerequisite',
        "'/add-driver', `$packageInf",
        "'/remove-device',",
        "'/delete-driver', `$publishedInf, '/force'",
        '--publish-presence',
        '--confirm-volume-observation',
        '--stop-event',
        'stop_event_observed',
        'candidate-activation-samples.json',
        "'during.json'",
        'Get-V1SwdEndpointObservationSnapshot',
        'default_endpoint_roles_preserved',
        'Windows assigned default roles to the isolated candidate',
        "'--wait-acl-connect', '60'",
        'Bluetooth radio state: ready',
        'Get-PnpDevice -PresentOnly',
        'The paired XM5 PnP topology is not healthy',
        'no child or endpoint is created before physical ACL connect',
        'no isolated child or endpoint was created',
        "'--discover'",
        "'--open-attempts', '1'",
        "'--hold-signaling-seconds', [string]`$DurationSeconds",
        'V1 capability-only AVDTP signaling hold is active',
        'release both before XM5 power-off',
        'transport_released_before_power_off =',
        'physical_power_off_requested = $false',
        'signaling_process = $signalingProcessEvidence',
        'capability_only_signaling = $true',
        'set_configuration_sent = $false',
        'media_channel_opened = $false',
        'avdtp_start_sent = $false',
        'media_packet_sent = $false',
        'the Bluetooth radio or XM5 parent topology was invalidated',
        'endpoint_volume_written = $false',
        'default_endpoint_written = $false',
        'avrcp_written = $false',
        'audio_playback_started = $false',
        'synchronization_proven = $false')) {
    if (-not $gate.Contains($required)) {
        throw "The SWD volume observation gate is missing: $required"
    }
}

$aclWatcherIndex = $gate.IndexOf(
    "'--wait-acl-connect', '60'",
    [StringComparison]::Ordinal)
$packageStageIndex = $gate.IndexOf(
    "'/add-driver', `$packageInf",
    [StringComparison]::Ordinal)
$signalingHoldIndex = $gate.IndexOf(
    "'--hold-signaling-seconds', [string]`$DurationSeconds",
    [StringComparison]::Ordinal)
$endpointHostIndex = $gate.IndexOf(
    "'v1_swd_endpoint_host.exe'",
    [StringComparison]::Ordinal)
if ($aclWatcherIndex -lt 0 -or $packageStageIndex -lt 0 -or
    $signalingHoldIndex -lt 0 -or $endpointHostIndex -lt 0 -or
    $packageStageIndex -ge $aclWatcherIndex -or
    $aclWatcherIndex -ge $signalingHoldIndex -or
    $signalingHoldIndex -ge $endpointHostIndex) {
    throw 'The SWD volume observation gate must stage only the package, prove ACL, hold signaling, then create the endpoint.'
}

foreach ($required in @(
        '$script:V1SwdVolumeObservationPolicyVersion = 4',
        'Get-V1SwdVolumeObservationClassification',
        'Test-V1SwdVolumeObservationEvidence',
        'Test-V1SwdDefaultEndpointRolesStable',
        'candidate-only-changed',
        'no-public-endpoint-change')) {
    if (-not $common.Contains($required)) {
        throw "The SWD volume observation evidence is missing: $required"
    }
}

if ($gate.Contains("'--wait-acl-disconnect', '60'")) {
    throw 'The volume observation gate must release signaling before XM5 power-off instead of waiting for disconnect while holding it.'
}

foreach ($required in @(
        'Get-V1SwdBindingSnapshot',
        'Test-V1SwdExactRegisteredCandidate',
        'Get-V1SwdCandidateRegisteredDevices')) {
    if (-not $system.Contains($required)) {
        throw "The SWD system observation helper is missing: $required"
    }
}

foreach ($required in @(
        'OpenForInstanceId',
        '--publish-presence',
        '--confirm-volume-observation',
        '--stop-event',
        'SWD endpoint candidate presence released')) {
    if (-not ($hostSource + $sink).Contains($required)) {
        throw "The exact candidate presence host is missing: $required"
    }
}

foreach ($required in @(
        'xm5_connection_probe',
        'transport_probe',
        'volume_observation_presence_supported = $true',
        'stop_event_supported = $true',
        'capability_only_signaling_hold_supported = $true')) {
    if (-not $candidateBuild.Contains($required)) {
        throw "The volume observation candidate build is missing: $required"
    }
}

foreach ($required in @(
        '--hold-signaling-seconds',
        'Signaling channel hold active for up to',
        'Signaling channel hold stop event observed.',
        'holdSignalingSeconds != 0u',
        '--discover without configuration or media')) {
    if (-not $transportSource.Contains($required)) {
        throw "The bounded capability-only signaling hold is missing: $required"
    }
}

foreach ($forbidden in @(
        '/install',
        '/uninstall',
        'Import-Certificate',
        'Disable-PnpDevice',
        'Enable-PnpDevice',
        'Restart-PnpDevice',
        'BluetoothSetServiceState',
        'SetDefaultEndpoint',
        'SetMasterVolume',
        'IAudioEndpointVolume::Set')) {
    if (($gate + $common + $system).IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The volume observation path contains a forbidden mutation: $forbidden"
    }
}

foreach ($forbiddenTransportArgument in @(
        "'--configure'",
        "'--media-session'",
        "'--stream-silence'",
        "'--stream-tone'",
        "'--stream-system'",
        "'--play-system'",
        "'--play-endpoint'")) {
    if ($gate.Contains($forbiddenTransportArgument)) {
        throw "The volume observation gate enables forbidden media/configuration: $forbiddenTransportArgument"
    }
}

foreach ($forbiddenPackagedFile in @(
        "'v1-swd-endpoint-system-common.ps1' =",
        "'v1-swd-volume-observation-common.ps1' =",
        "'run-v1-swd-volume-observation-gate.ps1' =")) {
    if ($candidateBuild.Contains($forbiddenPackagedFile)) {
        throw "The staged-only candidate packages a system gate: $forbiddenPackagedFile"
    }
}

foreach ($required in @(
        'v1_swd_volume_observation_policy',
        'v1_swd_volume_observation_evidence')) {
    if (-not $cmake.Contains($required)) {
        throw "The volume observation CMake contract is missing: $required"
    }
}

Write-Host 'V1 SWD volume observation policy tests passed.'
