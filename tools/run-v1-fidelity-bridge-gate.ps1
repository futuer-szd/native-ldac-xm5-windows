# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1FidelityBridge,
    [ValidateRange(240,360)][int]$DurationSeconds = 300,
    [string]$CandidatePath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-fidelity-bridge-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1FidelityBridge) {
    throw 'Refusing to authorize the fidelity-bridge gate. Re-run with -ConfirmV1FidelityBridge.'
}
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $root 'artifacts\v1-fidelity-bridge\candidate'
}
$candidate = Get-V1FidelityBridgeCandidate -CandidatePath $CandidatePath
$manifest = $candidate.manifest
if ((& git.exe -C $root rev-parse HEAD).Trim() -ne
        [string]$manifest.source_commit -or
    @(& git.exe -C $root status --porcelain).Count -ne 0) {
    throw 'The fidelity-bridge candidate must match clean Git HEAD.'
}

$prerequisitePath = Join-Path $root `
    $script:V1FidelityBridgePrerequisiteRelativePath
if (-not (Test-Path -LiteralPath $prerequisitePath -PathType Leaf)) {
    throw 'The completed policy v9 prerequisite is missing.'
}
$prerequisite = Get-Content -LiteralPath $prerequisitePath -Raw |
    ConvertFrom-Json
$prerequisiteResultPath = [string]$prerequisite.result
if (-not (Test-Path -LiteralPath $prerequisiteResultPath -PathType Leaf)) {
    throw 'The completed policy v9 result is missing.'
}
$prerequisiteResult = Get-Content -LiteralPath $prerequisiteResultPath -Raw |
    ConvertFrom-Json
if (-not (Test-V1FidelityBridgePrerequisite `
        -Transaction $prerequisite -Result $prerequisiteResult `
        -ExpectedDriverTree ([string]$manifest.driver_tree)) -or
    [string]$manifest.prerequisite_source_commit -ne
        [string]$prerequisite.source_commit) {
    throw 'Policy v9 is not the frozen transport-verified/quality-not-assessed prerequisite.'
}

$v8 = Get-Content -LiteralPath ([string]$prerequisite.prerequisite) -Raw |
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
$devices = @($baseline.a2dp_devices)
$native = @($baseline.native_audio_devices | Where-Object { $_.present })
$alt = $baseline.original_a2dp_user_service
if ($devices.Count -ne 1 -or
    [string]$devices[0].service -ne 'LdacNative' -or
    [int]$devices[0].problem_code -ne 0 -or
    @($baseline.transport_test_packages).Count -ne 1 -or
    $native.Count -ne 1 -or
    [string]$native[0].service -ne 'NativeLdacAudio' -or
    [int]$native[0].problem_code -ne 0 -or
    @($baseline.workspace_processes).Count -ne 0 -or
    @($baseline.scheduled_tasks).Count -ne 0 -or
    $null -eq $alt -or [string]$alt.start_mode -ne 'Manual' -or
    [string]$alt.state -ne 'Stopped') {
    throw 'The persistent LdacNative plus V1 endpoint baseline is not healthy.'
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

Write-Host 'V1 ten-second fidelity-bridge readiness preflight passed.'
Write-Host "Candidate source: $($manifest.source_commit)"
Write-Host 'The -1 dBFS value is strictly a digital sample-peak ceiling (0.89125094). It is not true-peak and must not be described as dBTP.'
Write-Host 'Policy v9 transport is frozen as the prerequisite; its acoustic quality remains explicitly not assessed.'
Write-Host 'The ceiling ramps by successfully sent frame from 0.25 to 0.89125094 over 2000 ms, with a 100 ms sent-frame fade-in.'
Write-Host 'Windows volume, mute, sample format, and stream epoch are locked after PCM prepare; any change fails the gate.'
Write-Host 'Only zero-exchange OpenSignaling Win32 71 may retry, at most four attempts for this ACL generation.'
Write-Host 'This preflight was read-only; no install or reboot is required.'
$target = 'one generation-bound ten-second XM5 fidelity-bridge session'
$action = 'Lock Windows PCM state, fade in for 100 ms, ramp the digital sample-peak ceiling for 2000 ms, pace ten seconds, then SUSPEND/CLOSE'
if (-not $PSCmdlet.ShouldProcess($target, $action)) { return }

$trialRoot = Join-Path $root 'artifacts\v1-fidelity-bridge\trial'
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
    transport_policy_version = 10
    source_commit = $manifest.source_commit
    driver_tree = $manifest.driver_tree
    prerequisite = $prerequisitePath
    created_at = (Get-Date).ToString('o')
    status = 'running'
    directory = $dir
    state = $statePath
    session = $sessionPath
    result = $resultPath
    error = $null
}
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
Write-Host 'V1 fidelity-bridge agent armed.'
Write-Host 'Turn on XM5, select Native LDAC, choose a comfortable Windows volume, and play clear recognizable content continuously.'
Write-Host 'After playback begins, do not touch Windows volume/mute, output format, output device, player pause, or Bluetooth.'
Write-Host 'The media interval lasts ten seconds after START; then it must SUSPEND and CLOSE.'
$agent = Join-Path $candidate.root 'v1_presence_agent.exe'
$worker = Join-Path $candidate.root `
    'v1_transport_fidelity_bridge_worker.exe'
$saved = $ErrorActionPreference
$exitCode = -1
try {
    $ErrorActionPreference = 'Continue'
    @(& $agent --run-for-ms ($DurationSeconds * 1000) --state $statePath `
        --endpoint-presence --observe-render-demand --observe-engine-ready `
        --exercise-transport-pcm-burst --transport-result $sessionPath `
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
$passed = $null -ne $state -and $null -ne $session -and
    (Test-V1FidelityBridgeEvidence -State $state -Session $session `
        -Attempts $attempts -AgentExitCode $exitCode)
if (-not $passed) {
    $failure = 'Fidelity-bridge transport or telemetry evidence failed.'
    if ($null -ne $session -and [int64]$session.volume_change_count -gt 0) {
        $failure = 'Windows volume, mute, format, or stream epoch changed after the fidelity lock.'
    } elseif ($null -ne $session -and
        [int64]$session.fade_commit_failures -gt 0) {
        $failure = 'Prepared fade frames and successfully sent frames did not commit exactly.'
    } elseif ($null -ne $state -and
        [int]$state.transport_retry_budget_exhausted -eq 1 -and
        [int]$state.transport_retryable_failures -eq 4) {
        $failure = 'OpenSignaling Win32 71 remained zero-exchange for all four bounded attempts.'
    }
    $transaction.status = 'failed'
    $transaction.error = $failure
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    throw "V1 fidelity-bridge gate failed: $failure Stop playback, turn off XM5, and do not retry. Transaction: $transactionPath"
}

$result = [ordered]@{
    schema_version = 1
    transport_passed = $true
    fidelity_observation = 'user-report-required'
    transport_policy_version = 10
    source_commit = $manifest.source_commit
    driver_tree = $manifest.driver_tree
    prerequisite = $prerequisitePath
    transaction = $transactionPath
    peak_measurement = 'digital-sample-peak'
    peak_unit = 'dBFS'
    sample_peak_dbfs = $script:V1FidelitySamplePeakDbfs
    sample_peak_ceiling = $script:V1FidelitySamplePeakCeiling
    remote_seid = [int]$session.remote_seid
    sample_rate_hz = [int]$session.sample_rate_hz
    bits_per_sample = [int]$session.bits_per_sample
    stream_epoch = [int64]$session.stream_epoch
    session_generation = [int64]$session.session_generation
    acl_generation = [int64]$state.acl_generation
    outgoing_mtu = [int]$session.outgoing_mtu
    open_attempts = [int]$state.transport_open_executed
    target_duration_ms = [int]$session.target_duration_ms
    actual_duration_ms = [int]$session.actual_duration_ms
    pcm_frames_read = [int64]$session.pcm_frames_read
    pcm_frames_sent = [int64]$session.pcm_frames_sent
    pcm_prepare_attempts = [int]$session.pcm_prepare_attempts
    pcm_epoch_restarts = [int]$session.pcm_epoch_restarts
    consumer_lease_acquire_count =
        [int]$session.consumer_lease_acquire_count
    consumer_lease_release_count =
        [int]$session.consumer_lease_release_count
    media_packets_written = [int]$session.media_packets_written
    media_bytes_written = [int]$session.media_bytes_written
    maximum_gain_scalar = [double]$session.maximum_gain_scalar
    maximum_output_peak_ceiling =
        [double]$session.maximum_output_peak_ceiling
    maximum_pre_gain_peak = [double]$session.maximum_pre_gain_peak
    maximum_unlimited_post_gain_peak =
        [double]$session.maximum_unlimited_post_gain_peak
    maximum_post_gain_peak = [double]$session.maximum_post_gain_peak
    limiter_algorithm = [string]$session.limiter_algorithm
    limiter_algorithm_version = [int]$session.limiter_algorithm_version
    limiter_fallback_clamp_count =
        [int64]$session.limiter_fallback_clamp_count
    output_chain_version = [int]$session.output_chain_version
    volume_query_count = [int64]$session.volume_query_count
    volume_change_count = [int64]$session.volume_change_count
    volume_scalar_minimum = [double]$session.volume_scalar_minimum
    volume_scalar_maximum = [double]$session.volume_scalar_maximum
    volume_scalar_last = [double]$session.volume_scalar_last
    volume_db_minimum = [double]$session.volume_db_minimum
    volume_db_maximum = [double]$session.volume_db_maximum
    volume_db_last = [double]$session.volume_db_last
    volume_stable = $session.volume_stable
    fade_algorithm = [string]$session.fade_algorithm
    fade_algorithm_version = [int]$session.fade_algorithm_version
    fade_in_ms = [double]$session.fade_in_ms
    fade_duration_frames = [int64]$session.fade_duration_frames
    fade_committed_sent_frames =
        [int64]$session.fade_committed_sent_frames
    fade_frames_below_unity = [int64]$session.fade_frames_below_unity
    fade_blocks_prepared = [int64]$session.fade_blocks_prepared
    fade_blocks_committed = [int64]$session.fade_blocks_committed
    fade_commit_failures = [int64]$session.fade_commit_failures
    fade_sanitized_sample_count =
        [int64]$session.fade_sanitized_sample_count
    fade_minimum_gain = [double]$session.fade_minimum_gain
    fade_last_gain = [double]$session.fade_last_gain
    ceiling_ramp_start = [double]$session.ceiling_ramp_start
    ceiling_ramp_ms = [double]$session.ceiling_ramp_ms
    ceiling_ramp_last = [double]$session.ceiling_ramp_last
    consumer_lease_released = $session.consumer_lease_released
    start_accepted = $session.avdtp_start_accepted
    suspend_accepted = $session.avdtp_suspend_accepted
    close_accepted = $session.avdtp_close_accepted
    driver_installed_or_updated = $false
    rebooted = $false
    bluetooth_toggled = $false
    default_output_changed = $false
    link_state_written = $false
}
Write-LegacyJsonAtomic -Value $result -Path $resultPath
$transaction.status = 'transport-verified-awaiting-fidelity-report'
$transaction.error = $null
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
Write-Host 'V1 ten-second fidelity-bridge transport and telemetry evidence passed.'
Write-Host "Digital sample peak $($session.maximum_post_gain_peak), ceiling $($script:V1FidelitySamplePeakCeiling) (-1 dBFS sample-peak; not true-peak/dBTP)."
Write-Host "Fade committed $($session.fade_committed_sent_frames) sent frame(s); ceiling ramp ended at $($session.ceiling_ramp_last)."
Write-Host "Stable PCM queries $($session.volume_query_count), changes $($session.volume_change_count); generation $($session.session_generation)."
Write-Host 'ConsumerLease released; SUSPEND and CLOSE accepted.'
Write-Host 'Stop playback and turn off XM5 normally. Report recognizability, loudness, bass, clarity, pumping, noise, speed, and distortion.'
Write-Host 'No reboot or rollback is required.'
Write-Host "Result: $resultPath"
