# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateRange(0, 200)]
    [int]$LogLines = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$taskName = 'Native LDAC Agent'
$nativeRoot = Join-Path $env:LOCALAPPDATA 'NativeLdac'
$agentLog = Join-Path $nativeRoot 'logs\agent.log'
$probeLog = Join-Path $nativeRoot 'logs\probe.log'
$statePath = Join-Path $nativeRoot 'logs\state.json'
$configPath = Join-Path $nativeRoot 'config.json'
$task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
$processes = @(Get-CimInstance Win32_Process -Filter "Name = 'ldac_agent.exe'" -ErrorAction SilentlyContinue)

if ($task) {
    $taskInfo = Get-ScheduledTaskInfo -TaskName $taskName
    Write-Host "Task: installed, state $($task.State), last result $($taskInfo.LastTaskResult)"
    Write-Host "Last run: $($taskInfo.LastRunTime)"
    Write-Host "Next run: $($taskInfo.NextRunTime)"
} else {
    Write-Host 'Task: not installed'
}
Write-Host "Agent processes: $($processes.Count)"
foreach ($process in $processes) {
    Write-Host "Agent PID $($process.ProcessId): $($process.ExecutablePath)"
}

Write-Host "State file: $statePath"
if (Test-Path -LiteralPath $statePath -PathType Leaf) {
    try {
        $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
        Write-Host "Agent state: $($state.state), quality $($state.quality), generation $($state.generation)"
        $probeExitSummary = if ($state.state -eq 'stopped' -and
            $state.last_probe_exit_code -eq 130) {
            '130 (controlled stop)'
        } else {
            [string]$state.last_probe_exit_code
        }
        Write-Host "State PIDs: agent $($state.agent_pid), probe $($state.probe_pid); last exit $probeExitSummary, retry $($state.retry_delay_ms) ms"
        Write-Host "State updated: $($state.updated_at)"
    } catch {
        Write-Warning "Could not parse the agent state file: $($_.Exception.Message)"
    }
} else {
    Write-Host 'Agent state: unavailable'
}

Write-Host "Config file: $configPath"
if (Test-Path -LiteralPath $configPath -PathType Leaf) {
    try {
        $config = Get-Content -LiteralPath $configPath -Raw |
            ConvertFrom-Json
        Write-Host "Config: enabled $($config.enabled), quality $($config.quality), revision $($config.revision)"
    } catch {
        Write-Warning "Could not parse the agent config file: $($_.Exception.Message)"
    }
} else {
    Write-Host 'Config: defaults are active (enabled, task quality)'
}

Write-Host "Agent log: $agentLog"
if ($LogLines -gt 0 -and (Test-Path -LiteralPath $agentLog -PathType Leaf)) {
    Get-Content -LiteralPath $agentLog -Tail $LogLines
}
Write-Host "Probe log: $probeLog"
if ($LogLines -gt 0 -and (Test-Path -LiteralPath $probeLog -PathType Leaf)) {
    Get-Content -LiteralPath $probeLog -Tail $LogLines
}
