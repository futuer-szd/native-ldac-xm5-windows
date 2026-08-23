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
    throw 'Refusing to build the V1 PCM/encode observer from a dirty or unknown Git source.'
}

& (Join-Path $PSScriptRoot 'build-v1-engine-ready-observer.ps1') `
    -Configuration $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "V1 engine-ready base build failed with exit code $LASTEXITCODE."
}

$cmakePath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$buildRoot = Join-Path $projectRoot 'build\protocol'
& $cmakePath --build $buildRoot --config $Configuration `
    --target v1_pcm_encode_engine
if ($LASTEXITCODE -ne 0) {
    throw "V1 PCM/encode engine build failed with exit code $LASTEXITCODE."
}
$postBuildCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$postBuildStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or
    $postBuildCommit -ne $sourceCommit -or
    $postBuildStatus.Count -ne 0) {
    throw 'The Git source changed during the V1 PCM/encode observer build.'
}

$baseCandidate = Join-Path $projectRoot `
    'artifacts\v1-engine-ready-observer\candidate'
$outputRoot = Join-Path $projectRoot `
    'artifacts\v1-pcm-encode-observer\candidate'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$files = @(
    [ordered]@{
        source = Join-Path $baseCandidate 'v1_presence_agent.exe'
        relative = 'v1_presence_agent.exe'
    },
    [ordered]@{
        source = Join-Path $buildRoot `
            "$Configuration\v1_pcm_encode_engine.exe"
        relative = 'v1_pcm_encode_engine.exe'
    },
    [ordered]@{
        source = Join-Path $baseCandidate 'audio_endpoint_probe.exe'
        relative = 'audio_endpoint_probe.exe'
    },
    [ordered]@{
        source = Join-Path $baseCandidate 'xm5_connection_probe.exe'
        relative = 'xm5_connection_probe.exe'
    },
    [ordered]@{
        source = Join-Path $baseCandidate `
            'xm5_connection_probe.manifest.json'
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
    required_consumer_lease_abi = 1
    required_presence_abi = 1
    render_poll_ms = 250
    engine_ready_timeout_ms = 3000
    capabilities = @(
        'exact_XM5_ACL_presence_lease',
        'connected_only_PCM_Info_GET',
        'job_object_containment',
        'Native_PCM_read',
        'LDAC_HQ_encode_discard',
        'independent_PCM_consumer_lease',
        'engine_ready_proves_PCM_read_and_encode',
        'ready_after_first_encoded_frame',
        'transport_OPEN_requested_not_executed',
        'no_media_LinkState_write',
        'no_Bluetooth_open'
    )
    files = @($entries)
}
$manifestPath = Join-Path $outputRoot 'manifest.json'
$manifest | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "Built V1 PCM/encode observer: $outputRoot"
Write-Host "Source commit: $sourceCommit"
Write-Host "Manifest: $manifestPath"
Write-Host 'No driver, endpoint, Bluetooth request, default output, or system setting was changed.'
