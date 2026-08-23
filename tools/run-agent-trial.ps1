# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmAgentTrial,
    [ValidateRange(15, 300)]
    [int]$DurationSeconds = 60,
    [ValidateSet('mq', 'sq', 'hq', 'auto')]
    [string]$Quality = 'hq',
    [ValidateSet('stereo', 'dual', 'mono')]
    [string]$ChannelMode = 'stereo',
    [ValidateSet(44100, 48000, 88200, 96000)]
    [int]$SampleRate = 48000,
    [ValidateSet(16, 24)]
    [int]$BitsPerSample = 16,
    [switch]$RequireReconnect
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

Assert-Administrator
if (-not $ConfirmAgentTrial) {
    throw 'Refusing to start the bounded agent trial. Re-run with -ConfirmAgentTrial.'
}
if ($RequireReconnect -and $DurationSeconds -lt 90) {
    throw 'A reconnect trial requires at least 90 seconds.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$stagedRoot = Join-Path $projectRoot 'artifacts\agent'
$stagedAgent = Join-Path $stagedRoot 'ldac_agent.exe'
$stagedProbe = Join-Path $stagedRoot 'transport_probe.exe'
$requiredFiles = @($stagedAgent, $stagedProbe)
$missingFiles = @($requiredFiles | Where-Object {
    -not (Test-Path -LiteralPath $_ -PathType Leaf)
})
if ($missingFiles.Count -ne 0) {
    throw "Staged agent files are missing. Run tools\build-agent.ps1 first: $($missingFiles -join ', ')"
}

$task = Get-ScheduledTask -TaskName 'Native LDAC Agent' -ErrorAction SilentlyContinue
if ($task) {
    throw 'The login agent task is already installed. Remove it before running an isolated trial.'
}

$activeAgents = @(Get-Process -Name ldac_agent -ErrorAction SilentlyContinue)
$candidateMediaProcesses = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('transport_probe.exe', 'audio_endpoint_probe.exe')
})
$activeMediaProcesses = @($candidateMediaProcesses | Where-Object {
    $isTransportProbe = $_.Name -eq 'transport_probe.exe'
    $isAudioEndpointProbe = $_.Name -eq 'audio_endpoint_probe.exe'
    $isReadOnlyLinkMonitor = $isAudioEndpointProbe -and
        -not [string]::IsNullOrWhiteSpace([string]$_.CommandLine) -and
        [string]$_.CommandLine -match '(?i)(^|\s)--monitor-link(\s|$)'
    $isTransportProbe -or ($isAudioEndpointProbe -and -not $isReadOnlyLinkMonitor)
})
if ($activeAgents.Count -ne 0 -or $activeMediaProcesses.Count -ne 0) {
    $processSummary = @()
    $processSummary += @($activeAgents | ForEach-Object {
        "ldac_agent.exe (PID $($_.Id))"
    })
    $processSummary += @($activeMediaProcesses | ForEach-Object {
        "$($_.Name) (PID $($_.ProcessId))"
    })
    throw "Stop the existing LDAC agent, UI, or probe first: $($processSummary -join ', ')"
}

$trialRoot = Join-Path $projectRoot 'artifacts\agent-trial'
New-Item -ItemType Directory -Path $trialRoot -Force | Out-Null
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$agentLog = Join-Path $trialRoot "agent-$timestamp.log"
$probeLog = Join-Path $trialRoot "probe-$timestamp.log"
$statePath = Join-Path $trialRoot "state-$timestamp.json"
$instanceSuffix = "trial-$timestamp"
$durationMilliseconds = $DurationSeconds * 1000
$target = "staged Native LDAC agent and XM5 for $DurationSeconds seconds"
$trialMode = if ($RequireReconnect) { 'reconnect trial' } else { 'trial' }
$actionDescription = "Run a bounded $Quality/$ChannelMode/$SampleRate-Hz/$BitsPerSample-bit $trialMode; no scheduled task, driver, or system setting will be changed"
if (-not $PSCmdlet.ShouldProcess($target, $actionDescription)) {
    return
}

$quotedProbe = '"' + $stagedProbe + '"'
$quotedAgentLog = '"' + $agentLog + '"'
$quotedProbeLog = '"' + $probeLog + '"'
$quotedStatePath = '"' + $statePath + '"'
$arguments = "--probe $quotedProbe --wait-for-xm5 --quality $Quality --channel-mode $ChannelMode --sample-rate $SampleRate --bits $BitsPerSample --run-for-ms $durationMilliseconds --instance-suffix $instanceSuffix --log $quotedAgentLog --probe-log $quotedProbeLog --state $quotedStatePath"

Write-Host "Starting the bounded Native LDAC trial for $DurationSeconds seconds."
Write-Host 'The agent will stop its media session automatically when the time limit expires.'
if ($RequireReconnect) {
    Write-Host 'After audio starts, turn the XM5 off exactly once. Wait at least 30 seconds for complete shutdown before turning it on again. Do not repeat the power cycle during this trial.'
}
$trialProcess = Start-Process -FilePath $stagedAgent `
    -ArgumentList $arguments `
    -WindowStyle Hidden `
    -PassThru

try {
    $trialProcess.WaitForExit()
} finally {
    if (-not $trialProcess.HasExited) {
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

if ($trialProcess.ExitCode -ne 0) {
    throw "The bounded agent trial returned exit code $($trialProcess.ExitCode). Logs: $trialRoot"
}
if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
    throw "The trial finished without a state file. Logs: $trialRoot"
}
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
if ($state.state -ne 'stopped' -or $state.probe_pid -ne 0) {
    throw "The final trial state is $($state.state), probe PID $($state.probe_pid). Logs: $trialRoot"
}
if ($state.generation -eq 0) {
    throw "The XM5/Native LDAC readiness gate never opened, so no media probe was started. Check the agent log and the A2DP transport PnP status. Logs: $trialRoot"
}
if (-not (Test-Path -LiteralPath $probeLog -PathType Leaf)) {
    throw "The trial finished without a probe log. Logs: $trialRoot"
}
$probeOutput = Get-Content -LiteralPath $probeLog -Raw
$requiredEvidence = @(
    'XM5 accepted START; the LDAC Media transport is ready.',
    'Live:',
    'XM5 accepted SUSPEND.',
    'XM5 accepted CLOSE;'
)
$missingEvidence = @($requiredEvidence | Where-Object {
    -not $probeOutput.Contains($_)
})
if ($missingEvidence.Count -ne 0) {
    throw "The trial stopped, but the XM5 session evidence is incomplete: $($missingEvidence -join ' | '). Logs: $trialRoot"
}
$volumeBindingObserved = [regex]::IsMatch(
    $probeOutput,
    '(?m)^Source: .*, volume \d+%(?: \(muted\))?\.\r?$'
)
if (-not $volumeBindingObserved) {
    throw "The LDAC session ran, but the probe never bound the Native LDAC Windows endpoint volume. Audio remained fail-muted for safety. Logs: $trialRoot"
}
$acceptedStartCount = [regex]::Matches(
    $probeOutput,
    [regex]::Escape('XM5 accepted START; the LDAC Media transport is ready.')
).Count
if ($RequireReconnect -and
    ($state.generation -lt 2 -or $acceptedStartCount -lt 2)) {
    throw "The trial stopped, but a complete disconnect/reconnect was not observed: generation $($state.generation), accepted START count $acceptedStartCount. Logs: $trialRoot"
}

$remainingMediaProcesses = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('ldac_agent.exe', 'transport_probe.exe')
})
if ($remainingMediaProcesses.Count -ne 0) {
    $remainingSummary = @($remainingMediaProcesses | ForEach-Object {
        "$($_.Name) (PID $($_.ProcessId))"
    })
    throw "The trial ended but an agent or probe process remains: $($remainingSummary -join ', ')"
}

Write-Host 'Bounded Native LDAC trial completed and stopped cleanly.'
Write-Host 'Windows endpoint volume binding observed. This confirms safe gain control, not audible loudness.'
$probeExitSummary = if ($state.last_probe_exit_code -eq 130) {
    'controlled stop 130 (expected)'
} else {
    [string]$state.last_probe_exit_code
}
Write-Host "Final state: $($state.state), generation $($state.generation), last probe exit $probeExitSummary"
if ($RequireReconnect) {
    Write-Host "Reconnect evidence: $acceptedStartCount accepted START sessions across $($state.generation) agent generations."
}
Write-Host "Agent log: $agentLog"
Write-Host "Probe log: $probeLog"
Write-Host "State: $statePath"
Write-Host 'No scheduled task, driver, installed file, or system setting was changed.'
