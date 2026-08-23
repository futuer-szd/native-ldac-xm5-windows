# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Medium')]
param(
    [switch]$ConfirmV1LifecycleSoakCompletion,
    [string]$TransactionPath,
    [string]$CandidatePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-lifecycle-soak-common.ps1')

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The lifecycle-soak completion requires PowerShell 7. Run it with pwsh.exe, not powershell.exe.'
}
$principal = [Security.Principal.WindowsPrincipal]::new(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this completion from an elevated PowerShell 7 terminal.'
}
if (-not $ConfirmV1LifecycleSoakCompletion) {
    throw 'Refusing to finalize recovered lifecycle-soak evidence. Re-run with -ConfirmV1LifecycleSoakCompletion.'
}

$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$expectedTransactionPath = [System.IO.Path]::GetFullPath((Join-Path $root `
    $script:V1LifecycleSoakRecoverableTransactionRelativePath))
if ([string]::IsNullOrWhiteSpace($TransactionPath)) {
    $TransactionPath = $expectedTransactionPath
}
$TransactionPath = [System.IO.Path]::GetFullPath($TransactionPath)
if (-not $TransactionPath.Equals(
        $expectedTransactionPath,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Only the recorded policy 22 monitor-compatibility false failure is eligible for this completion.'
}

$transaction = Get-Content -LiteralPath $TransactionPath -Raw |
    ConvertFrom-Json
$expectedFailure =
    'The read-only ACL/public/PnP timeline did not show three healthy power cycles.'
if ([int]$transaction.schema_version -ne 1 -or
    [int]$transaction.transport_policy_version -ne 22 -or
    [string]$transaction.status -ne 'failed' -or
    [string]$transaction.error -ne $expectedFailure -or
    [string]$transaction.source_commit -notmatch '^[0-9a-fA-F]{40}$' -or
    [string]$transaction.driver_tree -ne
        $script:V1NormalStopApprovedDriverTree) {
    throw 'The selected transaction is not the recoverable policy 22 false failure.'
}

$dir = [System.IO.Path]::GetFullPath([string]$transaction.directory)
$statePath = Join-Path $dir 'state.json'
$sessionPath = Join-Path $dir 'session.json'
$agentLogPath = Join-Path $dir 'agent.log'
$endpointStateLogPath = Join-Path $dir 'endpoint-state.log'
$endpointStateErrorPath = Join-Path $dir 'endpoint-state-error.log'
$aclTimelinePath = Join-Path $dir 'acl-timeline.log'
$aclTimelineErrorPath = Join-Path $dir 'acl-timeline-error.log'
$originalResultPath = Join-Path $dir 'result.json'
foreach ($contract in @(
        @{ stored=[string]$transaction.state; expected=$statePath },
        @{ stored=[string]$transaction.session; expected=$sessionPath },
        @{ stored=[string]$transaction.endpoint_state_log
            expected=$endpointStateLogPath },
        @{ stored=[string]$transaction.acl_timeline
            expected=$aclTimelinePath },
        @{ stored=[string]$transaction.result; expected=$originalResultPath })) {
    if (-not ([System.IO.Path]::GetFullPath($contract.stored)).Equals(
            [System.IO.Path]::GetFullPath($contract.expected),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The transaction evidence paths no longer match the recorded session directory.'
    }
}
foreach ($path in @(
        $statePath, $sessionPath, $agentLogPath, $endpointStateLogPath,
        $endpointStateErrorPath, $aclTimelinePath,
        $aclTimelineErrorPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required lifecycle-soak evidence is missing: $path"
    }
}
if (Test-Path -LiteralPath $originalResultPath) {
    throw 'The original gate already wrote a result; evidence-only recovery is not applicable.'
}

$endpointError = (Get-Content -LiteralPath $endpointStateErrorPath -Raw).Trim()
$endpointText = Get-Content -LiteralPath $endpointStateLogPath -Raw
$aclError = Get-Content -LiteralPath $aclTimelineErrorPath -Raw
if ($endpointError -cne 'Invalid argument: --monitor-state' -or
    $endpointText -notmatch '(?m)^Usage:\r?$' -or
    $endpointText -notmatch
        '(?m)^  endpoint_volume_probe\.exe --monitor-state <seconds> \[--all\]\r?$' -or
    -not [string]::IsNullOrWhiteSpace($aclError)) {
    throw 'The monitor failure does not match the exact bounded policy 22 compatibility bug.'
}

if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $root 'artifacts\v1-normal-stop\candidate'
}
$fidelityPrerequisitePath = Join-Path $root `
    $script:V1NormalStopFidelityPrerequisiteRelativePath
$pnpPrerequisitePath = Join-Path $root `
    $script:V1NormalStopPnpPrerequisiteRelativePath
$candidate = Get-V1NormalStopCandidate `
    -CandidatePath $CandidatePath `
    -ExpectedFidelityPrerequisitePath $fidelityPrerequisitePath `
    -ExpectedPnpPrerequisitePath $pnpPrerequisitePath
$manifest = $candidate.manifest
$capabilities = @($manifest.capabilities | ForEach-Object { [string]$_ })
if ([string]$manifest.source_commit -ne
        [string]$transaction.source_commit -or
    [string]$manifest.driver_tree -ne
        [string]$transaction.driver_tree -or
    'three_generation_lifecycle_soak_evidence' -notin $capabilities) {
    throw 'The preserved candidate no longer matches the recorded lifecycle-soak transaction.'
}

$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$session = Get-Content -LiteralPath $sessionPath -Raw | ConvertFrom-Json
$agentText = Get-Content -LiteralPath $agentLogPath -Raw
$aclText = Get-Content -LiteralPath $aclTimelinePath -Raw
$generationStates = @()
$generationSessions = @()
for ($generation = 1; $generation -le 3; $generation++) {
    $generationStatePath = "$statePath.generation-$generation.json"
    $generationSessionPath =
        "$sessionPath.generation-$generation.attempt-1.json"
    if (-not (Test-Path -LiteralPath $generationStatePath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $generationSessionPath -PathType Leaf)) {
        throw "Generation $generation evidence is incomplete."
    }
    $generationStates += Get-Content -LiteralPath $generationStatePath -Raw |
        ConvertFrom-Json
    $generationSessions += Get-Content `
        -LiteralPath $generationSessionPath -Raw | ConvertFrom-Json
}
if ($agentText -notmatch
    '(?m)^V1 presence agent stopped: 3 connect, 3 disconnect, 3 transport OPEN, 3 child process, 20 endpoint update, 0 endpoint failure, 3 render start, 2 render stop, 0 render query failure, 3 engine ready, 3 clean engine stop, 3 transport OPEN executed, 0 capability discovery, 0 discovery session complete, 0 configuration session complete, 0 silence session complete, 0 PCM burst session complete\.\r?$') {
    throw 'The recorded presence-agent completion summary is missing or changed.'
}

$endpointTimelineObserved =
    Test-V1LifecycleSoakEndpointAclTimeline -Text $aclText
$aclTimelineObserved = Test-V1LifecycleSoakAclTimeline -Text $aclText
if (-not $endpointTimelineObserved -or -not $aclTimelineObserved) {
    throw 'The recovered ACL snapshots do not prove three healthy endpoint publication cycles.'
}

$pnpPrerequisite = Get-Content -LiteralPath $pnpPrerequisitePath -Raw |
    ConvertFrom-Json
$fidelityPrerequisite = Get-Content -LiteralPath $fidelityPrerequisitePath `
    -Raw | ConvertFrom-Json
$v9 = Get-Content -LiteralPath ([string]$fidelityPrerequisite.prerequisite) `
    -Raw | ConvertFrom-Json
$v8 = Get-Content -LiteralPath ([string]$v9.prerequisite) -Raw |
    ConvertFrom-Json
$v7 = Get-Content -LiteralPath ([string]$v8.prerequisite) -Raw |
    ConvertFrom-Json
$v6 = Get-Content -LiteralPath ([string]$v7.prerequisite) -Raw |
    ConvertFrom-Json
$v5 = Get-Content -LiteralPath ([string]$v6.prerequisite) -Raw |
    ConvertFrom-Json
$silence = Get-Content -LiteralPath ([string]$v5.prerequisite) -Raw |
    ConvertFrom-Json
$zeroPacket = Get-Content -LiteralPath ([string]$silence.prerequisite) -Raw |
    ConvertFrom-Json
$backupPath = [string]$zeroPacket.backup_path

$connectionProbe = Join-Path $candidate.root 'xm5_connection_probe.exe'
if ((Get-NativeLdacBluetoothRadioState -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne
        'ready' -or
    (Get-NativeLdacXm5BluetoothState -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne
        'disconnected') {
    throw 'Windows Bluetooth must be on and XM5 must remain powered off for completion.'
}
$baseline = Get-NativeLdacBaselineSnapshot -BackupPath $backupPath
$baselineAssessment = Get-V1NormalStopBaselineAssessment `
    -Baseline $baseline `
    -ExpectedTransportInf ([string]$pnpPrerequisite.selected_inf)
if (-not $baselineAssessment.healthy) {
    throw "The current delayed PnP baseline is unhealthy: $($baselineAssessment.failures -join '; ')."
}

$endpointProbe = Join-Path $candidate.root 'audio_endpoint_probe.exe'
$presence = @(& $endpointProbe --presence 2>&1); $presenceExit = $LASTEXITCODE
$link = @(& $endpointProbe --link-state 2>&1); $linkExit = $LASTEXITCODE
$stream = @(& $endpointProbe --info 2>&1); $streamExit = $LASTEXITCODE
$lease = @(& $endpointProbe --consumer-lease 2>&1); $leaseExit = $LASTEXITCODE
if ($presenceExit -ne 0 -or $linkExit -ne 0 -or $streamExit -ne 0 -or
    $leaseExit -ne 0 -or
    ($presence -join "`n") -notmatch '(?m)^Physical presence absent:' -or
    ($link -join "`n") -notmatch '(?m)^Link disconnected:' -or
    ($stream -join "`n") -notmatch '(?m)^Stream idle[:,]' -or
    ($lease -join "`n") -notmatch
        '(?m)^PCM consumer lease released: generation 0\.\r?$') {
    throw 'The Native endpoint is not fully absent, link-disconnected, render-idle, and lease-released.'
}

$passed = Test-V1LifecycleSoakEvidence `
    -FinalState $state -GenerationStates $generationStates `
    -GenerationSessions $generationSessions -FinalSession $session `
    -AgentExitCode 0 `
    -EndpointTimelineObserved $endpointTimelineObserved `
    -AclTimelineObserved $aclTimelineObserved `
    -FinalPublicDisconnectObserved $true -FinalPnpHealthy $true
if (-not $passed) {
    throw 'The recorded three-generation session does not satisfy the complete lifecycle-soak evidence contract.'
}

Write-Host 'The three-generation core evidence and current disconnected baseline are verified.'
Write-Host 'This completion writes only result/transaction JSON; it does not touch the driver, PnP, radio, endpoint settings, or audio path.'
$target = $TransactionPath
$action = 'Finalize the already completed lifecycle soak after recovering endpoint publication evidence from the read-only ACL snapshots'
if (-not $PSCmdlet.ShouldProcess($target, $action)) { return }

$generations = @()
$totalLeaseAcquires = 0
$totalLeaseReleases = 0
for ($index = 0; $index -lt 3; $index++) {
    $generationSession = $generationSessions[$index]
    $leaseAcquires = [int]$generationSession.consumer_lease_acquire_count
    $leaseReleases = [int]$generationSession.consumer_lease_release_count
    $totalLeaseAcquires += $leaseAcquires
    $totalLeaseReleases += $leaseReleases
    $generations += [ordered]@{
        acl_generation = $index + 1
        expected_outcome = if ($index -eq 1) {
            'physical-disconnect'
        } else {
            'graceful-stop'
        }
        media_duration_ms = [int]$generationSession.actual_duration_ms
        media_packets_written = [int]$generationSession.media_packets_written
        volume_change_count = [int64]$generationSession.volume_change_count
        ended_by_graceful_stop = $generationSession.ended_by_graceful_stop
        ended_by_peer_close = $generationSession.ended_by_peer_close
        consumer_lease_acquire_count = $leaseAcquires
        consumer_lease_release_count = $leaseReleases
        transport_open_executed = 1
    }
}
$resultPath = Join-Path $dir 'completed-result.json'
$result = [ordered]@{
    schema_version = 1
    transport_policy_version = 22
    lifecycle_soak_passed = $true
    finalized_after_monitor_compatibility_failure = $true
    monitor_failure = 'endpoint_volume_probe rejected --monitor-state 600 before the 600-second bound was supported'
    endpoint_timeline_recovered_from_acl_snapshots = $true
    source_commit = [string]$transaction.source_commit
    driver_tree = [string]$transaction.driver_tree
    prerequisite = [string]$transaction.prerequisite
    transaction = $TransactionPath
    acl_generations = 3
    sequence = @('graceful-stop', 'physical-disconnect', 'graceful-stop')
    generations = $generations
    endpoint_three_active_absent_cycles_observed = $true
    acl_three_connect_disconnect_cycles_observed = $true
    final_public_disconnect_observed = $true
    delayed_pnp_window_ms = 20000
    final_pnp_healthy = $true
    total_transport_open_attempts = [int]$state.transport_open_executed
    total_transport_retry_count = [int]$state.transport_retries_scheduled
    total_consumer_lease_acquires = $totalLeaseAcquires
    total_consumer_lease_releases = $totalLeaseReleases
    driver_installed_or_updated = $false
    rebooted = $false
    bluetooth_toggled = $false
    default_output_changed = $false
    link_state_written = $false
}
Write-LegacyJsonAtomic -Value $result -Path $resultPath
$transaction.result = $resultPath
$transaction.status = 'lifecycle-soak-verified'
$transaction.error = $null
$transaction | Add-Member -NotePropertyName completed_at `
    -NotePropertyValue (Get-Date).ToString('o') -Force
$transaction | Add-Member `
    -NotePropertyName finalized_after_monitor_compatibility_failure `
    -NotePropertyValue $true -Force
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath

Write-Host 'V1 three-generation lifecycle-soak evidence finalized successfully.'
Write-Host 'Three connect/disconnect generations, three single-attempt inbound OPENs, zero retry, and ConsumerLease 3/3 are verified.'
Write-Host 'No driver, PnP, Bluetooth radio, endpoint setting, or audio-path change occurred.'
Write-Host "Result: $resultPath"
