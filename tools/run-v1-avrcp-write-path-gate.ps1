# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1AvrcpWritePath,
    [ValidateRange(20, 120)]
    [int]$ObservationSeconds = 45,
    [ValidateRange(120, 600)]
    [int]$ConnectTimeoutSeconds = 300,
    [string]$FilterCandidatePath,
    [string]$ObserverCandidatePath,
    [string]$ConnectionProbePath,
    [string]$TransportProbePath,
    [string]$FilterProbePath,
    [string]$ExecutorPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'v1-avrcp-filter-gate-common.ps1')
. (Join-Path $PSScriptRoot 'v1-native-process-live.ps1')

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The V1 AVRCP write-path gate requires PowerShell 7.'
}
Assert-V1AvrcpFilterAdministrator
if (-not $ConfirmV1AvrcpWritePath) {
    throw 'Refusing the AVRCP write-path gate. Re-run with -ConfirmV1AvrcpWritePath.'
}

$script:TargetPrefix = `
    'BTHENUM\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
$script:MicrosoftInf = 'microsoft_bluetooth_avrcptransport.inf'
$script:MicrosoftService = 'Microsoft_Bluetooth_AvrcpTransport'
$script:ObserverInf = 'NativeLdacAvrcpObserver.inf'
$script:ObserverService = 'NativeLdacAvrcpObserver'
$script:SendCommandIoctl = '0x8001A00C'

if ([string]::IsNullOrWhiteSpace($FilterCandidatePath)) {
    $FilterCandidatePath = Join-Path $projectRoot `
        'artifacts\v1-volume-sync\avrcp-filter-candidate'
}
if ([string]::IsNullOrWhiteSpace($ObserverCandidatePath)) {
    $ObserverCandidatePath = Join-Path $projectRoot `
        'artifacts\v1-volume-sync\avrcp-observer-candidate'
}
if ([string]::IsNullOrWhiteSpace($ConnectionProbePath)) {
    $ConnectionProbePath = Join-Path $FilterCandidatePath `
        'tools\xm5_connection_probe.exe'
}
if ([string]::IsNullOrWhiteSpace($TransportProbePath)) {
    $TransportProbePath = Join-Path $FilterCandidatePath `
        'tools\transport_probe.exe'
}
if ([string]::IsNullOrWhiteSpace($FilterProbePath)) {
    $FilterProbePath = Join-Path $FilterCandidatePath `
        'tools\v1_avrcp_filter_probe.exe'
}
if ([string]::IsNullOrWhiteSpace($ExecutorPath)) {
    $ExecutorPath = Join-Path $projectRoot `
        'build\protocol\Release\v1_avrcp_action_executor.exe'
}

function Get-V1WritePathTargetDevice {
    $devices = @(Get-PnpDevice -ErrorAction SilentlyContinue |
        Where-Object {
            $_.InstanceId.StartsWith(
                $script:TargetPrefix,
                [StringComparison]::OrdinalIgnoreCase)
        })
    if ($devices.Count -ne 1) {
        throw "The exact XM5 AVRCP PDO must resolve to one device; found $($devices.Count)."
    }
    return $devices[0]
}

function Get-V1WritePathSnapshot {
    param([Parameter(Mandatory = $true)]$Device)
    $inf = Get-V1AvrcpFilterPropertyData -InstanceId $Device.InstanceId `
        -KeyName 'DEVPKEY_Device_DriverInfPath'
    $service = Get-V1AvrcpFilterPropertyData -InstanceId $Device.InstanceId `
        -KeyName 'DEVPKEY_Device_Service'
    $problem = Get-V1AvrcpFilterPropertyData -InstanceId $Device.InstanceId `
        -KeyName 'DEVPKEY_Device_ProblemCode'
    return [pscustomobject][ordered]@{
        instance_id = $Device.InstanceId
        present = $Device.Present
        status = $Device.Status
        problem_code = [int]$problem
        inf = [string]$inf
        service = [string]$service
    }
}

function Test-V1WritePathMicrosoftBaseline {
    param([Parameter(Mandatory = $true)]$Snapshot)
    return $Snapshot.present -and
        $Snapshot.status -eq 'OK' -and
        [int]$Snapshot.problem_code -eq 0 -and
        [string]$Snapshot.inf -ieq $script:MicrosoftInf -and
        [string]$Snapshot.service -ieq $script:MicrosoftService
}

function Test-V1WritePathObserverBound {
    param([Parameter(Mandatory = $true)]$Snapshot)
    return $Snapshot.present -and
        $Snapshot.status -eq 'OK' -and
        [int]$Snapshot.problem_code -eq 0 -and
        ([string]$Snapshot.inf -match '^oem\d+\.inf$') -and
        [string]$Snapshot.service -ieq $script:ObserverService
}

function Get-V1WritePathMediaDiagnostic {
    param([Parameter(Mandatory = $true)][string]$ErrorLog)
    $lines = @()
    $errorLines = @(Read-V1WritePathFile $ErrorLog)
    if ($errorLines.Count -ne 0) {
        $lines += 'media stderr:'
        $lines += @($errorLines | ForEach-Object { "  $_" })
    }
    $a2dp = @(Get-PnpDevice -ErrorAction SilentlyContinue |
        Where-Object {
            $_.InstanceId.StartsWith(
                'BTHENUM\{0000110B-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0',
                [StringComparison]::OrdinalIgnoreCase)
        })
    if ($a2dp.Count -ge 1) {
        $snapshot = Get-V1WritePathSnapshot -Device $a2dp[0]
        $lines += 'A2DP Sink PDO (media transport):' +
            " present=$($snapshot.present) status=$($snapshot.status)" +
            " problem=$($snapshot.problem_code) service=$($snapshot.service)"
        if ([int]$snapshot.problem_code -eq 38) {
            $lines += '  Code 38 means LdacNative failed to load; a Windows' +
                ' restart usually clears the stale kernel driver object.'
        }
    } else {
        $lines += 'A2DP Sink PDO was not present after the physical connect.'
    }
    return $lines
}

function Wait-V1WritePathSnapshot {
    param(
        [Parameter(Mandatory = $true)]$Predicate,
        [Parameter(Mandatory = $true)][string]$InstanceId,
        [int]$TimeoutSeconds = 30
    )
    $deadline = [DateTimeOffset]::Now.AddSeconds($TimeoutSeconds)
    do {
        $device = Get-PnpDevice -InstanceId $InstanceId `
            -ErrorAction SilentlyContinue
        if ($null -ne $device) {
            $snapshot = Get-V1WritePathSnapshot -Device $device
            if (& $Predicate $snapshot) { return $snapshot }
        }
        Start-Sleep -Milliseconds 500
    } while ([DateTimeOffset]::Now -lt $deadline)
    return $null
}

function Stop-V1WritePathProcess {
    param([AllowNull()]$Process)
    if ($null -eq $Process) { return }
    if (-not $Process.HasExited) {
        $Process.Kill($true)
        $Process.WaitForExit()
    }
    $Process.Dispose()
}

function Read-V1WritePathFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return @() }
    return @(Get-Content -LiteralPath $Path | ForEach-Object {
        [string]$_
    })
}

# --- Preflight -------------------------------------------------------------
Write-Host 'V1 AVRCP write-path gate preflight:'
Write-Host "  filter candidate:   $FilterCandidatePath"
Write-Host "  observer candidate: $ObserverCandidatePath"
& (Join-Path $PSScriptRoot 'verify-v1-avrcp-filter-candidate.ps1') `
    -CandidatePath $FilterCandidatePath
$filterManifest = Get-Content -LiteralPath `
    (Join-Path $FilterCandidatePath 'manifest.json') -Raw |
    ConvertFrom-Json
if ($filterManifest.policy_version -lt 6) {
    throw "The filter candidate must be policy 6 or newer; found $($filterManifest.policy_version)."
}
$observerPackage = Join-Path $ObserverCandidatePath 'package'
if (-not (Test-Path -LiteralPath `
        (Join-Path $observerPackage 'NativeLdacAvrcpObserver.inf') `
        -PathType Leaf)) {
    throw "The observer candidate package is incomplete: $ObserverCandidatePath"
}
$filterPackage = Join-Path $FilterCandidatePath 'package'
foreach ($path in @(
        $ConnectionProbePath,
        $TransportProbePath,
        $FilterProbePath,
        $ExecutorPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required tool is missing: $path"
    }
}

$target = Get-V1WritePathTargetDevice
$baseline = Get-V1WritePathSnapshot -Device $target
if (-not (Test-V1WritePathMicrosoftBaseline -Snapshot $baseline)) {
    throw 'The exact XM5 AVRCP PDO is not the healthy Microsoft baseline.'
}
$stateProbe = Get-PnpDevice -InstanceId `
    'SWD\NativeLdacAvrcpIoFilter\*' -ErrorAction SilentlyContinue
if ($null -ne $stateProbe) {
    throw 'An active filter control device is already present.'
}
$filterPackages = @(Get-V1AvrcpFilterPackages)
if ($filterPackages.Count -ne 0) {
    throw "Historical filter packages block the gate: $($filterPackages.published_inf -join ', ')"
}
$observerPackages = @(Get-WindowsDriver -Online -All |
    Where-Object {
        (Split-Path -Leaf ([string]$_.OriginalFileName)) -ieq
            'NativeLdacAvrcpObserver.inf'
    })
if ($observerPackages.Count -ne 0) {
    throw "Historical observer packages block the gate: $($observerPackages.published_inf -join ', ')"
}

Write-Host 'Preflight passed. XM5 must remain off until the ACL watcher is armed.'

# --- Trial layout ----------------------------------------------------------
$trialRoot = Join-Path $projectRoot (
    'artifacts\v1-volume-sync\trial\avrcp-write-path-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $trialRoot -Force | Out-Null
$resultPath = Join-Path $trialRoot 'result.json'
$result = [ordered]@{
    schema_version = 1
    created_at = (Get-Date).ToString('o')
    passed = $false
    failure = ''
    failure_code = ''
    source_commit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
    filter_candidate = $FilterCandidatePath
    observer_candidate = $ObserverCandidatePath
    baseline = $baseline
    steps = @()
    evidence = [ordered]@{
        send_command_ioctl_lines = @()
        executor_lines = @()
        filter_probe_lines = @()
    }
    restore = $null
}

$installLog = Join-Path $trialRoot 'install.log'
$restartLog = Join-Path $trialRoot 'exact-pdo-restart.log'
$connectOut = Join-Path $trialRoot 'connect.out.log'
$connectErr = Join-Path $trialRoot 'connect.err.log'
$mediaOut = Join-Path $trialRoot 'silence-media.out.log'
$mediaErr = Join-Path $trialRoot 'silence-media.err.log'
$filterOut = Join-Path $trialRoot 'filter-probe.out.log'
$filterErr = Join-Path $trialRoot 'filter-probe.err.log'
$executorOut = Join-Path $trialRoot 'executor.out.log'
$executorErr = Join-Path $trialRoot 'executor.err.log'

# Process handles are pre-initialized so the failure path can always stop
# them and write the trial result even under Set-StrictMode when a phase
# throws before a process was started.
$connectProcess = $null
$mediaProcess = $null
$filterProcess = $null
$executorProcess = $null
$mediaStopEvent = $null

try {
    # --- Phase 1: install the upper filter --------------------------------
    Write-Host 'Phase 1: installing the Microsoft-preserving upper filter.'
    $addFilter = Invoke-V1AvrcpFilterPnpUtil -Arguments @(
        '/add-driver',
        (Join-Path $filterPackage 'NativeLdacAvrcpIoFilter.inf'))
    $addFilter.lines | Set-Content -LiteralPath $installLog -Encoding utf8
    if ($addFilter.exit_code -ne 0) {
        throw "Filter package add failed (exit $($addFilter.exit_code))."
    }
    $publishedFilterInf = Get-V1AvrcpFilterPublishedInfFromOutput `
        -Lines $addFilter.lines
    $result.steps += [ordered]@{
        action = 'add-filter-package'
        published_inf = $publishedFilterInf
        exit_code = $addFilter.exit_code
    }

    Write-Host 'Restarting the exact XM5 AVRCP PDO once so the filter attaches to a fresh Microsoft stack.'
    $restartFilter = Invoke-V1AvrcpFilterPnpUtil -Arguments @(
        '/restart-device', $target.InstanceId)
    $restartFilter.lines | Set-Content -LiteralPath $restartLog -Encoding utf8
    if ($restartFilter.exit_code -ne 0) {
        throw "Exact PDO restart failed (exit $($restartFilter.exit_code))."
    }
    $result.steps += [ordered]@{
        action = 'restart-exact-pdo-for-filter'
        exit_code = $restartFilter.exit_code
    }
    $afterFilter = Wait-V1WritePathSnapshot `
        -Predicate ${function:Test-V1WritePathMicrosoftBaseline} `
        -InstanceId $target.InstanceId
    if ($null -eq $afterFilter) {
        throw 'Microsoft AVRCP did not return healthy after the filter restart.'
    }
    $result.steps += [ordered]@{ action = 'verify-filter-stack'; passed = $true }

    # --- Phase 2: ACL watcher + media silence -----------------------------
    Write-Host 'Phase 2: arming the ACL watcher. Turn on XM5 normally now.'
    $connectProcess = Start-Process -FilePath $ConnectionProbePath `
        -ArgumentList @('--wait-acl-connect', "$ConnectTimeoutSeconds") `
        -RedirectStandardOutput $connectOut `
        -RedirectStandardError $connectErr `
        -PassThru -WindowStyle Hidden
    $armedDeadline = [DateTimeOffset]::Now.AddSeconds(30)
    $armed = $false
    while ([DateTimeOffset]::Now -lt $armedDeadline) {
        foreach ($line in (Read-V1WritePathFile $connectOut)) {
            if ($line -match 'ACL watcher armed') { $armed = $true }
        }
        if ($armed -or $connectProcess.HasExited) { break }
        Start-Sleep -Milliseconds 250
    }
    if (-not $armed) {
        throw 'The ACL watcher did not reach its armed state.'
    }
    Write-Host 'ACL watcher armed. Turn on XM5 normally now.'

    $connected = $false
    $mediaStarted = $false
    $mediaReady = $false
    $connectDeadline = [DateTimeOffset]::Now.AddSeconds($ConnectTimeoutSeconds)
    while ([DateTimeOffset]::Now -lt $connectDeadline) {
        foreach ($line in (Read-V1WritePathFile $connectOut)) {
            if ($line -match 'XM5 ACL event: connected') { $connected = $true }
        }
        if ($connected -and -not $mediaStarted) {
            Write-Host 'XM5 connected. Starting the bounded LDAC encoded-silence media prerequisite.'
            $mediaStopEventName = 'Local\NativeLdacWritePathStop-' +
                $PID + '-' + [guid]::NewGuid().ToString('N')
            $mediaStopCreatedNew = $false
            $mediaStopEvent = [Threading.EventWaitHandle]::new(
                $false,
                [Threading.EventResetMode]::ManualReset,
                $mediaStopEventName,
                [ref]$mediaStopCreatedNew)
            if (-not $mediaStopCreatedNew) {
                throw 'The bounded media stop event already existed.'
            }
            $mediaProcess = Start-Process -FilePath $TransportProbePath `
                -ArgumentList @(
                    '--stream-silence-continuous',
                    '--open-attempts', '1',
                    '--stop-event', $mediaStopEventName) `
                -RedirectStandardOutput $mediaOut `
                -RedirectStandardError $mediaErr `
                -PassThru -WindowStyle Hidden
            $mediaStarted = $true
        }
        if ($mediaStarted) {
            foreach ($line in (Read-V1WritePathFile $mediaOut)) {
                if ($line -match 'XM5 accepted START; the LDAC Media transport is ready') {
                    $mediaReady = $true
                }
            }
        }
        if ($mediaReady) { break }
        if (-not $connected -and $connectProcess.HasExited) {
            throw 'The ACL watcher exited before a physical connect was observed.'
        }
        if ($mediaStarted -and $mediaProcess.HasExited) {
            $diagnostic = Get-V1WritePathMediaDiagnostic -ErrorLog $mediaErr
            throw ('The encoded-silence media process exited before START. ' +
                ($diagnostic -join ' '))
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $mediaReady) {
        throw 'The encoded-silence media session did not reach START.'
    }
    Write-Host 'Media START ready. Draining connection-time filter events now.'

    # --- Phase 3: observer handoff (function driver switch) ---------------
    Write-Host 'Phase 3: switching the exact PDO to the observer function driver.'
    $addObserver = Invoke-V1AvrcpFilterPnpUtil -Arguments @(
        '/add-driver',
        (Join-Path $observerPackage 'NativeLdacAvrcpObserver.inf'),
        '/install')
    $addObserver.lines | Set-Content -LiteralPath `
        (Join-Path $trialRoot 'observer-add.log') -Encoding utf8
    if ($addObserver.exit_code -notin @(0, 259)) {
        throw "Observer package add failed (exit $($addObserver.exit_code))."
    }
    $publishedObserverInf = Get-V1AvrcpFilterPublishedInfFromOutput `
        -Lines $addObserver.lines
    $observerBound = Wait-V1WritePathSnapshot `
        -Predicate ${function:Test-V1WritePathObserverBound} `
        -InstanceId $target.InstanceId
    if ($null -eq $observerBound) {
        Write-Host 'Observer did not bind immediately; restarting the exact PDO once.'
        $restartObserver = Invoke-V1AvrcpFilterPnpUtil -Arguments @(
            '/restart-device', $target.InstanceId)
        $restartObserver.lines | Set-Content -LiteralPath `
            (Join-Path $trialRoot 'observer-restart.log') -Encoding utf8
        if ($restartObserver.exit_code -ne 0) {
            throw "Observer handoff restart failed (exit $($restartObserver.exit_code))."
        }
        $observerBound = Wait-V1WritePathSnapshot `
            -Predicate ${function:Test-V1WritePathObserverBound} `
            -InstanceId $target.InstanceId -TimeoutSeconds 45
    }
    if ($null -eq $observerBound) {
        throw 'The observer did not bind as the function driver after the handoff restart.'
    }
    $result.steps += [ordered]@{
        action = 'observer-handoff'
        passed = $true
    }

    # --- Phase 4: filter probe + executor writes --------------------------
    Write-Host 'Phase 4: observing the write path. Filter probe is read-only and the executor applies only mapper-authorized decisions.'
    $filterProcess = Start-Process -FilePath $FilterProbePath `
        -ArgumentList @(
            '--duration-seconds', "$ObservationSeconds") `
        -RedirectStandardOutput $filterOut `
        -RedirectStandardError $filterErr `
        -PassThru -WindowStyle Hidden

    $executorProcess = Start-Process -FilePath $ExecutorPath `
        -ArgumentList @(
            '--live',
            '--duration-seconds', "$ObservationSeconds",
            '--volume-sync',
            '--route-media-keys',
            '--apply') `
        -RedirectStandardOutput $executorOut `
        -RedirectStandardError $executorErr `
        -PassThru -WindowStyle Hidden

    Write-Host '=== XM5 ACTION WINDOW READY ==='
    Write-Host 'Now change the PC volume: +2 steps, wait 2 seconds, then -2 steps.'
    Write-Host 'Optionally swipe the XM5 volume up and down once so the headset side is also verified.'

    $observationDeadline = [DateTimeOffset]::Now.AddSeconds(
        $ObservationSeconds + 10)
    $pcWriteSeen = $false
    $halfwayReminderShown = $false
    while ([DateTimeOffset]::Now -lt $observationDeadline) {
        if (-not $pcWriteSeen) {
            foreach ($line in (Read-V1WritePathFile $executorOut)) {
                if ($line -match 'action send-xm5-volume value=\d+ \(sent\)') {
                    $pcWriteSeen = $true
                }
            }
        }
        if (-not $pcWriteSeen -and -not $halfwayReminderShown -and
            ($observationDeadline - [DateTimeOffset]::Now).TotalSeconds -le
                ($ObservationSeconds / 2)) {
            $halfwayReminderShown = $true
            Write-Host 'No PC-to-XM5 write observed yet. Adjust the PC volume'
            Write-Host 'now (+2 steps, wait, then -2 steps); headset swipes'
            Write-Host 'only drive the follow direction and cannot pass this gate.'
        }
        if ($executorProcess.HasExited -and $filterProcess.HasExited) {
            break
        }
        Start-Sleep -Milliseconds 250
    }
    if ($null -ne $mediaStopEvent) {
        [void]$mediaStopEvent.Set()
    }
    if ($null -ne $mediaProcess -and -not $mediaProcess.HasExited) {
        if (-not $mediaProcess.WaitForExit(30000)) {
            $mediaTimedOut = $true
        }
    }
    Stop-V1WritePathProcess $executorProcess
    Stop-V1WritePathProcess $filterProcess
    Stop-V1WritePathProcess $mediaProcess
    Stop-V1WritePathProcess $connectProcess

    # --- Evidence ---------------------------------------------------------
    $result.evidence.executor_lines = @(Read-V1WritePathFile $executorOut)
    $result.evidence.filter_probe_lines = @(Read-V1WritePathFile $filterOut)
    $result.evidence.send_command_ioctl_lines = @(
        $result.evidence.filter_probe_lines | Where-Object {
            $_ -match "ioctl=$script:SendCommandIoctl" -and
            $_ -match 'raw=.*\b50 00 00 00 00 00 00 00 01'
        })
    $sentWrites = @($result.evidence.executor_lines |
        Where-Object { $_ -match 'action send-xm5-volume value=\d+ \(sent\)' })
    $windowsFollow = @($result.evidence.executor_lines |
        Where-Object { $_ -match 'action set-windows-volume' })
    if ($result.evidence.send_command_ioctl_lines.Count -eq 0) {
        throw ('No SEND_COMMAND IOCTL with pdu 0x50 was observed on the ' +
            'filter surface. The gate requires PC volume changes during the ' +
            'action window; headset swipes only produce follow writes.')
    }
    if ($sentWrites.Count -eq 0) {
        throw ('The executor did not report any sent SetAbsoluteVolume write. ' +
            'Adjust the PC volume during the action window; headset swipes ' +
            'only produce follow writes.')
    }
    $result.steps += [ordered]@{
        action = 'write-path-evidence'
        send_command_ioctl_count = $result.evidence.send_command_ioctl_lines.Count
        sent_write_count = $sentWrites.Count
        windows_volume_action_count = $windowsFollow.Count
        passed = $true
    }

    # --- Phase 5: restore Microsoft AVRCP ---------------------------------
    Write-Host 'Phase 5: restoring Microsoft AVRCP before the disconnect watcher is released.'
    $deleteObserver = Invoke-V1AvrcpFilterPnpUtil -Arguments @(
        '/delete-driver', $publishedObserverInf, '/uninstall', '/force')
    $deleteObserver.lines | Set-Content -LiteralPath `
        (Join-Path $trialRoot 'observer-delete.log') -Encoding utf8
    $scanObserver = Invoke-V1AvrcpFilterPnpUtil -Arguments @('/scan-devices')
    $restoreResult = Invoke-V1AvrcpFilterPnpUtil -Arguments @(
        '/restart-device', $target.InstanceId)
    $restoreResult.lines | Set-Content -LiteralPath `
        (Join-Path $trialRoot 'restore.log') -Encoding utf8
    $restored = Wait-V1WritePathSnapshot `
        -Predicate ${function:Test-V1WritePathMicrosoftBaseline} `
        -InstanceId $target.InstanceId -TimeoutSeconds 45
    $result.restore = [ordered]@{
        passed = $null -ne $restored
        observer_delete_exit = $deleteObserver.exit_code
        restore_restart_exit = $restoreResult.exit_code
    }
    if ($null -eq $restored) {
        throw 'Microsoft AVRCP could not be restored after the write-path trial.'
    }

    $filterRollback = Invoke-V1AvrcpFilterRollback `
        -PublishedInf $publishedFilterInf `
        -InstanceId $target.InstanceId `
        -ProbePath $FilterProbePath `
        -LogDirectory $trialRoot
    $result.restore.filter_rollback = $filterRollback
    if (-not $filterRollback.passed) {
        throw 'The filter package rollback did not complete.'
    }

    $result.passed = $true
} catch {
    $result.failure = [string]$_
    $result.failure_code = 'write-path-gate-failed'
} finally {
    foreach ($process in @($executorProcess, $filterProcess,
                           $mediaProcess, $connectProcess)) {
        if ($null -ne $process) {
            try {
                Stop-V1WritePathProcess $process
            } catch {
                # Cleanup must never mask the recorded trial failure.
            }
        }
    }
    if ($null -ne $mediaStopEvent) {
        try {
            $mediaStopEvent.Dispose()
        } catch {
            # Cleanup must never mask the recorded trial failure.
        }
    }
    Write-V1AvrcpFilterJsonAtomically -Path $resultPath -Value $result
}

if ($result.passed) {
    Write-Host 'V1 AVRCP write-path gate passed.'
    Write-Host "Result: $resultPath"
} else {
    Write-Host "V1 AVRCP write-path gate failed: $($result.failure)"
    Write-Host "Result: $resultPath"
    exit 1
}
