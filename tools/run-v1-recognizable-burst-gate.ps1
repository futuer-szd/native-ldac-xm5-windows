# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1RecognizableBurst,
    [ValidateRange(240,300)][int]$DurationSeconds = 300,
    [string]$CandidatePath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-recognizable-burst-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1RecognizableBurst) {
    throw 'Refusing to authorize five seconds of recognizable-audio PCM. Re-run with -ConfirmV1RecognizableBurst.'
}
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $root 'artifacts\v1-recognizable-burst\candidate'
}
$candidate = Get-V1RecognizableBurstCandidate -CandidatePath $CandidatePath
$manifest = $candidate.manifest
if ((& git.exe -C $root rev-parse HEAD).Trim() -ne
        [string]$manifest.source_commit -or
    @(& git.exe -C $root status --porcelain).Count -ne 0) {
    throw 'The recognizable-burst candidate must match clean Git HEAD.'
}
$prerequisitePath = Join-Path $root `
    'artifacts\v1-audible-burst\trial\transaction-20260725-155634-355.json'
if (-not (Test-Path -LiteralPath $prerequisitePath -PathType Leaf)) {
    throw 'The verified policy v6 faint-audibility prerequisite is missing.'
}
$prerequisite = Get-Content -LiteralPath $prerequisitePath -Raw |
    ConvertFrom-Json
$prerequisiteResultPath = [string]$prerequisite.result
if ([string]$prerequisite.status -ne 'verified-faint-audibility' -or
    [string]$prerequisite.driver_tree -ne [string]$manifest.driver_tree -or
    -not (Test-Path -LiteralPath $prerequisiteResultPath -PathType Leaf)) {
    throw 'The recognizable candidate does not match verified policy v6 evidence.'
}
$prerequisiteResult = Get-Content -LiteralPath $prerequisiteResultPath -Raw |
    ConvertFrom-Json
if ($prerequisiteResult.transport_passed -ne $true -or
    $prerequisiteResult.audibility_confirmed -ne $true -or
    [string]$prerequisiteResult.audibility_observation -ne
        'user-reported-faint-audio' -or
    [string]$prerequisiteResult.source_commit -ne
        [string]$prerequisite.source_commit -or
    [int]$prerequisiteResult.target_duration_ms -ne 5000 -or
    [Math]::Abs([double]$prerequisiteResult.maximum_gain_scalar - 0.25) -gt
        0.000001 -or
    [double]$prerequisiteResult.maximum_post_gain_peak -le 0.0 -or
    $prerequisiteResult.start_accepted -ne $true -or
    $prerequisiteResult.suspend_accepted -ne $true -or
    $prerequisiteResult.close_accepted -ne $true -or
    $prerequisiteResult.consumer_lease_released -ne $true -or
    $prerequisiteResult.driver_installed_or_updated -ne $false -or
    $prerequisiteResult.rebooted -ne $false -or
    $prerequisiteResult.bluetooth_toggled -ne $false) {
    throw 'The policy v6 faint-audibility prerequisite result is not valid.'
}
$v5 = Get-Content -LiteralPath ([string]$prerequisite.prerequisite) -Raw |
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
Write-Host 'V1 five-second recognizable-audio readiness preflight passed.'
Write-Host "Candidate source: $($manifest.source_commit)"
Write-Host 'Windows post-volume PCM uses unity gain. An independent hard limiter prevents output above 0.25 (-12 dBFS).'
Write-Host 'Only zero-exchange OpenSignaling Win32 71 may recover, with at most four attempts and 15/30/45-second backoff.'
Write-Host 'LdacNative is already installed; no install or reboot is required.'
Write-Host 'This preflight was read-only.'
$target = 'one generation-bound five-second recognizable-audio XM5 session'
$action = 'Require non-silent PCM before OPEN, use unity gain with a -12 dBFS limiter, pace five seconds, then SUSPEND/CLOSE'
if (-not $PSCmdlet.ShouldProcess($target, $action)) { return }

$trialRoot = Join-Path $root 'artifacts\v1-recognizable-burst\trial'
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
Write-Host 'V1 recognizable-audio agent armed.'
Write-Host 'Turn on XM5, select Native LDAC, set a normal comfortable Windows volume, unmute it, and continuously play clear, recognizable content.'
Write-Host 'You have up to 120 seconds after RenderDemand starts; Bluetooth OPEN stays forbidden until genuinely non-silent PCM is captured.'
Write-Host 'Pre-START render gaps keep the same bounded local PCM wait alive; they do not consume another transport attempt.'
Write-Host 'Once media starts, listen for at most five seconds. Do not toggle Windows Bluetooth.'
$agent = Join-Path $candidate.root 'v1_presence_agent.exe'
$worker = Join-Path $candidate.root 'v1_transport_recognizable_worker.exe'
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
        -ExpectedDurationMs 5000 -ExpectedMaximumGain 1.0 `
        -ExpectedMaximumOutputPeak 0.25 -RequireOutputPeakField `
        -RequirePretransportRenderGapTolerance `
        -MaximumAttempts 4)
if (-not $passed) {
    $failure = 'Recognizable-audio transport evidence failed.'
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
    throw "V1 recognizable-audio gate failed: $failure Stop playback, turn off XM5, and do not retry. Transaction: $transactionPath"
}
$result = [ordered]@{schema_version=1; transport_passed=$true;
    audibility_observation='user-report-required';
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
    media_packets_written=[int]$session.media_packets_written;
    media_bytes_written=[int]$session.media_bytes_written;
    maximum_gain_scalar=[double]$session.maximum_gain_scalar;
    maximum_output_peak_ceiling=[double]$session.maximum_output_peak_ceiling;
    maximum_pre_gain_peak=[double]$session.maximum_pre_gain_peak;
    maximum_post_gain_peak=[double]$session.maximum_post_gain_peak;
    consumer_lease_released=$session.consumer_lease_released;
    start_accepted=$session.avdtp_start_accepted;
    suspend_accepted=$session.avdtp_suspend_accepted;
    close_accepted=$session.avdtp_close_accepted;
    driver_installed_or_updated=$false; rebooted=$false;
    bluetooth_toggled=$false; default_output_changed=$false;
    link_state_written=$false}
Write-LegacyJsonAtomic -Value $result -Path $resultPath
$transaction.status='transport-verified-awaiting-user-recognizability'
$transaction.error=$null
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
Write-Host 'V1 five-second recognizable-audio transport evidence passed.'
Write-Host "$($session.sample_rate_hz) Hz/$($session.bits_per_sample)-bit; $($session.media_packets_written) packets; pre/post peak $($session.maximum_pre_gain_peak)/$($session.maximum_post_gain_peak); limiter $($session.maximum_output_peak_ceiling)."
Write-Host 'ConsumerLease released; SUSPEND and CLOSE accepted. Stop playback and turn off XM5 normally.'
Write-Host 'Report whether the content was recognizable and whether you heard dropouts, wrong speed, or noise. Packet delivery alone does not prove acoustic quality.'
Write-Host 'No reboot or rollback is required.'
Write-Host "Result: $resultPath"
