# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1PlaybackDisconnect,
    [ValidateRange(300,420)][int]$DurationSeconds = 360,
    [string]$CandidatePath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-playback-disconnect-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1PlaybackDisconnect) {
    throw 'Refusing to authorize the playback-disconnect gate. Re-run with -ConfirmV1PlaybackDisconnect.'
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
if ((& git.exe -C $root rev-parse HEAD).Trim() -ne
        [string]$manifest.source_commit -or
    @(& git.exe -C $root status --porcelain).Count -ne 0) {
    throw 'The playback-disconnect candidate must match clean Git HEAD.'
}

$prerequisitePath = Join-Path $root `
    $script:V1PlaybackDisconnectPrerequisiteRelativePath
$prerequisite = Get-Content -LiteralPath $prerequisitePath -Raw |
    ConvertFrom-Json
$prerequisiteResultPath = [string]$prerequisite.result
$prerequisiteResult = Get-Content -LiteralPath $prerequisiteResultPath -Raw |
    ConvertFrom-Json
if (-not (Test-V1PlaybackDisconnectPrerequisite `
        -Transaction $prerequisite -TransactionPath $prerequisitePath `
        -Result $prerequisiteResult -ResultPath $prerequisiteResultPath `
        -ExpectedDriverTree ([string]$manifest.driver_tree))) {
    throw 'Policy 16 normal Render STOP is not a valid playback-disconnect prerequisite.'
}

$pnpPrerequisite = Get-Content -LiteralPath $pnpPrerequisitePath -Raw |
    ConvertFrom-Json
$fidelityPrerequisite = Get-Content `
    -LiteralPath $fidelityPrerequisitePath -Raw |
    ConvertFrom-Json
$v9Path = [string]$fidelityPrerequisite.prerequisite
$v9 = Get-Content -LiteralPath $v9Path -Raw | ConvertFrom-Json
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
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne 'ready' -or
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
if ($presenceExit -ne 0 -or $linkExit -ne 0 -or $infoExit -ne 0 -or
    $leaseExit -ne 0 -or
    ($presence -join "`n") -notmatch '(?m)^Physical presence absent:' -or
    ($link -join "`n") -notmatch '(?m)^Link disconnected:' -or
    ($info -join "`n") -notmatch '(?m)^Stream idle[:,]' -or
    ($lease -join "`n") -notmatch
        '(?m)^PCM consumer lease released: generation 0\.$') {
    throw 'The Native endpoint must be absent/idle with LinkState disconnected and ConsumerLease released.'
}

Write-Host 'V1 in-playback physical-disconnect readiness preflight passed.'
Write-Host "Candidate source: $($manifest.source_commit)"
Write-Host 'Successful evidence requires one inbound signaling OPEN, zero retry, at least five seconds of media, and one bounded physical-disconnect cleanup path.'
Write-Host "Transport OPEN waits for $($script:V1NormalStopTransportOpenRenderStabilityMs) ms of continuous Render RUN in one epoch, so short system sounds cannot consume the session."
Write-Host 'No AVDTP SUSPEND or CLOSE may be sent after physical ACL loss.'
Write-Host 'This preflight was read-only; no install or reboot is required.'
$target = 'one generation-bound XM5 in-playback physical disconnect'
$action = 'Start transparent LDAC, keep the player running, power off XM5 after at least ten seconds, and prove peer-CLOSE handoff or ACL-loss local cancellation'
if (-not $PSCmdlet.ShouldProcess($target, $action)) { return }

$trialRoot = Join-Path $root 'artifacts\v1-playback-disconnect\trial'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$dir = Join-Path $trialRoot "session-$stamp"
New-Item -ItemType Directory -Path $dir -Force | Out-Null
$statePath = Join-Path $dir 'state.json'
$sessionPath = Join-Path $dir 'session.json'
$logPath = Join-Path $dir 'agent.log'
$endpointStateLogPath = Join-Path $dir 'endpoint-state.log'
$endpointStateErrorPath = Join-Path $dir 'endpoint-state-error.log'
$resultPath = Join-Path $dir 'result.json'
$transactionPath = Join-Path $trialRoot "transaction-$stamp.json"
$transaction = [ordered]@{
    schema_version = 1
    transport_policy_version = $script:V1PlaybackDisconnectPolicyVersion
    source_commit = $manifest.source_commit
    driver_tree = $manifest.driver_tree
    prerequisite = $prerequisitePath
    created_at = (Get-Date).ToString('o')
    status = 'running'
    directory = $dir; state = $statePath; session = $sessionPath
    endpoint_state_log = $endpointStateLogPath
    result = $resultPath; error = $null
}
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
Write-Host 'V1 in-playback physical-disconnect agent armed.'
Write-Host 'You may leave NetEase Cloud Music open before turning on XM5; do not start playback until the Native LDAC endpoint is selectable.'
Write-Host 'Turn on XM5, select Native LDAC, and continuously play clear content at a comfortable volume.'
Write-Host 'After media START, change Windows volume once and confirm playback continues; this is required policy-v20 evidence.'
Write-Host "After 'V1 bounded PCM media started', keep playback running for at least 10 seconds, then power off XM5 without pausing or closing the player."
Write-Host "Wait for 'V1 ACL disconnected' and the final result before stopping the player."
Write-Host 'Do not change output device, format, or Windows Bluetooth after media START.'
$agent = Join-Path $candidate.root 'v1_presence_agent.exe'
$worker = Join-Path $candidate.root 'v1_transport_normal_stop_worker.exe'
$saved = $ErrorActionPreference
$exitCode = -1
$endpointStateProcess = $null
try {
    $endpointStateProcess = Start-Process -FilePath $endpointStateProbe `
        -ArgumentList @('--monitor-state', [string]$DurationSeconds) `
        -NoNewWindow -PassThru `
        -RedirectStandardOutput $endpointStateLogPath `
        -RedirectStandardError $endpointStateErrorPath
    $ErrorActionPreference = 'Continue'
    @(& $agent --run-for-ms ($DurationSeconds * 1000) --state $statePath `
        --endpoint-presence --observe-render-demand --observe-engine-ready `
        --exercise-transport-pcm-burst --pcm-fast-signaling-acquisition `
        --transport-open-render-stability-ms `
            $script:V1NormalStopTransportOpenRenderStabilityMs `
        --await-playback-disconnect --render-start-timeout-ms 45000 `
        --transport-result $sessionPath `
        --engine-executable $worker 2>&1 | Tee-Object -FilePath $logPath |
        ForEach-Object { Write-Host ([string]$_); $_ }) | Out-Null
    $exitCode = $LASTEXITCODE
} finally {
    if ($null -ne $endpointStateProcess -and
        -not $endpointStateProcess.HasExited) {
        Stop-Process -Id $endpointStateProcess.Id -Force
        $endpointStateProcess.WaitForExit()
    }
    $ErrorActionPreference = $saved
}
$state = if (Test-Path -LiteralPath $statePath) {
    Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
} else { $null }
$session = if (Test-Path -LiteralPath $sessionPath) {
    Get-Content -LiteralPath $sessionPath -Raw | ConvertFrom-Json
} else { $null }
$attemptFiles = @(Get-ChildItem -LiteralPath $dir `
    -Filter 'session.json.attempt-*.json' -File | Sort-Object Name)
$attempts = @($attemptFiles | ForEach-Object {
    Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
})
$endpointStateText = if (Test-Path -LiteralPath $endpointStateLogPath) {
    Get-Content -LiteralPath $endpointStateLogPath -Raw
} else { '' }
$endpointActiveObserved = $endpointStateText -match
    '(?m)^\+\d+ms: .*Native LDAC.* -> active(?:\s|$)'
$passed = Test-V1PlaybackDisconnectEvidence -State $state `
    -Session $session -Attempts $attempts -AgentExitCode $exitCode
$passed = $passed -and $endpointActiveObserved
if (-not $passed) {
    $failure = 'In-playback physical-disconnect lifecycle evidence failed.'
    $signalingHeaders = ''
    $sessionProperties = if ($null -ne $session) {
        @($session.PSObject.Properties.Name)
    } else { @() }
    if ($null -ne $session -and
        $sessionProperties -contains
            'last_signaling_tx_header_available' -and
        $session.last_signaling_tx_header_available -eq $true) {
        $signalingHeaders = " TX(label=$([int]$session.last_signaling_tx_transaction_label),type=$([int]$session.last_signaling_tx_message_type),signal=$([int]$session.last_signaling_tx_signal_id));"
        if ($sessionProperties -contains
                'last_signaling_rx_header_available' -and
            $session.last_signaling_rx_header_available -eq $true) {
            $signalingHeaders += " RX(label=$([int]$session.last_signaling_rx_transaction_label),type=$([int]$session.last_signaling_rx_message_type),signal=$([int]$session.last_signaling_rx_signal_id),bytes=$([int]$session.last_signaling_response_size))."
        } else {
            $signalingHeaders += ' RX(header unavailable).'
        }
    }
    if ($sessionProperties -contains
            'peer_signaling_commands_received' -and
        $sessionProperties -contains
            'peer_discover_commands_accepted' -and
        $sessionProperties -contains
            'peer_capability_commands_accepted' -and
        $sessionProperties -contains
            'peer_configuration_commands_rejected' -and
        $sessionProperties -contains
            'peer_close_commands_accepted') {
        $signalingHeaders += " Peer commands(received=$([int]$session.peer_signaling_commands_received),discover=$([int]$session.peer_discover_commands_accepted),capabilities=$([int]$session.peer_capability_commands_accepted),configuration_rejected=$([int]$session.peer_configuration_commands_rejected),close=$([int]$session.peer_close_commands_accepted))."
    }
    if ($null -ne $session -and
        [string]$session.disposition -ne 'succeeded' -and
        $session.media_started_notified -ne $true) {
        $failure = "The PCM/AVDTP session failed before media START: disposition=$($session.disposition), stage=$([int]$session.stage), protocol_error=$([int]$session.protocol_error), media_packets=$([int]$session.media_packets_written).$signalingHeaders"
    } elseif ($null -ne $session -and
        [string]$session.disposition -notin @('succeeded','cancelled')) {
        $failure = "The PCM/AVDTP session failed after media START: disposition=$($session.disposition), stage=$([int]$session.stage), protocol_error=$([int]$session.protocol_error), media_packets=$([int]$session.media_packets_written).$signalingHeaders"
    } elseif ($null -ne $session -and $session.completed_full_duration -eq $true) {
        $failure = 'XM5 was not powered off before the sixty-second media hard bound.'
    } elseif (-not $endpointActiveObserved) {
        $failure = "Windows did not publish an active Native LDAC render endpoint within the bounded startup window; inspect $endpointStateLogPath."
    } elseif ($null -ne $state -and
        $state.render_start_timed_out -eq $true) {
        $failure = 'The player did not produce Render START within 45 seconds of physical ACL connect.'
    } elseif ($null -ne $state -and
        [int]$state.render_stop_timeout_events -ne 0) {
        $failure = 'Render STOP persisted past the bounded transition window before physical ACL disconnect.'
    } elseif ($null -ne $session -and [int]$session.actual_duration_ms -lt
            $script:V1PlaybackDisconnectMinimumMediaDurationMs) {
        $failure = 'XM5 was powered off less than five seconds after media START.'
    } elseif ($null -ne $state -and [int]$state.disconnected_events -ne 1) {
        $failure = 'One physical ACL disconnect was not observed during playback.'
    } elseif ($null -ne $state -and
        [int]$state.transport_retries_scheduled -ne 0) {
        $failure = 'The transport required a retry; this gate requires one inbound OPEN.'
    } elseif ($null -ne $session -and
        ($session.avdtp_suspend_accepted -eq $true -or
         $session.avdtp_close_accepted -eq $true)) {
        $failure = 'Remote SUSPEND/CLOSE was used after physical disconnect instead of local cancellation.'
    }
    $transaction.status = 'failed'; $transaction.error = $failure
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    throw "V1 playback-disconnect gate failed: $failure Stop the player, keep XM5 off, and do not retry. Transaction: $transactionPath"
}

$result = [ordered]@{
    schema_version = 1
    transport_policy_version = $script:V1PlaybackDisconnectPolicyVersion
    playback_disconnect_passed = $true
    source_commit = $manifest.source_commit; driver_tree = $manifest.driver_tree
    prerequisite = $prerequisitePath; transaction = $transactionPath
    session_generation = [int64]$session.session_generation
    acl_generation = [int64]$state.acl_generation
    media_duration_ms = [int]$session.actual_duration_ms
    media_packets_written = [int]$session.media_packets_written
    pcm_frames_sent = [int64]$session.pcm_frames_sent
    transport_open_attempts = [int]$state.transport_open_executed
    transport_open_render_stability_ms =
        [int]$state.transport_open_render_stability_ms
    transport_open_stability_resets =
        [int]$state.transport_open_stability_resets
    transport_open_stable_authorizations =
        [int]$state.transport_open_stable_authorizations
    transport_retry_count = [int]$state.transport_retries_scheduled
    peer_signaling_commands_received =
        [int]$session.peer_signaling_commands_received
    peer_discover_commands_accepted =
        [int]$session.peer_discover_commands_accepted
    peer_capability_commands_accepted =
        [int]$session.peer_capability_commands_accepted
    peer_configuration_commands_rejected =
        [int]$session.peer_configuration_commands_rejected
    peer_close_commands_accepted =
        [int]$session.peer_close_commands_accepted
    peer_signaling_read_timeouts =
        [int]$session.peer_signaling_read_timeouts
    pre_media_render_stop_events =
        [int]$state.pre_media_render_stop_events
    pre_start_pcm_frames_discarded =
        [int64]$session.pre_start_pcm_frames_discarded
    ended_by_peer_close = $session.ended_by_peer_close
    signaling_direction = 'inbound'
    render_stopped_events = [int]$state.render_stopped_events
    render_stop_resumed_events = [int]$state.render_stop_resumed_events
    volume_change_count = [int64]$session.volume_change_count
    endpoint_active_observed = $true
    endpoint_state_log = $endpointStateLogPath
    physical_acl_disconnected = $true
    cancel_actions = [int]$state.transport_cancel_actions
    graceful_stop_actions = [int]$state.transport_graceful_stop_actions
    suspend_accepted = $session.avdtp_suspend_accepted
    close_accepted = $session.avdtp_close_accepted
    local_signaling_close_succeeded = $session.close_succeeded
    consumer_lease_released = $session.consumer_lease_released
    lifecycle_outcome = 'physical-disconnect-local-cancel'
    driver_installed_or_updated = $false; rebooted = $false
    bluetooth_toggled = $false; default_output_changed = $false
    link_state_written = $false
}
Write-LegacyJsonAtomic -Value $result -Path $resultPath
$transaction.status = 'playback-disconnect-verified'; $transaction.error = $null
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
Write-Host 'V1 in-playback physical-disconnect gate passed.'
Write-Host "Transport OPEN followed $($state.transport_open_render_stability_ms) ms of stable Render RUN; startup stability resets: $($state.transport_open_stability_resets)."
Write-Host "Media ran for $($session.actual_duration_ms) ms before physical ACL loss."
Write-Host "Peer DISCOVER collisions accepted: $($session.peer_discover_commands_accepted)."
Write-Host "Peer capability-query collisions accepted: $($session.peer_capability_commands_accepted)."
Write-Host "Peer configuration collisions rejected: $($session.peer_configuration_commands_rejected)."
Write-Host "Peer CLOSE commands accepted during streaming: $($session.peer_close_commands_accepted)."
Write-Host "Pre-media Render STOP events: $($state.pre_media_render_stop_events); pre-start silent PCM frames discarded: $($session.pre_start_pcm_frames_discarded)."
if ($session.ended_by_peer_close -eq $true) {
    Write-Host 'Remote CLOSE was accepted during streaming; no local AVDTP SUSPEND/CLOSE command was sent.'
} else {
    Write-Host 'Physical ACL loss won the cleanup race; the worker used local cancellation only.'
}
Write-Host 'ConsumerLease and local resources were released. Stop the player normally; keep XM5 off.'
Write-Host 'No reboot or rollback is required.'
Write-Host "Result: $resultPath"
