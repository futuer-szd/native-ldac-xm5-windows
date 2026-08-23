# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
. (Join-Path $projectRoot 'tools\v1-pcm-burst-common.ps1')

$state = [pscustomobject]@{
    mode = 'transport-pcm-burst-exercise'
    state = 'stopped'
    physical_presence = 'absent'
    render_demand = 'idle'
    connected_events = 1
    child_processes_started = 3
    engine_ready_events = 3
    transport_open_actions = 3
    transport_open_executed = 3
    transport_open_attempts_for_generation = 0
    transport_retryable_failures = 2
    transport_retries_scheduled = 2
    transport_retry_budget_exhausted = 0
    capabilities_discovered_events = 1
    discovery_sessions_completed = 0
    configuration_sessions_completed = 0
    silence_sessions_completed = 0
    pcm_burst_sessions_completed = 1
    media_started_events = 1
    media_stopped_events = 1
    media_failed_events = 0
    transport_stop_acknowledgements = 3
    engine_graceful_stops = 3
    engine_exit_events = 3
    engine_unexpected_exits = 0
}
$retryable = [pscustomobject]@{
    disposition = 'backend-failure'
    stage = 1
    backend_error = 71
    open_attempts = 1
    signaling_exchanges = 0
    strictly_retryable_open_failure = $true
    pcm_prepared = $true
    consumer_lease_acquired = $true
    consumer_lease_released = $true
    audible_pcm_confirmed_before_open = $true
    pcm_frames_read = 128
}
$session = [pscustomobject]@{
    disposition = 'succeeded'
    open_attempts = 1
    signaling_exchanges = 9
    signaling_opened = $true
    strictly_retryable_open_failure = $false
    remote_seid = 3
    sample_rate_hz = 48000
    bits_per_sample = 16
    incoming_mtu = 1000
    outgoing_mtu = 895
    set_configuration_accepted = $true
    avdtp_open_accepted = $true
    media_opened = $true
    avdtp_start_accepted = $true
    media_started_notified = $true
    completed_full_duration = $true
    ended_by_graceful_stop = $false
    target_duration_ms = 10000
    actual_duration_ms = 10002
    pcm_frames_read = 480128
    pcm_frames_sent = 480128
    media_packets_written = 938
    pacing_waits = 938
    media_bytes_written = 840000
    maximum_gain_scalar = 0.01
    maximum_pre_gain_peak = 0.5
    maximum_post_gain_peak = 0.005
    pcm_prepared = $true
    consumer_lease_acquired = $true
    consumer_lease_released = $true
    audible_pcm_confirmed_before_open = $true
    avdtp_suspend_accepted = $true
    avdtp_close_accepted = $true
    remote_stream_cleanup_required = $false
    close_attempted = $true
    close_succeeded = $true
}
$attempts = @($retryable, $retryable, $session)
if (-not (Test-V1PcmBurstEvidence -State $state -Session $session `
        -Attempts $attempts -AgentExitCode 0)) {
    throw 'Valid bounded PCM evidence was rejected.'
}

$unsafeGain = $session | ConvertTo-Json | ConvertFrom-Json
$unsafeGain.maximum_gain_scalar = 0.02
if (Test-V1PcmBurstEvidence -State $state -Session $unsafeGain `
        -Attempts @($retryable, $retryable, $unsafeGain) -AgentExitCode 0) {
    throw 'PCM evidence above the fixed gain ceiling was accepted.'
}
$silent = $session | ConvertTo-Json | ConvertFrom-Json
$silent.maximum_pre_gain_peak = 0.0
if (Test-V1PcmBurstEvidence -State $state -Session $silent `
        -Attempts @($retryable, $retryable, $silent) -AgentExitCode 0) {
    throw 'PCM evidence without audible input was accepted.'
}
$short = $session | ConvertTo-Json | ConvertFrom-Json
$short.actual_duration_ms = 9999
if (Test-V1PcmBurstEvidence -State $state -Session $short `
        -Attempts @($retryable, $retryable, $short) -AgentExitCode 0) {
    throw 'A short PCM session was accepted.'
}
$leaked = $session | ConvertTo-Json | ConvertFrom-Json
$leaked.consumer_lease_released = $false
if (Test-V1PcmBurstEvidence -State $state -Session $leaked `
        -Attempts @($retryable, $retryable, $leaked) -AgentExitCode 0) {
    throw 'A leaked PCM consumer lease was accepted.'
}
$unsafeCleanup = $session | ConvertTo-Json | ConvertFrom-Json
$unsafeCleanup.remote_stream_cleanup_required = $true
if (Test-V1PcmBurstEvidence -State $state -Session $unsafeCleanup `
        -Attempts @($retryable, $retryable, $unsafeCleanup) `
        -AgentExitCode 0) {
    throw 'A session requiring remote cleanup was accepted.'
}
$badRetry = $retryable | ConvertTo-Json | ConvertFrom-Json
$badRetry.backend_error = 1167
if (Test-V1PcmBurstEvidence -State $state -Session $session `
        -Attempts @($retryable, $badRetry, $session) -AgentExitCode 0) {
    throw 'A non-Win32-71 retry was accepted.'
}
$noStartState = $state | ConvertTo-Json | ConvertFrom-Json
$noStartState.media_started_events = 0
if (Test-V1PcmBurstEvidence -State $noStartState -Session $session `
        -Attempts $attempts -AgentExitCode 0) {
    throw 'Evidence without a reducer MediaStarted event was accepted.'
}

Write-Host 'V1 bounded low-gain PCM evidence tests passed.'
