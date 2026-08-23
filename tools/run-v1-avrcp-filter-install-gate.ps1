# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1AvrcpFilterInstall,
    [ValidateRange(15, 120)]
    [int]$ObservationSeconds = 30,
    [ValidateRange(120, 600)]
    [int]$DurationSeconds = 300,
    [string]$CandidatePath,
    [string]$FilterProbePath,
    [string]$ConnectionProbePath,
    [string]$TransportProbePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-avrcp-filter-gate-common.ps1')

function Get-NewProcessLines {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][ref]$Offset
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return @() }
    try {
        $content = [string](Get-Content -LiteralPath $Path -Raw)
        if ([string]::IsNullOrEmpty($content)) { return @() }
        $offsetValue = [int]$Offset.Value
        if ($content.Length -le $offsetValue) { return @() }
        $pending = $content.Substring($offsetValue)
        $lastNewline = $pending.LastIndexOf("`n")
        if ($lastNewline -lt 0) { return @() }
        $complete = $pending.Substring(0, [int]$lastNewline + 1)
        $Offset.Value = [int]($offsetValue + $lastNewline + 1)
        $parts = @($complete -split '\r?\n')
        $new = [System.Collections.Generic.List[string]]::new()
        for ($index = 0; $index -lt $parts.Count - 1; $index++) {
            [void]$new.Add([string]$parts[$index])
        }
        return @($new)
    } catch {
        # A native process may hold the file or flush a partial buffer while
        # the gate polls. Live forwarding must return the lines that are
        # safely readable and never take the whole gate down on a transient
        # read; the final full-file read still preserves the evidence.
        return @()
    }
}

function Stop-V1FilterProcess {
    param([AllowNull()]$Process)
    if ($null -eq $Process) { return }
    if (-not $Process.HasExited) {
        $Process.Kill($true)
        $Process.WaitForExit()
    }
    $Process.Dispose()
}

function Complete-V1FilterMediaSession {
    param(
        [AllowNull()]$Process,
        [AllowNull()]$StopEvent,
        [Parameter(Mandatory = $true)][string]$OutputPath,
        [Parameter(Mandatory = $true)][string]$ErrorPath
    )
    $forced = $false
    if ($null -ne $StopEvent) {
        [void]$StopEvent.Set()
    }
    if ($null -ne $Process -and -not $Process.HasExited) {
        if (-not $Process.WaitForExit(30000)) {
            $forced = $true
            $Process.Kill($true)
            $Process.WaitForExit()
        }
    }
    $exitCode = if ($null -ne $Process -and $Process.HasExited) {
        $Process.ExitCode
    } else { -1 }
    $stdout = if (Test-Path -LiteralPath $OutputPath -PathType Leaf) {
        Get-Content -LiteralPath $OutputPath -Raw
    } else { '' }
    $stderr = if (Test-Path -LiteralPath $ErrorPath -PathType Leaf) {
        Get-Content -LiteralPath $ErrorPath -Raw
    } else { '' }
    return [pscustomobject][ordered]@{
        exit_code = $exitCode
        forced_termination = $forced
        stdout = $stdout
        stderr = $stderr
        start_accepted = $stdout -match `
            '(?m)^XM5 accepted START; the LDAC Media transport is ready\.\s*$'
        close_accepted = $stdout -match `
            '(?m)^XM5 accepted CLOSE; the test stream was released normally\.\s*$'
        signaling_closed = $stdout -match `
            '(?m)^Signaling channel closed\.\s*$'
    }
}

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The V1 AVRCP filter install gate requires PowerShell 7.'
}
Assert-V1AvrcpFilterAdministrator
if (-not $ConfirmV1AvrcpFilterInstall) {
    throw 'Refusing the upper-filter install gate. Re-run with -ConfirmV1AvrcpFilterInstall.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $projectRoot `
        'artifacts\v1-volume-sync\avrcp-filter-candidate'
}
$CandidatePath = [IO.Path]::GetFullPath($CandidatePath)
& (Join-Path $PSScriptRoot 'verify-v1-avrcp-filter-candidate.ps1') `
    -CandidatePath $CandidatePath
& (Join-Path $PSScriptRoot 'verify-v1-golden-checkpoint.ps1')
$manifest = Get-Content -LiteralPath (Join-Path $CandidatePath 'manifest.json') `
    -Raw | ConvertFrom-Json
if ([int]$manifest.policy_version -ne 7) {
    throw 'The live filter install gate requires a policy 7 candidate.'
}
$dirty = @(& git.exe -C $projectRoot status --porcelain)
$head = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $dirty.Count -ne 0 -or
    $head -cne [string]$manifest.source_commit) {
    throw 'The AVRCP filter gate candidate must match clean Git HEAD.'
}

$packageRoot = Join-Path $CandidatePath 'package'
$candidateInf = Join-Path $packageRoot 'NativeLdacAvrcpIoFilter.inf'
$candidateCertificate = Join-Path $packageRoot 'NativeLdacAvrcpIoFilter.cer'
$expectedFilterProbePath = Join-Path $CandidatePath `
    'tools\v1_avrcp_filter_probe.exe'
$expectedConnectionProbePath = Join-Path $CandidatePath `
    'tools\xm5_connection_probe.exe'
$expectedTransportProbePath = Join-Path $CandidatePath `
    'tools\transport_probe.exe'
if ([string]::IsNullOrWhiteSpace($FilterProbePath)) {
    $FilterProbePath = $expectedFilterProbePath
}
if ([string]::IsNullOrWhiteSpace($ConnectionProbePath)) {
    $ConnectionProbePath = $expectedConnectionProbePath
}
if ([string]::IsNullOrWhiteSpace($TransportProbePath)) {
    $TransportProbePath = $expectedTransportProbePath
}
$FilterProbePath = [IO.Path]::GetFullPath($FilterProbePath)
$ConnectionProbePath = [IO.Path]::GetFullPath($ConnectionProbePath)
$TransportProbePath = [IO.Path]::GetFullPath($TransportProbePath)
if ($FilterProbePath -ine [IO.Path]::GetFullPath($expectedFilterProbePath) -or
    $ConnectionProbePath -ine [IO.Path]::GetFullPath(
        $expectedConnectionProbePath) -or
    $TransportProbePath -ine [IO.Path]::GetFullPath(
        $expectedTransportProbePath)) {
    throw 'The live filter gate does not allow probe path overrides.'
}
foreach ($required in @($candidateInf, $candidateCertificate,
                        $FilterProbePath, $ConnectionProbePath,
                        $TransportProbePath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "The filter gate input is missing: $required"
    }
}
$transportInfoLines = @(& $TransportProbePath --info 2>&1)
$transportInfoExit = [int]$LASTEXITCODE
if ($transportInfoExit -ne 0) {
    throw "The read-only LDAC transport preflight failed with exit $transportInfoExit."
}

$control = Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control'
if ([string]$control.SystemStartOptions -notmatch '(^|\s)TESTSIGNING(\s|$)') {
    throw 'The current boot is not in TESTSIGNING mode.'
}
$candidateCertificateInfo = Get-PfxCertificate -FilePath $candidateCertificate
$certificateThumbprint = [string]$candidateCertificateInfo.Thumbprint
$rootTrusted = @(Get-ChildItem Cert:\LocalMachine\Root |
    Where-Object { [string]$_.Thumbprint -ieq $certificateThumbprint })
$publisherTrusted = @(Get-ChildItem Cert:\LocalMachine\TrustedPublisher |
    Where-Object { [string]$_.Thumbprint -ieq $certificateThumbprint })
if ($rootTrusted.Count -ne 1 -or $publisherTrusted.Count -ne 1) {
    throw 'The candidate test certificate is not already trusted in both LocalMachine stores; no certificate import was attempted.'
}

$target = Get-V1AvrcpFilterTargetDevice
$baseline = Get-V1AvrcpFilterSnapshot -Device $target
if (-not (Test-V1AvrcpFilterMicrosoftBaseline -Snapshot $baseline)) {
    throw 'The current XM5 AVRCP PDO is not the healthy Microsoft baseline.'
}
$existingPackages = @(Get-V1AvrcpFilterPackages)
if ($existingPackages.Count -ne 0) {
    throw 'A NativeLdacAvrcpIoFilter package is already staged; run the filter rollback command first.'
}
$preexistingControl = Test-V1AvrcpFilterControlAbsent `
    -ProbePath $FilterProbePath
if (-not [bool]$preexistingControl.absent) {
    throw 'A NativeLdacAvrcpIoFilter control device is already loaded without a managed package state.'
}
$radioLines = @(& $ConnectionProbePath --radio-state 2>&1)
if ($LASTEXITCODE -ne 0 -or
    ($radioLines -join "`n") -notmatch 'ready|connectable') {
    throw 'Windows Bluetooth must be on and connectable.'
}
$stateLines = @(& $ConnectionProbePath --state 2>&1)
if (($stateLines -join "`n") -notmatch 'disconnected') {
    throw 'XM5 must be physically off and disconnected before this gate starts.'
}

Write-Host 'V1 AVRCP upper-filter install preflight passed.'
Write-Host 'XM5 must remain off until the exact filter control device and ACL watcher are ready.'
Write-Host 'Microsoft AVRCP remains the function driver; the package adds only one exact-device upper filter.'
Write-Host 'The gate will restart the exact XM5 AVRCP PDO once after installing the extension filter; it never restarts the Bluetooth radio or device class.'
Write-Host 'After physical connection, the gate starts one bounded LDAC encoded-silence media session so Microsoft AVRCP can enter its media-ready state.'
Write-Host 'Only after media START will the gate drain and arm the read-only filter probe, then display a dedicated action prompt.'
Write-Host 'Raw filter events and one-second media telemetry remain in the result logs without scrolling the action prompt away.'
Write-Host 'After the filter observation completes, turn off XM5 only when the disconnect watcher says it is armed.'

if (-not $PSCmdlet.ShouldProcess(
        'one exact XM5 AVRCP 0x110E device-level upper filter',
        'Stage the filter, restart the exact PDO once, hold encoded silence, observe read-only Microsoft AVRCP I/O, then remove the filter and verify rollback')) {
    return
}

$trialRoot = Join-Path $projectRoot (
    'artifacts\v1-volume-sync\trial\avrcp-filter-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$stateRoot = Join-Path $projectRoot 'artifacts\v1-volume-sync\filter-gate'
New-Item -ItemType Directory -Path $trialRoot -Force | Out-Null
New-Item -ItemType Directory -Path $stateRoot -Force | Out-Null
$resultPath = Join-Path $trialRoot 'result.json'
$statePath = Join-Path $stateRoot 'install-state.json'
$publishedInf = $null
$restartCount = 0
$failure = $null
$failureCode = $null
$rollback = $null
$connectLines = [System.Collections.Generic.List[string]]::new()
$disconnectLines = [System.Collections.Generic.List[string]]::new()
$filterLines = [System.Collections.Generic.List[string]]::new()
$filterErrorLines = [System.Collections.Generic.List[string]]::new()
$mediaLines = [System.Collections.Generic.List[string]]::new()
$actions = [System.Collections.Generic.List[object]]::new()
$connectProcess = $null
$disconnectProcess = $null
$filterProcess = $null
$mediaProcess = $null
$mediaStopEvent = $null
$mediaStopEventName = ''
$mediaCompletion = $null
$connectOutput = Join-Path $trialRoot 'connect.out.log'
$connectError = Join-Path $trialRoot 'connect.err.log'
$disconnectOutput = Join-Path $trialRoot 'disconnect.out.log'
$disconnectError = Join-Path $trialRoot 'disconnect.err.log'
$filterOutput = Join-Path $trialRoot 'filter-probe.out.log'
$filterError = Join-Path $trialRoot 'filter-probe.err.log'
$mediaOutput = Join-Path $trialRoot 'silence-media.out.log'
$mediaError = Join-Path $trialRoot 'silence-media.err.log'
$connectOffset = 0
$disconnectOffset = 0
$filterOffset = 0
$mediaOffset = 0
$connectExit = -1
$disconnectExit = -1
$filterExit = -1
$connectObserved = $false
$disconnectObserved = $false
$filterReady = $false
$mediaStarted = $false
$mediaReady = $false
$mediaReleased = $false
$mediaReadyDeadline = [DateTimeOffset]::MinValue
$filterProbeRequests = 0
$filterProbeCompletions = 0
$filterProbeFailures = 0
$filterProbeDecodedCapability = 0
$filterProbeDecodedVolumeChanged = 0
$filterProbeDecodedPassThrough = 0
$filterProbeDecodedVendorCommand = 0
$filterProbeDecodedProtocolError = 0
$safeToRollback = $true
$aclConnectedSeen = $false
$filterArmed = $false
$filterArmedDeadline = [DateTimeOffset]::MinValue
$filterStartedAfterAcl = $false
$filterStartedAfterMediaReady = $false
$gesturePrompted = $false
$state = [ordered]@{
    state_version = 1
    transaction_state = 'pre-mutation'
    created_at = (Get-Date).ToString('o')
    source_commit = $head
    candidate_path = $CandidatePath
    published_inf = $null
    baseline = $baseline
    candidate_manifest = $manifest
    certificate_thumbprint = $certificateThumbprint
    exact_pdo_restart_count = 0
}
Write-V1AvrcpFilterJsonAtomically -Path $statePath -Value $state

try {
    $install = Invoke-V1AvrcpFilterPnpUtil -Arguments @(
        '/add-driver', $candidateInf, '/install')
    $install.lines | Set-Content -LiteralPath `
        (Join-Path $trialRoot 'install.log') -Encoding utf8
    $actions.Add([pscustomobject][ordered]@{
        action = 'stage-and-install-extension-inf'
        exit_code = $install.exit_code
        lines = @($install.lines)
    })
    $afterInstallPackages = @(Get-V1AvrcpFilterPackages)
    if ($afterInstallPackages.Count -eq 1) {
        $publishedInf = Assert-V1AvrcpFilterPublishedInf `
            -PublishedInf ([string]$afterInstallPackages[0].published_inf)
        if (-not (Test-V1AvrcpFilterPublishedInfMatchesCandidate `
                -PublishedInf $publishedInf `
                -CandidateInfPath $candidateInf)) {
            throw 'The newly staged Driver Store INF does not match the verified filter candidate.'
        }
        $state.published_inf = $publishedInf
        $state.transaction_state = 'package-detected'
        Write-V1AvrcpFilterJsonAtomically -Path $statePath -Value $state
    }
    if ($install.exit_code -notin @(0, 259, 3010)) {
        throw "Filter package installation failed with exit $($install.exit_code)."
    }
    if ($afterInstallPackages.Count -ne 1) {
        throw 'Exactly one newly staged NativeLdacAvrcpIoFilter package is required.'
    }
    $state.transaction_state = 'package-installed'
    Write-V1AvrcpFilterJsonAtomically -Path $statePath -Value $state

    $controlCheck = Test-V1AvrcpFilterControlAbsent `
        -ProbePath $FilterProbePath
    $actions.Add([pscustomobject][ordered]@{
        action = 'probe-filter-control-before-required-restart'
        exit_code = $controlCheck.exit_code
        absent = [bool]$controlCheck.absent
        healthy = [bool]$controlCheck.healthy
    })

    # Extension INF installation can expose the filter control device while
    # BthAvctpSvc still owns a handle opened through the pre-filter stack.
    # Recreate the exact Microsoft function stack once on every fresh install.
    $restart = Invoke-V1AvrcpFilterPnpUtil -Arguments @(
        '/restart-device', $baseline.instance_id)
    $restart.lines | Set-Content -LiteralPath `
        (Join-Path $trialRoot 'exact-pdo-restart.log') -Encoding utf8
    $actions.Add([pscustomobject][ordered]@{
        action = 'restart-exact-avrcp-pdo-after-extension-install'
        exit_code = $restart.exit_code
        lines = @($restart.lines)
    })
    $restartCount = 1
    $state.exact_pdo_restart_count = $restartCount
    $state.transaction_state = 'exact-pdo-restart-issued'
    Write-V1AvrcpFilterJsonAtomically -Path $statePath -Value $state
    if ($restart.exit_code -ne 0) {
        throw 'The exact AVRCP PDO restart failed while loading the filter.'
    }
    $state.transaction_state = 'exact-pdo-restarted'
    Write-V1AvrcpFilterJsonAtomically -Path $statePath -Value $state
    Start-Sleep -Seconds 2

    $filterReadyDeadline = [DateTimeOffset]::Now.AddSeconds(30)
    do {
        $controlCheck = Test-V1AvrcpFilterControlAbsent -ProbePath $FilterProbePath
        if ([bool]$controlCheck.healthy) {
            $filterReady = $true
            break
        }
        if (-not [bool]$controlCheck.absent) {
            throw "The filter control device is present but unhealthy after restart (probe exit $($controlCheck.exit_code))."
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTimeOffset]::Now -lt $filterReadyDeadline)
    if (-not $filterReady) {
        throw 'The exact upper-filter control device did not become available.'
    }
    $loadedSnapshot = Wait-V1AvrcpFilterMicrosoftBaseline `
        -InstanceId $baseline.instance_id -TimeoutSeconds 30
    if ($null -eq $loadedSnapshot -or
        $loadedSnapshot.instance_id -ine $baseline.instance_id) {
        throw 'Microsoft AVRCP owner or PDO health changed while the upper filter loaded.'
    }
    $actions.Add([pscustomobject][ordered]@{
        action = 'verify-microsoft-owner-after-required-restart'
        snapshot = $loadedSnapshot
    })
    $state.transaction_state = 'filter-loaded'
    Write-V1AvrcpFilterJsonAtomically -Path $statePath -Value $state

    $connectProcess = Start-Process -FilePath $ConnectionProbePath `
        -ArgumentList @('--wait-acl-connect', '90') `
        -RedirectStandardOutput $connectOutput `
        -RedirectStandardError $connectError `
        -WindowStyle Hidden -PassThru
    $armedDeadline = [DateTimeOffset]::Now.AddSeconds(30)
    $armed = $false
    while ([DateTimeOffset]::Now -lt $armedDeadline) {
        foreach ($line in @(Get-NewProcessLines -Path $connectOutput -Offset ([ref]$connectOffset))) {
            [void]$connectLines.Add($line)
            Write-Host $line
            if ($line -match 'ACL watcher armed') { $armed = $true }
        }
        if ($armed -or $connectProcess.HasExited) { break }
        Start-Sleep -Milliseconds 100
    }
    if (-not $armed) {
        throw 'The ACL watcher did not reach its armed state.'
    }

    Write-Host 'The ACL watcher is armed and the filter control device is healthy. Turn on XM5 normally now.'
    $observationDeadline = [DateTimeOffset]::Now.AddSeconds($DurationSeconds)
    while ([DateTimeOffset]::Now -lt $observationDeadline) {
        foreach ($line in @(Get-NewProcessLines -Path $connectOutput -Offset ([ref]$connectOffset))) {
            [void]$connectLines.Add($line)
            Write-Host $line
            if (-not $aclConnectedSeen -and
                $line -match 'XM5 ACL event: connected\.') {
                $aclConnectedSeen = $true
                Write-Host 'XM5 is connected. Starting the bounded LDAC encoded-silence media prerequisite now.'
                $mediaStopEventName = 'Local\NativeLdacV1AvrcpFilterMediaStop-' +
                    $PID + '-' + [guid]::NewGuid().ToString('N')
                $mediaCreatedNew = $false
                $mediaStopEvent = [Threading.EventWaitHandle]::new(
                    $false,
                    [Threading.EventResetMode]::ManualReset,
                    $mediaStopEventName,
                    [ref]$mediaCreatedNew)
                if (-not $mediaCreatedNew) {
                    throw 'The bounded silence-media stop event already existed.'
                }
                $mediaProcess = Start-Process `
                    -FilePath $TransportProbePath `
                    -ArgumentList @(
                        '--stream-silence-continuous',
                        '--open-attempts', '1',
                        '--stop-event', $mediaStopEventName) `
                    -RedirectStandardOutput $mediaOutput `
                    -RedirectStandardError $mediaError `
                    -WindowStyle Hidden -PassThru
                $mediaStarted = $null -ne $mediaProcess
                if (-not $mediaStarted) {
                    throw 'The bounded LDAC silence media session did not start.'
                }
                $mediaReadyDeadline = [DateTimeOffset]::Now.AddSeconds(30)
            }
        }
        foreach ($line in @(Get-NewProcessLines -Path $mediaOutput -Offset ([ref]$mediaOffset))) {
            [void]$mediaLines.Add($line)
            if ($line -notmatch '^Live:') { Write-Host $line }
            if ($line -match
                'XM5 accepted START; the LDAC Media transport is ready\.') {
                $mediaReady = $true
            }
        }
        if ($mediaStarted -and -not $mediaReady) {
            if ($mediaProcess.HasExited) {
                throw 'The bounded LDAC silence media session exited before reaching START.'
            }
            if ([DateTimeOffset]::Now -ge $mediaReadyDeadline) {
                throw 'The bounded LDAC silence media session did not reach START within 30 seconds.'
            }
        }
        if ($mediaReady -and $null -eq $filterProcess) {
            Write-Host 'Minimal LDAC media session is ready. Draining connection-time filter events and arming the gesture observation now.'
            $filterProcess = Start-Process -FilePath $FilterProbePath `
                -ArgumentList @(
                    '--duration-seconds', [string]$ObservationSeconds,
                    '--wait-for-first-request-seconds', '90') `
                -RedirectStandardOutput $filterOutput `
                -RedirectStandardError $filterError `
                -WindowStyle Hidden -PassThru
            $filterStartedAfterAcl = $aclConnectedSeen
            $filterStartedAfterMediaReady = $mediaReady
            $filterArmedDeadline = [DateTimeOffset]::Now.AddSeconds(30)
        }
        if ($null -ne $filterProcess) {
            foreach ($line in @(Get-NewProcessLines -Path $filterOutput -Offset ([ref]$filterOffset))) {
                [void]$filterLines.Add($line)
                if ($line -notmatch '^event sequence=') { Write-Host $line }
                if (-not $filterArmed -and
                    $line -match 'AVRCP filter trace watcher armed') {
                    $filterArmed = $true
                    Write-Host ''
                    Write-Host '=== XM5 ACTION WINDOW READY ===' -ForegroundColor Cyan
                    Write-Host "Start now: perform at least two volume-up gestures, two volume-down gestures, and one double-tap. The first captured request starts a full $ObservationSeconds-second observation window."
                    Write-Host 'Do not start a player or change PC volume. Raw trace and media telemetry are still being saved to the result.'
                    Write-Host '================================' -ForegroundColor Cyan
                    $gesturePrompted = $true
                }
            }
            if (-not $filterArmed) {
                if ($filterProcess.HasExited) {
                    throw 'The read-only filter probe exited before reaching its armed state.'
                }
                if ([DateTimeOffset]::Now -ge $filterArmedDeadline) {
                    throw 'The read-only filter probe did not reach its armed state.'
                }
            }
        }
        if ($connectProcess.HasExited -and
            $null -ne $filterProcess -and $filterProcess.HasExited) {
            break
        }
        Start-Sleep -Milliseconds 100
    }
    foreach ($line in @(Get-NewProcessLines -Path $connectOutput -Offset ([ref]$connectOffset))) {
        [void]$connectLines.Add($line)
        Write-Host $line
    }
    foreach ($line in @(Get-NewProcessLines -Path $filterOutput -Offset ([ref]$filterOffset))) {
        [void]$filterLines.Add($line)
        if ($line -notmatch '^event sequence=') { Write-Host $line }
    }
    foreach ($line in @(Get-NewProcessLines -Path $mediaOutput -Offset ([ref]$mediaOffset))) {
        [void]$mediaLines.Add($line)
        if ($line -notmatch '^Live:') { Write-Host $line }
    }
    if ($null -eq $filterProcess) {
        throw 'The read-only filter probe was not started after media readiness.'
    }
    if (-not $filterProcess.HasExited) {
        throw 'The bounded filter observation duration expired.'
    }
    $filterProcess.WaitForExit()
    $filterExit = $filterProcess.ExitCode
    if (Test-Path -LiteralPath $filterError -PathType Leaf) {
        foreach ($line in @(Get-Content -LiteralPath $filterError)) {
            [void]$filterErrorLines.Add([string]$line)
        }
    }
    $connectObserved = @($connectLines | Where-Object {
        $_ -match 'XM5 ACL event: connected\.'
    }).Count -gt 0
    if (-not $connectProcess.HasExited) {
        [void]$connectProcess.WaitForExit(5000)
    }
    if ($connectProcess.HasExited) { $connectExit = $connectProcess.ExitCode }
    if (-not $connectObserved -or $connectExit -ne 0) {
        throw 'One physical XM5 ACL connect was not observed.'
    }
    if (-not $mediaStarted -or -not $mediaReady) {
        throw 'The bounded LDAC silence media prerequisite was not active during observation.'
    }
    if (-not $filterStartedAfterAcl -or
        -not $filterStartedAfterMediaReady -or
        -not $filterArmed -or -not $gesturePrompted) {
        throw 'The gesture observation was not armed after physical ACL connect and media readiness.'
    }
    $filterJoined = $filterLines -join "`n"
    $statusMatch = [regex]::Match(
        $filterJoined,
        'window status: requests (?<requests>\d+); completions (?<completions>\d+); capture-failures (?<failures>\d+)')
    if (-not $statusMatch.Success) {
        if ($filterExit -eq 8 -and
            ($filterErrorLines -join "`n") -match
                'No AVRCP filter request was observed') {
            $failureCode = 'no-post-connect-filter-request'
            throw 'No post-connect AVRCP request reached the upper filter before the bounded wait expired.'
        }
        throw 'The filter probe did not publish its final bounded status.'
    }
    $filterProbeRequests = [int]$statusMatch.Groups['requests'].Value
    $filterProbeCompletions = [int]$statusMatch.Groups['completions'].Value
    $filterProbeFailures = [int]$statusMatch.Groups['failures'].Value
    if ($filterExit -eq 8 -and $filterProbeRequests -eq 0) {
        $failureCode = 'no-post-connect-filter-request'
        throw 'No post-connect AVRCP request reached the upper filter before the bounded wait expired.'
    }
    if ($filterExit -ne 0 -or $filterProbeRequests -lt 1 -or
        $filterProbeCompletions -lt 1 -or $filterProbeFailures -ne 0) {
        throw "Filter evidence was incomplete: requests=$filterProbeRequests, completions=$filterProbeCompletions, capture-failures=$filterProbeFailures."
    }
    $decodedMatch = [regex]::Match(
        $filterJoined,
        'decoded status: capability=(?<capability>\d+); volume-changed=(?<volume>\d+); pass-through=(?<pass>\d+); vendor-command=(?<vendor>\d+); protocol-error=(?<error>\d+)')
    if (-not $decodedMatch.Success) {
        throw 'The filter probe did not publish its decoded AVRCP summary.'
    }
    $filterProbeDecodedCapability = [int]$decodedMatch.Groups['capability'].Value
    $filterProbeDecodedVolumeChanged = [int]$decodedMatch.Groups['volume'].Value
    $filterProbeDecodedPassThrough = [int]$decodedMatch.Groups['pass'].Value
    $filterProbeDecodedVendorCommand = [int]$decodedMatch.Groups['vendor'].Value
    $filterProbeDecodedProtocolError = [int]$decodedMatch.Groups['error'].Value
    if ($filterProbeDecodedVolumeChanged -lt 1 -or
        $filterProbeDecodedPassThrough -lt 1) {
        throw "Decoded AVRCP gesture evidence was incomplete: volume-changed=$filterProbeDecodedVolumeChanged, pass-through=$filterProbeDecodedPassThrough."
    }
    $observedSnapshot = Get-V1AvrcpFilterSnapshot `
        -Device (Get-V1AvrcpFilterTargetDevice)
    if ($observedSnapshot.instance_id -ine $baseline.instance_id -or
        -not (Test-V1AvrcpFilterMicrosoftBaseline `
            -Snapshot $observedSnapshot)) {
        throw 'Microsoft AVRCP owner or PDO health changed during filter observation.'
    }
    $mediaCompletion = Complete-V1FilterMediaSession `
        -Process $mediaProcess `
        -StopEvent $mediaStopEvent `
        -OutputPath $mediaOutput `
        -ErrorPath $mediaError
    if ($null -ne $mediaProcess) {
        $mediaProcess.Dispose()
        $mediaProcess = $null
    }
    if ($null -ne $mediaStopEvent) {
        $mediaStopEvent.Dispose()
        $mediaStopEvent = $null
    }
    $mediaExitAccepted = [int]$mediaCompletion.exit_code -in @(0, 130) -or
        ([int]$mediaCompletion.exit_code -eq 5 -and
         [bool]$mediaCompletion.close_accepted)
    if (-not $mediaExitAccepted -or
        [bool]$mediaCompletion.forced_termination -or
        -not [bool]$mediaCompletion.start_accepted -or
        -not [bool]$mediaCompletion.close_accepted -or
        -not [bool]$mediaCompletion.signaling_closed) {
        throw "The bounded LDAC silence media session did not close cleanly (exit $($mediaCompletion.exit_code))."
    }
    $mediaReleased = $true
    Write-Host 'The encoded-silence media session was released cleanly while XM5 remained on.'
    $state.transaction_state = 'observation-complete'
    Write-V1AvrcpFilterJsonAtomically -Path $statePath -Value $state

    $disconnectProcess = Start-Process -FilePath $ConnectionProbePath `
        -ArgumentList @('--wait-acl-disconnect', '90') `
        -RedirectStandardOutput $disconnectOutput `
        -RedirectStandardError $disconnectError `
        -WindowStyle Hidden -PassThru
    $disconnectArmedDeadline = [DateTimeOffset]::Now.AddSeconds(30)
    $disconnectArmed = $false
    while ([DateTimeOffset]::Now -lt $disconnectArmedDeadline) {
        foreach ($line in @(Get-NewProcessLines -Path $disconnectOutput -Offset ([ref]$disconnectOffset))) {
            [void]$disconnectLines.Add($line)
            Write-Host $line
            if ($line -match 'ACL watcher armed') { $disconnectArmed = $true }
        }
        if ($disconnectArmed -or $disconnectProcess.HasExited) { break }
        Start-Sleep -Milliseconds 100
    }
    if (-not $disconnectArmed) {
        throw 'The disconnect watcher did not reach its armed state.'
    }
    Write-Host 'Filter observation is complete. Turn off XM5 normally now.'
    foreach ($line in @(Get-NewProcessLines -Path $disconnectOutput -Offset ([ref]$disconnectOffset))) {
        [void]$disconnectLines.Add($line)
        Write-Host $line
    }
    $disconnectDeadline = [DateTimeOffset]::Now.AddSeconds(120)
    while ([DateTimeOffset]::Now -lt $disconnectDeadline -and
           -not $disconnectProcess.HasExited) {
        foreach ($line in @(Get-NewProcessLines -Path $disconnectOutput -Offset ([ref]$disconnectOffset))) {
            [void]$disconnectLines.Add($line)
            Write-Host $line
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $disconnectProcess.HasExited) {
        $safeToRollback = $false
        throw 'The physical XM5 disconnect watcher timed out; leave the package staged and use the rollback command after XM5 is off.'
    }
    foreach ($line in @(Get-NewProcessLines -Path $disconnectOutput -Offset ([ref]$disconnectOffset))) {
        [void]$disconnectLines.Add($line)
        Write-Host $line
    }
    $disconnectProcess.WaitForExit()
    $disconnectExit = $disconnectProcess.ExitCode
    $disconnectObserved = @($disconnectLines | Where-Object {
        $_ -match 'XM5 ACL event: disconnected\.'
    }).Count -gt 0
    if (-not $disconnectObserved -or $disconnectExit -ne 0) {
        $safeToRollback = $false
        throw 'One physical XM5 ACL disconnect was not observed; leave the package staged and use the rollback command after XM5 is off.'
    }

    $rollback = Invoke-V1AvrcpFilterRollback `
        -PublishedInf $publishedInf `
        -InstanceId $baseline.instance_id `
        -ProbePath $FilterProbePath `
        -LogDirectory $trialRoot `
        -AllowExactPdoRestart $false `
        -ExistingExactPdoRestartCount $restartCount
    if (-not [bool]$rollback.passed) {
        throw 'The filter package could not be removed cleanly without exceeding the one-restart bound.'
    }
    $state.transaction_state = 'passed-and-restored'
    Write-V1AvrcpFilterJsonAtomically -Path $statePath -Value $state
} catch {
    $failure = $_.Exception.Message
} finally {
    if ($null -ne $mediaProcess -or $null -ne $mediaStopEvent) {
        try {
            $mediaCompletion = Complete-V1FilterMediaSession `
                -Process $mediaProcess `
                -StopEvent $mediaStopEvent `
                -OutputPath $mediaOutput `
                -ErrorPath $mediaError
            $mediaReleased = [bool]$mediaCompletion.close_accepted -and
                [bool]$mediaCompletion.signaling_closed -and
                -not [bool]$mediaCompletion.forced_termination
        } catch {
            if ([string]::IsNullOrWhiteSpace($failure)) {
                $failure = 'Silence-media cleanup failed: ' +
                    $_.Exception.Message
            }
        } finally {
            if ($null -ne $mediaProcess) {
                $mediaProcess.Dispose()
                $mediaProcess = $null
            }
            if ($null -ne $mediaStopEvent) {
                $mediaStopEvent.Dispose()
                $mediaStopEvent = $null
            }
        }
    }
    try {
        Stop-V1FilterProcess -Process $filterProcess
        Stop-V1FilterProcess -Process $connectProcess
        Stop-V1FilterProcess -Process $disconnectProcess
    } catch {
        if ([string]::IsNullOrWhiteSpace($failure)) {
            $failure = 'Process cleanup failed: ' + $_.Exception.Message
        }
    }
    if ($filterErrorLines.Count -eq 0 -and
        (Test-Path -LiteralPath $filterError -PathType Leaf)) {
        foreach ($line in @(Get-Content -LiteralPath $filterError)) {
            [void]$filterErrorLines.Add([string]$line)
        }
    }
    if ($null -ne $publishedInf -and $null -eq $rollback) {
        $safeStateLines = @(& $ConnectionProbePath --state 2>&1)
        $safeToRollback = $safeToRollback -and
            (($safeStateLines -join "`n") -match 'disconnected')
        if ($safeToRollback) {
            try {
                $rollback = Invoke-V1AvrcpFilterRollback `
                    -PublishedInf $publishedInf `
                    -InstanceId $baseline.instance_id `
                    -ProbePath $FilterProbePath `
                    -LogDirectory $trialRoot `
                    -AllowExactPdoRestart $false `
                    -ExistingExactPdoRestartCount $restartCount
            } catch {
                $rollback = [pscustomobject][ordered]@{
                    passed = $false
                    error = $_.Exception.Message
                }
            }
        } else {
            Write-Host 'The filter package remains staged because XM5 is still connected or its disconnect was not proven.'
            Write-Host 'Turn off XM5, then run: pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\rollback-v1-avrcp-filter-install-gate.ps1 -ConfirmV1AvrcpFilterRollback'
        }
    }
}

$passed = [string]::IsNullOrWhiteSpace($failure) -and
    $null -ne $rollback -and [bool]$rollback.passed
if ($null -ne $publishedInf) {
    $state.transaction_state = if ($passed) {
        'passed-and-restored'
    } elseif ($null -ne $rollback -and [bool]$rollback.passed) {
        'failed-and-restored'
    } else {
        'rollback-required'
    }
    $state.completed_at = (Get-Date).ToString('o')
    Write-V1AvrcpFilterJsonAtomically -Path $statePath -Value $state
}
$result = [ordered]@{
    result_version = 5
    created_at = (Get-Date).ToString('o')
    passed = $passed
    status = if ($passed) { 'passed-and-restored' } elseif (
        $null -ne $rollback -and [bool]$rollback.passed) {
        'failed-and-restored'
    } else { 'rollback-required' }
    failure = $failure
    failure_code = $failureCode
    source_commit = $head
    candidate_path = $CandidatePath
    published_inf = $publishedInf
    exact_pdo_restart_count = $restartCount
    baseline = $baseline
    connect_lines = @($connectLines)
    disconnect_lines = @($disconnectLines)
    filter_probe_lines = @($filterLines)
    filter_probe_error_lines = @($filterErrorLines)
    filter_probe_exit = $filterExit
    filter_probe_requests = $filterProbeRequests
    filter_probe_completions = $filterProbeCompletions
    filter_probe_capture_failures = $filterProbeFailures
    filter_probe_decoded_capability = $filterProbeDecodedCapability
    filter_probe_decoded_volume_changed = $filterProbeDecodedVolumeChanged
    filter_probe_decoded_pass_through = $filterProbeDecodedPassThrough
    filter_probe_decoded_vendor_command = $filterProbeDecodedVendorCommand
    filter_probe_decoded_protocol_error = $filterProbeDecodedProtocolError
    filter_probe_started_after_acl = $filterStartedAfterAcl
    filter_probe_started_after_media_ready = $filterStartedAfterMediaReady
    gesture_prompted = $gesturePrompted
    silence_media = [ordered]@{
        started = $mediaStarted
        ready = $mediaReady
        released_cleanly = $mediaReleased
        live_lines = @($mediaLines)
        completion = $mediaCompletion
    }
    actions = @($actions)
    rollback = $rollback
    state_path = $statePath
}
Write-V1AvrcpFilterJsonAtomically -Path $resultPath -Value $result

if (-not $passed) {
    $reason = if (-not [string]::IsNullOrWhiteSpace($failure)) {
        $failure
    } else { 'The exact filter package was not removed and Microsoft AVRCP was not fully verified.' }
    throw "V1 AVRCP upper-filter install gate failed: $reason Result: $resultPath"
}
Write-Host 'V1 AVRCP upper-filter install gate passed.'
Write-Host 'Microsoft AVRCP remained the exact PDO function driver and the filter package was removed.'
Write-Host 'The bounded LDAC encoded-silence media session reached START and closed cleanly before power-off.'
Write-Host "Observed filter requests=$filterProbeRequests completions=$filterProbeCompletions capture-failures=$filterProbeFailures."
Write-Host "Result: $resultPath"
