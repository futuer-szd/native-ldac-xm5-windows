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

if (-not (Test-Path -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt') -PathType Leaf)) {
    & $cmakePath -S $projectRoot -B $buildRoot -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE."
    }
}

& $cmakePath --build $buildRoot --config $Configuration --target endpoint_volume_probe
if ($LASTEXITCODE -ne 0) {
    throw "Volume probe build failed with exit code $LASTEXITCODE."
}

$sourcePath = Join-Path $buildRoot "$Configuration\endpoint_volume_probe.exe"
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "Volume probe output is missing: $sourcePath"
}
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$destinationPath = Join-Path $outputRoot 'endpoint_volume_probe.exe'
Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
$hash = Get-FileHash -LiteralPath $destinationPath -Algorithm SHA256

Write-Host "Built read-only endpoint volume probe: $destinationPath"
Write-Host "SHA-256: $($hash.Hash)"
Write-Host 'No device, volume, driver, process, or system setting was changed.'
