# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1InboundPnpRundown,
    [switch]$ResumeAfterCycle1,
    [ValidateRange(180,420)][int]$DurationSeconds = 240,
    [string]$TransactionPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'v1-inbound-pnp-rundown-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1InboundPnpRundown) {
    throw 'Refusing to run the two-cycle PnP-rundown gate. Re-run with -ConfirmV1InboundPnpRundown.'
}

$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$trialRoot = Join-Path $root 'artifacts\v1-inbound-pnp-rundown\trial'
if ([string]::IsNullOrWhiteSpace($TransactionPath)) {
    $latestPath = Join-Path $trialRoot 'latest-transaction.txt'
    if (-not (Test-Path -LiteralPath $latestPath -PathType Leaf)) {
        throw 'No prepared PnP-rundown transaction was found.'
    }
    $TransactionPath = (Get-Content -LiteralPath $latestPath -Raw).Trim()
}
$TransactionPath = [System.IO.Path]::GetFullPath($TransactionPath)
$transaction = Get-Content -LiteralPath $TransactionPath -Raw |
    ConvertFrom-Json
$transactionIsResumeable = $ResumeAfterCycle1 -and
    [int]$transaction.schema_version -eq 1 -and
    [int]$transaction.transport_policy_version -eq
        $script:V1InboundPnpRundownPolicyVersion -and
    [string]$transaction.status -eq 'rollback-required' -and
    [string]$transaction.phase -eq
        'validation-failed-xm5-disconnected' -and
    $transaction.rollback.attempted -eq $false -and
    @($transaction.cycles).Count -eq 1 -and
    [int]$transaction.cycles[0].cycle -eq 1 -and
    [string]$transaction.cycles[0].failure -eq
        'Cycle 1 evidence did not satisfy the PnP-rundown contract.'
$transactionIsFresh = -not $ResumeAfterCycle1 -and
    [int]$transaction.schema_version -eq 1 -and
    [int]$transaction.transport_policy_version -eq
        $script:V1InboundPnpRundownPolicyVersion -and
    [string]$transaction.status -eq 'reboot-required'
if (-not $transactionIsResumeable -and -not $transactionIsFresh) {
    throw 'The selected transaction is not eligible for this PnP-rundown run or cycle-2 resume.'
}

$candidate = Get-V1InboundPnpRundownCandidate `
    -CandidatePath ([string]$transaction.candidate_path)
$manifest = $candidate.manifest
$head = (& git.exe -C $root rev-parse HEAD).Trim()
$headExit = $LASTEXITCODE
$headDriverTree = (& git.exe -C $root rev-parse 'HEAD:driver').Trim()
$treeExit = $LASTEXITCODE
$gitStatus = @(& git.exe -C $root status --porcelain)
$statusExit = $LASTEXITCODE
if ($headExit -ne 0 -or $treeExit -ne 0 -or $statusExit -ne 0 -or
    $gitStatus.Count -ne 0 -or
    ((-not $ResumeAfterCycle1) -and
        $head -ne [string]$manifest.source_commit) -or
    ($ResumeAfterCycle1 -and
        ($headDriverTree -ne [string]$manifest.driver_tree -or
         [string]$transaction.source_commit -ne
            [string]$manifest.source_commit)) -or
    [string]$transaction.driver_tree -ne [string]$manifest.driver_tree) {
    throw 'The PnP-rundown transaction must match the current clean source and approved driver tree.'
}

$currentBoot = Get-V1InboundPnpCurrentBootTime
if ([datetime]$currentBoot -le [datetime]$transaction.boot_time_before) {
    throw 'Restart Windows exactly once before running the PnP-rundown gate.'
}
if ($ResumeAfterCycle1 -and
    [datetime]$currentBoot -ne [datetime]$transaction.boot_time_after) {
    throw 'The cycle-2 resume must run in the same Windows boot as cycle 1.'
}
$connectionProbe = Join-Path $candidate.root 'xm5_connection_probe.exe'
$transportProbe = Join-Path $candidate.root 'transport_probe.exe'
if ((Get-NativeLdacBluetoothRadioState -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne
    'ready') {
    throw 'Windows Bluetooth is off or unavailable.'
}
if ((Get-NativeLdacXm5BluetoothState -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne
    'disconnected') {
    throw 'Keep XM5 off before arming the PnP-rundown gate.'
}
if (@(Get-NativeLdacWorkspaceProcesses).Count -ne 0) {
    throw 'Close all workspace media and agent processes.'
}

$devices = @(Get-LegacyXm5A2dpDevices)
if ($devices.Count -ne 1) {
    throw 'Exactly one present XM5 A2DP Sink service PDO is required after reboot.'
}
$binding = Get-LegacyXm5A2dpSnapshot -Device $devices[0]
if ([string]$binding.service -ne 'LdacNative' -or
    [int]$binding.problem_code -ne 0 -or
    -not ([string]$binding.published_inf).Equals(
        [string]$transaction.selected_inf,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw ("The fixed PnP-rundown binding is not healthy after reboot: " +
        "$($binding.service)/$($binding.published_inf)/problem " +
        "$($binding.problem_code).")
}
$ready = Wait-V1InboundTransportInfo -ProbePath $transportProbe `
    -ExpectedFlags $script:V1InboundReadyFlags -TimeoutSeconds 2
if ($null -eq $ready) {
    throw 'The fixed driver does not expose ABI 0.5 ready flags 0xF.'
}
$bootFailures = @(Get-V1InboundPnpKernelFailureEvents `
    -StartTime ([datetime]$currentBoot))
if ($bootFailures.Count -ne 0) {
    throw 'Kernel-PnP already reported LdacNative prior-unload failure after the activation reboot.'
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

if ($ResumeAfterCycle1) {
    Write-Host 'V1 inbound PnP-rundown cycle-2 resume preflight passed.'
} else {
    Write-Host 'V1 inbound PnP-rundown post-reboot preflight passed.'
}
Write-Host "Binding: $($binding.service)/$($binding.published_inf), problem code 0, ready flags 0x0000000F."
Write-Host 'The activation boot has no LdacNative Kernel-PnP 219 / 0xC000038E event.'
if ($ResumeAfterCycle1) {
    Write-Host 'Recorded cycle 1 will be reclassified from its original HCI trace; this run performs only cycle 2.'
} else {
    Write-Host 'This gate performs exactly two DISCOVER-only XM5 power cycles.'
}
Write-Host 'No SET_CONFIGURATION, media OPEN, START, media packet, Bluetooth radio toggle, PnP restart, or driver update is allowed.'
$target = if ($ResumeAfterCycle1) {
    'one remaining physical XM5 power cycle with the fixed inbound driver'
} else {
    'two physical XM5 power cycles with the fixed inbound driver'
}
if (-not $PSCmdlet.ShouldProcess(
        $target,
        'Prove inbound signaling, normal disconnect, driver reload, and absence of Code 38 without media')) {
    return
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$directoryName = if ($ResumeAfterCycle1) {
    "resume-cycle-2-$stamp"
} else {
    "lifecycle-$stamp"
}
$directory = Join-Path $trialRoot $directoryName
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$resultPath = Join-Path $directory 'result.json'
$transaction.boot_time_after = $currentBoot
$transaction.reboot_verified = $true
if (-not $ResumeAfterCycle1) {
    $transaction.status = 'running-pnp-rundown-validation'
    $transaction.phase = 'cycle-1-waiting-for-connect'
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
}

function Invoke-V1InboundPnpRundownCycle {
    param(
        [Parameter(Mandatory = $true)][int]$CycleNumber,
        [Parameter(Mandatory = $true)][int]$ConnectTimeoutSeconds
    )

    $cycleStart = Get-Date
    $cycleDirectory = Join-Path $directory "cycle-$CycleNumber"
    New-Item -ItemType Directory -Path $cycleDirectory -Force |
        Out-Null
    $cycle = [ordered]@{
        cycle = $CycleNumber
        started_at = $cycleStart.ToString('o')
        completed_at = $null
        passed = $false
        failure = $null
        acl_connect_observed = $false
        acl_disconnect_observed = $false
        public_disconnect_observed = $false
        public_disconnect_elapsed_ms = $null
        binding_on_connect_healthy = $false
        binding_after_disconnect = $null
        discover_exit_code = $null
        discover_text = ''
        open_diagnostic = ''
        l2cap_summary = $null
        code38_event_count = 0
        code38_events = @()
        set_configuration_commands = 0
        avdtp_open_commands = 0
        avdtp_start_commands = 0
        media_l2cap_open_commands = 0
        media_packets = 0
    }
    $connect = $null
    $disconnect = $null
    $publicDisconnect = $null
    $lastDiagnostic = $null
    $discover = $null
    try {
        Write-Host "Cycle $CycleNumber/2: the ACL watcher will tell you when it is armed; then turn on XM5. Do not start audio."
        $connect = Invoke-V1InboundStreamingAclProbe `
            -ProbePath $connectionProbe `
            -Arguments @(
                '--wait-acl-connect',
                [string]$ConnectTimeoutSeconds)
        if ($connect.exit_code -ne 0) {
            throw "Cycle $CycleNumber did not observe one physical ACL connect."
        }
        $cycle.acl_connect_observed = $true

        $connectedDevices = @(Get-LegacyXm5A2dpDevices)
        if ($connectedDevices.Count -ne 1) {
            throw "Cycle $CycleNumber did not expose exactly one A2DP PDO."
        }
        $connectedBinding = Get-LegacyXm5A2dpSnapshot `
            -Device $connectedDevices[0]
        if ([string]$connectedBinding.service -ne 'LdacNative' -or
            [int]$connectedBinding.problem_code -ne 0 -or
            -not ([string]$connectedBinding.published_inf).Equals(
                [string]$transaction.selected_inf,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "Cycle $CycleNumber loaded an unhealthy or unexpected driver binding."
        }
        $cycle.binding_on_connect_healthy = $true

        $diagnosticDeadline = (Get-Date).AddSeconds(5)
        do {
            $lastDiagnostic = Invoke-V1InboundTransportProbe `
                -ProbePath $transportProbe `
                -Arguments @('--open-diagnostics')
            $diagnosticText =
                $lastDiagnostic.stdout + $lastDiagnostic.stderr
            if ($lastDiagnostic.exit_code -eq 0 -and
                $diagnosticText -match
                    '(?m)^Signaling channel direction: inbound\.\r?$' -and
                $diagnosticText -match
                    '(?m)^L2CAP OPEN state: completed, succeeded\.\r?$') {
                $cycle.open_diagnostic = $diagnosticText
                break
            }
            Start-Sleep -Milliseconds 100
        } while ((Get-Date) -lt $diagnosticDeadline)
        if ([string]::IsNullOrWhiteSpace(
                [string]$cycle.open_diagnostic)) {
            if ($null -ne $lastDiagnostic) {
                ($lastDiagnostic.stdout + $lastDiagnostic.stderr) |
                    Set-Content -LiteralPath (Join-Path $cycleDirectory `
                        'incoming-open-diagnostics-last.log') `
                        -Encoding UTF8
            }
            throw "Cycle $CycleNumber did not publish a completed inbound signaling channel."
        }
        $cycle.open_diagnostic | Set-Content -LiteralPath `
            (Join-Path $cycleDirectory 'incoming-open-diagnostics.log') `
            -Encoding UTF8

        $discover = Invoke-V1InboundTransportProbe `
            -ProbePath $transportProbe `
            -Arguments @('--discover', '--open-attempts', '1')
        $cycle.discover_exit_code = [int]$discover.exit_code
        $cycle.discover_text = $discover.stdout + $discover.stderr
        $cycle.discover_text | Write-Host
        $cycle.discover_text | Set-Content -LiteralPath `
            (Join-Path $cycleDirectory 'discover.log') -Encoding UTF8
        if ($discover.exit_code -ne 0) {
            throw "Cycle $CycleNumber DISCOVER failed."
        }
    } catch {
        $cycle.failure = $_.Exception.Message
    } finally {
        if ($null -ne $connect -and $connect.exit_code -eq 0) {
            Write-Host "Cycle $CycleNumber/2: the ACL watcher will tell you when it is armed; then turn off XM5."
            $disconnect = Invoke-V1InboundStreamingAclProbe `
                -ProbePath $connectionProbe `
                -Arguments @('--wait-acl-disconnect', '60')
            $cycle.acl_disconnect_observed =
                $disconnect.exit_code -eq 0
        }
        Write-Host "Cycle $CycleNumber/2: waiting for public XM5 state to converge to disconnected."
        $publicDisconnect = Wait-V1InboundPublicDisconnect `
            -ProbePath $connectionProbe `
            -ExpectedSourceCommit ([string]$manifest.source_commit) `
            -TimeoutSeconds 60
        $cycle.public_disconnect_observed =
            $publicDisconnect.disconnected
        $cycle.public_disconnect_elapsed_ms =
            [long]$publicDisconnect.elapsed_ms
        $publicDisconnect | ConvertTo-Json -Depth 4 |
            Set-Content -LiteralPath (Join-Path $cycleDirectory `
                'public-disconnect-convergence.json') -Encoding UTF8

        Write-Host "Cycle $CycleNumber/2: observing a 20-second delayed PnP failure window."
        Start-Sleep -Seconds 20

        $summary = [pscustomobject]@{
            inbound_avdtp_connection_requests = 0
            outbound_avdtp_connection_requests = 0
            outbound_success_responses_to_inbound_avdtp = 0
            outbound_rejections_to_inbound_avdtp = 0
            inbound_avdtp_psm_not_supported_after_success = 0
            inbound_avdtp_unresolved_requests = 1
            inbound_no_resources_responses = 0
            inbound_avdtp_pending_without_success = $true
        }
        try {
            $hciChannel = 'Microsoft-Windows-BTH-BTHPORT/HCI'
            $events = @(Get-WinEvent -FilterHashtable @{
                LogName = $hciChannel
                StartTime = $cycleStart
            } -Oldest -ErrorAction SilentlyContinue)
            $hciXml = Join-Path $cycleDirectory 'hci.xml'
            @($events | ForEach-Object { $_.ToXml() }) |
                Set-Content -LiteralPath $hciXml -Encoding UTF8
            $summaryPath = Join-Path $cycleDirectory `
                'l2cap-summary.json'
            & (Join-Path $PSScriptRoot `
                'summarize-bluetooth-l2cap-trace.ps1') `
                -InputPath $hciXml -OutputPath $summaryPath | Out-Null
            $summary = Get-Content -LiteralPath $summaryPath -Raw |
                ConvertFrom-Json
            $cycle.l2cap_summary = $summaryPath
        } catch {
            if ([string]::IsNullOrWhiteSpace([string]$cycle.failure)) {
                $cycle.failure =
                    "Cycle $CycleNumber HCI evidence failed: $($_.Exception.Message)"
            }
        }

        $afterDevices = @(Get-LegacyXm5A2dpDevices)
        if ($afterDevices.Count -eq 1) {
            $afterBinding = Get-LegacyXm5A2dpSnapshot `
                -Device $afterDevices[0]
            $cycle.binding_after_disconnect = [ordered]@{
                service = [string]$afterBinding.service
                published_inf = [string]$afterBinding.published_inf
                problem_code = [int]$afterBinding.problem_code
            }
            if ([int]$afterBinding.problem_code -eq 38 -and
                [string]::IsNullOrWhiteSpace([string]$cycle.failure)) {
                $cycle.failure =
                    "Cycle $CycleNumber left the PDO in Code 38."
            }
        } elseif ($afterDevices.Count -gt 1 -and
            [string]::IsNullOrWhiteSpace([string]$cycle.failure)) {
            $cycle.failure =
                "Cycle $CycleNumber left multiple present A2DP PDOs."
        }

        # Kernel-PnP 219 can be published after the problem code is set.
        # The fixed delay above covers the failure latency observed on the
        # rejected long-lived listener before this final collection.
        $code38Events = @(Get-V1InboundPnpKernelFailureEvents `
            -StartTime $cycleStart)
        $cycle.code38_event_count = $code38Events.Count
        $cycle.code38_events = @($code38Events)

        if ([string]::IsNullOrWhiteSpace([string]$cycle.failure) -and
            -not (Test-V1InboundPnpCycleEvidence `
                -Cycle ([pscustomobject]$cycle) -Summary $summary)) {
            $cycle.failure =
                "Cycle $CycleNumber evidence did not satisfy the PnP-rundown contract."
        }
        $cycle.passed =
            [string]::IsNullOrWhiteSpace([string]$cycle.failure)
        $cycle.completed_at = (Get-Date).ToString('o')
        Write-LegacyJsonAtomic -Value $cycle `
            -Path (Join-Path $cycleDirectory 'result.json')
    }
    return [pscustomobject]$cycle
}

$channelEnableFailure = $null
$cycles = @()
$cycleNumbers = @(1, 2)
if ($ResumeAfterCycle1) {
    $originalCycle = $transaction.cycles[0]
    $originalSummaryPath = [string]$originalCycle.l2cap_summary
    $originalCycleDirectory = Split-Path -Parent $originalSummaryPath
    $originalHciPath = Join-Path $originalCycleDirectory 'hci.xml'
    if (-not (Test-Path -LiteralPath $originalHciPath -PathType Leaf)) {
        throw 'The recorded cycle-1 raw HCI trace is missing.'
    }
    $reclassifiedSummaryPath = Join-Path $directory `
        'cycle-1-reclassified-l2cap-summary.json'
    & (Join-Path $PSScriptRoot 'summarize-bluetooth-l2cap-trace.ps1') `
        -InputPath $originalHciPath `
        -OutputPath $reclassifiedSummaryPath | Out-Null
    $reclassifiedSummary = Get-Content `
        -LiteralPath $reclassifiedSummaryPath -Raw | ConvertFrom-Json
    if (-not (Test-V1InboundPnpCycleEvidence `
            -Cycle $originalCycle -Summary $reclassifiedSummary)) {
        throw 'The recorded cycle-1 evidence still fails the corrected PnP-rundown contract.'
    }
    $reclassifiedCycle = $originalCycle | ConvertTo-Json -Depth 12 |
        ConvertFrom-Json
    $reclassifiedCycle.passed = $true
    $reclassifiedCycle.failure = $null
    $reclassificationPath = Join-Path $directory `
        'cycle-1-reclassification.json'
    $reclassification = [ordered]@{
        schema_version = 1
        transport_policy_version =
            $script:V1InboundPnpRundownPolicyVersion
        transaction = $TransactionPath
        original_result = [string]$transaction.result
        original_cycle_result = Join-Path $originalCycleDirectory `
            'result.json'
        original_summary = $originalSummaryPath
        raw_hci = $originalHciPath
        corrected_summary = $reclassifiedSummaryPath
        classification =
            'passed-one-shot-listener-post-success-psm-not-supported'
        created_at = (Get-Date).ToString('o')
    }
    Write-LegacyJsonAtomic -Value $reclassification `
        -Path $reclassificationPath
    $transaction | Add-Member -NotePropertyName `
        cycle_1_reclassification -NotePropertyValue `
        $reclassificationPath -Force
    $transaction.status = 'running-pnp-rundown-validation'
    $transaction.phase = 'cycle-2-waiting-for-connect'
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
    $cycles = @($reclassifiedCycle)
    $cycleNumbers = @(2)
    Write-Host 'Recorded cycle 1 passed the corrected one-shot-listener evidence contract.'
}
try {
    foreach ($channel in $channels) {
        if (-not $channelStates[$channel]) {
            & wevtutil.exe sl $channel /e:true /q:true
            if ($LASTEXITCODE -ne 0) {
                throw "Unable to enable Bluetooth analytic channel: $channel"
            }
        }
    }
    $gateDeadline = (Get-Date).AddSeconds($DurationSeconds)
    foreach ($cycleNumber in $cycleNumbers) {
        $remaining = [int][Math]::Floor(
            ($gateDeadline - (Get-Date)).TotalSeconds)
        if ($remaining -lt 30) {
            throw 'The bounded two-cycle gate expired before the next ACL connect.'
        }
        $transaction.phase = "cycle-$cycleNumber-running"
        $transaction.updated_at = (Get-Date).ToString('o')
        Write-LegacyJsonAtomic -Value $transaction `
            -Path $TransactionPath
        $cycle = Invoke-V1InboundPnpRundownCycle `
            -CycleNumber $cycleNumber `
            -ConnectTimeoutSeconds $remaining
        $cycles += $cycle
        if (-not $cycle.passed) {
            break
        }
    }
} catch {
    $channelEnableFailure = $_.Exception.Message
} finally {
    foreach ($channel in $channels) {
        if (-not $channelStates[$channel]) {
            & wevtutil.exe sl $channel /e:false | Out-Null
        }
    }
}

$passed = $null -eq $channelEnableFailure -and
    $cycles.Count -eq 2 -and
    @($cycles | Where-Object { -not $_.passed }).Count -eq 0
$failure = if ($passed) {
    $null
} elseif ($null -ne $channelEnableFailure) {
    $channelEnableFailure
} elseif ($cycles.Count -eq 0) {
    'No PnP-rundown lifecycle cycle completed.'
} else {
    [string]$cycles[-1].failure
}
$code38Total = if ($cycles.Count -eq 0) {
    0
} else {
    [int](@($cycles | ForEach-Object {
        [int]$_.code38_event_count
    }) | Measure-Object -Sum).Sum
}
$result = [ordered]@{
    schema_version = 1
    transport_policy_version =
        $script:V1InboundPnpRundownPolicyVersion
    passed = $passed
    source_commit = [string]$manifest.source_commit
    driver_tree = [string]$manifest.driver_tree
    transaction = $TransactionPath
    binding_inf = [string]$transaction.selected_inf
    reboot_verified = $true
    cycle_count = $cycles.Count
    cycles = @($cycles)
    code38_event_count = $code38Total
    set_configuration_commands = 0
    avdtp_open_commands = 0
    avdtp_start_commands = 0
    media_l2cap_open_commands = 0
    media_packets = 0
    bluetooth_toggled = $false
    pnp_restarted = $false
    error = $failure
}
Write-LegacyJsonAtomic -Value $result -Path $resultPath
$transaction.cycles = @($cycles)
$transaction.result = $resultPath
$transaction.error = $failure
if ($passed) {
    $transaction.status = 'pnp-rundown-verified'
    $transaction.phase = 'complete'
} elseif ($cycles.Count -ne 0 -and
    $cycles[-1].public_disconnect_observed) {
    $transaction.status = 'rollback-required'
    $transaction.phase = 'validation-failed-xm5-disconnected'
} else {
    $transaction.status = 'operator-disconnect-required'
    $transaction.phase = 'validation-failed-waiting-for-xm5-disconnect'
}
$transaction.updated_at = (Get-Date).ToString('o')
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath

if (-not $passed) {
    throw "V1 inbound PnP-rundown gate failed: $failure Keep XM5 off and do not retry. Transaction: $TransactionPath"
}

Write-Host 'V1 inbound PnP-rundown gate passed.'
Write-Host 'Two physical connect/disconnect cycles completed with inbound signaling and one DISCOVER each.'
Write-Host 'Both cycles had outbound PSM 0x0019 = 0, remote NO_RESOURCES = 0, and Code 38 events = 0.'
Write-Host 'No SET_CONFIGURATION, media OPEN, START, media packet, PnP restart, or Bluetooth radio toggle occurred.'
Write-Host "Result: $resultPath"
