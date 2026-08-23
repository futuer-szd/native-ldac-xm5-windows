# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmLegacyPostRebootTransport,
    [string]$TransactionPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'native-ldac-baseline-common.ps1')
. (Join-Path $PSScriptRoot 'legacy-open-diagnostic-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmLegacyPostRebootTransport) {
    throw 'Refusing to open an AVDTP signaling session. Re-run with -ConfirmLegacyPostRebootTransport.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$transactionRoot = Join-Path $projectRoot 'artifacts\legacy-reboot'
if ([string]::IsNullOrWhiteSpace($TransactionPath)) {
    $latestTransactionPath = Join-Path $transactionRoot `
        'latest-transaction.txt'
    if (-not (Test-Path -LiteralPath $latestTransactionPath -PathType Leaf)) {
        throw 'No pending legacy reboot transaction was found.'
    }
    $TransactionPath = (Get-Content -LiteralPath $latestTransactionPath `
        -Raw).Trim()
}
$TransactionPath = [System.IO.Path]::GetFullPath($TransactionPath)
$transaction = Get-Content -LiteralPath $TransactionPath -Raw |
    ConvertFrom-Json
if ([int]$transaction.transaction_version -ne 1 -or
    [string]$transaction.status -ne 'awaiting_reboot' -or
    [string]$transaction.phase -ne 'installed_awaiting_reboot') {
    throw 'The selected transaction is not waiting for its first post-install reboot.'
}

$preparedBoot = [datetime]::Parse(
    [string]$transaction.prepared_boot_time_utc).ToUniversalTime()
$currentBoot = Get-NativeLdacCurrentBootTime
if ($currentBoot -le $preparedBoot.AddSeconds(1)) {
    throw 'Windows has not rebooted since LdacNative was installed. Keep the XM5 off and reboot before this gate.'
}

$candidatePath = [System.IO.Path]::GetFullPath(
    [string]$transaction.candidate_path)
$verifyScript = Join-Path $PSScriptRoot 'verify-legacy-candidate.ps1'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $verifyScript `
    -CandidatePath $candidatePath
if ($LASTEXITCODE -ne 0) {
    throw "Legacy candidate verification failed with exit code $LASTEXITCODE."
}
$manifest = Get-Content -LiteralPath `
    (Join-Path $candidatePath 'manifest.json') -Raw | ConvertFrom-Json
if ([string]$manifest.source_commit -ne
    [string]$transaction.candidate_source_commit) {
    throw 'The candidate no longer matches the pending transaction.'
}

$processes = @(Get-NativeLdacWorkspaceProcesses)
$tasks = @(Get-ScheduledTask -TaskName 'Native LDAC Agent' `
    -ErrorAction SilentlyContinue)
if ($processes.Count -ne 0 -or $tasks.Count -ne 0) {
    throw 'No LDAC task or media process may be active during the post-reboot transport gate.'
}
$audioPackages = @(Get-LegacyDriverPackages `
    -OriginalInfNames @('NativeLdacAudio.inf'))
$rootDevices = @(Get-NativeLdacRootDevices | Where-Object { $_.present })
if ($audioPackages.Count -ne 0 -or $rootDevices.Count -ne 0) {
    throw 'The clean transport-only gate does not permit a Native audio endpoint.'
}

$altService = Get-NativeLdacAltA2dpUserService
if ($null -eq $altService -or $altService.state -ne 'Stopped' -or
    $altService.start_mode -ne 'Manual' -or
    $altService.process_id -ne 0) {
    throw 'Alternative A2DP Service must remain Manual/Stopped for this isolated gate.'
}

$connectionProbePath = Join-Path $candidatePath `
    'xm5_connection_probe.exe'
if ((Get-NativeLdacXm5BluetoothState -ProbePath $connectionProbePath `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne
    'disconnected') {
    throw 'The XM5 must still be powered off at post-reboot preflight.'
}

$target = 'one post-reboot XM5 AVDTP DISCOVER transaction'
$action = 'Wait for one real XM5 ACL connect event, require explicit physical power-on confirmation and one healthy LdacNative PDO, issue one bounded signaling OPEN/DISCOVER, close signaling, and leave the installed driver unchanged'
if (-not $PSCmdlet.ShouldProcess($target, $action)) {
    return
}

$logDirectory = Join-Path (Split-Path -Parent $TransactionPath) `
    ([System.IO.Path]::GetFileNameWithoutExtension($TransactionPath))
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$transaction.status = 'running_post_reboot'
$transaction.phase = 'wait_for_fresh_connection'
$transaction.post_reboot = [ordered]@{
    boot_time_utc = $currentBoot.ToString('o')
    installed = $null
    transport_info = $null
    acl_event_observed_at = $null
    physical_power_on_confirmed_at = $null
    connected_at = $null
}
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath

Write-Host 'The post-reboot environment is clean and Alternative A2DP Service is isolated.'
Write-Host 'The ACL watcher is armed. Physically turn on the XM5 now and wait for its power-on voice prompt.'
$aclEventLog = Join-Path $logDirectory 'acl-connect-event.log'
$aclEvent = Invoke-LegacyNativeCapture -FilePath $connectionProbePath `
    -Arguments @('--wait-acl-connect', '90')
$aclEventText = $aclEvent.stdout + $aclEvent.stderr
$aclEventText | Set-Content -LiteralPath $aclEventLog -Encoding UTF8
if ($aclEvent.exit_code -ne 0 -or
    $aclEventText -notmatch '(?m)^XM5 ACL event: connected\.\r?$') {
    $transaction.status = 'transport_failed_rollback_required'
    $transaction.phase = 'failed'
    $transaction.error =
        "No real XM5 ACL connect event was observed within 90 seconds; watcher exit code $($aclEvent.exit_code)."
    Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
    throw "$($transaction.error) Turn the XM5 off and run rollback-legacy-reboot-gate.ps1."
}
$transaction.post_reboot.acl_event_observed_at = (Get-Date).ToString('o')
$transaction.post_reboot.acl_event_log = $aclEventLog
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath

Write-Host 'A real ACL connection event for the selected XM5 was observed.'
$physicalConfirmation = Read-Host `
    'Type XM5-ON only if you just pressed the XM5 power button and heard its power-on voice prompt; otherwise press Ctrl+C'
if ($physicalConfirmation -cne 'XM5-ON') {
    $transaction.status = 'transport_failed_rollback_required'
    $transaction.phase = 'failed'
    $transaction.error =
        'The operator did not confirm a physical XM5 power-on; no L2CAP OPEN was submitted.'
    Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
    throw "$($transaction.error) Turn the XM5 off and run rollback-legacy-reboot-gate.ps1."
}
$transaction.post_reboot.physical_power_on_confirmed_at =
    (Get-Date).ToString('o')
if ((Get-NativeLdacXm5BluetoothState -ProbePath $connectionProbePath `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne
    'connected') {
    $transaction.status = 'transport_failed_rollback_required'
    $transaction.phase = 'failed'
    $transaction.error =
        'The ACL event was observed, but the verified XM5 state is no longer connected; no L2CAP OPEN was submitted.'
    Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
    throw "$($transaction.error) Turn the XM5 off and run rollback-legacy-reboot-gate.ps1."
}
$transaction.post_reboot.connected_at = (Get-Date).ToString('o')
$installed = Wait-LegacyXm5A2dpService -ExpectedService 'LdacNative' `
    -TimeoutSeconds 30
if ($null -eq $installed -or $installed.problem_code -ne 0) {
    $transaction.status = 'transport_failed_rollback_required'
    $transaction.phase = 'failed'
    $transaction.error =
        'A fresh physical connection was observed, but one healthy LdacNative PDO did not enumerate within 30 seconds.'
    Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
    throw "$($transaction.error) Turn the XM5 off and run rollback-legacy-reboot-gate.ps1."
}
$transaction.post_reboot.installed = $installed

$probePath = Join-Path $candidatePath 'transport_probe.exe'
$info = Invoke-LegacyNativeCapture -FilePath $probePath `
    -Arguments @('--info')
$infoText = $info.stdout + $info.stderr
if ($info.exit_code -ne 0 -or
    $infoText -notmatch '(?m)^Driver ABI: 0\.5,' -or
    $infoText -notmatch '(?m)^Ready flags: 0x00000007\r?$') {
    $transaction.status = 'transport_failed_rollback_required'
    $transaction.phase = 'failed'
    $transaction.error =
        'The newly enumerated post-reboot transport does not expose ABI 0.5 / ready flags 7.'
    Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
    throw "$($transaction.error) Turn the XM5 off and run rollback-legacy-reboot-gate.ps1."
}
$transaction.post_reboot.transport_info = $infoText.Trim()
$transaction.phase = 'discover'
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
Start-Sleep -Seconds 3

$traceRoot = Join-Path $logDirectory 'bluetooth-trace'
$traceScript = Join-Path $PSScriptRoot 'capture-bluetooth-trace.ps1'
$discover = Invoke-LegacyNativeCapture -FilePath 'powershell.exe' `
    -Arguments @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $traceScript,
        '-ProbePath', $probePath,
        '-OutputRoot', $traceRoot)
$discoverText = $discover.stdout + $discover.stderr
$discoverLog = Join-Path $logDirectory 'post-reboot-discover.log'
$discoverText | Set-Content -LiteralPath $discoverLog -Encoding UTF8
$capturePath = if ($discoverText -match
    '(?m)^Bluetooth trace captured at: (.+)\r?$') {
    $Matches[1].Trim()
} else {
    $null
}
$discoveryPassed = $discover.exit_code -eq 0 -and
    $discoverText -match '(?m)^Selected LDAC audio sink SEID: \d+\r?$' -and
    $discoverText -match '(?m)^Signaling channel closed\.\r?$'
$diagnosticSummary = Get-LegacyOpenDiagnosticSummary `
    -Text $discoverText -DiscoveryPassed $discoveryPassed
$transaction.discovery = [ordered]@{
    exit_code = $discover.exit_code
    selected_ldac_sink = $discoverText -match
        '(?m)^Selected LDAC audio sink SEID: \d+\r?$'
    signaling_closed = $discoverText -match
        '(?m)^Signaling channel closed\.\r?$'
    open_diagnostic_reported =
        $diagnosticSummary.open_diagnostic_reported
    open_diagnostic = $diagnosticSummary.open_diagnostic
    remote_response = $diagnosticSummary.remote_response
    diagnostic_disposition =
        $diagnosticSummary.diagnostic_disposition
    trace_root = $traceRoot
    trace_capture = $capturePath
    log = $discoverLog
}
if ($discoveryPassed) {
    $transaction.status = 'transport_verified'
    $transaction.phase = 'complete'
    $transaction.error = $null
} else {
    $transaction.status = 'transport_failed_rollback_required'
    $transaction.phase = 'failed'
    $transaction.error =
        "Post-reboot DISCOVER failed with exit code $($discover.exit_code); diagnostic disposition: $($diagnosticSummary.diagnostic_disposition)."
}
$transaction.completed_at = (Get-Date).ToString('o')
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath

if (-not $discoveryPassed) {
    throw "$($transaction.error) Turn the XM5 off, then run rollback-legacy-reboot-gate.ps1. No retry was attempted. Log: $discoverLog"
}

Write-Host 'Post-reboot legacy transport gate passed.'
Write-Host 'Exactly one signaling OPEN/DISCOVER completed and the signaling channel closed normally.'
Write-Host 'No audio endpoint, encoder, media channel, or playback session was started.'
Write-Host 'Leave the current driver installed and report this result before proceeding to endpoint playback.'
Write-Host "Transaction: $TransactionPath"
