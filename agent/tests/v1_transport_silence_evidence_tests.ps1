# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
. (Join-Path $projectRoot 'tools\v1-silence-burst-common.ps1')

$state = [pscustomobject]@{
    mode = 'transport-silence-exercise'
    state = 'stopped'
    physical_presence = 'absent'
    connected_events = 1
    child_processes_started = 3
    engine_ready_events = 3
    transport_open_actions = 3
    transport_open_executed = 3
    transport_open_attempts_for_generation = 0
    transport_retryable_failures = 2
    transport_retries_scheduled = 2
    transport_retry_budget_exhausted = 0
    capabilities_discovered_events = 1
    discovery_sessions_completed = 0
    configuration_sessions_completed = 0
    silence_sessions_completed = 1
    media_started_events = 0
    media_failed_events = 0
    transport_stop_acknowledgements = 3
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
    signaling_exchanges = 7
    signaling_opened = $true
    remote_seid = 3
    incoming_mtu = 1000
    outgoing_mtu = 895
    set_configuration_accepted = $true
    avdtp_open_accepted = $true
    media_opened = $true
    avdtp_start_accepted = $true
    media_packets_written = 4
    media_bytes_written = 2680
    avdtp_suspend_accepted = $true
    avdtp_close_accepted = $true
    remote_stream_cleanup_required = $false
    close_attempted = $true
    close_succeeded = $true
    strictly_retryable_open_failure = $false
}
$attempts = @($retryable, $retryable, $session)
if (-not (Test-V1SilenceBurstEvidence -State $state -Session $session `
        -Attempts $attempts -AgentExitCode 0)) {
    throw 'Valid three-attempt silence-burst evidence was rejected.'
}

$badPackets = $session | ConvertTo-Json | ConvertFrom-Json
$badPackets.media_packets_written = 5
if (Test-V1SilenceBurstEvidence -State $state -Session $badPackets `
        -Attempts @($retryable, $retryable, $badPackets) `
        -AgentExitCode 0) {
    throw 'Evidence containing more than four packets was accepted.'
}
$unsafeCleanup = $session | ConvertTo-Json | ConvertFrom-Json
$unsafeCleanup.remote_stream_cleanup_required = $true
if (Test-V1SilenceBurstEvidence -State $state -Session $unsafeCleanup `
        -Attempts @($retryable, $retryable, $unsafeCleanup) `
        -AgentExitCode 0) {
    throw 'Evidence requiring remote stream cleanup was accepted.'
}
$badRetry = $retryable | ConvertTo-Json | ConvertFrom-Json
$badRetry.backend_error = 1167
if (Test-V1SilenceBurstEvidence -State $state -Session $session `
        -Attempts @($retryable, $badRetry, $session) `
        -AgentExitCode 0) {
    throw 'A non-Win32-71 retry was accepted.'
}
$presentState = $state | ConvertTo-Json | ConvertFrom-Json
$presentState.physical_presence = 'present'
if (Test-V1SilenceBurstEvidence -State $presentState -Session $session `
        -Attempts $attempts -AgentExitCode 0) {
    throw 'Evidence with an unexpired endpoint presence lease was accepted.'
}

Write-Host 'V1 four-packet digital-zero evidence tests passed.'
