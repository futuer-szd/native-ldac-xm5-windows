# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1DailyRemoval,
    [string]$InstallRoot = (Join-Path $env:ProgramFiles 'NativeLdac\V1'),
    [string]$RuntimeRoot = (Join-Path $env:LOCALAPPDATA 'NativeLdac\V1'),
    [switch]$RemoveInstalledFiles,
    [switch]$RemoveRuntimeData
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-daily-host-common.ps1')

Assert-V1DailyPowerShell7
Assert-V1DailyAdministrator
if (-not $ConfirmV1DailyRemoval) {
    throw 'Refusing to remove the V1 daily login host. Re-run with -ConfirmV1DailyRemoval.'
}
$paths = Get-V1DailyPaths -InstallRoot $InstallRoot `
    -RuntimeRoot $RuntimeRoot
$target = "$script:V1DailyTaskName and its running host"
if (-not $PSCmdlet.ShouldProcess(
        $target,
        'Stop gracefully and unregister the V1 daily login task')) {
    return
}

$stopScript = Join-Path $paths.InstallRoot 'stop-v1-daily-host.ps1'
if ((Test-Path -LiteralPath $stopScript -PathType Leaf) -and
    (Test-Path -LiteralPath $paths.Config -PathType Leaf)) {
    & $stopScript -InstallRoot $paths.InstallRoot `
        -RuntimeRoot $paths.RuntimeRoot
}
$remaining = @(Get-V1DailyProcesses -AgentPath $paths.Agent)
if ($remaining.Count -ne 0) {
    throw 'The V1 daily host is still running; no task or file was removed.'
}

$task = Get-ScheduledTask -TaskName $script:V1DailyTaskName `
    -ErrorAction SilentlyContinue
if ($task) {
    Unregister-ScheduledTask -TaskName $script:V1DailyTaskName `
        -Confirm:$false
}

if ($RemoveInstalledFiles -and
    (Test-Path -LiteralPath $paths.InstallRoot -PathType Container)) {
    $expected = [IO.Path]::GetFullPath(
        (Join-Path $env:ProgramFiles 'NativeLdac\V1'))
    if (-not $paths.InstallRoot.Equals(
            $expected,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected install root: $($paths.InstallRoot)"
    }
    Remove-Item -LiteralPath $paths.InstallRoot -Recurse -Force
}
if ($RemoveRuntimeData -and
    (Test-Path -LiteralPath $paths.RuntimeRoot -PathType Container)) {
    $expected = [IO.Path]::GetFullPath(
        (Join-Path $env:LOCALAPPDATA 'NativeLdac\V1'))
    if (-not $paths.RuntimeRoot.Equals(
            $expected,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected runtime root: $($paths.RuntimeRoot)"
    }
    Remove-Item -LiteralPath $paths.RuntimeRoot -Recurse -Force
}

Write-Host 'V1 daily host stopped and login task removed.'
Write-Host ($RemoveInstalledFiles `
    ? "Removed installed files: $($paths.InstallRoot)" `
    : "Preserved installed files: $($paths.InstallRoot)")
Write-Host ($RemoveRuntimeData `
    ? "Removed runtime data: $($paths.RuntimeRoot)" `
    : "Preserved config, state, results, and logs: $($paths.RuntimeRoot)")
