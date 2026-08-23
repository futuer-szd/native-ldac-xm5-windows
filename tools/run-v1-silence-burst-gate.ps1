# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1SilenceBurst,
    [ValidateRange(90,300)][int]$DurationSeconds = 180,
    [string]$CandidatePath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-silence-burst-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1SilenceBurst) {
    throw 'Refusing to authorize AVDTP START and four digital-zero packets. Re-run with -ConfirmV1SilenceBurst.'
}
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $root 'artifacts\v1-silence-burst\candidate'
}
$candidate = Get-V1SilenceBurstCandidate -CandidatePath $CandidatePath
$manifest = $candidate.manifest
if ((& git.exe -C $root rev-parse HEAD).Trim() -ne
        [string]$manifest.source_commit -or
    @(& git.exe -C $root status --porcelain).Count -ne 0) {
    throw 'The silence-burst candidate must match clean Git HEAD.'
}
$prerequisitePath = Join-Path $root `
    'artifacts\v1-reboot-discovery\trial\transaction-20260723-110405-197.json'
if (-not (Test-Path -LiteralPath $prerequisitePath -PathType Leaf)) {
    throw 'The verified zero-packet prerequisite transaction is missing.'
}
$prerequisite = Get-Content -LiteralPath $prerequisitePath -Raw |
    ConvertFrom-Json
if ([string]$prerequisite.status -ne 'configuration-verified' -or
    $prerequisite.configuration.passed -ne $true -or
    [string]$prerequisite.driver_tree -ne [string]$manifest.driver_tree) {
    throw 'The silence-burst candidate does not match the verified zero-packet driver tree.'
}
$baseline = Get-NativeLdacBaselineSnapshot `
    -BackupPath ([string]$prerequisite.backup_path)
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
$presence = @(& $endpointProbe --presence 2>&1)
$link = @(& $endpointProbe --link-state 2>&1)
$info = @(& $endpointProbe --info 2>&1)
if (($presence -join "`n") -notmatch '(?m)^Physical presence absent:' -or
    ($link -join "`n") -notmatch '(?m)^Link disconnected:' -or
    ($info -join "`n") -notmatch '(?m)^Stream idle,') {
    throw 'The Native endpoint must be absent, LinkState disconnected, and WaveRT idle.'
}
Write-Host 'V1 four-packet digital-zero readiness preflight passed.'
Write-Host "Candidate source: $($manifest.source_commit)"
Write-Host 'LdacNative is already installed from the verified driver tree; no install or reboot is required.'
Write-Host 'This preflight was read-only.'
$target = 'one generation-bound XM5 four-packet digital-zero LDAC session'
$action = 'Authorize AVDTP START, exactly four encoded zero packets, SUSPEND, CLOSE, and local cleanup'
if (-not $PSCmdlet.ShouldProcess($target, $action)) { return }

$trialRoot = Join-Path $root 'artifacts\v1-silence-burst\trial'
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
Write-Host 'V1 four-packet silence agent armed.'
Write-Host 'Turn on XM5, select Native LDAC, and start one audio source. No audible sound is expected from this digital-zero gate.'
Write-Host 'Keep playback running until the gate reports START/four packets/SUSPEND/CLOSE complete. Do not toggle Windows Bluetooth.'
$agent = Join-Path $candidate.root 'v1_presence_agent.exe'
$worker = Join-Path $candidate.root 'v1_transport_silence_worker.exe'
$saved = $ErrorActionPreference; $exitCode = -1
try {
    $ErrorActionPreference = 'Continue'
    @(& $agent --run-for-ms ($DurationSeconds * 1000) --state $statePath `
        --endpoint-presence --observe-render-demand --observe-engine-ready `
        --exercise-transport-silence --transport-result $sessionPath `
        --engine-executable $worker 2>&1 | Tee-Object -FilePath $logPath |
        ForEach-Object { Write-Host ([string]$_); $_ }) | Out-Null
    $exitCode = $LASTEXITCODE
} finally { $ErrorActionPreference = $saved }
$state = if (Test-Path -LiteralPath $statePath) {
    Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json } else { $null }
$session = if (Test-Path -LiteralPath $sessionPath) {
    Get-Content -LiteralPath $sessionPath -Raw | ConvertFrom-Json } else { $null }
$attemptFiles = @(Get-ChildItem -LiteralPath $dir `
    -Filter 'session.json.attempt-*.json' -File | Sort-Object Name)
$attempts = @($attemptFiles | ForEach-Object {
    Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json })
$passed = $null -ne $state -and $null -ne $session -and
    (Test-V1SilenceBurstEvidence -State $state -Session $session `
        -Attempts $attempts -AgentExitCode $exitCode)
if (-not $passed) {
    $transaction.status='failed'; $transaction.error='Silence-burst evidence failed.'
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    throw "V1 silence-burst gate failed. Stop playback, turn off XM5, and keep LdacNative installed for diagnosis. Transaction: $transactionPath"
}
$result = [ordered]@{schema_version=1; passed=$true;
    source_commit=$manifest.source_commit; transaction=$transactionPath;
    remote_seid=[int]$session.remote_seid;
    sample_rate_hz=[int]$session.sample_rate_hz;
    incoming_mtu=[int]$session.incoming_mtu;
    outgoing_mtu=[int]$session.outgoing_mtu;
    open_attempts=[int]$state.transport_open_executed;
    media_packets_written=[int]$session.media_packets_written;
    media_bytes_written=[int]$session.media_bytes_written;
    start_accepted=$session.avdtp_start_accepted;
    suspend_accepted=$session.avdtp_suspend_accepted;
    close_accepted=$session.avdtp_close_accepted;
    real_pcm_frames_read=0; driver_installed_or_updated=$false;
    rebooted=$false; bluetooth_toggled=$false}
Write-LegacyJsonAtomic -Value $result -Path $resultPath
$transaction.status='verified'; $transaction.error=$null
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
Write-Host 'V1 four-packet digital-zero gate passed.'
Write-Host "START accepted; $($session.media_packets_written) packet(s), $($session.media_bytes_written) byte(s); SUSPEND and CLOSE accepted."
Write-Host 'Stop playback and turn off XM5 normally. LdacNative remains installed; no reboot or rollback is required.'
Write-Host "Result: $resultPath"
