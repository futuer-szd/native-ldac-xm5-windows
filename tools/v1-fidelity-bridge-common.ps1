# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'v1-linked-limiter-common.ps1')

$script:V1FidelityBridgePrerequisiteRelativePath =
    'artifacts\v1-linked-limiter\trial\transaction-20260726-154646-723.json'
$script:V1FidelitySamplePeakCeiling = 0.89125094
$script:V1FidelitySamplePeakDbfs = -1.0
$script:V1FidelityFadeInMs = 100.0
$script:V1FidelityCeilingRampStart = 0.25
$script:V1FidelityCeilingRampMs = 2000.0

function Test-V1FidelityBridgePrerequisite {
    param(
        [Parameter(Mandatory = $true)]$Transaction,
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)][string]$ExpectedDriverTree
    )
    return [int]$Transaction.schema_version -eq 1 -and
        [int]$Transaction.transport_policy_version -eq 9 -and
        [string]$Transaction.status -eq
            'transport-verified-quality-not-assessed' -and
        [string]$Transaction.source_commit -match '^[0-9a-fA-F]{40}$' -and
        [string]$Transaction.driver_tree -eq $ExpectedDriverTree -and
        [int]$Result.schema_version -eq 1 -and
        [int]$Result.transport_policy_version -eq 9 -and
        $Result.transport_passed -eq $true -and
        [string]$Result.source_commit -eq
            [string]$Transaction.source_commit -and
        [string]$Result.driver_tree -eq [string]$Transaction.driver_tree -and
        [string]$Result.quality_comparison_observation -eq
            'not-assessed-by-user' -and
        $Result.quality_assessed_by_user -eq $false -and
        $Result.careful_listening_reported -eq $false -and
        [string]$Result.bass_observation -eq 'not-assessed' -and
        [string]$Result.clarity_observation -eq 'not-assessed' -and
        [string]$Result.pumping_observation -eq 'not-assessed' -and
        [string]$Result.noise_observation -eq 'not-assessed' -and
        [string]$Result.speed_observation -eq 'not-assessed' -and
        [string]$Result.distortion_observation -eq 'not-assessed' -and
        [string]$Result.limiter_algorithm -eq 'linked-stereo-block' -and
        [int]$Result.limiter_algorithm_version -eq 1 -and
        [int64]$Result.limiter_fallback_clamp_count -eq 0 -and
        [int]$Result.target_duration_ms -eq 60000 -and
        [int]$Result.actual_duration_ms -ge 60000 -and
        [int]$Result.actual_duration_ms -le 60050 -and
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

function Get-V1FidelityBridgeCandidate {
    param([Parameter(Mandatory = $true)][string]$CandidatePath)
    $root = [System.IO.Path]::GetFullPath($CandidatePath)
    $manifestPath = Join-Path $root 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "V1 fidelity-bridge manifest is missing: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw |
        ConvertFrom-Json
    $required = @(
        'verified_policy_v9_transport_quality_not_assessed_prerequisite',
        'installed_LdacNative_driver_tree_prerequisite',
        'exact_XM5_ACL_generation',
        'render_demand_authorized',
        'dynamic_volume_mute_format_epoch_lock',
        'unity_post_volume_gain',
        'digital_sample_peak_ceiling_minus_1_dbfs',
        'not_true_peak_and_not_dbtp',
        'sent_frame_fade_in_100_ms',
        'sent_frame_ceiling_ramp_2000_ms',
        'bounded_10000_ms_PCM_clock_pacing',
        'AVDTP_START_then_SUSPEND_CLOSE',
        'retry_only_OpenSignaling_Win32_71_zero_exchange',
        'maximum_four_zero_exchange_open_attempts',
        'consumer_lease_release_required',
        'no_LinkState_write',
        'no_driver_install',
        'no_reboot')
    $capabilities = @($manifest.capabilities |
        ForEach-Object { [string]$_ })
    $expectedFiles = @(
        'v1_presence_agent.exe',
        'v1_transport_fidelity_bridge_worker.exe',
        'audio_endpoint_probe.exe',
        'xm5_connection_probe.exe',
        'xm5_connection_probe.manifest.json')
    $manifestFiles = @($manifest.files)
    $manifestPaths = @($manifestFiles | ForEach-Object { [string]$_.path })
    if ([int]$manifest.manifest_version -ne 1 -or
        [int]$manifest.transport_policy_version -ne 10 -or
        [string]$manifest.limiter_algorithm -ne
            'linked-stereo-sample-peak' -or
        [int]$manifest.limiter_algorithm_version -ne 1 -or
        [string]$manifest.fade_algorithm -ne 'sent-frame-linear-fade' -or
        [int]$manifest.fade_algorithm_version -ne 1 -or
        [int]$manifest.output_chain_version -ne 1 -or
        [string]$manifest.peak_measurement -ne 'digital-sample-peak' -or
        [string]$manifest.peak_unit -ne 'dBFS' -or
        [Math]::Abs([double]$manifest.sample_peak_dbfs -
            $script:V1FidelitySamplePeakDbfs) -gt 0.000001 -or
        [Math]::Abs([double]$manifest.sample_peak_ceiling -
            $script:V1FidelitySamplePeakCeiling) -gt 0.000001 -or
        [Math]::Abs([double]$manifest.maximum_gain_scalar - 1.0) -gt
            0.000001 -or
        [int]$manifest.target_duration_ms -ne 10000 -or
        [Math]::Abs([double]$manifest.fade_in_ms -
            $script:V1FidelityFadeInMs) -gt 0.0001 -or
        [Math]::Abs([double]$manifest.ceiling_ramp_start -
            $script:V1FidelityCeilingRampStart) -gt 0.000001 -or
        [Math]::Abs([double]$manifest.ceiling_ramp_ms -
            $script:V1FidelityCeilingRampMs) -gt 0.0001 -or
        [int]$manifest.maximum_transport_open_attempts -ne 4 -or
        $manifest.source_dirty -ne $false -or
        [string]$manifest.source_commit -notmatch '^[0-9a-fA-F]{40}$' -or
        [string]$manifest.driver_tree -notmatch '^[0-9a-fA-F]{40}$' -or
        @($required | Where-Object { $_ -notin $capabilities }).Count -ne 0 -or
        $manifestFiles.Count -ne $expectedFiles.Count -or
        @($expectedFiles | Where-Object { $_ -notin $manifestPaths }).Count -ne
            0 -or
        @($manifestPaths | Select-Object -Unique).Count -ne
            $expectedFiles.Count) {
        throw 'The V1 fidelity-bridge candidate contract is invalid.'
    }
    foreach ($file in $manifestFiles) {
        $path = Join-Path $root ([string]$file.path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            [long]$file.length -le 0 -or
            (Get-Item -LiteralPath $path).Length -ne [long]$file.length -or
            -not (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.Equals(
                [string]$file.sha256,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "V1 fidelity-bridge file failed its hash check: $($file.path)"
        }
    }
    [pscustomobject]@{ root = $root; manifest = $manifest }
}

function Test-V1FidelityBridgeEvidence {
    param(
        $State,
        $Session,
        [object[]]$Attempts,
        [int]$AgentExitCode
    )
    if ($null -eq $State -or $null -eq $Session) {
        return $false
    }
    if ([int64]$State.acl_generation -le 0) {
        return $false
    }
    foreach ($attempt in $Attempts) {
        $generationProperty = $attempt.PSObject.Properties[
            'session_generation']
        if ($null -eq $generationProperty -or
            [int64]$generationProperty.Value -ne
                [int64]$State.acl_generation) {
            return $false
        }
    }
    if (-not (Test-V1PcmBurstEvidence -State $State -Session $Session `
            -Attempts $Attempts -AgentExitCode $AgentExitCode `
            -ExpectedDurationMs 10000 -ExpectedMaximumPackets 8192 `
            -ExpectedMaximumGain 1.0 `
            -ExpectedMaximumOutputPeak $script:V1FidelitySamplePeakCeiling `
            -RequireOutputPeakField -RequireLimiterTelemetry `
            -AllowDynamicOutputCeiling `
            -RequireEpochReacquireTelemetry `
            -RequirePretransportRenderGapTolerance -MaximumAttempts 4)) {
        return $false
    }
    foreach ($property in @(
            'session_generation', 'volume_query_count',
            'volume_change_count', 'volume_scalar_minimum',
            'volume_scalar_maximum', 'volume_scalar_last',
            'volume_db_minimum', 'volume_db_maximum', 'volume_db_last',
            'volume_stable', 'fade_algorithm', 'fade_algorithm_version',
            'fade_in_ms', 'fade_duration_frames',
            'fade_committed_sent_frames', 'fade_frames_below_unity',
            'fade_blocks_prepared', 'fade_blocks_committed',
            'fade_commit_failures', 'fade_sanitized_sample_count',
            'fade_minimum_gain', 'fade_last_gain', 'fade_session_started',
            'ceiling_ramp_start', 'ceiling_ramp_ms', 'ceiling_ramp_last',
            'output_chain_version', 'limiter_algorithm',
            'limiter_fallback_clamp_count')) {
        if ($null -eq $Session.PSObject.Properties[$property]) {
            return $false
        }
    }
    $sampleRate = [int]$Session.sample_rate_hz
    $fadeFrames = [int64][Math]::Ceiling(
        $sampleRate * $script:V1FidelityFadeInMs / 1000.0)
    $expectedMinimumGain = 1.0 / [double]$fadeFrames
    return [int64]$Session.session_generation -gt 0 -and
        [int64]$Session.session_generation -eq [int64]$State.acl_generation -and
        [int64]$Session.volume_query_count -gt 0 -and
        [int64]$Session.volume_change_count -eq 0 -and
        $Session.volume_stable -eq $true -and
        $Session.volume_control_available -eq $true -and
        $Session.volume_muted -eq $false -and
        [Math]::Abs([double]$Session.volume_scalar_minimum -
            [double]$Session.volume_scalar_maximum) -le 0.000001 -and
        [Math]::Abs([double]$Session.volume_scalar_last -
            [double]$Session.volume_scalar_minimum) -le 0.000001 -and
        [Math]::Abs([double]$Session.volume_db_minimum -
            [double]$Session.volume_db_maximum) -le 0.001 -and
        [Math]::Abs([double]$Session.volume_db_last -
            [double]$Session.volume_db_minimum) -le 0.001 -and
        [string]$Session.fade_algorithm -eq 'sent-frame-linear-fade' -and
        [int]$Session.fade_algorithm_version -eq 1 -and
        $Session.fade_session_started -eq $true -and
        [Math]::Abs([double]$Session.fade_in_ms -
            $script:V1FidelityFadeInMs) -le 0.0001 -and
        [int64]$Session.fade_duration_frames -eq $fadeFrames -and
        [int64]$Session.fade_committed_sent_frames -eq
            [int64]$Session.pcm_frames_sent -and
        [int64]$Session.fade_frames_below_unity -eq ($fadeFrames - 1) -and
        [int64]$Session.fade_blocks_prepared -gt 0 -and
        [int64]$Session.fade_blocks_prepared -eq
            [int64]$Session.fade_blocks_committed -and
        [int64]$Session.fade_commit_failures -eq 0 -and
        [int64]$Session.fade_sanitized_sample_count -eq 0 -and
        [Math]::Abs([double]$Session.fade_minimum_gain -
            $expectedMinimumGain) -le 0.000001 -and
        [Math]::Abs([double]$Session.fade_last_gain - 1.0) -le 0.000001 -and
        [Math]::Abs([double]$Session.ceiling_ramp_start -
            $script:V1FidelityCeilingRampStart) -le 0.000001 -and
        [Math]::Abs([double]$Session.ceiling_ramp_ms -
            $script:V1FidelityCeilingRampMs) -le 0.0001 -and
        [Math]::Abs([double]$Session.ceiling_ramp_last -
            $script:V1FidelitySamplePeakCeiling) -le 0.000001 -and
        [int]$Session.output_chain_version -eq 1 -and
        [string]$Session.limiter_algorithm -eq
            'linked-stereo-sample-peak' -and
        [int]$Session.limiter_algorithm_version -eq 1 -and
        [int64]$Session.limiter_fallback_clamp_count -eq 0 -and
        [Math]::Abs([double]$Session.maximum_output_peak_ceiling -
            $script:V1FidelitySamplePeakCeiling) -le 0.000001 -and
        [double]$Session.maximum_post_gain_peak -le
            ($script:V1FidelitySamplePeakCeiling + 0.000001)
}

function Test-V1FidelityBridgeCompletionEvidence {
    param(
        [Parameter(Mandatory = $true)]$Transaction,
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)]$State,
        [Parameter(Mandatory = $true)]$Session,
        [Parameter(Mandatory = $true)][object[]]$Attempts,
        [Parameter(Mandatory = $true)][string]$TransactionPath,
        [Parameter(Mandatory = $true)][string]$ResultPath
    )
    $transactionFullPath = [System.IO.Path]::GetFullPath($TransactionPath)
    $resultFullPath = [System.IO.Path]::GetFullPath($ResultPath)
    $samePath = {
        param([string]$Left, [string]$Right)
        if ([string]::IsNullOrWhiteSpace($Left) -or
            [string]::IsNullOrWhiteSpace($Right)) {
            return $false
        }
        [System.IO.Path]::GetFullPath($Left).Equals(
            [System.IO.Path]::GetFullPath($Right),
            [StringComparison]::OrdinalIgnoreCase)
    }
    $expectedPeak = 0.03015155
    $transportEvidence = Test-V1FidelityBridgeEvidence `
        -State $State -Session $Session -Attempts $Attempts -AgentExitCode 0
    return [int]$Transaction.schema_version -eq 1 -and
        [int]$Transaction.transport_policy_version -eq 10 -and
        [string]$Transaction.status -eq
            'transport-verified-awaiting-fidelity-report' -and
        [int]$Result.schema_version -eq 1 -and
        [int]$Result.transport_policy_version -eq 10 -and
        $Result.transport_passed -eq $true -and
        [string]$Result.fidelity_observation -eq 'user-report-required' -and
        (& $samePath ([string]$Transaction.result) $resultFullPath) -and
        (& $samePath ([string]$Result.transaction) $transactionFullPath) -and
        (& $samePath ([string]$Transaction.state) `
            ([string]$State.__evidence_path)) -and
        (& $samePath ([string]$Transaction.session) `
            ([string]$Session.__evidence_path)) -and
        (& $samePath ([string]$Transaction.prerequisite) `
            ([string]$Result.prerequisite)) -and
        (& $samePath ([string]$Transaction.prerequisite) `
            ([string]$Manifest.prerequisite)) -and
        [string]$Transaction.source_commit -match '^[0-9a-fA-F]{40}$' -and
        [string]$Transaction.source_commit -eq
            [string]$Manifest.source_commit -and
        [string]$Result.source_commit -eq
            [string]$Transaction.source_commit -and
        [string]$Transaction.driver_tree -match '^[0-9a-fA-F]{40}$' -and
        [string]$Transaction.driver_tree -eq [string]$Manifest.driver_tree -and
        [string]$Result.driver_tree -eq [string]$Transaction.driver_tree -and
        [int64]$Result.session_generation -eq [int64]$State.acl_generation -and
        [int64]$Result.session_generation -eq
            [int64]$Session.session_generation -and
        [int64]$Result.acl_generation -eq [int64]$State.acl_generation -and
        [int64]$Result.stream_epoch -eq [int64]$Session.stream_epoch -and
        [int64]$Result.volume_query_count -eq
            [int64]$Session.volume_query_count -and
        [int64]$Result.volume_change_count -eq 0 -and
        $Result.volume_stable -eq $true -and
        [int64]$Result.fade_committed_sent_frames -eq
            [int64]$Session.fade_committed_sent_frames -and
        [int64]$Result.fade_blocks_prepared -eq
            [int64]$Result.fade_blocks_committed -and
        [int64]$Result.fade_commit_failures -eq 0 -and
        [int64]$Result.fade_sanitized_sample_count -eq 0 -and
        [int]$Result.consumer_lease_acquire_count -ge 1 -and
        [int]$Result.consumer_lease_acquire_count -eq
            [int]$Result.consumer_lease_release_count -and
        $Result.consumer_lease_released -eq $true -and
        [string]$Session.limiter_algorithm -eq
            'linked-stereo-sample-peak' -and
        [int64]$Session.limiter_attack_count -eq 0 -and
        [int64]$Session.limiter_gain_reduced_frames -eq 0 -and
        [int64]$Session.limiter_gain_reduced_samples -eq 0 -and
        [int64]$Session.limiter_fallback_clamp_count -eq 0 -and
        [int64]$Session.limiter_sanitized_sample_count -eq 0 -and
        [Math]::Abs([double]$Result.maximum_pre_gain_peak -
            $expectedPeak) -le 0.000000005 -and
        [Math]::Abs([double]$Result.maximum_unlimited_post_gain_peak -
            $expectedPeak) -le 0.000000005 -and
        [Math]::Abs([double]$Result.maximum_post_gain_peak -
            $expectedPeak) -le 0.000000005 -and
        [Math]::Abs([double]$Session.maximum_pre_gain_peak -
            $expectedPeak) -le 0.000000005 -and
        [Math]::Abs([double]$Session.maximum_unlimited_post_gain_peak -
            $expectedPeak) -le 0.000000005 -and
        [Math]::Abs([double]$Session.maximum_post_gain_peak -
            $expectedPeak) -le 0.000000005 -and
        [int]$Result.target_duration_ms -eq 10000 -and
        [int]$Result.actual_duration_ms -eq [int]$Session.actual_duration_ms -and
        $Result.start_accepted -eq $true -and
        $Result.suspend_accepted -eq $true -and
        $Result.close_accepted -eq $true -and
        $Result.driver_installed_or_updated -eq $false -and
        $Result.rebooted -eq $false -and
        $Result.bluetooth_toggled -eq $false -and
        $Result.default_output_changed -eq $false -and
        $Result.link_state_written -eq $false -and
        $transportEvidence
}
