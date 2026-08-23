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
$outputRoot = Join-Path $projectRoot 'artifacts\diagnostics'

$sourceCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$sourceStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or
    $sourceCommit -notmatch '^[0-9a-fA-F]{40}$' -or
    $sourceStatus.Count -ne 0) {
    throw 'Refusing to build the trusted connection probe from a dirty or unknown Git source.'
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
    --target xm5_connection_probe
if ($LASTEXITCODE -ne 0) {
    throw "XM5 connection probe build failed with exit code $LASTEXITCODE."
}

$sourcePath = Join-Path $buildRoot `
    "$Configuration\xm5_connection_probe.exe"
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "XM5 connection probe output is missing: $sourcePath"
}
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$destinationPath = Join-Path $outputRoot 'xm5_connection_probe.exe'
Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
$hash = Get-FileHash -LiteralPath $destinationPath -Algorithm SHA256
$postBuildCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$postBuildStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or
    $postBuildCommit -ne $sourceCommit -or
    $postBuildStatus.Count -ne 0) {
    throw 'The Git source changed during the trusted connection probe build.'
}
$manifestPath = Join-Path $outputRoot `
    'xm5_connection_probe.manifest.json'
$manifest = [ordered]@{
    manifest_version = 3
    source_commit = $sourceCommit
    source_dirty = $false
    file_name = 'xm5_connection_probe.exe'
    sha256 = $hash.Hash
    capabilities = @(
        'BluetoothFindFirstDevice_fConnected_no_inquiry',
        'GUID_BLUETOOTH_HCI_EVENT_acl_transition',
        'BluetoothIsConnectable_radio_state',
        'read_only_acl_pdo_render_timeline'
    )
}
$manifest | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "Built read-only XM5 connection probe: $destinationPath"
Write-Host "SHA-256: $($hash.Hash)"
Write-Host "Manifest: $manifestPath"
Write-Host 'No inquiry, connection request, driver, endpoint, process, or system setting was changed.'
