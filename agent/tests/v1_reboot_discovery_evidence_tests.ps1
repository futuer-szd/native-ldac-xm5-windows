# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
. (Join-Path $projectRoot 'tools\v1-reboot-discovery-common.ps1')

$state = [pscustomobject]@{
    mode = 'transport-discovery-exercise'
    state = 'stopped'
    physical_presence = 'absent'
    connected_events = 1
    disconnected_events = 1
    transport_open_actions = 3
    transport_open_executed = 3
    transport_open_attempts_for_generation = 0
    transport_retryable_failures = 2
    transport_retries_scheduled = 2
    transport_retry_budget_exhausted = 0
    capabilities_discovered_events = 1
    discovery_sessions_completed = 1
    media_started_events = 0
    media_failed_events = 0
    transport_stop_acknowledgements = 3
    child_processes_started = 3
    engine_ready_events = 3
    engine_graceful_stops = 3
    engine_exit_events = 3
    engine_unexpected_exits = 0
}
$retryable = [pscustomobject]@{
    disposition = 'backend-failure'
    stage = 1
    backend_error = 71
    open_attempts = 1
    signaling_exchanges = 0
    strictly_retryable_open_failure = $true
}
$session = [pscustomobject]@{
    disposition = 'succeeded'
    open_attempts = 1
    signaling_exchanges = 4
    signaling_opened = $true
    close_attempted = $true
    close_succeeded = $true
    strictly_retryable_open_failure = $false
}
$attempts = @($retryable, $retryable, $session)

$legacyTransaction = [pscustomobject]@{
    source_commit = '0000000000000000000000000000000000000000'
    discovery = [pscustomobject]@{
        passed = $false
        agent_exit_code = 0
    }
    post_reboot = [pscustomobject]@{
        radio_state = 'ready'
        state = 'legacy-state.json'
        session = 'legacy-session.json'
    }
}
if (Test-V1CapabilityPrerequisiteTransaction `
        -Transaction $legacyTransaction `
        -ExpectedDriverTree '1111111111111111111111111111111111111111' `
        -ProjectRoot $projectRoot) {
    throw 'A legacy transaction without attempt_results was accepted.'
}

if (-not (Test-V1RebootDiscoveryEvidence `
        -State $state `
        -Session $session `
        -Attempts $attempts `
        -AgentExitCode 0)) {
    throw 'A successful three-attempt discovery was rejected after ACL cleanup reset the generation-local counter.'
}

$staleState = $state | ConvertTo-Json | ConvertFrom-Json
$staleState.transport_open_attempts_for_generation = 3
if (Test-V1RebootDiscoveryEvidence `
        -State $staleState `
        -Session $session `
        -Attempts $attempts `
        -AgentExitCode 0) {
    throw 'Disconnected evidence retained a stale generation-local attempt count.'
}

$badAttempt = $retryable | ConvertTo-Json | ConvertFrom-Json
$badAttempt.backend_error = 1167
if (Test-V1RebootDiscoveryEvidence `
        -State $state `
        -Session $session `
        -Attempts @($retryable, $badAttempt, $session) `
        -AgentExitCode 0) {
    throw 'A non-Win32-71 intermediate failure was accepted.'
}

if (Test-V1RebootDiscoveryEvidence `
        -State $state `
        -Session $session `
        -Attempts @($retryable, $session) `
        -AgentExitCode 0) {
    throw 'Attempt archive count mismatch was accepted.'
}

$configurationState = $state | ConvertTo-Json | ConvertFrom-Json
$configurationState.mode = 'transport-configuration-exercise'
$configurationState | Add-Member -NotePropertyName `
    configuration_sessions_completed -NotePropertyValue 1
$configurationState.discovery_sessions_completed = 0
$configurationSession = [pscustomobject]@{
    disposition = 'succeeded'
    open_attempts = 1
    signaling_exchanges = 5
    signaling_opened = $true
    set_configuration_accepted = $true
    avdtp_open_accepted = $true
    media_opened = $true
    outgoing_mtu = 895
    avdtp_close_accepted = $true
    close_attempted = $true
    close_succeeded = $true
    media_start_commands = 0
    media_packets_written = 0
    strictly_retryable_open_failure = $false
}
if (-not (Test-V1RebootConfigurationEvidence `
        -State $configurationState `
        -Session $configurationSession `
        -Attempts @($retryable, $retryable, $configurationSession) `
        -AgentExitCode 0)) {
    throw 'A valid zero-packet configuration result was rejected.'
}
$lateFinalizationState = $configurationState |
    ConvertTo-Json | ConvertFrom-Json
$lateFinalizationState.disconnected_events = 0
if (-not (Test-V1RebootConfigurationCoreEvidence `
        -State $lateFinalizationState `
        -Session $configurationSession `
        -Attempts @($retryable, $retryable, $configurationSession) `
        -AgentExitCode 0)) {
    throw 'Valid transport evidence was rejected only because the bounded agent ended before physical disconnect.'
}
if (Test-V1RebootConfigurationEvidence `
        -State $lateFinalizationState `
        -Session $configurationSession `
        -Attempts @($retryable, $retryable, $configurationSession) `
        -AgentExitCode 0) {
    throw 'The normal gate accepted evidence without an observed ACL disconnect.'
}
$unsafeSession = $configurationSession | ConvertTo-Json | ConvertFrom-Json
$unsafeSession.media_start_commands = 1
if (Test-V1RebootConfigurationEvidence `
        -State $configurationState `
        -Session $unsafeSession `
        -Attempts @($retryable, $retryable, $unsafeSession) `
        -AgentExitCode 0) {
    throw 'A configuration result containing AVDTP START was accepted.'
}

Write-Host 'V1 reboot pre-stream evidence tests passed.'
