# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1LinkedLimiter,
    [ValidateRange(300,420)][int]$DurationSeconds = 360,
    [string]$CandidatePath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-linked-limiter-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1LinkedLimiter) {
    throw 'Refusing to authorize the linked-limiter comparison. Re-run with -ConfirmV1LinkedLimiter.'
}
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $root 'artifacts\v1-linked-limiter\candidate'
}
$candidate = Get-V1LinkedLimiterCandidate -CandidatePath $CandidatePath
$manifest = $candidate.manifest
if ((& git.exe -C $root rev-parse HEAD).Trim() -ne
        [string]$manifest.source_commit -or
    @(& git.exe -C $root status --porcelain).Count -ne 0) {
    throw 'The linked-limiter candidate must match clean Git HEAD.'
}

$prerequisitePath = Join-Path $root `
    $script:V1LinkedLimiterPrerequisiteRelativePath
if (-not (Test-Path -LiteralPath $prerequisitePath -PathType Leaf)) {
    throw 'The completed policy v8 comparison prerequisite is missing.'
}
$prerequisite = Get-Content -LiteralPath $prerequisitePath -Raw |
    ConvertFrom-Json
$prerequisiteResultPath = [string]$prerequisite.result
if (-not (Test-Path -LiteralPath $prerequisiteResultPath -PathType Leaf)) {
    throw 'The completed policy v8 result is missing.'
}
$prerequisiteResult = Get-Content -LiteralPath $prerequisiteResultPath -Raw |
    ConvertFrom-Json
if (-not (Test-V1LinkedLimiterPrerequisite `
        -Transaction $prerequisite -Result $prerequisiteResult `
        -ExpectedDriverTree ([string]$manifest.driver_tree)) -or
    [string]$manifest.prerequisite_source_commit -ne
        [string]$prerequisite.source_commit) {
    throw 'Policy v8 is not the frozen generally-clear/muffled-bass prerequisite.'
}

$v7 = Get-Content -LiteralPath ([string]$prerequisite.prerequisite) -Raw |
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

Write-Host 'V1 linked-limiter sixty-second comparison readiness preflight passed.'
Write-Host "Candidate source: $($manifest.source_commit)"
Write-Host 'Policy v8 transport and the generally-clear/muffled-bass user report are frozen prerequisites; policy v8 will not run again.'
Write-Host 'Policy v9 keeps unity gain, the 0.25 (-12 dBFS) output ceiling, sixty seconds, and the same bounded signaling recovery.'
Write-Host 'The only intended audio change is linked-stereo block gain reduction before the fallback hard clamp.'
Write-Host 'This preflight was read-only; no install or reboot is required.'
$target = 'one generation-bound sixty-second XM5 linked-limiter comparison'
$action = 'Require non-silent PCM before OPEN, apply linked-stereo limiter v1 at the 0.25 ceiling, pace sixty seconds, then SUSPEND/CLOSE'
if (-not $PSCmdlet.ShouldProcess($target, $action)) { return }

$trialRoot = Join-Path $root 'artifacts\v1-linked-limiter\trial'
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
    transport_policy_version = 9
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
Write-Host 'V1 linked-limiter comparison agent armed.'
Write-Host 'Turn on XM5, select Native LDAC, use the same comfortable Windows volume, and continuously play varied content with clear bass.'
Write-Host 'Pre-START render gaps and PCM epoch changes remain inside the same bounded local wait.'
Write-Host 'Once media starts, listen for the full sixty seconds and compare bass definition, clarity, pumping, and distortion with policy v8.'
Write-Host 'Do not pause, change output device, or toggle Windows Bluetooth.'
$agent = Join-Path $candidate.root 'v1_presence_agent.exe'
$worker = Join-Path $candidate.root `
    'v1_transport_linked_limiter_worker.exe'
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
    (Test-V1LinkedLimiterEvidence -State $state -Session $session `
        -Attempts $attempts -AgentExitCode $exitCode)
if (-not $passed) {
    $failure = 'Linked-limiter sixty-second transport or telemetry evidence failed.'
    if ($null -ne $session -and [int]$session.open_attempts -eq 0 -and
        $session.audible_pcm_confirmed_before_open -ne $true) {
        $failure = 'No non-silent PCM was captured before Bluetooth OPEN; media packets remained zero.'
    } elseif ($null -ne $state -and
        [int]$state.transport_retry_budget_exhausted -eq 1 -and
        [int]$state.transport_retryable_failures -eq 4) {
        $failure = 'OpenSignaling Win32 71 remained zero-exchange for all four bounded attempts.'
    } elseif ($null -ne $session -and
        $session.PSObject.Properties['limiter_fallback_clamp_count'] -and
        [int64]$session.limiter_fallback_clamp_count -ne 0) {
        $failure = 'The linked limiter required a fallback hard clamp; audio-quality evidence was rejected.'
    }
    $transaction.status = 'failed'
    $transaction.error = $failure
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    throw "V1 linked-limiter comparison failed: $failure Stop playback, turn off XM5, and do not retry. Transaction: $transactionPath"
}

$result = [ordered]@{
    schema_version = 1
    transport_passed = $true
    quality_comparison_observation = 'user-report-required'
    transport_policy_version = 9
    source_commit = $manifest.source_commit
    driver_tree = $manifest.driver_tree
    prerequisite = $prerequisitePath
    transaction = $transactionPath
    remote_seid = [int]$session.remote_seid
    sample_rate_hz = [int]$session.sample_rate_hz
    bits_per_sample = [int]$session.bits_per_sample
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
    limited_output_samples = [int64]$session.limited_output_samples
    limiter_algorithm = [string]$session.limiter_algorithm
    limiter_algorithm_version = [int]$session.limiter_algorithm_version
    limiter_release_ms = [double]$session.limiter_release_ms
    limiter_minimum_gain = [double]$session.limiter_minimum_gain
    limiter_gain_reduced_frames =
        [int64]$session.limiter_gain_reduced_frames
    limiter_gain_reduced_samples =
        [int64]$session.limiter_gain_reduced_samples
    limiter_fallback_clamp_count =
        [int64]$session.limiter_fallback_clamp_count
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
$transaction.status = 'transport-verified-awaiting-linked-limiter-report'
$transaction.error = $null
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
Write-Host 'V1 linked-limiter sixty-second transport and telemetry evidence passed.'
Write-Host "Limiter $($session.limiter_algorithm) v$($session.limiter_algorithm_version); minimum gain $($session.limiter_minimum_gain)."
Write-Host "Gain reduced $($session.limiter_gain_reduced_frames) frame(s) / $($session.limiter_gain_reduced_samples) sample(s); fallback clamps $($session.limiter_fallback_clamp_count)."
Write-Host 'ConsumerLease released; SUSPEND and CLOSE accepted.'
Write-Host 'Stop playback and turn off XM5 normally. Report whether bass definition improved and whether clarity, pumping, noise, speed, or distortion changed.'
Write-Host 'No reboot or rollback is required.'
Write-Host "Result: $resultPath"
