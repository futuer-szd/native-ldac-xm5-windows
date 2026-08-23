# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1StabilityBurst,
    [ValidateRange(300,420)][int]$DurationSeconds = 360,
    [string]$CandidatePath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-stability-burst-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1StabilityBurst) {
    throw 'Refusing to authorize sixty seconds of stability PCM. Re-run with -ConfirmV1StabilityBurst.'
}
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $root 'artifacts\v1-stability-burst\candidate'
}
$candidate = Get-V1StabilityBurstCandidate -CandidatePath $CandidatePath
$manifest = $candidate.manifest
if ((& git.exe -C $root rev-parse HEAD).Trim() -ne
        [string]$manifest.source_commit -or
    @(& git.exe -C $root status --porcelain).Count -ne 0) {
    throw 'The stability-burst candidate must match clean Git HEAD.'
}
$prerequisitePath = Join-Path $root `
    'artifacts\v1-recognizable-burst\trial\transaction-20260725-222235-095.json'
if (-not (Test-Path -LiteralPath $prerequisitePath -PathType Leaf)) {
    throw 'The verified policy v7 recognizable-audio prerequisite is missing.'
}
$prerequisite = Get-Content -LiteralPath $prerequisitePath -Raw |
    ConvertFrom-Json
$prerequisiteResultPath = [string]$prerequisite.result
if ([string]$prerequisite.status -ne 'verified-recognizable-audio' -or
    [string]$prerequisite.driver_tree -ne [string]$manifest.driver_tree -or
    -not (Test-Path -LiteralPath $prerequisiteResultPath -PathType Leaf)) {
    throw 'The stability candidate does not match verified policy v7 evidence.'
}
$prerequisiteResult = Get-Content -LiteralPath $prerequisiteResultPath -Raw |
    ConvertFrom-Json
if ($prerequisiteResult.transport_passed -ne $true -or
    $prerequisiteResult.audibility_confirmed -ne $true -or
    $prerequisiteResult.recognizability_confirmed -ne $true -or
    [string]$prerequisiteResult.audibility_observation -ne
        'user-reported-audio-after-media-start' -or
    [string]$prerequisiteResult.recognizability_observation -ne
        'user-reported-recognizable-audio' -or
    [string]$prerequisiteResult.source_commit -ne
        [string]$prerequisite.source_commit -or
    [int]$prerequisiteResult.target_duration_ms -ne 5000 -or
    [Math]::Abs([double]$prerequisiteResult.maximum_gain_scalar - 1.0) -gt
        0.000001 -or
    [Math]::Abs(
        [double]$prerequisiteResult.maximum_output_peak_ceiling - 0.25) -gt
        0.000001 -or
    $prerequisiteResult.start_accepted -ne $true -or
    $prerequisiteResult.suspend_accepted -ne $true -or
    $prerequisiteResult.close_accepted -ne $true -or
    $prerequisiteResult.consumer_lease_released -ne $true -or
    $prerequisiteResult.driver_installed_or_updated -ne $false -or
    $prerequisiteResult.rebooted -ne $false -or
    $prerequisiteResult.bluetooth_toggled -ne $false) {
    throw 'The policy v7 recognizable-audio prerequisite result is not valid.'
}
$v6 = Get-Content -LiteralPath ([string]$prerequisite.prerequisite) -Raw |
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
if ($devices.Count -ne 1 -or [string]$devices[0].service -ne 'LdacNative' -or
    [int]$devices[0].problem_code -ne 0 -or
    @($baseline.transport_test_packages).Count -ne 1 -or
    $native.Count -ne 1 -or [string]$native[0].service -ne 'NativeLdacAudio' -or
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
Write-Host 'V1 sixty-second stability readiness preflight passed.'
Write-Host "Candidate source: $($manifest.source_commit)"
Write-Host 'Windows post-volume PCM uses unity gain. The independent 0.25 (-12 dBFS) hard limiter remains active and now reports every limited sample.'
Write-Host 'Only zero-exchange OpenSignaling Win32 71 may recover, with at most four attempts and 15/30/45-second backoff.'
Write-Host 'LdacNative is already installed; no install or reboot is required.'
Write-Host 'This preflight was read-only.'
$target = 'one generation-bound sixty-second XM5 stability session'
$action = 'Require non-silent PCM before OPEN, use unity gain with limiter telemetry, pace sixty seconds, then SUSPEND/CLOSE'
if (-not $PSCmdlet.ShouldProcess($target, $action)) { return }

$trialRoot = Join-Path $root 'artifacts\v1-stability-burst\trial'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$dir = Join-Path $trialRoot "session-$stamp"
New-Item -ItemType Directory -Path $dir -Force | Out-Null
$statePath = Join-Path $dir 'state.json'
$sessionPath = Join-Path $dir 'session.json'
$logPath = Join-Path $dir 'agent.log'
$resultPath = Join-Path $dir 'result.json'
$transactionPath = Join-Path $trialRoot "transaction-$stamp.json"
$transaction = [ordered]@{schema_version=1; source_commit=$manifest.source_commit;
    driver_tree=$manifest.driver_tree; prerequisite=$prerequisitePath;
    created_at=(Get-Date).ToString('o'); status='running'; directory=$dir;
    state=$statePath; session=$sessionPath; result=$resultPath; error=$null}
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
Write-Host 'V1 sixty-second stability agent armed.'
Write-Host 'Turn on XM5, select Native LDAC, keep a normal comfortable Windows volume, unmute it, and continuously play varied, recognizable content.'
Write-Host 'Pre-START render gaps keep the same bounded local PCM wait alive and do not consume another transport attempt.'
Write-Host 'If WaveRT changes epoch before non-silent PCM arrives, the worker releases the old ConsumerLease and rebinds the next active epoch within the same deadline.'
Write-Host 'Once media starts, listen for the full sixty seconds. Do not pause, change output device, or toggle Windows Bluetooth.'
$agent = Join-Path $candidate.root 'v1_presence_agent.exe'
$worker = Join-Path $candidate.root 'v1_transport_stability_worker.exe'
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
    Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json } else { $null }
$session = if (Test-Path -LiteralPath $sessionPath) {
    Get-Content -LiteralPath $sessionPath -Raw | ConvertFrom-Json } else { $null }
$attemptFiles = @(Get-ChildItem -LiteralPath $dir `
    -Filter 'session.json.attempt-*.json' -File | Sort-Object Name)
$attempts = @($attemptFiles | ForEach-Object {
    Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json })
$passed = $null -ne $state -and $null -ne $session -and
    (Test-V1PcmBurstEvidence -State $state -Session $session `
        -Attempts $attempts -AgentExitCode $exitCode `
        -ExpectedDurationMs 60000 -ExpectedMaximumPackets 32768 `
        -ExpectedMaximumGain 1.0 -ExpectedMaximumOutputPeak 0.25 `
        -RequireOutputPeakField -RequireLimiterTelemetry `
        -RequireEpochReacquireTelemetry `
        -RequirePretransportRenderGapTolerance -MaximumAttempts 4)
if (-not $passed) {
    $failure = 'Sixty-second stability transport evidence failed.'
    if ($null -ne $session -and $session.pcm_prepared -ne $true -and
        [int]$session.backend_error -eq 258 -and
        [int]$session.open_attempts -eq 0) {
        $failure = 'No active WaveRT RUN appeared during the bounded wait; Bluetooth OPEN and media packets remained zero.'
    } elseif ($null -ne $session -and
        [int]$session.open_attempts -eq 0 -and
        $session.audible_pcm_confirmed_before_open -ne $true) {
        $failure = "No non-silent PCM was captured during the bounded 120-second local wait (maximum peak $($session.maximum_pre_gain_peak)); Bluetooth OPEN and media packets remained zero."
    } elseif ($null -ne $state -and
        [int]$state.transport_retry_budget_exhausted -eq 1 -and
        [int]$state.transport_retryable_failures -eq 4) {
        $failure = 'OpenSignaling Win32 71 remained strictly zero-exchange for all four bounded attempts; no AVDTP command or media packet was sent.'
    }
    $transaction.status='failed'; $transaction.error=$failure
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    throw "V1 sixty-second stability gate failed: $failure Stop playback, turn off XM5, and do not retry. Transaction: $transactionPath"
}
$totalOutputSamples = [int64]$session.pcm_frames_read * 2
$limitedFraction = if ($totalOutputSamples -gt 0) {
    [double]$session.limited_output_samples / $totalOutputSamples
} else { 0.0 }
$result = [ordered]@{schema_version=1; transport_passed=$true;
    stability_observation='user-report-required';
    source_commit=$manifest.source_commit; transaction=$transactionPath;
    remote_seid=[int]$session.remote_seid;
    sample_rate_hz=[int]$session.sample_rate_hz;
    bits_per_sample=[int]$session.bits_per_sample;
    outgoing_mtu=[int]$session.outgoing_mtu;
    open_attempts=[int]$state.transport_open_executed;
    target_duration_ms=[int]$session.target_duration_ms;
    actual_duration_ms=[int]$session.actual_duration_ms;
    pcm_frames_read=[int64]$session.pcm_frames_read;
    pcm_frames_sent=[int64]$session.pcm_frames_sent;
    pcm_prepare_attempts=[int]$session.pcm_prepare_attempts;
    pcm_epoch_restarts=[int]$session.pcm_epoch_restarts;
    consumer_lease_acquire_count=[int]$session.consumer_lease_acquire_count;
    consumer_lease_release_count=[int]$session.consumer_lease_release_count;
    media_packets_written=[int]$session.media_packets_written;
    media_bytes_written=[int]$session.media_bytes_written;
    maximum_gain_scalar=[double]$session.maximum_gain_scalar;
    maximum_output_peak_ceiling=[double]$session.maximum_output_peak_ceiling;
    maximum_pre_gain_peak=[double]$session.maximum_pre_gain_peak;
    maximum_unlimited_post_gain_peak=[double]$session.maximum_unlimited_post_gain_peak;
    maximum_post_gain_peak=[double]$session.maximum_post_gain_peak;
    limited_output_samples=[int64]$session.limited_output_samples;
    limited_output_fraction=$limitedFraction;
    consumer_lease_released=$session.consumer_lease_released;
    start_accepted=$session.avdtp_start_accepted;
    suspend_accepted=$session.avdtp_suspend_accepted;
    close_accepted=$session.avdtp_close_accepted;
    driver_installed_or_updated=$false; rebooted=$false;
    bluetooth_toggled=$false; default_output_changed=$false;
    link_state_written=$false}
Write-LegacyJsonAtomic -Value $result -Path $resultPath
$transaction.status='transport-verified-awaiting-user-stability-report'
$transaction.error=$null
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
Write-Host 'V1 sixty-second stability transport evidence passed.'
Write-Host "$($session.sample_rate_hz) Hz/$($session.bits_per_sample)-bit; $($session.media_packets_written) packets; unlimited/output peak $($session.maximum_unlimited_post_gain_peak)/$($session.maximum_post_gain_peak)."
Write-Host "Limiter engaged for $($session.limited_output_samples) output sample(s), fraction $limitedFraction."
Write-Host "PCM prepare/rebind: $($session.pcm_prepare_attempts) prepare(s), $($session.pcm_epoch_restarts) epoch restart(s); ConsumerLease $($session.consumer_lease_acquire_count)/$($session.consumer_lease_release_count) acquired/released."
Write-Host 'ConsumerLease released; SUSPEND and CLOSE accepted. Stop playback and turn off XM5 normally.'
Write-Host 'Report loudness, dropouts, wrong speed, noise, and distortion. Transport evidence alone does not prove acoustic quality.'
Write-Host 'No reboot or rollback is required.'
Write-Host "Result: $resultPath"
