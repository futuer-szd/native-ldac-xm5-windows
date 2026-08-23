# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-daily-host-common.ps1')

Assert-V1DailyPowerShell7
if ($Configuration -cne 'Release') {
    throw 'The V1 daily host candidate must use Release configuration.'
}
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$sourceStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or
    $sourceCommit -notmatch '^[0-9a-fA-F]{40}$' -or
    $sourceStatus.Count -ne 0) {
    throw 'Refusing to build the V1 daily host candidate from dirty or unknown Git source.'
}

$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
$cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
if (-not $cmakePath) {
    $visualStudioCmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (Test-Path -LiteralPath $visualStudioCmake -PathType Leaf) {
        $cmakePath = $visualStudioCmake
    }
}
if (-not $cmakePath) {
    throw 'CMake was not found.'
}
$buildRoot = Join-Path $projectRoot 'build\protocol'
if (-not (Test-Path -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt') `
        -PathType Leaf)) {
    & $cmakePath -S $projectRoot -B $buildRoot -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE."
    }
}
& $cmakePath --build $buildRoot --config $Configuration `
    --target v1_presence_agent v1_transport_daily_worker audio_endpoint_probe endpoint_format_control
if ($LASTEXITCODE -ne 0) {
    throw "V1 daily host build failed with exit code $LASTEXITCODE."
}

$outputRoot = Join-Path $projectRoot `
    'artifacts\v1-daily-host\candidate'
if (Test-Path -LiteralPath $outputRoot -PathType Container) {
    Remove-Item -LiteralPath $outputRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$sources = [ordered]@{
    'v1_presence_agent.exe' = Join-Path $buildRoot `
        "$Configuration\v1_presence_agent.exe"
    'v1_transport_daily_worker.exe' = Join-Path $buildRoot `
        "$Configuration\v1_transport_daily_worker.exe"
    'audio_endpoint_probe.exe' = Join-Path $buildRoot `
        "$Configuration\audio_endpoint_probe.exe"
    'endpoint_format_control.exe' = Join-Path $buildRoot `
        "$Configuration\endpoint_format_control.exe"
    'v1-daily-host-common.ps1' = Join-Path $PSScriptRoot `
        'v1-daily-host-common.ps1'
    'start-v1-daily-host.ps1' = Join-Path $PSScriptRoot `
        'start-v1-daily-host.ps1'
    'stop-v1-daily-host.ps1' = Join-Path $PSScriptRoot `
        'stop-v1-daily-host.ps1'
    'get-v1-daily-host-status.ps1' = Join-Path $PSScriptRoot `
        'get-v1-daily-host-status.ps1'
    'install-v1-daily-autostart.ps1' = Join-Path $PSScriptRoot `
        'install-v1-daily-autostart.ps1'
    'remove-v1-daily-autostart.ps1' = Join-Path $PSScriptRoot `
        'remove-v1-daily-autostart.ps1'
    'run-v1-daily-ui.ps1' = Join-Path $PSScriptRoot `
        'run-v1-daily-ui.ps1'
    'run-v1-daily-full-cycle.ps1' = Join-Path $PSScriptRoot `
        'run-v1-daily-full-cycle.ps1'
    'run-v1-daily-quality-gate.ps1' = Join-Path $PSScriptRoot `
        'run-v1-daily-quality-gate.ps1'
    'ldac_control.py' = Join-Path $projectRoot 'ui\ldac_control.py'
    'agent_config.py' = Join-Path $projectRoot 'ui\agent_config.py'
    'daily_state.py' = Join-Path $projectRoot 'ui\daily_state.py'
    'daily_config_ipc.py' = Join-Path $projectRoot 'ui\daily_config_ipc.py'
    'probe_protocol.py' = Join-Path $projectRoot 'ui\probe_protocol.py'
}
$entries = @()
foreach ($name in $sources.Keys) {
    $source = [string]$sources[$name]
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "V1 daily candidate source is missing: $source"
    }
    $destination = Join-Path $outputRoot $name
    Copy-Item -LiteralPath $source -Destination $destination -Force
    $item = Get-Item -LiteralPath $destination
    $entries += [ordered]@{
        path = $name
        length = [long]$item.Length
        sha256 = Get-V1DailyFileSha256 -Path $destination
    }
}

$agentHelp = @(& (Join-Path $outputRoot 'v1_presence_agent.exe') `
    --help 2>&1)
if ($LASTEXITCODE -ne 0 -or
    ($agentHelp -join "`n") -notmatch '(?m)^Daily use:') {
    throw 'The candidate V1 presence agent does not expose daily mode.'
}
$workerHelp = @(& (Join-Path $outputRoot `
    'v1_transport_daily_worker.exe') --help 2>&1)
if ($LASTEXITCODE -ne 0 -or
    ($workerHelp -join "`n") -notmatch '(?m)^Usage:') {
    throw 'The candidate V1 daily worker help check failed.'
}

$manifest = [ordered]@{
    manifest_version = 1
    host_policy_version = $script:V1DailyHostPolicyVersion
    source_commit = $sourceCommit
    source_dirty = $false
    configuration = $Configuration
    requires_powershell_major = 7
    architecture = 'NativeLdacAudio_WaveRT_plus_LdacNative_transport_plus_V1_daily_user_host'
    audio_policy = [ordered]@{
        quality_allowlist = @('HQ', 'SQ', 'MQ')
        quality_selection_boundary = 'next_safe_media_session'
        gain = 'unity'
        sample_peak_ceiling = 1.0
        dynamic_windows_volume = $true
        startup_silence_ms = 20.0
        fade_in_ms = 500.0
        transient_resume_startup_silence = $true
        transient_resume_fade_in = $true
        ceiling_ramp_ms = 0.0
        continuous_until_explicit_stop = $true
    }
    lifecycle_policy = [ordered]@{
        render_stability_ms = 1000
        signaling_retry_delays_ms = @(1000, 2000, 4000)
        same_acl_multiple_media_sessions = $true
        fault_requires_fresh_acl = $true
        graceful_daily_stop = $true
    }
    files = @($entries)
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content `
    -LiteralPath (Join-Path $outputRoot 'manifest.json') `
    -Encoding utf8NoBOM
$null = Test-V1DailyBundleManifest -Root $outputRoot

$postCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$postStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or $postCommit -cne $sourceCommit -or
    $postStatus.Count -ne 0) {
    throw 'Git source changed during the V1 daily host candidate build.'
}
Write-Host "Built V1 daily host candidate: $outputRoot"
Write-Host "Source commit: $sourceCommit"
Write-Host 'No driver, task, process, Bluetooth request, endpoint, PnP, radio, or system setting was changed.'
