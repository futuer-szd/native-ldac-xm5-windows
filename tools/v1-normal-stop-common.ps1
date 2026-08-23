# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'v1-fidelity-bridge-common.ps1')

$script:V1NormalStopFidelityPrerequisiteRelativePath =
    'artifacts\v1-fidelity-bridge\trial\transaction-20260726-195238-523.json'
$script:V1NormalStopPrerequisiteRelativePath =
    $script:V1NormalStopFidelityPrerequisiteRelativePath
$script:V1NormalStopPnpPrerequisiteRelativePath =
    'artifacts\v1-inbound-pnp-rundown\trial\transaction-20260731-123447-646.json'
$script:V1NormalStopApprovedDriverTree =
    '85a0b46231ae2f3212e6616346e2d6905314f0ff'
$script:V1NormalStopFidelityDriverTree =
    'b31841ff597f1c871addb750539b4f5d39d2cf7e'
$script:V1NormalStopPolicyVersion = 20
$script:V1NormalStopMaximumDurationMs = 60000
$script:V1NormalStopMinimumMediaDurationMs = 5000
$script:V1NormalStopMaximumMediaDurationMs = 55000
$script:V1NormalStopMaximumPackets = 32768
$script:V1NormalStopSamplePeakCeiling = 1.0
$script:V1NormalStopStartupSilenceMs = 20.0
$script:V1NormalStopFadeInMs = 100.0
$script:V1NormalStopTransportOpenRenderStabilityMs = 1000

function Get-V1FileSha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    $stream = [System.IO.File]::OpenRead(
        [System.IO.Path]::GetFullPath($Path))
    try {
        $algorithm = [System.Security.Cryptography.SHA256]::Create()
        try {
            $hash = $algorithm.ComputeHash($stream)
            return [System.BitConverter]::ToString($hash).Replace('-', '')
        } finally {
            $algorithm.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

function Get-V1NormalStopResumeDecision {
    param(
        [Parameter(Mandatory = $true)][bool]$GitClean,
        [Parameter(Mandatory = $true)][bool]$PrerequisiteValid,
        [Parameter(Mandatory = $true)][bool]$CandidatePresent,
        [Parameter(Mandatory = $true)][bool]$CandidateValid,
        [Parameter(Mandatory = $true)][bool]$CandidateCurrent
    )
    if (-not $GitClean) {
        return [pscustomobject]@{
            state = 'source-dirty'; action = 'clean-worktree'; ready = $false
        }
    }
    if (-not $PrerequisiteValid) {
        return [pscustomobject]@{
            state = 'prerequisite-invalid'; action = 'repair-prerequisite'
            ready = $false
        }
    }
    if (-not $CandidatePresent) {
        return [pscustomobject]@{
            state = 'candidate-missing'; action = 'rebuild-candidate'
            ready = $false
        }
    }
    if (-not $CandidateValid) {
        return [pscustomobject]@{
            state = 'candidate-invalid'; action = 'rebuild-candidate'
            ready = $false
        }
    }
    if (-not $CandidateCurrent) {
        return [pscustomobject]@{
            state = 'candidate-stale'; action = 'rebuild-candidate'
            ready = $false
        }
    }
    [pscustomobject]@{
        state = 'ready-to-resume'; action = 'run-read-only-preflight'
        ready = $true
    }
}

function Get-V1NormalStopBaselineAssessment {
    param(
        [Parameter(Mandatory = $true)]$Baseline,
        [Parameter(Mandatory = $true)][string]$ExpectedTransportInf
    )

    $devices = @($Baseline.a2dp_devices)
    $native = @($Baseline.native_audio_devices |
        Where-Object { $_.present })
    $transportPackages = @($Baseline.transport_test_packages)
    $selectedPackages = @($transportPackages | Where-Object {
        ([string]$_.published_inf).Equals(
            $ExpectedTransportInf,
            [StringComparison]::OrdinalIgnoreCase)
    })
    $alt = $Baseline.original_a2dp_user_service
    $failures = @()
    if ($devices.Count -ne 1) {
        $failures += "A2DP device count is $($devices.Count), expected 1"
    } elseif ([string]$devices[0].service -ne 'LdacNative' -or
        [int]$devices[0].problem_code -ne 0 -or
        -not ([string]$devices[0].published_inf).Equals(
            $ExpectedTransportInf,
            [StringComparison]::OrdinalIgnoreCase)) {
        $failures += "A2DP binding is $([string]$devices[0].service)/$([string]$devices[0].published_inf)/problem $([int]$devices[0].problem_code), expected LdacNative/$ExpectedTransportInf/problem 0"
    }
    if ($selectedPackages.Count -ne 1) {
        $failures += "selected transport package count is $($selectedPackages.Count), expected 1"
    }
    if ($native.Count -ne 1) {
        $failures += "present Native audio endpoint count is $($native.Count), expected 1"
    } elseif ([string]$native[0].service -ne 'NativeLdacAudio' -or
        [int]$native[0].problem_code -ne 0) {
        $failures += "Native audio binding is $([string]$native[0].service)/problem $([int]$native[0].problem_code), expected NativeLdacAudio/problem 0"
    }
    if (@($Baseline.workspace_processes).Count -ne 0) {
        $failures += 'workspace media processes are still running'
    }
    if (@($Baseline.scheduled_tasks).Count -ne 0) {
        $failures += 'the Native LDAC scheduled task is still present'
    }
    if ($null -eq $alt) {
        $failures += 'Alternative A2DP Service was not found'
    } elseif ([string]$alt.start_mode -ne 'Manual' -or
        [string]$alt.state -ne 'Stopped') {
        $failures += "Alternative A2DP Service is $([string]$alt.start_mode)/$([string]$alt.state), expected Manual/Stopped"
    }
    [pscustomobject]@{
        healthy = $failures.Count -eq 0
        failures = @($failures)
        transport_package_count = $transportPackages.Count
        selected_transport_package_count = $selectedPackages.Count
    }
}

function Test-V1NormalStopFidelityPrerequisite {
    param(
        [Parameter(Mandatory = $true)]$Transaction,
        [Parameter(Mandatory = $true)]$Result
    )
    return [int]$Transaction.schema_version -eq 1 -and
        [int]$Transaction.transport_policy_version -eq 10 -and
        [string]$Transaction.status -eq 'fidelity-verified-user-report' -and
        [string]$Transaction.source_commit -match '^[0-9a-fA-F]{40}$' -and
        [string]$Transaction.driver_tree -eq
            $script:V1NormalStopFidelityDriverTree -and
        [int]$Result.schema_version -eq 1 -and
        [int]$Result.transport_policy_version -eq 10 -and
        $Result.transport_passed -eq $true -and
        [string]$Result.source_commit -eq
            [string]$Transaction.source_commit -and
        [string]$Result.driver_tree -eq [string]$Transaction.driver_tree -and
        [string]$Result.fidelity_observation -eq
            'user-reported-overall-normal-subtle-differences-not-discernible' -and
        [string]$Result.overall_observation -eq 'normal' -and
        [string]$Result.ten_second_playback_observation -eq 'normal' -and
        [string]$Result.comparison_observation -eq
            'subtle-differences-not-discernible' -and
        [Math]::Abs([double]$Result.maximum_gain_scalar - 1.0) -le
            0.000001 -and
        [int64]$Result.volume_change_count -eq 0 -and
        $Result.volume_stable -eq $true -and
        [int64]$Result.fade_committed_sent_frames -eq
            [int64]$Result.pcm_frames_sent -and
        [int64]$Result.fade_commit_failures -eq 0 -and
        [int64]$Result.fade_sanitized_sample_count -eq 0 -and
        [int]$Result.consumer_lease_acquire_count -ge 1 -and
        [int]$Result.consumer_lease_acquire_count -eq
            [int]$Result.consumer_lease_release_count -and
        $Result.consumer_lease_released -eq $true -and
        $Result.start_accepted -eq $true -and
        $Result.suspend_accepted -eq $true -and
        $Result.close_accepted -eq $true -and
        $Result.driver_installed_or_updated -eq $false -and
        $Result.rebooted -eq $false -and
        $Result.bluetooth_toggled -eq $false -and
        $Result.default_output_changed -eq $false -and
        $Result.link_state_written -eq $false
}

function Test-V1NormalStopPnpPrerequisite {
    param(
        [Parameter(Mandatory = $true)]$Transaction,
        [Parameter(Mandatory = $true)][string]$TransactionPath,
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)][string]$ResultPath,
        [Parameter(Mandatory = $true)][string]$ExpectedDriverTree
    )

    $cycles = @($Result.cycles)
    if ([int]$Transaction.schema_version -ne 1 -or
        [int]$Transaction.transport_policy_version -ne 14 -or
        [string]$Transaction.status -ne 'pnp-rundown-verified' -or
        [string]$Transaction.phase -ne 'complete' -or
        $Transaction.rollback.attempted -ne $false -or
        [string]$Transaction.driver_tree -ne $ExpectedDriverTree -or
        [string]$Transaction.result -ne $ResultPath -or
        [int]$Result.schema_version -ne 1 -or
        [int]$Result.transport_policy_version -ne 14 -or
        $Result.passed -ne $true -or
        [string]$Result.source_commit -ne
            [string]$Transaction.source_commit -or
        [string]$Result.driver_tree -ne $ExpectedDriverTree -or
        [string]$Result.transaction -ne $TransactionPath -or
        [string]$Result.binding_inf -ne
            [string]$Transaction.selected_inf -or
        $Result.reboot_verified -ne $true -or
        [int]$Result.cycle_count -ne 2 -or $cycles.Count -ne 2 -or
        [int]$Result.code38_event_count -ne 0 -or
        [int]$Result.set_configuration_commands -ne 0 -or
        [int]$Result.avdtp_open_commands -ne 0 -or
        [int]$Result.avdtp_start_commands -ne 0 -or
        [int]$Result.media_l2cap_open_commands -ne 0 -or
        [int]$Result.media_packets -ne 0 -or
        $Result.bluetooth_toggled -ne $false -or
        $Result.pnp_restarted -ne $false) {
        return $false
    }
    foreach ($cycle in $cycles) {
        if ($cycle.passed -ne $true -or
            -not [string]::IsNullOrWhiteSpace([string]$cycle.failure) -or
            $cycle.acl_connect_observed -ne $true -or
            $cycle.acl_disconnect_observed -ne $true -or
            $cycle.public_disconnect_observed -ne $true -or
            $cycle.binding_on_connect_healthy -ne $true -or
            [string]$cycle.binding_after_disconnect.service -ne
                'LdacNative' -or
            [string]$cycle.binding_after_disconnect.published_inf -ne
                [string]$Transaction.selected_inf -or
            [int]$cycle.binding_after_disconnect.problem_code -ne 0 -or
            [int]$cycle.discover_exit_code -ne 0 -or
            [string]$cycle.open_diagnostic -notmatch
                '(?m)^Signaling channel direction: inbound\.\r?$' -or
            [string]$cycle.open_diagnostic -notmatch
                '(?m)^L2CAP OPEN state: completed, succeeded\.\r?$' -or
            [int]$cycle.code38_event_count -ne 0 -or
            [int]$cycle.set_configuration_commands -ne 0 -or
            [int]$cycle.avdtp_open_commands -ne 0 -or
            [int]$cycle.avdtp_start_commands -ne 0 -or
            [int]$cycle.media_l2cap_open_commands -ne 0 -or
            [int]$cycle.media_packets -ne 0) {
            return $false
        }
    }
    return $true
}

function Get-V1NormalStopCandidate {
    param(
        [Parameter(Mandatory = $true)][string]$CandidatePath,
        [Parameter(Mandatory = $true)][string]$ExpectedFidelityPrerequisitePath,
        [Parameter(Mandatory = $true)][string]$ExpectedPnpPrerequisitePath
    )
    $root = [System.IO.Path]::GetFullPath($CandidatePath)
    $expectedFidelityPrerequisite =
        [System.IO.Path]::GetFullPath($ExpectedFidelityPrerequisitePath)
    $expectedPnpPrerequisite =
        [System.IO.Path]::GetFullPath($ExpectedPnpPrerequisitePath)
    $manifestPath = Join-Path $root 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "V1 normal-stop manifest is missing: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw |
        ConvertFrom-Json
    $required = @(
        'verified_policy_v10_transparent_transport_prerequisite',
        'verified_policy_v14_inbound_pnp_rundown_prerequisite',
        'one_shot_inbound_listener',
        'inbound_signaling_ready_flags_0xF',
        'successful_open_diagnostics_required',
        'inbound_signaling_channel_required',
        'single_transport_attempt_required',
        'zero_transport_retry_success_contract',
        'exact_XM5_ACL_generation',
        'render_stop_required_after_media_started',
        'graceful_STOP_uses_SUSPEND_CLOSE',
        'ACL_disconnect_uses_local_cancel_only',
        'streaming_peer_close_observer',
        'remote_close_accept_local_cleanup',
        'pre_media_render_gap_accounting',
        'pre_start_pcm_discard_accounting',
        'continuous_render_epoch_before_transport_open',
        'dynamic_post_volume_tracking',
        'bounded_post_start_render_rebind',
        'public_endpoint_state_capture',
        'unity_post_volume_gain',
        'unity_full_scale_sample_boundary_only',
        'no_post_start_ceiling_ramp',
        'sent_frame_fade_in_100_ms',
        'minimum_5000_ms_before_render_stop',
        'maximum_60000_ms_hard_bound',
        'consumer_lease_release_required',
        'physical_ACL_disconnect_required',
        'no_LinkState_write',
        'no_driver_install',
        'no_reboot')
    $capabilities = @($manifest.capabilities |
        ForEach-Object { [string]$_ })
    $expectedFiles = @(
        'v1_presence_agent.exe',
        'v1_transport_normal_stop_worker.exe',
        'audio_endpoint_probe.exe',
        'endpoint_volume_probe.exe',
        'transport_probe.exe',
        'xm5_connection_probe.exe',
        'xm5_connection_probe.manifest.json')
    $manifestFiles = @($manifest.files)
    $manifestPaths = @($manifestFiles | ForEach-Object { [string]$_.path })
    if ([int]$manifest.manifest_version -ne 1 -or
        [int]$manifest.transport_policy_version -ne
            $script:V1NormalStopPolicyVersion -or
        [string]$manifest.configuration -cne 'Release' -or
        [System.IO.Path]::GetFullPath(
            [string]$manifest.fidelity_prerequisite) -cne
            $expectedFidelityPrerequisite -or
        [string]$manifest.fidelity_prerequisite_source_commit -notmatch
            '^[0-9a-fA-F]{40}$' -or
        [string]$manifest.fidelity_driver_tree -ne
            $script:V1NormalStopFidelityDriverTree -or
        [System.IO.Path]::GetFullPath(
            [string]$manifest.pnp_prerequisite) -cne
            $expectedPnpPrerequisite -or
        [string]$manifest.pnp_prerequisite_source_commit -notmatch
            '^[0-9a-fA-F]{40}$' -or
        [string]$manifest.driver_tree -ne
            $script:V1NormalStopApprovedDriverTree -or
        [string]$manifest.output_policy -ne
            'transparent-unity-full-scale-boundary' -or
        [string]$manifest.peak_measurement -ne 'digital-sample-peak' -or
        [string]$manifest.peak_unit -ne 'dBFS' -or
        [Math]::Abs([double]$manifest.sample_peak_ceiling -
            $script:V1NormalStopSamplePeakCeiling) -gt 0.000001 -or
        [Math]::Abs([double]$manifest.maximum_gain_scalar - 1.0) -gt
            0.000001 -or
        [int]$manifest.maximum_duration_ms -ne
            $script:V1NormalStopMaximumDurationMs -or
        [int]$manifest.minimum_stop_duration_ms -ne
            $script:V1NormalStopMinimumMediaDurationMs -or
        [Math]::Abs([double]$manifest.fade_in_ms -
            $script:V1NormalStopFadeInMs) -gt 0.0001 -or
        [Math]::Abs([double]$manifest.startup_silence_ms -
            $script:V1NormalStopStartupSilenceMs) -gt 0.0001 -or
        [double]$manifest.ceiling_ramp_ms -ne 0.0 -or
        [int]$manifest.transport_open_render_stability_ms -ne
            $script:V1NormalStopTransportOpenRenderStabilityMs -or
        [int]$manifest.required_transport_open_attempts -ne 1 -or
        $manifest.source_dirty -ne $false -or
        [string]$manifest.source_commit -notmatch '^[0-9a-fA-F]{40}$' -or
        [string]$manifest.driver_tree -notmatch '^[0-9a-fA-F]{40}$' -or
        @($required | Where-Object { $_ -notin $capabilities }).Count -ne 0 -or
        $manifestFiles.Count -ne $expectedFiles.Count -or
        @($expectedFiles | Where-Object { $_ -notin $manifestPaths }).Count -ne
            0 -or
        @($manifestPaths | Select-Object -Unique).Count -ne
            $expectedFiles.Count) {
        throw 'The V1 normal-stop candidate contract is invalid.'
    }
    foreach ($file in $manifestFiles) {
        $path = Join-Path $root ([string]$file.path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            [long]$file.length -le 0 -or
            (Get-Item -LiteralPath $path).Length -ne [long]$file.length -or
            -not (Get-V1FileSha256 -Path $path).Equals(
                [string]$file.sha256,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "V1 normal-stop file failed its hash check: $($file.path)"
        }
    }
    [pscustomobject]@{ root = $root; manifest = $manifest }
}

function Test-V1NormalStopRetryAttempt {
    param($Attempt, [int64]$Generation)
    foreach ($property in @(
            'open_diagnostic_query_attempts',
            'open_diagnostic_query_error',
            'open_diagnostic_query_bytes',
            'open_diagnostic_available',
            'open_diagnostic_remote_response_valid',
            'open_diagnostic_operation', 'open_diagnostic_psm',
            'open_diagnostic_remote_bluetooth_address',
            'open_diagnostic_channel_flags', 'open_diagnostic_flags',
            'open_diagnostic_response',
            'open_diagnostic_response_status',
            'open_diagnostic_remote_no_resources')) {
        if ($null -eq $Attempt -or
            $null -eq $Attempt.PSObject.Properties[$property]) {
            return $false
        }
    }
    return (
        [int64]$Attempt.session_generation -eq $Generation -and
        [string]$Attempt.disposition -eq 'backend-failure' -and
        [int]$Attempt.stage -eq 1 -and
        [int]$Attempt.backend_error -eq 71 -and
        [int]$Attempt.open_attempts -eq 1 -and
        [int]$Attempt.signaling_exchanges -eq 0 -and
        $Attempt.signaling_opened -eq $false -and
        $Attempt.strictly_retryable_open_failure -eq $true -and
        [int]$Attempt.open_diagnostic_query_attempts -ge 1 -and
        [int]$Attempt.open_diagnostic_query_error -eq 0 -and
        [int]$Attempt.open_diagnostic_query_bytes -ge 48 -and
        $Attempt.open_diagnostic_available -eq $true -and
        $Attempt.open_diagnostic_remote_response_valid -eq $true -and
        [int]$Attempt.open_diagnostic_operation -eq 1 -and
        [uint64]$Attempt.open_diagnostic_remote_bluetooth_address -ne 0 -and
        [int]$Attempt.open_diagnostic_channel_flags -eq 0x00060000 -and
        [int]$Attempt.open_diagnostic_psm -eq 0x0019 -and
        [int]$Attempt.open_diagnostic_response -eq 4 -and
        [int]$Attempt.open_diagnostic_response_status -eq 0 -and
        $Attempt.open_diagnostic_remote_no_resources -eq $true -and
        [int]$Attempt.media_packets_written -eq 0 -and
        [int]$Attempt.consumer_lease_acquire_count -eq
            [int]$Attempt.consumer_lease_release_count -and
        $Attempt.consumer_lease_released -eq $true)
}

function Test-V1NormalStopEvidence {
    param($State, $Session, [object[]]$Attempts, [int]$AgentExitCode)
    if ($null -eq $State -or $null -eq $Session -or
        [int64]$State.acl_generation -le 0) {
        return $false
    }
    $generation = [int64]$State.acl_generation
    $count = @($Attempts).Count
    if ($count -ne 1 -or
        [int]$State.transport_open_executed -ne $count) {
        return $false
    }
    for ($index = 0; $index -lt ($count - 1); ++$index) {
        if (-not (Test-V1NormalStopRetryAttempt `
                -Attempt $Attempts[$index] -Generation $generation)) {
            return $false
        }
    }
    $last = $Attempts[$count - 1]
    if ([int64]$last.session_generation -ne $generation -or
        [string]$last.disposition -ne 'cancelled' -or
        $last.strictly_retryable_open_failure -ne $false) {
        return $false
    }
    foreach ($property in @(
            'open_diagnostic_query_attempts',
            'open_diagnostic_query_error',
            'open_diagnostic_query_bytes',
            'open_diagnostic_available',
            'open_diagnostic_remote_response_valid',
            'open_diagnostic_operation',
            'open_diagnostic_remote_bluetooth_address',
            'open_diagnostic_channel_flags',
            'open_diagnostic_flags',
            'open_diagnostic_psm',
            'open_diagnostic_remote_no_resources',
            'limiter_attack_count', 'limiter_gain_reduced_frames',
            'limiter_gain_reduced_samples', 'limiter_fallback_clamp_count',
            'limiter_sanitized_sample_count', 'fade_committed_sent_frames',
            'fade_blocks_prepared', 'fade_blocks_committed',
            'fade_commit_failures', 'fade_sanitized_sample_count',
            'startup_silence_ms', 'startup_silence_frames_sent',
            'startup_silence_packets_written',
            'boundary_resume_count',
            'boundary_resume_fade_frames',
            'transport_frames_sent',
            'ceiling_ramp_start', 'ceiling_ramp_ms', 'ceiling_ramp_last')) {
        if ($null -eq $Session.PSObject.Properties[$property]) {
            return $false
        }
    }
    $duration = [int]$Session.actual_duration_ms
    $fadePrepared = [int64]$Session.fade_blocks_prepared
    $fadeCommitted = [int64]$Session.fade_blocks_committed
    $pcmSent = [int64]$Session.pcm_frames_sent
    return $AgentExitCode -eq 0 -and
        [string]$State.mode -eq 'transport-pcm-burst-exercise' -and
        [string]$State.state -eq 'stopped' -and
        [string]$State.physical_presence -eq 'absent' -and
        [string]$State.render_demand -eq 'idle' -and
        [int]$State.connected_events -eq 1 -and
        [int]$State.disconnected_events -eq 1 -and
        [int]$State.render_started_events -ge 1 -and
        [int]$State.render_stopped_events -ge 1 -and
        [int]$State.pre_media_render_stop_events -ge 0 -and
        [int]$State.render_stopped_events -eq
            ([int]$State.pre_media_render_stop_events +
             [int]$State.render_stop_deferred_events) -and
        [int]$State.render_started_events -eq
            (1 + [int]$State.pre_media_render_stop_events +
             [int]$State.render_stop_resumed_events) -and
        [int]$State.render_stop_resumed_events -ge 0 -and
        [int]$State.render_stop_timeout_events -eq 1 -and
        [int]$State.render_stop_acl_cancelled_events -eq 0 -and
        $State.render_stop_pending -eq $false -and
        [int]$State.child_processes_started -eq $count -and
        [int]$State.engine_ready_events -eq $count -and
        [int]$State.transport_open_render_stability_ms -eq
            $script:V1NormalStopTransportOpenRenderStabilityMs -and
        [int]$State.transport_open_stability_waits -eq $count -and
        [int]$State.transport_open_stability_resets -ge 0 -and
        [int]$State.transport_open_stable_authorizations -eq $count -and
        $State.transport_open_stability_pending -eq $false -and
        [int]$State.transport_retryable_failures -eq ($count - 1) -and
        [int]$State.transport_retries_scheduled -eq ($count - 1) -and
        [int]$State.transport_retry_budget_exhausted -eq 0 -and
        [int]$State.media_started_events -eq 1 -and
        [int]$State.media_stopped_events -eq 1 -and
        [int]$State.media_failed_events -eq 0 -and
        [int]$State.transport_graceful_stop_actions -eq 1 -and
        [int]$State.transport_stop_acknowledgements -eq $count -and
        [int]$State.engine_graceful_stops -eq $count -and
        [int]$State.engine_exit_events -eq $count -and
        [int]$State.engine_unexpected_exits -eq 0 -and
        [int]$State.transport_open_attempts_for_generation -eq 0 -and
        [int64]$Session.session_generation -eq $generation -and
        [string]$Session.disposition -eq 'cancelled' -and
        $Session.completed_full_duration -eq $false -and
        $Session.ended_by_graceful_stop -eq $true -and
        [int]$Session.target_duration_ms -eq
            $script:V1NormalStopMaximumDurationMs -and
        $duration -ge $script:V1NormalStopMinimumMediaDurationMs -and
        $duration -le $script:V1NormalStopMaximumMediaDurationMs -and
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
        [int]$Session.signaling_exchanges -ge 9 -and
        $Session.signaling_opened -eq $true -and
        $Session.set_configuration_accepted -eq $true -and
        $Session.avdtp_open_accepted -eq $true -and
        $Session.media_opened -eq $true -and
        $Session.avdtp_start_accepted -eq $true -and
        $Session.media_started_notified -eq $true -and
        [int]$Session.media_packets_written -gt 4 -and
        [int]$Session.media_packets_written -le
            $script:V1NormalStopMaximumPackets -and
        [int64]$Session.pcm_frames_sent -gt 0 -and
        [int64]$Session.fade_committed_sent_frames -eq
            $pcmSent -and
        $fadePrepared -ge $fadeCommitted -and
        ($fadePrepared - $fadeCommitted) -le 1 -and
        $fadeCommitted * 128 -eq
            [int64]$Session.fade_committed_sent_frames -and
        [int64]$Session.fade_commit_failures -eq 0 -and
        [int64]$Session.fade_sanitized_sample_count -eq 0 -and
        [Math]::Abs([double]$Session.startup_silence_ms -
            $script:V1NormalStopStartupSilenceMs) -le 0.0001 -and
        [int64]$Session.startup_silence_frames_sent -gt 0 -and
        [int]$Session.startup_silence_packets_written -gt 0 -and
        [int]$Session.boundary_resume_count -ge 0 -and
        [int64]$Session.boundary_resume_fade_frames -ge 0 -and
        [int64]$Session.transport_frames_sent -eq
            ([int64]$Session.startup_silence_frames_sent + $pcmSent) -and
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
        (([int64]$Session.volume_change_count -eq 0 -and
          $Session.volume_stable -eq $true) -or
         ([int64]$Session.volume_change_count -gt 0 -and
          $Session.volume_stable -eq $false)) -and
        [int]$Session.consumer_lease_acquire_count -ge 1 -and
        [int]$Session.consumer_lease_acquire_count -eq
            [int]$Session.consumer_lease_release_count -and
        $Session.consumer_lease_released -eq $true -and
        $Session.avdtp_suspend_accepted -eq $true -and
        $Session.avdtp_close_accepted -eq $true -and
        $Session.remote_stream_cleanup_required -eq $false -and
        $Session.close_attempted -eq $true -and
        $Session.close_succeeded -eq $true
}
