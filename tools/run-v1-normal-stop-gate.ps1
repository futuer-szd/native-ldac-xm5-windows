# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1NormalStop,
    [ValidateRange(300,420)][int]$DurationSeconds = 360,
    [string]$CandidatePath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-normal-stop-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1NormalStop) {
    throw 'Refusing to authorize the normal-stop gate. Re-run with -ConfirmV1NormalStop.'
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
    throw 'The normal-stop candidate must match clean Git HEAD.'
}

$fidelityPrerequisite = Get-Content `
    -LiteralPath $fidelityPrerequisitePath -Raw |
    ConvertFrom-Json
$fidelityResult = Get-Content -LiteralPath `
    ([string]$fidelityPrerequisite.result) -Raw | ConvertFrom-Json
if (-not (Test-V1NormalStopFidelityPrerequisite `
        -Transaction $fidelityPrerequisite -Result $fidelityResult) -or
    [string]$manifest.fidelity_prerequisite_source_commit -ne
        [string]$fidelityPrerequisite.source_commit) {
    throw 'Policy v10 is not the completed transparent-path prerequisite.'
}
$pnpPrerequisite = Get-Content -LiteralPath $pnpPrerequisitePath -Raw |
    ConvertFrom-Json
$pnpResultPath = [string]$pnpPrerequisite.result
$pnpResult = Get-Content -LiteralPath $pnpResultPath -Raw |
    ConvertFrom-Json
if (-not (Test-V1NormalStopPnpPrerequisite `
        -Transaction $pnpPrerequisite `
        -TransactionPath $pnpPrerequisitePath `
        -Result $pnpResult -ResultPath $pnpResultPath `
        -ExpectedDriverTree ([string]$manifest.driver_tree)) -or
    [string]$manifest.pnp_prerequisite_source_commit -ne
        [string]$pnpPrerequisite.source_commit) {
    throw 'Policy v14 is not the completed inbound PnP-rundown prerequisite.'
}

$v9 = Get-Content -LiteralPath `
    ([string]$fidelityPrerequisite.prerequisite) -Raw |
    ConvertFrom-Json
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
$transportInfoExit = $LASTEXITCODE
if ($transportInfoExit -ne 0 -or
    ($transportInfo -join "`n") -notmatch
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
$presence = @(& $endpointProbe --presence 2>&1)
$presenceExit = $LASTEXITCODE
$link = @(& $endpointProbe --link-state 2>&1)
$linkExit = $LASTEXITCODE
$info = @(& $endpointProbe --info 2>&1)
$infoExit = $LASTEXITCODE
$lease = @(& $endpointProbe --consumer-lease 2>&1)
$leaseExit = $LASTEXITCODE
if ($presenceExit -ne 0 -or $linkExit -ne 0 -or $infoExit -ne 0 -or
    $leaseExit -ne 0 -or
    ($presence -join "`n") -notmatch '(?m)^Physical presence absent:' -or
    ($link -join "`n") -notmatch '(?m)^Link disconnected:' -or
    ($info -join "`n") -notmatch '(?m)^Stream idle[:,]' -or
    ($lease -join "`n") -notmatch
        '(?m)^PCM consumer lease released: generation 0\.$') {
    throw 'The Native endpoint must be absent/idle with LinkState disconnected and ConsumerLease released.'
}

Write-Host 'V1 transparent normal-stop readiness preflight passed.'
Write-Host "Candidate source: $($manifest.source_commit)"
Write-Host 'The steady-state audio path uses unity gain with no sub-full-scale ceiling; only NaN/Inf cleanup and the final +/-1.0 sample boundary remain.'
Write-Host 'Playback boundaries use 20 ms of encoded startup silence and the existing 100 ms fade-in. PCM StreamStopped adds no synthetic tail; a bounded resume gets fresh silence/fade-in.'
Write-Host 'The sixty-second duration is only a hard safety bound; this gate must end earlier because you close the player.'
Write-Host 'Successful evidence requires exactly one inbound signaling OPEN and zero transport retry.'
Write-Host "Transport OPEN waits for $($script:V1NormalStopTransportOpenRenderStabilityMs) ms of continuous Render RUN in one epoch, so short system sounds cannot consume the session."
Write-Host 'Any retry remains a bounded failure diagnostic and cannot pass this gate.'
if ([int]$baselineAssessment.transport_package_count -gt 1) {
    Write-Host "Historical unbound LdacNative packages retained: $([int]$baselineAssessment.transport_package_count - 1); the active package is $([string]$pnpPrerequisite.selected_inf)."
}
Write-Host 'This preflight was read-only; no install or reboot is required.'
$target = 'one generation-bound XM5 normal Render STOP session'
$action = 'Start transparent LDAC, require at least five seconds of media, close the player to trigger Render STOP, SUSPEND/CLOSE, then require physical ACL disconnect'
if (-not $PSCmdlet.ShouldProcess($target, $action)) { return }

$trialRoot = Join-Path $root 'artifacts\v1-normal-stop\trial'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$dir = Join-Path $trialRoot "session-$stamp"
New-Item -ItemType Directory -Path $dir -Force | Out-Null
$statePath = Join-Path $dir 'state.json'
$sessionPath = Join-Path $dir 'session.json'
$logPath = Join-Path $dir 'agent.log'
$resultPath = Join-Path $dir 'result.json'
$transactionPath = Join-Path $trialRoot "transaction-$stamp.json"
$transaction = [ordered]@{
    schema_version = 1
    transport_policy_version = $script:V1NormalStopPolicyVersion
    source_commit = $manifest.source_commit
    driver_tree = $manifest.driver_tree
    fidelity_prerequisite = $fidelityPrerequisitePath
    pnp_prerequisite = $pnpPrerequisitePath
    created_at = (Get-Date).ToString('o')
    status = 'running'
    directory = $dir
    state = $statePath
    session = $sessionPath
    result = $resultPath
    error = $null
}
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
Write-Host 'V1 transparent normal-stop agent armed.'
Write-Host 'Turn on XM5, select Native LDAC, keep a comfortable fixed Windows volume, and continuously play clear content.'
Write-Host "After 'V1 bounded PCM media started', keep playing for at least 10 seconds, then completely exit the player. Pause alone may leave WaveRT RUN and does not prove STOP."
Write-Host "Wait for 'V1 contained engine stopped cleanly', then turn off XM5 and wait for 'V1 ACL disconnected'."
Write-Host 'Windows volume may be adjusted while media is playing; do not change output device, format, Windows Bluetooth, or reopen the player during this gate.'
$agent = Join-Path $candidate.root 'v1_presence_agent.exe'
$worker = Join-Path $candidate.root 'v1_transport_normal_stop_worker.exe'
$saved = $ErrorActionPreference
$exitCode = -1
try {
    $ErrorActionPreference = 'Continue'
    @(& $agent --run-for-ms ($DurationSeconds * 1000) --state $statePath `
        --endpoint-presence --observe-render-demand --observe-engine-ready `
        --exercise-transport-pcm-burst --pcm-fast-signaling-acquisition `
        --transport-open-render-stability-ms `
            $script:V1NormalStopTransportOpenRenderStabilityMs `
        --transport-result $sessionPath `
        --engine-executable $worker 2>&1 | Tee-Object -FilePath $logPath |
        ForEach-Object { Write-Host ([string]$_); $_ }) | Out-Null
    $exitCode = $LASTEXITCODE
} finally {
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
$passed = Test-V1NormalStopEvidence -State $state -Session $session `
    -Attempts $attempts -AgentExitCode $exitCode
if (-not $passed) {
    $failure = 'Normal Render STOP lifecycle evidence failed.'
    if ($null -ne $session -and
        [string]$session.disposition -eq 'backend-failure' -and
        [int]$session.stage -eq 1) {
        $diagnosticAvailable =
            $null -ne $session.PSObject.Properties[
                'open_diagnostic_available'] -and
            $session.open_diagnostic_available -eq $true
        $remoteResponseValid =
            $null -ne $session.PSObject.Properties[
                'open_diagnostic_remote_response_valid'] -and
            $session.open_diagnostic_remote_response_valid -eq $true
        if ($diagnosticAvailable -and $remoteResponseValid) {
            $response = [int]$session.open_diagnostic_response
            if ($response -eq 4) {
                $failure = 'XM5 reported no L2CAP resources through all four fast signaling attempts.'
            } elseif ($response -eq 2) {
                $failure = 'XM5 reported that the AVDTP signaling PSM is not supported.'
            } elseif ($response -eq 3) {
                $failure = 'XM5 rejected the signaling channel for security reasons.'
            } else {
                $failure = "XM5 returned L2CAP response $response during signaling OPEN."
            }
        } elseif ($diagnosticAvailable) {
            $failure = 'Signaling OPEN failed locally without a valid negative XM5 L2CAP response.'
        } else {
            $queryAttempts = if ($null -ne $session.PSObject.Properties[
                    'open_diagnostic_query_attempts']) {
                [int]$session.open_diagnostic_query_attempts
            } else { 0 }
            $queryError = if ($null -ne $session.PSObject.Properties[
                    'open_diagnostic_query_error']) {
                [int]$session.open_diagnostic_query_error
            } else { 0 }
            $queryBytes = if ($null -ne $session.PSObject.Properties[
                    'open_diagnostic_query_bytes']) {
                [int]$session.open_diagnostic_query_bytes
            } else { 0 }
            $failure = "Signaling OPEN failed and ABI 0.5 diagnostics could not be read (query attempts $queryAttempts, Win32 $queryError, bytes $queryBytes)."
        }
    } elseif ($null -ne $session -and
        $session.media_started_notified -ne $true) {
        if ([int]$session.stage -eq 9 -and
            ($session.volume_control_available -ne $true -or
             $session.volume_muted -eq $true)) {
            $failure = 'The Native endpoint volume state became unavailable or muted before Bluetooth OPEN; no signaling request or media packet was submitted.'
        } else {
            $failure = 'The transport stopped before media START.'
        }
    } elseif ($null -ne $session -and
        [int]$session.actual_duration_ms -lt
            $script:V1NormalStopMinimumMediaDurationMs) {
        $failure = 'The player stopped less than five seconds after media START.'
    } elseif ($null -ne $session -and
        $session.completed_full_duration -eq $true) {
        $failure = 'Render STOP was not observed before the sixty-second hard bound.'
    } elseif ($null -ne $session -and
        [int64]$session.volume_change_count -gt 0) {
        $failure = 'Windows volume, mute, format, or stream epoch changed after the lock.'
    } elseif ($null -ne $session -and
        ([int64]$session.limiter_attack_count -gt 0 -or
         [int64]$session.limiter_gain_reduced_frames -gt 0)) {
        $failure = 'The fixed sample safety boundary intervened; transparent-path evidence was not obtained.'
    } elseif ($null -ne $state -and
        [int]$state.disconnected_events -ne 1) {
        $failure = 'One physical ACL disconnect was not observed after graceful STOP.'
    }
    $transaction.status = 'failed'
    $transaction.error = $failure
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    throw "V1 normal-stop gate failed: $failure Stop playback, turn off XM5, and do not retry. Transaction: $transactionPath"
}

$result = [ordered]@{
    schema_version = 1
    transport_policy_version = $script:V1NormalStopPolicyVersion
    normal_stop_passed = $true
    source_commit = $manifest.source_commit
    driver_tree = $manifest.driver_tree
    fidelity_prerequisite = $fidelityPrerequisitePath
    pnp_prerequisite = $pnpPrerequisitePath
    transaction = $transactionPath
    session_generation = [int64]$session.session_generation
    acl_generation = [int64]$state.acl_generation
    render_started_events = [int]$state.render_started_events
    render_stopped_events = [int]$state.render_stopped_events
    disconnected_events = [int]$state.disconnected_events
    transport_open_attempts = [int]$state.transport_open_executed
    transport_open_render_stability_ms =
        [int]$state.transport_open_render_stability_ms
    transport_open_stability_resets =
        [int]$state.transport_open_stability_resets
    transport_open_stable_authorizations =
        [int]$state.transport_open_stable_authorizations
    transport_retry_count = [int]$state.transport_retries_scheduled
    signaling_direction = 'inbound'
    open_diagnostic_query_attempts =
        [int]$session.open_diagnostic_query_attempts
    open_diagnostic_query_error =
        [int]$session.open_diagnostic_query_error
    open_diagnostic_query_bytes =
        [int]$session.open_diagnostic_query_bytes
    open_diagnostic_channel_flags =
        [int]$session.open_diagnostic_channel_flags
    open_diagnostic_flags = [int]$session.open_diagnostic_flags
    target_duration_ms = [int]$session.target_duration_ms
    actual_duration_ms = [int]$session.actual_duration_ms
    media_packets_written = [int]$session.media_packets_written
    media_bytes_written = [int]$session.media_bytes_written
    pcm_frames_read = [int64]$session.pcm_frames_read
    pcm_frames_sent = [int64]$session.pcm_frames_sent
    transport_frames_sent = [int64]$session.transport_frames_sent
    startup_silence_ms = [double]$session.startup_silence_ms
    startup_silence_frames_sent =
        [int64]$session.startup_silence_frames_sent
    startup_silence_packets_written =
        [int]$session.startup_silence_packets_written
    maximum_gain_scalar = [double]$session.maximum_gain_scalar
    maximum_output_peak_ceiling =
        [double]$session.maximum_output_peak_ceiling
    maximum_pre_gain_peak = [double]$session.maximum_pre_gain_peak
    maximum_post_gain_peak = [double]$session.maximum_post_gain_peak
    limiter_attack_count = [int64]$session.limiter_attack_count
    limiter_gain_reduced_frames =
        [int64]$session.limiter_gain_reduced_frames
    limiter_gain_reduced_samples =
        [int64]$session.limiter_gain_reduced_samples
    limiter_fallback_clamp_count =
        [int64]$session.limiter_fallback_clamp_count
    volume_query_count = [int64]$session.volume_query_count
    volume_change_count = [int64]$session.volume_change_count
    fade_committed_sent_frames =
        [int64]$session.fade_committed_sent_frames
    fade_commit_failures = [int64]$session.fade_commit_failures
    boundary_resume_count = [int]$session.boundary_resume_count
    boundary_resume_fade_frames =
        [int64]$session.boundary_resume_fade_frames
    ceiling_ramp_start = [double]$session.ceiling_ramp_start
    ceiling_ramp_ms = [double]$session.ceiling_ramp_ms
    ceiling_ramp_last = [double]$session.ceiling_ramp_last
    ended_by_graceful_stop = $session.ended_by_graceful_stop
    completed_full_duration = $session.completed_full_duration
    consumer_lease_released = $session.consumer_lease_released
    suspend_accepted = $session.avdtp_suspend_accepted
    close_accepted = $session.avdtp_close_accepted
    physical_acl_disconnected = $true
    stop_reason = 'render-stop'
    graceful_stop_actions = [int]$state.transport_graceful_stop_actions
    cancel_actions = [int]$state.transport_cancel_actions
    media_duration_ms = [int]$session.actual_duration_ms
    final_attempt_archived = $true
    resources_released = $true
    lifecycle_outcome = 'graceful-stop'
    driver_installed_or_updated = $false
    rebooted = $false
    bluetooth_toggled = $false
    default_output_changed = $false
    link_state_written = $false
}
Write-LegacyJsonAtomic -Value $result -Path $resultPath
$transaction.status = 'normal-stop-verified'
$transaction.error = $null
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
Write-Host 'V1 transparent normal-stop gate passed.'
Write-Host 'One inbound signaling OPEN completed with zero transport retry.'
Write-Host "Transport OPEN followed $($state.transport_open_render_stability_ms) ms of stable Render RUN; startup stability resets: $($state.transport_open_stability_resets)."
Write-Host "Media ran for $($session.actual_duration_ms) ms and stopped because RenderDemand became idle."
Write-Host 'SUSPEND/CLOSE, child acknowledgement, ConsumerLease release, and physical ACL disconnect all passed.'
Write-Host 'No audio-quality comparison is required for this lifecycle gate.'
Write-Host 'No reboot or rollback is required.'
Write-Host "Result: $resultPath"
