# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))

. (Join-Path $PSScriptRoot 'v1_normal_stop_evidence_tests.ps1')
$normalStateTemplate = $state | ConvertTo-Json -Depth 16 | ConvertFrom-Json
$normalSessionTemplate = $session | ConvertTo-Json -Depth 16 | ConvertFrom-Json
. (Join-Path $PSScriptRoot 'v1_playback_disconnect_evidence_tests.ps1')
$disconnectStateTemplate = $state | ConvertTo-Json -Depth 16 | ConvertFrom-Json
$disconnectSessionTemplate = $session | ConvertTo-Json -Depth 16 |
    ConvertFrom-Json
. (Join-Path $root 'tools\v1-lifecycle-soak-common.ps1')

function Copy-SoakTestObject($Value) {
    $Value | ConvertTo-Json -Depth 16 | ConvertFrom-Json
}

$normalIncrement = Copy-SoakTestObject $disconnectStateTemplate
foreach ($name in @(
        'render_started_events', 'render_stopped_events',
        'pre_media_render_stop_events', 'render_stop_deferred_events',
        'render_stop_resumed_events', 'render_stop_timeout_events',
        'render_stop_acl_cancelled_events', 'media_started_events',
        'media_stopped_events', 'media_failed_events',
        'transport_graceful_stop_actions', 'transport_stop_acknowledgements',
        'engine_graceful_stops', 'engine_exit_events',
        'engine_unexpected_exits')) {
    $normalIncrement.$name = [int64]$normalStateTemplate.$name
}
$normalIncrement.transport_cancel_actions = 0
$normalIncrement.playback_disconnect_wait_enabled = $true
$normalIncrement | Add-Member -NotePropertyName `
    playback_reconnect_wait_enabled -NotePropertyValue $true
$normalIncrement | Add-Member -NotePropertyName `
    playback_reconnect_target_generations -NotePropertyValue 3
$disconnectIncrement = Copy-SoakTestObject $disconnectStateTemplate
$disconnectIncrement | Add-Member -NotePropertyName `
    playback_reconnect_wait_enabled -NotePropertyValue $true
$disconnectIncrement | Add-Member -NotePropertyName `
    playback_reconnect_target_generations -NotePropertyValue 3
foreach ($increment in @($normalIncrement, $disconnectIncrement)) {
    foreach ($name in @(
            'discovery_sessions_completed',
            'configuration_sessions_completed',
            'silence_sessions_completed')) {
        if ($null -eq $increment.PSObject.Properties[$name]) {
            $increment | Add-Member -NotePropertyName $name `
                -NotePropertyValue 0
        }
    }
}

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
    'engine_stop_requests', 'engine_ready_events', 'engine_exit_events',
    'engine_start_failures', 'engine_ready_timeouts',
    'engine_stop_failures', 'engine_graceful_stops',
    'engine_unexpected_exits')

function New-SoakSnapshot {
    param($Increment, $Previous, [int]$Generation)
    $snapshot = Copy-SoakTestObject $Increment
    foreach ($name in $cumulative) {
        $before = if ($null -eq $Previous) { 0 } else { [int64]$Previous.$name }
        $snapshot.$name = $before + [int64]$Increment.$name
    }
    $snapshot.acl_generation = $Generation
    $snapshot.state = 'absent'
    $snapshot.physical_presence = 'absent'
    $snapshot.render_demand = 'idle'
    return $snapshot
}

$generationOneState = New-SoakSnapshot `
    -Increment $normalIncrement -Previous $null -Generation 1
$generationTwoState = New-SoakSnapshot `
    -Increment $disconnectIncrement -Previous $generationOneState -Generation 2
$generationThreeState = New-SoakSnapshot `
    -Increment $normalIncrement -Previous $generationTwoState -Generation 3
$generationOneSession = Copy-SoakTestObject $normalSessionTemplate
$generationOneSession.session_generation = 1
$generationTwoSession = Copy-SoakTestObject $disconnectSessionTemplate
$generationTwoSession.session_generation = 2
$generationThreeSession = Copy-SoakTestObject $normalSessionTemplate
$generationThreeSession.session_generation = 3
$finalState = Copy-SoakTestObject $generationThreeState
$finalState.state = 'stopped'

$valid = @{
    FinalState = $finalState
    GenerationStates = @(
        $generationOneState, $generationTwoState, $generationThreeState)
    GenerationSessions = @(
        $generationOneSession, $generationTwoSession,
        $generationThreeSession)
    FinalSession = $generationThreeSession
    AgentExitCode = 0
    EndpointTimelineObserved = $true
    AclTimelineObserved = $true
    FinalPublicDisconnectObserved = $true
    FinalPnpHealthy = $true
}
if (-not (Test-V1LifecycleSoakEvidence @valid)) {
    throw 'Valid three-generation lifecycle-soak evidence was rejected.'
}

function Assert-SoakRejected {
    param([string]$Name, [hashtable]$Arguments)
    try {
        $accepted = Test-V1LifecycleSoakEvidence @Arguments
    } catch {
        throw "Soak validator threw for '$Name': $($_.Exception.Message)"
    }
    if ($accepted) {
        throw "Soak validator accepted negative case: $Name"
    }
}

$missing = $valid.Clone()
$missing.GenerationStates = @($generationOneState, $generationTwoState)
Assert-SoakRejected 'missing third generation' $missing

$wrongMiddle = $valid.Clone()
$wrongMiddle.GenerationSessions = @(
    $generationOneSession, $generationThreeSession, $generationThreeSession)
Assert-SoakRejected 'middle generation is not a playback disconnect' $wrongMiddle

$retryState = Copy-SoakTestObject $finalState
$retryState.transport_retries_scheduled = 1
$retry = $valid.Clone(); $retry.FinalState = $retryState
Assert-SoakRejected 'aggregate retry' $retry

$leak = Copy-SoakTestObject $generationThreeSession
$leak.consumer_lease_release_count = 0
$leak.consumer_lease_released = $false
$unreleased = $valid.Clone()
$unreleased.GenerationSessions = @(
    $generationOneSession, $generationTwoSession, $leak)
$unreleased.FinalSession = $leak
Assert-SoakRejected 'generation three ConsumerLease leak' $unreleased

$wrongTargetState = Copy-SoakTestObject $finalState
$wrongTargetState.playback_reconnect_target_generations = 2
$wrongTarget = $valid.Clone(); $wrongTarget.FinalState = $wrongTargetState
Assert-SoakRejected 'host target is not three generations' $wrongTarget

$noEndpoint = $valid.Clone(); $noEndpoint.EndpointTimelineObserved = $false
Assert-SoakRejected 'missing endpoint lifecycle' $noEndpoint
$badPnp = $valid.Clone(); $badPnp.FinalPnpHealthy = $false
Assert-SoakRejected 'unhealthy delayed PnP baseline' $badPnp

$endpointTimeline = @'
+100ms: Speakers (Native LDAC Speaker Topology) -> active
+200ms: Speakers (Native LDAC Speaker Topology) -> unplugged
+300ms: Speakers (Native LDAC Speaker Topology) -> active
+400ms: Speakers (Native LDAC Speaker Topology) -> unplugged
+500ms: Speakers (Native LDAC Speaker Topology) -> active
+600ms: Speakers (Native LDAC Speaker Topology) -> unplugged
'@
if (-not (Test-V1LifecycleSoakEndpointTimeline -Text $endpointTimeline)) {
    throw 'Valid three-cycle endpoint timeline was rejected.'
}
if (Test-V1LifecycleSoakEndpointTimeline -Text `
        ($endpointTimeline -replace '\+500ms:[^\r\n]+\r?\n', '')) {
    throw 'Endpoint timeline without generation three active was accepted.'
}

$aclLines = @()
for ($generation = 1; $generation -le 3; $generation++) {
    $offset = $generation * 1000
    $aclLines += "+${offset}ms ACL connected."
    $aclLines += "+${offset}ms snapshot(acl-event): fConnected=connected, a2dp-pdo=1/1-healthy service=LdacNative, render=1 active=1 unplugged=0 not-present=0 disabled=0 native=1."
    $offset += 500
    $aclLines += "+${offset}ms ACL disconnected."
    $aclLines += "+${offset}ms snapshot(acl-event): fConnected=disconnected, a2dp-pdo=1/1-healthy service=LdacNative, render=1 active=0 unplugged=1 not-present=0 disabled=0 native=1."
}
$aclTimeline = $aclLines -join "`r`n"
if (-not (Test-V1LifecycleSoakAclTimeline -Text $aclTimeline)) {
    throw 'Valid three-cycle ACL/PnP timeline was rejected.'
}
if (-not (Test-V1LifecycleSoakEndpointAclTimeline -Text $aclTimeline)) {
    throw 'Valid endpoint lifecycle in ACL snapshots was rejected.'
}
if (Test-V1LifecycleSoakAclTimeline -Text `
        ($aclTimeline -replace '1/1-healthy', '0/1-unhealthy')) {
    throw 'ACL timeline with unhealthy PnP snapshots was accepted.'
}

$driverTree = '85a0b46231ae2f3212e6616346e2d6905314f0ff'
$transactionPath = 'C:\evidence\playback-reconnect-transaction.json'
$resultPath = 'C:\evidence\playback-reconnect-result.json'
$transaction = [pscustomobject]@{
    schema_version=1; transport_policy_version=21
    status='playback-reconnect-verified'; source_commit=('1' * 40)
    driver_tree=$driverTree; result=$resultPath
}
$result = [pscustomobject]@{
    schema_version=1; transport_policy_version=21
    playback_reconnect_passed=$true; source_commit=('1' * 40)
    driver_tree=$driverTree; transaction=$transactionPath
    acl_generations=2
    generations=@(
        [pscustomobject]@{ acl_generation=1; transport_open_executed=1
            consumer_lease_acquire_count=1; consumer_lease_release_count=1 },
        [pscustomobject]@{ acl_generation=2; transport_open_executed=1
            consumer_lease_acquire_count=1; consumer_lease_release_count=1 })
    intermediate_public_disconnect_observed=$true
    endpoint_active_absent_active_observed=$true
    final_public_disconnect_observed=$true
    total_transport_open_attempts=2; total_transport_retry_count=0
    total_consumer_lease_acquires=2; total_consumer_lease_releases=2
    driver_installed_or_updated=$false; rebooted=$false
    bluetooth_toggled=$false; default_output_changed=$false
    link_state_written=$false
}
$prerequisite = @{
    Transaction=$transaction; TransactionPath=$transactionPath
    Result=$result; ResultPath=$resultPath; ExpectedDriverTree=$driverTree
}
if (-not (Test-V1LifecycleSoakPrerequisite @prerequisite)) {
    throw 'Valid policy 21 soak prerequisite was rejected.'
}
$result.total_transport_retry_count = 1
if (Test-V1LifecycleSoakPrerequisite @prerequisite) {
    throw 'A policy 21 prerequisite with a retry was accepted.'
}

Write-Host 'V1 lifecycle soak evidence tests passed.'
