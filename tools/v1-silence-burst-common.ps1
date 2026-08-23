# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'native-ldac-baseline-common.ps1')

function Get-V1SilenceBurstCandidate {
    param([Parameter(Mandatory = $true)][string]$CandidatePath)
    $root = [System.IO.Path]::GetFullPath($CandidatePath)
    $manifestPath = Join-Path $root 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "V1 silence-burst manifest is missing: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $required = @(
        'installed_LdacNative_driver_tree_prerequisite',
        'exact_XM5_ACL_generation',
        'render_demand_authorized',
        'maximum_four_digital_zero_packets',
        'AVDTP_START_then_SUSPEND_CLOSE',
        'retry_only_OpenSignaling_Win32_71',
        'no_real_PCM',
        'no_driver_install',
        'no_reboot')
    $capabilities = @($manifest.capabilities | ForEach-Object { [string]$_ })
    $expectedFiles = @(
        'v1_presence_agent.exe',
        'v1_transport_silence_worker.exe',
        'audio_endpoint_probe.exe',
        'xm5_connection_probe.exe',
        'xm5_connection_probe.manifest.json')
    $manifestFiles = @($manifest.files)
    $manifestPaths = @($manifestFiles | ForEach-Object { [string]$_.path })
    if ([int]$manifest.manifest_version -ne 1 -or
        [int]$manifest.transport_policy_version -ne 4 -or
        $manifest.source_dirty -ne $false -or
        [string]$manifest.source_commit -notmatch '^[0-9a-fA-F]{40}$' -or
        [string]$manifest.driver_tree -notmatch '^[0-9a-fA-F]{40}$' -or
        @($required | Where-Object { $_ -notin $capabilities }).Count -ne 0 -or
        $manifestFiles.Count -ne $expectedFiles.Count -or
        @($expectedFiles | Where-Object { $_ -notin $manifestPaths }).Count -ne 0 -or
        @($manifestPaths | Select-Object -Unique).Count -ne
            $expectedFiles.Count) {
        throw 'The V1 silence-burst candidate contract is invalid.'
    }
    foreach ($file in $manifestFiles) {
        $path = Join-Path $root ([string]$file.path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            [long]$file.length -le 0 -or
            (Get-Item -LiteralPath $path).Length -ne [long]$file.length -or
            -not (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.Equals(
                [string]$file.sha256, [StringComparison]::OrdinalIgnoreCase)) {
            throw "V1 silence-burst file failed its hash check: $($file.path)"
        }
    }
    [pscustomobject]@{ root = $root; manifest = $manifest }
}

function Test-V1SilenceBurstEvidence {
    param($State, $Session, [object[]]$Attempts, [int]$AgentExitCode)
    $count = [int]$State.transport_open_executed
    if ($count -lt 1 -or $count -gt 3 -or $Attempts.Count -ne $count) {
        return $false
    }
    for ($i = 0; $i -lt $Attempts.Count; ++$i) {
        $attempt = $Attempts[$i]
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
    return $AgentExitCode -eq 0 -and
        [string]$State.mode -eq 'transport-silence-exercise' -and
        [string]$State.state -eq 'stopped' -and
        [string]$State.physical_presence -eq 'absent' -and
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
        [int]$State.silence_sessions_completed -eq 1 -and
        [int]$State.media_started_events -eq 0 -and
        [int]$State.media_failed_events -eq 0 -and
        [int]$State.transport_stop_acknowledgements -eq $count -and
        [int]$State.engine_graceful_stops -eq $count -and
        [int]$State.engine_exit_events -eq $count -and
        [int]$State.engine_unexpected_exits -eq 0 -and
        [string]$Session.disposition -eq 'succeeded' -and
        [int]$Session.open_attempts -eq 1 -and
        [int]$Session.signaling_exchanges -ge 7 -and
        $Session.signaling_opened -eq $true -and
        [int]$Session.remote_seid -gt 0 -and
        [int]$Session.incoming_mtu -gt 0 -and
        [int]$Session.outgoing_mtu -gt 0 -and
        $Session.set_configuration_accepted -eq $true -and
        $Session.avdtp_open_accepted -eq $true -and
        $Session.media_opened -eq $true -and
        $Session.avdtp_start_accepted -eq $true -and
        [int]$Session.media_packets_written -eq 4 -and
        [int]$Session.media_bytes_written -gt 0 -and
        $Session.avdtp_suspend_accepted -eq $true -and
        $Session.avdtp_close_accepted -eq $true -and
        $Session.remote_stream_cleanup_required -eq $false -and
        $Session.close_attempted -eq $true -and
        $Session.close_succeeded -eq $true -and
        $Session.strictly_retryable_open_failure -eq $false
}
