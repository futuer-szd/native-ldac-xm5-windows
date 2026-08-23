# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    Get-Content -LiteralPath (Join-Path $root $RelativePath) -Raw
}

$agent = Read-ProjectFile 'agent\v1_presence_agent.cpp'
$openStability = Read-ProjectFile 'agent\v1_transport_open_stability.cpp'
$openStabilityTests = Read-ProjectFile `
    'agent\tests\v1_transport_open_stability_tests.cpp'
$common = Read-ProjectFile 'tools\v1-playback-disconnect-common.ps1'
$gate = Read-ProjectFile 'tools\run-v1-playback-disconnect-gate.ps1'
$worker = Read-ProjectFile 'agent\v1_transport_normal_stop_worker.cpp'
$pcm = Read-ProjectFile 'agent\v1_transport_pcm_session.cpp'
$lifecycle = Read-ProjectFile 'agent\v1_lifecycle.cpp'
$endpointProbe = Read-ProjectFile 'tools\endpoint_volume_probe.cpp'
. (Join-Path $root 'tools\v1-playback-disconnect-common.ps1')

$transactionPath = 'C:\evidence\normal-stop-transaction.json'
$resultPath = 'C:\evidence\normal-stop-result.json'
$driverTree = '85a0b46231ae2f3212e6616346e2d6905314f0ff'
$transaction = [pscustomobject]@{
    schema_version = 1; transport_policy_version = 16
    status = 'normal-stop-verified'; source_commit = ('1' * 40)
    driver_tree = $driverTree; result = $resultPath
}
$result = [pscustomobject]@{
    schema_version = 1; transport_policy_version = 16
    normal_stop_passed = $true; source_commit = ('1' * 40)
    driver_tree = $driverTree; transaction = $transactionPath
    transport_open_attempts = 1; transport_retry_count = 0
    signaling_direction = 'inbound'; ended_by_graceful_stop = $true
    suspend_accepted = $true; close_accepted = $true
    consumer_lease_released = $true; physical_acl_disconnected = $true
    lifecycle_outcome = 'graceful-stop'
    driver_installed_or_updated = $false; rebooted = $false
    bluetooth_toggled = $false
}
$prerequisiteArguments = @{
    Transaction = $transaction; TransactionPath = $transactionPath
    Result = $result; ResultPath = $resultPath
    ExpectedDriverTree = $driverTree
}
if (-not (Test-V1PlaybackDisconnectPrerequisite @prerequisiteArguments)) {
    throw 'Valid policy 16 normal-stop prerequisite was rejected.'
}
$transaction.result = 'C:\evidence\substituted-result.json'
if (Test-V1PlaybackDisconnectPrerequisite @prerequisiteArguments) {
    throw 'A substituted normal-stop result path was accepted.'
}
$transaction.result = $resultPath
$result.source_commit = ('2' * 40)
if (Test-V1PlaybackDisconnectPrerequisite @prerequisiteArguments) {
    throw 'A normal-stop result from another source commit was accepted.'
}
$result.source_commit = ('1' * 40)
foreach ($required in @(
        'ArchiveTransportAttemptResultAt',
        'ending_transport_attempt',
        'engine_was_active',
        'V1ActionCancelTransport',
        'V1 cancelled transport result archive failed',
        'await_playback_disconnect',
        'playback_disconnect_wait_enabled',
        'playback_disconnect_failure_deadline = now + 30000u',
        'playback_disconnect_fail_closed_release',
        'render_stop_pending',
        'render_stop_resumed_events',
        'render_stop_timeout_events',
        'render_start_timed_out',
        'transport_open_stability_waits',
        'transport_open_stability_resets',
        'transport_open_stable_authorizations',
        'Observe a last-moment RUN edge before classifying',
        'V1 playback-disconnect fallback released the')) {
    if (-not $agent.Contains($required)) {
        throw "The disconnect final-attempt archive is missing: $required"
    }
}
foreach ($required in @(
        'ResetDisconnectedState',
        'V1ActionFailMute | V1ActionPublishEndpointAbsent',
        'V1ActionCancelTransport')) {
    if (-not $lifecycle.Contains($required)) {
        throw "The fail-closed disconnect reducer is missing: $required"
    }
}
foreach ($required in @(
        '#define V1_TRANSPORT_PCM_POST_START_STOP_CLASSIFICATION_TIMEOUT_MS 30000u')) {
    if (-not $worker.Contains($required)) {
        throw "The playback-disconnect worker policy is missing: $required"
    }
}
foreach ($required in @(
        'WaitForExplicitStop',
        'post_start_stop_classification_timeout_ms',
        'kMaximumPeerSignalingCommands = 3u',
        'BuildPeerDiscoverAccept',
        'BuildPeerCapabilitiesAccept',
        'BuildPeerSetConfigurationReject',
        'BuildPeerCloseAccept',
        'BeginPeerSignalingRead',
        'PollPeerSignalingRead',
        'SendPeerSignalingResponse',
        'peer_discover_commands_accepted',
        'peer_capability_commands_accepted',
        'peer_configuration_commands_rejected',
        'peer_close_commands_accepted',
        'last_signaling_tx_transaction_label',
        'last_signaling_rx_transaction_label',
        'V1TransportPcmStopDisposition::Cancel')) {
    if (-not $pcm.Contains($required)) {
        throw "The playback-disconnect PCM classification is missing: $required"
    }
}
foreach ($required in @(
        '$script:V1PlaybackDisconnectPolicyVersion = 20',
        'transport_open_render_stability_ms',
        'transport_open_stable_authorizations',
        'pre_media_render_stop_events',
        'render_stop_deferred_events',
        'render_stop_timeout_events -eq 0',
        'Test-V1PlaybackDisconnectPrerequisite',
        'transport_open_executed -ne $count',
        '$count -ne 1',
        'open_diagnostic_available -eq $true',
        'open_diagnostic_flags -band 0x17',
        'peer_signaling_commands_received -le 4',
        'peer_capability_commands_accepted -le',
        '[int]$Session.peer_close_commands_accepted',
        'pre_start_pcm_frames_discarded',
        '$pcmRead - $pcmSent - $preStartDiscarded',
        'transport_graceful_stop_actions -eq 0',
        'transport_cancel_actions -eq $count',
        'media_stopped_events -eq 0',
        'ended_by_graceful_stop -eq $false',
        'avdtp_suspend_accepted -eq $false',
        'avdtp_close_accepted -eq $false',
        'remote_stream_cleanup_required -eq $true',
        'ended_by_peer_close -eq $true',
        'consumer_lease_released -eq $true')) {
    if (-not $common.Contains($required)) {
        throw "The playback-disconnect evidence lock is missing: $required"
    }
}
$requiredGateText = @(
    '[ValidateRange(300,420)][int]$DurationSeconds = 360',
    '-ConfirmV1PlaybackDisconnect',
    'Test-V1PlaybackDisconnectPrerequisite',
    'Test-V1PlaybackDisconnectEvidence',
    '--exercise-transport-pcm-burst',
    '--pcm-fast-signaling-acquisition',
    '--transport-open-render-stability-ms',
    '--await-playback-disconnect',
    '--render-start-timeout-ms 45000',
    'endpoint_volume_probe.exe',
    '--monitor-state',
    'The candidate endpoint monitor is stale or does not support --monitor-state.',
    'The PCM/AVDTP session failed before media START:',
    'TX(label=', 'RX(label=',
    'Peer commands(received=',
    'if ($session.ended_by_peer_close -eq $true)',
    'Remote CLOSE was accepted during streaming;',
    'Physical ACL loss won the cleanup race;',
    'XM5 was not powered off before the sixty-second media hard bound.',
    'endpointActiveObserved',
    'change Windows volume once',
    'keep playback running for at least 10 seconds',
    'power off XM5 without pausing or closing the player',
    "Wait for 'V1 ACL disconnected'",
    'No AVDTP SUSPEND or CLOSE may be sent after physical ACL loss.',
    'transport_retries_scheduled -ne 0',
    'actual_duration_ms',
    "lifecycle_outcome = 'physical-disconnect-local-cancel'",
    'local_signaling_close_succeeded',
    'ConsumerLease and local resources were released.',
    'No reboot or rollback is required.')
foreach ($required in $requiredGateText) {
    if (-not $gate.Contains($required)) {
        throw "The playback-disconnect hardware gate is missing: $required"
    }
}
foreach ($required in @(
        'ObserveV1TransportOpenStability',
        'render_epoch != gate->render_epoch',
        'acl_generation != gate->acl_generation')) {
    if (-not $openStability.Contains($required)) {
        throw "The playback-disconnect stability gate is missing: $required"
    }
}
foreach ($required in @(
        'short RUN STOP resets',
        'stable RUN authorizes',
        'authorization occurs exactly once',
        'ACL generation change cancels')) {
    if (-not $openStabilityTests.Contains($required)) {
        throw "The playback-disconnect stability test is missing: $required"
    }
}
if (-not $endpointProbe.Contains('+0ms: %ls -> %ls (initial)')) {
    throw 'The endpoint state monitor does not publish its initial state.'
}
if (-not $endpointProbe.Contains('parsed > 600u')) {
    throw 'The endpoint state monitor cannot span all bounded gate durations.'
}
foreach ($forbidden in @(
        'pnputil', 'devcon', 'Restart-Computer', 'Disable-PnpDevice',
        'Enable-PnpDevice', 'Stop-Service', 'Start-Service',
        'SetDefaultEndpoint', 'Set-NativeLdacBluetoothRadioState')) {
    if ($gate.IndexOf(
            $forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The playback-disconnect gate mutates the baseline: $forbidden"
    }
}
$tokens = $null
$errors = $null
foreach ($relative in @(
        'tools\v1-playback-disconnect-common.ps1',
        'tools\run-v1-playback-disconnect-gate.ps1')) {
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $root $relative), [ref]$tokens, [ref]$errors)
    if (@($errors).Count -ne 0) {
        throw "The playback-disconnect PowerShell file does not parse: $relative"
    }
}

Write-Host 'V1 playback-disconnect policy tests passed.'
