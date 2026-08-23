# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Medium')]
param(
    [switch]$ConfirmV1ZeroPacketCompletion,
    [string]$TransactionPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'v1-reboot-discovery-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1ZeroPacketCompletion) {
    throw 'Refusing to finalize the zero-packet evidence. Re-run with -ConfirmV1ZeroPacketCompletion.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$trialRoot = Join-Path $projectRoot `
    'artifacts\v1-reboot-discovery\trial'
if ([string]::IsNullOrWhiteSpace($TransactionPath)) {
    $latestPath = Join-Path $trialRoot 'latest-transaction.txt'
    if (-not (Test-Path -LiteralPath $latestPath -PathType Leaf)) {
        throw 'No V1 zero-packet transaction was found.'
    }
    $TransactionPath = (Get-Content -LiteralPath $latestPath -Raw).Trim()
}
$TransactionPath = [System.IO.Path]::GetFullPath($TransactionPath)
$transaction = Get-Content -LiteralPath $TransactionPath -Raw |
    ConvertFrom-Json
$gateKindProperty = $transaction.PSObject.Properties['gate_kind']
if ([int]$transaction.schema_version -ne 1 -or
    $null -eq $gateKindProperty -or
    [string]$gateKindProperty.Value -ne 'zero-packet-configuration' -or
    [string]$transaction.status -notin @(
        'rollback-required',
        'finalization-required') -or
    $null -eq $transaction.configuration -or
    $null -eq $transaction.post_reboot -or
    $transaction.rollback.attempted -ne $false) {
    throw 'The selected transaction is not eligible for evidence-only completion.'
}

$candidate = Get-V1RebootDiscoveryCandidate `
    -CandidatePath ([string]$transaction.candidate_path)
if ([string]$candidate.manifest.source_commit -ne
    [string]$transaction.source_commit) {
    throw 'The stored candidate no longer matches the transaction.'
}
$connectionProbe = Join-Path $candidate.root 'xm5_connection_probe.exe'
if ((Get-NativeLdacBluetoothRadioState `
        -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$transaction.source_commit)) -ne
    'ready') {
    throw 'Windows Bluetooth must remain on for the physical-disconnect check.'
}
if ((Get-NativeLdacXm5BluetoothState `
        -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$transaction.source_commit)) -ne
    'disconnected') {
    throw 'Fully stop playback, turn off XM5, and wait for physical disconnection before finalizing.'
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
    throw 'The installed LdacNative plus V1 endpoint baseline is not healthy. Keep XM5 off and use rollback.'
}

$endpointProbe = Join-Path $candidate.root 'audio_endpoint_probe.exe'
$presence = @(& $endpointProbe --presence 2>&1)
$link = @(& $endpointProbe --link-state 2>&1)
$pcm = @(& $endpointProbe --info 2>&1)
if (($presence -join "`n") -notmatch '(?m)^Physical presence absent:' -or
    ($link -join "`n") -notmatch '(?m)^Link disconnected:' -or
    ($pcm -join "`n") -notmatch '(?m)^Stream idle(?:,|:)') {
    throw 'The Native endpoint is not fully absent, link-disconnected, and render-idle. Fully exit the player and wait a few seconds.'
}

$statePath = [string]$transaction.post_reboot.state
$sessionPath = [string]$transaction.post_reboot.session
if (-not (Test-Path -LiteralPath $statePath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $sessionPath -PathType Leaf)) {
    throw 'The zero-packet state or session evidence is missing.'
}
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$session = Get-Content -LiteralPath $sessionPath -Raw | ConvertFrom-Json
$attemptPaths = @($transaction.configuration.attempt_results)
if ($attemptPaths.Count -eq 0 -or
    @($attemptPaths | Where-Object {
        -not (Test-Path -LiteralPath ([string]$_) -PathType Leaf)
    }).Count -ne 0) {
    throw 'One or more zero-packet attempt archives are missing.'
}
$attempts = @($attemptPaths | ForEach-Object {
    Get-Content -LiteralPath ([string]$_) -Raw | ConvertFrom-Json
})
if (-not (Test-V1RebootConfigurationCoreEvidence `
        -State $state `
        -Session $session `
        -Attempts $attempts `
        -AgentExitCode ([int]$transaction.configuration.agent_exit_code))) {
    throw 'The recorded transport evidence does not satisfy the zero-packet core contract. Keep XM5 off and use rollback.'
}

Write-Host 'V1 zero-packet core evidence and current physical disconnect are verified.'
Write-Host 'This completion writes only result/transaction JSON and leaves LdacNative installed.'
$target = $TransactionPath
$action = 'Accept the already completed zero-packet transport session after current physical disconnect verification'
if (-not $PSCmdlet.ShouldProcess($target, $action)) {
    return
}

$resultPath = [string]$transaction.post_reboot.result
$result = [ordered]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    source_commit = [string]$transaction.source_commit
    passed = $true
    finalized_after_agent_deadline = $true
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
    attempt_results = @($attemptPaths)
    actual_media_channel_opened = 1
    actual_avdtp_start_commands = 0
    actual_media_packets_sent = 0
    acl_disconnect_observed_by_agent = $false
    current_physical_disconnect_verified = $true
    reboot_count = 1
    bluetooth_toggled = $false
    driver_left_installed = $true
}
Write-LegacyJsonAtomic -Value $result -Path $resultPath
$transaction.configuration.passed = $true
$transaction.configuration | Add-Member -NotePropertyName core_passed `
    -NotePropertyValue $true -Force
$transaction.configuration | Add-Member `
    -NotePropertyName finalized_after_agent_deadline `
    -NotePropertyValue $true -Force
$transaction.configuration | Add-Member `
    -NotePropertyName current_physical_disconnect_verified `
    -NotePropertyValue $true -Force
$transaction.status = 'configuration-verified'
$transaction.phase = 'complete'
$transaction.error = $null
$transaction.updated_at = (Get-Date).ToString('o')
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath

Write-Host 'V1 zero-packet configuration gate finalized successfully.'
Write-Host "SEID $($session.remote_seid), $($session.sample_rate_hz) Hz, outgoing MTU $($session.outgoing_mtu); START 0, media packets 0."
Write-Host 'LdacNative remains installed. No driver, service, Bluetooth radio, or endpoint setting was changed.'
Write-Host "Result: $resultPath"
