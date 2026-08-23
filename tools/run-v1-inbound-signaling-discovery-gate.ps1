# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1InboundSignalingDiscovery,
    [ValidateRange(120,300)][int]$DurationSeconds = 180,
    [string]$TransactionPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'v1-inbound-signaling-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1InboundSignalingDiscovery) {
    throw 'Refusing to run the inbound-signaling discovery gate. Re-run with -ConfirmV1InboundSignalingDiscovery.'
}
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$trialRoot = Join-Path $root 'artifacts\v1-inbound-signaling\trial'
if ([string]::IsNullOrWhiteSpace($TransactionPath)) {
    $latest = Join-Path $trialRoot 'latest-transaction.txt'
    if (-not (Test-Path -LiteralPath $latest -PathType Leaf)) {
        throw 'No prepared inbound-signaling transaction was found.'
    }
    $TransactionPath = (Get-Content -LiteralPath $latest -Raw).Trim()
}
$TransactionPath = [System.IO.Path]::GetFullPath($TransactionPath)
$transaction = Get-Content -LiteralPath $TransactionPath -Raw |
    ConvertFrom-Json
if ([int]$transaction.schema_version -ne 1 -or
    [int]$transaction.transport_policy_version -ne
        $script:V1InboundSignalingPolicyVersion -or
    [string]$transaction.status -ne 'driver-updated-ready') {
    throw 'The selected transaction is not awaiting inbound discovery validation.'
}
$candidate = Get-V1InboundSignalingCandidate `
    -CandidatePath ([string]$transaction.candidate_path)
$manifest = $candidate.manifest
$head = (& git.exe -C $root rev-parse HEAD).Trim()
$gitStatus = @(& git.exe -C $root status --porcelain)
if ($LASTEXITCODE -ne 0 -or $gitStatus.Count -ne 0 -or
    $head -ne [string]$manifest.source_commit -or
    [string]$transaction.driver_tree -ne [string]$manifest.driver_tree) {
    throw 'The transaction candidate must match the current clean Git HEAD.'
}
$connectionProbe = Join-Path $candidate.root 'xm5_connection_probe.exe'
$transportProbe = Join-Path $candidate.root 'transport_probe.exe'
if ((Get-NativeLdacBluetoothRadioState -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne 'ready') {
    throw 'Windows Bluetooth is off or unavailable.'
}
if ((Get-NativeLdacXm5BluetoothState -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne
    'disconnected') {
    throw 'Turn off XM5 before arming this gate.'
}
$devices = @(Get-LegacyXm5A2dpDevices)
if ($devices.Count -ne 1) {
    throw 'Exactly one present XM5 A2DP Sink service PDO is required.'
}
$binding = Get-LegacyXm5A2dpSnapshot -Device $devices[0]
if ([string]$binding.service -ne 'LdacNative' -or
    [int]$binding.problem_code -ne 0 -or
    -not ([string]$binding.published_inf).Equals(
        [string]$transaction.installed_inf,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The prepared inbound-signaling driver binding changed.'
}
$ready = Wait-V1InboundTransportInfo -ProbePath $transportProbe `
    -ExpectedFlags $script:V1InboundReadyFlags -TimeoutSeconds 2
if ($null -eq $ready) {
    throw 'The prepared driver no longer exposes ABI 0.5 ready flags 0xF.'
}

$channels = @(
    'Microsoft-Windows-BTH-BTHPORT/HCI',
    'Microsoft-Windows-BTH-BTHPORT/L2CAP')
$channelStates = @{}
foreach ($channel in $channels) {
    $configuration = @(& wevtutil.exe gl $channel 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to query Bluetooth analytic channel: $channel"
    }
    $channelStates[$channel] = [bool](
        $configuration -match '^enabled:\s*true\s*$')
}

Write-Host 'V1 inbound-signaling DISCOVER preflight passed.'
Write-Host 'This gate sends one AVDTP DISCOVER only: no SET_CONFIGURATION, AVDTP OPEN/START, Media L2CAP, or media packet.'
Write-Host 'It must prove XM5 initiated PSM 0x0019, Windows completed SUCCESS, and the probe reused that channel without a second outbound PSM 0x0019 request.'
Write-Host 'No playback is needed. Do not toggle Windows Bluetooth.'
if (-not $PSCmdlet.ShouldProcess(
        'one XM5 inbound AVDTP signaling handoff with HCI/L2CAP capture',
        'Wait for one physical ACL connection, require the accepted inbound channel, run one DISCOVER, close locally, capture proof, and rollback the driver if validation fails')) {
    return
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$directory = Join-Path $trialRoot "validation-$stamp"
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$captureStart = Get-Date
$transaction.status = 'running-validation'
$transaction.phase = 'waiting-for-xm5-inbound-signaling'
$transaction.updated_at = (Get-Date).ToString('o')
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath

$connect = $null
$disconnect = $null
$aclDisconnectObservedAt = $null
$publicDisconnect = $null
$incomingDiagnostic = $null
$lastIncomingDiagnostic = $null
$discover = $null
$finalDiagnostic = $null
$channelResults = @()
$captureFailure = $null
try {
    foreach ($channel in $channels) {
        if (-not $channelStates[$channel]) {
            & wevtutil.exe sl $channel /e:true /q:true
            if ($LASTEXITCODE -ne 0) {
                throw "Unable to enable Bluetooth analytic channel: $channel"
            }
        }
    }
    Write-Host 'The ACL watcher will tell you when it is armed; then turn on XM5. Do not start or select any audio source.'
    $connect = Invoke-V1InboundStreamingAclProbe `
        -ProbePath $connectionProbe `
        -Arguments @('--wait-acl-connect', [string]$DurationSeconds)
    if ($connect.exit_code -ne 0) {
        throw 'No bounded physical XM5 ACL connection was observed.'
    }

    $diagnosticDeadline = (Get-Date).AddSeconds(5)
    do {
        $probeResult = Invoke-V1InboundTransportProbe `
            -ProbePath $transportProbe -Arguments @('--open-diagnostics')
        $lastIncomingDiagnostic = $probeResult
        $probeText = $probeResult.stdout + $probeResult.stderr
        if ($probeResult.exit_code -eq 0 -and
            $probeText -match
                '(?m)^Signaling channel direction: inbound\.\r?$' -and
            $probeText -match
                '(?m)^L2CAP OPEN state: completed, succeeded\.\r?$') {
            $incomingDiagnostic = $probeResult
            break
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $diagnosticDeadline)
    if ($null -eq $incomingDiagnostic) {
        if ($null -ne $lastIncomingDiagnostic) {
            ($lastIncomingDiagnostic.stdout +
                $lastIncomingDiagnostic.stderr) | Set-Content `
                -LiteralPath (Join-Path $directory `
                    'incoming-open-diagnostics-last.log') -Encoding UTF8
        }
        throw 'The driver did not publish a completed inbound signaling channel within five seconds of ACL connect.'
    }
    ($incomingDiagnostic.stdout + $incomingDiagnostic.stderr) |
        Set-Content -LiteralPath (Join-Path $directory `
            'incoming-open-diagnostics.log') -Encoding UTF8

    $transaction.phase = 'running-single-discover'
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
    $discover = Invoke-V1InboundTransportProbe -ProbePath $transportProbe `
        -Arguments @('--discover', '--open-attempts', '1')
    $discoverText = $discover.stdout + $discover.stderr
    $discoverText | Write-Host
    $discoverText | Set-Content -LiteralPath `
        (Join-Path $directory 'discover.log') -Encoding UTF8
    $finalDiagnostic = Invoke-V1InboundTransportProbe `
        -ProbePath $transportProbe -Arguments @('--open-diagnostics')
    ($finalDiagnostic.stdout + $finalDiagnostic.stderr) |
        Set-Content -LiteralPath (Join-Path $directory `
            'final-open-diagnostics.log') -Encoding UTF8
} catch {
    $captureFailure = $_.Exception.Message
} finally {
    if ($null -ne $connect -and $connect.exit_code -eq 0) {
        Write-Host 'The ACL watcher will tell you when it is armed; then turn off XM5.'
        $disconnect = Invoke-V1InboundStreamingAclProbe `
            -ProbePath $connectionProbe `
            -Arguments @('--wait-acl-disconnect', '30')
    } else {
        $stateCapture = Invoke-V1InboundTransportProbe `
            -ProbePath $connectionProbe -Arguments @('--state')
        $disconnect = [pscustomobject]@{
            exit_code = if ($stateCapture.exit_code -eq 10) { 0 } else { 1 }
            stdout = $stateCapture.stdout
            stderr = $stateCapture.stderr
        }
        ($disconnect.stdout + $disconnect.stderr) | Write-Host
    }
    if ($disconnect.exit_code -eq 0) {
        $aclDisconnectObservedAt = (Get-Date).ToString('o')
    }
    Write-Host 'Waiting for the Windows public XM5 state to converge to disconnected.'
    $publicDisconnect = Wait-V1InboundPublicDisconnect `
        -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit) `
        -TimeoutSeconds 30
    $publicDisconnect | ConvertTo-Json -Depth 3 |
        Set-Content -LiteralPath (Join-Path $directory `
            'public-disconnect-convergence.json') -Encoding UTF8
    if ($publicDisconnect.disconnected) {
        Write-Host ("Windows public XM5 state converged to disconnected " +
            "after $($publicDisconnect.elapsed_ms) ms and " +
            "$($publicDisconnect.poll_attempts) poll(s).")
    } else {
        Write-Host ("Windows public XM5 state did not converge within " +
            "$($publicDisconnect.elapsed_ms) ms; no PnP rollback is allowed.")
    }
    foreach ($channel in $channels) {
        $safe = $channel -replace '[^A-Za-z0-9.-]', '_'
        $evtx = Join-Path $directory "$safe.evtx"
        $xml = Join-Path $directory "$safe.xml"
        & wevtutil.exe epl $channel $evtx /ow:true
        $events = @(Get-WinEvent -FilterHashtable @{
            LogName = $channel
            StartTime = $captureStart
        } -Oldest -ErrorAction SilentlyContinue)
        @($events | ForEach-Object { $_.ToXml() }) |
            Set-Content -LiteralPath $xml -Encoding UTF8
        $channelResults += [pscustomobject][ordered]@{
            channel = $channel
            enabled_before = [bool]$channelStates[$channel]
            event_count = $events.Count
            evtx = $evtx
            xml = $xml
        }
    }
    foreach ($channel in $channels) {
        if (-not $channelStates[$channel]) {
            & wevtutil.exe sl $channel /e:false | Out-Null
        }
    }
}

$hci = @($channelResults | Where-Object {
    $_.channel -like '*/HCI'
})[0]
$summaryPath = Join-Path $directory 'l2cap-summary.json'
& (Join-Path $PSScriptRoot 'summarize-bluetooth-l2cap-trace.ps1') `
    -InputPath $hci.xml -OutputPath $summaryPath | Out-Null
$summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
$discoverText = if ($null -eq $discover) { '' } else {
    $discover.stdout + $discover.stderr
}
$finalDiagnosticText = if ($null -eq $finalDiagnostic) { '' } else {
    $finalDiagnostic.stdout + $finalDiagnostic.stderr
}
$corePassed = Test-V1InboundDiscoveryCoreEvidence `
    -CaptureFailure $captureFailure `
    -ConnectExitCode $(if ($null -eq $connect) { -1 } else {
        [int]$connect.exit_code
    }) `
    -DiscoverExitCode $(if ($null -eq $discover) { -1 } else {
        [int]$discover.exit_code
    }) `
    -DiscoverText $discoverText `
    -FinalDiagnosticText $finalDiagnosticText `
    -Summary $summary `
    -SetConfigurationCommands 0 `
    -AvdtpOpenCommands 0 `
    -AvdtpStartCommands 0 `
    -MediaL2capOpenCommands 0 `
    -MediaPackets 0
$passed = $corePassed -and
    $null -ne $disconnect -and $disconnect.exit_code -eq 0 -and
    $null -ne $publicDisconnect -and $publicDisconnect.disconnected

$resultPath = Join-Path $directory 'result.json'
$result = [ordered]@{
    schema_version = 1
    transport_policy_version = $script:V1InboundSignalingPolicyVersion
    source_commit = [string]$manifest.source_commit
    driver_tree = [string]$manifest.driver_tree
    passed = $passed
    core_passed = $corePassed
    transaction = $TransactionPath
    capture_failure = $captureFailure
    discover_exit_code = if ($null -eq $discover) { $null } else {
        [int]$discover.exit_code
    }
    acl_connect_observed = $null -ne $connect -and $connect.exit_code -eq 0
    acl_disconnect_observed =
        $null -ne $disconnect -and $disconnect.exit_code -eq 0
    acl_disconnect_observed_at = $aclDisconnectObservedAt
    public_bluetooth_disconnect_observed =
        $null -ne $publicDisconnect -and $publicDisconnect.disconnected
    public_bluetooth_disconnect_started_at =
        if ($null -eq $publicDisconnect) {
            $null
        } else {
            [string]$publicDisconnect.started_at
        }
    public_bluetooth_disconnect_converged_at =
        if ($null -eq $publicDisconnect) {
            $null
        } else {
            $publicDisconnect.converged_at
        }
    public_bluetooth_disconnect_elapsed_ms =
        if ($null -eq $publicDisconnect) {
            $null
        } else {
            [long]$publicDisconnect.elapsed_ms
        }
    public_bluetooth_disconnect_poll_attempts =
        if ($null -eq $publicDisconnect) {
            0
        } else {
            [int]$publicDisconnect.poll_attempts
        }
    public_bluetooth_disconnect_last_exit_code =
        if ($null -eq $publicDisconnect) {
            $null
        } else {
            $publicDisconnect.last_exit_code
        }
    inbound_open_diagnostic = $finalDiagnosticText
    l2cap_summary = $summaryPath
    inbound_avdtp_connection_requests =
        [int]$summary.inbound_avdtp_connection_requests
    outbound_avdtp_connection_requests =
        [int]$summary.outbound_avdtp_connection_requests
    outbound_success_responses_to_inbound_avdtp =
        [int]$summary.outbound_success_responses_to_inbound_avdtp
    inbound_no_resources_responses =
        [int]$summary.inbound_no_resources_responses
    set_configuration_commands = 0
    avdtp_open_commands = 0
    avdtp_start_commands = 0
    media_l2cap_open_commands = 0
    media_packets = 0
    driver_installed_or_updated = $true
    rebooted = $false
    bluetooth_toggled = $false
}
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resultPath `
    -Encoding UTF8
$transaction.validation = [ordered]@{
    passed = $passed
    directory = $directory
    result = $resultPath
    l2cap_summary = $summaryPath
    acl_disconnect_observed_at = $aclDisconnectObservedAt
    public_bluetooth_disconnect_observed =
        $null -ne $publicDisconnect -and $publicDisconnect.disconnected
    public_bluetooth_disconnect_converged_at =
        if ($null -eq $publicDisconnect) {
            $null
        } else {
            $publicDisconnect.converged_at
        }
    public_bluetooth_disconnect_elapsed_ms =
        if ($null -eq $publicDisconnect) {
            $null
        } else {
            [long]$publicDisconnect.elapsed_ms
        }
    public_bluetooth_disconnect_poll_attempts =
        if ($null -eq $publicDisconnect) {
            0
        } else {
            [int]$publicDisconnect.poll_attempts
        }
}
if ($passed) {
    $transaction.status = 'inbound-discovery-verified'
    $transaction.phase = 'inbound-discovery-verified'
    $transaction.error = $null
} elseif ($corePassed) {
    $transaction.status = 'finalization-required'
    $transaction.phase = 'awaiting-delayed-physical-disconnect'
    $transaction.error =
        'Inbound DISCOVER core evidence passed, but physical disconnect did not complete inside the gate window.'
} else {
    $transaction.status = 'validation-failed'
    $transaction.phase = 'validation-failed'
    $transaction.error = if ($null -ne $captureFailure) {
        $captureFailure
    } elseif ($null -eq $publicDisconnect -or
        -not $publicDisconnect.disconnected) {
        'Windows public Bluetooth state did not converge to disconnected within the bounded wait; PnP rollback was not attempted.'
    } else {
        'The inbound signaling HCI/DISCOVER evidence contract failed.'
    }
}
$transaction.updated_at = (Get-Date).ToString('o')
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath

if (-not $passed) {
    if ($corePassed) {
        $transaction.updated_at = (Get-Date).ToString('o')
        Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
        throw "V1 inbound-signaling DISCOVER core evidence passed, but physical disconnect was delayed. Keep XM5 off and run .\tools\complete-v1-inbound-signaling-gate.ps1 -ConfirmV1InboundSignalingCompletion after Windows reports disconnected. Transaction: $TransactionPath"
    } elseif ($null -ne $publicDisconnect -and
        $publicDisconnect.disconnected) {
        $transaction.rollback.attempted = $true
        try {
            $rollbackDirectory = Join-Path $directory 'rollback'
            New-Item -ItemType Directory -Path $rollbackDirectory -Force |
                Out-Null
            $restored = Restore-V1InboundPreviousDriver `
                -Transaction $transaction -LogDirectory $rollbackDirectory
            $transaction.rollback.succeeded = $true
            $transaction.rollback.restored_inf =
                [string]$restored.published_inf
            $transaction.status = 'validation-failed-and-restored'
            $transaction.phase = 'validation-failed-and-restored'
        } catch {
            $transaction.rollback.error = $_.Exception.Message
            $transaction.status = 'rollback-failed'
            $transaction.phase = 'rollback-failed'
        }
    } else {
        $transaction.status = 'rollback-required'
        $transaction.phase = 'turn-off-xm5-before-rollback'
    }
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
    throw "V1 inbound-signaling DISCOVER failed: $($transaction.error) Status: $($transaction.status). Result: $resultPath"
}

Write-Host 'V1 inbound-signaling DISCOVER gate passed.'
Write-Host "XM5 inbound PSM 0x0019 requests: $($result.inbound_avdtp_connection_requests); Windows SUCCESS responses: $($result.outbound_success_responses_to_inbound_avdtp)."
Write-Host 'Outbound PSM 0x0019 requests: 0; remote NO_RESOURCES: 0.'
Write-Host 'DISCOVER completed and signaling closed; SET_CONFIGURATION, media OPEN, START, and media packets remained zero.'
Write-Host 'No reboot or rollback is required. Keep the updated LdacNative installed.'
Write-Host "Result: $resultPath"
