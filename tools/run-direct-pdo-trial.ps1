# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmDirectPdoTrial,
    [ValidateRange(30, 180)]
    [int]$DurationSeconds = 60,
    [ValidateSet('mq', 'sq', 'hq', 'auto')]
    [string]$Quality = 'hq',
    [switch]$RequireCrashRecovery,
    [switch]$ColdConnect,
    [string]$BundlePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script from an elevated Windows PowerShell.'
    }
}

function Wait-Until {
    param(
        [Parameter(Mandatory = $true)]
        [scriptblock]$Condition,
        [Parameter(Mandatory = $true)]
        [int]$TimeoutSeconds,
        [int]$PollMilliseconds = 200
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (& $Condition) {
            return $true
        }
        Start-Sleep -Milliseconds $PollMilliseconds
    }
    return $false
}

function New-TrialPaths {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    return [pscustomobject]@{
        Suffix = "direct-$Label-$timestamp"
        AgentLog = Join-Path $Root "agent-$Label-$timestamp.log"
        EngineLog = Join-Path $Root "engine-$Label-$timestamp.log"
        State = Join-Path $Root "state-$Label-$timestamp.json"
    }
}

function Start-TrialAgent {
    param(
        [Parameter(Mandatory = $true)][string]$AgentPath,
        [Parameter(Mandatory = $true)]$Paths,
        [Parameter(Mandatory = $true)][int]$RunMilliseconds,
        [Parameter(Mandatory = $true)][string]$TrialQuality
    )

    $quotedAgentLog = '"' + $Paths.AgentLog + '"'
    $quotedEngineLog = '"' + $Paths.EngineLog + '"'
    $quotedState = '"' + $Paths.State + '"'
    $arguments = "--direct-pdo-trial --quality $TrialQuality " +
        "--channel-mode stereo --run-for-ms $RunMilliseconds " +
        "--instance-suffix $($Paths.Suffix) --log $quotedAgentLog " +
        "--probe-log $quotedEngineLog --state $quotedState"
    return Start-Process -FilePath $AgentPath `
        -ArgumentList $arguments `
        -WindowStyle Hidden `
        -PassThru
}

function Stop-TrialAgent {
    param(
        [Parameter(Mandatory = $true)][string]$AgentPath,
        [Parameter(Mandatory = $true)]$Paths,
        [Parameter(Mandatory = $true)]$Process
    )

    if ($Process.HasExited) {
        return
    }
    $stopArguments = "--stop --instance-suffix $($Paths.Suffix)"
    $stopProcess = Start-Process -FilePath $AgentPath `
        -ArgumentList $stopArguments `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($stopProcess.ExitCode -ne 0) {
        throw "The direct trial stop request returned $($stopProcess.ExitCode)."
    }
    if (-not $Process.WaitForExit(30000)) {
        throw 'The direct trial agent did not stop within 30 seconds. It was not forcibly terminated.'
    }
}

function Test-EngineEvidence {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }
    $output = Get-Content -LiteralPath $Path -Raw
    return $output.Contains('Direct-PDO LDAC engine started:') -and
        $output.Contains('Direct:')
}

function Get-StateObject {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

Assert-Administrator
if (-not $ConfirmDirectPdoTrial) {
    throw 'Refusing to start a Direct-PDO hardware trial. Re-run with -ConfirmDirectPdoTrial.'
}
if ($RequireCrashRecovery -and $DurationSeconds -lt 90) {
    throw 'A Direct-PDO crash-recovery trial requires at least 90 seconds.'
}
if ($ColdConnect -and $RequireCrashRecovery) {
    throw 'Cold-connect and crash-recovery are separate bounded trials.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'direct-pdo-install-common.ps1')
Assert-DirectPdoHardwareTestsEnabled -ProjectRoot $projectRoot
if ([string]::IsNullOrWhiteSpace($BundlePath)) {
    $BundlePath = Join-Path $projectRoot `
        'artifacts\direct-pdo\candidate'
}
$BundlePath = [System.IO.Path]::GetFullPath($BundlePath)
$manifestPath = Join-Path $BundlePath 'manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($ColdConnect) {
    if ((Get-DirectPdoArtifactKind -Manifest $manifest) -ne 'candidate') {
        throw 'Cold-connect trial requires the signed Direct-PDO candidate bundle.'
    }
    $verifyScript = Join-Path $PSScriptRoot `
        'verify-direct-pdo-candidate.ps1'
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File $verifyScript `
        -CandidatePath $BundlePath
    if ($LASTEXITCODE -ne 0) {
        throw "Direct-PDO candidate verification failed with exit code $LASTEXITCODE."
    }
    if ($manifest.source_dirty -eq $true) {
        throw 'The candidate was built from a dirty Git worktree.'
    }
    $latestTransactionPath = Join-Path $projectRoot `
        'artifacts\direct-pdo\install\latest-transaction.txt'
    if (-not (Test-Path -LiteralPath $latestTransactionPath -PathType Leaf)) {
        throw 'No committed Direct-PDO installation transaction was recorded.'
    }
    $transactionPath = (Get-Content -LiteralPath `
        $latestTransactionPath -Raw).Trim()
    $transactionRecord = Read-DirectPdoTransaction -Path $transactionPath
    $transaction = $transactionRecord.transaction
    if ([string]$transaction.status -ne 'committed' -or
        [string]$transaction.candidate.service -ne 'NativeLdacDirectPdo' -or
        [string]::IsNullOrWhiteSpace(
            [string]$transaction.candidate.published_inf)) {
        throw 'The latest Direct-PDO transaction is not a committed NativeLdacDirectPdo binding.'
    }
    $presentXm5Pdos = @(Get-Xm5A2dpDevice)
    if ($presentXm5Pdos.Count -ne 0) {
        throw 'Turn off the XM5 and wait for its Bluetooth A2DP PDO to disappear before starting a cold-connect trial.'
    }
    Write-Host 'Direct-PDO cold-connect readiness preflight passed.'
    Write-Host "Recorded binding: $($transaction.candidate.published_inf), service NativeLdacDirectPdo."
    Write-Host 'XM5 A2DP PDO is absent; the bounded agent can start before the headset connects.'
} else {
    $readinessScript = Join-Path $PSScriptRoot `
        'test-direct-pdo-runtime-readiness.ps1'
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File $readinessScript `
        -BundlePath $BundlePath -AllowRecoverableMediaTimeout
    if ($LASTEXITCODE -ne 0) {
        throw "Direct-PDO runtime preflight failed with exit code $LASTEXITCODE."
    }
}

$agentPath = Join-Path $BundlePath 'ldac_agent.exe'
$statusProbe = Join-Path $BundlePath 'audio_endpoint_probe.exe'
$routeProbe = Join-Path $BundlePath 'endpoint_volume_probe.exe'
if (-not (Test-Path -LiteralPath $routeProbe -PathType Leaf)) {
    throw "The verified Direct-PDO bundle has no endpoint route probe: $routeProbe"
}
$task = Get-ScheduledTask -TaskName 'Native LDAC Agent' `
    -ErrorAction SilentlyContinue
if ($task) {
    throw 'The login agent task is installed. Remove it before an isolated Direct-PDO trial.'
}
$conflictingProcesses = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @(
        'ldac_agent.exe',
        'ldac_direct_engine.exe',
        'transport_probe.exe',
        'audio_endpoint_probe.exe'
    )
})
if ($conflictingProcesses.Count -ne 0) {
    $summary = @($conflictingProcesses | ForEach-Object {
        "$($_.Name) (PID $($_.ProcessId))"
    })
    throw "Stop the existing LDAC agent, UI, or probe first: $($summary -join ', ')"
}

if (-not $ColdConnect) {
    $directStatusLines = @(& $statusProbe --direct-status 2>&1)
    $directStatusExitCode = $LASTEXITCODE
    $directStatusText = $directStatusLines -join [Environment]::NewLine
    if ($directStatusExitCode -ne 0) {
        throw "Could not verify the Direct-PDO PCM state (probe exit $directStatusExitCode).`n$directStatusText"
    }
    if ($directStatusText -match '(?m)^Stream active(?:,|:)') {
        throw @"
An existing WaveRT RUN session is already active before the isolated trial agent starts.
Fully stop or close every application or browser tab playing to Native LDAC, wait a few seconds, and run this command again.
The preflight must report 'Stream idle' before confirmation; start playback only after the trial prints 'Starting a bounded Direct-PDO trial'.
No recovery or Bluetooth request was submitted.
"@
    }
}

if (-not $ColdConnect) {
    $routeLines = @(& $routeProbe --verify-direct-route 2>&1)
    $routeExitCode = $LASTEXITCODE
    $routeText = $routeLines -join [Environment]::NewLine
    if ($routeExitCode -ne 0) {
        throw "Direct-PDO audio routing is not ready (probe exit $routeExitCode). No agent or Bluetooth session was started.`n$routeText"
    }
    Write-Host $routeText
}

$trialRoot = Join-Path $projectRoot 'artifacts\direct-pdo-trial'
New-Item -ItemType Directory -Path $trialRoot -Force | Out-Null
$primaryPaths = New-TrialPaths -Root $trialRoot -Label 'primary'
$recoveryPaths = $null
$primaryProcess = $null
$recoveryProcess = $null
$crashIssued = $false
$ownedEnginePid = 0
$faultStatus = ''
$target = if ($ColdConnect) {
    "the staged Direct-PDO agent before an XM5 cold connection for $DurationSeconds seconds"
} else {
    "the coordinated Direct-PDO bundle and XM5 for $DurationSeconds seconds"
}
$action = if ($RequireCrashRecovery) {
    'Run a bounded session, terminate only its owned trial agent, verify media-timeout, then run one bounded recovery agent'
} elseif ($ColdConnect) {
    'Start one bounded agent while XM5 is off, then observe a fresh device connection and media session'
} else {
    'Run one bounded Direct-PDO session and stop it gracefully'
}
if (-not $PSCmdlet.ShouldProcess($target, $action)) {
    return
}

Write-Host "Starting a bounded Direct-PDO trial for $DurationSeconds seconds."
$primaryProcess = Start-TrialAgent `
    -AgentPath $agentPath `
    -Paths $primaryPaths `
    -RunMilliseconds ($DurationSeconds * 1000) `
    -TrialQuality $Quality
if ($ColdConnect) {
    Write-Host 'The trial agent is running. Turn on the XM5 now; after Windows connects, select Native LDAC and start a looping audio source.'
} else {
    Write-Host 'The trial agent is running. Now start a looping system audio source to create a fresh WaveRT RUN session.'
}

try {
    $primaryStarted = Wait-Until -TimeoutSeconds 60 -Condition {
        if ($primaryProcess.HasExited) {
            return $false
        }
        return Test-EngineEvidence -Path $primaryPaths.EngineLog
    }
    if (-not $primaryStarted) {
        throw "Direct-PDO media did not start within 60 seconds. Keep audio playing and inspect $($primaryPaths.AgentLog)."
    }

    if ($ColdConnect) {
        $routeLines = @(& $routeProbe --verify-direct-route 2>&1)
        $routeExitCode = $LASTEXITCODE
        $routeText = $routeLines -join [Environment]::NewLine
        if ($routeExitCode -ne 0) {
            throw "Direct media started after cold connection, but the endpoint is not the console/multimedia default output.`n$routeText"
        }
        Write-Host $routeText
    }

    if ($RequireCrashRecovery) {
        $primaryState = Get-StateObject -Path $primaryPaths.State
        if ($null -eq $primaryState -or [int]$primaryState.probe_pid -le 0) {
            throw 'The primary trial did not publish its owned Direct engine PID.'
        }
        $ownedEnginePid = [int]$primaryState.probe_pid
        Write-Host "Direct media is active. Terminating only trial agent PID $($primaryProcess.Id); engine PID $ownedEnginePid is contained by its Job Object."
        Stop-Process -Id $primaryProcess.Id -Force
        $primaryProcess.WaitForExit()
        $crashIssued = $true

        $engineContained = Wait-Until -TimeoutSeconds 5 -Condition {
            $null -eq (Get-Process -Id $ownedEnginePid `
                -ErrorAction SilentlyContinue)
        }
        if (-not $engineContained) {
            throw "Owned Direct engine PID $ownedEnginePid survived its Job Object."
        }

        $faultObserved = Wait-Until -TimeoutSeconds 12 `
            -PollMilliseconds 250 -Condition {
            $script:faultStatus = @(& $statusProbe --direct-status 2>&1) -join `
                [Environment]::NewLine
            return $LASTEXITCODE -eq 0 -and
                $script:faultStatus -match `
                    '(?m)^Direct-PDO Media ABI 3: faulted ' -and
                $script:faultStatus -match `
                    '(?m)^Failure: media-timeout \(2\),'
        }
        if (-not $faultObserved) {
            throw "The driver did not publish a media-timeout fault after containment.`n$faultStatus"
        }

        $recoveryPaths = New-TrialPaths -Root $trialRoot -Label 'recovery'
        $recoveryProcess = Start-TrialAgent `
            -AgentPath $agentPath `
            -Paths $recoveryPaths `
            -RunMilliseconds ($DurationSeconds * 1000) `
            -TrialQuality $Quality
        $recovered = Wait-Until -TimeoutSeconds 60 -Condition {
            if ($recoveryProcess.HasExited) {
                return $false
            }
            if (-not (Test-Path -LiteralPath $recoveryPaths.AgentLog)) {
                return $false
            }
            $agentOutput = Get-Content -LiteralPath `
                $recoveryPaths.AgentLog -Raw
            return $agentOutput.Contains(
                    'Direct-PDO idle recovery accepted for fault generation') -and
                (Test-EngineEvidence -Path $recoveryPaths.EngineLog)
        }
        if (-not $recovered) {
            throw "The second bounded agent did not recover media within 60 seconds. Keep looping audio active and inspect $($recoveryPaths.AgentLog)."
        }
        Stop-TrialAgent -AgentPath $agentPath `
            -Paths $recoveryPaths -Process $recoveryProcess
    } else {
        $primaryProcess.WaitForExit()
        if ($primaryProcess.ExitCode -ne 0) {
            throw "The bounded Direct-PDO agent returned $($primaryProcess.ExitCode)."
        }
    }
} finally {
    if (-not $crashIssued -and $null -ne $primaryProcess -and
        -not $primaryProcess.HasExited) {
        Stop-TrialAgent -AgentPath $agentPath `
            -Paths $primaryPaths -Process $primaryProcess
    }
    if ($null -ne $recoveryProcess -and -not $recoveryProcess.HasExited) {
        Stop-TrialAgent -AgentPath $agentPath `
            -Paths $recoveryPaths -Process $recoveryProcess
    }
}

$remainingProcesses = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('ldac_agent.exe', 'ldac_direct_engine.exe')
})
if ($remainingProcesses.Count -ne 0) {
    $remainingSummary = @($remainingProcesses | ForEach-Object {
        "$($_.Name) (PID $($_.ProcessId))"
    })
    throw "The Direct-PDO trial ended with remaining media processes: $($remainingSummary -join ', ')"
}

$result = [ordered]@{
    completed_at = (Get-Date).ToString('o')
    quality = $Quality
    crash_recovery_required = [bool]$RequireCrashRecovery
    cold_connect = [bool]$ColdConnect
    primary_agent_pid = $primaryProcess.Id
    primary_engine_pid = $ownedEnginePid
    primary_agent_log = $primaryPaths.AgentLog
    primary_engine_log = $primaryPaths.EngineLog
    primary_state = $primaryPaths.State
    fault_status = $faultStatus
    recovery_agent_pid = if ($null -eq $recoveryProcess) {
        0
    } else {
        $recoveryProcess.Id
    }
    recovery_agent_log = if ($null -eq $recoveryPaths) {
        ''
    } else {
        $recoveryPaths.AgentLog
    }
    recovery_engine_log = if ($null -eq $recoveryPaths) {
        ''
    } else {
        $recoveryPaths.EngineLog
    }
}
$resultPath = Join-Path $trialRoot `
    ("result-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.json')
$result | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $resultPath -Encoding UTF8

Write-Host 'Bounded Direct-PDO trial completed with no remaining agent or engine process.'
if ($RequireCrashRecovery) {
    Write-Host 'The owned process containment, media-timeout fault, generation-bound recovery, and second media generation were observed.'
}
Write-Host "Result: $resultPath"
Write-Host 'No driver, certificate, scheduled task, installed file, Bluetooth radio, or system setting was changed.'
