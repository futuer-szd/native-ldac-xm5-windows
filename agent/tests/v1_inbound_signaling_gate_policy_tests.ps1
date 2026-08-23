# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    Get-Content -LiteralPath (Join-Path $root $RelativePath) -Raw
}

$common = Read-ProjectFile 'tools\v1-inbound-signaling-common.ps1'
$build = Read-ProjectFile 'tools\build-v1-inbound-signaling-candidate.ps1'
$prepare = Read-ProjectFile 'tools\prepare-v1-inbound-signaling-gate.ps1'
$gate = Read-ProjectFile `
    'tools\run-v1-inbound-signaling-discovery-gate.ps1'
$rollback = Read-ProjectFile `
    'tools\rollback-v1-inbound-signaling-gate.ps1'
$completion = Read-ProjectFile `
    'tools\complete-v1-inbound-signaling-gate.ps1'
$summary = Read-ProjectFile 'tools\summarize-bluetooth-l2cap-trace.ps1'
$connectionProbe = Read-ProjectFile 'tools\xm5_connection_probe.cpp'
. (Join-Path $root 'tools\v1-inbound-signaling-common.ps1')

foreach ($required in @(
        '$script:V1InboundReadyFlags = 0x0000000F',
        'read_only_handles_preserve_inbound_channel',
        'hci_proves_no_outbound_psm25_request',
        'Restore-V1InboundPreviousDriver',
        "'/restart-device'",
        'previous_ready_flags')) {
    if (-not $common.Contains($required)) {
        throw "The inbound-signaling common contract is missing: $required"
    }
}
$validSummary = [pscustomobject]@{
    inbound_avdtp_connection_requests = 2
    outbound_avdtp_connection_requests = 0
    outbound_success_responses_to_inbound_avdtp = 2
    outbound_rejections_to_inbound_avdtp = 0
    inbound_no_resources_responses = 0
    inbound_avdtp_pending_without_success = $false
}
$validCore = @{
    CaptureFailure = $null
    ConnectExitCode = 0
    DiscoverExitCode = 0
    DiscoverText = "Selected LDAC audio sink SEID: 3`r`n" +
        "Signaling channel closed.`r`n"
    FinalDiagnosticText = "Signaling channel direction: inbound.`r`n" +
        "L2CAP OPEN state: completed, succeeded.`r`n"
    Summary = $validSummary
    SetConfigurationCommands = 0
    AvdtpOpenCommands = 0
    AvdtpStartCommands = 0
    MediaL2capOpenCommands = 0
    MediaPackets = 0
}
if (-not (Test-V1InboundDiscoveryCoreEvidence @validCore)) {
    throw 'Valid delayed-disconnect DISCOVER core evidence was rejected.'
}
$validCore.MediaPackets = 1
if (Test-V1InboundDiscoveryCoreEvidence @validCore) {
    throw 'Inbound completion accepted media payload evidence.'
}
$validCore.MediaPackets = 0
$validCore.DiscoverText = ''
if (Test-V1InboundDiscoveryCoreEvidence @validCore) {
    throw 'Inbound completion accepted missing DISCOVER output.'
}
$validCore.DiscoverText = "Selected LDAC audio sink SEID: 3`r`n" +
    "Signaling channel closed.`r`n"
$validSummary.outbound_avdtp_connection_requests = 1
if (Test-V1InboundDiscoveryCoreEvidence @validCore) {
    throw 'Inbound completion accepted an outbound PSM 0x0019 request.'
}
$validSummary.outbound_avdtp_connection_requests = 0
foreach ($required in @(
        'build-legacy-candidate.ps1',
        'required_ready_flags',
        'single_discover_no_media',
        'No driver, service, process, Bluetooth request, or system setting was changed.')) {
    if (-not $build.Contains($required)) {
        throw "The inbound-signaling build contract is missing: $required"
    }
}
foreach ($required in @(
        'ConfirmV1InboundSignalingCompletion',
        'Test-V1InboundDiscoveryCoreEvidence',
        'current_physical_disconnect_verified',
        'finalized_after_delayed_disconnect',
        "status = 'inbound-discovery-verified'",
        'No SET_CONFIGURATION, media OPEN, START, or media packet occurred.')) {
    if (-not $completion.Contains($required)) {
        throw "The delayed-disconnect completion contract is missing: $required"
    }
}
foreach ($forbidden in @(
        'pnputil', 'Restart-Computer', 'Disable-PnpDevice',
        'Enable-PnpDevice', 'Stop-Service', 'Start-Service',
        'IOCTL_LDAC_NATIVE_OPEN_SIGNALING', "'--discover'")) {
    if ($completion.IndexOf(
            $forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The evidence-only completion mutates or reopens transport: $forbidden"
    }
}
foreach ($required in @(
        'ConfirmV1InboundDriverUpdate',
        "'/export-driver'",
        "'/add-driver'",
        "'/restart-device'",
        'ready flags 0xF',
        'Restore-V1InboundPreviousDriver',
        'No Windows reboot or Bluetooth radio toggle is authorized')) {
    if (-not $prepare.Contains($required)) {
        throw "The inbound-signaling preparation contract is missing: $required"
    }
}
foreach ($forbidden in @(
        'Restart-Computer', 'Disable-PnpDevice', 'Enable-PnpDevice',
        'Set-Service', 'Start-Service', 'Stop-Service')) {
    if ($prepare.Contains($forbidden)) {
        throw "Preparation contains a forbidden system mutation: $forbidden"
    }
}
foreach ($required in @(
        'ConfirmV1InboundSignalingDiscovery',
        "@('--discover', '--open-attempts', '1')",
        "@('--open-diagnostics')",
        'Signaling channel direction: inbound',
        'L2CAP OPEN state: completed, succeeded',
        'Test-V1InboundDiscoveryCoreEvidence',
        'set_configuration_commands = 0',
        'avdtp_start_commands = 0',
        'media_packets = 0',
        'Wait-V1InboundPublicDisconnect',
        'Invoke-V1InboundStreamingAclProbe',
        'public_bluetooth_disconnect_observed',
        'public-disconnect-convergence.json',
        'Restore-V1InboundPreviousDriver')) {
    if (-not $gate.Contains($required)) {
        throw "The inbound-signaling validation gate is missing: $required"
    }
}
foreach ($required in @(
        'outbound_avdtp_connection_requests -eq 0',
        'outbound_success_responses_to_inbound_avdtp -ge 1',
        'outbound_rejections_to_inbound_avdtp -eq 0',
        'inbound_no_resources_responses -eq 0',
        'inbound_avdtp_pending_without_success -eq $false')) {
    if (-not $common.Contains($required)) {
        throw "The inbound DISCOVER core validator is missing: $required"
    }
}
$connectArm = $connectionProbe.IndexOf(
    'XM5 ACL watcher armed. Turn on XM5 normally now.')
$disconnectArm = $connectionProbe.IndexOf(
    'XM5 ACL watcher armed. Turn off XM5 normally now.')
$registerNotification = $connectionProbe.IndexOf(
    'RegisterDeviceNotificationW(')
$secondStateCheck = $connectionProbe.IndexOf(
    'const native_ldac::agent::Xm5ConnectionState armed_state',
    $registerNotification)
if ($registerNotification -lt 0 -or
    $secondStateCheck -le $registerNotification -or
    $connectArm -le $secondStateCheck -or
    $disconnectArm -le $secondStateCheck -or
    -not $connectionProbe.Contains('std::fflush(stdout);')) {
    throw 'The operator ACL transition prompt can run before the watcher is armed.'
}
foreach ($forbidden in @(
        "Write-Host 'Turn on XM5 normally now.",
        "Write-Host 'Turn off XM5 now;")) {
    if ($gate.Contains($forbidden)) {
        throw "The gate prompts before ACL watcher readiness: $forbidden"
    }
}
$publicDisconnectStart = $gate.IndexOf(
    '$publicDisconnect = Wait-V1InboundPublicDisconnect')
$rollbackStart = $gate.IndexOf(
    'Restore-V1InboundPreviousDriver', $publicDisconnectStart)
if ($publicDisconnectStart -lt 0 -or
    $rollbackStart -le $publicDisconnectStart -or
    -not $gate.Contains(
        '$publicDisconnect.disconnected) {')) {
    throw 'PnP rollback is not gated on public Bluetooth disconnection.'
}
foreach ($required in @(
        'function Test-V1InboundPublicDisconnectedCapture',
        '[int]$Capture.exit_code -eq 10',
        'XM5 Bluetooth state: disconnected\.\r?$',
        'function Wait-V1InboundPublicDisconnect',
        "@('--state')",
        'poll_attempts',
        'elapsed_ms')) {
    if (-not $common.Contains($required)) {
        throw "The public disconnect convergence contract is missing: $required"
    }
}
$disconnectedCrlf = [pscustomobject]@{
    exit_code = 10
    stdout = "XM5 Bluetooth state: disconnected.`r`n"
    stderr = ''
}
$connectedCrlf = [pscustomobject]@{
    exit_code = 0
    stdout = "XM5 Bluetooth state: connected.`r`n"
    stderr = ''
}
if (-not (Test-V1InboundPublicDisconnectedCapture `
        -Capture $disconnectedCrlf) -or
    (Test-V1InboundPublicDisconnectedCapture `
        -Capture $connectedCrlf)) {
    throw 'Exit code 10/CRLF public disconnect semantics are not strict.'
}
$crlfDiagnostic = "Signaling channel direction: inbound.`r`n" +
    "L2CAP OPEN state: completed, succeeded.`r`n"
if ($crlfDiagnostic -notmatch
        '(?m)^Signaling channel direction: inbound\.\r?$' -or
    $crlfDiagnostic -notmatch
        '(?m)^L2CAP OPEN state: completed, succeeded\.\r?$' -or
    -not $gate.Contains('incoming-open-diagnostics-last.log')) {
    throw 'The inbound diagnostics gate does not accept or preserve native CRLF output.'
}
foreach ($forbidden in @(
        "'--configure'", "'--media-session'", "'--stream-silence'",
        "'--stream-tone'", "'--stream-system'", 'Restart-Computer')) {
    if ($gate.Contains($forbidden)) {
        throw "The DISCOVER-only gate contains a forbidden action: $forbidden"
    }
}
foreach ($required in @(
        'ConfirmV1InboundSignalingRollback',
        'Restore-V1InboundPreviousDriver',
        'No reboot or Windows Bluetooth toggle occurred.')) {
    if (-not $rollback.Contains($required)) {
        throw "The inbound-signaling rollback contract is missing: $required"
    }
}
foreach ($required in @(
        'outbound_pending_responses_to_inbound_avdtp',
        'outbound_success_responses_to_inbound_avdtp',
        'outbound_rejections_to_inbound_avdtp',
        'inbound_avdtp_pending_without_success')) {
    if (-not $summary.Contains($required)) {
        throw "The L2CAP summary is missing inbound response evidence: $required"
    }
}

foreach ($relative in @(
        'tools\v1-inbound-signaling-common.ps1',
        'tools\build-v1-inbound-signaling-candidate.ps1',
        'tools\prepare-v1-inbound-signaling-gate.ps1',
        'tools\run-v1-inbound-signaling-discovery-gate.ps1',
        'tools\complete-v1-inbound-signaling-gate.ps1',
        'tools\rollback-v1-inbound-signaling-gate.ps1')) {
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $root $relative), [ref]$tokens, [ref]$errors)
    if (@($errors).Count -ne 0) {
        throw "The inbound-signaling PowerShell file does not parse: $relative"
    }
}

Write-Host 'V1 inbound-signaling gate policy tests passed.'
