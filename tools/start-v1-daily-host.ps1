# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$InstallRoot = (Join-Path $env:ProgramFiles 'NativeLdac\V1'),
    [string]$RuntimeRoot = (Join-Path $env:LOCALAPPDATA 'NativeLdac\V1')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-daily-host-common.ps1')

Assert-V1DailyPowerShell7
Assert-V1DailyHandoffRetired
$paths = Get-V1DailyPaths -InstallRoot $InstallRoot `
    -RuntimeRoot $RuntimeRoot
$null = Test-V1DailyBundleManifest -Root $paths.InstallRoot
$config = Read-V1DailyConfig -Path $paths.Config
$volumeSyncEnabled = $false
$volumeSyncProperty = $config.PSObject.Properties['volume_sync']
if ($null -ne $volumeSyncProperty -and $null -ne $volumeSyncProperty.Value) {
    $volumeSyncConfig = $volumeSyncProperty.Value
    $enabledProperty = $volumeSyncConfig.PSObject.Properties['enabled']
    if ($null -ne $enabledProperty) {
        $volumeSyncEnabled = [bool]$enabledProperty.Value
    }
}

foreach ($directory in @(
        $paths.StateDirectory,
        $paths.ResultDirectory,
        $paths.LogDirectory)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}
Invoke-V1DailyLogRotation -Path $paths.StandardLog `
    -MaximumBytes ([long]$config.maximum_log_bytes) `
    -RetainedLogs ([int]$config.retained_logs)
Invoke-V1DailyLogRotation -Path $paths.ErrorLog `
    -MaximumBytes ([long]$config.maximum_log_bytes) `
    -RetainedLogs ([int]$config.retained_logs)

$startedAt = (Get-Date).ToString('o')
Write-Host "State: $($paths.State)"
Write-Host "Transport result: $($paths.Result)"
Write-Host "Log: $($paths.StandardLog)"
Write-Host 'V1 daily host remains running until an explicit stop request.'
Add-Content -LiteralPath $paths.StandardLog -Encoding utf8NoBOM `
    -Value "[$startedAt] Starting V1 daily host policy $script:V1DailyHostPolicyVersion."

$arguments = @(
    '--daily',
    '--state', $paths.State,
    '--engine-executable', $paths.Worker,
    '--transport-result', $paths.Result,
    '--instance-suffix', [string]$config.instance_suffix
)
if ($volumeSyncEnabled) {
    $arguments += '--volume-sync'
}
Push-Location $paths.InstallRoot
try {
    & $paths.Agent @arguments `
        1>> $paths.StandardLog `
        2>> $paths.ErrorLog
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
$stoppedAt = (Get-Date).ToString('o')
Add-Content -LiteralPath $paths.StandardLog -Encoding utf8NoBOM `
    -Value "[$stoppedAt] V1 daily host exited with code $exitCode."
exit $exitCode
