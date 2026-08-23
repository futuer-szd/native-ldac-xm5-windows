# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1PostRebootDiscovery,
    [ValidateRange(120, 300)]
    [int]$DurationSeconds = 180,
    [string]$TransactionPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'v1-reboot-discovery-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1PostRebootDiscovery) {
    throw 'Refusing to authorize one signaling OPEN. Re-run with -ConfirmV1PostRebootDiscovery.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$transactionRoot = Join-Path $projectRoot `
    'artifacts\v1-reboot-discovery\trial'
if ([string]::IsNullOrWhiteSpace($TransactionPath)) {
    $latestPath = Join-Path $transactionRoot 'latest-transaction.txt'
    if (-not (Test-Path -LiteralPath $latestPath -PathType Leaf)) {
        throw 'No V1 reboot discovery transaction was found.'
    }
    $TransactionPath = (Get-Content -LiteralPath $latestPath -Raw).Trim()
}
$TransactionPath = [System.IO.Path]::GetFullPath($TransactionPath)
$transaction = Get-Content -LiteralPath $TransactionPath -Raw |
    ConvertFrom-Json
if ([int]$transaction.schema_version -ne 1 -or
    [string]$transaction.status -ne 'awaiting-reboot' -or
    [string]$transaction.phase -ne 'installed-awaiting-reboot') {
    throw "The selected transaction is not awaiting its one post-install reboot: $TransactionPath"
}

$preparedBoot = [datetime]::Parse(
    [string]$transaction.prepared_boot_time_utc).ToUniversalTime()
$currentBoot = Get-NativeLdacCurrentBootTime
if ($currentBoot -le $preparedBoot.AddSeconds(1)) {
    throw 'Windows has not rebooted since preparation. Keep XM5 off and reboot exactly once before this gate.'
}

$candidate = Get-V1RebootDiscoveryCandidate `
    -CandidatePath ([string]$transaction.candidate_path)
$manifest = $candidate.manifest
$candidateRoot = $candidate.root
if ([string]$manifest.source_commit -ne
    [string]$transaction.source_commit) {
    throw 'The candidate no longer matches the pending transaction.'
}

$connectionProbe = Join-Path $candidateRoot 'xm5_connection_probe.exe'
if ((Get-NativeLdacBluetoothRadioState `
        -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne
    'ready') {
    throw 'Windows Bluetooth is off or the radio is unavailable. Turn Bluetooth on before this gate; no transport request was submitted.'
}
if ((Get-NativeLdacXm5BluetoothState `
        -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne
    'disconnected') {
    throw 'XM5 must be powered off at post-reboot preflight.'
}

$baseline = Get-NativeLdacBaselineSnapshot `
    -BackupPath ([string]$transaction.backup_path)
$transportDevices = @($baseline.a2dp_devices)
$nativeEndpoints = @($baseline.native_audio_devices | Where-Object {
    $_.present
})
$altService = $baseline.original_a2dp_user_service
if ($transportDevices.Count -ne 1 -or
    [string]$transportDevices[0].service -ne 'LdacNative' -or
    [int]$transportDevices[0].problem_code -ne 0 -or
    @($baseline.transport_test_packages).Count -ne 1 -or
    $nativeEndpoints.Count -ne 1 -or
    [string]$nativeEndpoints[0].service -ne 'NativeLdacAudio' -or
    [int]$nativeEndpoints[0].problem_code -ne 0 -or
    @($baseline.workspace_processes).Count -ne 0 -or
    @($baseline.scheduled_tasks).Count -ne 0 -or
    $null -eq $altService -or
    [string]$altService.start_mode -ne 'Manual' -or
    [string]$altService.state -ne 'Stopped') {
    throw 'The post-reboot LdacNative plus V1 endpoint ownership baseline is not healthy. Keep XM5 off and run the rollback script.'
}

$endpointProbe = Join-Path $candidateRoot 'audio_endpoint_probe.exe'
$initialPresence = @(& $endpointProbe --presence 2>&1)
$initialLink = @(& $endpointProbe --link-state 2>&1)
if (($initialPresence -join "`n") -notmatch
        '(?m)^Physical presence absent:' -or
    ($initialLink -join "`n") -notmatch '(?m)^Link disconnected:') {
    throw 'The V1 endpoint is not in its absent/disconnected preflight state. Keep XM5 off and run rollback.'
}

Write-Host 'V1 post-reboot zero-packet configuration preflight passed.'
Write-Host 'Windows Bluetooth is on, XM5 is off, and LdacNative owns the A2DP Sink PDO after a fresh boot.'
Write-Host 'This preflight was read-only; no transport request has been submitted.'

$target = 'one post-reboot, generation-bound XM5 zero-packet configuration session'
$action = 'Wait for one real ACL connection and RenderDemand, authorize at most three same-generation attempts with 15/30-second OpenSignaling backoff, complete SET_CONFIGURATION and AVDTP/Media OPEN, then immediately CLOSE without START or media payload'
if (-not $PSCmdlet.ShouldProcess($target, $action)) {
    return
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$logDirectory = Join-Path $transactionRoot "post-reboot-zero-packet-$stamp"
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$statePath = Join-Path $logDirectory 'state.json'
$agentLogPath = Join-Path $logDirectory 'agent.log'
$sessionPath = Join-Path $logDirectory 'session.json'
$resultPath = Join-Path $logDirectory 'result.json'
$transaction.status = 'running-post-reboot'
$transaction.phase = 'waiting-for-acl-render-demand'
$transaction.post_reboot = [ordered]@{
    boot_time_utc = $currentBoot.ToString('o')
    radio_state = 'ready'
    agent_log = $agentLogPath
    state = $statePath
    session = $sessionPath
    result = $resultPath
}
$transaction.updated_at = (Get-Date).ToString('o')
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath

Write-Host 'V1 zero-packet configuration agent armed.'
Write-Host 'Now turn on XM5 normally; do not toggle Windows Bluetooth.'
Write-Host 'After Native LDAC becomes available, select it and play one source.'
Write-Host "After the immediate AVDTP CLOSE completion message, stop playback and turn off XM5."
Write-Host 'Only OpenSignaling Win32 71 may retry at 15/30 seconds while the same ACL generation and RenderDemand remain valid. Protocol/configuration/media failures stop immediately; AVDTP START and media payload are forbidden.'

$agentPath = Join-Path $candidateRoot 'v1_presence_agent.exe'
$workerPath = Join-Path $candidateRoot `
    'v1_transport_configuration_worker.exe'
$agentExit = -1
$savedPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    $captured = @(
        & $agentPath `
            --run-for-ms ($DurationSeconds * 1000) `
            --state $statePath `
            --endpoint-presence `
            --observe-render-demand `
            --observe-engine-ready `
            --exercise-transport-configuration `
            --transport-result $sessionPath `
            --engine-executable $workerPath 2>&1 |
            Tee-Object -FilePath $agentLogPath |
            ForEach-Object {
                $line = [string]$_
                Write-Host $line
                $line
            }
    )
    $agentExit = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $savedPreference
}

$state = if (Test-Path -LiteralPath $statePath -PathType Leaf) {
    Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
} else {
    $null
}
$session = if (Test-Path -LiteralPath $sessionPath -PathType Leaf) {
    Get-Content -LiteralPath $sessionPath -Raw | ConvertFrom-Json
} else {
    $null
}
$attemptResults = @(Get-ChildItem `
    -LiteralPath (Split-Path -Parent $sessionPath) `
    -Filter ((Split-Path -Leaf $sessionPath) + '.attempt-*.json') `
    -File -ErrorAction SilentlyContinue | Sort-Object Name)
$attemptEvidence = @($attemptResults | ForEach-Object {
    Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
})
$passed = $null -ne $state -and $null -ne $session -and
    (Test-V1RebootConfigurationEvidence `
        -State $state `
        -Session $session `
        -Attempts $attemptEvidence `
        -AgentExitCode $agentExit)
$corePassed = $null -ne $state -and $null -ne $session -and
    (Test-V1RebootConfigurationCoreEvidence `
        -State $state `
        -Session $session `
        -Attempts $attemptEvidence `
        -AgentExitCode $agentExit)

$transaction.configuration = [ordered]@{
    passed = $passed
    core_passed = $corePassed
    agent_exit_code = $agentExit
    open_attempts = if ($null -eq $state) { 0 } else {
        [int]$state.transport_open_executed
    }
    signaling_exchanges = if ($null -eq $session) { 0 } else {
        [int]$session.signaling_exchanges
    }
    disposition = if ($null -eq $session) { 'no-result' } else {
        [string]$session.disposition
    }
    backend_error = if ($null -eq $session) { 0 } else {
        [int]$session.backend_error
    }
    attempt_results = @($attemptResults.FullName)
}

if (-not $passed) {
    if ($corePassed) {
        $transaction.status = 'finalization-required'
        $transaction.phase = 'wait-for-physical-disconnect'
        $transaction.error = 'The zero-packet transport contract passed, but the bounded agent ended before observing physical disconnect.'
        $transaction.updated_at = (Get-Date).ToString('o')
        Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
        throw "V1 zero-packet transport succeeded, but final physical-disconnect evidence is pending. Fully stop playback, turn off XM5, then run .\tools\complete-v1-zero-packet-gate.ps1 -ConfirmV1ZeroPacketCompletion. Do not rollback or rerun the transport gate. Transaction: $TransactionPath"
    }
    $transaction.error = 'The post-reboot zero-packet configuration contract failed.'
    $xm5State = Get-NativeLdacXm5BluetoothState `
        -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit)
    if ($xm5State -eq 'disconnected') {
        $transaction.rollback.attempted = $true
        try {
            $restored = Restore-V1RebootDiscoveryOriginalA2dp `
                -Transaction $transaction -LogDirectory $logDirectory
            $transaction.rollback.succeeded = $true
            $transaction.rollback.service = $restored.service
            $transaction.rollback.published_inf = $restored.published_inf
            $transaction.status = 'failed-and-restored'
            $transaction.phase = 'failed-and-restored'
        } catch {
            $transaction.rollback.error = $_.Exception.Message
            $transaction.status = 'rollback-failed'
            $transaction.phase = 'rollback-failed'
        }
    } else {
        $transaction.status = 'rollback-required'
        $transaction.phase = 'wait-for-physical-disconnect'
    }
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
    if ($transaction.rollback.succeeded) {
        throw "V1 post-reboot zero-packet configuration failed or exhausted its bounded policy and AltA2DP was restored. Result: $sessionPath"
    }
    throw "V1 post-reboot zero-packet configuration failed or exhausted its bounded policy. Turn off XM5, then run .\tools\rollback-v1-zero-packet-gate.ps1 -ConfirmV1ZeroPacketRollback. Transaction: $TransactionPath"
}

$finalPresence = @(& $endpointProbe --presence 2>&1)
$finalLink = @(& $endpointProbe --link-state 2>&1)
if (($finalPresence -join "`n") -notmatch
        '(?m)^Physical presence absent:' -or
    ($finalLink -join "`n") -notmatch '(?m)^Link disconnected:') {
    $transaction.status = 'rollback-required'
    $transaction.phase = 'post-configuration-endpoint-mismatch'
    $transaction.error = 'Zero-packet configuration succeeded, but endpoint cleanup evidence is inconsistent.'
    Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
    throw "Endpoint cleanup evidence failed. Keep XM5 off and run rollback. Transaction: $TransactionPath"
}

$result = [ordered]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    source_commit = [string]$manifest.source_commit
    passed = $true
    transaction = $TransactionPath
    state = $statePath
    session = $sessionPath
    remote_seid = [int]$session.remote_seid
    sample_rate_hz = [int]$session.sample_rate_hz
    channel_mode = [int]$session.channel_mode
    signaling_exchanges = [int]$session.signaling_exchanges
    outgoing_mtu = [int]$session.outgoing_mtu
    set_configuration_accepted = $session.set_configuration_accepted
    avdtp_open_accepted = $session.avdtp_open_accepted
    media_opened = $session.media_opened
    avdtp_close_accepted = $session.avdtp_close_accepted
    total_signaling_open_attempts = [int]$state.transport_open_executed
    retryable_open_failures = [int]$state.transport_retryable_failures
    attempt_results = @($attemptResults.FullName)
    actual_media_channel_opened = 1
    actual_avdtp_start_commands = 0
    actual_media_packets_sent = 0
    reboot_count = 1
    bluetooth_toggled = $false
    driver_left_installed = $true
}
$result | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $resultPath -Encoding UTF8
$transaction.status = 'configuration-verified'
$transaction.phase = 'complete'
$transaction.error = $null
$transaction.updated_at = (Get-Date).ToString('o')
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath

Write-Host 'V1 post-reboot zero-packet configuration passed.'
Write-Host "XM5 LDAC session: SEID $($session.remote_seid), $($session.sample_rate_hz) Hz, channel mode 0x$('{0:X2}' -f [int]$session.channel_mode), outgoing MTU $($session.outgoing_mtu)."
Write-Host "$($state.transport_open_executed) signaling OPEN attempt(s) occurred in one ACL generation; the successful worker completed SET_CONFIGURATION, AVDTP OPEN, Media L2CAP OPEN, AVDTP CLOSE, and local close."
Write-Host 'Only OpenSignaling Win32 71 used bounded recovery; AVDTP START, media payload, LinkState write, and Bluetooth toggle remained zero.'
Write-Host 'LdacNative remains installed for the next reviewed START-with-zero-gain gate; do not rerun this gate.'
Write-Host "Result: $resultPath"
