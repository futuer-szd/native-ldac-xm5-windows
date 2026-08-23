# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$IncludeDirectPdoEngine
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = Join-Path $projectRoot 'build\protocol'
$outputRoot = Join-Path $projectRoot 'artifacts\agent'

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

$ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctestPath -PathType Leaf)) {
    throw "CTest was not found next to CMake: $ctestPath"
}

if (-not (Test-Path -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt') -PathType Leaf)) {
    & $cmakePath -S $projectRoot -B $buildRoot -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE."
    }
}

$buildTargets = @(
    'ldac_agent',
    'transport_probe',
    'ldac_agent_probe_stub',
    'ldac_agent_runtime_tests'
)
if ($IncludeDirectPdoEngine) {
    $buildTargets += 'ldac_direct_engine'
}
& $cmakePath --build $buildRoot --config $Configuration --target $buildTargets
if ($LASTEXITCODE -ne 0) {
    throw "Agent build failed with exit code $LASTEXITCODE."
}

& $ctestPath --test-dir $buildRoot -C $Configuration -R '^ldac_agent_(runtime_tests|smoke|reconnect|control|containment|config_reload|legacy_installed_rejects_probe_override|retired_direct_installed_mode_fails_closed|legacy_install_policy|direct_trial_requires_bounds|direct_trial_rejects_probe_override)$' --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "Agent tests failed with exit code $LASTEXITCODE."
}

$configurationRoot = Join-Path $buildRoot $Configuration
$agentPath = Join-Path $configurationRoot 'ldac_agent.exe'
$probePath = Join-Path $configurationRoot 'transport_probe.exe'
$directEnginePath = Join-Path $configurationRoot 'ldac_direct_engine.exe'
$requiredFiles = @($agentPath, $probePath)
if ($IncludeDirectPdoEngine) {
    $requiredFiles += $directEnginePath
}
$missingFiles = @($requiredFiles | Where-Object {
    -not (Test-Path -LiteralPath $_ -PathType Leaf)
})
if ($missingFiles.Count -ne 0) {
    throw "Agent build outputs are missing: $($missingFiles -join ', ')"
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
if (-not $IncludeDirectPdoEngine) {
    $retiredDirectEngine = Join-Path $outputRoot 'ldac_direct_engine.exe'
    if (Test-Path -LiteralPath $retiredDirectEngine -PathType Leaf) {
        Remove-Item -LiteralPath $retiredDirectEngine -Force
    }
}
foreach ($sourcePath in $requiredFiles) {
    Copy-Item -LiteralPath $sourcePath -Destination $outputRoot -Force
}

$fileEntries = foreach ($sourcePath in $requiredFiles) {
    $stagedPath = Join-Path $outputRoot (Split-Path -Leaf $sourcePath)
    $file = Get-Item -LiteralPath $stagedPath
    $hash = Get-FileHash -LiteralPath $stagedPath -Algorithm SHA256
    [ordered]@{
        name = $file.Name
        length = $file.Length
        sha256 = $hash.Hash
    }
}
$manifest = [ordered]@{
    created_at = (Get-Date).ToString('o')
    configuration = $Configuration
    files = @($fileEntries)
    default_quality = 'hq'
    default_channel_mode = 'stereo'
    default_sample_rate = 48000
    default_bits_per_sample = 16
    transport_mode = if ($IncludeDirectPdoEngine) {
        'legacy-user-mode-avdtp-plus-suspended-direct-pdo-companion'
    } else {
        'legacy-user-mode-avdtp'
    }
    xm5_presence_gate = 'connected_and_transport_ready_with_fresh_generation_after_unexpected_exit'
}
$manifestPath = Join-Path $outputRoot 'manifest.json'
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "Built and staged LDAC agent: $outputRoot"
Write-Host "Agent: $(Join-Path $outputRoot 'ldac_agent.exe')"
Write-Host "Probe: $(Join-Path $outputRoot 'transport_probe.exe')"
if ($IncludeDirectPdoEngine) {
    Write-Host "Suspended Direct-PDO companion: $(Join-Path $outputRoot 'ldac_direct_engine.exe')"
}
Write-Host 'No scheduled task, process, driver, or system setting was changed.'
