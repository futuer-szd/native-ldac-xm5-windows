# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$sourceStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or
    $sourceCommit -notmatch '^[0-9a-fA-F]{40}$' -or
    $sourceStatus.Count -ne 0) {
    throw 'Refusing to build the V1 render-demand observer from a dirty or unknown Git source.'
}

& (Join-Path $PSScriptRoot 'build-xm5-connection-probe.ps1') `
    -Configuration $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "Trusted XM5 connection probe build failed with exit code $LASTEXITCODE."
}

$cmakePath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmakePath -PathType Leaf)) {
    throw 'Visual Studio CMake was not found.'
}
$buildRoot = Join-Path $projectRoot 'build\protocol'
& $cmakePath -S $projectRoot -B $buildRoot -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}
& $cmakePath --build $buildRoot --config $Configuration `
    --target v1_presence_agent audio_endpoint_probe
if ($LASTEXITCODE -ne 0) {
    throw "V1 render-demand observer build failed with exit code $LASTEXITCODE."
}

$postBuildCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$postBuildStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or
    $postBuildCommit -ne $sourceCommit -or
    $postBuildStatus.Count -ne 0) {
    throw 'The Git source changed during the V1 render-demand observer build.'
}

$outputRoot = Join-Path $projectRoot `
    'artifacts\v1-render-demand-observer\candidate'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$files = @(
    [ordered]@{
        source = Join-Path $buildRoot `
            "$Configuration\v1_presence_agent.exe"
        relative = 'v1_presence_agent.exe'
    },
    [ordered]@{
        source = Join-Path $buildRoot `
            "$Configuration\audio_endpoint_probe.exe"
        relative = 'audio_endpoint_probe.exe'
    },
    [ordered]@{
        source = Join-Path $projectRoot `
            'artifacts\diagnostics\xm5_connection_probe.exe'
        relative = 'xm5_connection_probe.exe'
    },
    [ordered]@{
        source = Join-Path $projectRoot `
            'artifacts\diagnostics\xm5_connection_probe.manifest.json'
        relative = 'xm5_connection_probe.manifest.json'
    }
)

$entries = @()
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath $file.source -PathType Leaf)) {
        throw "Observer input is missing: $($file.source)"
    }
    $destination = Join-Path $outputRoot $file.relative
    Copy-Item -LiteralPath $file.source -Destination $destination -Force
    $item = Get-Item -LiteralPath $destination
    $entries += [ordered]@{
        path = [string]$file.relative
        length = [long]$item.Length
        sha256 = (Get-FileHash -LiteralPath $destination `
            -Algorithm SHA256).Hash
    }
}

$manifest = [ordered]@{
    manifest_version = 1
    source_commit = $sourceCommit
    source_dirty = $false
    configuration = $Configuration
    required_pcm_abi = 2
    required_presence_abi = 1
    render_poll_ms = 250
    render_confirmation_samples = 2
    capabilities = @(
        'exact_XM5_ACL_presence_lease',
        'connected_only_PCM_Info_GET',
        'render_actions_observed_not_executed',
        'no_PCM_read',
        'no_child_process',
        'no_transport_open'
    )
    files = @($entries)
}
$manifestPath = Join-Path $outputRoot 'manifest.json'
$manifest | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "Built V1 render-demand observer: $outputRoot"
Write-Host "Source commit: $sourceCommit"
Write-Host "Manifest: $manifestPath"
Write-Host 'No driver, endpoint, Bluetooth request, process, default output, or system setting was changed.'
