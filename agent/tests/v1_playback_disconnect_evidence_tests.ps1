# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $root 'tools\v1-playback-disconnect-common.ps1')

$generation = 9
$state = [pscustomobject]@{
    mode='transport-pcm-burst-exercise'; state='stopped'
    physical_presence='absent'; render_demand='idle'
    acl_generation=$generation; connected_events=1; disconnected_events=1
    render_started_events=1; render_stopped_events=0
    pre_media_render_stop_events=0
    render_stop_deferred_events=0; render_stop_resumed_events=0
    render_stop_timeout_events=0; render_stop_acl_cancelled_events=0
    render_stop_pending=$false
    render_start_timed_out=$false
    publish_present_actions=1; publish_absent_actions=1
    fail_mute_actions=1; transport_open_actions=1
    transport_open_render_stability_ms=1000
    transport_open_stability_waits=1
    transport_open_stability_resets=0
    transport_open_stable_authorizations=1
    transport_open_stability_pending=$false
    maximum_transport_open_attempts=4
    pretransport_render_gap_tolerance=$true
    child_processes_started=1; engine_ready_events=1
    transport_open_executed=1; transport_open_attempts_for_generation=0
    transport_retryable_failures=0; transport_retries_scheduled=0
    transport_retry_budget_exhausted=0; media_started_events=1
    media_stopped_events=0; media_failed_events=0
    transport_graceful_stop_actions=0; transport_cancel_actions=1
    transport_stop_acknowledgements=1; engine_graceful_stops=1
    engine_exit_events=1; engine_unexpected_exits=0
    capabilities_discovered_events=0; pcm_burst_sessions_completed=0
    endpoint_sink_enabled=$true; endpoint_presence_updates=2
    endpoint_presence_failures=0; render_observer_enabled=$true
    engine_ready_observer_enabled=$true
    transport_worker_exercise_enabled=$true
    transport_discovery_exercise_enabled=$true
    transport_configuration_exercise_enabled=$true
    transport_silence_exercise_enabled=$false
    transport_pcm_burst_exercise_enabled=$true
    playback_disconnect_wait_enabled=$true
    playback_disconnect_fail_closed_release=$false
    render_query_count=1; render_query_failures=0
    engine_start_requests=1; engine_stop_requests=1
    engine_start_failures=0; engine_ready_timeouts=0
    engine_stop_failures=0; last_engine_exit_code=0
}
$session = [pscustomobject]@{
    session_generation=$generation; disposition='cancelled'
    strictly_retryable_open_failure=$false
    completed_full_duration=$false; ended_by_graceful_stop=$false
    ended_by_peer_close=$false
    target_duration_ms=60000; actual_duration_ms=8000
    open_attempts=1; signaling_exchanges=7; signaling_opened=$true
    peer_signaling_commands_received=0
    peer_discover_commands_accepted=0
    peer_capability_commands_accepted=0
    peer_configuration_commands_rejected=0
    peer_close_commands_accepted=0
    peer_signaling_read_timeouts=0
    last_signaling_response_size=2
    last_signaling_tx_header_available=$true
    last_signaling_tx_transaction_label=6
    last_signaling_tx_message_type=0; last_signaling_tx_signal_id=9
    last_signaling_rx_header_available=$true
    last_signaling_rx_transaction_label=6
    last_signaling_rx_message_type=2; last_signaling_rx_signal_id=9
    open_diagnostic_query_attempts=1
    open_diagnostic_query_error=0; open_diagnostic_query_bytes=48
    open_diagnostic_available=$true
    open_diagnostic_remote_response_valid=$false
    open_diagnostic_operation=1
    open_diagnostic_remote_bluetooth_address=96861751701723
    open_diagnostic_channel_flags=393216; open_diagnostic_flags=23
    open_diagnostic_psm=25; open_diagnostic_remote_no_resources=$false
    set_configuration_accepted=$true; avdtp_open_accepted=$true
    media_opened=$true; avdtp_start_accepted=$true
    media_started_notified=$true; media_packets_written=1378
    pcm_frames_sent=352768; pcm_frames_read=352768
    pre_start_pcm_frames_discarded=0
    fade_committed_sent_frames=352768; fade_blocks_prepared=2756
    fade_blocks_committed=2756; fade_commit_failures=0
    fade_sanitized_sample_count=0; maximum_gain_scalar=1.0
    maximum_output_peak_ceiling=1.0
    ceiling_ramp_start=1.0; ceiling_ramp_ms=0.0
    ceiling_ramp_last=1.0; limiter_attack_count=0
    limiter_gain_reduced_frames=0; limiter_gain_reduced_samples=0
    limiter_fallback_clamp_count=0; limiter_sanitized_sample_count=0
    volume_query_count=10; volume_change_count=1; volume_stable=$false
    volume_scalar_minimum=0.5; volume_scalar_maximum=0.6
    volume_scalar_last=0.6; volume_db_minimum=-6.0
    volume_db_maximum=-4.5; volume_db_last=-4.5
    consumer_lease_acquire_count=1; consumer_lease_release_count=1
    consumer_lease_acquired=$true; consumer_lease_released=$true
    avdtp_suspend_accepted=$false; avdtp_close_accepted=$false
    remote_stream_cleanup_required=$true
    close_attempted=$true; close_succeeded=$true
}
$attempts = @($session)
if (-not (Test-V1PlaybackDisconnectEvidence -State $state `
        -Session $session -Attempts $attempts -AgentExitCode 0)) {
    throw 'Valid single-attempt inbound playback-disconnect evidence was rejected.'
}

$unstableAuthorization = $state | ConvertTo-Json -Depth 12 | ConvertFrom-Json
$unstableAuthorization.transport_open_stable_authorizations = 0
if (Test-V1PlaybackDisconnectEvidence -State $unstableAuthorization `
        -Session $session -Attempts $attempts -AgentExitCode 0) {
    throw 'A transport OPEN without stable Render authorization was accepted.'
}
$pendingAuthorization = $state | ConvertTo-Json -Depth 12 | ConvertFrom-Json
$pendingAuthorization.transport_open_stability_pending = $true
if (Test-V1PlaybackDisconnectEvidence -State $pendingAuthorization `
        -Session $session -Attempts $attempts -AgentExitCode 0) {
    throw 'A pending transport OPEN authorization was accepted.'
}

$preMediaGapState = $state | ConvertTo-Json -Depth 12 | ConvertFrom-Json
$preMediaGapState.render_started_events = 2
$preMediaGapState.render_stopped_events = 1
$preMediaGapState.pre_media_render_stop_events = 1
$preMediaGapSession = $session | ConvertTo-Json -Depth 12 | ConvertFrom-Json
$preMediaGapSession.pre_start_pcm_frames_discarded = 12032
$preMediaGapSession.pcm_frames_read =
    [int64]$preMediaGapSession.pcm_frames_sent + 12032
if (-not (Test-V1PlaybackDisconnectEvidence -State $preMediaGapState `
        -Session $preMediaGapSession -Attempts @($preMediaGapSession) `
        -AgentExitCode 0)) {
    throw 'Valid pre-media Render gap and silent PCM evidence was rejected.'
}

$collisionSession = $session | ConvertTo-Json -Depth 12 | ConvertFrom-Json
$collisionSession.peer_signaling_commands_received = 1
$collisionSession.peer_discover_commands_accepted = 1
$collisionSession.signaling_exchanges = 8
if (-not (Test-V1PlaybackDisconnectEvidence -State $state `
        -Session $collisionSession -Attempts @($collisionSession) `
        -AgentExitCode 0)) {
    throw 'Valid peer-DISCOVER collision evidence was rejected.'
}
$capabilityCollision = $collisionSession |
    ConvertTo-Json -Depth 12 | ConvertFrom-Json
$capabilityCollision.peer_signaling_commands_received = 2
$capabilityCollision.peer_capability_commands_accepted = 1
$capabilityCollision.signaling_exchanges = 9
if (-not (Test-V1PlaybackDisconnectEvidence -State $state `
        -Session $capabilityCollision -Attempts @($capabilityCollision) `
        -AgentExitCode 0)) {
    throw 'Valid peer capability-query collision evidence was rejected.'
}
$configurationCollision = $capabilityCollision |
    ConvertTo-Json -Depth 12 | ConvertFrom-Json
$configurationCollision.peer_signaling_commands_received = 3
$configurationCollision.peer_configuration_commands_rejected = 1
$configurationCollision.signaling_exchanges = 10
if (-not (Test-V1PlaybackDisconnectEvidence -State $state `
        -Session $configurationCollision -Attempts @($configurationCollision) `
        -AgentExitCode 0)) {
    throw 'Valid peer configuration collision evidence was rejected.'
}
$peerClose = $session | ConvertTo-Json -Depth 12 | ConvertFrom-Json
$peerClose.peer_signaling_commands_received = 1
$peerClose.peer_close_commands_accepted = 1
$peerClose.ended_by_peer_close = $true
$peerClose.remote_stream_cleanup_required = $false
if (-not (Test-V1PlaybackDisconnectEvidence -State $state `
        -Session $peerClose -Attempts @($peerClose) `
        -AgentExitCode 0)) {
    throw 'Valid streaming peer-CLOSE evidence was rejected.'
}
$unansweredCollision = $collisionSession |
    ConvertTo-Json -Depth 12 | ConvertFrom-Json
$unansweredCollision.peer_discover_commands_accepted = 0
if (Test-V1PlaybackDisconnectEvidence -State $state `
        -Session $unansweredCollision -Attempts @($unansweredCollision) `
        -AgentExitCode 0) {
    throw 'Unanswered peer-DISCOVER collision evidence was accepted.'
}

function Copy-Object($Value) {
    $Value | ConvertTo-Json -Depth 12 | ConvertFrom-Json
}
function Assert-Rejected {
    param(
        [string]$Name,
        $CandidateState = $state,
        $CandidateSession = $session,
        [object[]]$CandidateAttempts = $attempts,
        [int]$ExitCode = 0
    )
    try {
        $accepted = Test-V1PlaybackDisconnectEvidence `
            -State $CandidateState -Session $CandidateSession `
            -Attempts $CandidateAttempts -AgentExitCode $ExitCode
    } catch {
        throw "Validator threw for negative case '$Name': $($_.Exception.Message)"
    }
    if ($accepted) {
        throw "Playback-disconnect evidence accepted negative case: $Name"
    }
}

$peerCloseWithoutAccept = Copy-Object $peerClose
$peerCloseWithoutAccept.peer_signaling_commands_received = 0
$peerCloseWithoutAccept.peer_close_commands_accepted = 0
Assert-Rejected -Name 'peer CLOSE ended without one accepted command' `
    -CandidateSession $peerCloseWithoutAccept `
    -CandidateAttempts @($peerCloseWithoutAccept)
$peerCloseWithRemoteCleanup = Copy-Object $peerClose
$peerCloseWithRemoteCleanup.remote_stream_cleanup_required = $true
Assert-Rejected -Name 'peer CLOSE still requires remote cleanup' `
    -CandidateSession $peerCloseWithRemoteCleanup `
    -CandidateAttempts @($peerCloseWithRemoteCleanup)
$peerCloseWithLocalCommands = Copy-Object $peerClose
$peerCloseWithLocalCommands.avdtp_suspend_accepted = $true
$peerCloseWithLocalCommands.avdtp_close_accepted = $true
Assert-Rejected -Name 'peer CLOSE mixed with local SUSPEND CLOSE' `
    -CandidateSession $peerCloseWithLocalCommands `
    -CandidateAttempts @($peerCloseWithLocalCommands)
$latePeerClose = Copy-Object $peerClose
$latePeerClose.actual_duration_ms = 60000
$latePeerClose.completed_full_duration = $true
Assert-Rejected -Name 'peer CLOSE arrived only at the media hard bound' `
    -CandidateSession $latePeerClose -CandidateAttempts @($latePeerClose)

$gracefulState = Copy-Object $state
$gracefulState.transport_graceful_stop_actions = 1
$gracefulState.transport_cancel_actions = 0
Assert-Rejected -Name 'graceful signaling used for physical disconnect' `
    -CandidateState $gracefulState
$unclassifiedPreMediaStop = Copy-Object $state
$unclassifiedPreMediaStop.render_started_events = 2
$unclassifiedPreMediaStop.render_stopped_events = 1
Assert-Rejected -Name 'unclassified pre-media Render STOP' `
    -CandidateState $unclassifiedPreMediaStop
$miscountedPreStartPcm = Copy-Object $session
$miscountedPreStartPcm.pcm_frames_read =
    [int64]$miscountedPreStartPcm.pcm_frames_sent + 12032
Assert-Rejected -Name 'unaccounted pre-start PCM reads' `
    -CandidateSession $miscountedPreStartPcm `
    -CandidateAttempts @($miscountedPreStartPcm)
$unalignedPreStartPcm = Copy-Object $session
$unalignedPreStartPcm.pre_start_pcm_frames_discarded = 1
$unalignedPreStartPcm.pcm_frames_read =
    [int64]$unalignedPreStartPcm.pcm_frames_sent + 1
Assert-Rejected -Name 'unaligned pre-start PCM discard count' `
    -CandidateSession $unalignedPreStartPcm `
    -CandidateAttempts @($unalignedPreStartPcm)
$remoteCleanup = Copy-Object $session
$remoteCleanup.avdtp_suspend_accepted = $true
$remoteCleanup.avdtp_close_accepted = $true
$remoteCleanup.remote_stream_cleanup_required = $false
Assert-Rejected -Name 'remote SUSPEND/CLOSE claimed during ACL loss' `
    -CandidateSession $remoteCleanup -CandidateAttempts @($remoteCleanup)
$short = Copy-Object $session
$short.actual_duration_ms = 4999
Assert-Rejected -Name 'disconnect before five seconds' `
    -CandidateSession $short -CandidateAttempts @($short)
$limited = Copy-Object $session
$limited.limiter_attack_count = 1
Assert-Rejected -Name 'limiter intervention' `
    -CandidateSession $limited -CandidateAttempts @($limited)
$outbound = Copy-Object $session
$outbound.open_diagnostic_flags = 7
Assert-Rejected -Name 'outbound signaling channel' `
    -CandidateSession $outbound -CandidateAttempts @($outbound)
$remoteResponse = Copy-Object $session
$remoteResponse.open_diagnostic_remote_response_valid = $true
$remoteResponse.open_diagnostic_flags = 31
Assert-Rejected -Name 'negative remote OPEN response' `
    -CandidateSession $remoteResponse -CandidateAttempts @($remoteResponse)
$timedOutRenderStop = Copy-Object $state
$timedOutRenderStop.render_stopped_events = 1
$timedOutRenderStop.render_stop_deferred_events = 1
$timedOutRenderStop.render_stop_timeout_events = 1
Assert-Rejected -Name 'classified Render STOP mixed into physical disconnect' `
    -CandidateState $timedOutRenderStop
$renderStartTimeout = Copy-Object $state
$renderStartTimeout.render_start_timed_out = $true
Assert-Rejected -Name 'render start startup timeout' `
    -CandidateState $renderStartTimeout
$reboundState = Copy-Object $state
$reboundState.render_started_events = 2
$reboundState.render_stopped_events = 1
$reboundState.render_stop_deferred_events = 1
$reboundState.render_stop_resumed_events = 1
if (-not (Test-V1PlaybackDisconnectEvidence -State $reboundState `
        -Session $session -Attempts @($session) `
        -AgentExitCode 0)) {
    throw 'A resumed Render STOP with dynamic volume telemetry was rejected.'
}
$retryState = Copy-Object $state
$retryState.transport_retryable_failures = 1
$retryState.transport_retries_scheduled = 1
Assert-Rejected -Name 'transport retry present' -CandidateState $retryState
Assert-Rejected -Name 'multiple transport attempts' `
    -CandidateAttempts @($session, $session)
$missingSession = Copy-Object $session
$missingSession.PSObject.Properties.Remove('open_diagnostic_flags')
Assert-Rejected -Name 'missing successful OPEN diagnostics' `
    -CandidateSession $missingSession -CandidateAttempts @($missingSession)
$contradictoryArchive = Copy-Object $session
$contradictoryArchive.actual_duration_ms = 9000
Assert-Rejected -Name 'final archive contradicts session' `
    -CandidateAttempts @($contradictoryArchive)
$unreleasedLease = Copy-Object $session
$unreleasedLease.consumer_lease_released = $false
Assert-Rejected -Name 'ConsumerLease not released' `
    -CandidateSession $unreleasedLease -CandidateAttempts @($unreleasedLease)
$stale = Copy-Object $session
$stale.session_generation = 8
Assert-Rejected -Name 'stale ACL generation' `
    -CandidateSession $stale -CandidateAttempts @($stale)

Write-Host 'V1 playback-disconnect evidence tests passed.'
