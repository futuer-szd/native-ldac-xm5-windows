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

$instanceSuffix = 'ctest-containment'
$agentLog = Join-Path $LogRoot 'ldac-agent-containment.log'
$probeLog = Join-Path $LogRoot 'ldac-agent-containment-probe.log'
$statePath = Join-Path $LogRoot 'ldac-agent-containment-state.json'
Remove-Item -LiteralPath $agentLog, $probeLog, $statePath -ErrorAction SilentlyContinue

$quotedProbe = '"' + $Probe + '"'
$quotedAgentLog = '"' + $agentLog + '"'
$quotedProbeLog = '"' + $probeLog + '"'
$quotedStatePath = '"' + $statePath + '"'
$arguments = "--probe $quotedProbe --instance-suffix $instanceSuffix --log $quotedAgentLog --probe-log $quotedProbeLog --state $quotedStatePath"
$agentProcess = Start-Process -FilePath $Agent `
    -ArgumentList $arguments `
    -WindowStyle Hidden `
    -PassThru
$probeId = 0

try {
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        if ($agentProcess.HasExited) {
            throw "The containment agent exited early with code $($agentProcess.ExitCode)."
        }
        if (Test-Path -LiteralPath $statePath -PathType Leaf) {
            try {
                $state = Get-Content -LiteralPath $statePath -Raw |
                    ConvertFrom-Json
                if ($state.state -eq 'probe_running' -and
                    $state.probe_pid -gt 0) {
                    $probeId = [int]$state.probe_pid
                    break
                }
            } catch {
                # Atomic replacement means a concurrent read should be valid;
                # retry briefly if antivirus or indexing still races the open.
            }
        }
        Start-Sleep -Milliseconds 100
    }
    if ($probeId -le 0) {
        throw 'The containment test did not observe a running probe.'
    }
    if (-not (Get-Process -Id $probeId -ErrorAction SilentlyContinue)) {
        throw "The recorded probe PID $probeId was not running."
    }

    Stop-Process -Id $agentProcess.Id -Force
    if (-not $agentProcess.WaitForExit(5000)) {
        throw 'The forced test agent did not exit within 5 seconds.'
    }

    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        if (-not (Get-Process -Id $probeId -ErrorAction SilentlyContinue)) {
            $probeId = 0
            break
        }
        Start-Sleep -Milliseconds 100
    }
    if ($probeId -ne 0) {
        throw "Probe PID $probeId survived the agent Job Object close."
    }
} finally {
    if (-not $agentProcess.HasExited) {
        Stop-Process -Id $agentProcess.Id -Force -ErrorAction SilentlyContinue
    }
    if ($probeId -gt 0) {
        Stop-Process -Id $probeId -Force -ErrorAction SilentlyContinue
    }
}
