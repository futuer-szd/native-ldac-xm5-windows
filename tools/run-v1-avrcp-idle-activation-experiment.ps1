# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1AvrcpIdleActivationExperiment,
    [ValidateRange(20, 3600)]
    [int]$ObservationSeconds = 45,
    [ValidateRange(0, 15)]
    [int]$DelaySeconds = 0,
    [switch]$AllDelays,
    [string]$ObserverCandidatePath,
    [string]$ProbePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'v1-avrcp-filter-gate-common.ps1')

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The V1 AVRCP idle-activation experiment requires PowerShell 7.'
}
Assert-V1AvrcpFilterAdministrator
if (-not $ConfirmV1AvrcpIdleActivationExperiment) {
    throw 'Refusing the idle-activation experiment. Re-run with -ConfirmV1AvrcpIdleActivationExperiment.'
}

$script:TargetPrefix = `
    'BTHENUM\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
$script:MicrosoftInf = 'microsoft_bluetooth_avrcptransport.inf'
$script:MicrosoftService = 'Microsoft_Bluetooth_AvrcpTransport'
$script:ObserverService = 'NativeLdacAvrcpObserver'
$script:OpenFailedStatus = '0xC00000D0'

if ([string]::IsNullOrWhiteSpace($ObserverCandidatePath)) {
    $ObserverCandidatePath = Join-Path $projectRoot `
        'artifacts\v1-volume-sync\avrcp-observer-candidate'
}
if ([string]::IsNullOrWhiteSpace($ProbePath)) {
    # The observer candidate package ships the probe built when the package
    # was created; the idle-activation flags live in the current build tree,
    # so the experiment must use the freshly built probe.
    $ProbePath = Join-Path $projectRoot `
        'build\protocol\Release\v1_avrcp_observer_probe.exe'
}
function Get-V1IdleTargetDevice {
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

function Get-V1IdleSnapshot {
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

function Test-V1IdleMicrosoftBaseline {
    param([Parameter(Mandatory = $true)]$Snapshot)
    return $Snapshot.present -and
        $Snapshot.status -eq 'OK' -and
        [int]$Snapshot.problem_code -eq 0 -and
        [string]$Snapshot.inf -ieq $script:MicrosoftInf -and
        [string]$Snapshot.service -ieq $script:MicrosoftService
}

function Test-V1IdleObserverBound {
    param([Parameter(Mandatory = $true)]$Snapshot)
    return $Snapshot.present -and
        $Snapshot.status -eq 'OK' -and
        [int]$Snapshot.problem_code -eq 0 -and
        ([string]$Snapshot.inf -match '^oem\d+\.inf$') -and
        [string]$Snapshot.service -ieq $script:ObserverService
}

function Wait-V1IdleSnapshot {
    param(
        [Parameter(Mandatory = $true)]$Predicate,
        [Parameter(Mandatory = $true)][string]$InstanceId,
        [int]$TimeoutSeconds = 45
    )
    $deadline = [DateTimeOffset]::Now.AddSeconds($TimeoutSeconds)
    do {
        $device = Get-PnpDevice -InstanceId $InstanceId `
            -ErrorAction SilentlyContinue
        if ($null -ne $device) {
            $snapshot = Get-V1IdleSnapshot -Device $device
            if (& $Predicate $snapshot) { return $snapshot }
        }
        Start-Sleep -Milliseconds 500
    } while ([DateTimeOffset]::Now -lt $deadline)
    return $null
}

function Read-V1IdleFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return @() }
    return @(Get-Content -LiteralPath $Path | ForEach-Object {
        [string]$_ })
}

function Stop-V1IdleProcess {
    param([AllowNull()]$Process)
    if ($null -eq $Process) { return }
    if (-not $Process.HasExited) {
        $Process.Kill($true)
        $Process.WaitForExit()
    }
    $Process.Dispose()
}

function Test-V1IdleXm5Disconnected {
    $probe = Join-Path $ObserverCandidatePath `
        'tools\xm5_connection_probe.exe'
    if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) {
        return $true
    }
    $lines = @(& $probe --state 2>&1)
    return (($lines -join "`n") -match 'disconnected')
}

function Invoke-V1IdleProbeTrial {
    param(
        [Parameter(Mandatory = $true)][int]$DelaySeconds,
        [Parameter(Mandatory = $true)][string]$TrialRoot,
        [Parameter(Mandatory = $true)][int]$TrialIndex,
        [Parameter(Mandatory = $true)][int]$TotalTrials
    )
    Write-Host "=== Trial $TrialIndex/$($TotalTrials): idle open after $DelaySeconds s ==="
    Write-Host 'Action: power on XM5 now (no media, no playback).'
    Write-Host 'The probe auto-detects the connect and opens the channel; this takes up to ~2 minutes.'
    $trialDir = Join-Path $TrialRoot "delay-$DelaySeconds"
    New-Item -ItemType Directory -Path $trialDir -Force | Out-Null
    $connectOut = Join-Path $trialDir 'connect.out.log'
    $connectErr = Join-Path $trialDir 'connect.err.log'
    $outPath = Join-Path $trialDir 'probe.out.log'
    $errPath = Join-Path $trialDir 'probe.err.log'

    # Connection synchronization uses the system Bluetooth ACL watcher (the
    # same proven path as every earlier gate), NOT the observer driver's
    # generation counter, which does not advance on an idle connect.
    $connectProbe = Join-Path $ObserverCandidatePath `
        'tools\xm5_connection_probe.exe'
    $connectProcess = Start-Process -FilePath $connectProbe `
        -ArgumentList @('--wait-acl-connect', '300') `
        -RedirectStandardOutput $connectOut `
        -RedirectStandardError $connectErr `
        -PassThru -WindowStyle Hidden
    $connected = $false
    $connectDeadline = [DateTimeOffset]::Now.AddSeconds(330)
    while ([DateTimeOffset]::Now -lt $connectDeadline) {
        foreach ($line in @(Read-V1IdleFile $connectOut)) {
            if ($line -match 'XM5 ACL event: connected') { $connected = $true }
        }
        if ($connected) { break }
        if ($connectProcess.HasExited) { break }
        Start-Sleep -Milliseconds 250
    }
    Stop-V1IdleProcess $connectProcess
    if (-not $connected) {
        $connectError = @(Read-V1IdleFile $connectErr)
        Write-Host 'Trial aborted: no XM5 ACL connect was observed.'
        return [ordered]@{
            delay_seconds = $DelaySeconds
            passed = $false
            open_ok = $false
            open_failed_status_seen = $false
            channel_held = $false
            volume_or_passthrough_events = 0
            post_activation_line = ''
            final_status_line = ''
            same_channel_write_submitted = $false
            same_channel_write_responded = $false
            channel_hold_summary = ''
            probe_exit_code = $null
            stderr = 'no XM5 ACL connect observed; ' +
                ($connectError -join '; ')
        }
    }
    Write-Host 'XM5 connected. Starting the idle-open probe.'

    $process = Start-Process -FilePath $ProbePath `
        -ArgumentList @(
            '--delay-seconds', "$DelaySeconds",
            '--duration-seconds', "$ObservationSeconds",
            '--verify-same-channel-write') `
        -RedirectStandardOutput $outPath `
        -RedirectStandardError $errPath `
        -PassThru -WindowStyle Hidden
    $deadline = [DateTimeOffset]::Now.AddSeconds(
        $ObservationSeconds + $DelaySeconds + 60)
    while ([DateTimeOffset]::Now -lt $deadline -and
           -not $process.HasExited) {
        Start-Sleep -Milliseconds 500
    }
    if (-not $process.HasExited) {
        $process.Kill($true)
        $process.WaitForExit()
    }
    $probeExitCode = $process.ExitCode
    $process.Dispose()
    $lines = @(Read-V1IdleFile $outPath)
    $errLines = @(Read-V1IdleFile $errPath)
    $postActivation = @($lines | Where-Object {
        $_ -match 'post-activation status:' })
    $finalStatus = @($lines | Where-Object {
        $_ -match '^final status:' })
    $volumeEvents = @($lines | Where-Object {
        $_ -match 'type=(absolute-volume|pass-through|volume-capability)' })
    $openFailed = @($lines | Where-Object {
        $_ -match ('open 0x' + $script:OpenFailedStatus.Substring(2)) })
    $held = @($lines | Where-Object { $_ -match 'held=1' })
    $protocolError = @($lines | Where-Object { $_ -match 'protocol-error' })
    $writeSubmitted = @($lines | Where-Object {
        $_ -match '^same-channel write submitted value=\d+ pdu=0x50$' })
    $writeResponded = @($lines | Where-Object {
        $_ -match '^same-channel write response value=\d+ response=0x09$' })
    $holdSummary = @($lines | Where-Object {
        $_ -match '^channel hold summary: held_ms=\d+ released=[01]$' })
    # A channel is only usable when the AVCTP OPEN actually held or a
    # volume/passthrough event arrived; status codes such as 0xC00000D0,
    # 0xC00000BB (NOT_SUPPORTED) or 0xC0000001 are all failures.
    $openOk = ($held.Count -gt 0 -or $volumeEvents.Count -gt 0) -and
        $protocolError.Count -eq 0
    $passed = $probeExitCode -eq 0 -and $openOk -and
        $writeSubmitted.Count -gt 0 -and
        $writeResponded.Count -gt 0
    $trial = [ordered]@{
        delay_seconds = $DelaySeconds
        passed = [bool]$passed
        open_ok = [bool]$openOk
        open_failed_status_seen = $openFailed.Count -gt 0
        channel_held = $held.Count -gt 0
        volume_or_passthrough_events = $volumeEvents.Count
        same_channel_write_submitted = $writeSubmitted.Count -gt 0
        same_channel_write_responded = $writeResponded.Count -gt 0
        channel_hold_summary = if ($holdSummary.Count -gt 0) {
            $holdSummary[0]
        } else { '' }
        probe_exit_code = $probeExitCode
        post_activation_line = if ($postActivation.Count -gt 0) {
            $postActivation[0]
        } else { '' }
        final_status_line = if ($finalStatus.Count -gt 0) {
            $finalStatus[0]
        } else { '' }
        stderr = ($errLines -join '; ')
    }
    $openSummary = if ($trial.open_ok) { 'channel up' } else { 'OPEN failed' }
    Write-Host ("Trial $($TrialIndex) result: $openSummary " + `
        '(held=' + $trial.channel_held + ', volume/passthrough events=' +
        $trial.volume_or_passthrough_events + ', same-channel write=' +
        $trial.same_channel_write_responded + ')')
    if (-not $trial.passed) {
        Write-Host 'Action: power off XM5 now.'
    }
    return $trial
}

# --- Preflight -------------------------------------------------------------
Write-Host 'V1 AVRCP idle-activation experiment preflight:'
& (Join-Path $PSScriptRoot 'verify-v1-avrcp-observer-candidate.ps1') `
    -CandidatePath $ObserverCandidatePath
$observerPackage = Join-Path $ObserverCandidatePath 'package'
if (-not (Test-Path -LiteralPath `
        (Join-Path $observerPackage 'NativeLdacAvrcpObserver.inf') `
        -PathType Leaf)) {
    throw "The observer candidate package is incomplete: $ObserverCandidatePath"
}
foreach ($path in @($ProbePath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required tool is missing: $path"
    }
}
$target = Get-V1IdleTargetDevice
$baseline = Get-V1IdleSnapshot -Device $target
if (-not (Test-V1IdleMicrosoftBaseline -Snapshot $baseline)) {
    throw 'The exact XM5 AVRCP PDO is not the healthy Microsoft baseline.'
}
$observerPackages = @(Get-WindowsDriver -Online -All |
    Where-Object {
        (Split-Path -Leaf ([string]$_.OriginalFileName)) -ieq
            'NativeLdacAvrcpObserver.inf'
    })
if ($observerPackages.Count -ne 0) {
    throw "Historical observer packages block the experiment: $($observerPackages.published_inf -join ', ')"
}
if (-not (Test-V1IdleXm5Disconnected)) {
    throw 'XM5 must be physically off before the experiment starts.'
}
Write-Host 'Preflight passed. XM5 must remain off until each trial says to turn it on.'

# --- Trial layout ----------------------------------------------------------
$trialRoot = Join-Path $projectRoot (
    'artifacts\v1-volume-sync\trial\avrcp-idle-activation-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $trialRoot -Force | Out-Null
$resultPath = Join-Path $trialRoot 'result.json'
$result = [ordered]@{
    schema_version = 2
    created_at = (Get-Date).ToString('o')
    passed = $false
    experiment_completed = $false
    feasibility_passed = $false
    failure = ''
    failure_code = ''
    source_commit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
    observer_candidate = $ObserverCandidatePath
    baseline = $baseline
    delays_tested = @()
    trials = @()
    restore = $null
}

try {
    # --- Bind the observer function driver --------------------------------
    Write-Host 'Binding the observer function driver to the AVRCP PDO.'
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
    $observerBound = Wait-V1IdleSnapshot `
        -Predicate ${function:Test-V1IdleObserverBound} `
        -InstanceId $target.InstanceId
    if ($null -eq $observerBound) {
        Write-Host 'Observer did not bind immediately; restarting the PDO once.'
        $restartObserver = Invoke-V1AvrcpFilterPnpUtil -Arguments @(
            '/restart-device', $target.InstanceId)
        $restartObserver.lines | Set-Content -LiteralPath `
            (Join-Path $trialRoot 'observer-restart.log') -Encoding utf8
        if ($restartObserver.exit_code -ne 0) {
            throw "Observer bind restart failed (exit $($restartObserver.exit_code))."
        }
        $observerBound = Wait-V1IdleSnapshot `
            -Predicate ${function:Test-V1IdleObserverBound} `
            -InstanceId $target.InstanceId -TimeoutSeconds 60
    }
    if ($null -eq $observerBound) {
        throw 'The observer did not bind as the function driver.'
    }

    # --- Idle-open trials ------------------------------------------------
    if ($AllDelays) {
        $delays = @(0, 1, 5, 15)
    } else {
        $delays = @($DelaySeconds)
    }
    $passedDelay = $null
    for ($index = 0; $index -lt $delays.Count; $index++) {
        $delay = $delays[$index]
        $trial = Invoke-V1IdleProbeTrial `
            -DelaySeconds $delay `
            -TrialRoot $trialRoot `
            -TrialIndex ($index + 1) `
            -TotalTrials $delays.Count
        $result.trials += $trial
        if ($trial.passed -and $null -eq $passedDelay) {
            $passedDelay = $delay
        }
        if ($index + 1 -lt $delays.Count) {
            Write-Host ''
            [void](Read-Host 'Press Enter after XM5 is off to start the next trial')
            Write-Host ''
        }
    }
    $result.delays_tested = $delays
    $result.feasibility_passed = $null -ne $passedDelay
    $result.experiment_completed = $true
} catch {
    $result.failure = [string]$_
    $result.failure_code = 'avrcp-idle-activation-experiment-failed'
} finally {
    # --- Restore Microsoft AVRCP -----------------------------------------
    try {
        $deleteObserver = Invoke-V1AvrcpFilterPnpUtil -Arguments @(
            '/delete-driver', $publishedObserverInf, '/uninstall', '/force')
        $deleteObserver.lines | Set-Content -LiteralPath `
            (Join-Path $trialRoot 'observer-delete.log') -Encoding utf8
        (Invoke-V1AvrcpFilterPnpUtil -Arguments @('/scan-devices')).lines |
            Set-Content -LiteralPath `
                (Join-Path $trialRoot 'scan.log') -Encoding utf8
        $restoreResult = Invoke-V1AvrcpFilterPnpUtil -Arguments @(
            '/restart-device', $target.InstanceId)
        $restoreResult.lines | Set-Content -LiteralPath `
            (Join-Path $trialRoot 'restore.log') -Encoding utf8
        $restored = Wait-V1IdleSnapshot `
            -Predicate ${function:Test-V1IdleMicrosoftBaseline} `
            -InstanceId $target.InstanceId -TimeoutSeconds 60
        $result.restore = [ordered]@{
            passed = $null -ne $restored
            observer_delete_exit = $deleteObserver.exit_code
            restore_restart_exit = $restoreResult.exit_code
        }
    } catch {
        $result.restore = [ordered]@{
            passed = $false
            error = [string]$_
        }
    }
    $result.passed = [bool](
        $result.experiment_completed -and $result.restore.passed)
    if ($result.experiment_completed -and -not $result.restore.passed -and
        [string]::IsNullOrWhiteSpace($result.failure)) {
        $result.failure = 'The experiment completed, but Microsoft AVRCP was not restored.'
        $result.failure_code = 'avrcp-idle-activation-restore-failed'
    }
    Write-V1AvrcpFilterJsonAtomically -Path $resultPath -Value $result
}

if ($result.passed) {
    Write-Host 'V1 AVRCP idle-activation experiment completed.'
    Write-Host "Feasibility passed: $($result.feasibility_passed)"
    Write-Host "Result: $resultPath"
} else {
    Write-Host "V1 AVRCP idle-activation experiment failed: $($result.failure)"
    Write-Host "Result: $resultPath"
    exit 1
}
