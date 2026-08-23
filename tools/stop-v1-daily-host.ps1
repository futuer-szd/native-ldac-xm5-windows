# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$InstallRoot = (Join-Path $env:ProgramFiles 'NativeLdac\V1'),
    [string]$RuntimeRoot = (Join-Path $env:LOCALAPPDATA 'NativeLdac\V1'),
    [ValidateRange(1, 60)]
    [int]$TimeoutSeconds = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-daily-host-common.ps1')

Assert-V1DailyPowerShell7
$paths = Get-V1DailyPaths -InstallRoot $InstallRoot `
    -RuntimeRoot $RuntimeRoot
$config = Read-V1DailyConfig -Path $paths.Config
if (-not (Test-Path -LiteralPath $paths.Agent -PathType Leaf)) {
    throw "The installed V1 daily agent is missing: $($paths.Agent)"
}

& $paths.Agent --stop-daily --instance-suffix `
    ([string]$config.instance_suffix)
$stopExitCode = $LASTEXITCODE
if ($stopExitCode -notin @(0, 15)) {
    throw "The V1 daily stop request failed with exit code $stopExitCode."
}

for ($attempt = 0; $attempt -lt $TimeoutSeconds; ++$attempt) {
    $processes = @(Get-V1DailyProcesses -AgentPath $paths.Agent)
    if ($processes.Count -eq 0) {
        Write-Host 'V1 daily host is stopped.'
        return
    }
    Start-Sleep -Seconds 1
}
$remaining = @(Get-V1DailyProcesses -AgentPath $paths.Agent)
$summary = @()
foreach ($process in $remaining) {
    $summary += "PID $($process.ProcessId)"
}
throw "The V1 daily host did not stop cleanly: $($summary -join ', '). It was not forcibly terminated."
