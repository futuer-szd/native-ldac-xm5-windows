# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'native-ldac-baseline-common.ps1')

function Get-V1RebootDiscoveryCandidate {
    param([Parameter(Mandatory = $true)][string]$CandidatePath)

    $root = [System.IO.Path]::GetFullPath($CandidatePath)
    $manifestPath = Join-Path $root 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "The V1 reboot discovery manifest is missing: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw |
        ConvertFrom-Json
    $required = @(
        'persistent_LdacNative_function_driver_architecture',
        'cross_boot_driver_activation',
        'Bluetooth_radio_ready_precondition',
        'exact_XM5_ACL_generation',
        'job_object_contained_discovery_worker',
        'maximum_three_signaling_open_attempts_per_ACL_generation',
        'DISCOVER_and_capabilities_before_configuration',
        'distinct_capabilities_discovered_event',
        'retry_only_OpenSignaling_Win32_71',
        'retry_backoff_15s_30s',
        'cancel_retry_on_ACL_or_RenderStop',
        'SET_CONFIGURATION_then_AVDTP_OPEN',
        'open_media_L2CAP_then_immediate_AVDTP_CLOSE',
        'no_AVDTP_START',
        'no_media_payload',
        'no_media_LinkState_write',
        'restore_original_A2DP_on_failure',
        'no_Bluetooth_toggle'
    )
    $capabilities = @($manifest.capabilities | ForEach-Object {
        [string]$_
    })
    if ([int]$manifest.manifest_version -ne 1 -or
        [int]$manifest.transport_policy_version -ne 3 -or
        $manifest.source_dirty -ne $false -or
        [string]$manifest.source_commit -notmatch '^[0-9a-fA-F]{40}$' -or
        [string]$manifest.driver_tree -notmatch '^[0-9a-fA-F]{40}$' -or
        [string]$manifest.service_name -ne 'LdacNative' -or
        [string]$manifest.driver_abi -ne '0.5' -or
        @($required | Where-Object { $_ -notin $capabilities }).Count -ne
            0) {
        throw 'The V1 reboot discovery candidate contract is invalid.'
    }
    foreach ($file in @($manifest.files)) {
        $path = Join-Path $root ([string]$file.path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-Item -LiteralPath $path).Length -ne [long]$file.length -or
            -not (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.Equals(
                [string]$file.sha256,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "V1 reboot discovery file failed its hash check: $($file.path)"
        }
    }
    return [pscustomobject]@{
        root = $root
        manifest = $manifest
    }
}

function Test-V1RebootDiscoveryEvidence {
    param(
        [Parameter(Mandatory = $true)]$State,
        [Parameter(Mandatory = $true)]$Session,
        [Parameter(Mandatory = $true)][object[]]$Attempts,
        [Parameter(Mandatory = $true)][int]$AgentExitCode
    )

    $completedAttempts = [int]$State.transport_open_executed
    if ($completedAttempts -lt 1 -or $completedAttempts -gt 3 -or
        $Attempts.Count -ne $completedAttempts) {
        return $false
    }
    for ($index = 0; $index -lt $Attempts.Count; ++$index) {
        $attempt = $Attempts[$index]
        $isFinal = $index -eq ($Attempts.Count - 1)
        if ($isFinal) {
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
        [string]$State.mode -eq 'transport-discovery-exercise' -and
        [string]$State.state -eq 'stopped' -and
        [string]$State.physical_presence -eq 'absent' -and
        [int]$State.connected_events -eq 1 -and
        [int]$State.disconnected_events -eq 1 -and
        [int]$State.child_processes_started -eq $completedAttempts -and
        [int]$State.engine_ready_events -eq $completedAttempts -and
        [int]$State.transport_open_actions -eq $completedAttempts -and
        [int]$State.transport_open_attempts_for_generation -eq 0 -and
        [int]$State.transport_retryable_failures -eq
            ($completedAttempts - 1) -and
        [int]$State.transport_retries_scheduled -eq
            [int]$State.transport_retryable_failures -and
        [int]$State.transport_retry_budget_exhausted -eq 0 -and
        [int]$State.capabilities_discovered_events -eq 1 -and
        [int]$State.discovery_sessions_completed -eq 1 -and
        [int]$State.media_started_events -eq 0 -and
        [int]$State.media_failed_events -eq 0 -and
        [int]$State.transport_stop_acknowledgements -eq
            $completedAttempts -and
        [int]$State.engine_graceful_stops -eq $completedAttempts -and
        [int]$State.engine_exit_events -eq $completedAttempts -and
        [int]$State.engine_unexpected_exits -eq 0 -and
        [string]$Session.disposition -eq 'succeeded' -and
        [int]$Session.open_attempts -eq 1 -and
        [int]$Session.signaling_exchanges -ge 2 -and
        $Session.signaling_opened -eq $true -and
        $Session.close_attempted -eq $true -and
        $Session.close_succeeded -eq $true -and
        $Session.strictly_retryable_open_failure -eq $false
}

function Test-V1CapabilityPrerequisiteTransaction {
    param(
        [Parameter(Mandatory = $true)]$Transaction,
        [Parameter(Mandatory = $true)][string]$ExpectedDriverTree,
        [Parameter(Mandatory = $true)][string]$ProjectRoot
    )

    $discoveryProperty = $Transaction.PSObject.Properties['discovery']
    $postRebootProperty = $Transaction.PSObject.Properties['post_reboot']
    if ($null -eq $discoveryProperty -or
        $null -eq $discoveryProperty.Value -or
        $null -eq $postRebootProperty -or
        $null -eq $postRebootProperty.Value) {
        return $false
    }
    $discovery = $discoveryProperty.Value
    $postReboot = $postRebootProperty.Value
    $attemptResultsProperty =
        $discovery.PSObject.Properties['attempt_results']
    $agentExitProperty =
        $discovery.PSObject.Properties['agent_exit_code']
    $stateProperty = $postReboot.PSObject.Properties['state']
    $sessionProperty = $postReboot.PSObject.Properties['session']
    $radioProperty = $postReboot.PSObject.Properties['radio_state']
    if ($null -eq $attemptResultsProperty -or
        $null -eq $agentExitProperty -or
        $null -eq $stateProperty -or
        $null -eq $sessionProperty -or
        $null -eq $radioProperty -or
        [string]$radioProperty.Value -ne 'ready') {
        return $false
    }

    $driverTreeProperty =
        $Transaction.PSObject.Properties['driver_tree']
    $driverTree = if ($null -ne $driverTreeProperty -and
        -not [string]::IsNullOrWhiteSpace(
            [string]$driverTreeProperty.Value)) {
        [string]$driverTreeProperty.Value
    } else {
        $sourceProperty =
            $Transaction.PSObject.Properties['source_commit']
        if ($null -eq $sourceProperty -or
            [string]::IsNullOrWhiteSpace(
                [string]$sourceProperty.Value)) {
            return $false
        }
        $resolved = @(& git.exe -C $ProjectRoot rev-parse `
            (([string]$sourceProperty.Value) + ':driver') 2>$null)
        if ($LASTEXITCODE -ne 0 -or $resolved.Count -ne 1) {
            return $false
        }
        ([string]$resolved[0]).Trim()
    }
    if ($driverTree -ne $ExpectedDriverTree) {
        return $false
    }

    $statePath = [string]$stateProperty.Value
    $sessionPath = [string]$sessionProperty.Value
    if (-not (Test-Path -LiteralPath $statePath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $sessionPath -PathType Leaf)) {
        return $false
    }
    $attemptPaths = @($attemptResultsProperty.Value)
    if ($attemptPaths.Count -eq 0 -or
        @($attemptPaths | Where-Object {
            -not (Test-Path -LiteralPath ([string]$_) -PathType Leaf)
        }).Count -ne 0) {
        return $false
    }
    try {
        $state = Get-Content -LiteralPath $statePath -Raw |
            ConvertFrom-Json
        $session = Get-Content -LiteralPath $sessionPath -Raw |
            ConvertFrom-Json
        $attempts = @($attemptPaths | ForEach-Object {
            Get-Content -LiteralPath ([string]$_) -Raw |
                ConvertFrom-Json
        })
        return Test-V1RebootDiscoveryEvidence `
            -State $state `
            -Session $session `
            -Attempts $attempts `
            -AgentExitCode ([int]$agentExitProperty.Value)
    } catch {
        return $false
    }
}

function Test-V1RebootConfigurationCoreEvidence {
    param(
        [Parameter(Mandatory = $true)]$State,
        [Parameter(Mandatory = $true)]$Session,
        [Parameter(Mandatory = $true)][object[]]$Attempts,
        [Parameter(Mandatory = $true)][int]$AgentExitCode
    )

    $completedAttempts = [int]$State.transport_open_executed
    if ($completedAttempts -lt 1 -or $completedAttempts -gt 3 -or
        $Attempts.Count -ne $completedAttempts) {
        return $false
    }
    for ($index = 0; $index -lt $Attempts.Count; ++$index) {
        $attempt = $Attempts[$index]
        $isFinal = $index -eq ($Attempts.Count - 1)
        if ($isFinal) {
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
        [string]$State.mode -eq 'transport-configuration-exercise' -and
        [string]$State.state -eq 'stopped' -and
        [string]$State.physical_presence -eq 'absent' -and
        [int]$State.connected_events -eq 1 -and
        [int]$State.child_processes_started -eq $completedAttempts -and
        [int]$State.engine_ready_events -eq $completedAttempts -and
        [int]$State.transport_open_actions -eq $completedAttempts -and
        [int]$State.transport_open_attempts_for_generation -eq 0 -and
        [int]$State.transport_retryable_failures -eq
            ($completedAttempts - 1) -and
        [int]$State.transport_retries_scheduled -eq
            [int]$State.transport_retryable_failures -and
        [int]$State.transport_retry_budget_exhausted -eq 0 -and
        [int]$State.capabilities_discovered_events -eq 1 -and
        [int]$State.discovery_sessions_completed -eq 0 -and
        [int]$State.configuration_sessions_completed -eq 1 -and
        [int]$State.media_started_events -eq 0 -and
        [int]$State.media_failed_events -eq 0 -and
        [int]$State.transport_stop_acknowledgements -eq
            $completedAttempts -and
        [int]$State.engine_graceful_stops -eq $completedAttempts -and
        [int]$State.engine_exit_events -eq $completedAttempts -and
        [int]$State.engine_unexpected_exits -eq 0 -and
        [string]$Session.disposition -eq 'succeeded' -and
        [int]$Session.open_attempts -eq 1 -and
        [int]$Session.signaling_exchanges -ge 5 -and
        $Session.signaling_opened -eq $true -and
        $Session.set_configuration_accepted -eq $true -and
        $Session.avdtp_open_accepted -eq $true -and
        $Session.media_opened -eq $true -and
        [int]$Session.outgoing_mtu -gt 0 -and
        $Session.avdtp_close_accepted -eq $true -and
        $Session.close_attempted -eq $true -and
        $Session.close_succeeded -eq $true -and
        [int]$Session.media_start_commands -eq 0 -and
        [int]$Session.media_packets_written -eq 0 -and
        $Session.strictly_retryable_open_failure -eq $false
}

function Test-V1RebootConfigurationEvidence {
    param(
        [Parameter(Mandatory = $true)]$State,
        [Parameter(Mandatory = $true)]$Session,
        [Parameter(Mandatory = $true)][object[]]$Attempts,
        [Parameter(Mandatory = $true)][int]$AgentExitCode
    )

    return [int]$State.disconnected_events -eq 1 -and
        (Test-V1RebootConfigurationCoreEvidence `
            -State $State `
            -Session $Session `
            -Attempts $Attempts `
            -AgentExitCode $AgentExitCode)
}

function Restore-V1RebootDiscoveryOriginalA2dp {
    param(
        [Parameter(Mandatory = $true)]$Transaction,
        [Parameter(Mandatory = $true)][string]$LogDirectory
    )

    $null = Remove-LegacyTestDriverPackages -LogDirectory $LogDirectory
    $restoredState = Restore-LegacyOriginalA2dp `
        -BackupPath ([string]$Transaction.backup_path) `
        -LogDirectory $LogDirectory
    Set-Service -Name 'AltA2dpSVC' -StartupType Automatic
    Start-Service -Name 'AltA2dpSVC' -ErrorAction Stop
    $service = Get-NativeLdacAltA2dpUserService
    if ($null -eq $service -or
        [string]$service.start_mode -ne 'Auto' -or
        [string]$service.state -ne 'Running') {
        throw 'Alternative A2DP Service did not return to Automatic/Running.'
    }
    $after = Get-NativeLdacBaselineSnapshot `
        -BackupPath ([string]$Transaction.backup_path)
    if (-not $after.safe_original_a2dp) {
        throw 'The original A2DP baseline is not safe after rollback.'
    }
    return [pscustomobject]@{
        service = [string]$restoredState.service
        published_inf = [string]$restoredState.published_inf
        baseline = $after
    }
}
