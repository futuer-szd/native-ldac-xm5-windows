# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $root 'tools\v1-fidelity-bridge-common.ps1')

$state = [pscustomobject]@{
    mode='transport-pcm-burst-exercise'; state='stopped';
    physical_presence='absent'; render_demand='idle'; acl_generation=7;
    connected_events=1; child_processes_started=3; engine_ready_events=3;
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
    open_attempts=1; signaling_exchanges=0; session_generation=7;
    strictly_retryable_open_failure=$true; pcm_prepared=$true;
    consumer_lease_acquired=$true; consumer_lease_released=$true;
    audible_pcm_confirmed_before_open=$true; pcm_frames_read=256
}
$session = [pscustomobject]@{
    disposition='succeeded'; open_attempts=1; signaling_exchanges=9;
    session_generation=7; signaling_opened=$true;
    strictly_retryable_open_failure=$false;
    remote_seid=3; sample_rate_hz=44100; bits_per_sample=16;
    stream_epoch=31; incoming_mtu=1000; outgoing_mtu=895;
    set_configuration_accepted=$true; avdtp_open_accepted=$true;
    media_opened=$true; avdtp_start_accepted=$true;
    media_started_notified=$true; completed_full_duration=$true;
    ended_by_graceful_stop=$false; target_duration_ms=10000;
    actual_duration_ms=10002; pcm_frames_read=441088;
    pcm_frames_sent=441088; media_packets_written=1723;
    pcm_prepare_attempts=1; pcm_epoch_restarts=0;
    consumer_lease_acquire_count=1; consumer_lease_release_count=1;
    pacing_waits=1723; media_bytes_written=1160000;
    maximum_gain_scalar=1.0; maximum_output_peak_ceiling=0.89125094;
    maximum_pre_gain_peak=0.8; maximum_unlimited_post_gain_peak=0.8;
    maximum_post_gain_peak=0.7; limited_output_samples=1000;
    limiter_algorithm='linked-stereo-sample-peak';
    limiter_algorithm_version=1; limiter_fallback_clamp_count=0;
    volume_query_count=3446; volume_change_count=0;
    volume_scalar_minimum=0.5; volume_scalar_maximum=0.5;
    volume_scalar_last=0.5; volume_db_minimum=-6.0;
    volume_db_maximum=-6.0; volume_db_last=-6.0;
    volume_stable=$true; volume_control_available=$true;
    volume_muted=$false; fade_algorithm='sent-frame-linear-fade';
    fade_algorithm_version=1; fade_in_ms=100.0;
    fade_duration_frames=4410; fade_committed_sent_frames=441088;
    fade_frames_below_unity=4409; fade_blocks_prepared=3446;
    fade_blocks_committed=3446; fade_commit_failures=0;
    fade_sanitized_sample_count=0; fade_minimum_gain=(1.0/4410.0);
    fade_last_gain=1.0; fade_session_started=$true;
    ceiling_ramp_start=0.25; ceiling_ramp_ms=2000.0;
    ceiling_ramp_last=0.89125094; output_chain_version=1;
    pcm_prepared=$true; consumer_lease_acquired=$true;
    consumer_lease_released=$true;
    audible_pcm_confirmed_before_open=$true;
    avdtp_suspend_accepted=$true; avdtp_close_accepted=$true;
    remote_stream_cleanup_required=$false; close_attempted=$true;
    close_succeeded=$true
}
$attempts = @($retry, $retry, $session)
if (-not (Test-V1FidelityBridgeEvidence -State $state -Session $session `
        -Attempts $attempts -AgentExitCode 0)) {
    throw 'Valid fidelity-bridge evidence was rejected.'
}

$wrongGeneration = $session | ConvertTo-Json | ConvertFrom-Json
$wrongGeneration.session_generation = 8
if (Test-V1FidelityBridgeEvidence -State $state -Session $wrongGeneration `
        -Attempts @($retry, $retry, $wrongGeneration) -AgentExitCode 0) {
    throw 'A session from another ACL generation was accepted.'
}
$changedVolume = $session | ConvertTo-Json | ConvertFrom-Json
$changedVolume.volume_change_count = 1
$changedVolume.volume_stable = $false
if (Test-V1FidelityBridgeEvidence -State $state -Session $changedVolume `
        -Attempts @($retry, $retry, $changedVolume) -AgentExitCode 0) {
    throw 'A dynamic PCM volume/format/epoch change was accepted.'
}
$badCommit = $session | ConvertTo-Json | ConvertFrom-Json
$badCommit.fade_blocks_committed = 3445
if (Test-V1FidelityBridgeEvidence -State $state -Session $badCommit `
        -Attempts @($retry, $retry, $badCommit) -AgentExitCode 0) {
    throw 'Unbalanced fade prepare/commit telemetry was accepted.'
}
$readClockedFade = $session | ConvertTo-Json | ConvertFrom-Json
$readClockedFade.fade_committed_sent_frames =
    ([int64]$readClockedFade.pcm_frames_sent + 128)
if (Test-V1FidelityBridgeEvidence -State $state -Session $readClockedFade `
        -Attempts @($retry, $retry, $readClockedFade) -AgentExitCode 0) {
    throw 'A fade advanced beyond successfully sent frames was accepted.'
}
$shortRamp = $session | ConvertTo-Json | ConvertFrom-Json
$shortRamp.ceiling_ramp_last = 0.8
if (Test-V1FidelityBridgeEvidence -State $state -Session $shortRamp `
        -Attempts @($retry, $retry, $shortRamp) -AgentExitCode 0) {
    throw 'A ceiling ramp that never reached the target was accepted.'
}
$fallbackClamp = $session | ConvertTo-Json | ConvertFrom-Json
$fallbackClamp.limiter_fallback_clamp_count = 1
if (Test-V1FidelityBridgeEvidence -State $state -Session $fallbackClamp `
        -Attempts @($retry, $retry, $fallbackClamp) -AgentExitCode 0) {
    throw 'A fidelity bridge requiring fallback clamps was accepted.'
}

$v9Transaction = [pscustomobject]@{
    schema_version=1; transport_policy_version=9;
    status='transport-verified-quality-not-assessed';
    source_commit=('a' * 40); driver_tree=('b' * 40)
}
$v9Result = [pscustomobject]@{
    schema_version=1; transport_policy_version=9; transport_passed=$true;
    source_commit=$v9Transaction.source_commit;
    driver_tree=$v9Transaction.driver_tree;
    quality_comparison_observation='not-assessed-by-user';
    quality_assessed_by_user=$false; careful_listening_reported=$false;
    bass_observation='not-assessed'; clarity_observation='not-assessed';
    pumping_observation='not-assessed'; noise_observation='not-assessed';
    speed_observation='not-assessed'; distortion_observation='not-assessed';
    limiter_algorithm='linked-stereo-block'; limiter_algorithm_version=1;
    limiter_fallback_clamp_count=0; target_duration_ms=60000;
    actual_duration_ms=60000; consumer_lease_acquire_count=1;
    consumer_lease_release_count=1; consumer_lease_released=$true;
    start_accepted=$true; suspend_accepted=$true; close_accepted=$true;
    driver_installed_or_updated=$false; rebooted=$false;
    bluetooth_toggled=$false; default_output_changed=$false;
    link_state_written=$false
}
if (-not (Test-V1FidelityBridgePrerequisite -Transaction $v9Transaction `
        -Result $v9Result -ExpectedDriverTree ('b' * 40))) {
    throw 'The completed policy v9 prerequisite was rejected.'
}
$inventedQuality = $v9Result | ConvertTo-Json | ConvertFrom-Json
$inventedQuality.quality_assessed_by_user = $true
if (Test-V1FidelityBridgePrerequisite -Transaction $v9Transaction `
        -Result $inventedQuality -ExpectedDriverTree ('b' * 40)) {
    throw 'A policy v9 prerequisite with invented quality evidence was accepted.'
}

Write-Host 'V1 fidelity-bridge evidence tests passed.'
