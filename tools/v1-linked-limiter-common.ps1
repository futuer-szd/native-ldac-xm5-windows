# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'v1-stability-burst-common.ps1')

$script:V1LinkedLimiterAlgorithm = 'linked-stereo-block'
$script:V1LinkedLimiterAlgorithmVersion = 1
$script:V1LinkedLimiterPrerequisiteRelativePath =
    'artifacts\v1-stability-burst\trial\transaction-20260726-144127-295.json'

function Test-V1LinkedLimiterPrerequisite {
    param(
        [Parameter(Mandatory = $true)]$Transaction,
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)][string]$ExpectedDriverTree
    )
    $observations = @($Result.reported_observations |
        ForEach-Object { [string]$_ })
    return [int]$Transaction.schema_version -eq 1 -and
        [string]$Transaction.status -eq 'stability-verified-user-report' -and
        [string]$Transaction.source_commit -match '^[0-9a-fA-F]{40}$' -and
        [string]$Transaction.driver_tree -eq $ExpectedDriverTree -and
        [int]$Result.schema_version -eq 1 -and
        $Result.transport_passed -eq $true -and
        $Result.stability_reported -eq $true -and
        [string]$Result.source_commit -eq
            [string]$Transaction.source_commit -and
        [string]$Result.stability_observation -eq
            'user-reported-generally-clear-with-muffled-bass' -and
        [string]$Result.clarity_observation -eq 'generally-clear' -and
        [string]$Result.bass_observation -eq 'muffled-bass' -and
        'generally-clear' -in $observations -and
        'muffled-bass' -in $observations -and
        $observations.Count -eq 2 -and
        [string]$Result.dropouts_observation -eq 'not-reported' -and
        [string]$Result.speed_observation -eq 'not-reported' -and
        [string]$Result.noise_observation -eq 'not-reported' -and
        [string]$Result.distortion_observation -eq 'not-reported' -and
        [int]$Result.target_duration_ms -eq 60000 -and
        [int]$Result.actual_duration_ms -ge 60000 -and
        [int]$Result.actual_duration_ms -le 60050 -and
        [Math]::Abs([double]$Result.maximum_gain_scalar - 1.0) -le
            0.000001 -and
        [Math]::Abs([double]$Result.maximum_output_peak_ceiling - 0.25) -le
            0.000001 -and
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

function Get-V1LinkedLimiterCandidate {
    param([Parameter(Mandatory = $true)][string]$CandidatePath)
    $root = [System.IO.Path]::GetFullPath($CandidatePath)
    $manifestPath = Join-Path $root 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "V1 linked-limiter manifest is missing: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw |
        ConvertFrom-Json
    $required = @(
        'verified_policy_v8_generally_clear_muffled_bass_prerequisite',
        'installed_LdacNative_driver_tree_prerequisite',
        'exact_XM5_ACL_generation',
        'render_demand_authorized',
        'pretransport_render_gap_tolerance',
        'audible_PCM_before_Bluetooth_OPEN',
        'unity_post_volume_gain',
        'linked_stereo_block_limiter_v1',
        'independent_hard_output_ceiling_0_25',
        'linked_limiter_gain_reduction_telemetry',
        'bounded_120000_ms_pretransport_PCM_wait',
        'bounded_60000_ms_PCM_clock_pacing',
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
        'v1_transport_linked_limiter_worker.exe',
        'audio_endpoint_probe.exe',
        'xm5_connection_probe.exe',
        'xm5_connection_probe.manifest.json')
    $manifestFiles = @($manifest.files)
    $manifestPaths = @($manifestFiles | ForEach-Object { [string]$_.path })
    if ([int]$manifest.manifest_version -ne 1 -or
        [int]$manifest.transport_policy_version -ne 9 -or
        [string]$manifest.limiter_algorithm -ne
            $script:V1LinkedLimiterAlgorithm -or
        [int]$manifest.limiter_algorithm_version -ne
            $script:V1LinkedLimiterAlgorithmVersion -or
        [Math]::Abs([double]$manifest.limiter_release_ms - 50.0) -gt
            0.0001 -or
        [Math]::Abs([double]$manifest.maximum_gain_scalar - 1.0) -gt
            0.000001 -or
        [Math]::Abs([double]$manifest.maximum_output_peak - 0.25) -gt
            0.000001 -or
        [int]$manifest.target_duration_ms -ne 60000 -or
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
        throw 'The V1 linked-limiter candidate contract is invalid.'
    }
    foreach ($file in $manifestFiles) {
        $path = Join-Path $root ([string]$file.path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            [long]$file.length -le 0 -or
            (Get-Item -LiteralPath $path).Length -ne [long]$file.length -or
            -not (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.Equals(
                [string]$file.sha256,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "V1 linked-limiter file failed its hash check: $($file.path)"
        }
    }
    [pscustomobject]@{ root = $root; manifest = $manifest }
}

function Test-V1LinkedLimiterEvidence {
    param(
        $State,
        $Session,
        [object[]]$Attempts,
        [int]$AgentExitCode
    )
    if (-not (Test-V1PcmBurstEvidence -State $State -Session $Session `
            -Attempts $Attempts -AgentExitCode $AgentExitCode `
            -ExpectedDurationMs 60000 -ExpectedMaximumPackets 32768 `
            -ExpectedMaximumGain 1.0 -ExpectedMaximumOutputPeak 0.25 `
            -RequireOutputPeakField -RequireLimiterTelemetry `
            -RequireEpochReacquireTelemetry `
            -RequirePretransportRenderGapTolerance -MaximumAttempts 4)) {
        return $false
    }
    foreach ($property in @(
            'limiter_algorithm',
            'limiter_algorithm_version',
            'limiter_release_ms',
            'limiter_minimum_gain',
            'limiter_gain_reduced_frames',
            'limiter_gain_reduced_samples',
            'limiter_fallback_clamp_count')) {
        if ($null -eq $Session.PSObject.Properties[$property]) {
            return $false
        }
    }
    $minimumGain = [double]$Session.limiter_minimum_gain
    $reducedFrames = [int64]$Session.limiter_gain_reduced_frames
    $reducedSamples = [int64]$Session.limiter_gain_reduced_samples
    $fallbackClamps = [int64]$Session.limiter_fallback_clamp_count
    return [string]$Session.limiter_algorithm -eq
            $script:V1LinkedLimiterAlgorithm -and
        [int]$Session.limiter_algorithm_version -eq
            $script:V1LinkedLimiterAlgorithmVersion -and
        [Math]::Abs([double]$Session.limiter_release_ms - 50.0) -le
            0.0001 -and
        $minimumGain -gt 0.0 -and $minimumGain -lt 1.0 -and
        $reducedFrames -gt 0 -and
        $reducedFrames -le [int64]$Session.pcm_frames_sent -and
        $reducedSamples -ge $reducedFrames -and
        $reducedSamples -le ($reducedFrames * 2) -and
        $reducedSamples -le ([int64]$Session.pcm_frames_sent * 2) -and
        $fallbackClamps -eq 0
}

function Test-V1LinkedLimiterCompletionEvidence {
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
    $transportEvidence = Test-V1LinkedLimiterEvidence `
        -State $State -Session $Session -Attempts $Attempts -AgentExitCode 0
    return [int]$Transaction.schema_version -eq 1 -and
        [int]$Transaction.transport_policy_version -eq 9 -and
        [string]$Transaction.status -eq
            'transport-verified-awaiting-linked-limiter-report' -and
        [int]$Result.schema_version -eq 1 -and
        [int]$Result.transport_policy_version -eq 9 -and
        $Result.transport_passed -eq $true -and
        [string]$Result.quality_comparison_observation -eq
            'user-report-required' -and
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
        [string]$Result.limiter_algorithm -eq
            $script:V1LinkedLimiterAlgorithm -and
        [string]$Result.limiter_algorithm -eq
            [string]$Session.limiter_algorithm -and
        [int]$Result.limiter_algorithm_version -eq
            $script:V1LinkedLimiterAlgorithmVersion -and
        [int]$Result.limiter_algorithm_version -eq
            [int]$Session.limiter_algorithm_version -and
        [Math]::Abs([double]$Result.limiter_release_ms - 50.0) -le
            0.0001 -and
        [Math]::Abs([double]$Result.limiter_minimum_gain -
            [double]$Session.limiter_minimum_gain) -le 0.000001 -and
        [int64]$Result.limiter_gain_reduced_frames -eq
            [int64]$Session.limiter_gain_reduced_frames -and
        [int64]$Result.limiter_gain_reduced_samples -eq
            [int64]$Session.limiter_gain_reduced_samples -and
        [int64]$Result.limiter_fallback_clamp_count -eq 0 -and
        [int64]$Session.limiter_fallback_clamp_count -eq 0 -and
        [int]$Result.target_duration_ms -eq 60000 -and
        [int]$Result.actual_duration_ms -eq [int]$Session.actual_duration_ms -and
        [int]$Result.consumer_lease_acquire_count -ge 1 -and
        [int]$Result.consumer_lease_acquire_count -eq
            [int]$Result.consumer_lease_release_count -and
        [int]$Result.consumer_lease_acquire_count -eq
            [int]$Session.consumer_lease_acquire_count -and
        [int]$Result.consumer_lease_release_count -eq
            [int]$Session.consumer_lease_release_count -and
        $Result.consumer_lease_released -eq $true -and
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
