# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'v1-pcm-burst-common.ps1')

function Get-V1StabilityBurstCandidate {
    param([Parameter(Mandatory = $true)][string]$CandidatePath)
    $root = [System.IO.Path]::GetFullPath($CandidatePath)
    $manifestPath = Join-Path $root 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "V1 stability-burst manifest is missing: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw |
        ConvertFrom-Json
    $required = @(
        'verified_policy_v7_recognizable_audio_prerequisite',
        'installed_LdacNative_driver_tree_prerequisite',
        'exact_XM5_ACL_generation',
        'render_demand_authorized',
        'pretransport_render_gap_tolerance',
        'audible_PCM_before_Bluetooth_OPEN',
        'unity_post_volume_gain',
        'independent_hard_limiter_0_25',
        'limiter_engagement_telemetry',
        'bounded_120000_ms_pretransport_PCM_wait',
        'bounded_60000_ms_PCM_clock_pacing',
        'AVDTP_START_then_SUSPEND_CLOSE',
        'retry_only_OpenSignaling_Win32_71',
        'maximum_four_zero_exchange_open_attempts',
        'consumer_lease_release_required',
        'no_LinkState_write',
        'no_driver_install',
        'no_reboot')
    $capabilities = @($manifest.capabilities |
        ForEach-Object { [string]$_ })
    $expectedFiles = @(
        'v1_presence_agent.exe',
        'v1_transport_stability_worker.exe',
        'audio_endpoint_probe.exe',
        'xm5_connection_probe.exe',
        'xm5_connection_probe.manifest.json')
    $manifestFiles = @($manifest.files)
    $manifestPaths = @($manifestFiles |
        ForEach-Object { [string]$_.path })
    if ([int]$manifest.manifest_version -ne 1 -or
        [int]$manifest.transport_policy_version -ne 8 -or
        $manifest.source_dirty -ne $false -or
        [string]$manifest.source_commit -notmatch '^[0-9a-fA-F]{40}$' -or
        [string]$manifest.driver_tree -notmatch '^[0-9a-fA-F]{40}$' -or
        @($required | Where-Object { $_ -notin $capabilities }).Count -ne 0 -or
        $manifestFiles.Count -ne $expectedFiles.Count -or
        @($expectedFiles | Where-Object {
                $_ -notin $manifestPaths }).Count -ne 0 -or
        @($manifestPaths | Select-Object -Unique).Count -ne
            $expectedFiles.Count) {
        throw 'The V1 stability-burst candidate contract is invalid.'
    }
    foreach ($file in $manifestFiles) {
        $path = Join-Path $root ([string]$file.path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            [long]$file.length -le 0 -or
            (Get-Item -LiteralPath $path).Length -ne [long]$file.length -or
            -not (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.Equals(
                [string]$file.sha256,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "V1 stability-burst file failed its hash check: $($file.path)"
        }
    }
    [pscustomobject]@{ root = $root; manifest = $manifest }
}

function Test-V1StabilityCompletionEvidence {
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
    $transportEvidence = Test-V1PcmBurstEvidence `
        -State $State -Session $Session -Attempts $Attempts `
        -AgentExitCode 0 -ExpectedDurationMs 60000 `
        -ExpectedMaximumPackets 32768 -ExpectedMaximumGain 1.0 `
        -ExpectedMaximumOutputPeak 0.25 -RequireOutputPeakField `
        -RequireLimiterTelemetry -RequireEpochReacquireTelemetry `
        -RequirePretransportRenderGapTolerance -MaximumAttempts 4
    return [int]$Transaction.schema_version -eq 1 -and
        [string]$Transaction.status -eq
            'transport-verified-awaiting-user-stability-report' -and
        [int]$Result.schema_version -eq 1 -and
        $Result.transport_passed -eq $true -and
        [string]$Result.stability_observation -eq 'user-report-required' -and
        (& $samePath ([string]$Transaction.result) $resultFullPath) -and
        (& $samePath ([string]$Result.transaction) $transactionFullPath) -and
        (& $samePath ([string]$Transaction.state) `
            ([string]$State.__evidence_path)) -and
        (& $samePath ([string]$Transaction.session) `
            ([string]$Session.__evidence_path)) -and
        [string]$Transaction.source_commit -match '^[0-9a-fA-F]{40}$' -and
        [string]$Transaction.source_commit -eq
            [string]$Manifest.source_commit -and
        [string]$Result.source_commit -eq
            [string]$Transaction.source_commit -and
        [string]$Transaction.driver_tree -match '^[0-9a-fA-F]{40}$' -and
        [string]$Transaction.driver_tree -eq [string]$Manifest.driver_tree -and
        [int]$Result.target_duration_ms -eq 60000 -and
        [int]$Session.target_duration_ms -eq 60000 -and
        [int]$Result.actual_duration_ms -eq [int]$Session.actual_duration_ms -and
        [int]$Result.actual_duration_ms -ge 60000 -and
        [int]$Result.actual_duration_ms -le 60050 -and
        [int]$Result.consumer_lease_acquire_count -ge 1 -and
        [int]$Result.consumer_lease_acquire_count -eq
            [int]$Result.consumer_lease_release_count -and
        [int]$Result.consumer_lease_acquire_count -eq
            [int]$Session.consumer_lease_acquire_count -and
        [int]$Result.consumer_lease_release_count -eq
            [int]$Session.consumer_lease_release_count -and
        $Result.consumer_lease_released -eq $true -and
        $Session.consumer_lease_released -eq $true -and
        $Result.start_accepted -eq $true -and
        $Result.suspend_accepted -eq $true -and
        $Result.close_accepted -eq $true -and
        $Session.avdtp_start_accepted -eq $true -and
        $Session.avdtp_suspend_accepted -eq $true -and
        $Session.avdtp_close_accepted -eq $true -and
        $Result.driver_installed_or_updated -eq $false -and
        $Result.rebooted -eq $false -and
        $Result.bluetooth_toggled -eq $false -and
        $Result.default_output_changed -eq $false -and
        $Result.link_state_written -eq $false -and
        $transportEvidence
}
