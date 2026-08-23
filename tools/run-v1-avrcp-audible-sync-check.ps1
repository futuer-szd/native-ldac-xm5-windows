# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1AvrcpAudibleSync,
    [ValidateRange(20, 120)]
    [int]$ObservationSeconds = 60,
    [ValidateRange(0, 120)]
    [int]$PauseObservationSeconds = 0,
    [ValidateRange(0, 60)]
    [int]$ResumeObservationSeconds = 15,
    [ValidateRange(0, 30)]
    [int]$ResumeCooldownSeconds = 5,
    [switch]$PauseKeepSession,
    [switch]$SkipSyncWindow,
    [ValidateSet('off', 'up', 'down')]
    [string]$FirstAuthorityDirection = 'off',
    [ValidateRange(0, 120)]
    [int]$BoundaryCheckSeconds = 0,
    [ValidateRange(0, 120)]
    [int]$MediaKeyCheckSeconds = 0,
    [switch]$DiagnoseMediaKeys,
    [ValidateRange(0, 120)]
    [int]$DefaultSettleSeconds = 8,
    [ValidateRange(120, 600)]
    [int]$ConnectTimeoutSeconds = 300,
    [string]$ObserverCandidatePath,
    [string]$TransportProbePath,
    [string]$ConnectionProbePath,
    [string]$ExecutorPath,
    [string]$EndpointVolumeProbePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Native tools (endpoint_volume_probe, transport_probe, ...) emit UTF-8.
# Decode their output as UTF-8 regardless of the console codepage, so
# endpoint names such as "耳机" are not mojibake'd into the results.
try {
    [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
} catch {
    # Some hosts disallow changing the console encoding; the endpoint-probe
    # helper below re-applies it around each invocation.
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'v1-avrcp-filter-gate-common.ps1')

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The V1 AVRCP audible sync check requires PowerShell 7.'
}
Assert-V1AvrcpFilterAdministrator
if (-not $ConfirmV1AvrcpAudibleSync) {
    throw 'Refusing the AVRCP audible sync check. Re-run with -ConfirmV1AvrcpAudibleSync.'
}

$script:TargetPrefix = `
    'BTHENUM\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
$script:MicrosoftInf = 'microsoft_bluetooth_avrcptransport.inf'
$script:MicrosoftService = 'Microsoft_Bluetooth_AvrcpTransport'
$script:ObserverInf = 'NativeLdacAvrcpObserver.inf'
$script:ObserverService = 'NativeLdacAvrcpObserver'

if ([string]::IsNullOrWhiteSpace($ObserverCandidatePath)) {
    $ObserverCandidatePath = Join-Path $projectRoot `
        'artifacts\v1-volume-sync\avrcp-observer-candidate'
} else {
    $ObserverCandidatePath = $ExecutionContext.SessionState.Path.
        GetUnresolvedProviderPathFromPSPath($ObserverCandidatePath)
}
if ([string]::IsNullOrWhiteSpace($TransportProbePath)) {
    $TransportProbePath = Join-Path $ObserverCandidatePath `
        'tools\transport_probe.exe'
} else {
    $TransportProbePath = $ExecutionContext.SessionState.Path.
        GetUnresolvedProviderPathFromPSPath($TransportProbePath)
}
if ([string]::IsNullOrWhiteSpace($ConnectionProbePath)) {
    $ConnectionProbePath = Join-Path $ObserverCandidatePath `
        'tools\xm5_connection_probe.exe'
} else {
    $ConnectionProbePath = $ExecutionContext.SessionState.Path.
        GetUnresolvedProviderPathFromPSPath($ConnectionProbePath)
}
if ([string]::IsNullOrWhiteSpace($ExecutorPath)) {
    $ExecutorPath = Join-Path $projectRoot `
        'build\protocol\Release\v1_avrcp_action_executor.exe'
} else {
    $ExecutorPath = $ExecutionContext.SessionState.Path.
        GetUnresolvedProviderPathFromPSPath($ExecutorPath)
}
if ([string]::IsNullOrWhiteSpace($EndpointVolumeProbePath)) {
    $EndpointVolumeProbePath = Join-Path $projectRoot `
        'build\protocol\Release\endpoint_volume_probe.exe'
} else {
    $EndpointVolumeProbePath = $ExecutionContext.SessionState.Path.
        GetUnresolvedProviderPathFromPSPath($EndpointVolumeProbePath)
}

function Get-V1AudibleSyncTargetDevice {
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

function Get-V1AudibleSyncSnapshot {
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

function Test-V1AudibleSyncMicrosoftBaseline {
    param([Parameter(Mandatory = $true)]$Snapshot)
    return $Snapshot.present -and
        $Snapshot.status -eq 'OK' -and
        [int]$Snapshot.problem_code -eq 0 -and
        [string]$Snapshot.inf -ieq $script:MicrosoftInf -and
        [string]$Snapshot.service -ieq $script:MicrosoftService
}

function Test-V1AudibleSyncObserverBound {
    param([Parameter(Mandatory = $true)]$Snapshot)
    return $Snapshot.present -and
        $Snapshot.status -eq 'OK' -and
        [int]$Snapshot.problem_code -eq 0 -and
        ([string]$Snapshot.inf -match '^oem\d+\.inf$') -and
        [string]$Snapshot.service -ieq $script:ObserverService
}

function Wait-V1AudibleSyncSnapshot {
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
            $snapshot = Get-V1AudibleSyncSnapshot -Device $device
            if (& $Predicate $snapshot) { return $snapshot }
        }
        Start-Sleep -Milliseconds 500
    } while ([DateTimeOffset]::Now -lt $deadline)
    return $null
}

function Stop-V1AudibleSyncProcess {
    param([AllowNull()]$Process)
    if ($null -eq $Process) { return }
    if (-not $Process.HasExited) {
        $Process.Kill($true)
        $Process.WaitForExit()
    }
    $Process.Dispose()
}

function Read-V1AudibleSyncFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [switch]$IncludePartial
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return @() }
    [string]$text = Get-Content -LiteralPath $Path -Raw
    if ([string]::IsNullOrEmpty($text)) { return @() }
    $normalized = $text.Replace("`r`n", "`n").Replace("`r", "`n")
    $lines = @($normalized.Split("`n", [StringSplitOptions]::None))
    $lineCount = $lines.Count
    $hasTrailingNewline = $normalized.EndsWith("`n")
    if ($hasTrailingNewline -or -not $IncludePartial) {
        --$lineCount
    }
    if ($lineCount -le 0) { return @() }
    return @($lines[0..($lineCount - 1)] | ForEach-Object {
        [string]$_
    })
}

function Get-V1AudibleSyncMediaKeySummary {
    param([Parameter(Mandatory = $true)][string[]]$Lines)
    $rawEvents = @($Lines | Where-Object {
        $_ -match '^diagnostic: pass-through sequence='
    })
    $pressEvents = @($rawEvents | Where-Object { $_ -match 'released=no' })
    $releaseEvents = @($rawEvents | Where-Object { $_ -match 'released=yes' })
    $playEvents = @($rawEvents | Where-Object { $_ -match 'operation=0x44' })
    $pauseEvents = @($rawEvents | Where-Object { $_ -match 'operation=0x46' })
    $injected = @($Lines | Where-Object { $_ -match '^action inject vk=0x' })
    $sinkDiagnostics = @($Lines | Where-Object {
        $_ -match '^diagnostic: [^ ]+ vk=0x[0-9A-Fa-f]+ sent=\d+ requested=\d+ error=\d+$'
    })
    $sinkFailures = @($sinkDiagnostics | Where-Object {
        if ($_ -match 'sent=(\d+) requested=(\d+) error=(\d+)') {
            [int]$Matches[1] -ne [int]$Matches[2] -or
                [int]$Matches[3] -ne 0
        } else {
            $true
        }
    })
    $feedFailures = @($Lines | Where-Object {
        $_ -match 'pass-through result .*feed_ok=no'
    })
    $playbackStatusNotifications = @($Lines | Where-Object {
        $_ -match '^action notify-playback-status=\d+ ' +
            '\((sent|queued; transaction-aware)\)$'
    })
    return [ordered]@{
        raw_pass_through_event_count = $rawEvents.Count
        press_count = $pressEvents.Count
        release_count = $releaseEvents.Count
        play_operation_count = $playEvents.Count
        pause_operation_count = $pauseEvents.Count
        injected_count = $injected.Count
        sink_diagnostic_count = $sinkDiagnostics.Count
        sink_failure_count = $sinkFailures.Count
        feed_failure_count = $feedFailures.Count
        playback_status_notification_count = $playbackStatusNotifications.Count
    }
}

function Get-V1AudibleSyncDefaultRenderName {
    $previousOutputEncoding = [Console]::OutputEncoding
    try {
        [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
        $lines = @(& $EndpointVolumeProbePath --info --all 2>&1)
    } finally {
        [Console]::OutputEncoding = $previousOutputEncoding
    }
    $currentName = ''
    foreach ($line in $lines) {
        if ($line -match '^Endpoint: (.*)$') {
            $currentName = $Matches[1].Trim()
        } elseif ($line -match 'default roles:.*console') {
            return $currentName
        }
    }
    return ''
}

function Get-V1AudibleSyncDefaultRenderVolume {
    $previousOutputEncoding = [Console]::OutputEncoding
    try {
        [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
        $lines = @(& $EndpointVolumeProbePath --info --all 2>&1)
    } finally {
        [Console]::OutputEncoding = $previousOutputEncoding
    }
    $currentName = ''
    $currentVolume = $null
    foreach ($line in $lines) {
        if ($line -match '^Endpoint: (.*)$') {
            $currentName = $Matches[1].Trim()
        } elseif ($line -match 'volume:\s*([\d.]+)%') {
            $currentVolume = [double]$Matches[1]
        } elseif ($line -match 'default roles:.*console') {
            if ($null -ne $currentVolume) {
                return $currentVolume
            }
            return -1.0
        }
    }
    return -1.0
}

function Restore-V1AudibleSyncMicrosoft {
    param(
        [Parameter(Mandatory = $true)][string]$PublishedInf,
        [Parameter(Mandatory = $true)][string]$InstanceId,
        [Parameter(Mandatory = $true)][string]$TrialRoot
    )
    try {
        $deleteObserver = Invoke-V1AvrcpFilterPnpUtil -Arguments @(
            '/delete-driver', $PublishedInf, '/uninstall', '/force')
        $deleteObserver.lines | Set-Content -LiteralPath `
            (Join-Path $TrialRoot 'observer-delete.log') -Encoding utf8
        $scanObserver = Invoke-V1AvrcpFilterPnpUtil -Arguments @('/scan-devices')
        $scanObserver.lines | Set-Content -LiteralPath `
            (Join-Path $TrialRoot 'scan.log') -Encoding utf8
        $restoreResult = Invoke-V1AvrcpFilterPnpUtil -Arguments @(
            '/restart-device', $InstanceId)
        $restoreResult.lines | Set-Content -LiteralPath `
            (Join-Path $TrialRoot 'restore.log') -Encoding utf8
        $restored = Wait-V1AudibleSyncSnapshot `
            -Predicate ${function:Test-V1AudibleSyncMicrosoftBaseline} `
            -InstanceId $InstanceId -TimeoutSeconds 45
        return [ordered]@{
            passed = $null -ne $restored
            observer_delete_exit = $deleteObserver.exit_code
            restore_restart_exit = $restoreResult.exit_code
        }
    } catch {
        return [ordered]@{
            passed = $false
            error = [string]$_
        }
    }
}

# --- Preflight -------------------------------------------------------------
Write-Host 'V1 AVRCP audible sync check preflight:'
Write-Host "  observer candidate: $ObserverCandidatePath"
& (Join-Path $PSScriptRoot 'verify-v1-avrcp-observer-candidate.ps1') `
    -CandidatePath $ObserverCandidatePath
$observerPackage = Join-Path $ObserverCandidatePath 'package'
if (-not (Test-Path -LiteralPath `
        (Join-Path $observerPackage 'NativeLdacAvrcpObserver.inf') `
        -PathType Leaf)) {
    throw "The observer candidate package is incomplete: $ObserverCandidatePath"
}
foreach ($path in @(
        $TransportProbePath, $ConnectionProbePath, $ExecutorPath,
        $EndpointVolumeProbePath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required tool is missing: $path"
    }
}

$target = Get-V1AudibleSyncTargetDevice
$baseline = Get-V1AudibleSyncSnapshot -Device $target
if (-not (Test-V1AudibleSyncMicrosoftBaseline -Snapshot $baseline)) {
    throw ("The exact XM5 AVRCP PDO is not the healthy Microsoft baseline " +
        "(inf=$($baseline.inf), service=$($baseline.service), " +
        "problem=$($baseline.problem_code)). If a previous run left the " +
        'observer bound, run recover-observer-clean.ps1 through the ' +
        'documented elevated recovery flow.')
}
$observerPackages = @(Get-WindowsDriver -Online -All |
    Where-Object {
        (Split-Path -Leaf ([string]$_.OriginalFileName)) -ieq
            'NativeLdacAvrcpObserver.inf'
    })
if ($observerPackages.Count -ne 0) {
    throw "Historical observer packages block the check: $($observerPackages.published_inf -join ', ')"
}
$stateLines = @(& (Join-Path $ObserverCandidatePath `
    'tools\xm5_connection_probe.exe') --state 2>&1)
if (($stateLines -join "`n") -notmatch 'disconnected') {
    throw 'XM5 must be physically off and disconnected before the check starts.'
}

Write-Host 'Preflight passed. XM5 must remain off until the media probe arms.'

# --- Trial layout ----------------------------------------------------------
$trialRoot = Join-Path $projectRoot (
    'artifacts\v1-volume-sync\trial\avrcp-audible-sync-' +
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
    observer_candidate = $ObserverCandidatePath
    diagnose_media_keys = [bool]$DiagnoseMediaKeys
    baseline = $baseline
    default_render = ''
    pc_volume_before_media = -1.0
    steps = @()
    evidence = [ordered]@{
        executor_lines = @()
        media_lines = @()
    }
    diagnostics = $null
    summary = $null
    pause = $null
    resume = $null
    boundary = $null
    media_keys = $null
    restore = $null
}

$mediaOut = Join-Path $trialRoot 'media.out.log'
$mediaErr = Join-Path $trialRoot 'media.err.log'
$connectOut = Join-Path $trialRoot 'connect.out.log'
$connectErr = Join-Path $trialRoot 'connect.err.log'
$executorOut = Join-Path $trialRoot 'executor.out.log'
$executorErr = Join-Path $trialRoot 'executor.err.log'

$mediaProcess = $null
$executorProcess = $null
$connectProcess = $null
$resumeMediaProcess = $null
$observerInstalled = $false
$mediaStopEvent = $null
$resumeStopEvent = $null
$executorDuration = $ObservationSeconds + $BoundaryCheckSeconds + `
    $MediaKeyCheckSeconds + $PauseObservationSeconds + `
    $(if ($PauseObservationSeconds -gt 0) {
        $ResumeObservationSeconds
    } else {
        0
    }) + 30

try {
    # --- Phase 1: ACL watcher + audible media ----------------------------
    Write-Host 'Phase 1: arming the ACL watcher. Turn on XM5 now.'
    $connectProcess = Start-Process -FilePath $ConnectionProbePath `
        -ArgumentList @('--wait-acl-connect', "$ConnectTimeoutSeconds") `
        -RedirectStandardOutput $connectOut `
        -RedirectStandardError $connectErr `
        -PassThru -WindowStyle Hidden
    $armedDeadline = [DateTimeOffset]::Now.AddSeconds(30)
    $armed = $false
    while ([DateTimeOffset]::Now -lt $armedDeadline) {
        foreach ($line in (Read-V1AudibleSyncFile $connectOut)) {
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
    $connectDeadline = [DateTimeOffset]::Now.AddSeconds($ConnectTimeoutSeconds)
    while ([DateTimeOffset]::Now -lt $connectDeadline) {
        foreach ($line in (Read-V1AudibleSyncFile $connectOut)) {
            if ($line -match 'XM5 ACL event: connected') { $connected = $true }
        }
        if ($connected) { break }
        if ($connectProcess.HasExited) {
            throw 'The ACL watcher exited before a physical connect was observed.'
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $connected) {
        throw 'A physical XM5 ACL connect was not observed in time.'
    }
    $baselineDefaultRender = Get-V1AudibleSyncDefaultRenderName
    if ([string]::IsNullOrWhiteSpace($baselineDefaultRender)) {
        throw 'Could not determine the default render endpoint.'
    }
    if ($baselineDefaultRender -match 'XM5|Sony') {
        throw ("The default render endpoint is '$baselineDefaultRender'. " +
            'The audible check requires a PC-local default device (e.g. G24H1); ' +
            'set it in Sound settings and re-run.')
    }
    $result.default_render = $baselineDefaultRender
    Write-Host "Default render endpoint: $baselineDefaultRender"
    $pcVolumeBeforeMedia = Get-V1AudibleSyncDefaultRenderVolume
    if ($pcVolumeBeforeMedia -lt 0.0) {
        throw 'Could not determine the PC volume before media start.'
    }
    $result.pc_volume_before_media = $pcVolumeBeforeMedia
    Write-Host ("PC volume before media: $pcVolumeBeforeMedia%")
    if ($FirstAuthorityDirection -eq 'up') {
        Write-Host ("Set the XM5 volume clearly HIGHER than " +
            "$pcVolumeBeforeMedia% now, then leave it.")
    } elseif ($FirstAuthorityDirection -eq 'down') {
        Write-Host ("Set the XM5 volume clearly LOWER than " +
            "$pcVolumeBeforeMedia% now, then leave it.")
    }
    # This machine can auto-switch the default render endpoint to the XM5
    # Hands-Free device ~20 s after the XM5 connects. Wait through that
    # settle window before asking the user to start media, so the run is not
    # invalidated by the delayed switch.
    if ($DefaultSettleSeconds -gt 0) {
        Write-Host ("Waiting up to $DefaultSettleSeconds s for the default " +
            'render endpoint to settle...')
        $settleDeadline = [DateTimeOffset]::Now.AddSeconds($DefaultSettleSeconds)
        $lastSettleCheckTick = 0
        while ([DateTimeOffset]::Now -lt $settleDeadline) {
            if ([Environment]::TickCount64 - $lastSettleCheckTick -ge 2000) {
                $lastSettleCheckTick = [Environment]::TickCount64
                $currentDefault = Get-V1AudibleSyncDefaultRenderName
                if ($currentDefault -ne $baselineDefaultRender) {
                    Write-Host ("  default render endpoint changed to " +
                        "'$currentDefault' (waiting for it to settle)")
                    $baselineDefaultRender = $currentDefault
                }
            }
            Start-Sleep -Milliseconds 250
        }
        $currentDefault = Get-V1AudibleSyncDefaultRenderName
        if ($currentDefault -match 'XM5|Sony') {
            Write-Host ("The default render endpoint settled on '$currentDefault'.")
            Write-Host 'Set the default playback device to a PC-local device'
            Write-Host '(e.g. 2 - G24H1) in Sound settings, then press Enter.'
            [void](Read-Host 'Press Enter after setting the default device')
            $currentDefault = Get-V1AudibleSyncDefaultRenderName
            if ($currentDefault -match 'XM5|Sony') {
                throw ("The default render endpoint is still '$currentDefault'. " +
                    'Re-run after fixing it.')
            }
            $baselineDefaultRender = $currentDefault
        }
        $result.default_render = $baselineDefaultRender
        Write-Host "Default render endpoint: $baselineDefaultRender"
    }
    Write-Host 'XM5 connected. The WASAPI loopback capture needs audio that is'
    Write-Host 'already playing on the PC default endpoint before it starts.'
    Write-Host 'Start any music or video now and keep it playing.'
    [void](Read-Host 'Press Enter here once audio is playing on the PC')
    $defaultBeforeMedia = Get-V1AudibleSyncDefaultRenderName
    if ($defaultBeforeMedia -ne $baselineDefaultRender) {
        throw ("The default render endpoint changed before media start " +
            "('$baselineDefaultRender' -> '$defaultBeforeMedia'). Fix it and re-run.")
    }
    Write-Host 'Starting audible LDAC playback.'
    $mediaStopEventName = 'Local\NativeLdacAudibleSyncStop-' +
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
            '--play-system',
            '--open-attempts', '1',
            '--stop-event', $mediaStopEventName) `
        -RedirectStandardOutput $mediaOut `
        -RedirectStandardError $mediaErr `
        -PassThru -WindowStyle Hidden

    $mediaReady = $false
    $mediaDeadline = [DateTimeOffset]::Now.AddSeconds(120)
    while ([DateTimeOffset]::Now -lt $mediaDeadline) {
        foreach ($line in (Read-V1AudibleSyncFile $mediaOut)) {
            if ($line -match 'XM5 accepted START; the LDAC Media transport is ready') {
                $mediaReady = $true
            }
        }
        if ($mediaReady) { break }
        if ($mediaProcess.HasExited) {
            $diagnostic = @(Read-V1AudibleSyncFile $mediaErr)
            throw ('The audible media process exited before START. ' +
                ($diagnostic -join ' '))
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $mediaReady) {
        throw 'The audible media session did not reach START.'
    }
    Write-Host 'Audible LDAC playback is active. Keep the music or video playing.'

    # --- Phase 2: observer handoff (function driver switch) ---------------
    # The switch happens only after media START, matching the verified
    # write-path gate order; switching while XM5 is off leaves the observer's
    # single outbound AVCTP OPEN without a ready peer and no event stream.
    Write-Host 'Phase 2: switching the exact PDO to the observer function driver.'
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
    $observerBound = Wait-V1AudibleSyncSnapshot `
        -Predicate ${function:Test-V1AudibleSyncObserverBound} `
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
        $observerBound = Wait-V1AudibleSyncSnapshot `
            -Predicate ${function:Test-V1AudibleSyncObserverBound} `
            -InstanceId $target.InstanceId -TimeoutSeconds 45
    }
    if ($null -eq $observerBound) {
        throw 'The observer did not bind as the function driver after the handoff restart.'
    }
    $result.steps += [ordered]@{
        action = 'observer-handoff'
        passed = $true
    }
    $observerInstalled = $true

    # --- Phase 3: executor write mode + listening window -----------------
    Write-Host 'Phase 3: arming the write-mode executor.'
    $executorArguments = @(
        '--live',
        '--duration-seconds', "$executorDuration",
        '--volume-sync',
        '--route-media-keys',
        '--apply')
    if ($DiagnoseMediaKeys) {
        $executorArguments += '--diagnose-media-keys'
        Write-Host 'Media-key diagnostics are enabled.'
        Write-Host 'PAUSE/RESUME will test the XM5 double-tap, not just manual PC state changes.'
        Write-Host 'Use one double-tap at a time; wait for the phase result before retrying.'
    }
    $executorProcess = Start-Process -FilePath $ExecutorPath `
        -ArgumentList $executorArguments `
        -RedirectStandardOutput $executorOut `
        -RedirectStandardError $executorErr `
        -PassThru -WindowStyle Hidden

    $controlReady = $false
    $controlReadyDeadline = [DateTimeOffset]::Now.AddSeconds(20)
    while ([DateTimeOffset]::Now -lt $controlReadyDeadline) {
        foreach ($line in (Read-V1AudibleSyncFile $executorOut)) {
            if ($line -match '^live: control channel ready;') {
                $controlReady = $true
                break
            }
        }
        if ($controlReady) { break }
        if ($executorProcess.HasExited) {
            $diagnostic = @(Read-V1AudibleSyncFile $executorErr)
            throw ('The AVRCP executor exited before the absolute-volume ' +
                'control channel became ready. ' +
                ($diagnostic -join ' '))
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $controlReady) {
        throw 'The absolute-volume control channel did not become ready within 20 seconds.'
    }

    if ($SkipSyncWindow) {
        Write-Host '=== SYNC WINDOW (warm-up only; sync already verified) ==='
        Write-Host 'No volume action is required. Waiting for the session to stabilize.'
    } else {
        Write-Host '=== XM5 AUDIBLE SYNC WINDOW READY ==='
        Write-Host '1. Swipe the XM5 volume up and down once.'
        Write-Host '2. Change the PC volume: +2 steps, wait 2 seconds, then -2 steps.'
    }

    $observationDeadline = [DateTimeOffset]::Now.AddSeconds(
        $ObservationSeconds + 10)
    $pcWriteSeen = $false
    $halfwayReminderShown = $false
    $lastExecutorLineCount = 0
    $lastMediaLineCount = 0
    $lastMediaLiveShownTick = 0
    $lastDefaultCheckTick = 0
    $executorExitedEarly = $false
    while ([DateTimeOffset]::Now -lt $observationDeadline) {
        $executorLines = @(Read-V1AudibleSyncFile $executorOut)
        if ($executorLines.Count -gt $lastExecutorLineCount) {
            for ($lineIndex = $lastExecutorLineCount;
                 $lineIndex -lt $executorLines.Count;
                 $lineIndex++) {
                Write-Host ("[executor] " + $executorLines[$lineIndex])
            }
            $lastExecutorLineCount = $executorLines.Count
        }
        $mediaLines = @(Read-V1AudibleSyncFile $mediaOut)
        if ($mediaLines.Count -gt $lastMediaLineCount) {
            for ($lineIndex = $lastMediaLineCount;
                 $lineIndex -lt $mediaLines.Count;
                 $lineIndex++) {
                if ($mediaLines[$lineIndex] -match '^Live:' -and
                    [Environment]::TickCount64 - $lastMediaLiveShownTick -ge 5000) {
                    $lastMediaLiveShownTick = [Environment]::TickCount64
                    Write-Host ("[media] " + $mediaLines[$lineIndex])
                }
            }
            $lastMediaLineCount = $mediaLines.Count
        }
        if (-not $pcWriteSeen) {
            foreach ($line in $executorLines) {
                if ($line -match 'action send-xm5-volume value=\d+ \(sent\)') {
                    $pcWriteSeen = $true
                }
            }
        }
        if (-not $SkipSyncWindow -and
            -not $pcWriteSeen -and -not $halfwayReminderShown -and
            ($observationDeadline - [DateTimeOffset]::Now).TotalSeconds -le
                ($ObservationSeconds / 2)) {
            $halfwayReminderShown = $true
            Write-Host 'No PC-to-XM5 write observed yet. Adjust the PC volume'
            Write-Host 'now (+2 steps, wait, then -2 steps) and listen to the XM5.'
        }
        if ([Environment]::TickCount64 - $lastDefaultCheckTick -ge 2000) {
            $lastDefaultCheckTick = [Environment]::TickCount64
            $currentDefault = Get-V1AudibleSyncDefaultRenderName
            if ($currentDefault -ne $baselineDefaultRender) {
                throw ("The default render endpoint changed during the run " +
                    "('$baselineDefaultRender' -> '$currentDefault'). " +
                    'Fix the default device and re-run.')
            }
            if ($mediaProcess.HasExited) {
                $mediaDiag = @(Read-V1AudibleSyncFile $mediaErr) -join '; '
                throw "The audible media process exited during the window: $mediaDiag"
            }
        }
        if ($executorProcess.HasExited) {
            $executorExitedEarly = $true
            break
        }
        Start-Sleep -Milliseconds 250
    }

    # --- Phase 3.4a: boundary volume check (optional) ----------------------
    if ($BoundaryCheckSeconds -gt 0) {
        Write-Host '=== BOUNDARY VOLUME CHECK ==='
        Write-Host 'Swipe the XM5 volume to 0% and then to 100% (either'
        Write-Host 'order). The phase advances automatically once both are'
        Write-Host ("observed, up to $BoundaryCheckSeconds s.")
        $boundaryLineBase = @(Read-V1AudibleSyncFile $executorOut).Count
        $boundaryDeadline = [DateTimeOffset]::Now.AddSeconds($BoundaryCheckSeconds)
        $boundaryReminderShown = $false
        $boundaryZeroSeen = $false
        $boundaryHundredSeen = $false
        while ([DateTimeOffset]::Now -lt $boundaryDeadline) {
            $executorLines = @(Read-V1AudibleSyncFile $executorOut)
            if ($executorLines.Count -gt $lastExecutorLineCount) {
                for ($lineIndex = $lastExecutorLineCount;
                     $lineIndex -lt $executorLines.Count;
                     $lineIndex++) {
                    Write-Host ("[executor] " + $executorLines[$lineIndex])
                }
                $lastExecutorLineCount = $executorLines.Count
            }
            foreach ($line in @(
                    $executorLines |
                        Select-Object -Skip $boundaryLineBase)) {
                if ($line -match 'set-windows-volume percent=0|muted=yes') {
                    $boundaryZeroSeen = $true
                }
                if ($line -match 'set-windows-volume percent=100') {
                    $boundaryHundredSeen = $true
                }
            }
            if ($boundaryZeroSeen -and $boundaryHundredSeen) {
                Write-Host 'BOUNDARY satisfied (0% and 100% both seen); advancing.'
                break
            }
            if (-not $boundaryReminderShown -and
                ($boundaryDeadline - [DateTimeOffset]::Now).TotalSeconds -le
                    ($BoundaryCheckSeconds / 2)) {
                $boundaryReminderShown = $true
                Write-Host 'Still in the boundary window: swipe to 0%, then to 100%.'
            }
            if ($executorProcess.HasExited) { break }
            Start-Sleep -Milliseconds 250
        }
        $boundaryLines = @(
            Read-V1AudibleSyncFile $executorOut |
                Select-Object -Skip $boundaryLineBase)
        $boundaryZeroOrMute = @($boundaryLines | Where-Object {
            $_ -match 'set-windows-volume percent=0|muted=yes' })
        $boundaryHundred = @($boundaryLines | Where-Object {
            $_ -match 'set-windows-volume percent=100' })
        $result.boundary = [ordered]@{
            enabled = $true
            seconds = $BoundaryCheckSeconds
            zero_or_mute_seen = $boundaryZeroOrMute.Count -gt 0
            hundred_seen = $boundaryHundred.Count -gt 0
            satisfied = $boundaryZeroOrMute.Count -gt 0 -and
                $boundaryHundred.Count -gt 0
            executor_alive = $null -ne $executorProcess -and
                -not $executorProcess.HasExited
            executor_lines = @($boundaryLines)
        }
        $result.steps += [ordered]@{
            action = 'boundary-phase'
            passed = [bool]$result.boundary.satisfied
            zero_or_mute_seen = $result.boundary.zero_or_mute_seen
            hundred_seen = $result.boundary.hundred_seen
        }
        Write-Host ('BOUNDARY done. 0/mute seen: ' +
            $result.boundary.zero_or_mute_seen + ', 100% seen: ' +
            $result.boundary.hundred_seen + ', satisfied: ' +
            $result.boundary.satisfied)
    }

    # --- Phase 3.4b: media key check (optional) ---------------------------
    if ($MediaKeyCheckSeconds -gt 0) {
        Write-Host '=== MEDIA KEY CHECK ==='
        Write-Host 'Press XM5: play/pause, then next, then previous'
        Write-Host ('(as supported by the current media session) within the ' +
            "next $MediaKeyCheckSeconds s. The window runs for the full " +
            'configured duration.')
        $mediaKeyLineBase = @(Read-V1AudibleSyncFile $executorOut).Count
        $mediaKeyDeadline = [DateTimeOffset]::Now.AddSeconds($MediaKeyCheckSeconds)
        $mediaKeyReminderShown = $false
        while ([DateTimeOffset]::Now -lt $mediaKeyDeadline) {
            $executorLines = @(Read-V1AudibleSyncFile $executorOut)
            if ($executorLines.Count -gt $lastExecutorLineCount) {
                for ($lineIndex = $lastExecutorLineCount;
                     $lineIndex -lt $executorLines.Count;
                     $lineIndex++) {
                    Write-Host ("[executor] " + $executorLines[$lineIndex])
                }
                $lastExecutorLineCount = $executorLines.Count
            }
            if (-not $mediaKeyReminderShown -and
                ($mediaKeyDeadline - [DateTimeOffset]::Now).TotalSeconds -le
                    ($MediaKeyCheckSeconds / 2)) {
                $mediaKeyReminderShown = $true
                Write-Host 'Still in the media key window: press play/pause, next, previous.'
            }
            if ($executorProcess.HasExited) { break }
            Start-Sleep -Milliseconds 250
        }
        $mediaKeyLines = @(
            Read-V1AudibleSyncFile $executorOut |
                Select-Object -Skip $mediaKeyLineBase)
        $injected = @($mediaKeyLines | Where-Object {
            $_ -match 'action inject vk=0x' })
        $playPauseInjected = @($mediaKeyLines | Where-Object {
            $_ -match 'action inject vk=0x00B3\b' })
        $nextInjected = @($mediaKeyLines | Where-Object {
            $_ -match 'action inject vk=0x00B0\b' })
        $previousInjected = @($mediaKeyLines | Where-Object {
            $_ -match 'action inject vk=0x00B1\b' })
        $result.media_keys = [ordered]@{
            enabled = $true
            seconds = $MediaKeyCheckSeconds
            injected_count = $injected.Count
            play_pause_injected_count = $playPauseInjected.Count
            next_injected_count = $nextInjected.Count
            previous_injected_count = $previousInjected.Count
            all_standard_keys_seen = $playPauseInjected.Count -gt 0 -and
                $nextInjected.Count -gt 0 -and
                $previousInjected.Count -gt 0
            satisfied = $injected.Count -gt 0
            executor_alive = $null -ne $executorProcess -and
                -not $executorProcess.HasExited
            executor_lines = @($mediaKeyLines)
        }
        $result.steps += [ordered]@{
            action = 'media-key-phase'
            passed = [bool]$result.media_keys.satisfied
            injected_count = $injected.Count
            play_pause_injected_count = $playPauseInjected.Count
            next_injected_count = $nextInjected.Count
            previous_injected_count = $previousInjected.Count
            all_standard_keys_seen = [bool]$result.media_keys.all_standard_keys_seen
        }
        Write-Host ('MEDIA KEY window complete. Injected media keys: ' +
            $injected.Count + '; play/pause=' + $playPauseInjected.Count +
            ', next=' + $nextInjected.Count + ', previous=' +
            $previousInjected.Count + '; all standard keys seen: ' +
            $result.media_keys.all_standard_keys_seen + '; satisfied: ' +
            $result.media_keys.satisfied)
    }

    # --- Phase 3.5: pause phase (optional) --------------------------------
    # Simulates a player pause. With -PauseKeepSession the LDAC media session
    # stays alive (silence) while the player pauses, matching phone-style
    # pause; without it the LDAC stream stops (SUSPEND -> CLOSE), which the
    # XM5 treats as the end of the session (sync dies and a fresh AVDTP
    # signaling re-open fails with Win32 121 on the same ACL).
    if ($PauseObservationSeconds -gt 0) {
        Write-Host '=== PAUSE PHASE ==='
        if ($PauseKeepSession) {
            Write-Host 'Keep the LDAC session alive.'
            if ($DiagnoseMediaKeys) {
                Write-Host 'Leave the PC player PLAYING; do not pause it manually first.'
                Write-Host 'Double-tap the XM5 once now to request PAUSE, then wait 5 seconds.'
                Write-Host 'If the first attempt has no effect, retry only once after the reminder.'
            } else {
                Write-Host 'PAUSE your player now.'
            }
            Write-Host 'After pause, the LDAC stream continues with silence.'
            Write-Host ("Swipe the XM5 volume after the pause and again every ~10 seconds " +
                "for the next $PauseObservationSeconds seconds.")
        } else {
            Write-Host 'Stopping the LDAC media stream now (session ends).'
            Write-Host 'The AVRCP observer stays bound. Swipe the XM5 volume'
            Write-Host ("now and again every ~10 seconds for the next " +
                "$PauseObservationSeconds seconds.")
        }
        $pauseStart = [DateTimeOffset]::Now
        if (-not $PauseKeepSession) {
            if ($null -ne $mediaStopEvent) {
                [void]$mediaStopEvent.Set()
            }
            if ($null -ne $mediaProcess -and -not $mediaProcess.HasExited) {
                if (-not $mediaProcess.WaitForExit(30000)) {
                    $mediaTimedOut = $true
                }
            }
        }
        $pauseExecutorLinesBefore = @(
            Read-V1AudibleSyncFile $executorOut).Count
        $pauseMediaStopLines = @(Read-V1AudibleSyncFile $mediaOut |
            Where-Object {
                $_ -match 'SUSPEND|CLOSE|released|stream' })
        if ($PauseKeepSession) {
            $pauseMediaStopLines = @()
        }
        $pauseDeadline = $pauseStart.AddSeconds($PauseObservationSeconds)
        $pauseSyncSurvived = $false
        $pauseReminderShown = $false
        $executorAliveAtPauseStart = $null -ne $executorProcess -and
            -not $executorProcess.HasExited
        $executorExitedDuringPause = $false
        while ([DateTimeOffset]::Now -lt $pauseDeadline) {
            $executorLines = @(Read-V1AudibleSyncFile $executorOut)
            if ($executorLines.Count -gt $lastExecutorLineCount) {
                for ($lineIndex = $lastExecutorLineCount;
                     $lineIndex -lt $executorLines.Count;
                     $lineIndex++) {
                    Write-Host ("[executor] " + $executorLines[$lineIndex])
                }
                $lastExecutorLineCount = $executorLines.Count
            }
            if (-not $pauseReminderShown -and
                ($pauseDeadline - [DateTimeOffset]::Now).TotalSeconds -le
                    ($PauseObservationSeconds / 2)) {
                $pauseReminderShown = $true
                Write-Host 'Swipe the XM5 volume again now (pause window).'
            }
            if (-not $executorAliveAtPauseStart) { break }
            if ($executorProcess.HasExited) {
                $executorExitedDuringPause = $true
                break
            }
            Start-Sleep -Milliseconds 250
        }
        $pauseExecutorLinesAfter = @(
            Read-V1AudibleSyncFile $executorOut |
                Select-Object -Skip $pauseExecutorLinesBefore)
        $pauseSyncSurvived = @($pauseExecutorLinesAfter | Where-Object {
            $_ -match 'action set-windows-volume' }).Count -gt 0
        $result.pause = [ordered]@{
            enabled = $true
            keep_session = [bool]$PauseKeepSession
            observation_seconds = $PauseObservationSeconds
            sync_survived = [bool]$pauseSyncSurvived
            executor_alive_at_start = [bool]$executorAliveAtPauseStart
            executor_exited_during_pause = [bool]$executorExitedDuringPause
            valid = [bool]$executorAliveAtPauseStart
            media_stop_lines = @($pauseMediaStopLines)
            executor_lines = @($pauseExecutorLinesAfter)
        }
        $pausePhasePassed = [bool]$executorAliveAtPauseStart -and
            (-not $PauseKeepSession -or [bool]$pauseSyncSurvived)
        $result.steps += [ordered]@{
            action = 'pause-phase'
            passed = $pausePhasePassed
            sync_survived = [bool]$pauseSyncSurvived
        }
        if ($PauseKeepSession) {
            Write-Host ('PAUSE phase result: ' +
                $(if ($pausePhasePassed) { 'PASS' } else { 'FAIL' }) +
                '; sync_survived=' + $pauseSyncSurvived +
                '; executor_alive=' + $executorAliveAtPauseStart)
        } else {
            Write-Host ('PAUSE phase result: ' +
                $(if ($pausePhasePassed) { 'PASS' } else { 'FAIL' }) +
                '; sync_survived=' + $pauseSyncSurvived +
                '; executor_alive=' + $executorAliveAtPauseStart)
        }

        # --- Phase 3.6: resume phase --------------------------------------
        # With -PauseKeepSession the session never closed: resume = the user
        # resumes the player and sync continues on the same channels.
        # Otherwise a fresh media session is restarted (a known limitation:
        # the XM5 rejects a new AVDTP signaling open on the same ACL).
        if ($ResumeObservationSeconds -gt 0) {
            $result.resume = [ordered]@{
                enabled = $true
                keep_session = [bool]$PauseKeepSession
                observation_seconds = $ResumeObservationSeconds
                start_ready = $false
                media_resumed = $false
                volume_sync_resumed = $false
                sync_resumed = $false
                error_lines = @()
                executor_lines = @()
            }
            if ($PauseKeepSession) {
                Write-Host '=== RESUME PHASE ==='
                Write-Host 'The LDAC session never closed.'
                if ($DiagnoseMediaKeys) {
                    Write-Host 'The PC player should still be PAUSED from the prior phase.'
                    Write-Host 'Double-tap the XM5 once now to request PLAY, then wait 5 seconds.'
                    Write-Host 'If the first attempt has no effect, retry only once after the reminder.'
                } else {
                    Write-Host 'Resume your player now.'
                }
                Write-Host 'Swipe XM5 volume and adjust the PC volume once each'
                Write-Host "during the $ResumeObservationSeconds s resume window."
                $result.resume.start_ready = $true
                $resumeExecutorLinesBefore = @(
                    Read-V1AudibleSyncFile $executorOut).Count
                $resumeWindowDeadline = [DateTimeOffset]::Now.AddSeconds(
                    $ResumeObservationSeconds)
                while ([DateTimeOffset]::Now -lt $resumeWindowDeadline) {
                    $executorLines = @(Read-V1AudibleSyncFile $executorOut)
                    if ($executorLines.Count -gt $lastExecutorLineCount) {
                        for ($lineIndex = $lastExecutorLineCount;
                             $lineIndex -lt $executorLines.Count;
                             $lineIndex++) {
                            Write-Host ("[executor] " + $executorLines[$lineIndex])
                        }
                        $lastExecutorLineCount = $executorLines.Count
                    }
                    foreach ($line in @(
                            $executorLines |
                                Select-Object -Skip $resumeExecutorLinesBefore)) {
                        if ($line -match '^action inject vk=0x00B3 action=(16|64)$' -or
                            $line -match '^action notify-playback-status=1 ' +
                                '\((sent|queued; transaction-aware)\)$' -or
                            $line -match '^live: media-session .* playback=playing') {
                            $result.resume.media_resumed = $true
                        }
                        if ($line -match '^action send-xm5-volume value=\d+ \(sent\)$' -or
                            $line -match '^action set-windows-volume') {
                            $result.resume.volume_sync_resumed = $true
                        }
                    }
                    $result.resume.sync_resumed =
                        [bool]$result.resume.media_resumed
                    if ($executorProcess.HasExited) { break }
                    Start-Sleep -Milliseconds 250
                }
                $result.resume.executor_lines = @(
                    Read-V1AudibleSyncFile $executorOut |
                        Select-Object -Skip $resumeExecutorLinesBefore)
            } else {
                Write-Host '=== RESUME PHASE ==='
                Write-Host 'Restarting LDAC playback. Keep music/video playing on the PC.'
                if ($ResumeCooldownSeconds -gt 0) {
                    Write-Host ("Waiting $ResumeCooldownSeconds s after the media " +
                        'CLOSE before re-opening...')
                    Start-Sleep -Seconds $ResumeCooldownSeconds
                }
                $resumeMediaOut = Join-Path $trialRoot 'media-resume.out.log'
                $resumeMediaErr = Join-Path $trialRoot 'media-resume.err.log'
                $resumeStopEventName = 'Local\NativeLdacAudibleSyncStop-' +
                    $PID + '-' + [guid]::NewGuid().ToString('N')
                $resumeStopCreatedNew = $false
                $resumeStopEvent = [Threading.EventWaitHandle]::new(
                    $false,
                    [Threading.EventResetMode]::ManualReset,
                    $resumeStopEventName,
                    [ref]$resumeStopCreatedNew)
                if (-not $resumeStopCreatedNew) {
                    throw 'The resume media stop event already existed.'
                }
                $resumeMediaProcess = Start-Process -FilePath $TransportProbePath `
                    -ArgumentList @(
                        '--play-system',
                        '--open-attempts', '1',
                        '--stop-event', $resumeStopEventName) `
                    -RedirectStandardOutput $resumeMediaOut `
                    -RedirectStandardError $resumeMediaErr `
                    -PassThru -WindowStyle Hidden
                $resumeMediaLineBase = @(
                    Read-V1AudibleSyncFile $resumeMediaOut).Count
                $resumeReady = $false
                $resumeDeadline = [DateTimeOffset]::Now.AddSeconds(120)
                while ([DateTimeOffset]::Now -lt $resumeDeadline) {
                    foreach ($line in @(
                            Read-V1AudibleSyncFile $resumeMediaOut |
                                Select-Object -Skip $resumeMediaLineBase)) {
                        if ($line -match 'XM5 accepted START; the LDAC Media transport is ready') {
                            $resumeReady = $true
                        }
                    }
                    if ($resumeReady) { break }
                    if ($resumeMediaProcess.HasExited) { break }
                    Start-Sleep -Milliseconds 250
                }
                $result.resume.start_ready = [bool]$resumeReady
                $result.resume.error_lines = @(
                    Read-V1AudibleSyncFile $resumeMediaErr)
                if (-not $resumeReady) {
                    Write-Host 'The resumed media session did not reach START; recording evidence only.'
                } else {
                    Write-Host 'Resumed LDAC playback is active. Swipe XM5 volume and adjust'
                    Write-Host 'the PC volume once each during the resume window.'
                    $resumeExecutorLinesBefore = @(
                        Read-V1AudibleSyncFile $executorOut).Count
                    $resumeWindowDeadline = [DateTimeOffset]::Now.AddSeconds(
                        $ResumeObservationSeconds)
                    while ([DateTimeOffset]::Now -lt $resumeWindowDeadline) {
                        $executorLines = @(Read-V1AudibleSyncFile $executorOut)
                        if ($executorLines.Count -gt $lastExecutorLineCount) {
                            for ($lineIndex = $lastExecutorLineCount;
                                 $lineIndex -lt $executorLines.Count;
                                 $lineIndex++) {
                                Write-Host ("[executor] " + $executorLines[$lineIndex])
                            }
                            $lastExecutorLineCount = $executorLines.Count
                        }
                        foreach ($line in @(
                                $executorLines |
                                    Select-Object -Skip $resumeExecutorLinesBefore)) {
                            if ($line -match '^action inject vk=0x00B3 action=(16|64)$' -or
                                $line -match '^action notify-playback-status=1 ' +
                                    '\((sent|queued; transaction-aware)\)$' -or
                                $line -match '^live: media-session .* playback=playing') {
                                $result.resume.media_resumed = $true
                            }
                            if ($line -match '^action send-xm5-volume value=\d+ \(sent\)$' -or
                                $line -match '^action set-windows-volume') {
                                $result.resume.volume_sync_resumed = $true
                            }
                        }
                        $result.resume.sync_resumed =
                            [bool]$result.resume.media_resumed
                        if ($executorProcess.HasExited) { break }
                        Start-Sleep -Milliseconds 250
                    }
                    $result.resume.executor_lines = @(
                        Read-V1AudibleSyncFile $executorOut |
                            Select-Object -Skip $resumeExecutorLinesBefore)
                }
            }
            $result.steps += [ordered]@{
                action = 'resume-phase'
                passed = [bool]$result.resume.start_ready -and
                    (-not $PauseKeepSession -or
                        [bool]$result.resume.media_resumed)
                media_resumed = [bool]$result.resume.media_resumed
                volume_sync_resumed = [bool]$result.resume.volume_sync_resumed
                sync_resumed = [bool]$result.resume.sync_resumed
            }
            $resumePhasePassed = [bool]$result.resume.start_ready -and
                (-not $PauseKeepSession -or
                    [bool]$result.resume.media_resumed)
            Write-Host ('RESUME phase result: ' +
                $(if ($resumePhasePassed) { 'PASS' } else { 'FAIL' }) +
                '; start_ready=' + $result.resume.start_ready +
                '; sync_resumed=' + $result.resume.sync_resumed)
            # Stop the resumed media gracefully before restoring Microsoft.
            if ($null -ne $resumeStopEvent) {
                [void]$resumeStopEvent.Set()
            }
            if ($null -ne $resumeMediaProcess -and
                -not $resumeMediaProcess.HasExited) {
                if (-not $resumeMediaProcess.WaitForExit(30000)) {
                    $resumeMediaTimedOut = $true
                }
            }
            Stop-V1AudibleSyncProcess $resumeMediaProcess
            if ($null -ne $resumeStopEvent) {
                $resumeStopEvent.Dispose()
                $resumeStopEvent = $null
            }
        }
    }

    # --- Phase 4: stop and restore Microsoft AVRCP ------------------------
    if ($null -ne $mediaStopEvent) {
        [void]$mediaStopEvent.Set()
    }
    if ($null -ne $mediaProcess -and -not $mediaProcess.HasExited) {
        if (-not $mediaProcess.WaitForExit(30000)) {
            $mediaTimedOut = $true
        }
    }
    Stop-V1AudibleSyncProcess $executorProcess
    Stop-V1AudibleSyncProcess $mediaProcess
    Stop-V1AudibleSyncProcess $connectProcess

    Write-Host 'Phase 4: restoring Microsoft AVRCP.'
    $result.restore = Restore-V1AudibleSyncMicrosoft `
        -PublishedInf $publishedObserverInf `
        -InstanceId $target.InstanceId `
        -TrialRoot $trialRoot
    if (-not $result.restore.passed) {
        throw 'Microsoft AVRCP could not be restored after the audible sync check.'
    }

    # --- Evidence ---------------------------------------------------------
    $result.evidence.executor_lines = @(
        Read-V1AudibleSyncFile $executorOut -IncludePartial)
    $result.evidence.media_lines = @(
        Read-V1AudibleSyncFile $mediaOut -IncludePartial)
    $result.evidence.media_error_lines = @(
        Read-V1AudibleSyncFile $mediaErr -IncludePartial)
    $sentWrites = @($result.evidence.executor_lines |
        Where-Object { $_ -match 'action send-xm5-volume value=\d+ \(sent\)' })
    $windowsFollow = @($result.evidence.executor_lines |
        Where-Object { $_ -match 'action set-windows-volume' })
    if ($FirstAuthorityDirection -ne 'off') {
        $firstFollow = @($result.evidence.executor_lines | Where-Object {
            $_ -match '^action set-windows-volume percent=\d+' } |
            Select-Object -First 1)
        $firstAuthorityOk = $false
        $firstValue = -1
        if ($firstFollow.Count -gt 0 -and
            $firstFollow[0] -match 'percent=(\d+)') {
            $firstValue = [int]$Matches[1]
            if ($FirstAuthorityDirection -eq 'up') {
                $firstAuthorityOk = $firstValue -gt
                    $result.pc_volume_before_media
            } else {
                $firstAuthorityOk = $firstValue -lt
                    $result.pc_volume_before_media
            }
        }
        $result.steps += [ordered]@{
            action = 'first-authority'
            passed = [bool]$firstAuthorityOk
            direction = $FirstAuthorityDirection
            pc_volume_before_media = $result.pc_volume_before_media
            first_follow = if ($firstFollow.Count -gt 0) {
                $firstFollow[0]
            } else { '' }
        }
        if (-not $firstAuthorityOk) {
            throw ("First XM5 volume authority did not move $FirstAuthorityDirection " +
                "relative to PC volume $($result.pc_volume_before_media)%; first follow: " +
                $(if ($firstFollow.Count -gt 0) {
                    $firstFollow[0]
                } else { '(none)' }))
        }
    }
    if ($sentWrites.Count -eq 0 -and -not $SkipSyncWindow) {
        $reason = 'The executor did not report any sent SetAbsoluteVolume write.'
        if ($executorExitedEarly) {
            $reason += ' The executor exited early; its output: ' +
                (($result.evidence.executor_lines) -join ' | ')
        } else {
            $reason += (' PC-to-XM5 writes observed: {0}; XM5-to-PC volume ' +
                'actions observed: {1}. Adjust the PC volume during the ' +
                'action window.') -f $sentWrites.Count, $windowsFollow.Count
        }
        throw $reason
    }
    $result.steps += [ordered]@{
        action = 'audible-sync-evidence'
        sent_write_count = $sentWrites.Count
        windows_volume_action_count = $windowsFollow.Count
        passed = $true
    }

    $result.diagnostics = Get-V1AudibleSyncMediaKeySummary `
        -Lines @($result.evidence.executor_lines)
    $observerHandoffStep = @($result.steps | Where-Object {
        $_.action -eq 'observer-handoff'
    } | Select-Object -Last 1)
    $observerHandoffPassed = $observerHandoffStep.Count -gt 0 -and
        [bool]$observerHandoffStep[0].passed
    $audibleSyncPassed = $sentWrites.Count -gt 0 -or $SkipSyncWindow
    $boundaryPassed = $BoundaryCheckSeconds -le 0 -or
        [bool]$result.boundary.satisfied
    $mediaKeysPassed = $MediaKeyCheckSeconds -le 0 -or
        [bool]$result.media_keys.satisfied
    $pauseSyncRequired = $PauseObservationSeconds -gt 0 -and
        $PauseKeepSession
    $pauseSyncPassed = -not $pauseSyncRequired -or
        ($null -ne $result.pause -and
            [bool]$result.pause.sync_survived)
    $resumeSyncRequired = $ResumeObservationSeconds -gt 0 -and
        $PauseKeepSession
    $resumeSyncPassed = -not $resumeSyncRequired -or
        ($null -ne $result.resume -and
            [bool]$result.resume.start_ready -and
            [bool]$result.resume.media_resumed)
    $restorePassed = $null -ne $result.restore -and
        [bool]$result.restore.passed
    $requiredFailures = @()
    if (-not $observerHandoffPassed) {
        $requiredFailures += 'observer handoff did not pass'
    }
    if (-not $audibleSyncPassed) {
        $requiredFailures += 'no audible volume-sync write was observed'
    }
    if (-not $boundaryPassed) {
        $requiredFailures += 'boundary volume check was not satisfied'
    }
    if (-not $mediaKeysPassed) {
        $requiredFailures += 'media-key check observed no injected action'
    }
    if (-not $pauseSyncPassed) {
        $requiredFailures += 'pause keep-session sync did not survive'
    }
    if (-not $resumeSyncPassed) {
        $requiredFailures += 'resume keep-session sync did not resume'
    }
    if (-not $restorePassed) {
        $requiredFailures += 'Microsoft AVRCP restore did not pass'
    }
    $result.summary = [ordered]@{
        observer_handoff = $observerHandoffPassed
        audible_sync_evidence = $audibleSyncPassed
        boundary_check = $boundaryPassed
        media_key_check = $mediaKeysPassed
        pause_sync_required = $pauseSyncRequired
        pause_sync_survived = if ($null -ne $result.pause) {
            [bool]$result.pause.sync_survived
        } else { $null }
        pause_sync_passed = $pauseSyncPassed
        resume_sync_required = $resumeSyncRequired
        resume_sync_resumed = if ($null -ne $result.resume) {
            [bool]$result.resume.sync_resumed
        } else { $null }
        resume_media_resumed = if ($null -ne $result.resume) {
            [bool]$result.resume.media_resumed
        } else { $null }
        resume_volume_sync_resumed = if ($null -ne $result.resume) {
            [bool]$result.resume.volume_sync_resumed
        } else { $null }
        resume_sync_passed = $resumeSyncPassed
        microsoft_restore = $restorePassed
        overall = $requiredFailures.Count -eq 0
        failures = @($requiredFailures)
    }
    Write-Host '=== CHECK SUMMARY ==='
    Write-Host ('  observer handoff: ' +
        $(if ($observerHandoffPassed) { 'PASS' } else { 'FAIL' }))
    Write-Host ('  audible volume sync: ' +
        $(if ($audibleSyncPassed) { 'PASS' } else { 'FAIL' }))
    if ($PauseObservationSeconds -gt 0) {
        Write-Host ('  pause sync survived: ' +
            $(if ($pauseSyncPassed) { 'PASS' } else { 'FAIL' }) +
            ' (observed=' + $result.summary.pause_sync_survived + ')')
    }
    if ($ResumeObservationSeconds -gt 0) {
        Write-Host ('  resume sync resumed: ' +
            $(if ($resumeSyncPassed) { 'PASS' } else { 'FAIL' }) +
            ' (observed=' + $result.summary.resume_sync_resumed + ')')
    }
    if ($BoundaryCheckSeconds -gt 0) {
        Write-Host ('  boundary volume: ' +
            $(if ($boundaryPassed) { 'PASS' } else { 'FAIL' }))
    }
    if ($MediaKeyCheckSeconds -gt 0) {
        Write-Host ('  media-key injection: ' +
            $(if ($mediaKeysPassed) { 'PASS' } else { 'FAIL' }))
    }
    Write-Host ('  Microsoft AVRCP restore: ' +
        $(if ($restorePassed) { 'PASS' } else { 'FAIL' }))
    if ($DiagnoseMediaKeys) {
        Write-Host ('  diagnostic raw events=' +
            $result.diagnostics.raw_pass_through_event_count +
            '; injected=' + $result.diagnostics.injected_count +
            '; sink failures=' +
            $result.diagnostics.sink_failure_count)
    }
    if ($requiredFailures.Count -gt 0) {
        $result.failure = 'Required check(s) failed: ' +
            ($requiredFailures -join '; ')
        $result.failure_code = 'avrcp-audible-sync-check-failed'
        $result.passed = $false
    } else {
        $result.passed = $true
    }
} catch {
    $result.failure = [string]$_
    $result.failure_code = 'avrcp-audible-sync-failed'
} finally {
    foreach ($process in @(
            $executorProcess, $mediaProcess, $connectProcess,
            $resumeMediaProcess)) {
        if ($null -ne $process) {
            try {
                Stop-V1AudibleSyncProcess $process
            } catch {
                # Cleanup must never mask the recorded check failure.
            }
        }
    }
    if ($null -ne $mediaStopEvent) {
        try {
            $mediaStopEvent.Dispose()
        } catch {
            # Cleanup must never mask the recorded check failure.
        }
    }
    if ($null -ne $resumeStopEvent) {
        try {
            $resumeStopEvent.Dispose()
        } catch {
            # Cleanup must never mask the recorded check failure.
        }
    }
    # Any failure after the observer handoff must still restore Microsoft
    # AVRCP; otherwise the next run's preflight fails with the observer bound.
    if ($observerInstalled -and $null -eq $result.restore) {
        Write-Host 'Restoring Microsoft AVRCP after the failed run.'
        $result.restore = Restore-V1AudibleSyncMicrosoft `
            -PublishedInf $publishedObserverInf `
            -InstanceId $target.InstanceId `
            -TrialRoot $trialRoot
    }
    Write-V1AvrcpFilterJsonAtomically -Path $resultPath -Value $result
}

if ($result.passed) {
    Write-Host 'V1 AVRCP audible sync check passed.'
    Write-Host "Result: $resultPath"
} else {
    Write-Host "V1 AVRCP audible sync check failed: $($result.failure)"
    Write-Host "Result: $resultPath"
    exit 1
}
