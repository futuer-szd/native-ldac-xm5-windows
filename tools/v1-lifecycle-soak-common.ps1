# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'v1-playback-reconnect-common.ps1')

$script:V1LifecycleSoakPolicyVersion = 22
$script:V1LifecycleSoakPrerequisiteRelativePath =
    'artifacts\v1-playback-reconnect\trial\transaction-20260802-162034-202.json'
$script:V1LifecycleSoakRequiredGenerations = 3
$script:V1LifecycleSoakRecoverableTransactionRelativePath =
    'artifacts\v1-lifecycle-soak\trial\transaction-20260802-173536-587.json'

function Test-V1LifecycleSoakPrerequisite {
    param(
        [Parameter(Mandatory = $true)]$Transaction,
        [Parameter(Mandatory = $true)][string]$TransactionPath,
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)][string]$ResultPath,
        [Parameter(Mandatory = $true)][string]$ExpectedDriverTree
    )
    try {
        if ([int]$Transaction.schema_version -ne 1 -or
            [int]$Transaction.transport_policy_version -ne 21 -or
            [string]$Transaction.status -ne 'playback-reconnect-verified' -or
            [string]$Transaction.driver_tree -ne $ExpectedDriverTree -or
            [string]$Transaction.result -ne $ResultPath -or
            [int]$Result.schema_version -ne 1 -or
            [int]$Result.transport_policy_version -ne 21 -or
            $Result.playback_reconnect_passed -ne $true -or
            [string]$Result.source_commit -ne
                [string]$Transaction.source_commit -or
            [string]$Result.driver_tree -ne $ExpectedDriverTree -or
            [string]$Result.transaction -ne $TransactionPath -or
            [int]$Result.acl_generations -ne 2 -or
            @($Result.generations).Count -ne 2 -or
            $Result.intermediate_public_disconnect_observed -ne $true -or
            $Result.endpoint_active_absent_active_observed -ne $true -or
            $Result.final_public_disconnect_observed -ne $true -or
            [int]$Result.total_transport_open_attempts -ne 2 -or
            [int]$Result.total_transport_retry_count -ne 0 -or
            [int]$Result.total_consumer_lease_acquires -ne 2 -or
            [int]$Result.total_consumer_lease_releases -ne 2 -or
            $Result.driver_installed_or_updated -ne $false -or
            $Result.rebooted -ne $false -or
            $Result.bluetooth_toggled -ne $false -or
            $Result.default_output_changed -ne $false -or
            $Result.link_state_written -ne $false) {
            return $false
        }
        for ($index = 0; $index -lt 2; $index++) {
            $generation = $Result.generations[$index]
            if ([int64]$generation.acl_generation -ne ($index + 1) -or
                [int]$generation.transport_open_executed -ne 1 -or
                [int]$generation.consumer_lease_acquire_count -ne 1 -or
                [int]$generation.consumer_lease_release_count -ne 1) {
                return $false
            }
        }
        return $true
    } catch {
        return $false
    }
}

function Test-V1LifecycleSoakEndpointTimeline {
    param([AllowEmptyString()][string]$Text)
    $matches = [regex]::Matches(
        $Text,
        '(?m)^\+\d+ms: .*Native LDAC Speaker Topology.* -> (active|unplugged|not-present|disabled)\s*$')
    $expected = @(
        'active', 'absent', 'active', 'absent', 'active', 'absent')
    $observed = @()
    foreach ($match in $matches) {
        $state = [string]$match.Groups[1].Value
        $normalized = if ($state -eq 'active') { 'active' } else { 'absent' }
        if ($observed.Count -eq 0 -or $observed[-1] -ne $normalized) {
            $observed += $normalized
        }
    }
    return $observed.Count -eq $expected.Count -and
        (($observed -join ',') -eq ($expected -join ','))
}

function Test-V1LifecycleSoakAclTimeline {
    param([AllowEmptyString()][string]$Text)
    $events = [regex]::Matches(
        $Text,
        '(?m)^\+\d+ms ACL (connected|disconnected)\.\s*$')
    $expected = @(
        'connected', 'disconnected', 'connected', 'disconnected',
        'connected', 'disconnected')
    if ($events.Count -ne $expected.Count) {
        return $false
    }
    for ($index = 0; $index -lt $expected.Count; $index++) {
        if ([string]$events[$index].Groups[1].Value -ne $expected[$index]) {
            return $false
        }
    }
    $snapshots = [regex]::Matches(
        $Text,
        '(?m)^\+\d+ms snapshot\([^)]*\): ([^\r\n]+)\r?$')
    if ($snapshots.Count -lt 6) {
        return $false
    }
    $publicDisconnects = 0
    foreach ($snapshot in $snapshots) {
        $line = [string]$snapshot.Groups[1].Value
        if ($line -notmatch 'a2dp-pdo=1/1-healthy service=LdacNative') {
            return $false
        }
        if ($line -match '^fConnected=disconnected,') {
            $publicDisconnects++
        }
    }
    return $publicDisconnects -ge 3 -and
        (Test-V1LifecycleSoakEndpointAclTimeline -Text $Text)
}

function Test-V1LifecycleSoakEndpointAclTimeline {
    param([AllowEmptyString()][string]$Text)
    $phase = 'await-connect'
    $cycles = 0
    foreach ($line in @($Text -split '\r?\n')) {
        if ($line -match '^\+\d+ms ACL connected\.$') {
            if ($phase -ne 'await-connect') { return $false }
            $phase = 'await-active'
            continue
        }
        if ($line -match '^\+\d+ms ACL disconnected\.$') {
            if ($phase -ne 'await-disconnect') { return $false }
            $phase = 'await-absent'
            continue
        }
        if ($line -notmatch '^\+\d+ms snapshot\([^)]*\):') {
            continue
        }
        if ($line -notmatch
            'a2dp-pdo=1/1-healthy service=LdacNative') {
            return $false
        }
        if ($phase -eq 'await-active' -and
            $line -match 'fConnected=connected,.* render=\d+ active=1 ') {
            $phase = 'await-disconnect'
        } elseif ($phase -eq 'await-absent' -and
            $line -match
                'fConnected=disconnected,.* render=\d+ active=0 unplugged=[1-9]\d* ') {
            $cycles++
            $phase = 'await-connect'
        }
    }
    return $cycles -eq 3 -and $phase -eq 'await-connect'
}

function Test-V1LifecycleSoakEvidence {
    param(
        $FinalState,
        [object[]]$GenerationStates,
        [object[]]$GenerationSessions,
        $FinalSession,
        [int]$AgentExitCode,
        [bool]$EndpointTimelineObserved,
        [bool]$AclTimelineObserved,
        [bool]$FinalPublicDisconnectObserved,
        [bool]$FinalPnpHealthy
    )
    try {
        if ($null -eq $FinalState -or $null -eq $FinalSession -or
            @($GenerationStates).Count -ne
                $script:V1LifecycleSoakRequiredGenerations -or
            @($GenerationSessions).Count -ne
                $script:V1LifecycleSoakRequiredGenerations -or
            $AgentExitCode -ne 0 -or
            -not $EndpointTimelineObserved -or
            -not $AclTimelineObserved -or
            -not $FinalPublicDisconnectObserved -or
            -not $FinalPnpHealthy) {
            return $false
        }
        $required = @(
            'playback_reconnect_wait_enabled',
            'playback_reconnect_target_generations',
            'connected_events', 'disconnected_events', 'acl_generation',
            'media_started_events', 'media_stopped_events',
            'transport_graceful_stop_actions', 'transport_cancel_actions',
            'transport_open_executed',
            'transport_open_stable_authorizations',
            'transport_retries_scheduled', 'child_processes_started',
            'transport_stop_acknowledgements',
            'endpoint_presence_failures', 'engine_unexpected_exits')
        if (-not (Test-V1PlaybackDisconnectHasProperties `
                -Value $FinalState -Properties $required) -or
            $FinalState.playback_reconnect_wait_enabled -ne $true -or
            [int]$FinalState.playback_reconnect_target_generations -ne 3 -or
            [int64]$FinalState.acl_generation -ne 3 -or
            [int]$FinalState.connected_events -ne 3 -or
            [int]$FinalState.disconnected_events -ne 3 -or
            [int]$FinalState.media_started_events -ne 3 -or
            [int]$FinalState.media_stopped_events -ne 2 -or
            [int]$FinalState.transport_graceful_stop_actions -ne 2 -or
            [int]$FinalState.transport_cancel_actions -ne 1 -or
            [int]$FinalState.transport_open_executed -ne 3 -or
            [int]$FinalState.transport_open_stable_authorizations -ne 3 -or
            [int]$FinalState.transport_retries_scheduled -ne 0 -or
            [int]$FinalState.child_processes_started -ne 3 -or
            [int]$FinalState.transport_stop_acknowledgements -ne 3 -or
            [int]$FinalState.endpoint_presence_failures -ne 0 -or
            [int]$FinalState.engine_unexpected_exits -ne 0 -or
            -not (Test-V1PlaybackDisconnectSameArchive `
                -Session $FinalSession -Archive $GenerationSessions[2])) {
            return $false
        }

        $previous = $null
        for ($index = 0; $index -lt 3; $index++) {
            $generation = $index + 1
            $snapshot = $GenerationStates[$index]
            $session = $GenerationSessions[$index]
            if ([int64]$snapshot.acl_generation -ne $generation -or
                [string]$snapshot.state -ne 'absent' -or
                [string]$snapshot.physical_presence -ne 'absent' -or
                [string]$snapshot.render_demand -ne 'idle' -or
                $snapshot.playback_reconnect_wait_enabled -ne $true -or
                [int]$snapshot.playback_reconnect_target_generations -ne 3 -or
                [int64]$session.session_generation -ne $generation) {
                return $false
            }
            $generationState = Get-V1PlaybackReconnectGenerationState `
                -Current $snapshot -Previous $previous `
                -Generation $generation
            $valid = if ($index -eq 1) {
                Test-V1PlaybackDisconnectEvidence `
                    -State $generationState -Session $session `
                    -Attempts @($session) -AgentExitCode 0
            } else {
                Test-V1NormalStopEvidence `
                    -State $generationState -Session $session `
                    -Attempts @($session) -AgentExitCode 0
            }
            if (-not $valid) {
                return $false
            }
            $previous = $snapshot
        }
        return $true
    } catch {
        return $false
    }
}
