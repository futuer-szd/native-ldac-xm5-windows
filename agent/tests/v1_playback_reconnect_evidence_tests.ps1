# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $PSScriptRoot 'v1_playback_disconnect_evidence_tests.ps1')
. (Join-Path $root 'tools\v1-playback-reconnect-common.ps1')

function Copy-ReconnectTestObject($Value) {
    $Value | ConvertTo-Json -Depth 16 | ConvertFrom-Json
}

$generationOneState = Copy-ReconnectTestObject $state
$generationOneState.state = 'absent'
$generationOneState.acl_generation = 1
$generationOneState | Add-Member -NotePropertyName `
    playback_reconnect_wait_enabled -NotePropertyValue $true
$generationOneState | Add-Member -NotePropertyName `
    playback_reconnect_target_generations -NotePropertyValue 2
foreach ($name in @(
        'discovery_sessions_completed',
        'configuration_sessions_completed',
        'silence_sessions_completed')) {
    $generationOneState | Add-Member -NotePropertyName $name `
        -NotePropertyValue 0
}
$generationOneSession = Copy-ReconnectTestObject $session
$generationOneSession.session_generation = 1

$generationTwoState = Copy-ReconnectTestObject $generationOneState
$generationTwoState.acl_generation = 2
$cumulative = @(
    'connected_events', 'disconnected_events',
    'publish_present_actions', 'publish_absent_actions',
    'fail_mute_actions', 'transport_open_actions',
    'transport_open_executed', 'transport_open_stability_waits',
    'transport_open_stability_resets',
    'transport_open_stable_authorizations',
    'transport_retryable_failures', 'transport_retries_scheduled',
    'transport_retry_budget_exhausted',
    'capabilities_discovered_events', 'discovery_sessions_completed',
    'configuration_sessions_completed', 'silence_sessions_completed',
    'pcm_burst_sessions_completed', 'transport_graceful_stop_actions',
    'transport_cancel_actions', 'media_started_events',
    'media_stopped_events', 'media_failed_events',
    'transport_stop_acknowledgements', 'child_processes_started',
    'endpoint_presence_updates', 'endpoint_presence_failures',
    'render_query_count', 'render_query_failures',
    'render_started_events', 'render_stopped_events',
    'pre_media_render_stop_events', 'render_stop_deferred_events',
    'render_stop_resumed_events', 'render_stop_timeout_events',
    'render_stop_acl_cancelled_events', 'engine_start_requests',
    'engine_stop_requests', 'engine_ready_events',
    'engine_exit_events', 'engine_start_failures',
    'engine_ready_timeouts', 'engine_stop_failures',
    'engine_graceful_stops', 'engine_unexpected_exits')
foreach ($name in $cumulative) {
    $generationTwoState.$name = 2 * [int64]$generationOneState.$name
}
$generationTwoSession = Copy-ReconnectTestObject $generationOneSession
$generationTwoSession.session_generation = 2
$finalState = Copy-ReconnectTestObject $generationTwoState
$finalState.state = 'stopped'

$valid = @{
    FinalState = $finalState
    GenerationStates = @($generationOneState, $generationTwoState)
    GenerationSessions = @($generationOneSession, $generationTwoSession)
    FinalSession = $generationTwoSession
    AgentExitCode = 0
    EndpointReconnectObserved = $true
    IntermediatePublicDisconnectObserved = $true
    FinalPublicDisconnectObserved = $true
}
if (-not (Test-V1PlaybackReconnectEvidence @valid)) {
    throw 'Valid two-generation playback reconnect evidence was rejected.'
}

function Assert-ReconnectRejected {
    param([string]$Name, [hashtable]$Arguments)
    try {
        $accepted = Test-V1PlaybackReconnectEvidence @Arguments
    } catch {
        throw "Reconnect validator threw for '$Name': $($_.Exception.Message)"
    }
    if ($accepted) {
        throw "Reconnect validator accepted negative case: $Name"
    }
}

$missingGeneration = $valid.Clone()
$missingGeneration.GenerationStates = @($generationOneState)
Assert-ReconnectRejected 'missing second generation state' $missingGeneration

$retryState = Copy-ReconnectTestObject $finalState
$retryState.transport_retries_scheduled = 1
$retry = $valid.Clone(); $retry.FinalState = $retryState
Assert-ReconnectRejected 'retry in aggregate state' $retry

$unreleasedSession = Copy-ReconnectTestObject $generationTwoSession
$unreleasedSession.consumer_lease_released = $false
$unreleased = $valid.Clone()
$unreleased.GenerationSessions = @($generationOneSession, $unreleasedSession)
$unreleased.FinalSession = $unreleasedSession
Assert-ReconnectRejected 'generation two ConsumerLease leak' $unreleased

$staleSession = Copy-ReconnectTestObject $generationTwoSession
$staleSession.session_generation = 1
$stale = $valid.Clone()
$stale.GenerationSessions = @($generationOneSession, $staleSession)
$stale.FinalSession = $staleSession
Assert-ReconnectRejected 'stale generation two session' $stale

$noPublicEdge = $valid.Clone()
$noPublicEdge.IntermediatePublicDisconnectObserved = $false
Assert-ReconnectRejected 'missing intermediate public disconnect' $noPublicEdge

$noEndpointEdge = $valid.Clone()
$noEndpointEdge.EndpointReconnectObserved = $false
Assert-ReconnectRejected 'missing active absent active endpoint sequence' `
    $noEndpointEdge

$wrongFinal = $valid.Clone()
$wrongFinal.FinalSession = $generationOneSession
Assert-ReconnectRejected 'final session does not match generation two archive' `
    $wrongFinal

$driverTree = '85a0b46231ae2f3212e6616346e2d6905314f0ff'
$transactionPath = 'C:\evidence\playback-disconnect-transaction.json'
$resultPath = 'C:\evidence\playback-disconnect-result.json'
$transaction = [pscustomobject]@{
    schema_version = 1; transport_policy_version = 20
    status = 'playback-disconnect-verified'; source_commit = ('1' * 40)
    driver_tree = $driverTree; result = $resultPath
}
$result = [pscustomobject]@{
    schema_version = 1; transport_policy_version = 20
    playback_disconnect_passed = $true; source_commit = ('1' * 40)
    driver_tree = $driverTree; transaction = $transactionPath
    session_generation = 1; acl_generation = 1
    transport_open_attempts = 1; transport_retry_count = 0
    transport_open_stable_authorizations = 1
    signaling_direction = 'inbound'; endpoint_active_observed = $true
    physical_acl_disconnected = $true; consumer_lease_released = $true
    driver_installed_or_updated = $false; rebooted = $false
    bluetooth_toggled = $false; default_output_changed = $false
    link_state_written = $false
}
$prerequisite = @{
    Transaction = $transaction; TransactionPath = $transactionPath
    Result = $result; ResultPath = $resultPath
    ExpectedDriverTree = $driverTree
}
if (-not (Test-V1PlaybackReconnectPrerequisite @prerequisite)) {
    throw 'Valid policy 20 reconnect prerequisite was rejected.'
}
$result.transport_retry_count = 1
if (Test-V1PlaybackReconnectPrerequisite @prerequisite) {
    throw 'A policy 20 prerequisite with a retry was accepted.'
}

Write-Host 'V1 playback reconnect evidence tests passed.'
