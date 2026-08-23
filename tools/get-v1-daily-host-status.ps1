# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$InstallRoot = (Join-Path $env:ProgramFiles 'NativeLdac\V1'),
    [string]$RuntimeRoot = (Join-Path $env:LOCALAPPDATA 'NativeLdac\V1'),
    [ValidateRange(0, 200)]
    [int]$LogLines = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-daily-host-common.ps1')

Assert-V1DailyPowerShell7
$paths = Get-V1DailyPaths -InstallRoot $InstallRoot `
    -RuntimeRoot $RuntimeRoot
$task = Get-ScheduledTask -TaskName $script:V1DailyTaskName `
    -ErrorAction SilentlyContinue
if ($task) {
    $taskInfo = Get-ScheduledTaskInfo -TaskName $script:V1DailyTaskName
    Write-Host "Task: installed, state $($task.State), last result $($taskInfo.LastTaskResult)"
    Write-Host "Last run: $($taskInfo.LastRunTime)"
} else {
    Write-Host 'Task: not installed'
}

$processes = @(Get-V1DailyProcesses -AgentPath $paths.Agent)
Write-Host "V1 daily processes: $($processes.Count)"
foreach ($process in $processes) {
    Write-Host "PID $($process.ProcessId): $($process.ExecutablePath)"
}

Write-Host "Config: $($paths.Config)"
if (Test-Path -LiteralPath $paths.Config -PathType Leaf) {
    try {
        $config = Read-V1DailyConfig -Path $paths.Config
        Write-Host "Instance: $($config.instance_suffix); log limit $($config.maximum_log_bytes) bytes x $($config.retained_logs)"
    } catch {
        Write-Warning $_.Exception.Message
    }
}

Write-Host "State: $($paths.State)"
if (Test-Path -LiteralPath $paths.State -PathType Leaf) {
    try {
        $state = Get-Content -LiteralPath $paths.State -Raw |
            ConvertFrom-Json
        Write-Host "Agent state: $($state.state), presence $($state.physical_presence), render $($state.render_demand), generation $($state.acl_generation)"
        Write-Host "Sessions: $($state.completed_media_sessions_for_generation); workers: $($state.transport_worker_sequence)"
    } catch {
        Write-Warning "Could not parse daily state: $($_.Exception.Message)"
    }
}

foreach ($log in @($paths.StandardLog, $paths.ErrorLog)) {
    Write-Host "Log: $log"
    if ($LogLines -gt 0 -and
        (Test-Path -LiteralPath $log -PathType Leaf)) {
        Get-Content -LiteralPath $log -Tail $LogLines
    }
}
