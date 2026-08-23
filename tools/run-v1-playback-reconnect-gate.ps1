# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1PlaybackReconnect,
    [ValidateRange(360,420)][int]$DurationSeconds = 420,
    [string]$CandidatePath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-playback-reconnect-common.ps1')

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The playback-reconnect gate requires PowerShell 7. Run it with pwsh.exe, not powershell.exe.'
}
Assert-LegacyAdministrator
if (-not $ConfirmV1PlaybackReconnect) {
    throw 'Refusing to authorize the playback-reconnect gate. Re-run with -ConfirmV1PlaybackReconnect.'
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
if ('two_generation_playback_reconnect_evidence' -notin $capabilities) {
    throw 'The normal-stop candidate does not contain the two-generation reconnect host.'
}
if ((& git.exe -C $root rev-parse HEAD).Trim() -ne
        [string]$manifest.source_commit -or
    @(& git.exe -C $root status --porcelain).Count -ne 0) {
    throw 'The playback-reconnect candidate must match clean Git HEAD.'
}

$prerequisitePath = Join-Path $root `
    $script:V1PlaybackReconnectPrerequisiteRelativePath
$prerequisite = Get-Content -LiteralPath $prerequisitePath -Raw |
    ConvertFrom-Json
$prerequisiteResultPath = [string]$prerequisite.result
$prerequisiteResult = Get-Content -LiteralPath $prerequisiteResultPath -Raw |
    ConvertFrom-Json
if (-not (Test-V1PlaybackReconnectPrerequisite `
        -Transaction $prerequisite -TransactionPath $prerequisitePath `
        -Result $prerequisiteResult -ResultPath $prerequisiteResultPath `
        -ExpectedDriverTree ([string]$manifest.driver_tree))) {
    throw 'The verified policy 20 playback-disconnect result is not a valid reconnect prerequisite.'
}

$pnpPrerequisite = Get-Content -LiteralPath $pnpPrerequisitePath -Raw |
    ConvertFrom-Json
$fidelityPrerequisite = Get-Content -LiteralPath $fidelityPrerequisitePath -Raw |
    ConvertFrom-Json
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
$baseline = Get-NativeLdacBaselineSnapshot `
    -BackupPath ([string]$zeroPacket.backup_path)
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

Write-Host 'V1 two-generation playback reconnect readiness preflight passed.'
Write-Host "Candidate source: $($manifest.source_commit)"
Write-Host 'Both generations reuse the verified transparent PCM/LDAC, dynamic-volume, peer-CLOSE, and stable-Render policy.'
Write-Host 'Each generation requires one inbound OPEN, zero retry, at least five seconds of media, and a balanced ConsumerLease.'
Write-Host 'This preflight was read-only; no install, PnP restart, radio toggle, or reboot is required.'
$target = 'two consecutive physical XM5 ACL generations'
$action = 'Play and power off generation 1, wait for public disconnect, reconnect, play and power off generation 2, then prove isolated cleanup'
if (-not $PSCmdlet.ShouldProcess($target, $action)) { return }

$trialRoot = Join-Path $root 'artifacts\v1-playback-reconnect\trial'
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
    transport_policy_version = $script:V1PlaybackReconnectPolicyVersion
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

Write-Host 'V1 two-generation playback reconnect agent armed.'
Write-Host 'Generation 1: turn on XM5, select Native LDAC, and play continuously. After media START, change Windows volume once.'
Write-Host "Keep playback running for at least 10 seconds, then power off XM5 without pausing or closing the player."
Write-Host "After 'V1 reconnect checkpoint reached', wait until Windows shows XM5 disconnected and Native LDAC disappears."
Write-Host 'Generation 2: turn on XM5 again, reselect Native LDAC if needed, resume continuous playback, and change Windows volume once.'
Write-Host "After the second media START, keep playing for at least 10 seconds, then power off XM5 without pausing or closing the player."
Write-Host "Wait for the second 'V1 ACL disconnected' and the final result before stopping the player."
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
        --await-playback-reconnect --render-start-timeout-ms 45000 `
        --transport-result $sessionPath `
        --engine-executable $worker 2>&1 | Tee-Object -FilePath $agentLogPath |
        ForEach-Object { Write-Host ([string]$_); $_ }) | Out-Null
    $exitCode = $LASTEXITCODE
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
for ($generation = 1; $generation -le 2; $generation++) {
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
$endpointReconnectObserved = $endpointText -match
    '(?ms)^\+\d+ms: .*Native LDAC Speaker Topology.* -> active\s*$.*?^\+\d+ms: .*Native LDAC Speaker Topology.* -> (?:unplugged|not-present|disabled)\s*$.*?^\+\d+ms: .*Native LDAC Speaker Topology.* -> active\s*$'
$intermediatePublicDisconnectObserved = $aclText -match
    '(?ms)^\+\d+ms ACL disconnected\.\s*$.*?^\+\d+ms snapshot\([^)]*\): fConnected=disconnected,.*?^\+\d+ms ACL connected\.\s*$'
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
$passed = Test-V1PlaybackReconnectEvidence `
    -FinalState $state -GenerationStates $generationStates `
    -GenerationSessions $generationSessions -FinalSession $session `
    -AgentExitCode $exitCode `
    -EndpointReconnectObserved $endpointReconnectObserved `
    -IntermediatePublicDisconnectObserved `
        $intermediatePublicDisconnectObserved `
    -FinalPublicDisconnectObserved $finalPublicDisconnectObserved
if (-not $passed) {
    $failure = 'Two-generation reconnect lifecycle evidence failed.'
    if (@($generationStates).Count -lt 2) {
        $failure = 'Two physical ACL disconnect generation snapshots were not completed.'
    } elseif (@($generationSessions).Count -lt 2) {
        $failure = 'Both generations did not complete one archived PCM/AVDTP session.'
    } elseif (-not $intermediatePublicDisconnectObserved) {
        $failure = 'Windows public XM5 state did not converge to disconnected between generations.'
    } elseif (-not $endpointReconnectObserved) {
        $failure = 'Native LDAC did not show active, absent, then active endpoint publication.'
    } elseif (-not $finalPublicDisconnectObserved) {
        $failure = 'Windows public XM5 state is not disconnected after generation 2.'
    } elseif ($null -ne $state -and
        [int]$state.transport_retries_scheduled -ne 0) {
        $failure = 'At least one generation required a transport retry.'
    } elseif ($null -ne $state -and
        [int]$state.render_start_timed_out -ne 0) {
        $failure = 'A generation did not publish Render START within 45 seconds.'
    }
    $transaction.status = 'failed'
    $transaction.error = $failure
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    throw "V1 playback-reconnect gate failed: $failure Keep XM5 off, stop the player, and do not retry. Transaction: $transactionPath"
}

$generations = @()
for ($index = 0; $index -lt 2; $index++) {
    $generationState = $generationStates[$index]
    $generationSession = $generationSessions[$index]
    $generations += [ordered]@{
        acl_generation = $index + 1
        media_duration_ms = [int]$generationSession.actual_duration_ms
        media_packets_written = [int]$generationSession.media_packets_written
        volume_change_count = [int64]$generationSession.volume_change_count
        ended_by_peer_close = $generationSession.ended_by_peer_close
        peer_close_commands_accepted =
            [int]$generationSession.peer_close_commands_accepted
        consumer_lease_acquire_count =
            [int]$generationSession.consumer_lease_acquire_count
        consumer_lease_release_count =
            [int]$generationSession.consumer_lease_release_count
        transport_open_executed = if ($index -eq 0) {
            [int]$generationState.transport_open_executed
        } else {
            [int]$generationState.transport_open_executed -
                [int]$generationStates[0].transport_open_executed
        }
    }
}
$result = [ordered]@{
    schema_version = 1
    transport_policy_version = $script:V1PlaybackReconnectPolicyVersion
    playback_reconnect_passed = $true
    source_commit = $manifest.source_commit
    driver_tree = $manifest.driver_tree
    prerequisite = $prerequisitePath
    transaction = $transactionPath
    acl_generations = 2
    generations = $generations
    intermediate_public_disconnect_observed = $true
    endpoint_active_absent_active_observed = $true
    final_public_disconnect_observed = $true
    final_public_disconnect_elapsed_ms =
        [long]$finalPublicDisconnectTimer.ElapsedMilliseconds
    total_transport_open_attempts = [int]$state.transport_open_executed
    total_transport_retry_count = [int]$state.transport_retries_scheduled
    total_consumer_lease_acquires = 2
    total_consumer_lease_releases = 2
    driver_installed_or_updated = $false
    rebooted = $false
    bluetooth_toggled = $false
    default_output_changed = $false
    link_state_written = $false
}
Write-LegacyJsonAtomic -Value $result -Path $resultPath
$transaction.status = 'playback-reconnect-verified'
$transaction.error = $null
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
Write-Host 'V1 two-generation playback reconnect gate passed.'
Write-Host 'Both ACL generations used one stable inbound transport OPEN with zero retry.'
Write-Host 'Native LDAC became active, absent, and active again; public state disconnected between generations.'
Write-Host 'Both ConsumerLeases and all local transport resources were released.'
Write-Host 'Stop the player normally and keep XM5 off. No reboot or rollback is required.'
Write-Host "Result: $resultPath"
