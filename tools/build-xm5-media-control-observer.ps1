# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSVersion.Major -lt 7) {
    throw 'PowerShell 7 or newer is required.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = Join-Path $projectRoot 'build\protocol'
$outputRoot = Join-Path $projectRoot 'artifacts\diagnostics'
$sourceCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$sourceStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or
    $sourceCommit -notmatch '^[0-9a-fA-F]{40}$' -or
    $sourceStatus.Count -ne 0) {
    throw 'Refusing to build the XM5 media-control observer from a dirty or unknown Git source.'
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

& $cmakePath -S $projectRoot -B $buildRoot -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}
& $cmakePath --build $buildRoot --config $Configuration `
    --target xm5_media_control_observer xm5_media_control_event_tests
if ($LASTEXITCODE -ne 0) {
    throw "XM5 media-control observer build failed with exit code $LASTEXITCODE."
}

$testPath = Join-Path $buildRoot `
    "$Configuration\xm5_media_control_event_tests.exe"
& $testPath
if ($LASTEXITCODE -ne 0) {
    throw "XM5 media-control mapping tests failed with exit code $LASTEXITCODE."
}

$sourcePath = Join-Path $buildRoot `
    "$Configuration\xm5_media_control_observer.exe"
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "XM5 media-control observer output is missing: $sourcePath"
}
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$destinationPath = Join-Path $outputRoot `
    'xm5_media_control_observer.exe'
Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
$item = Get-Item -LiteralPath $destinationPath
$hash = Get-FileHash -LiteralPath $destinationPath -Algorithm SHA256

$manifest = [ordered]@{
    manifest_version = 1
    source_commit = $sourceCommit
    source_dirty = $false
    configuration = $Configuration
    generated_utc = [DateTimeOffset]::UtcNow.ToString('O')
    observer = [ordered]@{
        path = 'xm5_media_control_observer.exe'
        length = [long]$item.Length
        sha256 = $hash.Hash
    }
    capabilities = @(
        'observe_raw_input_hid_consumer_control',
        'observe_media_virtual_keys',
        'observe_WM_APPCOMMAND',
        'exact_Sony_054C_0DF0_filter',
        'no_input_injection',
        'no_media_control',
        'no_audio_or_system_change'
    )
}
$manifestPath = Join-Path $outputRoot `
    'xm5_media_control_observer.manifest.json'
$manifest | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $manifestPath -Encoding UTF8

$postBuildCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$postBuildStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or
    $postBuildCommit -ne $sourceCommit -or
    $postBuildStatus.Count -ne 0) {
    throw 'The Git source changed during the XM5 media-control observer build.'
}

Write-Host "Built read-only XM5 media-control observer: $destinationPath"
Write-Host "SHA-256: $($hash.Hash)"
Write-Host "Manifest: $manifestPath"
Write-Host 'No input was injected and no media, audio, Bluetooth, driver, or system state was changed.'
