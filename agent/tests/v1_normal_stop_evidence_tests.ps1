# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $root 'tools\v1-normal-stop-common.ps1')

$generation = 7
$state = [pscustomobject]@{
    mode='transport-pcm-burst-exercise'; state='stopped'
    physical_presence='absent'; render_demand='idle'
    acl_generation=$generation; connected_events=1; disconnected_events=1
    render_started_events=1; render_stopped_events=1
    pre_media_render_stop_events=0
    render_stop_deferred_events=1; render_stop_resumed_events=0
    render_stop_timeout_events=1; render_stop_acl_cancelled_events=0
    render_stop_pending=$false
    child_processes_started=1; engine_ready_events=1
    transport_open_executed=1; transport_open_attempts_for_generation=0
    transport_open_render_stability_ms=1000
    transport_open_stability_waits=1
    transport_open_stability_resets=0
    transport_open_stable_authorizations=1
    transport_open_stability_pending=$false
    transport_retryable_failures=0; transport_retries_scheduled=0
    transport_retry_budget_exhausted=0; media_started_events=1
    media_stopped_events=1; media_failed_events=0
    transport_graceful_stop_actions=1; transport_stop_acknowledgements=1
    engine_graceful_stops=1; engine_exit_events=1
    engine_unexpected_exits=0
}
$retry = [pscustomobject]@{
    session_generation=$generation; disposition='backend-failure'
    stage=1; backend_error=71; open_attempts=1; signaling_exchanges=0
    signaling_opened=$false; strictly_retryable_open_failure=$true
    open_diagnostic_query_attempts=1
    open_diagnostic_query_error=0
    open_diagnostic_query_bytes=48
    open_diagnostic_available=$true
    open_diagnostic_remote_response_valid=$true
    open_diagnostic_sequence=1; open_diagnostic_operation=1
    open_diagnostic_io_status=-1073741616
    open_diagnostic_brb_status=-1073741616
    open_diagnostic_bluetooth_status=255
    open_diagnostic_remote_bluetooth_address=96861751701723
    open_diagnostic_channel_flags=393216; open_diagnostic_flags=11
    open_diagnostic_psm=25
    open_diagnostic_response=4; open_diagnostic_response_status=0
    open_diagnostic_remote_no_resources=$true
    media_packets_written=0; consumer_lease_acquire_count=1
    consumer_lease_release_count=1; consumer_lease_released=$true
}
$session = [pscustomobject]@{
    session_generation=$generation; disposition='cancelled'
    strictly_retryable_open_failure=$false
    completed_full_duration=$false; ended_by_graceful_stop=$true
    target_duration_ms=60000; actual_duration_ms=8000
    open_diagnostic_query_attempts=1
    open_diagnostic_query_error=0; open_diagnostic_query_bytes=48
    open_diagnostic_available=$true
    open_diagnostic_remote_response_valid=$false
    open_diagnostic_operation=1
    open_diagnostic_remote_bluetooth_address=96861751701723
    open_diagnostic_channel_flags=393216; open_diagnostic_flags=23
    open_diagnostic_psm=25
    open_diagnostic_remote_no_resources=$false
    open_attempts=1; signaling_exchanges=11; signaling_opened=$true
    set_configuration_accepted=$true; avdtp_open_accepted=$true
    media_opened=$true; avdtp_start_accepted=$true
    media_started_notified=$true; media_packets_written=1378
    pcm_frames_sent=352768; pcm_frames_read=352768
    transport_frames_sent=353792
    startup_silence_ms=20.0; startup_silence_frames_sent=1024
    startup_silence_packets_written=4
    fade_committed_sent_frames=352768; fade_blocks_prepared=2756
    fade_blocks_committed=2756; fade_commit_failures=0
    fade_sanitized_sample_count=0; maximum_gain_scalar=1.0
    boundary_resume_count=0
    boundary_resume_fade_frames=0
    maximum_output_peak_ceiling=1.0
    ceiling_ramp_start=1.0; ceiling_ramp_ms=0.0
    ceiling_ramp_last=1.0; limiter_attack_count=0
    limiter_gain_reduced_frames=0; limiter_gain_reduced_samples=0
    limiter_fallback_clamp_count=0; limiter_sanitized_sample_count=0
    volume_query_count=10; volume_change_count=0; volume_stable=$true
    volume_scalar_minimum=0.5; volume_scalar_maximum=0.5
    volume_scalar_last=0.5; volume_db_minimum=-6.0
    volume_db_maximum=-6.0; volume_db_last=-6.0
    consumer_lease_acquire_count=1; consumer_lease_release_count=1
    consumer_lease_released=$true; avdtp_suspend_accepted=$true
    avdtp_close_accepted=$true; remote_stream_cleanup_required=$false
    close_attempted=$true; close_succeeded=$true
}
$attempts = @($session)
if (-not (Test-V1NormalStopEvidence -State $state -Session $session `
        -Attempts $attempts -AgentExitCode 0)) {
    throw 'Valid normal-stop evidence was rejected.'
}

$preMediaGapState = $state | ConvertTo-Json -Depth 12 | ConvertFrom-Json
$preMediaGapState.render_started_events = 2
$preMediaGapState.render_stopped_events = 2
$preMediaGapState.pre_media_render_stop_events = 1
$preMediaGapState.transport_open_stability_resets = 1
if (-not (Test-V1NormalStopEvidence -State $preMediaGapState `
        -Session $session -Attempts $attempts -AgentExitCode 0)) {
    throw 'Valid pre-authorization Render stability reset was rejected.'
}

function Copy-Object($Value) {
    $Value | ConvertTo-Json -Depth 12 | ConvertFrom-Json
}
$changedVolume = Copy-Object $session
$changedVolume.volume_change_count = 1
$changedVolume.volume_stable = $false
$changedVolume.volume_scalar_minimum = 0.25
$changedVolume.volume_scalar_last = 0.5
$changedVolume.volume_db_minimum = -12.0
if (-not (Test-V1NormalStopEvidence -State $state `
        -Session $changedVolume -Attempts @($changedVolume) `
        -AgentExitCode 0)) {
    throw 'A normal-stop session with coherent dynamic volume telemetry was rejected.'
}
$abandonedPreparedBlock = Copy-Object $session
$abandonedPreparedBlock.fade_blocks_prepared++
if (-not (Test-V1NormalStopEvidence -State $state `
        -Session $abandonedPreparedBlock `
        -Attempts @($abandonedPreparedBlock) -AgentExitCode 0)) {
    throw 'A graceful STOP with one prepared but unsent fade block was rejected.'
}
$tooManyAbandonedBlocks = Copy-Object $session
$tooManyAbandonedBlocks.fade_blocks_prepared += 2
if (Test-V1NormalStopEvidence -State $state `
        -Session $tooManyAbandonedBlocks `
        -Attempts @($tooManyAbandonedBlocks) -AgentExitCode 0) {
    throw 'Evidence with two prepared but unsent fade blocks was accepted.'
}
$badCommittedFrameCount = Copy-Object $session
$badCommittedFrameCount.fade_blocks_committed--
if (Test-V1NormalStopEvidence -State $state `
        -Session $badCommittedFrameCount `
        -Attempts @($badCommittedFrameCount) -AgentExitCode 0) {
    throw 'Evidence with mismatched committed fade blocks and frames was accepted.'
}
$badState = Copy-Object $state
$badState.disconnected_events = 0
if (Test-V1NormalStopEvidence -State $badState -Session $session `
        -Attempts $attempts -AgentExitCode 0) {
    throw 'Evidence without physical ACL disconnect was accepted.'
}
$badState = Copy-Object $state
$badState.transport_graceful_stop_actions = 0
if (Test-V1NormalStopEvidence -State $badState -Session $session `
        -Attempts $attempts -AgentExitCode 0) {
    throw 'Evidence without reducer graceful STOP was accepted.'
}
$badState = Copy-Object $state
$badState.transport_open_stable_authorizations = 0
if (Test-V1NormalStopEvidence -State $badState -Session $session `
        -Attempts $attempts -AgentExitCode 0) {
    throw 'Evidence without a stable Render authorization was accepted.'
}
$badState = Copy-Object $state
$badState.transport_open_stability_pending = $true
if (Test-V1NormalStopEvidence -State $badState -Session $session `
        -Attempts $attempts -AgentExitCode 0) {
    throw 'Evidence with a pending transport authorization was accepted.'
}
$short = Copy-Object $session
$short.actual_duration_ms = 4999
if (Test-V1NormalStopEvidence -State $state -Session $short `
        -Attempts @($short) -AgentExitCode 0) {
    throw 'A session stopped before five seconds was accepted.'
}
$full = Copy-Object $session
$full.actual_duration_ms = 60000
$full.completed_full_duration = $true
$full.ended_by_graceful_stop = $false
if (Test-V1NormalStopEvidence -State $state -Session $full `
        -Attempts @($full) -AgentExitCode 0) {
    throw 'A session that reached its hard duration was accepted.'
}
$limited = Copy-Object $session
$limited.limiter_attack_count = 1
$limited.limiter_gain_reduced_frames = 128
$limited.limiter_gain_reduced_samples = 256
if (Test-V1NormalStopEvidence -State $state -Session $limited `
        -Attempts @($limited) -AgentExitCode 0) {
    throw 'A normal-stop session with limiter intervention was accepted.'
}
$ramped = Copy-Object $session
$ramped.ceiling_ramp_start = 0.25
$ramped.ceiling_ramp_ms = 2000.0
if (Test-V1NormalStopEvidence -State $state -Session $ramped `
        -Attempts @($ramped) -AgentExitCode 0) {
    throw 'A normal-stop session with the old ceiling ramp was accepted.'
}
$retryState = Copy-Object $state
$retryState.child_processes_started = 2
$retryState.engine_ready_events = 2
$retryState.transport_open_executed = 2
$retryState.transport_retryable_failures = 1
$retryState.transport_retries_scheduled = 1
$retryState.transport_stop_acknowledgements = 2
$retryState.engine_graceful_stops = 2
$retryState.engine_exit_events = 2
if (Test-V1NormalStopEvidence -State $retryState -Session $session `
        -Attempts @($retry, $session) -AgentExitCode 0) {
    throw 'A normal-stop session requiring a transport retry was accepted.'
}
$unreadableDiagnostic = Copy-Object $session
$unreadableDiagnostic.open_diagnostic_available = $false
if (Test-V1NormalStopEvidence -State $state `
        -Session $unreadableDiagnostic -Attempts @($unreadableDiagnostic) `
        -AgentExitCode 0) {
    throw 'A successful OPEN without readable diagnostics was accepted.'
}
$outboundDiagnostic = Copy-Object $session
$outboundDiagnostic.open_diagnostic_flags = 7
if (Test-V1NormalStopEvidence -State $state `
        -Session $outboundDiagnostic -Attempts @($outboundDiagnostic) `
        -AgentExitCode 0) {
    throw 'An outbound signaling OPEN was accepted.'
}
$remoteResponseDiagnostic = Copy-Object $session
$remoteResponseDiagnostic.open_diagnostic_remote_response_valid = $true
$remoteResponseDiagnostic.open_diagnostic_flags = 31
if (Test-V1NormalStopEvidence -State $state `
        -Session $remoteResponseDiagnostic `
        -Attempts @($remoteResponseDiagnostic) -AgentExitCode 0) {
    throw 'An inbound OPEN with a remote response flag was accepted.'
}

Write-Host 'V1 normal-stop evidence tests passed.'
