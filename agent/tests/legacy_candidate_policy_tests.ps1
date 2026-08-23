# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))

function Read-ProjectFile {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    return Get-Content -LiteralPath `
        (Join-Path $projectRoot $RelativePath) -Raw
}

$builder = Read-ProjectFile 'tools\build-legacy-candidate.ps1'
$verifier = Read-ProjectFile 'tools\verify-legacy-candidate.ps1'
$installGate = Read-ProjectFile `
    'tools\run-legacy-install-rollback-gate.ps1'
$playbackGate = Read-ProjectFile `
    'tools\run-legacy-playback-gate.ps1'
$baseline = Read-ProjectFile 'tools\native-ldac-baseline-common.ps1'
$cleanup = Read-ProjectFile `
    'tools\cleanup-native-ldac-test-state.ps1'
$retiredEndpointRemover = Read-ProjectFile `
    'tools\remove-audio-endpoint.ps1'
$prepare = Read-ProjectFile 'tools\prepare-legacy-reboot-gate.ps1'
$postReboot = Read-ProjectFile `
    'tools\run-legacy-post-reboot-transport-gate.ps1'
$openDiagnosticCommon = Read-ProjectFile `
    'tools\legacy-open-diagnostic-common.ps1'
$connectionProbe = Read-ProjectFile 'tools\xm5_connection_probe.cpp'
$connectionProbe += Read-ProjectFile 'agent\xm5_acl_event.cpp'
$rollback = Read-ProjectFile 'tools\rollback-legacy-reboot-gate.ps1'

foreach ($required in @(
        'manifest_version = 3',
        'last_verified_driver_commit',
        'last_verified_driver_tree',
        'approved_diagnostic_driver_commit',
        'approved_diagnostic_driver_tree',
        'build-xm5-connection-probe.ps1',
        "'xm5_connection_probe.exe'",
        "'xm5_connection_probe.manifest.json'",
        "'HEAD:driver'",
        "first_hardware_gate = 'clean_baseline_install_reboot_acl_confirmed_single_open_diagnostic'",
        'requires_clean_original_a2dp = $true',
        'requires_reboot_before_avdtp = $true',
        'hot_swap_playback_forbidden = $true',
        'open_failure_telemetry_required = $true',
        'requires_acl_connect_event = $true',
        'requires_operator_power_confirmation = $true')) {
    if (-not $builder.Contains($required)) {
        throw "Legacy builder policy is missing: $required"
    }
}
if (-not $prepare.Contains('ConfirmPinImpactAndReboot') -or
    -not $prepare.Contains('invalidate Windows Hello PIN state') -or
    -not $prepare.Contains('current_driver_tree') -or
    -not $prepare.Contains('hasValidPowerCycleEvidence') -or
    -not $prepare.Contains('real ACL event and explicit physical power-on confirmation')) {
    throw 'The reboot preparation gate does not require PIN-impact consent or freeze a failed driver tree.'
}
foreach ($required in @(
        '[int]$manifest.manifest_version -ne 3',
        'current_driver_tree',
        'approved_diagnostic_driver_tree',
        'BluetoothFindFirstDevice_fConnected_no_inquiry',
        'The bundled read-only XM5 connection probe is not from the candidate source',
        'requires_reboot_before_avdtp',
        'hot_swap_playback_forbidden',
        'open_failure_telemetry_required',
        'requires_acl_connect_event',
        'requires_operator_power_confirmation')) {
    if (-not $verifier.Contains($required)) {
        throw "Legacy verifier policy is missing: $required"
    }
}
foreach ($retired in @($installGate, $playbackGate)) {
    if (-not $retired.Contains('same-boot') -or
        -not $retired.Contains('intentionally no command-line override') -or
        $retired.Contains('pnputil.exe') -or
        $retired.Contains('Invoke-LegacyPnpUtil')) {
        throw 'A retired same-boot gate can still modify a driver.'
    }
}
foreach ($required in @(
        'safe_original_a2dp',
        'clean_original_a2dp',
        'transport_test_packages',
        'native_audio_packages',
        'workspace_processes',
        'scheduled_tasks')) {
    if (-not $baseline.Contains($required)) {
        throw "Baseline inventory is missing: $required"
    }
}
foreach ($required in @(
        'xm5_connection_probe.manifest.json',
        'BluetoothFindFirstDevice_fConnected_no_inquiry',
        'GUID_BLUETOOTH_HCI_EVENT_acl_transition',
        'BluetoothIsConnectable_radio_state',
        'Get-FileHash',
        'ExpectedSourceCommit')) {
    if (-not $baseline.Contains($required)) {
        throw "Connection probe trust policy is missing: $required"
    }
}
foreach ($required in @(
        'ConfirmNativeLdacCleanup',
        'Get-NativeLdacXm5BluetoothState',
        "-OriginalInfNames @('NativeLdacAudio.inf')",
        "'/delete-driver'",
        'safe_original_a2dp',
        'and the shared test certificate were retained')) {
    if (-not $cleanup.Contains($required)) {
        throw "Native endpoint cleanup policy is missing: $required"
    }
}
if ($cleanup.Contains("'LdacNative.inf',") -or
    $cleanup.Contains("'NativeLdacDirectPdo.inf',")) {
    throw 'Native endpoint cleanup includes a transport package target.'
}
if (-not $retiredEndpointRemover.Contains(
        'legacy endpoint remover is retired') -or
    $retiredEndpointRemover.Contains('devcon.exe') -or
    $retiredEndpointRemover.Contains('pnputil.exe')) {
    throw 'The old endpoint remover can still mutate the system.'
}
foreach ($required in @(
        'ConfirmLegacyRebootPreparation',
        "Join-Path `$CandidatePath",
        'clean_original_a2dp',
        "Set-Service -Name 'AltA2dpSVC' -StartupType Manual",
        "status = 'awaiting_reboot'",
        "phase = 'installed_awaiting_reboot'",
        'prepared_boot_time_utc')) {
    if (-not $prepare.Contains($required)) {
        throw "Legacy reboot preparation policy is missing: $required"
    }
}
if ($prepare.Contains("@('--discover") -or
    $prepare.Contains("@('--play")) {
    throw 'Legacy reboot preparation opens Bluetooth or playback before reboot.'
}
foreach ($required in @(
        'ConfirmLegacyPostRebootTransport',
        "Join-Path `$candidatePath",
        '$currentBoot -le $preparedBoot.AddSeconds(1)',
        'capture-bluetooth-trace.ps1',
        'legacy-open-diagnostic-common.ps1',
        "@('--wait-acl-connect', '90')",
        'acl_event_observed_at',
        'physical_power_on_confirmed_at',
        "-cne 'XM5-ON'",
        "'-ProbePath', `$probePath",
        'open_diagnostic_reported',
        'remote_response',
        'diagnostic_disposition',
        'trace_capture',
        "status = 'transport_verified'",
        'No retry was attempted',
        'does not permit a Native audio endpoint')) {
    if (-not $postReboot.Contains($required)) {
        throw "Post-reboot transport policy is missing: $required"
    }
}
foreach ($required in @(
        'remote_psm_not_supported',
        'remote_security_block',
        'remote_no_resources',
        'no_valid_remote_response')) {
    if (-not $openDiagnosticCommon.Contains($required)) {
        throw "OPEN diagnostic classification is missing: $required"
    }
}
if ($postReboot.Contains('ldac_agent.exe') -or
    $postReboot.Contains('--play-endpoint') -or
    $postReboot.Contains('Wait-NativeLdacXm5Connected')) {
    throw 'The post-reboot transport gate includes media playback.'
}
$aclEventIndex = $postReboot.IndexOf(
    '$aclEvent = Invoke-LegacyNativeCapture',
    [StringComparison]::Ordinal)
$physicalConfirmationIndex = $postReboot.IndexOf(
    '$physicalConfirmation = Read-Host',
    [StringComparison]::Ordinal)
$freshPdoIndex = $postReboot.IndexOf(
    '$installed = Wait-LegacyXm5A2dpService',
    [StringComparison]::Ordinal)
$discoverIndex = $postReboot.IndexOf(
    '$discover = Invoke-LegacyNativeCapture',
    [StringComparison]::Ordinal)
if ($aclEventIndex -lt 0 -or $physicalConfirmationIndex -lt 0 -or
    $freshPdoIndex -lt 0 -or $discoverIndex -lt 0 -or
    $physicalConfirmationIndex -le $aclEventIndex -or
    $freshPdoIndex -le $physicalConfirmationIndex -or
    $discoverIndex -le $freshPdoIndex) {
    throw 'The post-reboot gate can reach L2CAP OPEN without a real ACL event and explicit physical power-on confirmation.'
}
foreach ($required in @(
        'GUID_BLUETOOTH_HCI_EVENT',
        'HCI_CONNECTION_TYPE_ACL',
        'target_address',
        'RegisterDeviceNotificationW',
        'XM5 ACL event:')) {
    if (-not $connectionProbe.Contains($required)) {
        throw "XM5 ACL watcher is missing: $required"
    }
}
foreach ($required in @(
        'ConfirmLegacyRebootRollback',
        'Get-NativeLdacXm5BluetoothState',
        'Remove-LegacyTestDriverPackages',
        'Restore-LegacyOriginalA2dp',
        "Set-Service -Name 'AltA2dpSVC' -StartupType Automatic",
        "status = 'rollback_verified'",
        'reboot_required_by_policy = $false',
        'this gate does not require a reboot')) {
    if (-not $rollback.Contains($required)) {
        throw "Legacy reboot rollback policy is missing: $required"
    }
}

Write-Host 'Legacy clean-baseline and cross-reboot candidate policy tests passed.'
