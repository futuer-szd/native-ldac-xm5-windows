# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmAgentRemoval,
    [switch]$RemoveInstalledFiles
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
if (-not $ConfirmAgentRemoval) {
    throw 'Refusing to remove the login agent. Re-run with -ConfirmAgentRemoval.'
}

$taskName = 'Native LDAC Agent'
$nativeRoot = [System.IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'NativeLdac'))
$programFilesRoot = [System.IO.Path]::GetFullPath($env:ProgramFiles)
$installRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $programFilesRoot 'NativeLdac\bin'))
$legacyInstallRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $nativeRoot 'bin'))
$installedAgent = Join-Path $installRoot 'ldac_agent.exe'
$legacyInstalledAgent = Join-Path $legacyInstallRoot 'ldac_agent.exe'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$stagedAgent = Join-Path $projectRoot 'artifacts\agent\ldac_agent.exe'
$controlAgent = if (Test-Path -LiteralPath $installedAgent -PathType Leaf) {
    $installedAgent
} elseif (Test-Path -LiteralPath $legacyInstalledAgent -PathType Leaf) {
    $legacyInstalledAgent
} elseif (Test-Path -LiteralPath $stagedAgent -PathType Leaf) {
    $stagedAgent
} else {
    $null
}

$target = "$taskName and running Native LDAC agent"
$actionDescription = if ($RemoveInstalledFiles) {
    'Stop the agent, unregister login autostart, and remove installed binaries'
} else {
    'Stop the agent and unregister login autostart'
}
if (-not $PSCmdlet.ShouldProcess($target, $actionDescription)) {
    return
}

if ($controlAgent) {
    $null = Start-Process -FilePath $controlAgent `
        -ArgumentList @('--stop') `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
}

for ($attempt = 0; $attempt -lt 30; $attempt++) {
    $agents = @(Get-Process -Name ldac_agent -ErrorAction SilentlyContinue)
    if ($agents.Count -eq 0) {
        break
    }
    Start-Sleep -Seconds 1
}
$remainingAgents = @(Get-Process -Name ldac_agent -ErrorAction SilentlyContinue)
if ($remainingAgents.Count -ne 0) {
    throw 'The agent did not stop within 30 seconds. It was not forcibly terminated and the task was not removed.'
}

$task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($task) {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
}

if ($RemoveInstalledFiles -and (Test-Path -LiteralPath $installRoot)) {
    $expectedInstallRoot = Join-Path $programFilesRoot 'NativeLdac\bin'
    if (-not $installRoot.Equals(
        [System.IO.Path]::GetFullPath($expectedInstallRoot),
        [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected install path: $installRoot"
    }
    Remove-Item -LiteralPath $installRoot -Recurse -Force
}
if ($RemoveInstalledFiles -and
    (Test-Path -LiteralPath $legacyInstallRoot -PathType Container)) {
    $expectedLegacyRoot = Join-Path $nativeRoot 'bin'
    if (-not $legacyInstallRoot.Equals(
        [System.IO.Path]::GetFullPath($expectedLegacyRoot),
        [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected legacy path: $legacyInstallRoot"
    }
    Remove-Item -LiteralPath $legacyInstallRoot -Recurse -Force
}

Write-Host 'Native LDAC agent stopped and login task removed.'
if ($RemoveInstalledFiles) {
    Write-Host "Removed installed binaries: $installRoot"
} else {
    Write-Host "Installed binaries were preserved: $installRoot"
}
Write-Host "Logs were preserved: $(Join-Path $nativeRoot 'logs')"
