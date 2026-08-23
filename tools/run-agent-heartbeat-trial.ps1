# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmHeartbeatTrial,
    [ValidateRange(30, 120)]
    [int]$ConnectTimeoutSeconds = 75,
    [ValidateSet('mq', 'sq', 'hq', 'auto')]
    [string]$Quality = 'hq',
    [ValidateSet('stereo', 'dual', 'mono')]
    [string]$ChannelMode = 'stereo',
    [ValidateSet(44100, 48000, 88200, 96000)]
    [int]$SampleRate = 48000,
    [ValidateSet(16, 24)]
    [int]$BitsPerSample = 16
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
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

function Get-NativeEndpointState {
    param([Parameter(Mandatory = $true)][string]$ProbePath)

    $lines = @(& $ProbePath --info 2>&1)
    for ($index = 0; $index -lt $lines.Count; $index++) {
        if ([string]$lines[$index] -notmatch 'Native LDAC') {
            continue
        }
        $last = [Math]::Min($lines.Count - 1, $index + 4)
        for ($line = $index + 1; $line -le $last; $line++) {
            if ([string]$lines[$line] -match '^\s*state:\s*(\S+)') {
                return $Matches[1]
            }
        }
    }
    return ''
}

Assert-Administrator
if (-not $ConfirmHeartbeatTrial) {
    throw 'Refusing to simulate an agent crash. Re-run with -ConfirmHeartbeatTrial.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$stagedRoot = Join-Path $projectRoot 'artifacts\agent'
$stagedAgent = Join-Path $stagedRoot 'ldac_agent.exe'
$stagedProbe = Join-Path $stagedRoot 'transport_probe.exe'
$linkProbe = Join-Path $projectRoot 'artifacts\audio-endpoint\audio_endpoint_probe.exe'
$endpointProbe = Join-Path $projectRoot 'artifacts\diagnostics\endpoint_volume_probe.exe'
$requiredFiles = @($stagedAgent, $stagedProbe, $linkProbe, $endpointProbe)
$missingFiles = @($requiredFiles | Where-Object {
    -not (Test-Path -LiteralPath $_ -PathType Leaf)
})
if ($missingFiles.Count -ne 0) {
    throw "Required staged files are missing: $($missingFiles -join ', ')"
}

$task = Get-ScheduledTask -TaskName 'Native LDAC Agent' -ErrorAction SilentlyContinue
if ($task) {
    throw 'The login agent task is already installed. Remove it before running an isolated crash trial.'
}
$conflictingProcesses = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @(
        'ldac_agent.exe',
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

$trialRoot = Join-Path $projectRoot 'artifacts\heartbeat-trial'
New-Item -ItemType Directory -Path $trialRoot -Force | Out-Null
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$agentLog = Join-Path $trialRoot "agent-$timestamp.log"
$probeLog = Join-Path $trialRoot "probe-$timestamp.log"
$statePath = Join-Path $trialRoot "state-$timestamp.json"
$resultPath = Join-Path $trialRoot "result-$timestamp.json"
$instanceSuffix = "heartbeat-$timestamp"
$target = 'an isolated staged LDAC agent and its owned probe process'
$action = 'Establish LDAC, forcibly terminate only the new trial agent, and verify the five-second driver heartbeat timeout'
if (-not $PSCmdlet.ShouldProcess($target, $action)) {
    return
}

$quotedProbe = '"' + $stagedProbe + '"'
$quotedAgentLog = '"' + $agentLog + '"'
$quotedProbeLog = '"' + $probeLog + '"'
$quotedStatePath = '"' + $statePath + '"'
$arguments = "--probe $quotedProbe --wait-for-xm5 --quality $Quality --channel-mode $ChannelMode --sample-rate $SampleRate --bits $BitsPerSample --run-for-ms 120000 --instance-suffix $instanceSuffix --log $quotedAgentLog --probe-log $quotedProbeLog --state $quotedStatePath"

Write-Host 'Starting an isolated Native LDAC heartbeat-expiry trial.'
Write-Host 'After XM5 accepts START, the script will forcibly terminate only the agent process it created.'
$trialProcess = Start-Process -FilePath $stagedAgent `
    -ArgumentList $arguments `
    -WindowStyle Hidden `
    -PassThru
$crashIssued = $false
$ownedProbePid = 0
$linkAfterCrash = ''
$linkAfterExpiry = ''
$endpointAfterExpiry = ''

try {
    $started = Wait-Until -TimeoutSeconds $ConnectTimeoutSeconds -Condition {
        if ($trialProcess.HasExited) {
            return $false
        }
        if (-not (Test-Path -LiteralPath $probeLog -PathType Leaf)) {
            return $false
        }
        [string]$output = Get-Content -LiteralPath $probeLog -Raw
        if ([string]::IsNullOrEmpty($output)) {
            return $false
        }
        return $output.Contains('XM5 accepted START; the LDAC Media transport is ready.') -and
            $output.Contains('Live:')
    }
    if (-not $started) {
        if ($trialProcess.HasExited) {
            throw "The trial agent exited before LDAC became active. Logs: $trialRoot"
        }
        throw "LDAC did not become active within $ConnectTimeoutSeconds seconds. Logs: $trialRoot"
    }

    $online = Wait-Until -TimeoutSeconds 10 -Condition {
        (Get-NativeEndpointState -ProbePath $endpointProbe) -eq 'active'
    }
    if (-not $online) {
        throw "The Native LDAC endpoint did not become active before the crash test. Logs: $trialRoot"
    }

    if (Test-Path -LiteralPath $statePath -PathType Leaf) {
        $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
        $ownedProbePid = [int]$state.probe_pid
    }
    if ($ownedProbePid -le 0) {
        throw "The owned probe PID was not published before the crash test. Logs: $trialRoot"
    }

    Write-Host "LDAC is active. Forcibly terminating trial agent PID $($trialProcess.Id); owned probe PID $ownedProbePid is contained by its Job Object."
    Stop-Process -Id $trialProcess.Id -Force
    $trialProcess.WaitForExit()
    $crashIssued = $true

    $probeContained = Wait-Until -TimeoutSeconds 5 -Condition {
        $null -eq (Get-Process -Id $ownedProbePid -ErrorAction SilentlyContinue)
    }
    if (-not $probeContained) {
        throw "The owned probe PID $ownedProbePid survived its agent Job Object."
    }

    Start-Sleep -Seconds 2
    $linkAfterCrash = (@(& $linkProbe --link-state 2>&1) -join [Environment]::NewLine)
    if ($linkAfterCrash -notmatch '(?m)^Link connected:') {
        throw "The link did not remain connected long enough to exercise heartbeat expiry.`n$linkAfterCrash"
    }

    $expired = Wait-Until -TimeoutSeconds 10 -PollMilliseconds 250 -Condition {
        $script:linkAfterExpiry = (@(& $linkProbe --link-state 2>&1) -join [Environment]::NewLine)
        return $script:linkAfterExpiry -match '(?m)^Link disconnected:'
    }
    if (-not $expired) {
        throw "The driver did not publish disconnected after heartbeat expiry.`n$linkAfterExpiry"
    }

    $endpointExpired = Wait-Until -TimeoutSeconds 10 -Condition {
        $script:endpointAfterExpiry = Get-NativeEndpointState -ProbePath $endpointProbe
        return $script:endpointAfterExpiry -eq 'unplugged'
    }
    if (-not $endpointExpired) {
        throw "The Native LDAC endpoint did not become unplugged after heartbeat expiry; current state: $endpointAfterExpiry"
    }
} finally {
    if (-not $crashIssued -and -not $trialProcess.HasExited) {
        $stopArguments = "--stop --instance-suffix $instanceSuffix"
        $null = Start-Process -FilePath $stagedAgent `
            -ArgumentList $stopArguments `
            -WindowStyle Hidden `
            -Wait `
            -PassThru
        if (-not $trialProcess.WaitForExit(30000)) {
            throw 'The trial agent did not stop within 30 seconds. It was not forcibly terminated.'
        }
    }
}

$remaining = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('ldac_agent.exe', 'transport_probe.exe')
})
if ($remaining.Count -ne 0) {
    $summary = @($remaining | ForEach-Object {
        "$($_.Name) (PID $($_.ProcessId))"
    })
    throw "The crash trial ended but a media process remains: $($summary -join ', ')"
}

$result = [ordered]@{
    completed_at = (Get-Date).ToString('o')
    quality = $Quality
    channel_mode = $ChannelMode
    sample_rate = $SampleRate
    bits_per_sample = $BitsPerSample
    agent_pid = $trialProcess.Id
    probe_pid = $ownedProbePid
    agent_exit_code = $trialProcess.ExitCode
    endpoint_after_expiry = $endpointAfterExpiry
    link_after_crash = $linkAfterCrash
    link_after_expiry = $linkAfterExpiry
    agent_log = $agentLog
    probe_log = $probeLog
    state_file = $statePath
}
$result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $resultPath -Encoding UTF8

Write-Host 'Heartbeat-expiry trial passed.'
Write-Host "Owned probe PID $ownedProbePid was terminated by containment; the driver then published disconnected and the endpoint became unplugged."
Write-Host "Result: $resultPath"
Write-Host "Agent log: $agentLog"
Write-Host "Probe log: $probeLog"
Write-Host 'No scheduled task, driver, installed file, or system setting was changed.'
