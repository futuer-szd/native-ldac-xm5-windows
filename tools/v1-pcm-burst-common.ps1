# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'native-ldac-baseline-common.ps1')

function Get-V1PcmBurstCandidate {
    param([Parameter(Mandatory = $true)][string]$CandidatePath)
    $root = [System.IO.Path]::GetFullPath($CandidatePath)
    $manifestPath = Join-Path $root 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "V1 PCM-burst manifest is missing: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $required = @(
        'verified_four_packet_silence_prerequisite',
        'installed_LdacNative_driver_tree_prerequisite',
        'exact_XM5_ACL_generation',
        'render_demand_authorized',
        'audible_PCM_before_Bluetooth_OPEN',
        'maximum_fixed_gain_0_01',
        'bounded_10000_ms_PCM_clock_pacing',
        'AVDTP_START_then_SUSPEND_CLOSE',
        'retry_only_OpenSignaling_Win32_71',
        'consumer_lease_release_required',
        'no_LinkState_write',
        'no_driver_install',
        'no_reboot')
    $capabilities = @($manifest.capabilities | ForEach-Object { [string]$_ })
    $expectedFiles = @(
        'v1_presence_agent.exe',
        'v1_transport_pcm_worker.exe',
        'audio_endpoint_probe.exe',
        'xm5_connection_probe.exe',
        'xm5_connection_probe.manifest.json')
    $manifestFiles = @($manifest.files)
    $manifestPaths = @($manifestFiles | ForEach-Object { [string]$_.path })
    if ([int]$manifest.manifest_version -ne 1 -or
        [int]$manifest.transport_policy_version -ne 5 -or
        $manifest.source_dirty -ne $false -or
        [string]$manifest.source_commit -notmatch '^[0-9a-fA-F]{40}$' -or
        [string]$manifest.driver_tree -notmatch '^[0-9a-fA-F]{40}$' -or
        @($required | Where-Object { $_ -notin $capabilities }).Count -ne 0 -or
        $manifestFiles.Count -ne $expectedFiles.Count -or
        @($expectedFiles | Where-Object { $_ -notin $manifestPaths }).Count -ne 0 -or
        @($manifestPaths | Select-Object -Unique).Count -ne
            $expectedFiles.Count) {
        throw 'The V1 PCM-burst candidate contract is invalid.'
    }
    foreach ($file in $manifestFiles) {
        $path = Join-Path $root ([string]$file.path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            [long]$file.length -le 0 -or
            (Get-Item -LiteralPath $path).Length -ne [long]$file.length -or
            -not (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.Equals(
                [string]$file.sha256, [StringComparison]::OrdinalIgnoreCase)) {
            throw "V1 PCM-burst file failed its hash check: $($file.path)"
        }
    }
    [pscustomobject]@{ root = $root; manifest = $manifest }
}
function Test-V1PcmBurstEvidence {
    param(
        $State,
        $Session,
        [object[]]$Attempts,
        [int]$AgentExitCode,
        [ValidateRange(1,60000)][int]$ExpectedDurationMs = 10000,
        [ValidateRange(4,32768)][int]$ExpectedMaximumPackets = 4096,
        [ValidateRange(0.000001,1.0)][double]$ExpectedMaximumGain = 0.01,
        [ValidateRange(0.0,1.0)][double]$ExpectedMaximumOutputPeak = 0.0,
        [switch]$RequireOutputPeakField,
        [switch]$RequireLimiterTelemetry,
        [switch]$AllowDynamicOutputCeiling,
        [switch]$RequireEpochReacquireTelemetry,
        [switch]$RequirePretransportRenderGapTolerance,
        [ValidateRange(1,4)][int]$MaximumAttempts = 3
    )
    $count = [int]$State.transport_open_executed
    $maximumProperty = $State.PSObject.Properties[
        'maximum_transport_open_attempts']
    $reportedMaximum = if ($null -eq $maximumProperty) {
        3
    } else {
        [int]$maximumProperty.Value
    }
    if ($count -lt 1 -or $count -gt $MaximumAttempts -or
        $reportedMaximum -ne $MaximumAttempts -or
        $Attempts.Count -ne $count) {
        return $false
    }
    $renderGapProperty = $State.PSObject.Properties[
        'pretransport_render_gap_tolerance']
    if ($RequirePretransportRenderGapTolerance -and
        ($null -eq $renderGapProperty -or
         $renderGapProperty.Value -ne $true)) {
        return $false
    }
    for ($i = 0; $i -lt $Attempts.Count; ++$i) {
        $attempt = $Attempts[$i]
        if ($attempt.pcm_prepared -ne $true -or
            $attempt.consumer_lease_acquired -ne $true -or
            $attempt.consumer_lease_released -ne $true -or
            $attempt.audible_pcm_confirmed_before_open -ne $true -or
            [int64]$attempt.pcm_frames_read -le 0) {
            return $false
        }
        if ($i -eq $Attempts.Count - 1) {
            if ([string]$attempt.disposition -ne 'succeeded' -or
                $attempt.strictly_retryable_open_failure -ne $false) {
                return $false
            }
        } elseif ([string]$attempt.disposition -ne 'backend-failure' -or
            [int]$attempt.stage -ne 1 -or
            [int]$attempt.backend_error -ne 71 -or
            [int]$attempt.open_attempts -ne 1 -or
            [int]$attempt.signaling_exchanges -ne 0 -or
            $attempt.strictly_retryable_open_failure -ne $true) {
            return $false
        }
    }
    $sampleRate = [int]$Session.sample_rate_hz
    $bits = [int]$Session.bits_per_sample
    $gain = [double]$Session.maximum_gain_scalar
    $outputPeakProperty = $Session.PSObject.Properties[
        'maximum_output_peak_ceiling']
    if ($RequireOutputPeakField -and $null -eq $outputPeakProperty) {
        return $false
    }
    $expectedOutputPeak = if ($ExpectedMaximumOutputPeak -gt 0.0) {
        $ExpectedMaximumOutputPeak
    } else {
        $ExpectedMaximumGain
    }
    $outputPeak = if ($null -eq $outputPeakProperty) {
        $expectedOutputPeak
    } else {
        [double]$outputPeakProperty.Value
    }
    $prePeak = [double]$Session.maximum_pre_gain_peak
    $postPeak = [double]$Session.maximum_post_gain_peak
    $unlimitedProperty = $Session.PSObject.Properties[
        'maximum_unlimited_post_gain_peak']
    $limitedSamplesProperty = $Session.PSObject.Properties[
        'limited_output_samples']
    if ($RequireLimiterTelemetry -and
        ($null -eq $unlimitedProperty -or
         $null -eq $limitedSamplesProperty)) {
        return $false
    }
    $unlimitedPeak = if ($null -eq $unlimitedProperty) {
        $postPeak
    } else {
        [double]$unlimitedProperty.Value
    }
    $limitedSamples = if ($null -eq $limitedSamplesProperty) {
        0
    } else {
        [int64]$limitedSamplesProperty.Value
    }
    $limiterConsistent = if ($AllowDynamicOutputCeiling) {
        $limitedSamples -ge 0 -and
            $unlimitedPeak -ge ($postPeak - 0.000001) -and
            $unlimitedPeak -le ($prePeak * $gain + 0.000001) -and
            ($limitedSamples -ne 0 -or
             [Math]::Abs($postPeak - $unlimitedPeak) -le 0.000001)
    } else {
        $limitedSamples -ge 0 -and
            $unlimitedPeak -ge ($postPeak - 0.000001) -and
            $unlimitedPeak -le ($prePeak * $gain + 0.000001) -and
            (($limitedSamples -eq 0 -and
              $unlimitedPeak -le ($expectedOutputPeak + 0.000001) -and
              [Math]::Abs($postPeak - $unlimitedPeak) -le 0.000001) -or
             ($limitedSamples -gt 0 -and
              $unlimitedPeak -gt $expectedOutputPeak -and
              [Math]::Abs($postPeak - $expectedOutputPeak) -le 0.000001))
    }
    $prepareProperty = $Session.PSObject.Properties[
        'pcm_prepare_attempts']
    $restartProperty = $Session.PSObject.Properties[
        'pcm_epoch_restarts']
    $acquireProperty = $Session.PSObject.Properties[
        'consumer_lease_acquire_count']
    $releaseProperty = $Session.PSObject.Properties[
        'consumer_lease_release_count']
    if ($RequireEpochReacquireTelemetry -and
        ($null -eq $prepareProperty -or $null -eq $restartProperty -or
         $null -eq $acquireProperty -or $null -eq $releaseProperty)) {
        return $false
    }
    $epochReacquireConsistent = $true
    if ($null -ne $prepareProperty -and $null -ne $restartProperty -and
        $null -ne $acquireProperty -and $null -ne $releaseProperty) {
        $prepareCount = [int]$prepareProperty.Value
        $restartCount = [int]$restartProperty.Value
        $acquireCount = [int]$acquireProperty.Value
        $releaseCount = [int]$releaseProperty.Value
        $epochReacquireConsistent = $prepareCount -ge 1 -and
            $restartCount -ge 0 -and
            $prepareCount -eq $acquireCount -and
            $acquireCount -eq $releaseCount -and
            $restartCount -eq ($acquireCount - 1)
    }
    return $AgentExitCode -eq 0 -and
        [string]$State.mode -eq 'transport-pcm-burst-exercise' -and
        [string]$State.state -eq 'stopped' -and
        [string]$State.physical_presence -eq 'absent' -and
        [string]$State.render_demand -eq 'idle' -and
        [int]$State.connected_events -eq 1 -and
        [int]$State.child_processes_started -eq $count -and
        [int]$State.engine_ready_events -eq $count -and
        [int]$State.transport_open_actions -eq $count -and
        [int]$State.transport_open_attempts_for_generation -eq 0 -and
        [int]$State.transport_retryable_failures -eq ($count - 1) -and
        [int]$State.transport_retries_scheduled -eq ($count - 1) -and
        [int]$State.transport_retry_budget_exhausted -eq 0 -and
        [int]$State.capabilities_discovered_events -eq 1 -and
        [int]$State.discovery_sessions_completed -eq 0 -and
        [int]$State.configuration_sessions_completed -eq 0 -and
        [int]$State.silence_sessions_completed -eq 0 -and
        [int]$State.pcm_burst_sessions_completed -eq 1 -and
        [int]$State.media_started_events -eq 1 -and
        [int]$State.media_stopped_events -eq 1 -and
        [int]$State.media_failed_events -eq 0 -and
        [int]$State.transport_stop_acknowledgements -eq $count -and
        [int]$State.engine_graceful_stops -eq $count -and
        [int]$State.engine_exit_events -eq $count -and
        [int]$State.engine_unexpected_exits -eq 0 -and
        [string]$Session.disposition -eq 'succeeded' -and
        [int]$Session.open_attempts -eq 1 -and
        [int]$Session.signaling_exchanges -ge 9 -and
        $Session.signaling_opened -eq $true -and
        [int]$Session.remote_seid -gt 0 -and
        $sampleRate -in @(44100, 48000, 88200, 96000) -and
        $bits -in @(16, 24) -and
        [int]$Session.incoming_mtu -gt 0 -and
        [int]$Session.outgoing_mtu -gt 0 -and
        $Session.set_configuration_accepted -eq $true -and
        $Session.avdtp_open_accepted -eq $true -and
        $Session.media_opened -eq $true -and
        $Session.avdtp_start_accepted -eq $true -and
        $Session.media_started_notified -eq $true -and
        $Session.completed_full_duration -eq $true -and
        $Session.ended_by_graceful_stop -eq $false -and
        [int]$Session.target_duration_ms -eq $ExpectedDurationMs -and
        [int]$Session.actual_duration_ms -ge $ExpectedDurationMs -and
        [int]$Session.actual_duration_ms -le ($ExpectedDurationMs + 50) -and
        [int64]$Session.pcm_frames_read -gt 0 -and
        [int64]$Session.pcm_frames_sent -ge
            ([int64]$sampleRate * $ExpectedDurationMs / 1000) -and
        [int]$Session.media_packets_written -gt 4 -and
        [int]$Session.media_packets_written -le $ExpectedMaximumPackets -and
        [int]$Session.pacing_waits -eq
            [int]$Session.media_packets_written -and
        [int]$Session.media_bytes_written -gt 0 -and
        [Math]::Abs($gain - $ExpectedMaximumGain) -le 0.000001 -and
        [Math]::Abs($outputPeak - $expectedOutputPeak) -le 0.000001 -and
        $prePeak -ge 0.0001 -and
        $postPeak -gt 0.0 -and
        $postPeak -le ($expectedOutputPeak + 0.000001) -and
        $postPeak -le ([Math]::Min(
                $prePeak * $gain, $expectedOutputPeak) + 0.000001) -and
        $limiterConsistent -and
        $epochReacquireConsistent -and
        $Session.pcm_prepared -eq $true -and
        $Session.consumer_lease_acquired -eq $true -and
        $Session.consumer_lease_released -eq $true -and
        $Session.audible_pcm_confirmed_before_open -eq $true -and
        $Session.avdtp_suspend_accepted -eq $true -and
        $Session.avdtp_close_accepted -eq $true -and
        $Session.remote_stream_cleanup_required -eq $false -and
        $Session.close_attempted -eq $true -and
        $Session.close_succeeded -eq $true -and
        $Session.strictly_retryable_open_failure -eq $false
}
