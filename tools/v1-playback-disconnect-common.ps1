# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'v1-normal-stop-common.ps1')

$script:V1PlaybackDisconnectPolicyVersion = 20
$script:V1PlaybackDisconnectPrerequisiteRelativePath =
    'artifacts\v1-normal-stop\trial\transaction-20260731-174959-985.json'
$script:V1PlaybackDisconnectMinimumMediaDurationMs = 5000
$script:V1PlaybackDisconnectMaximumMediaDurationMs = 55000

function Test-V1PlaybackDisconnectHasProperties {
    param($Value, [string[]]$Properties)
    if ($null -eq $Value) {
        return $false
    }
    foreach ($property in $Properties) {
        if ($null -eq $Value.PSObject.Properties[$property]) {
            return $false
        }
    }
    return $true
}

function Test-V1PlaybackDisconnectSameArchive {
    param($Session, $Archive)
    try {
        $sessionJson = $Session | ConvertTo-Json -Depth 16 -Compress
        $archiveJson = $Archive | ConvertTo-Json -Depth 16 -Compress
        return [string]$sessionJson -ceq [string]$archiveJson
    } catch {
        return $false
    }
}

function Test-V1PlaybackDisconnectPrerequisite {
    param(
        [Parameter(Mandatory = $true)]$Transaction,
        [Parameter(Mandatory = $true)][string]$TransactionPath,
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)][string]$ResultPath,
        [Parameter(Mandatory = $true)][string]$ExpectedDriverTree
    )
    return [int]$Transaction.schema_version -eq 1 -and
        [int]$Transaction.transport_policy_version -eq 16 -and
        [string]$Transaction.status -eq 'normal-stop-verified' -and
        [string]$Transaction.driver_tree -eq $ExpectedDriverTree -and
        [string]$Transaction.result -eq $ResultPath -and
        [int]$Result.schema_version -eq 1 -and
        [int]$Result.transport_policy_version -eq 16 -and
        $Result.normal_stop_passed -eq $true -and
        [string]$Result.source_commit -eq
            [string]$Transaction.source_commit -and
        [string]$Result.driver_tree -eq $ExpectedDriverTree -and
        [string]$Result.transaction -eq $TransactionPath -and
        [int]$Result.transport_open_attempts -eq 1 -and
        [int]$Result.transport_retry_count -eq 0 -and
        [string]$Result.signaling_direction -eq 'inbound' -and
        $Result.ended_by_graceful_stop -eq $true -and
        $Result.suspend_accepted -eq $true -and
        $Result.close_accepted -eq $true -and
        $Result.consumer_lease_released -eq $true -and
        $Result.physical_acl_disconnected -eq $true -and
        [string]$Result.lifecycle_outcome -eq 'graceful-stop' -and
        $Result.driver_installed_or_updated -eq $false -and
        $Result.rebooted -eq $false -and
        $Result.bluetooth_toggled -eq $false
}

function Test-V1PlaybackDisconnectEvidence {
    param($State, $Session, [object[]]$Attempts, [int]$AgentExitCode)
    $stateProperties = @(
        'mode', 'state', 'physical_presence', 'render_demand',
        'acl_generation', 'connected_events', 'disconnected_events',
        'publish_present_actions', 'publish_absent_actions',
        'fail_mute_actions', 'transport_open_actions',
        'transport_open_executed',
        'transport_open_render_stability_ms',
        'transport_open_stability_waits',
        'transport_open_stability_resets',
        'transport_open_stable_authorizations',
        'transport_open_stability_pending',
        'transport_open_attempts_for_generation',
        'maximum_transport_open_attempts',
        'pretransport_render_gap_tolerance',
        'transport_retryable_failures', 'transport_retries_scheduled',
        'transport_retry_budget_exhausted',
        'capabilities_discovered_events', 'pcm_burst_sessions_completed',
        'transport_graceful_stop_actions', 'transport_cancel_actions',
        'media_started_events', 'media_stopped_events',
        'media_failed_events', 'transport_stop_acknowledgements',
        'child_processes_started', 'endpoint_sink_enabled',
        'endpoint_presence_updates', 'endpoint_presence_failures',
        'render_observer_enabled', 'engine_ready_observer_enabled',
        'transport_worker_exercise_enabled',
        'transport_discovery_exercise_enabled',
        'transport_configuration_exercise_enabled',
        'transport_silence_exercise_enabled',
        'transport_pcm_burst_exercise_enabled',
        'playback_disconnect_wait_enabled',
        'playback_disconnect_fail_closed_release', 'render_query_count',
        'render_query_failures', 'render_started_events',
        'render_stopped_events', 'pre_media_render_stop_events',
        'render_stop_deferred_events',
        'render_stop_resumed_events', 'render_stop_timeout_events',
        'render_stop_acl_cancelled_events', 'render_stop_pending',
        'render_start_timed_out',
        'engine_start_requests',
        'engine_stop_requests', 'engine_ready_events',
        'engine_exit_events', 'engine_start_failures',
        'engine_ready_timeouts', 'engine_stop_failures',
        'engine_graceful_stops', 'engine_unexpected_exits',
        'last_engine_exit_code')
    $sessionProperties = @(
        'session_generation', 'disposition',
        'strictly_retryable_open_failure', 'completed_full_duration',
        'ended_by_graceful_stop', 'ended_by_peer_close',
        'target_duration_ms',
        'actual_duration_ms', 'open_attempts', 'signaling_exchanges',
        'peer_signaling_commands_received',
        'peer_discover_commands_accepted',
        'peer_capability_commands_accepted',
        'peer_configuration_commands_rejected',
        'peer_close_commands_accepted',
        'peer_signaling_read_timeouts',
        'last_signaling_response_size',
        'last_signaling_tx_header_available',
        'last_signaling_tx_transaction_label',
        'last_signaling_tx_message_type',
        'last_signaling_tx_signal_id',
        'last_signaling_rx_header_available',
        'last_signaling_rx_transaction_label',
        'last_signaling_rx_message_type',
        'last_signaling_rx_signal_id',
        'open_diagnostic_query_attempts',
        'open_diagnostic_query_error', 'open_diagnostic_query_bytes',
        'open_diagnostic_available',
        'open_diagnostic_remote_response_valid',
        'open_diagnostic_operation',
        'open_diagnostic_remote_bluetooth_address',
        'open_diagnostic_channel_flags', 'open_diagnostic_flags',
        'open_diagnostic_psm', 'open_diagnostic_remote_no_resources',
        'signaling_opened', 'set_configuration_accepted',
        'avdtp_open_accepted', 'media_opened', 'avdtp_start_accepted',
        'media_started_notified', 'media_packets_written',
        'pcm_frames_sent', 'pcm_frames_read',
        'pre_start_pcm_frames_discarded',
        'fade_committed_sent_frames', 'fade_blocks_prepared',
        'fade_blocks_committed', 'fade_commit_failures',
        'fade_sanitized_sample_count', 'maximum_gain_scalar',
        'maximum_output_peak_ceiling', 'ceiling_ramp_start',
        'ceiling_ramp_ms', 'ceiling_ramp_last',
        'limiter_attack_count', 'limiter_gain_reduced_frames',
        'limiter_gain_reduced_samples', 'limiter_fallback_clamp_count',
        'limiter_sanitized_sample_count', 'volume_query_count',
        'volume_change_count', 'volume_scalar_minimum',
        'volume_scalar_maximum', 'volume_scalar_last',
        'volume_db_minimum', 'volume_db_maximum', 'volume_db_last',
        'volume_stable', 'consumer_lease_acquired',
        'consumer_lease_acquire_count',
        'consumer_lease_release_count', 'consumer_lease_released',
        'avdtp_suspend_accepted',
        'avdtp_close_accepted', 'remote_stream_cleanup_required',
        'close_attempted', 'close_succeeded')
    if (-not (Test-V1PlaybackDisconnectHasProperties `
            -Value $State -Properties $stateProperties) -or
        -not (Test-V1PlaybackDisconnectHasProperties `
            -Value $Session -Properties $sessionProperties) -or
        [int64]$State.acl_generation -le 0) {
        return $false
    }
    $generation = [int64]$State.acl_generation
    $count = @($Attempts).Count
    if ($count -ne 1 -or
        [int]$State.transport_open_executed -ne $count) {
        return $false
    }
    $last = $Attempts[$count - 1]
    if (-not (Test-V1PlaybackDisconnectHasProperties `
            -Value $last -Properties $sessionProperties) -or
        -not (Test-V1PlaybackDisconnectSameArchive `
            -Session $Session -Archive $last) -or
        [int64]$last.session_generation -ne $generation -or
        [string]$last.disposition -ne 'cancelled' -or
        $last.strictly_retryable_open_failure -ne $false) {
        return $false
    }
    $duration = [int]$Session.actual_duration_ms
    $fadePrepared = [int64]$Session.fade_blocks_prepared
    $fadeCommitted = [int64]$Session.fade_blocks_committed
    $pcmRead = [int64]$Session.pcm_frames_read
    $pcmSent = [int64]$Session.pcm_frames_sent
    $preStartDiscarded =
        [int64]$Session.pre_start_pcm_frames_discarded
    return $AgentExitCode -eq 0 -and
        [string]$State.mode -eq 'transport-pcm-burst-exercise' -and
        [string]$State.state -eq 'stopped' -and
        [string]$State.physical_presence -eq 'absent' -and
        [string]$State.render_demand -eq 'idle' -and
        [int]$State.connected_events -eq 1 -and
        [int]$State.disconnected_events -eq 1 -and
        [int]$State.publish_present_actions -eq 1 -and
        [int]$State.publish_absent_actions -eq 1 -and
        [int]$State.fail_mute_actions -eq 1 -and
        [int]$State.pre_media_render_stop_events -ge 0 -and
        [int]$State.render_stopped_events -eq
            ([int]$State.pre_media_render_stop_events +
             [int]$State.render_stop_deferred_events) -and
        [int]$State.render_started_events -eq
            (1 + [int]$State.pre_media_render_stop_events +
             [int]$State.render_stop_resumed_events) -and
        [int]$State.render_stop_timeout_events -eq 0 -and
        [int]$State.render_stop_resumed_events +
            [int]$State.render_stop_acl_cancelled_events -eq
            [int]$State.render_stop_deferred_events -and
        $State.render_stop_pending -eq $false -and
        $State.render_start_timed_out -eq $false -and
        [int]$State.child_processes_started -eq $count -and
        [int]$State.transport_open_actions -eq $count -and
        [int]$State.engine_ready_events -eq $count -and
        [int]$State.transport_open_render_stability_ms -eq
            $script:V1NormalStopTransportOpenRenderStabilityMs -and
        [int]$State.transport_open_stability_waits -eq $count -and
        [int]$State.transport_open_stability_resets -ge 0 -and
        [int]$State.transport_open_stable_authorizations -eq $count -and
        $State.transport_open_stability_pending -eq $false -and
        [int]$State.transport_retryable_failures -eq 0 -and
        [int]$State.transport_retries_scheduled -eq 0 -and
        [int]$State.transport_retry_budget_exhausted -eq 0 -and
        [int]$State.capabilities_discovered_events -eq 0 -and
        [int]$State.pcm_burst_sessions_completed -eq 0 -and
        [int]$State.media_started_events -eq 1 -and
        [int]$State.media_stopped_events -eq 0 -and
        [int]$State.media_failed_events -eq 0 -and
        [int]$State.transport_graceful_stop_actions -eq 0 -and
        [int]$State.transport_cancel_actions -eq $count -and
        [int]$State.transport_stop_acknowledgements -eq $count -and
        [int]$State.engine_graceful_stops -eq $count -and
        [int]$State.engine_exit_events -eq $count -and
        [int]$State.engine_unexpected_exits -eq 0 -and
        [int]$State.maximum_transport_open_attempts -eq 4 -and
        $State.pretransport_render_gap_tolerance -eq $true -and
        $State.endpoint_sink_enabled -eq $true -and
        [int]$State.endpoint_presence_updates -ge 2 -and
        [int]$State.endpoint_presence_failures -eq 0 -and
        $State.render_observer_enabled -eq $true -and
        $State.engine_ready_observer_enabled -eq $true -and
        $State.transport_worker_exercise_enabled -eq $true -and
        $State.transport_discovery_exercise_enabled -eq $true -and
        $State.transport_configuration_exercise_enabled -eq $true -and
        $State.transport_silence_exercise_enabled -eq $false -and
        $State.transport_pcm_burst_exercise_enabled -eq $true -and
        $State.playback_disconnect_wait_enabled -eq $true -and
        $State.playback_disconnect_fail_closed_release -eq $false -and
        [int]$State.render_query_count -ge 1 -and
        [int]$State.render_query_failures -eq 0 -and
        [int]$State.engine_start_requests -eq $count -and
        [int]$State.engine_stop_requests -eq $count -and
        [int]$State.engine_start_failures -eq 0 -and
        [int]$State.engine_ready_timeouts -eq 0 -and
        [int]$State.engine_stop_failures -eq 0 -and
        [int]$State.last_engine_exit_code -eq 0 -and
        [int]$State.transport_open_attempts_for_generation -eq 0 -and
        [int64]$Session.session_generation -eq $generation -and
        [string]$Session.disposition -eq 'cancelled' -and
        $Session.completed_full_duration -eq $false -and
        $Session.ended_by_graceful_stop -eq $false -and
        [int]$Session.target_duration_ms -eq
            $script:V1NormalStopMaximumDurationMs -and
        $duration -ge $script:V1PlaybackDisconnectMinimumMediaDurationMs -and
        $duration -le $script:V1PlaybackDisconnectMaximumMediaDurationMs -and
        [int]$Session.open_attempts -eq 1 -and
        [int]$Session.open_diagnostic_query_attempts -ge 1 -and
        [int]$Session.open_diagnostic_query_error -eq 0 -and
        [int]$Session.open_diagnostic_query_bytes -ge 48 -and
        $Session.open_diagnostic_available -eq $true -and
        $Session.open_diagnostic_remote_response_valid -eq $false -and
        [int]$Session.open_diagnostic_operation -eq 1 -and
        [uint64]$Session.open_diagnostic_remote_bluetooth_address -ne 0 -and
        [int]$Session.open_diagnostic_channel_flags -eq 0x00060000 -and
        ([uint32]$Session.open_diagnostic_flags -band 0x17) -eq 0x17 -and
        ([uint32]$Session.open_diagnostic_flags -band 0x08) -eq 0 -and
        [int]$Session.open_diagnostic_psm -eq 0x0019 -and
        $Session.open_diagnostic_remote_no_resources -eq $false -and
        [int]$Session.peer_signaling_commands_received -ge 0 -and
        [int]$Session.peer_signaling_commands_received -le 4 -and
        [int]$Session.peer_discover_commands_accepted -ge 0 -and
        [int]$Session.peer_discover_commands_accepted -le 1 -and
        [int]$Session.peer_capability_commands_accepted -ge 0 -and
        [int]$Session.peer_capability_commands_accepted -le 1 -and
        [int]$Session.peer_capability_commands_accepted -le
            [int]$Session.peer_discover_commands_accepted -and
        [int]$Session.peer_configuration_commands_rejected -ge 0 -and
        [int]$Session.peer_configuration_commands_rejected -le 1 -and
        [int]$Session.peer_configuration_commands_rejected -le
            [int]$Session.peer_capability_commands_accepted -and
        [int]$Session.peer_close_commands_accepted -ge 0 -and
        [int]$Session.peer_close_commands_accepted -le 1 -and
        [int]$Session.peer_signaling_read_timeouts -ge 0 -and
        [int]$Session.peer_signaling_read_timeouts -le 2 -and
        [int]$Session.peer_signaling_commands_received -eq
            ([int]$Session.peer_discover_commands_accepted +
             [int]$Session.peer_capability_commands_accepted +
             [int]$Session.peer_configuration_commands_rejected +
             [int]$Session.peer_close_commands_accepted) -and
        [int]$Session.signaling_exchanges -eq
            (7 + [int]$Session.peer_signaling_commands_received -
             [int]$Session.peer_close_commands_accepted) -and
        $Session.signaling_opened -eq $true -and
        $Session.set_configuration_accepted -eq $true -and
        $Session.avdtp_open_accepted -eq $true -and
        $Session.media_opened -eq $true -and
        $Session.avdtp_start_accepted -eq $true -and
        $Session.media_started_notified -eq $true -and
        [int]$Session.media_packets_written -gt 4 -and
        $pcmSent -gt 0 -and
        $pcmRead -ge $pcmSent -and
        $preStartDiscarded -ge 0 -and
        $preStartDiscarded -le ($pcmRead - $pcmSent) -and
        $preStartDiscarded % 128 -eq 0 -and
        ($pcmRead - $pcmSent - $preStartDiscarded) -le 128 -and
        [int64]$Session.fade_committed_sent_frames -eq
            $pcmSent -and
        $fadePrepared -ge $fadeCommitted -and
        ($fadePrepared - $fadeCommitted) -le 1 -and
        $fadeCommitted * 128 -eq
            [int64]$Session.fade_committed_sent_frames -and
        [int64]$Session.fade_commit_failures -eq 0 -and
        [int64]$Session.fade_sanitized_sample_count -eq 0 -and
        [Math]::Abs([double]$Session.maximum_gain_scalar - 1.0) -le
            0.000001 -and
        [Math]::Abs([double]$Session.maximum_output_peak_ceiling -
            $script:V1NormalStopSamplePeakCeiling) -le 0.000001 -and
        [Math]::Abs([double]$Session.ceiling_ramp_start -
            $script:V1NormalStopSamplePeakCeiling) -le 0.000001 -and
        [double]$Session.ceiling_ramp_ms -eq 0.0 -and
        [Math]::Abs([double]$Session.ceiling_ramp_last -
            $script:V1NormalStopSamplePeakCeiling) -le 0.000001 -and
        [int64]$Session.limiter_attack_count -eq 0 -and
        [int64]$Session.limiter_gain_reduced_frames -eq 0 -and
        [int64]$Session.limiter_gain_reduced_samples -eq 0 -and
        [int64]$Session.limiter_fallback_clamp_count -eq 0 -and
        [int64]$Session.limiter_sanitized_sample_count -eq 0 -and
        [int64]$Session.volume_query_count -ge 1 -and
        [double]$Session.volume_scalar_minimum -le
            [double]$Session.volume_scalar_last -and
        [double]$Session.volume_scalar_last -le
            [double]$Session.volume_scalar_maximum -and
        [double]$Session.volume_db_minimum -le
            [double]$Session.volume_db_last -and
        [double]$Session.volume_db_last -le
            [double]$Session.volume_db_maximum -and
        [int64]$Session.volume_change_count -gt 0 -and
        $Session.volume_stable -eq $false -and
        [int]$Session.consumer_lease_acquire_count -ge 1 -and
        [int]$Session.consumer_lease_acquire_count -eq
            [int]$Session.consumer_lease_release_count -and
        $Session.consumer_lease_acquired -eq $true -and
        $Session.consumer_lease_released -eq $true -and
        $Session.avdtp_suspend_accepted -eq $false -and
        $Session.avdtp_close_accepted -eq $false -and
        (($Session.ended_by_peer_close -eq $false -and
          [int]$Session.peer_close_commands_accepted -eq 0 -and
          $Session.remote_stream_cleanup_required -eq $true) -or
         ($Session.ended_by_peer_close -eq $true -and
          [int]$Session.peer_close_commands_accepted -eq 1 -and
          $Session.remote_stream_cleanup_required -eq $false)) -and
        $Session.close_attempted -eq $true -and
        $Session.close_succeeded -eq $true
}
