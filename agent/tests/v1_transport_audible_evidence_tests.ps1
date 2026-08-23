# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $root 'tools\v1-pcm-burst-common.ps1')

$state = [pscustomobject]@{
    mode='transport-pcm-burst-exercise'; state='stopped';
    physical_presence='absent'; render_demand='idle'; connected_events=1;
    child_processes_started=2; engine_ready_events=2;
    transport_open_actions=2; transport_open_executed=2;
    transport_open_attempts_for_generation=0; transport_retryable_failures=1;
    transport_retries_scheduled=1; transport_retry_budget_exhausted=0;
    maximum_transport_open_attempts=4;
    capabilities_discovered_events=1; discovery_sessions_completed=0;
    configuration_sessions_completed=0; silence_sessions_completed=0;
    pcm_burst_sessions_completed=1; media_started_events=1;
    media_stopped_events=1; media_failed_events=0;
    transport_stop_acknowledgements=2; engine_graceful_stops=2;
    engine_exit_events=2; engine_unexpected_exits=0
}
$retry = [pscustomobject]@{
    disposition='backend-failure'; stage=1; backend_error=71;
    open_attempts=1; signaling_exchanges=0;
    strictly_retryable_open_failure=$true; pcm_prepared=$true;
    consumer_lease_acquired=$true; consumer_lease_released=$true;
    audible_pcm_confirmed_before_open=$true; pcm_frames_read=256
}
$session = [pscustomobject]@{
    disposition='succeeded'; open_attempts=1; signaling_exchanges=9;
    signaling_opened=$true; strictly_retryable_open_failure=$false;
    remote_seid=3; sample_rate_hz=44100; bits_per_sample=16;
    incoming_mtu=1000; outgoing_mtu=895;
    set_configuration_accepted=$true; avdtp_open_accepted=$true;
    media_opened=$true; avdtp_start_accepted=$true;
    media_started_notified=$true; completed_full_duration=$true;
    ended_by_graceful_stop=$false; target_duration_ms=5000;
    actual_duration_ms=5001; pcm_frames_read=220672; pcm_frames_sent=220672;
    media_packets_written=862; pacing_waits=862; media_bytes_written=580000;
    maximum_gain_scalar=0.25; maximum_pre_gain_peak=0.08;
    maximum_post_gain_peak=0.02; pcm_prepared=$true;
    consumer_lease_acquired=$true; consumer_lease_released=$true;
    audible_pcm_confirmed_before_open=$true; avdtp_suspend_accepted=$true;
    avdtp_close_accepted=$true; remote_stream_cleanup_required=$false;
    close_attempted=$true; close_succeeded=$true
}
$attempts = @($retry, $session)
if (-not (Test-V1PcmBurstEvidence -State $state -Session $session `
        -Attempts $attempts -AgentExitCode 0 -ExpectedDurationMs 5000 `
        -ExpectedMaximumGain 0.25 -MaximumAttempts 4)) {
    throw 'Valid cautious-audibility evidence was rejected.'
}
$unsafe = $session | ConvertTo-Json | ConvertFrom-Json
$unsafe.maximum_gain_scalar = 0.26
if (Test-V1PcmBurstEvidence -State $state -Session $unsafe `
        -Attempts @($retry, $unsafe) -AgentExitCode 0 `
        -ExpectedDurationMs 5000 -ExpectedMaximumGain 0.25 `
        -MaximumAttempts 4) {
    throw 'Evidence above the fixed audible gain profile was accepted.'
}
$wrongDuration = $session | ConvertTo-Json | ConvertFrom-Json
$wrongDuration.target_duration_ms = 10000
if (Test-V1PcmBurstEvidence -State $state -Session $wrongDuration `
        -Attempts @($retry, $wrongDuration) -AgentExitCode 0 `
        -ExpectedDurationMs 5000 -ExpectedMaximumGain 0.25 `
        -MaximumAttempts 4) {
    throw 'Evidence with the wrong duration was accepted.'
}
Write-Host 'V1 five-second audibility evidence tests passed.'
