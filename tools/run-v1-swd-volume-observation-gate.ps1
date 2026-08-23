# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1SwdVolumeObservation,
    [ValidateRange(15, 60)][int]$ObservationSeconds = 30,
    [ValidateRange(180, 300)][int]$DurationSeconds = 240,
    [string]$CandidatePath,
    [string]$GoldenCheckpointPath,
    [string]$BindingResultPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-swd-endpoint-candidate-common.ps1')
. (Join-Path $PSScriptRoot 'v1-swd-endpoint-system-common.ps1')
. (Join-Path $PSScriptRoot 'v1-swd-volume-observation-common.ps1')

function Invoke-V1SwdStreamingProbe {
    param(
        [Parameter(Mandatory = $true)][string]$ProbePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    $lines = @()
    $exitCode = -1
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $ProbePath @Arguments 2>&1 | ForEach-Object {
            $line = [string]$_
            Write-Host $line
            $line
        })
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    return [pscustomobject][ordered]@{
        exit_code = $exitCode
        text = @($lines)
    }
}

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The SWD volume observation gate requires PowerShell 7.'
}
Assert-V1SwdAdministrator
if (-not $ConfirmV1SwdVolumeObservation) {
    throw 'Refusing to run the isolated volume observation without -ConfirmV1SwdVolumeObservation.'
}
if ($DurationSeconds -lt $ObservationSeconds + 120) {
    throw 'The host duration must leave at least 120 seconds around the observation window.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $projectRoot `
        'artifacts\v1-volume-sync\endpoint-candidate'
}
$candidate = Get-V1SwdEndpointCandidate -CandidatePath $CandidatePath
$manifest = $candidate.manifest
if ([string]$manifest.expected_instance_id -ine
        $script:V1SwdEndpointBindingInstanceId -or
    [string]$manifest.hardware_id -ine
        $script:V1SwdEndpointBindingHardwareId -or
    [string]$manifest.service_name -cne
        $script:V1SwdEndpointBindingService -or
    $manifest.volume_observation_presence_supported -ne $true -or
    $manifest.stop_event_supported -ne $true -or
    $manifest.xm5_connection_probe_included -ne $true -or
    $manifest.capability_only_signaling_hold_supported -ne $true) {
    throw 'The endpoint candidate does not support the bounded volume observation.'
}
$head = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$status = @(& git.exe -C $projectRoot status --porcelain `
    --untracked-files=all)
if ($LASTEXITCODE -ne 0 -or $status.Count -ne 0 -or
    $head -cne [string]$manifest.source_commit) {
    throw 'The volume observation gate requires the exact clean candidate source.'
}
& (Join-Path $candidate.root 'verify-v1-swd-endpoint-candidate.ps1') `
    -CandidatePath $candidate.root

if ([string]::IsNullOrWhiteSpace($GoldenCheckpointPath)) {
    $GoldenCheckpointPath = [string]$manifest.golden_checkpoint
}
& (Join-Path $candidate.root 'verify-v1-golden-checkpoint.ps1') `
    -CheckpointPath $GoldenCheckpointPath

if ([string]::IsNullOrWhiteSpace($BindingResultPath)) {
    $bindingTrials = Get-ChildItem -LiteralPath (Join-Path $projectRoot `
        'artifacts\v1-volume-sync\trial') -Filter 'endpoint-binding-*' `
        -Directory -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending
    foreach ($trial in $bindingTrials) {
        $candidateResult = Join-Path $trial.FullName 'result.json'
        if (-not (Test-Path -LiteralPath $candidateResult -PathType Leaf)) {
            continue
        }
        $observedResult = Get-Content -LiteralPath $candidateResult -Raw |
            ConvertFrom-Json
        if ($observedResult.passed -eq $true) {
            $BindingResultPath = $candidateResult
            break
        }
    }
}
if ([string]::IsNullOrWhiteSpace($BindingResultPath) -or
    -not (Test-Path -LiteralPath $BindingResultPath -PathType Leaf)) {
    throw 'A passed endpoint-binding prerequisite is required.'
}
$BindingResultPath = [IO.Path]::GetFullPath($BindingResultPath)
$binding = Get-Content -LiteralPath $BindingResultPath -Raw |
    ConvertFrom-Json
if ([int]$binding.policy_version -ne 2 -or
    $binding.passed -ne $true -or
    [string]$binding.expected_parent -ine
        [string]$manifest.expected_parent -or
    [string]$binding.expected_container -ine
        [string]$manifest.remote_container_id -or
    $binding.host_process.completed -ne $true -or
    $binding.rollback.device_instance_absent -ne $true -or
    $binding.rollback.package_remove_succeeded -ne $true -or
    $binding.safety.current_root_endpoint_preserved -ne $true) {
    throw 'The endpoint-binding prerequisite does not prove safe topology and rollback.'
}

$candidateThumbprint = [string]$manifest.certificate_thumbprint
foreach ($store in @('Root', 'TrustedPublisher')) {
    if (-not (Test-Path -LiteralPath `
            "Cert:\LocalMachine\$store\$candidateThumbprint")) {
        throw "The existing candidate signer is not trusted in LocalMachine\$store."
    }
}

$connectionProbe = Join-Path $candidate.root 'xm5_connection_probe.exe'
$transportProbe = Join-Path $candidate.root 'transport_probe.exe'
$connectionState = @(& $connectionProbe --state 2>&1)
$connectionExit = $LASTEXITCODE
if ($connectionExit -ne 10 -or
    ($connectionState -join "`n") -notmatch
        '(?m)^XM5 Bluetooth state: disconnected\.\s*$') {
    throw 'Keep XM5 powered off before the volume observation gate.'
}
$radioState = @(& $connectionProbe --radio-state 2>&1)
if ($LASTEXITCODE -ne 0 -or
    ($radioState -join "`n") -notmatch
        '(?m)^Bluetooth radio state: ready\.\s*$') {
    throw 'Windows Bluetooth must be on and connectable.'
}
$remoteAddressMatch = [regex]::Match(
    [string]$manifest.expected_parent,
    '(?i)([0-9A-F]{12})_C00000000$')
if (-not $remoteAddressMatch.Success) {
    throw 'The candidate manifest does not expose the exact XM5 address.'
}
$remoteAddress = $remoteAddressMatch.Groups[1].Value
$xm5ProblemDevices = @(Get-PnpDevice -PresentOnly -ErrorAction Stop |
    Where-Object {
        [string]$_.InstanceId -match [regex]::Escape($remoteAddress) -and
        [string]$_.Problem -cne 'CM_PROB_NONE'
    })
if ($xm5ProblemDevices.Count -ne 0) {
    $problem = $xm5ProblemDevices[0]
    throw ('The paired XM5 PnP topology is not healthy: {0}, {1}. ' +
        'Recover this device before the isolated observation.' -f
        [string]$problem.FriendlyName,
        [string]$problem.Problem)
}

$volumeProbe = Join-Path $candidate.root 'endpoint_volume_probe.exe'
$endpointProbe = Join-Path $candidate.root 'audio_endpoint_probe.exe'
$before = Get-V1SwdBindingSnapshot -Manifest $manifest `
    -VolumeProbePath $volumeProbe
if (@($before.candidate_children).Count -ne 0 -or
    @($before.candidate_registered_devices).Count -ne 0 -or
    @($before.candidate_packages).Count -ne 0 -or
    @($before.candidate_mmdevices | Where-Object {
        Test-V1SwdEndpointPublishedState -State ([string]$_.state)
    }).Count -ne 0 -or
    @($before.root_mmdevices).Count -ne 1 -or
    [string]$before.transport.service -cne 'LdacNative' -or
    [int]$before.transport.problem_code -ne 0 -or
    [string]$before.root_endpoint.service -cne 'NativeLdacAudio' -or
    [int]$before.root_endpoint.problem_code -ne 0) {
    throw 'The frozen transport plus ROOT endpoint baseline is not clean.'
}
$streamInfo = @(& $endpointProbe --info 2>&1)
$streamExit = $LASTEXITCODE
$consumerLease = @(& $endpointProbe --consumer-lease 2>&1)
$leaseExit = $LASTEXITCODE
$presence = @(& $endpointProbe --presence 2>&1)
$presenceExit = $LASTEXITCODE
if ($streamExit -ne 0 -or $leaseExit -ne 0 -or $presenceExit -ne 0 -or
    ($streamInfo -join "`n") -notmatch '(?m)^Stream idle[:,]' -or
    ($consumerLease -join "`n") -notmatch
        '(?m)^PCM consumer lease released: generation 0\.$' -or
    ($presence -join "`n") -notmatch '(?m)^Physical presence absent:') {
    throw 'Stop every player and wait for the existing Native endpoint to become absent and idle.'
}

$trialRoot = Join-Path $projectRoot 'artifacts\v1-volume-sync\trial'
$directory = Join-Path $trialRoot `
    ('volume-observation-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$before | ConvertTo-Json -Depth 12 | Set-Content `
    -LiteralPath (Join-Path $directory 'before.json') -Encoding utf8NoBOM

Write-Host 'V1 transport-owned endpoint volume observation preflight passed.'
Write-Host 'Keep XM5 off until the ACL watcher says it is armed.'
Write-Host 'No player, default-endpoint change, endpoint-volume write, AVRCP write, media channel, START, or audio packet is allowed.'
Write-Host 'The isolated package is staged first, but no child or endpoint is created before physical ACL connect.'
Write-Host 'After ACL connect, one DISCOVER/GET_CAPABILITIES-only signaling channel is held open to preserve the real profile session.'
Write-Host "The signaling holder and isolated candidate are bounded to at most $DurationSeconds seconds and are removed automatically."
if (-not $PSCmdlet.ShouldProcess(
        'one isolated XM5 transport-owned endpoint and one physical connection',
        'Stage only the package, prove physical ACL, hold capability-only signaling, activate only the candidate jack, observe public volume changes, release both before XM5 power-off, remove, and verify rollback')) {
    return
}

$publishedInf = ''
$addResult = $null
$deviceRemoveResult = $null
$packageRemoveResult = $null
$process = $null
$stdoutTask = $null
$stderrTask = $null
$hostOutput = ''
$hostError = ''
$hostStarted = $false
$hostCompleted = $false
$hostTimedOut = $false
$hostForced = $false
$hostExit = -1
$stopEvent = $null
$stopEventName = ''
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
$signalingOutPath = Join-Path $directory 'signaling-hold.out.log'
$signalingErrPath = Join-Path $directory 'signaling-hold.err.log'
$during = $null
$after = $null
$operationError = ''
$cleanupErrors = [Collections.Generic.List[string]]::new()
$activationSamples = [Collections.Generic.List[object]]::new()
$samples = [Collections.Generic.List[object]]::new()
$queryFailures = 0
$activationDefaultRoleViolations = 0
$candidateActiveSamples = 0
$candidateDefaultRoleViolations = 0
$observationCompleted = $false
$aclConnectObserved = $false
$aclDisconnectObserved = $false
$transportReleasedBeforePowerOff = $false
$connectCapture = $null
$disconnectCapture = $null
$classification = $null
$observationFailure = ''

try {
    $packageInf = Join-Path $candidate.root `
        'package\NativeLdacSwdAudio.inf'
    $addResult = Invoke-V1SwdPnpUtil -Arguments @('/add-driver', $packageInf)
    $addResult.text | Set-Content `
        -LiteralPath (Join-Path $directory 'pnputil-add.log') `
        -Encoding utf8NoBOM
    if ([int]$addResult.exit_code -ne 0) {
        throw "The isolated package staging failed with exit $($addResult.exit_code)."
    }
    $staged = @(Get-V1SwdCandidatePackages)
    if ($staged.Count -ne 1) {
        throw 'Package staging did not produce exactly one isolated package.'
    }
    $publishedInf = [string]$staged[0].published_inf
    if ($publishedInf -notmatch '^oem\d+\.inf$') {
        throw 'The isolated package did not receive a bounded OEM INF identity.'
    }

    Write-Host 'The ACL watcher is starting. Turn on XM5 only after it reports that it is armed. Do not start audio.'
    $connectCapture = Invoke-V1SwdStreamingProbe `
        -ProbePath $connectionProbe `
        -Arguments @('--wait-acl-connect', '60')
    $connectCapture.text | Set-Content `
        -LiteralPath (Join-Path $directory 'acl-connect.log') `
        -Encoding utf8NoBOM
    if ([int]$connectCapture.exit_code -ne 0) {
        throw 'One physical XM5 ACL connection was not observed; no isolated child or endpoint was created.'
    }
    $aclConnectObserved = $true

    $signalingStopEventName = 'Local\NativeLdacV1SwdSignalingStop-' +
        $PID + '-' + [guid]::NewGuid().ToString('N')
    $signalingCreatedNew = $false
    $signalingStopEvent = [Threading.EventWaitHandle]::new(
        $false,
        [Threading.EventResetMode]::ManualReset,
        $signalingStopEventName,
        [ref]$signalingCreatedNew)
    if (-not $signalingCreatedNew) {
        throw 'The bounded signaling stop event already existed.'
    }
    $signalingArguments = @(
        '--discover',
        '--open-attempts', '1',
        '--hold-signaling-seconds', [string]$DurationSeconds,
        '--stop-event', $signalingStopEventName)
    $signalingProcess = Start-Process `
        -FilePath $transportProbe `
        -ArgumentList $signalingArguments `
        -RedirectStandardOutput $signalingOutPath `
        -RedirectStandardError $signalingErrPath `
        -WindowStyle Hidden `
        -PassThru
    $signalingStarted = $null -ne $signalingProcess
    if (-not $signalingStarted) {
        throw 'The bounded capability-only signaling holder did not start.'
    }
    $signalingDeadline = [DateTime]::UtcNow.AddSeconds(20)
    while ([DateTime]::UtcNow -lt $signalingDeadline) {
        if ($signalingProcess.HasExited) { break }
        if (Test-Path -LiteralPath $signalingOutPath -PathType Leaf) {
            $signalingOutput = Get-Content -LiteralPath $signalingOutPath -Raw
            if ($signalingOutput -match
                '(?m)^Signaling channel hold active for up to \d+ second\(s\)\.\s*$') {
                $signalingReady = $true
                break
            }
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $signalingReady) {
        if (Test-Path -LiteralPath $signalingErrPath -PathType Leaf) {
            $signalingError = Get-Content -LiteralPath $signalingErrPath -Raw
        }
        throw ('The capability-only signaling holder did not reach its bounded hold state. ' +
            $signalingError.Trim())
    }
    Write-Host 'V1 capability-only AVDTP signaling hold is active; no configuration, media channel, START, or media packet was sent.'

    $stopEventName = 'Local\NativeLdacV1SwdVolumeStop-' +
        $PID + '-' + [guid]::NewGuid().ToString('N')
    $createdNew = $false
    $stopEvent = [Threading.EventWaitHandle]::new(
        $false,
        [Threading.EventResetMode]::ManualReset,
        $stopEventName,
        [ref]$createdNew)
    if (-not $createdNew) {
        throw 'The bounded candidate stop event already existed.'
    }

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = Join-Path $candidate.root `
        'v1_swd_endpoint_host.exe'
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.StandardOutputEncoding = [Text.Encoding]::UTF8
    $startInfo.StandardErrorEncoding = [Text.Encoding]::UTF8
    foreach ($argument in @(
            '--create',
            '--parent', [string]$manifest.expected_parent,
            '--container', [string]$manifest.remote_container_id,
            '--duration-seconds', [string]$DurationSeconds,
            '--confirm-endpoint-binding-probe',
            '--publish-presence',
            '--confirm-volume-observation',
            '--stop-event', $stopEventName)) {
        $startInfo.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $hostStarted = $process.Start()
    if (-not $hostStarted) {
        throw 'The bounded endpoint host did not start.'
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()

    $activationTimer = [Diagnostics.Stopwatch]::StartNew()
    while ($activationTimer.Elapsed.TotalSeconds -lt 30) {
        if ($signalingProcess.HasExited) {
            $signalingExit = $signalingProcess.ExitCode
            throw "The capability-only signaling holder exited before candidate activation (exit $signalingExit)."
        }
        $endpointState = Get-V1SwdEndpointObservationSnapshot `
            -ProbePath $volumeProbe
        $candidateStates = @($endpointState.endpoints |
            Where-Object {
                Test-V1SwdEndpointNameContains `
                    -Name ([string]$_.name) `
                    -Marker $script:V1SwdEndpointBindingNameMarker
            })
        $activationSamples.Add([pscustomobject][ordered]@{
            elapsed_ms = [long]$activationTimer.ElapsedMilliseconds
            captured_at = (Get-Date).ToString('o')
            candidates = @($candidateStates | Select-Object `
                name, state, id, container_id, default_roles, `
                volume_available, volume_percent, step_available, `
                step_index, step_count)
        })
        $publishedCandidate = @($candidateStates | Where-Object {
            [string]$_.state -ceq 'active' -and
            $_.volume_available -eq $true
        })
        $activeDefaultCandidates = @($candidateStates | Where-Object {
            [string]$_.state -ceq 'active' -and
            [string]$_.default_roles -cne '(none)'
        })
        if ($activeDefaultCandidates.Count -ne 0) {
            $activationDefaultRoleViolations +=
                $activeDefaultCandidates.Count
            throw 'Windows assigned default roles to the isolated candidate; rollback was performed.'
        }
        if ($publishedCandidate.Count -eq 1 -and
            [string]$publishedCandidate[0].default_roles -ceq '(none)') {
            break
        }
        Start-Sleep -Milliseconds 250
    }
    $activationTimer.Stop()
    if ($publishedCandidate.Count -ne 1) {
        throw 'The exact candidate endpoint did not become active within 30 seconds.'
    }
    $during = Get-V1SwdBindingSnapshot -Manifest $manifest `
        -VolumeProbePath $volumeProbe
    $duringPublishedCandidate = @($during.candidate_mmdevices |
        Where-Object {
            [string]$_.state -ceq 'active' -and
            $_.volume_available -eq $true
        })
    if (@($during.candidate_children).Count -ne 1 -or
        $duringPublishedCandidate.Count -ne 1 -or
        [string]$duringPublishedCandidate[0].default_roles -cne '(none)') {
        throw 'The candidate endpoint changed before the full during snapshot completed.'
    }
    $during | ConvertTo-Json -Depth 12 | Set-Content `
        -LiteralPath (Join-Path $directory 'during.json') `
        -Encoding utf8NoBOM

    $connectedState = @(& $connectionProbe --state 2>&1)
    if ($LASTEXITCODE -ne 0 -or
        ($connectedState -join "`n") -notmatch
            '(?m)^XM5 Bluetooth state: connected\.\s*$') {
        throw 'The physical XM5 ACL disconnected while the isolated endpoint was being activated.'
    }

    Write-Host "For the next $ObservationSeconds seconds, swipe XM5 volume up at least three times, then down at least three times."
    Write-Host 'Do not use the PC volume control and do not start playback.'
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $lastRootSignature = ''
    $lastCandidateSignature = ''
    while ($timer.Elapsed.TotalSeconds -lt $ObservationSeconds) {
        if ($signalingProcess.HasExited) {
            $signalingExit = $signalingProcess.ExitCode
            $observationFailure =
                "The capability-only signaling holder exited during observation (exit $signalingExit)."
            break
        }
        if ($process.HasExited) {
            $hostExit = $process.ExitCode
            $observationFailure =
                "The candidate endpoint host exited during observation (exit $hostExit); the Bluetooth radio or XM5 parent topology was invalidated."
            break
        }
        try {
            $endpointSnapshot = Get-V1SwdEndpointObservationSnapshot `
                -ProbePath $volumeProbe
            $pair = Get-V1SwdVolumePair -Manifest $manifest `
                -Endpoints $endpointSnapshot.endpoints
            if ($pair.root_count -ne 1 -or
                $pair.candidate_count -ne 1 -or
                $null -eq $pair.root -or
                $null -eq $pair.candidate -or
                $pair.root.volume_available -ne $true -or
                $pair.candidate.volume_available -ne $true) {
                $queryFailures++
                $observationFailure =
                    'The ROOT/candidate endpoint pair became unavailable during observation.'
                break
            }
            if ([string]$pair.candidate.state -ceq 'active') {
                $candidateActiveSamples++
            }
            if ([string]$pair.candidate.default_roles -cne '(none)') {
                $candidateDefaultRoleViolations++
            }
            $sample = [pscustomobject][ordered]@{
                elapsed_ms = [long]$timer.ElapsedMilliseconds
                captured_at = (Get-Date).ToString('o')
                root = $pair.root
                candidate = $pair.candidate
            }
            $samples.Add($sample)
            $rootSignature = '{0:F3}|{1}|{2}' -f
                [double]$pair.root.volume_percent,
                [bool]$pair.root.muted,
                [int]$pair.root.step_index
            $candidateSignature = '{0:F3}|{1}|{2}' -f
                [double]$pair.candidate.volume_percent,
                [bool]$pair.candidate.muted,
                [int]$pair.candidate.step_index
            if ($rootSignature -cne $lastRootSignature -or
                $candidateSignature -cne $lastCandidateSignature) {
                Write-Host ('V1 volume sample +{0} ms: ROOT {1:F1}% step {2}; candidate {3:F1}% step {4}.' -f
                    [long]$timer.ElapsedMilliseconds,
                    [double]$pair.root.volume_percent,
                    [int]$pair.root.step_index,
                    [double]$pair.candidate.volume_percent,
                    [int]$pair.candidate.step_index)
                $lastRootSignature = $rootSignature
                $lastCandidateSignature = $candidateSignature
            }
        } catch {
            $queryFailures++
            $observationFailure =
                'The endpoint-volume query failed during observation: ' +
                $_.Exception.Message
            break
        }
        Start-Sleep -Milliseconds 250
    }
    $timer.Stop()
    $observationCompleted =
        $timer.Elapsed.TotalSeconds -ge ($ObservationSeconds - 1) -and
        $samples.Count -ge 10 -and
        $queryFailures -eq 0
    $classification = Get-V1SwdVolumeObservationClassification `
        -Samples @($samples)
    ConvertTo-Json -InputObject @($samples) -Depth 10 | Set-Content `
        -LiteralPath (Join-Path $directory 'volume-samples.json') `
        -Encoding utf8NoBOM
    Write-Host "V1 public volume observation: $($classification.classification)."
    if (-not $observationCompleted) {
        if ($signalingProcess.HasExited) {
            $signalingExit = $signalingProcess.ExitCode
            throw "The capability-only signaling holder exited during observation (exit $signalingExit)."
        }
        if ($process.HasExited) {
            $hostExit = $process.ExitCode
            throw "The candidate endpoint host exited during observation (exit $hostExit); do not toggle Windows Bluetooth or invalidate the XM5 parent while the gate is running."
        }
        if (-not [string]::IsNullOrWhiteSpace($observationFailure)) {
            throw $observationFailure
        }
        throw 'The bounded endpoint volume observation did not complete cleanly.'
    }

    $connectedState = @(& $connectionProbe --state 2>&1)
    if ($LASTEXITCODE -ne 0 -or
        ($connectedState -join "`n") -notmatch
            '(?m)^XM5 Bluetooth state: connected\.\s*$') {
        throw 'The physical XM5 ACL disconnected before the clean transport release.'
    }

    Write-Host 'Volume observation is complete. Keep XM5 on while the isolated endpoint and signaling channel are released.'

    if ($null -ne $stopEvent) {
        [void]$stopEvent.Set()
    }
    $hostCompleted = $process.WaitForExit(30000)
    if (-not $hostCompleted) {
        $hostTimedOut = $true
        throw 'The candidate host did not stop after its bounded stop event.'
    }
    $hostExit = $process.ExitCode
    if ($hostExit -ne 0) {
        throw "The candidate host failed with exit $hostExit."
    }

    if ($null -ne $signalingStopEvent) {
        [void]$signalingStopEvent.Set()
    }
    if (-not $signalingProcess.HasExited) {
        $signalingCompleted = $signalingProcess.WaitForExit(30000)
    } else {
        $signalingCompleted = $true
    }
    if (-not $signalingCompleted) {
        $signalingTimedOut = $true
        throw 'The capability-only signaling holder did not stop after its bounded stop event.'
    }
    $signalingExit = $signalingProcess.ExitCode
    if ($signalingExit -ne 0) {
        throw "The capability-only signaling holder failed with exit $signalingExit."
    }
    $transportReleasedBeforePowerOff = $true
    Write-Host 'V1 isolated endpoint and capability-only signaling were released cleanly while XM5 remained on.'
} catch {
    $operationError = $_.Exception.Message
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
            if ($signalingProcess.HasExited) {
                $signalingCompleted = -not $signalingForced
                $signalingExit = $signalingProcess.ExitCode
            }
        }
        if (Test-Path -LiteralPath $signalingOutPath -PathType Leaf) {
            $signalingOutput = Get-Content -LiteralPath $signalingOutPath -Raw
        }
        if (Test-Path -LiteralPath $signalingErrPath -PathType Leaf) {
            $signalingError = Get-Content -LiteralPath $signalingErrPath -Raw
        }
    } catch {
        $cleanupErrors.Add('signaling-holder cleanup: ' + $_.Exception.Message)
    } finally {
        if ($null -ne $signalingProcess) {
            $signalingProcess.Dispose()
        }
        if ($null -ne $signalingStopEvent) {
            $signalingStopEvent.Dispose()
        }
    }
    try {
        if ($null -ne $stopEvent) {
            [void]$stopEvent.Set()
        }
        if ($null -ne $process) {
            if (-not $process.HasExited) {
                if (-not $process.WaitForExit(15000)) {
                    $hostForced = $true
                    $process.Kill($true)
                    $process.WaitForExit()
                }
            }
            if ($null -ne $stdoutTask) {
                $hostOutput = $stdoutTask.GetAwaiter().GetResult()
            }
            if ($null -ne $stderrTask) {
                $hostError = $stderrTask.GetAwaiter().GetResult()
            }
            if ($process.HasExited) {
                $hostCompleted = -not $hostForced
                $hostExit = $process.ExitCode
            }
        }
    } catch {
        $cleanupErrors.Add('host cleanup: ' + $_.Exception.Message)
    } finally {
        if ($null -ne $process) {
            $process.Dispose()
        }
        if ($null -ne $stopEvent) {
            $stopEvent.Dispose()
        }
    }
    try {
        $hostOutput | Set-Content `
            -LiteralPath (Join-Path $directory 'endpoint-host.out.log') `
            -Encoding utf8NoBOM
        $hostError | Set-Content `
            -LiteralPath (Join-Path $directory 'endpoint-host.err.log') `
            -Encoding utf8NoBOM
    } catch {
        $cleanupErrors.Add('host log capture: ' + $_.Exception.Message)
    }

    try {
        $childDeadline = [DateTime]::UtcNow.AddSeconds(10)
        while ([DateTime]::UtcNow -lt $childDeadline -and
            @(Get-V1SwdCandidateChildren).Count -ne 0) {
            Start-Sleep -Milliseconds 200
        }
    } catch {
        $cleanupErrors.Add('child rundown query: ' + $_.Exception.Message)
    }
    try {
        $registered = @(Get-V1SwdCandidateRegisteredDevices)
        if ($registered.Count -gt 1) {
            throw 'More than one registered isolated SWD endpoint exists.'
        }
        if ($registered.Count -eq 1) {
            if (-not (Test-V1SwdExactRegisteredCandidate `
                    -Device $registered[0] -Manifest $manifest `
                    -PublishedInf $publishedInf)) {
                throw 'The registered SWD endpoint does not match the exact isolated identity.'
            }
            $deviceRemoveResult = Invoke-V1SwdPnpUtil -Arguments @(
                '/remove-device',
                $script:V1SwdEndpointBindingInstanceId)
            $deviceRemoveResult.text | Set-Content `
                -LiteralPath (Join-Path $directory `
                    'pnputil-remove-device.log') -Encoding utf8NoBOM
            if ([int]$deviceRemoveResult.exit_code -ne 0) {
                throw "The isolated device removal failed with exit $($deviceRemoveResult.exit_code)."
            }
        }
        $deviceDeadline = [DateTime]::UtcNow.AddSeconds(10)
        while ([DateTime]::UtcNow -lt $deviceDeadline -and
            @(Get-V1SwdCandidateRegisteredDevices).Count -ne 0) {
            Start-Sleep -Milliseconds 200
        }
    } catch {
        $cleanupErrors.Add('device-instance rollback: ' + $_.Exception.Message)
    }
    try {
        if ([string]::IsNullOrWhiteSpace($publishedInf)) {
            $rollbackPackages = @(Get-V1SwdCandidatePackages)
            if ($rollbackPackages.Count -eq 1) {
                $publishedInf = [string]$rollbackPackages[0].published_inf
            }
        }
        if (-not [string]::IsNullOrWhiteSpace($publishedInf)) {
            $packageRemoveResult = Invoke-V1SwdPnpUtil -Arguments @(
                '/delete-driver', $publishedInf, '/force')
            $packageRemoveResult.text | Set-Content `
                -LiteralPath (Join-Path $directory `
                    'pnputil-remove-package.log') -Encoding utf8NoBOM
            if ([int]$packageRemoveResult.exit_code -ne 0) {
                throw "The isolated package removal failed with exit $($packageRemoveResult.exit_code)."
            }
        }
        $packageDeadline = [DateTime]::UtcNow.AddSeconds(10)
        while ([DateTime]::UtcNow -lt $packageDeadline -and
            @(Get-V1SwdCandidatePackages).Count -ne 0) {
            Start-Sleep -Milliseconds 200
        }
    } catch {
        $cleanupErrors.Add('package rollback: ' + $_.Exception.Message)
    }
    try {
        $endpointDeadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            $endpointState = Get-V1SwdEndpointObservationSnapshot `
                -ProbePath $volumeProbe
            $publishedCandidate = @($endpointState.endpoints |
                Where-Object {
                    (Test-V1SwdEndpointNameContains `
                        -Name ([string]$_.name) `
                        -Marker $script:V1SwdEndpointBindingNameMarker) -and
                    (Test-V1SwdEndpointPublishedState `
                        -State ([string]$_.state))
                })
            if ($publishedCandidate.Count -eq 0) { break }
            Start-Sleep -Milliseconds 250
        } while ([DateTime]::UtcNow -lt $endpointDeadline)
        $after = Get-V1SwdBindingSnapshot -Manifest $manifest `
            -VolumeProbePath $volumeProbe
    } catch {
        $cleanupErrors.Add('final snapshot: ' + $_.Exception.Message)
    }
}

if ($null -eq $during) {
    $during = $before
}
$during | ConvertTo-Json -Depth 12 | Set-Content `
    -LiteralPath (Join-Path $directory 'during.json') -Encoding utf8NoBOM
$afterCaptured = $null -ne $after
if (-not $afterCaptured) {
    $after = [pscustomobject]@{
        capture_failed = $true
        errors = @($cleanupErrors)
        candidate_children = @()
        candidate_registered_devices = @()
        candidate_packages = @()
        candidate_mmdevices = @()
    }
}
$after | ConvertTo-Json -Depth 12 | Set-Content `
    -LiteralPath (Join-Path $directory 'after.json') -Encoding utf8NoBOM
ConvertTo-Json -InputObject @($activationSamples) -Depth 10 | Set-Content `
    -LiteralPath (Join-Path $directory 'candidate-activation-samples.json') `
    -Encoding utf8NoBOM
ConvertTo-Json -InputObject @($samples) -Depth 10 | Set-Content `
    -LiteralPath (Join-Path $directory 'volume-samples.json') `
    -Encoding utf8NoBOM
if ($null -eq $classification) {
    $classification = Get-V1SwdVolumeObservationClassification `
        -Samples @($samples)
}
$observation = [pscustomobject][ordered]@{
    completed = $observationCompleted
    observation_seconds = $ObservationSeconds
    sample_count = $samples.Count
    query_failures = $queryFailures
    candidate_active_samples = $candidateActiveSamples
    candidate_default_role_violations = $candidateDefaultRoleViolations
    classification = [string]$classification.classification
    candidate_volume_changed =
        [bool]$classification.candidate_volume_changed
    root_volume_changed = [bool]$classification.root_volume_changed
    candidate_distinct_values =
        [int]$classification.candidate_distinct_values
    root_distinct_values = [int]$classification.root_distinct_values
}
$activation = [pscustomobject][ordered]@{
    sample_count = $activationSamples.Count
    default_role_violations = $activationDefaultRoleViolations
    candidate_became_active = @($activationSamples | Where-Object {
        @($_.candidates | Where-Object {
            [string]$_.state -ceq 'active'
        }).Count -ne 0
    }).Count -ne 0
}
$hostProcess = [pscustomobject][ordered]@{
    started = $hostStarted
    completed = $hostCompleted
    timed_out = $hostTimedOut
    forced_termination = $hostForced
    stop_event_observed = $hostOutput -match
        '(?m)^SWD endpoint candidate stop event observed\.\s*$'
    exit_code = $hostExit
}
$signalingProcessEvidence = [pscustomobject][ordered]@{
    started = $signalingStarted
    ready = $signalingReady
    completed = $signalingCompleted
    timed_out = $signalingTimedOut
    forced_termination = $signalingForced
    capability_discovery_completed = $signalingOutput -match
        '(?m)^Selected LDAC audio sink SEID: \d+\s*$'
    hold_started = $signalingOutput -match
        '(?m)^Signaling channel hold active for up to \d+ second\(s\)\.\s*$'
    stop_event_observed = $signalingOutput -match
        '(?m)^Signaling channel hold stop event observed\.\s*$'
    channel_closed = $signalingOutput -match
        '(?m)^Signaling channel closed\.\s*$'
    exit_code = $signalingExit
}
$acl = [pscustomobject][ordered]@{
    connect_observed = $aclConnectObserved
    disconnect_observed = $aclDisconnectObserved
    disconnect_required = $false
    transport_released_before_power_off =
        $transportReleasedBeforePowerOff
    connect_exit_code = if ($null -eq $connectCapture) {
        $null
    } else {
        [int]$connectCapture.exit_code
    }
    disconnect_exit_code = if ($null -eq $disconnectCapture) {
        $null
    } else {
        [int]$disconnectCapture.exit_code
    }
}
$rollback = [pscustomobject][ordered]@{
    device_remove_attempted = $null -ne $deviceRemoveResult
    device_remove_exit_code = if ($null -eq $deviceRemoveResult) {
        $null
    } else {
        [int]$deviceRemoveResult.exit_code
    }
    device_instance_absent = $afterCaptured -and
        @($after.candidate_registered_devices).Count -eq 0
    package_remove_attempted = $null -ne $packageRemoveResult
    package_remove_exit_code = if ($null -eq $packageRemoveResult) {
        $null
    } else {
        [int]$packageRemoveResult.exit_code
    }
    package_remove_succeeded = $null -ne $packageRemoveResult -and
        [int]$packageRemoveResult.exit_code -eq 0 -and
        $afterCaptured -and @($after.candidate_packages).Count -eq 0
    published_candidate_endpoint_absent = $afterCaptured -and @(
        $after.candidate_mmdevices | Where-Object {
            Test-V1SwdEndpointPublishedState -State ([string]$_.state)
        }).Count -eq 0
    cleanup_errors = @($cleanupErrors)
}
$evidencePassed = $false
if ($afterCaptured -and $cleanupErrors.Count -eq 0) {
    $evidencePassed = Test-V1SwdVolumeObservationEvidence `
        -Before $before -During $during -After $after `
        -Observation $observation -HostProcess $hostProcess `
        -SignalingProcess $signalingProcessEvidence -Acl $acl `
        -Rollback $rollback `
        -ExpectedParent ([string]$manifest.expected_parent) `
        -ExpectedContainer ([string]$manifest.remote_container_id)
}
$passed = [string]::IsNullOrWhiteSpace($operationError) -and
    $evidencePassed
$defaultEndpointRolesPreserved = $afterCaptured -and
    $activationDefaultRoleViolations -eq 0 -and
    (Test-V1SwdDefaultEndpointRolesStable `
        -Before $before -During $during -After $after)
$rootEndpointPreserved = $afterCaptured -and
    (Test-V1SwdEndpointStableDevice `
        -Before $before.transport -During $during.transport `
        -After $after.transport -ExpectedService 'LdacNative') -and
    (Test-V1SwdEndpointStableDevice `
        -Before $before.root_endpoint -During $during.root_endpoint `
        -After $after.root_endpoint -ExpectedService 'NativeLdacAudio') -and
    (Test-V1SwdVolumeRootMmIdentity `
        -Before $before.root_mmdevice -During $during.root_mmdevice `
        -After $after.root_mmdevice)
$result = [ordered]@{
    schema_version = 1
    policy_version = $script:V1SwdVolumeObservationPolicyVersion
    passed = $passed
    source_commit = [string]$manifest.source_commit
    candidate = $candidate.root
    binding_prerequisite = $BindingResultPath
    golden_checkpoint = [IO.Path]::GetFullPath($GoldenCheckpointPath)
    duration_seconds = $DurationSeconds
    expected_parent = [string]$manifest.expected_parent
    expected_container = [string]$manifest.remote_container_id
    staged_published_inf = $publishedInf
    before = 'before.json'
    during = 'during.json'
    activation_samples = 'candidate-activation-samples.json'
    samples = 'volume-samples.json'
    after = 'after.json'
    activation = $activation
    observation = $observation
    host_process = $hostProcess
    signaling_process = $signalingProcessEvidence
    signaling_stdout = 'signaling-hold.out.log'
    signaling_stderr = 'signaling-hold.err.log'
    acl = $acl
    rollback = $rollback
    synchronization_proven = $false
    error = $operationError
    safety = [ordered]@{
        current_root_endpoint_preserved = $rootEndpointPreserved
        default_endpoint_roles_preserved =
            $defaultEndpointRolesPreserved
        default_endpoint_written = $false
        endpoint_volume_written = $false
        avrcp_written = $false
        capability_only_signaling = $true
        set_configuration_sent = $false
        media_channel_opened = $false
        avdtp_start_sent = $false
        media_packet_sent = $false
        certificate_imported = $false
        pnp_restarted = $false
        bluetooth_toggled = $false
        audio_playback_started = $false
        physical_power_off_requested = $false
        transport_released_before_power_off =
            $transportReleasedBeforePowerOff
        isolated_presence_only = $true
        isolated_device_instance_removed =
            [bool]$rollback.device_instance_absent
        isolated_package_removed =
            [bool]$rollback.package_remove_succeeded
    }
}
$resultPath = Join-Path $directory 'result.json'
$result | ConvertTo-Json -Depth 12 | Set-Content `
    -LiteralPath $resultPath -Encoding utf8NoBOM
if (-not $passed) {
    $reason = if ([string]::IsNullOrWhiteSpace($operationError)) {
        'The volume-observation evidence contract failed.'
    } else {
        $operationError
    }
    throw "V1 transport-owned endpoint volume observation failed: $reason Result: $resultPath"
}

Write-Host 'V1 transport-owned endpoint volume observation gate passed.'
Write-Host "Public endpoint result: $($observation.classification)."
Write-Host 'The result records observation only; synchronization_proven remains false until the evidence is reviewed.'
Write-Host 'The exact child and isolated package were removed; the existing ROOT endpoint and audio path were untouched.'
Write-Host 'The signaling channel was released before power-off. You may turn off XM5 normally now.'
Write-Host "Result: $resultPath"
