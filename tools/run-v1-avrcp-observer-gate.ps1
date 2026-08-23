# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1AvrcpObserver,
    [ValidateRange(20, 120)]
    [int]$ObservationSeconds = 45,
    [ValidateRange(120, 600)]
    [int]$DurationSeconds = 300,
    [string]$CandidatePath,
    [switch]$MinimalMediaSession,
    [string]$ExecutorPath,
    [switch]$ExecutorApply,
    [ValidateRange(0, 100)]
    [int]$InitialVolumePercent = 50,
    [ValidateRange(1, 4294967295)]
    [long]$OwnerLease = 1
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:TargetPrefix = `
    'BTHENUM\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
$script:OriginalInf = 'NativeLdacAvrcpObserver.inf'
$script:BaselineInf = 'microsoft_bluetooth_avrcptransport.inf'
$script:BaselineService = 'Microsoft_Bluetooth_AvrcpTransport'
$script:OutboundOpenFlag = 0x00000080
$script:SignalingLabel = if ($MinimalMediaSession) {
    'minimal LDAC media session (silence only)'
} else {
    'capability-only AVDTP signaling'
}
$script:SignalingReadyPattern = if ($MinimalMediaSession) {
    '(?m)^XM5 accepted START; the LDAC Media transport is ready\.\s*$'
} else {
    '(?m)^Signaling channel hold active for up to \d+ second\(s\)\.\s*$'
}

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'The V1 AVRCP observer gate requires an elevated PowerShell 7 terminal.'
    }
}

function Get-PropertyData {
    param([string]$InstanceId, [string]$KeyName)
    $property = Get-PnpDeviceProperty -InstanceId $InstanceId `
        -KeyName $KeyName -ErrorAction SilentlyContinue
    if ($null -eq $property) { return $null }
    return $property.Data
}

function Get-TargetDevice {
    $matches = @(Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object {
        $_.InstanceId.StartsWith(
            $script:TargetPrefix,
            [StringComparison]::OrdinalIgnoreCase)
    })
    if ($matches.Count -ne 1) {
        throw 'Exactly one paired XM5 AVRCP 0x110E PDO is required.'
    }
    return $matches[0]
}

function Get-DeviceSnapshot {
    param([Parameter(Mandatory = $true)]$Device)
    [pscustomobject][ordered]@{
        instance_id = [string]$Device.InstanceId
        present = [bool]$Device.Present
        status = [string]$Device.Status
        problem = [string]$Device.Problem
        problem_code = [int](Get-PropertyData -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_ProblemCode')
        inf = [string](Get-PropertyData -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverInfPath')
        service = [string](Get-PropertyData -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Service')
        parent = [string](Get-PropertyData -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Parent')
        container_id = [string](Get-PropertyData -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_ContainerId')
    }
}

function Get-CandidatePackages {
    return @(Get-WindowsDriver -Online -All | Where-Object {
        (Split-Path -Leaf ([string]$_.OriginalFileName)) -ieq
            $script:OriginalInf
    } | ForEach-Object {
        [pscustomobject][ordered]@{
            published_inf = [string]$_.Driver
            original_inf = Split-Path -Leaf ([string]$_.OriginalFileName)
            version = [string]$_.Version
            provider = [string]$_.ProviderName
        }
    })
}

function Invoke-PnpUtil {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $lines = @(& pnputil.exe @Arguments 2>&1)
    [pscustomobject][ordered]@{
        exit_code = $LASTEXITCODE
        lines = @($lines)
    }
}

function Restore-Baseline {
    param([string]$PublishedInf, [string]$InstanceId)
    $steps = @()
    $passed = $true
    if ([string]::IsNullOrWhiteSpace($PublishedInf)) {
        $discovered = @(Get-CandidatePackages)
        if ($discovered.Count -eq 1) {
            $PublishedInf = [string]$discovered[0].published_inf
        } elseif ($discovered.Count -gt 1) {
            $passed = $false
        }
    }
    if ($PublishedInf -match '^oem\d+\.inf$') {
        $delete = Invoke-PnpUtil -Arguments @(
            '/delete-driver', $PublishedInf, '/uninstall', '/force')
        $steps += [pscustomobject]@{
            action = 'delete-candidate'
            exit_code = $delete.exit_code
            lines = @($delete.lines)
        }
        if ($delete.exit_code -notin @(0, 259)) { $passed = $false }
    }
    $scan = Invoke-PnpUtil -Arguments @('/scan-devices')
    $steps += [pscustomobject]@{
        action = 'scan-devices'
        exit_code = $scan.exit_code
        lines = @($scan.lines)
    }
    if ($scan.exit_code -ne 0) { $passed = $false }
    Start-Sleep -Seconds 2
    $current = Get-PnpDevice -InstanceId $InstanceId `
        -ErrorAction SilentlyContinue
    if ($null -ne $current -and [bool]$current.Present) {
        $restart = Invoke-PnpUtil -Arguments @('/restart-device', $InstanceId)
        $steps += [pscustomobject]@{
            action = 'restart-exact-avrcp-pdo'
            exit_code = $restart.exit_code
            lines = @($restart.lines)
        }
        if ($restart.exit_code -ne 0) { $passed = $false }
        Start-Sleep -Seconds 2
    }
    $remaining = @(Get-CandidatePackages)
    $snapshot = Get-DeviceSnapshot -Device (Get-TargetDevice)
    if ($remaining.Count -ne 0 -or
        $snapshot.inf -ine $script:BaselineInf -or
        $snapshot.service -ine $script:BaselineService -or
        $snapshot.problem_code -ne 0) {
        $passed = $false
    }
    [pscustomobject][ordered]@{
        passed = $passed
        steps = @($steps)
        remaining_packages = @($remaining)
        final_target = $snapshot
    }
}

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The V1 AVRCP observer gate requires PowerShell 7.'
}
Assert-Administrator
if (-not $ConfirmV1AvrcpObserver) {
    throw 'Refusing to bind the XM5 AVRCP PDO without -ConfirmV1AvrcpObserver.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $projectRoot `
        'artifacts\v1-volume-sync\avrcp-observer-candidate'
}
$CandidatePath = [IO.Path]::GetFullPath($CandidatePath)
& (Join-Path $PSScriptRoot 'verify-v1-avrcp-observer-candidate.ps1') `
    -CandidatePath $CandidatePath
& (Join-Path $PSScriptRoot 'verify-v1-golden-checkpoint.ps1')

$manifest = Get-Content -LiteralPath (Join-Path $CandidatePath 'manifest.json') `
    -Raw | ConvertFrom-Json
$dirty = @(& git.exe -C $projectRoot status --porcelain)
$head = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $dirty.Count -ne 0 -or
    $head -cne [string]$manifest.source_commit) {
    throw 'The AVRCP gate candidate must match clean Git HEAD.'
}
$existingPackages = @(Get-CandidatePackages)
if ($existingPackages.Count -ne 0) {
    throw 'A NativeLdacAvrcpObserver package is already staged; rollback it first.'
}
$target = Get-TargetDevice
$baseline = Get-DeviceSnapshot -Device $target
if ($baseline.inf -ine $script:BaselineInf -or
    $baseline.service -ine $script:BaselineService -or
    $baseline.problem_code -ne 0) {
    throw 'The current XM5 AVRCP baseline is not the healthy Microsoft binding.'
}

$aclProbe = Join-Path $CandidatePath 'tools\xm5_connection_probe.exe'
$observerProbe = Join-Path $CandidatePath `
    'tools\v1_avrcp_observer_probe.exe'
$transportProbe = Join-Path $CandidatePath 'tools\transport_probe.exe'
$candidateInf = Join-Path $CandidatePath `
    'package\NativeLdacAvrcpObserver.inf'
foreach ($required in @(
        $aclProbe, $observerProbe, $transportProbe, $candidateInf)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Gate input is missing: $required"
    }
}
$radioLines = @(& $aclProbe --radio-state 2>&1)
if ($LASTEXITCODE -ne 0 -or
    ($radioLines -join "`n") -notmatch 'ready|connectable') {
    throw 'Windows Bluetooth must be on and connectable.'
}
$stateLines = @(& $aclProbe --state 2>&1)
if (($stateLines -join "`n") -notmatch 'disconnected') {
    throw 'XM5 must be physically off and disconnected before the gate starts.'
}

Write-Host 'V1 AVRCP observe-only preflight passed.'
Write-Host 'Keep XM5 off, close all players, and do not change PC volume.'
Write-Host 'The candidate package is staged while Microsoft AVRCP remains bound.'
Write-Host ("After physical ACL connect, one " + $script:SignalingLabel + " keeps the audio profile present while the exact AVRCP PDO is replaced.")
if ($MinimalMediaSession) {
    Write-Host 'The minimal session performs SET_CONFIGURATION, media OPEN, START, and LDAC silence packets; no audible audio, Core Audio write, or endpoint change.'
}
Write-Host 'The candidate performs exactly one outbound PSM 0x0017 OPEN and records events without retries or public writes.'
Write-Host 'Microsoft AVRCP is restored before the AVDTP signaling hold is released.'
Write-Host 'It does not replace LdacNative, ROOT\MEDIA\0001, the default endpoint, or the audio path.'

if (-not $PSCmdlet.ShouldProcess(
        'one XM5 AVRCP 0x110E service PDO',
        "Stage one observe-only driver, connect under Microsoft AVRCP, hold $($script:SignalingLabel), bind the exact PDO, perform one outbound PSM 0x0017 OPEN, observe events, then restore Microsoft AVRCP before releasing signaling")) {
    return
}

$trialRoot = Join-Path $projectRoot (
    'artifacts\v1-volume-sync\trial\avrcp-observer-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $trialRoot -Force | Out-Null
$resultPath = Join-Path $trialRoot 'result.json'
$deadline = [DateTimeOffset]::Now.AddSeconds($DurationSeconds)
$publishedInf = $null
$failure = $null
$rollback = $null
$probeLines = @()
$listenerLines = @()
$connectLines = @()
$disconnectLines = @()
$boundSnapshot = $null
$signalingProcess = $null
$signalingStopEvent = $null
$signalingStopEventName = ''
$signalingStarted = $false
$signalingReady = $false
$signalingCompleted = $false
$signalingTimedOut = $false
$signalingForced = $false
$signalingExit = -1
$signalingOutput = ''
$signalingError = ''
$signalingReleasedBeforePowerOff = $false
$baselineRestoredBeforeSignalingRelease = $false
$signalingOutPath = Join-Path $trialRoot 'signaling-hold.out.log'
$signalingErrPath = Join-Path $trialRoot 'signaling-hold.err.log'

try {
    $stage = Invoke-PnpUtil -Arguments @('/add-driver', $candidateInf)
    $stage.lines | Set-Content -LiteralPath `
        (Join-Path $trialRoot 'stage.log') -Encoding utf8
    if ($stage.exit_code -notin @(0, 259)) {
        throw "Candidate staging failed with exit $($stage.exit_code)."
    }
    $packages = @(Get-CandidatePackages)
    if ($packages.Count -ne 1 -or
        [string]$packages[0].published_inf -notmatch '^oem\d+\.inf$') {
        throw 'Exactly one staged AVRCP observer package was not found.'
    }
    $publishedInf = [string]$packages[0].published_inf

    Write-Host 'The observe-only package is staged and Microsoft AVRCP remains bound. Turn on XM5 normally now. Do not start audio.'
    $connectLines = @(& $aclProbe --wait-acl-connect 90 2>&1)
    $connectLines | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { throw 'One physical XM5 ACL connect was not observed.' }

    $signalingStopEventName = 'Local\NativeLdacV1AvrcpSignalingStop-' +
        $PID + '-' + [guid]::NewGuid().ToString('N')
    $signalingCreatedNew = $false
    $signalingStopEvent = [Threading.EventWaitHandle]::new(
        $false,
        [Threading.EventResetMode]::ManualReset,
        $signalingStopEventName,
        [ref]$signalingCreatedNew)
    if (-not $signalingCreatedNew) {
        throw 'The bounded AVDTP signaling stop event already existed.'
    }
    $signalingArguments = if ($MinimalMediaSession) {
        @('--stream-silence-continuous',
          '--open-attempts', '1',
          '--stop-event', $signalingStopEventName)
    } else {
        @('--discover',
          '--open-attempts', '1',
          '--hold-signaling-seconds',
          [string][math]::Min($DurationSeconds, 300),
          '--stop-event', $signalingStopEventName)
    }
    $signalingProcess = Start-Process `
        -FilePath $transportProbe `
        -ArgumentList $signalingArguments `
        -RedirectStandardOutput $signalingOutPath `
        -RedirectStandardError $signalingErrPath `
        -WindowStyle Hidden `
        -PassThru
    $signalingStarted = $null -ne $signalingProcess
    if (-not $signalingStarted) {
        throw "The $script:SignalingLabel holder did not start."
    }
    $signalingDeadline = [DateTime]::UtcNow.AddSeconds(20)
    while ([DateTime]::UtcNow -lt $signalingDeadline) {
        if ($signalingProcess.HasExited) { break }
        if (Test-Path -LiteralPath $signalingOutPath -PathType Leaf) {
            $signalingOutput = Get-Content -LiteralPath `
                $signalingOutPath -Raw
            if ($signalingOutput -match $script:SignalingReadyPattern) {
                $signalingReady = $true
                break
            }
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $signalingReady) {
        if (Test-Path -LiteralPath $signalingErrPath -PathType Leaf) {
            $signalingError = Get-Content -LiteralPath `
                $signalingErrPath -Raw
        }
        throw ("The $script:SignalingLabel holder did not reach " +
            'its ready state. ' + $signalingError.Trim())
    }
    if ($MinimalMediaSession) {
        Write-Host 'Minimal LDAC media session is active; LDAC silence packets only, no audible audio.'
    } else {
        Write-Host 'Capability-only AVDTP signaling is active; no SET_CONFIGURATION, media OPEN, START, or media packet was sent.'
    }

    $apply = Invoke-PnpUtil -Arguments @(
        '/add-driver', $candidateInf, '/install')
    $apply.lines | Set-Content -LiteralPath `
        (Join-Path $trialRoot 'apply.log') -Encoding utf8
    if ($apply.exit_code -notin @(0, 259)) {
        throw "Candidate binding failed with exit $($apply.exit_code)."
    }

    $bindingReady = $false
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        $boundSnapshot = Get-DeviceSnapshot -Device (Get-TargetDevice)
        if ($boundSnapshot.inf -ieq $publishedInf -and
            $boundSnapshot.service -ieq 'NativeLdacAvrcpObserver' -and
            $boundSnapshot.problem_code -eq 0) {
            $bindingReady = $true
            break
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $bindingReady) {
        $restart = Invoke-PnpUtil -Arguments @(
            '/restart-device', $baseline.instance_id)
        $restart.lines | Set-Content -LiteralPath `
            (Join-Path $trialRoot 'candidate-restart.log') -Encoding utf8
        if ($restart.exit_code -ne 0) {
            throw 'The exact AVRCP candidate PDO did not bind and could not be restarted.'
        }
        Start-Sleep -Seconds 3
        $boundSnapshot = Get-DeviceSnapshot -Device (Get-TargetDevice)
        if ($boundSnapshot.inf -ine $publishedInf -or
            $boundSnapshot.service -ine 'NativeLdacAvrcpObserver' -or
            $boundSnapshot.problem_code -ne 0) {
            throw 'The exact AVRCP candidate PDO did not reach a healthy binding.'
        }
    }
    Write-Host 'The exact AVRCP PDO is bound to the outbound observe-only candidate while the AVDTP hold remains active.'

    Write-Host "For the next $ObservationSeconds seconds: swipe volume up at least three times, down at least three times, double-tap once, then swipe next and previous once each."
    Write-Host 'Do not use PC volume and do not start playback; this gate records events only.'
    $observerMode = 'probe'
    if (-not [string]::IsNullOrWhiteSpace($ExecutorPath)) {
        $observerMode = 'executor-live'
        $executorArguments = @(
            '--live',
            '--duration-seconds', [string]$ObservationSeconds,
            '--owner-lease', [string]$OwnerLease,
            '--initial-volume-percent', [string]$InitialVolumePercent,
            '--volume-sync', '--route-media-keys')
        if ($ExecutorApply) { $executorArguments += '--apply' }
        Write-Host 'Running the V1 AVRCP action executor as the live observer (trial mode; evidence checks are not applied).'
        $executorOutPath = Join-Path $trialRoot 'executor.log'
        $executorErrPath = Join-Path $trialRoot 'executor.err.log'
        $executorProcess = Start-Process `
            -FilePath $ExecutorPath `
            -ArgumentList $executorArguments `
            -RedirectStandardOutput $executorOutPath `
            -RedirectStandardError $executorErrPath `
            -WindowStyle Hidden `
            -PassThru
        $probeLines = @()
        $streamedLineCount = 0
        while (-not $executorProcess.HasExited) {
            if (Test-Path -LiteralPath $executorOutPath -PathType Leaf) {
                $availableLines = @(Get-Content -LiteralPath $executorOutPath)
                for ($lineIndex = $streamedLineCount;
                     $lineIndex -lt $availableLines.Count;
                     $lineIndex++) {
                    Write-Host $availableLines[$lineIndex]
                    $streamedLineCount++
                }
            }
            Start-Sleep -Milliseconds 100
        }
        $executorProcess.WaitForExit()
        $probeExit = $executorProcess.ExitCode
        $executorProcess.Dispose()
        if (Test-Path -LiteralPath $executorOutPath -PathType Leaf) {
            $probeLines = @(Get-Content -LiteralPath $executorOutPath)
            for ($lineIndex = $streamedLineCount;
                 $lineIndex -lt $probeLines.Count;
                 $lineIndex++) {
                Write-Host $probeLines[$lineIndex]
            }
        }
        if ($probeExit -ne 0) {
            throw "The V1 AVRCP action executor failed with exit $probeExit."
        }
    } else {
        $probeLines = @(& $observerProbe `
            --duration-seconds $ObservationSeconds 2>&1)
        $probeExit = $LASTEXITCODE
        $probeLines | ForEach-Object { Write-Host $_ }
        $probeLines | Set-Content -LiteralPath `
            (Join-Path $trialRoot 'observer.log') -Encoding utf8
        if ($probeExit -ne 0) {
            throw "The read-only observer probe failed with exit $probeExit."
        }
        $joined = $probeLines -join "`n"
        $statusMatch = [regex]::Match(
            $joined,
            'ABI: 0\.11; flags 0x(?<flags>[0-9A-Fa-f]{8});.*protocol 0x(?<protocol>[0-9A-Fa-f]{8}); open 0x(?<open>[0-9A-Fa-f]{8}); close 0x(?<close>[0-9A-Fa-f]{8})')
        if (-not $statusMatch.Success -or
            (([Convert]::ToUInt32($statusMatch.Groups['flags'].Value, 16) `
                -band $script:OutboundOpenFlag) -eq 0)) {
            throw 'The outbound ABI 0.11 observer status was not published.'
        }
        $capabilityCount = ([regex]::Matches(
            $joined, 'type=volume-capability.*value=0x00000001')).Count
        $volumeCount = ([regex]::Matches(
            $joined, 'type=absolute-volume')).Count
        $setVolumeCount = ([regex]::Matches(
            $joined, 'type=vendor-command.*pdu=0x50')).Count
        $volumePassCount = ([regex]::Matches(
            $joined, 'type=pass-through.*value=0x0000004[12]')).Count
        $passThroughCount = ([regex]::Matches(
            $joined, 'type=pass-through')).Count
        $volumeEvidence = ($volumeCount -ge 2) -or
            ($setVolumeCount -ge 2) -or ($volumePassCount -ge 2)
        if ($capabilityCount -lt 1 -or -not $volumeEvidence -or
            $passThroughCount -lt 2) {
            $statusLine = @($probeLines | Where-Object {
                $_ -match '^ABI:'
            } | Select-Object -First 1)
            $statusSuffix = if ($statusLine.Count -eq 1) {
                ' ' + [string]$statusLine[0]
            } else { '' }
            throw "Observe-only evidence was incomplete: capability=$capabilityCount, absolute-volume=$volumeCount, set-absolute-volume=$setVolumeCount, volume-pass-through=$volumePassCount, pass-through=$passThroughCount.$statusSuffix"
        }
    }

    $rollback = Restore-Baseline -PublishedInf $publishedInf `
        -InstanceId $baseline.instance_id
    if (-not [bool]$rollback.passed) {
        throw 'The Microsoft AVRCP baseline could not be restored while the AVDTP signaling hold remained active.'
    }
    $baselineRestoredBeforeSignalingRelease = $true
    Write-Host "Microsoft AVRCP was restored while $script:SignalingLabel still held the physical profile connection."

    [void]$signalingStopEvent.Set()
    if (-not $signalingProcess.HasExited) {
        $signalingCompleted = $signalingProcess.WaitForExit(30000)
    } else {
        $signalingCompleted = $true
    }
    if (-not $signalingCompleted) {
        $signalingTimedOut = $true
        throw "The $script:SignalingLabel holder did not stop after its bounded stop event."
    }
    $signalingExit = $signalingProcess.ExitCode
    $signalingOutput = Get-Content -LiteralPath $signalingOutPath -Raw
    $channelClosed = $signalingOutput -match
        '(?m)^Signaling channel closed\.\s*$'
    if ($MinimalMediaSession) {
        $gracefulMediaClose = $signalingOutput -match
            '(?m)^XM5 accepted CLOSE; the test stream was released normally\.\s*$'
        $signalingExitAccepted = $signalingExit -in @(0, 130) -or
            ($signalingExit -eq 5 -and $gracefulMediaClose)
        $signalingDone = $channelClosed -and $gracefulMediaClose
    } else {
        $signalingExitAccepted = $signalingExit -eq 0
        $signalingDone = $channelClosed
    }
    if (-not $signalingExitAccepted -or -not $signalingDone) {
        throw "The $script:SignalingLabel holder failed with exit $signalingExit."
    }
    $signalingReleasedBeforePowerOff = $true

    if ([DateTimeOffset]::Now -ge $deadline) {
        throw 'The bounded AVRCP gate duration expired.'
    }
    Write-Host 'Observation is complete. The disconnect watcher is armed; turn off XM5 normally now.'
    $disconnectLines = @(& $aclProbe --wait-acl-disconnect 90 2>&1)
    $disconnectLines | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
        throw 'One physical XM5 ACL disconnect was not observed.'
    }
} catch {
    $failure = $_.Exception.Message
} finally {
    try {
        if ($null -ne $signalingStopEvent) {
            [void]$signalingStopEvent.Set()
        }
        if ($null -ne $signalingProcess) {
            if (-not $signalingProcess.HasExited) {
                if (-not $signalingProcess.WaitForExit(15000)) {
                    $signalingTimedOut = $true
                    $signalingForced = $true
                    $signalingProcess.Kill($true)
                    $signalingProcess.WaitForExit()
                }
            }
            $signalingCompleted = -not $signalingForced
            $signalingExit = $signalingProcess.ExitCode
        }
        if (Test-Path -LiteralPath $signalingOutPath -PathType Leaf) {
            $signalingOutput = Get-Content -LiteralPath `
                $signalingOutPath -Raw
        }
        if (Test-Path -LiteralPath $signalingErrPath -PathType Leaf) {
            $signalingError = Get-Content -LiteralPath `
                $signalingErrPath -Raw
        }
    } catch {
        if ([string]::IsNullOrWhiteSpace($failure)) {
            $failure = 'Signaling-holder cleanup failed: ' +
                $_.Exception.Message
        }
    } finally {
        if ($null -ne $signalingProcess) {
            $signalingProcess.Dispose()
        }
        if ($null -ne $signalingStopEvent) {
            $signalingStopEvent.Dispose()
        }
    }
    if ($null -eq $rollback -or -not [bool]$rollback.passed) {
        try {
            $rollback = Restore-Baseline -PublishedInf $publishedInf `
                -InstanceId $baseline.instance_id
        } catch {
            $rollback = [pscustomobject][ordered]@{
                passed = $false
                error = $_.Exception.Message
            }
        }
    }
}

$passed = [string]::IsNullOrWhiteSpace($failure) -and
    $null -ne $rollback -and [bool]$rollback.passed
$result = [ordered]@{
    result_version = 1
    created_at = (Get-Date).ToString('o')
    passed = $passed
    status = if ($passed) { 'passed-and-restored' } else {
        if ($rollback -and [bool]$rollback.passed) {
            'failed-and-restored'
        } else {
            'rollback-required'
        }
    }
    error = $failure
    source_commit = $head
    candidate_path = $CandidatePath
    staged_published_inf = $publishedInf
    baseline = $baseline
    bound_snapshot = $boundSnapshot
    listener_lines = @($listenerLines)
    connect_lines = @($connectLines)
    observer_mode = $observerMode
    observer_lines = @($probeLines)
    executor_apply = if ($observerMode -eq 'executor-live') {
        [bool]$ExecutorApply
    } else {
        $null
    }
    disconnect_lines = @($disconnectLines)
    signaling = [ordered]@{
        started = $signalingStarted
        ready = $signalingReady
        completed = $signalingCompleted
        timed_out = $signalingTimedOut
        forced_termination = $signalingForced
        exit_code = $signalingExit
        released_before_power_off = $signalingReleasedBeforePowerOff
        baseline_restored_before_release = `
            $baselineRestoredBeforeSignalingRelease
        capability_discovery_completed = $signalingOutput -match
            '(?m)^Selected LDAC audio sink SEID: \d+\s*$'
        hold_started = $signalingOutput -match
            '(?m)^Signaling channel hold active for up to \d+ second\(s\)\.\s*$'
        stop_event_observed = $signalingOutput -match
            '(?m)^Signaling channel hold stop event observed\.\s*$'
        channel_closed = $signalingOutput -match
            '(?m)^Signaling channel closed\.\s*$'
        stdout = $signalingOutput
        stderr = $signalingError
    }
    rollback = $rollback
}
$result | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $resultPath -Encoding utf8

if (-not $passed) {
    $reason = if (-not [string]::IsNullOrWhiteSpace($failure)) {
        $failure
    } else {
        'The Microsoft AVRCP baseline was not restored.'
    }
    throw "V1 AVRCP observer gate failed: $reason Result: $resultPath"
}

if ($observerMode -eq 'executor-live') {
    Write-Host 'V1 AVRCP executor live trial completed and the Microsoft binding was restored.'
    if ($ExecutorApply) {
        Write-Host 'Executor apply mode: enabled; authorized decisions were written to the system.'
    } else {
        Write-Host 'Executor apply mode: dry-run; decisions were printed without system writes.'
    }
} else {
    Write-Host 'V1 AVRCP observer gate passed and the Microsoft binding was restored.'
}
if ($MinimalMediaSession) {
    Write-Host 'Absolute-volume notification and PASS THROUGH events were observed during a minimal LDAC silence media session and without Core Audio writes, input injection, audible playback, or AVRCP writes.'
} else {
    Write-Host 'Absolute-volume notification and PASS THROUGH events were observed with capability-only AVDTP signaling and without Core Audio writes, input injection, playback, media START, or media packets.'
}
Write-Host "Result: $resultPath"
