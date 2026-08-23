# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Medium')]
param(
    [switch]$ConfirmV1InboundSignalingCompletion,
    [string]$TransactionPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'v1-inbound-signaling-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1InboundSignalingCompletion) {
    throw 'Refusing to finalize delayed inbound-signaling evidence. Re-run with -ConfirmV1InboundSignalingCompletion.'
}
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$trialRoot = Join-Path $root 'artifacts\v1-inbound-signaling\trial'
if ([string]::IsNullOrWhiteSpace($TransactionPath)) {
    $latest = Join-Path $trialRoot 'latest-transaction.txt'
    if (-not (Test-Path -LiteralPath $latest -PathType Leaf)) {
        throw 'No inbound-signaling transaction was found.'
    }
    $TransactionPath = (Get-Content -LiteralPath $latest -Raw).Trim()
}
$TransactionPath = [System.IO.Path]::GetFullPath($TransactionPath)
$transaction = Get-Content -LiteralPath $TransactionPath -Raw |
    ConvertFrom-Json
if ([int]$transaction.schema_version -ne 1 -or
    [int]$transaction.transport_policy_version -ne
        $script:V1InboundSignalingPolicyVersion -or
    [string]$transaction.status -notin @(
        'rollback-required', 'finalization-required') -or
    $transaction.rollback.attempted -ne $false -or
    $null -eq $transaction.validation) {
    throw 'The selected transaction is not eligible for delayed-disconnect completion.'
}

$candidate = Get-V1InboundSignalingCandidate `
    -CandidatePath ([string]$transaction.candidate_path)
$manifest = $candidate.manifest
if ([string]$manifest.source_commit -ne [string]$transaction.source_commit -or
    [string]$manifest.driver_tree -ne [string]$transaction.driver_tree) {
    throw 'The stored candidate no longer matches the transaction.'
}
$connectionProbe = Join-Path $candidate.root 'xm5_connection_probe.exe'
if ((Get-NativeLdacBluetoothRadioState `
        -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$transaction.source_commit)) -ne
    'ready') {
    throw 'Windows Bluetooth must remain on for disconnect verification.'
}
if ((Get-NativeLdacXm5BluetoothState `
        -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$transaction.source_commit)) -ne
    'disconnected') {
    throw 'Keep XM5 off and wait until its public Bluetooth state is disconnected.'
}
if (@(Get-NativeLdacWorkspaceProcesses).Count -ne 0) {
    throw 'Close all workspace media and agent processes before completion.'
}
$devices = @(Get-LegacyXm5A2dpDevices)
if ($devices.Count -ne 1) {
    throw 'Exactly one present XM5 A2DP Sink service PDO is required.'
}
$binding = Get-LegacyXm5A2dpSnapshot -Device $devices[0]
if ([string]$binding.service -ne 'LdacNative' -or
    [int]$binding.problem_code -ne 0 -or
    -not ([string]$binding.published_inf).Equals(
        [string]$transaction.installed_inf,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw ('The verified inbound-signaling driver binding is not healthy: ' +
        "service $($binding.service), INF $($binding.published_inf), " +
        "problem code $($binding.problem_code). No completion data was written.")
}
$transportProbe = Join-Path $candidate.root 'transport_probe.exe'
$ready = Wait-V1InboundTransportInfo -ProbePath $transportProbe `
    -ExpectedFlags $script:V1InboundReadyFlags -TimeoutSeconds 2
if ($null -eq $ready) {
    throw 'The installed driver no longer exposes ABI 0.5 ready flags 0xF.'
}

$originalResultPath = [string]$transaction.validation.result
$summaryPath = [string]$transaction.validation.l2cap_summary
$directory = [string]$transaction.validation.directory
$discoverPath = Join-Path $directory 'discover.log'
foreach ($path in @($originalResultPath, $summaryPath, $discoverPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required inbound-signaling evidence is missing: $path"
    }
}
$original = Get-Content -LiteralPath $originalResultPath -Raw |
    ConvertFrom-Json
$summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
$discoverText = Get-Content -LiteralPath $discoverPath -Raw
if ([int]$original.schema_version -ne 1 -or
    [int]$original.transport_policy_version -ne
        $script:V1InboundSignalingPolicyVersion -or
    [string]$original.source_commit -ne [string]$transaction.source_commit -or
    [string]$original.driver_tree -ne [string]$transaction.driver_tree -or
    -not (Test-V1InboundDiscoveryCoreEvidence `
        -CaptureFailure $original.capture_failure `
        -ConnectExitCode $(if ($original.acl_connect_observed) { 0 } else {
            -1
        }) `
        -DiscoverExitCode ([int]$original.discover_exit_code) `
        -DiscoverText $discoverText `
        -FinalDiagnosticText ([string]$original.inbound_open_diagnostic) `
        -Summary $summary `
        -SetConfigurationCommands ([int]$original.set_configuration_commands) `
        -AvdtpOpenCommands ([int]$original.avdtp_open_commands) `
        -AvdtpStartCommands ([int]$original.avdtp_start_commands) `
        -MediaL2capOpenCommands ([int]$original.media_l2cap_open_commands) `
        -MediaPackets ([int]$original.media_packets))) {
    throw 'The stored DISCOVER evidence does not satisfy the inbound core contract.'
}

Write-Host 'Inbound DISCOVER core evidence and current physical disconnect are verified.'
Write-Host 'This completion writes only result/transaction JSON and leaves LdacNative installed.'
if (-not $PSCmdlet.ShouldProcess(
        $TransactionPath,
        'Finalize the successful inbound DISCOVER after delayed physical disconnect')) {
    return
}

$resultPath = Join-Path $directory 'completed-result.json'
$result = [ordered]@{
    schema_version = 1
    transport_policy_version = $script:V1InboundSignalingPolicyVersion
    source_commit = [string]$transaction.source_commit
    driver_tree = [string]$transaction.driver_tree
    passed = $true
    core_passed = $true
    finalized_after_delayed_disconnect = $true
    finalized_at = (Get-Date).ToString('o')
    transaction = $TransactionPath
    original_result = $originalResultPath
    current_physical_disconnect_verified = $true
    acl_connect_observed = $original.acl_connect_observed
    acl_disconnect_observed_in_gate = $original.acl_disconnect_observed
    discover_exit_code = [int]$original.discover_exit_code
    inbound_open_diagnostic = [string]$original.inbound_open_diagnostic
    l2cap_summary = $summaryPath
    inbound_avdtp_connection_requests =
        [int]$summary.inbound_avdtp_connection_requests
    outbound_avdtp_connection_requests =
        [int]$summary.outbound_avdtp_connection_requests
    outbound_success_responses_to_inbound_avdtp =
        [int]$summary.outbound_success_responses_to_inbound_avdtp
    inbound_no_resources_responses =
        [int]$summary.inbound_no_resources_responses
    set_configuration_commands = 0
    avdtp_open_commands = 0
    avdtp_start_commands = 0
    media_l2cap_open_commands = 0
    media_packets = 0
    driver_left_installed = $true
    rebooted = $false
    bluetooth_toggled = $false
}
Write-LegacyJsonAtomic -Value $result -Path $resultPath
$transaction.validation.passed = $true
$transaction.validation | Add-Member `
    -NotePropertyName original_result `
    -NotePropertyValue $originalResultPath -Force
$transaction.validation.result = $resultPath
$transaction.validation | Add-Member `
    -NotePropertyName finalized_after_delayed_disconnect `
    -NotePropertyValue $true -Force
$transaction.validation | Add-Member `
    -NotePropertyName current_physical_disconnect_verified `
    -NotePropertyValue $true -Force
$transaction.status = 'inbound-discovery-verified'
$transaction.phase = 'complete'
$transaction.error = $null
$transaction.updated_at = (Get-Date).ToString('o')
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath

Write-Host 'V1 inbound-signaling DISCOVER finalized successfully.'
Write-Host "Inbound PSM 0x0019: $($result.inbound_avdtp_connection_requests); outbound PSM 0x0019: 0; remote NO_RESOURCES: 0."
Write-Host 'No SET_CONFIGURATION, media OPEN, START, or media packet occurred.'
Write-Host 'LdacNative remains installed; no reboot, radio toggle, or rollback occurred.'
Write-Host "Result: $resultPath"
