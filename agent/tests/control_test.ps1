[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Agent,
    [Parameter(Mandatory = $true)]
    [string]$Probe,
    [Parameter(Mandatory = $true)]
    [string]$LogRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$instanceSuffix = 'ctest-control'
$agentLog = Join-Path $LogRoot 'ldac-agent-control.log'
$probeLog = Join-Path $LogRoot 'ldac-agent-control-probe.log'
$statePath = Join-Path $LogRoot 'ldac-agent-control-state.json'
Remove-Item -LiteralPath $agentLog, $probeLog, $statePath -ErrorAction SilentlyContinue

$quotedProbe = '"' + $Probe + '"'
$quotedAgentLog = '"' + $agentLog + '"'
$quotedProbeLog = '"' + $probeLog + '"'
$quotedStatePath = '"' + $statePath + '"'
$primaryArguments = "--probe $quotedProbe --instance-suffix $instanceSuffix --log $quotedAgentLog --probe-log $quotedProbeLog --state $quotedStatePath"
$primary = Start-Process -FilePath $Agent `
    -ArgumentList $primaryArguments `
    -WindowStyle Hidden `
    -PassThru

try {
    Start-Sleep -Milliseconds 500
    if ($primary.HasExited) {
        throw "The primary agent exited early with code $($primary.ExitCode)."
    }

    $duplicate = Start-Process -FilePath $Agent `
        -ArgumentList $primaryArguments `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($duplicate.ExitCode -ne 10) {
        throw "The duplicate agent returned $($duplicate.ExitCode), expected 10."
    }

    $stopper = Start-Process -FilePath $Agent `
        -ArgumentList "--stop --instance-suffix $instanceSuffix" `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($stopper.ExitCode -ne 0) {
        throw "The stop controller returned $($stopper.ExitCode), expected 0."
    }

    if (-not $primary.WaitForExit(10000)) {
        throw 'The primary agent did not exit within 10 seconds.'
    }
    if ($primary.ExitCode -ne 0) {
        throw "The primary agent returned $($primary.ExitCode), expected 0."
    }
    if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
        throw 'The agent did not create its atomic state file.'
    }
    $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    if ($state.state -ne 'stopped' -or $state.probe_pid -ne 0) {
        throw "Unexpected final state: $($state.state), probe PID $($state.probe_pid)."
    }
    $temporaryStates = @(Get-ChildItem -LiteralPath $LogRoot `
        -Filter 'ldac-agent-control-state.json.tmp.*' `
        -ErrorAction SilentlyContinue)
    if ($temporaryStates.Count -ne 0) {
        throw 'The atomic state writer left a temporary file behind.'
    }
} finally {
    if (-not $primary.HasExited) {
        Stop-Process -Id $primary.Id -Force -ErrorAction SilentlyContinue
    }
    Get-Process -Name ldac_agent_probe_stub -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
}
