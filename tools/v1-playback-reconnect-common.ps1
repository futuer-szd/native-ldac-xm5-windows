# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'v1-playback-disconnect-common.ps1')

$script:V1PlaybackReconnectPolicyVersion = 21
$script:V1PlaybackReconnectPrerequisiteRelativePath =
    'artifacts\v1-playback-disconnect\trial\transaction-20260802-145230-814.json'
$script:V1PlaybackReconnectRequiredGenerations = 2

function Test-V1PlaybackReconnectPrerequisite {
    param(
        [Parameter(Mandatory = $true)]$Transaction,
        [Parameter(Mandatory = $true)][string]$TransactionPath,
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)][string]$ResultPath,
        [Parameter(Mandatory = $true)][string]$ExpectedDriverTree
    )
    return [int]$Transaction.schema_version -eq 1 -and
        [int]$Transaction.transport_policy_version -eq 20 -and
        [string]$Transaction.status -eq 'playback-disconnect-verified' -and
        [string]$Transaction.driver_tree -eq $ExpectedDriverTree -and
        [string]$Transaction.result -eq $ResultPath -and
        [int]$Result.schema_version -eq 1 -and
        [int]$Result.transport_policy_version -eq 20 -and
        $Result.playback_disconnect_passed -eq $true -and
        [string]$Result.source_commit -eq
            [string]$Transaction.source_commit -and
        [string]$Result.driver_tree -eq $ExpectedDriverTree -and
        [string]$Result.transaction -eq $TransactionPath -and
        [int64]$Result.session_generation -eq 1 -and
        [int64]$Result.acl_generation -eq 1 -and
        [int]$Result.transport_open_attempts -eq 1 -and
        [int]$Result.transport_retry_count -eq 0 -and
        [int]$Result.transport_open_stable_authorizations -eq 1 -and
        [string]$Result.signaling_direction -eq 'inbound' -and
        $Result.endpoint_active_observed -eq $true -and
        $Result.physical_acl_disconnected -eq $true -and
        $Result.consumer_lease_released -eq $true -and
        $Result.driver_installed_or_updated -eq $false -and
        $Result.rebooted -eq $false -and
        $Result.bluetooth_toggled -eq $false -and
        $Result.default_output_changed -eq $false -and
        $Result.link_state_written -eq $false
}

function Copy-V1PlaybackReconnectObject {
    param([Parameter(Mandatory = $true)]$Value)
    return $Value | ConvertTo-Json -Depth 16 | ConvertFrom-Json
}

function Get-V1PlaybackReconnectGenerationState {
    param(
        [Parameter(Mandatory = $true)]$Current,
        $Previous,
        [Parameter(Mandatory = $true)][int64]$Generation
    )
    $result = Copy-V1PlaybackReconnectObject -Value $Current
    $cumulative = @(
        'connected_events', 'disconnected_events',
        'publish_present_actions', 'publish_absent_actions',
        'fail_mute_actions', 'transport_open_actions',
        'transport_open_executed', 'transport_open_stability_waits',
        'transport_open_stability_resets',
        'transport_open_stable_authorizations',
        'transport_retryable_failures', 'transport_retries_scheduled',
        'transport_retry_budget_exhausted',
        'capabilities_discovered_events',
        'discovery_sessions_completed',
        'configuration_sessions_completed', 'silence_sessions_completed',
        'pcm_burst_sessions_completed',
        'transport_graceful_stop_actions', 'transport_cancel_actions',
        'media_started_events', 'media_stopped_events',
        'media_failed_events', 'transport_stop_acknowledgements',
        'child_processes_started', 'endpoint_presence_updates',
        'endpoint_presence_failures', 'render_query_count',
        'render_query_failures', 'render_started_events',
        'render_stopped_events', 'pre_media_render_stop_events',
        'render_stop_deferred_events', 'render_stop_resumed_events',
        'render_stop_timeout_events',
        'render_stop_acl_cancelled_events', 'engine_start_requests',
        'engine_stop_requests', 'engine_ready_events',
        'engine_exit_events', 'engine_start_failures',
        'engine_ready_timeouts', 'engine_stop_failures',
        'engine_graceful_stops', 'engine_unexpected_exits')
    if ($null -ne $Previous) {
        foreach ($name in $cumulative) {
            if ($null -eq $Current.PSObject.Properties[$name] -or
                $null -eq $Previous.PSObject.Properties[$name]) {
                throw "Reconnect generation state is missing counter: $name"
            }
            $result.$name = [int64]$Current.$name -
                [int64]$Previous.$name
        }
    }
    $result.state = 'stopped'
    $result.acl_generation = $Generation
    $result.transport_open_attempts_for_generation = 0
    $result.transport_open_stability_pending = $false
    $result.render_stop_pending = $false
    return $result
}

function Test-V1PlaybackReconnectEvidence {
    param(
        $FinalState,
        [object[]]$GenerationStates,
        [object[]]$GenerationSessions,
        $FinalSession,
        [int]$AgentExitCode,
        [bool]$EndpointReconnectObserved,
        [bool]$IntermediatePublicDisconnectObserved,
        [bool]$FinalPublicDisconnectObserved
    )
    try {
        if ($null -eq $FinalState -or $null -eq $FinalSession -or
            @($GenerationStates).Count -ne
                $script:V1PlaybackReconnectRequiredGenerations -or
            @($GenerationSessions).Count -ne
                $script:V1PlaybackReconnectRequiredGenerations -or
            $AgentExitCode -ne 0 -or
            -not $EndpointReconnectObserved -or
            -not $IntermediatePublicDisconnectObserved -or
            -not $FinalPublicDisconnectObserved) {
            return $false
        }
        $requiredFinalProperties = @(
            'playback_reconnect_wait_enabled', 'connected_events',
            'playback_reconnect_target_generations',
            'disconnected_events', 'acl_generation',
            'media_started_events', 'transport_open_executed',
            'transport_open_stable_authorizations',
            'transport_retries_scheduled', 'child_processes_started',
            'transport_stop_acknowledgements',
            'endpoint_presence_failures', 'engine_unexpected_exits')
        if (-not (Test-V1PlaybackDisconnectHasProperties `
                -Value $FinalState -Properties $requiredFinalProperties) -or
            $FinalState.playback_reconnect_wait_enabled -ne $true -or
            [int]$FinalState.playback_reconnect_target_generations -ne 2 -or
            [int64]$FinalState.acl_generation -ne 2 -or
            [int]$FinalState.connected_events -ne 2 -or
            [int]$FinalState.disconnected_events -ne 2 -or
            [int]$FinalState.media_started_events -ne 2 -or
            [int]$FinalState.transport_open_executed -ne 2 -or
            [int]$FinalState.transport_open_stable_authorizations -ne 2 -or
            [int]$FinalState.transport_retries_scheduled -ne 0 -or
            [int]$FinalState.child_processes_started -ne 2 -or
            [int]$FinalState.transport_stop_acknowledgements -ne 2 -or
            [int]$FinalState.endpoint_presence_failures -ne 0 -or
            [int]$FinalState.engine_unexpected_exits -ne 0 -or
            -not (Test-V1PlaybackDisconnectSameArchive `
                -Session $FinalSession -Archive $GenerationSessions[1])) {
            return $false
        }
        $previous = $null
        for ($index = 0; $index -lt 2; $index++) {
            $generation = $index + 1
            $snapshot = $GenerationStates[$index]
            if ([int64]$snapshot.acl_generation -ne $generation -or
                [string]$snapshot.state -ne 'absent' -or
                [string]$snapshot.physical_presence -ne 'absent' -or
                [string]$snapshot.render_demand -ne 'idle' -or
                $snapshot.playback_reconnect_wait_enabled -ne $true -or
                [int]$snapshot.playback_reconnect_target_generations -ne 2 -or
                [int64]$GenerationSessions[$index].session_generation -ne
                    $generation) {
                return $false
            }
            $generationState =
                Get-V1PlaybackReconnectGenerationState `
                    -Current $snapshot -Previous $previous `
                    -Generation $generation
            if (-not (Test-V1PlaybackDisconnectEvidence `
                    -State $generationState `
                    -Session $GenerationSessions[$index] `
                    -Attempts @($GenerationSessions[$index]) `
                    -AgentExitCode 0)) {
                return $false
            }
            $previous = $snapshot
        }
        return $true
    } catch {
        return $false
    }
}
