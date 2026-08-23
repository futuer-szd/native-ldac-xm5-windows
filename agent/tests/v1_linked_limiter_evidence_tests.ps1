# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $root 'tools\v1-linked-limiter-common.ps1')

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
    maximum_pre_gain_peak=0.93;
    maximum_unlimited_post_gain_peak=0.93;
    maximum_post_gain_peak=0.25; limited_output_samples=205000;
    limiter_algorithm='linked-stereo-block'; limiter_algorithm_version=1;
    limiter_release_ms=50.0;
    limiter_minimum_gain=0.2688; limiter_gain_reduced_frames=150000;
    limiter_gain_reduced_samples=205000; limiter_fallback_clamp_count=0;
    pcm_prepared=$true; consumer_lease_acquired=$true;
    consumer_lease_released=$true;
    audible_pcm_confirmed_before_open=$true; avdtp_suspend_accepted=$true;
    avdtp_close_accepted=$true; remote_stream_cleanup_required=$false;
    close_attempted=$true; close_succeeded=$true
}
$attempts = @($retry, $retry, $session)
if (-not (Test-V1LinkedLimiterEvidence -State $state -Session $session `
        -Attempts $attempts -AgentExitCode 0)) {
    throw 'Valid linked-limiter evidence was rejected.'
}

$wrongAlgorithm = $session | ConvertTo-Json | ConvertFrom-Json
$wrongAlgorithm.limiter_algorithm = 'independent-sample-clamp'
if (Test-V1LinkedLimiterEvidence -State $state -Session $wrongAlgorithm `
        -Attempts @($retry, $retry, $wrongAlgorithm) -AgentExitCode 0) {
    throw 'Evidence from another limiter algorithm was accepted.'
}
$missingVersion = $session | ConvertTo-Json | ConvertFrom-Json
$missingVersion.PSObject.Properties.Remove('limiter_algorithm_version')
if (Test-V1LinkedLimiterEvidence -State $state -Session $missingVersion `
        -Attempts @($retry, $retry, $missingVersion) -AgentExitCode 0) {
    throw 'Linked-limiter evidence without an algorithm version was accepted.'
}
$invalidMinimum = $session | ConvertTo-Json | ConvertFrom-Json
$invalidMinimum.limiter_minimum_gain = 1.0
if (Test-V1LinkedLimiterEvidence -State $state -Session $invalidMinimum `
        -Attempts @($retry, $retry, $invalidMinimum) -AgentExitCode 0) {
    throw 'Linked-limiter evidence without actual gain reduction was accepted.'
}
$wrongRelease = $session | ConvertTo-Json | ConvertFrom-Json
$wrongRelease.limiter_release_ms = 100.0
if (Test-V1LinkedLimiterEvidence -State $state -Session $wrongRelease `
        -Attempts @($retry, $retry, $wrongRelease) -AgentExitCode 0) {
    throw 'Linked-limiter evidence from another release profile was accepted.'
}
$badCounts = $session | ConvertTo-Json | ConvertFrom-Json
$badCounts.limiter_gain_reduced_samples =
    ([int64]$badCounts.limiter_gain_reduced_frames * 2 + 1)
if (Test-V1LinkedLimiterEvidence -State $state -Session $badCounts `
        -Attempts @($retry, $retry, $badCounts) -AgentExitCode 0) {
    throw 'Inconsistent linked frame/sample reduction counts were accepted.'
}
$fallbackClamp = $session | ConvertTo-Json | ConvertFrom-Json
$fallbackClamp.limiter_fallback_clamp_count = 1
if (Test-V1LinkedLimiterEvidence -State $state -Session $fallbackClamp `
        -Attempts @($retry, $retry, $fallbackClamp) -AgentExitCode 0) {
    throw 'Linked-limiter evidence requiring a fallback clamp was accepted.'
}

$v8Transaction = [pscustomobject]@{
    schema_version=1; status='stability-verified-user-report';
    source_commit=('a' * 40); driver_tree=('b' * 40)
}
$v8Result = [pscustomobject]@{
    schema_version=1; transport_passed=$true; stability_reported=$true;
    source_commit=$v8Transaction.source_commit;
    stability_observation=
        'user-reported-generally-clear-with-muffled-bass';
    reported_observations=@('generally-clear','muffled-bass');
    clarity_observation='generally-clear'; bass_observation='muffled-bass';
    dropouts_observation='not-reported'; speed_observation='not-reported';
    noise_observation='not-reported'; distortion_observation='not-reported';
    target_duration_ms=60000; actual_duration_ms=60000;
    maximum_gain_scalar=1.0; maximum_output_peak_ceiling=0.25;
    consumer_lease_acquire_count=1; consumer_lease_release_count=1;
    consumer_lease_released=$true; start_accepted=$true;
    suspend_accepted=$true; close_accepted=$true;
    driver_installed_or_updated=$false; rebooted=$false;
    bluetooth_toggled=$false; default_output_changed=$false;
    link_state_written=$false
}
if (-not (Test-V1LinkedLimiterPrerequisite -Transaction $v8Transaction `
        -Result $v8Result -ExpectedDriverTree ('b' * 40))) {
    throw 'The verified policy v8 user-report prerequisite was rejected.'
}
$wrongBass = $v8Result | ConvertTo-Json | ConvertFrom-Json
$wrongBass.bass_observation = 'balanced-bass'
if (Test-V1LinkedLimiterPrerequisite -Transaction $v8Transaction `
        -Result $wrongBass -ExpectedDriverTree ('b' * 40)) {
    throw 'A policy v8 prerequisite without muffled bass was accepted.'
}

Write-Host 'V1 linked-limiter evidence tests passed.'
