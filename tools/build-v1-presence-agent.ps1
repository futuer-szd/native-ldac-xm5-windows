# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = Join-Path $projectRoot 'build\protocol'
$outputRoot = Join-Path $projectRoot 'artifacts\v1-presence'

$sourceCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$sourceStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or
    $sourceCommit -notmatch '^[0-9a-fA-F]{40}$' -or
    $sourceStatus.Count -ne 0) {
    throw 'Refusing to build the V1 presence bundle from a dirty or unknown Git source.'
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
    throw 'CMake was not found in PATH or the Visual Studio 2022 Community installation.'
}

if (-not (Test-Path -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt') `
        -PathType Leaf)) {
    & $cmakePath -S $projectRoot -B $buildRoot -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE."
    }
}

& $cmakePath --build $buildRoot --config $Configuration `
    --target v1_presence_agent xm5_connection_probe
if ($LASTEXITCODE -ne 0) {
    throw "V1 presence bundle build failed with exit code $LASTEXITCODE."
}

$agentSource = Join-Path $buildRoot `
    "$Configuration\v1_presence_agent.exe"
$probeSource = Join-Path $buildRoot `
    "$Configuration\xm5_connection_probe.exe"
foreach ($path in @($agentSource, $probeSource)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "V1 presence bundle output is missing: $path"
    }
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$agentPath = Join-Path $outputRoot 'v1_presence_agent.exe'
$probePath = Join-Path $outputRoot 'xm5_connection_probe.exe'
Copy-Item -LiteralPath $agentSource -Destination $agentPath -Force
Copy-Item -LiteralPath $probeSource -Destination $probePath -Force
$agentHash = Get-FileHash -LiteralPath $agentPath -Algorithm SHA256
$probeHash = Get-FileHash -LiteralPath $probePath -Algorithm SHA256

$postBuildCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$postBuildStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or
    $postBuildCommit -ne $sourceCommit -or
    $postBuildStatus.Count -ne 0) {
    throw 'The Git source changed during the V1 presence bundle build.'
}

$manifestPath = Join-Path $outputRoot 'manifest.json'
$manifest = [ordered]@{
    manifest_version = 1
    source_commit = $sourceCommit
    source_dirty = $false
    agent_file = 'v1_presence_agent.exe'
    agent_sha256 = $agentHash.Hash
    connection_probe_file = 'xm5_connection_probe.exe'
    connection_probe_sha256 = $probeHash.Hash
    capabilities = @(
        'exact_XM5_ACL_event_presence',
        'V1_lifecycle_reducer_presence_only',
        'no_child_process',
        'no_transport_open'
    )
}
$manifest | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "Built V1 presence-only agent: $agentPath"
Write-Host "Agent SHA-256: $($agentHash.Hash)"
Write-Host "Connection probe SHA-256: $($probeHash.Hash)"
Write-Host "Manifest: $manifestPath"
Write-Host 'No Bluetooth request, driver, endpoint, service, task, or system setting was changed.'
