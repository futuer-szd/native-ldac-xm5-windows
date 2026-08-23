# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1AudibleBurst,
    [ValidateRange(240,300)][int]$DurationSeconds = 300,
    [string]$CandidatePath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-audible-burst-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1AudibleBurst) {
    throw 'Refusing to authorize five seconds of cautious audible PCM. Re-run with -ConfirmV1AudibleBurst.'
}
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $root 'artifacts\v1-audible-burst\candidate'
}
$candidate = Get-V1AudibleBurstCandidate -CandidatePath $CandidatePath
$manifest = $candidate.manifest
if ((& git.exe -C $root rev-parse HEAD).Trim() -ne
        [string]$manifest.source_commit -or
    @(& git.exe -C $root status --porcelain).Count -ne 0) {
    throw 'The audible-burst candidate must match clean Git HEAD.'
}
$prerequisitePath = Join-Path $root `
    'artifacts\v1-pcm-burst\trial\transaction-20260725-122007-581.json'
if (-not (Test-Path -LiteralPath $prerequisitePath -PathType Leaf)) {
    throw 'The verified policy v5 transport prerequisite is missing.'
}
$prerequisite = Get-Content -LiteralPath $prerequisitePath -Raw |
    ConvertFrom-Json
$prerequisiteResultPath = [string]$prerequisite.result
if ([string]$prerequisite.status -ne 'verified' -or
    [string]$prerequisite.driver_tree -ne [string]$manifest.driver_tree -or
    -not (Test-Path -LiteralPath $prerequisiteResultPath -PathType Leaf)) {
    throw 'The audible candidate does not match verified policy v5 transport evidence.'
}
$prerequisiteResult = Get-Content -LiteralPath $prerequisiteResultPath -Raw |
    ConvertFrom-Json
if ($prerequisiteResult.passed -ne $true -or
    [int]$prerequisiteResult.target_duration_ms -ne 10000 -or
    [double]$prerequisiteResult.maximum_gain_scalar -gt 0.010001 -or
    [int]$prerequisiteResult.media_packets_written -le 4 -or
    $prerequisiteResult.start_accepted -ne $true -or
    $prerequisiteResult.suspend_accepted -ne $true -or
    $prerequisiteResult.close_accepted -ne $true -or
    $prerequisiteResult.consumer_lease_released -ne $true -or
    $prerequisiteResult.driver_installed_or_updated -ne $false -or
    $prerequisiteResult.rebooted -ne $false -or
    $prerequisiteResult.bluetooth_toggled -ne $false) {
    throw 'The policy v5 transport prerequisite result is not valid.'
}
$silenceTransaction = Get-Content -LiteralPath `
    ([string]$prerequisite.prerequisite) -Raw | ConvertFrom-Json
$zeroPacket = Get-Content -LiteralPath `
    ([string]$silenceTransaction.prerequisite) -Raw | ConvertFrom-Json
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
    ($info -join "`n") -notmatch '(?m)^Stream idle,' -or
    ($lease -join "`n") -notmatch
        '(?m)^PCM consumer lease released: generation 0\.$') {
    throw 'The Native endpoint must be absent/idle with LinkState disconnected and ConsumerLease released.'
}
Write-Host 'V1 five-second audibility readiness preflight passed.'
Write-Host "Candidate source: $($manifest.source_commit)"
Write-Host 'The post-volume multiplier is fixed at 0.25 (-12 dB); output samples can never exceed 0.25 (-12 dBFS).'
Write-Host 'Only zero-exchange OpenSignaling Win32 71 may recover, with at most four attempts and 15/30/45-second backoff.'
Write-Host 'LdacNative is already installed; no install or reboot is required.'
Write-Host 'This preflight was read-only.'
$target = 'one generation-bound five-second cautious-audibility XM5 session'
$action = 'Require non-silent PCM before OPEN, apply fixed -12 dB gain, pace five seconds, then SUSPEND/CLOSE'
if (-not $PSCmdlet.ShouldProcess($target, $action)) { return }

$trialRoot = Join-Path $root 'artifacts\v1-audible-burst\trial'
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
Write-Host 'V1 cautious audibility agent armed.'
Write-Host 'Turn on XM5, select Native LDAC, keep its Windows volume at your normal comfortable level, and play a clearly audible source continuously.'
Write-Host 'After RenderDemand starts, the worker tolerates an initial idle WaveRT transition and waits for the next active RUN.'
Write-Host 'The local PCM wait remains armed for up to 120 seconds so device selection, unmute, and playback require no rush.'
Write-Host 'Bluetooth OPEN remains forbidden until genuinely non-silent PCM is captured.'
Write-Host 'Keep audio playing; once non-silent PCM is captured, the media session lasts at most five seconds. Do not toggle Windows Bluetooth.'
$agent = Join-Path $candidate.root 'v1_presence_agent.exe'
$worker = Join-Path $candidate.root 'v1_transport_audible_worker.exe'
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
        -ExpectedDurationMs 5000 -ExpectedMaximumGain 0.25 `
        -MaximumAttempts 4)
if (-not $passed) {
    $failure = 'Audibility transport evidence failed.'
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
    throw "V1 audibility transport gate failed: $failure Stop playback, turn off XM5, and do not retry. Transaction: $transactionPath"
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
$transaction.status='transport-verified-awaiting-user-audibility'
$transaction.error=$null
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
Write-Host 'V1 five-second audibility transport evidence passed.'
Write-Host "$($session.sample_rate_hz) Hz/$($session.bits_per_sample)-bit; $($session.media_packets_written) packets; pre/post peak $($session.maximum_pre_gain_peak)/$($session.maximum_post_gain_peak)."
Write-Host 'ConsumerLease released; SUSPEND and CLOSE accepted. Stop playback and turn off XM5 normally.'
Write-Host 'Report whether you heard recognizable audio; the script intentionally does not infer acoustic success from packet delivery.'
Write-Host 'No reboot or rollback is required.'
Write-Host "Result: $resultPath"
