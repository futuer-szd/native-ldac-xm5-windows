# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $root 'tools\v1-pcm-burst-common.ps1')

$state = [pscustomobject]@{
    mode='transport-pcm-burst-exercise'; state='stopped';
    physical_presence='absent'; render_demand='idle'; connected_events=1;
    child_processes_started=3; engine_ready_events=3;
    transport_open_actions=3; transport_open_executed=3;
    transport_open_attempts_for_generation=0; transport_retryable_failures=2;
    transport_retries_scheduled=2; transport_retry_budget_exhausted=0;
    maximum_transport_open_attempts=4;
    pretransport_render_gap_tolerance=$true;
    capabilities_discovered_events=1; discovery_sessions_completed=0;
    configuration_sessions_completed=0; silence_sessions_completed=0;
    pcm_burst_sessions_completed=1; media_started_events=1;
    media_stopped_events=1; media_failed_events=0;
    transport_stop_acknowledgements=3; engine_graceful_stops=3;
    engine_exit_events=3; engine_unexpected_exits=0
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
    ended_by_graceful_stop=$false; target_duration_ms=60000;
    actual_duration_ms=60003; pcm_frames_read=2646144;
    pcm_frames_sent=2646144; media_packets_written=10337;
    pcm_prepare_attempts=2; pcm_epoch_restarts=1;
    consumer_lease_acquire_count=2; consumer_lease_release_count=2;
    pacing_waits=10337; media_bytes_written=6961000;
    maximum_gain_scalar=1.0; maximum_output_peak_ceiling=0.25;
    maximum_pre_gain_peak=0.22;
    maximum_unlimited_post_gain_peak=0.22;
    maximum_post_gain_peak=0.22; limited_output_samples=0;
    pcm_prepared=$true; consumer_lease_acquired=$true;
    consumer_lease_released=$true;
    audible_pcm_confirmed_before_open=$true; avdtp_suspend_accepted=$true;
    avdtp_close_accepted=$true; remote_stream_cleanup_required=$false;
    close_attempted=$true; close_succeeded=$true
}
$attempts = @($retry, $retry, $session)
$arguments = @{
    State=$state; Session=$session; Attempts=$attempts; AgentExitCode=0;
    ExpectedDurationMs=60000; ExpectedMaximumPackets=32768;
    ExpectedMaximumGain=1.0; ExpectedMaximumOutputPeak=0.25;
    RequireOutputPeakField=$true; RequireLimiterTelemetry=$true;
    RequireEpochReacquireTelemetry=$true;
    RequirePretransportRenderGapTolerance=$true; MaximumAttempts=4
}
if (-not (Test-V1PcmBurstEvidence @arguments)) {
    throw 'Valid sixty-second stability evidence was rejected.'
}
$limited = $session | ConvertTo-Json | ConvertFrom-Json
$limited.maximum_pre_gain_peak = 0.4
$limited.maximum_unlimited_post_gain_peak = 0.4
$limited.maximum_post_gain_peak = 0.25
$limited.limited_output_samples = 512
$arguments.Session = $limited
$arguments.Attempts = @($retry, $retry, $limited)
if (-not (Test-V1PcmBurstEvidence @arguments)) {
    throw 'Valid limiter-engagement evidence was rejected.'
}
$badLimiter = $limited | ConvertTo-Json | ConvertFrom-Json
$badLimiter.maximum_post_gain_peak = 0.24
$arguments.Session = $badLimiter
$arguments.Attempts = @($retry, $retry, $badLimiter)
if (Test-V1PcmBurstEvidence @arguments) {
    throw 'Inconsistent limiter telemetry was accepted.'
}
$missingTelemetry = $session | ConvertTo-Json | ConvertFrom-Json
$missingTelemetry.PSObject.Properties.Remove('limited_output_samples')
$arguments.Session = $missingTelemetry
$arguments.Attempts = @($retry, $retry, $missingTelemetry)
if (Test-V1PcmBurstEvidence @arguments) {
    throw 'Stability evidence without limiter telemetry was accepted.'
}
$tooManyPackets = $session | ConvertTo-Json | ConvertFrom-Json
$tooManyPackets.media_packets_written = 32769
$tooManyPackets.pacing_waits = 32769
$arguments.Session = $tooManyPackets
$arguments.Attempts = @($retry, $retry, $tooManyPackets)
if (Test-V1PcmBurstEvidence @arguments) {
    throw 'Stability evidence above the packet bound was accepted.'
}
$leakedEpochLease = $session | ConvertTo-Json | ConvertFrom-Json
$leakedEpochLease.consumer_lease_release_count = 1
$arguments.Session = $leakedEpochLease
$arguments.Attempts = @($retry, $retry, $leakedEpochLease)
if (Test-V1PcmBurstEvidence @arguments) {
    throw 'Stability evidence with an epoch lease leak was accepted.'
}
Write-Host 'V1 sixty-second stability evidence tests passed.'
