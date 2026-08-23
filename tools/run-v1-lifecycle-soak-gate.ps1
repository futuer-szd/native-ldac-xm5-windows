# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1LifecycleSoak,
    [ValidateRange(540,600)][int]$DurationSeconds = 600,
    [string]$CandidatePath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-lifecycle-soak-common.ps1')

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The lifecycle-soak gate requires PowerShell 7. Run it with pwsh.exe, not powershell.exe.'
}
Assert-LegacyAdministrator
if (-not $ConfirmV1LifecycleSoak) {
    throw 'Refusing to authorize the lifecycle-soak gate. Re-run with -ConfirmV1LifecycleSoak.'
}
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
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
if ('three_generation_lifecycle_soak_evidence' -notin $capabilities) {
    throw 'The normal-stop candidate does not contain the three-generation lifecycle-soak host.'
}
if ((& git.exe -C $root rev-parse HEAD).Trim() -ne
        [string]$manifest.source_commit -or
    @(& git.exe -C $root status --porcelain).Count -ne 0) {
    throw 'The lifecycle-soak candidate must match clean Git HEAD.'
}

$prerequisitePath = Join-Path $root `
    $script:V1LifecycleSoakPrerequisiteRelativePath
$prerequisite = Get-Content -LiteralPath $prerequisitePath -Raw |
    ConvertFrom-Json
$prerequisiteResultPath = [string]$prerequisite.result
$prerequisiteResult = Get-Content -LiteralPath $prerequisiteResultPath -Raw |
    ConvertFrom-Json
if (-not (Test-V1LifecycleSoakPrerequisite `
        -Transaction $prerequisite -TransactionPath $prerequisitePath `
        -Result $prerequisiteResult -ResultPath $prerequisiteResultPath `
        -ExpectedDriverTree ([string]$manifest.driver_tree))) {
    throw 'The verified policy 21 playback-reconnect result is not a valid soak prerequisite.'
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
$baseline = Get-NativeLdacBaselineSnapshot -BackupPath $backupPath
$baselineAssessment = Get-V1NormalStopBaselineAssessment `
    -Baseline $baseline `
    -ExpectedTransportInf ([string]$pnpPrerequisite.selected_inf)
if (-not $baselineAssessment.healthy) {
    throw "The persistent LdacNative plus V1 endpoint baseline is not healthy: $($baselineAssessment.failures -join '; ')."
}

$transportProbe = Join-Path $candidate.root 'transport_probe.exe'
$transportInfo = @(& $transportProbe --info 2>&1)
if ($LASTEXITCODE -ne 0 -or ($transportInfo -join "`n") -notmatch
        '(?m)^Ready flags: 0x0000000F\s*\r?$') {
    throw 'The installed LdacNative driver is not the inbound-ready ABI 0.5 build.'
}
$connectionProbe = Join-Path $candidate.root 'xm5_connection_probe.exe'
if ((Get-NativeLdacBluetoothRadioState -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne
        'ready' -or
    (Get-NativeLdacXm5BluetoothState -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne
        'disconnected') {
    throw 'Windows Bluetooth must be on and XM5 must be powered off before this gate.'
}
$endpointProbe = Join-Path $candidate.root 'audio_endpoint_probe.exe'
$endpointStateProbe = Join-Path $candidate.root 'endpoint_volume_probe.exe'
$monitorCheck = @(& $endpointStateProbe --monitor-state 1 2>&1)
if ($LASTEXITCODE -ne 0 -or ($monitorCheck -join "`n") -notmatch
        '(?m)^Monitoring read-only endpoint state changes for 1 seconds\.') {
    throw 'The candidate endpoint monitor is stale or does not support --monitor-state.'
}
$presence = @(& $endpointProbe --presence 2>&1); $presenceExit = $LASTEXITCODE
$link = @(& $endpointProbe --link-state 2>&1); $linkExit = $LASTEXITCODE
$info = @(& $endpointProbe --info 2>&1); $infoExit = $LASTEXITCODE
$lease = @(& $endpointProbe --consumer-lease 2>&1); $leaseExit = $LASTEXITCODE
$endpointPreflightFailures = @()
foreach ($check in @(
        [pscustomobject]@{ name='presence'; exit=$presenceExit
            output=($presence -join "`n")
            pattern='(?m)^Physical presence absent:' },
        [pscustomobject]@{ name='link-state'; exit=$linkExit
            output=($link -join "`n")
            pattern='(?m)^Link disconnected:' },
        [pscustomobject]@{ name='stream'; exit=$infoExit
            output=($info -join "`n")
            pattern='(?m)^Stream idle[:,]' },
        [pscustomobject]@{ name='consumer-lease'; exit=$leaseExit
            output=($lease -join "`n")
            pattern='(?m)^PCM consumer lease released: generation 0\.$' })) {
    if ([int]$check.exit -ne 0 -or
        [string]$check.output -notmatch [string]$check.pattern) {
        $text = ([string]$check.output).Trim() -replace '\s+', ' '
        if ([string]::IsNullOrWhiteSpace($text)) { $text = '(no output)' }
        $endpointPreflightFailures +=
            "$($check.name) exit $($check.exit): $text"
    }
}
if ($endpointPreflightFailures.Count -ne 0) {
    throw "Native endpoint preflight failed: $($endpointPreflightFailures -join '; ')"
}

Write-Host 'V1 three-generation lifecycle-soak readiness preflight passed.'
Write-Host "Candidate source: $($manifest.source_commit)"
Write-Host 'The sequence is fixed: normal STOP, in-playback physical disconnect, normal STOP.'
Write-Host 'All three generations reuse the frozen transparent PCM/LDAC and dynamic-volume policy.'
Write-Host 'This preflight was read-only; no install, PnP restart, radio toggle, or reboot is required.'
$target = 'three consecutive physical XM5 ACL generations'
$action = 'Alternate normal STOP, playback disconnect, and normal STOP; require one inbound OPEN per generation and a healthy delayed PnP window'
if (-not $PSCmdlet.ShouldProcess($target, $action)) { return }

$trialRoot = Join-Path $root 'artifacts\v1-lifecycle-soak\trial'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$dir = Join-Path $trialRoot "session-$stamp"
New-Item -ItemType Directory -Path $dir -Force | Out-Null
$statePath = Join-Path $dir 'state.json'
$sessionPath = Join-Path $dir 'session.json'
$agentLogPath = Join-Path $dir 'agent.log'
$endpointStateLogPath = Join-Path $dir 'endpoint-state.log'
$endpointStateErrorPath = Join-Path $dir 'endpoint-state-error.log'
$aclTimelinePath = Join-Path $dir 'acl-timeline.log'
$aclTimelineErrorPath = Join-Path $dir 'acl-timeline-error.log'
$resultPath = Join-Path $dir 'result.json'
$transactionPath = Join-Path $trialRoot "transaction-$stamp.json"
$transaction = [ordered]@{
    schema_version = 1
    transport_policy_version = $script:V1LifecycleSoakPolicyVersion
    source_commit = $manifest.source_commit
    driver_tree = $manifest.driver_tree
    prerequisite = $prerequisitePath
    created_at = (Get-Date).ToString('o')
    status = 'running'
    directory = $dir
    state = $statePath
    session = $sessionPath
    endpoint_state_log = $endpointStateLogPath
    acl_timeline = $aclTimelinePath
    result = $resultPath
    error = $null
}
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath

Write-Host 'V1 three-generation lifecycle-soak agent armed.'
Write-Host 'Generation 1: turn on XM5, select Native LDAC, and play continuously.'
Write-Host "After media START, play for at least 10 seconds, completely exit the player, and wait for 'V1 contained engine stopped cleanly'."
Write-Host 'Then power off XM5 and wait for the reconnect checkpoint.'
Write-Host 'Generation 2: turn on XM5, select Native LDAC, play continuously, and change Windows volume once.'
Write-Host 'After media START, play for at least 10 seconds, then power off XM5 without pausing or closing the player.'
Write-Host 'Wait for the reconnect checkpoint and for Native LDAC to disappear.'
Write-Host 'Generation 3: turn on XM5, select Native LDAC, and play continuously.'
Write-Host "After media START, play for at least 10 seconds, completely exit the player, and wait for 'V1 contained engine stopped cleanly'."
Write-Host 'Then power off XM5. Keep the player closed and wait for the final 20-second PnP observation.'
Write-Host 'Do not toggle Windows Bluetooth, restart PnP, change format, or select another output during this gate.'

$agent = Join-Path $candidate.root 'v1_presence_agent.exe'
$worker = Join-Path $candidate.root 'v1_transport_normal_stop_worker.exe'
$endpointStateProcess = $null
$aclTimelineProcess = $null
$exitCode = -1
$saved = $ErrorActionPreference
try {
    $endpointStateProcess = Start-Process -FilePath $endpointStateProbe `
        -ArgumentList @('--monitor-state', [string]$DurationSeconds) `
        -NoNewWindow -PassThru `
        -RedirectStandardOutput $endpointStateLogPath `
        -RedirectStandardError $endpointStateErrorPath
    $aclTimelineProcess = Start-Process -FilePath $connectionProbe `
        -ArgumentList @('--observe-acl', [string]$DurationSeconds) `
        -NoNewWindow -PassThru `
        -RedirectStandardOutput $aclTimelinePath `
        -RedirectStandardError $aclTimelineErrorPath
    $ErrorActionPreference = 'Continue'
    @(& $agent --run-for-ms ($DurationSeconds * 1000) --state $statePath `
        --endpoint-presence --observe-render-demand --observe-engine-ready `
        --exercise-transport-pcm-burst --pcm-fast-signaling-acquisition `
        --transport-open-render-stability-ms `
            $script:V1NormalStopTransportOpenRenderStabilityMs `
        --await-playback-reconnect --playback-reconnect-generations 3 `
        --render-start-timeout-ms 60000 `
        --transport-result $sessionPath `
        --engine-executable $worker 2>&1 | Tee-Object -FilePath $agentLogPath |
        ForEach-Object { Write-Host ([string]$_); $_ }) | Out-Null
    $exitCode = $LASTEXITCODE
    if ($exitCode -eq 0) {
        Write-Host 'The three ACL generations ended; observing the fixed 20-second delayed PnP failure window.'
        Start-Sleep -Seconds 20
    }
} finally {
    $ErrorActionPreference = $saved
    Start-Sleep -Milliseconds 1500
    foreach ($process in @($endpointStateProcess, $aclTimelineProcess)) {
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
            $process.WaitForExit()
        }
    }
}

$state = if (Test-Path -LiteralPath $statePath) {
    Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
} else { $null }
$session = if (Test-Path -LiteralPath $sessionPath) {
    Get-Content -LiteralPath $sessionPath -Raw | ConvertFrom-Json
} else { $null }
$generationStates = @()
$generationSessions = @()
for ($generation = 1; $generation -le 3; $generation++) {
    $generationStatePath = "$statePath.generation-$generation.json"
    $generationSessionPath =
        "$sessionPath.generation-$generation.attempt-1.json"
    if (Test-Path -LiteralPath $generationStatePath) {
        $generationStates += Get-Content -LiteralPath $generationStatePath `
            -Raw | ConvertFrom-Json
    }
    if (Test-Path -LiteralPath $generationSessionPath) {
        $generationSessions += Get-Content `
            -LiteralPath $generationSessionPath -Raw | ConvertFrom-Json
    }
}
$endpointText = if (Test-Path -LiteralPath $endpointStateLogPath) {
    Get-Content -LiteralPath $endpointStateLogPath -Raw
} else { '' }
$aclText = if (Test-Path -LiteralPath $aclTimelinePath) {
    Get-Content -LiteralPath $aclTimelinePath -Raw
} else { '' }
$endpointTimelineObserved =
    Test-V1LifecycleSoakEndpointTimeline -Text $endpointText
$aclTimelineObserved = Test-V1LifecycleSoakAclTimeline -Text $aclText
$finalPublicDisconnectObserved = $false
$finalPublicDisconnectTimer = [System.Diagnostics.Stopwatch]::StartNew()
do {
    try {
        $finalPublicDisconnectObserved =
            (Get-NativeLdacXm5BluetoothState -ProbePath $connectionProbe `
                -ExpectedSourceCommit ([string]$manifest.source_commit)) -eq
            'disconnected'
    } catch {
        $finalPublicDisconnectObserved = $false
    }
    if ($finalPublicDisconnectObserved -or
        $finalPublicDisconnectTimer.ElapsedMilliseconds -ge 30000) {
        break
    }
    Start-Sleep -Milliseconds 250
} while ($true)
$finalPublicDisconnectTimer.Stop()
$finalBaseline = Get-NativeLdacBaselineSnapshot -BackupPath $backupPath
$finalBaselineAssessment = Get-V1NormalStopBaselineAssessment `
    -Baseline $finalBaseline `
    -ExpectedTransportInf ([string]$pnpPrerequisite.selected_inf)
$finalPnpHealthy = [bool]$finalBaselineAssessment.healthy
$passed = Test-V1LifecycleSoakEvidence `
    -FinalState $state -GenerationStates $generationStates `
    -GenerationSessions $generationSessions -FinalSession $session `
    -AgentExitCode $exitCode `
    -EndpointTimelineObserved $endpointTimelineObserved `
    -AclTimelineObserved $aclTimelineObserved `
    -FinalPublicDisconnectObserved $finalPublicDisconnectObserved `
    -FinalPnpHealthy $finalPnpHealthy
if (-not $passed) {
    $failure = 'Three-generation lifecycle-soak evidence failed.'
    if (@($generationStates).Count -lt 3) {
        $failure = 'Three physical ACL disconnect generation snapshots were not completed.'
    } elseif (@($generationSessions).Count -lt 3) {
        $failure = 'All three generations did not archive exactly one PCM/AVDTP session.'
    } elseif (-not $aclTimelineObserved) {
        $failure = 'The read-only ACL/public/PnP timeline did not show three healthy power cycles.'
    } elseif (-not $endpointTimelineObserved) {
        $failure = 'Native LDAC did not complete three active/absent publication cycles.'
    } elseif (-not $finalPublicDisconnectObserved) {
        $failure = 'Windows public XM5 state is not disconnected after generation 3.'
    } elseif (-not $finalPnpHealthy) {
        $failure = "The final delayed PnP baseline is unhealthy: $($finalBaselineAssessment.failures -join '; ')."
    } elseif ($null -ne $state -and
        [int]$state.transport_retries_scheduled -ne 0) {
        $failure = 'At least one generation required a transport retry.'
    } elseif ($null -ne $state -and $state.render_start_timed_out -eq $true) {
        $failure = 'A generation did not publish Render START within 60 seconds.'
    }
    $transaction.status = 'failed'
    $transaction.error = $failure
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    throw "V1 lifecycle-soak gate failed: $failure Keep XM5 off, keep the player closed, and do not retry. Transaction: $transactionPath"
}

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
$result = [ordered]@{
    schema_version = 1
    transport_policy_version = $script:V1LifecycleSoakPolicyVersion
    lifecycle_soak_passed = $true
    source_commit = $manifest.source_commit
    driver_tree = $manifest.driver_tree
    prerequisite = $prerequisitePath
    transaction = $transactionPath
    acl_generations = 3
    sequence = @('graceful-stop', 'physical-disconnect', 'graceful-stop')
    generations = $generations
    endpoint_three_active_absent_cycles_observed = $true
    acl_three_connect_disconnect_cycles_observed = $true
    final_public_disconnect_observed = $true
    final_public_disconnect_elapsed_ms =
        [long]$finalPublicDisconnectTimer.ElapsedMilliseconds
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
$transaction.status = 'lifecycle-soak-verified'
$transaction.error = $null
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
Write-Host 'V1 three-generation lifecycle-soak gate passed.'
Write-Host 'The normal STOP, playback-disconnect, and normal STOP sequence completed with one inbound OPEN per generation and zero retry.'
Write-Host 'All ConsumerLeases and local transport resources were released; the delayed PnP baseline remained healthy.'
Write-Host 'Keep XM5 off. No reboot or rollback is required.'
Write-Host "Result: $resultPath"
