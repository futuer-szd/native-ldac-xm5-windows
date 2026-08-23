# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $root 'tools\v1-pcm-burst-common.ps1')

$state = [pscustomobject]@{
    mode='transport-pcm-burst-exercise'; state='stopped';
    physical_presence='absent'; render_demand='idle'; connected_events=1;
    child_processes_started=4; engine_ready_events=4;
    transport_open_actions=4; transport_open_executed=4;
    transport_open_attempts_for_generation=0; transport_retryable_failures=3;
    transport_retries_scheduled=3; transport_retry_budget_exhausted=0;
    maximum_transport_open_attempts=4;
    pretransport_render_gap_tolerance=$true;
    capabilities_discovered_events=1; discovery_sessions_completed=0;
    configuration_sessions_completed=0; silence_sessions_completed=0;
    pcm_burst_sessions_completed=1; media_started_events=1;
    media_stopped_events=1; media_failed_events=0;
    transport_stop_acknowledgements=4; engine_graceful_stops=4;
    engine_exit_events=4; engine_unexpected_exits=0
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
    actual_duration_ms=5003; pcm_frames_read=220672; pcm_frames_sent=220672;
    media_packets_written=862; pacing_waits=862; media_bytes_written=580126;
    maximum_gain_scalar=1.0; maximum_output_peak_ceiling=0.25;
    maximum_pre_gain_peak=0.04401705; maximum_post_gain_peak=0.04401705;
    pcm_prepared=$true; consumer_lease_acquired=$true;
    consumer_lease_released=$true;
    audible_pcm_confirmed_before_open=$true; avdtp_suspend_accepted=$true;
    avdtp_close_accepted=$true; remote_stream_cleanup_required=$false;
    close_attempted=$true; close_succeeded=$true
}
$attempts = @($retry, $retry, $retry, $session)
$arguments = @{
    State=$state; Session=$session; Attempts=$attempts; AgentExitCode=0;
    ExpectedDurationMs=5000; ExpectedMaximumGain=1.0;
    ExpectedMaximumOutputPeak=0.25; RequireOutputPeakField=$true;
    RequirePretransportRenderGapTolerance=$true;
    MaximumAttempts=4
}
if (-not (Test-V1PcmBurstEvidence @arguments)) {
    throw 'Valid recognizable-audio evidence was rejected.'
}
$missingLimiter = $session | ConvertTo-Json | ConvertFrom-Json
$missingLimiter.PSObject.Properties.Remove('maximum_output_peak_ceiling')
$arguments.Session = $missingLimiter
$arguments.Attempts = @($retry, $retry, $retry, $missingLimiter)
if (Test-V1PcmBurstEvidence @arguments) {
    throw 'Recognizable evidence without an explicit limiter was accepted.'
}
$clippedAboveLimit = $session | ConvertTo-Json | ConvertFrom-Json
$clippedAboveLimit.maximum_post_gain_peak = 0.26
$arguments.Session = $clippedAboveLimit
$arguments.Attempts = @($retry, $retry, $retry, $clippedAboveLimit)
if (Test-V1PcmBurstEvidence @arguments) {
    throw 'Recognizable evidence above the independent limiter was accepted.'
}
$wrongGain = $session | ConvertTo-Json | ConvertFrom-Json
$wrongGain.maximum_gain_scalar = 0.25
$arguments.Session = $wrongGain
$arguments.Attempts = @($retry, $retry, $retry, $wrongGain)
if (Test-V1PcmBurstEvidence @arguments) {
    throw 'Recognizable evidence with policy v6 gain was accepted.'
}
Write-Host 'V1 recognizable-audio evidence tests passed.'
