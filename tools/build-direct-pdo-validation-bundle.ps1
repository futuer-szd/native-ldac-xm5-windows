# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$AllowDirtySource
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-DefineNumber {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $text = Get-Content -LiteralPath $Path -Raw
    $pattern = '(?m)^\s*#define\s+' + [regex]::Escape($Name) +
        '\s+([0-9]+)(?:u|ul|U|UL)?\s*$'
    $match = [regex]::Match($text, $pattern)
    if (-not $match.Success) {
        throw "Could not read $Name from $Path."
    }
    return [int]$match.Groups[1].Value
}

function Get-FileEntry {
    param([Parameter(Mandatory = $true)][string]$Path)

    $file = Get-Item -LiteralPath $Path
    $hash = Get-FileHash -LiteralPath $Path -Algorithm SHA256
    return [ordered]@{
        name = $file.Name
        length = $file.Length
        sha256 = $hash.Hash
    }
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$prototypeScript = Join-Path $PSScriptRoot 'build-direct-pdo-prototype.ps1'
$agentScript = Join-Path $PSScriptRoot 'build-agent.ps1'
$verifyScript = Join-Path $PSScriptRoot 'verify-direct-pdo-validation-bundle.ps1'
$gitCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($gitCommit)) {
    throw 'Could not determine the source Git commit.'
}
$gitStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0) {
    throw 'Could not determine the source Git status.'
}
$sourceDirty = $gitStatus.Count -ne 0
if ($sourceDirty -and -not $AllowDirtySource) {
    throw 'Refusing to create a coordinated validation bundle from a dirty Git worktree. Commit the reviewed source or use -AllowDirtySource only for local development.'
}

powershell.exe -NoProfile -ExecutionPolicy Bypass -File $prototypeScript `
    -Configuration $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "Direct-PDO prototype build failed with exit code $LASTEXITCODE."
}
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $agentScript `
    -Configuration $Configuration `
    -IncludeDirectPdoEngine
if ($LASTEXITCODE -ne 0) {
    throw "Agent build failed with exit code $LASTEXITCODE."
}

$cmakeCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\CMake\bin\cmake.exe'
)
$cmakePath = $cmakeCandidates |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $cmakePath) {
    throw 'CMake was not found.'
}
$cmakeBuildRoot = Join-Path $projectRoot 'build\protocol'
if (-not (Test-Path -LiteralPath (Join-Path $cmakeBuildRoot 'CMakeCache.txt'))) {
    & $cmakePath -S $projectRoot -B $cmakeBuildRoot -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE."
    }
}
& $cmakePath --build $cmakeBuildRoot --config $Configuration `
    --target audio_endpoint_probe endpoint_volume_probe
if ($LASTEXITCODE -ne 0) {
    throw "Direct-PDO status probe build failed with exit code $LASTEXITCODE."
}

$postBuildCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $postBuildCommit -ne $gitCommit) {
    throw 'The source Git commit changed during the coordinated build.'
}
$postBuildStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0) {
    throw 'Could not verify the source Git status after the build.'
}
if (-not $AllowDirtySource -and $postBuildStatus.Count -ne 0) {
    throw 'The coordinated build changed the clean source worktree. Refusing to publish the validation bundle.'
}

$prototypeManifestPath = Join-Path $projectRoot `
    'artifacts\direct-pdo\portcls-prototype\manifest.json'
$prototypeManifest = Get-Content -LiteralPath $prototypeManifestPath -Raw |
    ConvertFrom-Json
if ($prototypeManifest.installable -ne $false -or
    $prototypeManifest.contains_inf -ne $false -or
    $prototypeManifest.contains_catalog -ne $false -or
    $prototypeManifest.contains_certificate -ne $false) {
    throw 'The Direct-PDO prototype manifest is not explicitly non-installable.'
}

$sourceFiles = [ordered]@{
    'NativeLdacDirectPdoPrototype.sys' = Join-Path $projectRoot `
        'artifacts\direct-pdo\portcls-prototype\NativeLdacDirectPdoPrototype.sys'
    'ldac_agent.exe' = Join-Path $projectRoot 'artifacts\agent\ldac_agent.exe'
    'ldac_direct_engine.exe' = Join-Path $projectRoot `
        'artifacts\agent\ldac_direct_engine.exe'
    'audio_endpoint_probe.exe' = Join-Path $cmakeBuildRoot `
        "$Configuration\audio_endpoint_probe.exe"
    'endpoint_volume_probe.exe' = Join-Path $cmakeBuildRoot `
        "$Configuration\endpoint_volume_probe.exe"
}
$missingFiles = @($sourceFiles.Values | Where-Object {
    -not (Test-Path -LiteralPath $_ -PathType Leaf)
})
if ($missingFiles.Count -ne 0) {
    throw "Validation bundle inputs are missing: $($missingFiles -join ', ')"
}

$outputRoot = Join-Path $projectRoot `
    'artifacts\direct-pdo\validation-bundle'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
Get-ChildItem -LiteralPath $outputRoot -File -ErrorAction SilentlyContinue |
    Remove-Item -Force
$staleDirectories = @(Get-ChildItem -LiteralPath $outputRoot -Directory `
    -ErrorAction SilentlyContinue)
if ($staleDirectories.Count -ne 0) {
    throw "Refusing a validation bundle with stale directories: $($staleDirectories.Name -join ', ')"
}
foreach ($entry in $sourceFiles.GetEnumerator()) {
    Copy-Item -LiteralPath $entry.Value `
        -Destination (Join-Path $outputRoot $entry.Key) -Force
}

$forbiddenFiles = @(Get-ChildItem -LiteralPath $outputRoot -File -Recurse |
    Where-Object { $_.Extension -in @('.inf', '.cat', '.cer') })
if ($forbiddenFiles.Count -ne 0) {
    throw "Refusing an installable validation bundle: $($forbiddenFiles.Name -join ', ')"
}

$mediaHeader = Join-Path $projectRoot `
    'direct-pdo\include\nativeldac_direct_pdo_media_abi.h'
$pcmHeader = Join-Path $projectRoot `
    'audio-endpoint\Source\Inc\nativeldac_pcm_abi.h'

$fileEntries = foreach ($name in $sourceFiles.Keys) {
    Get-FileEntry -Path (Join-Path $outputRoot $name)
}
$manifest = [ordered]@{
    manifest_version = 1
    created_at = (Get-Date).ToString('o')
    configuration = $Configuration
    source_commit = $gitCommit
    source_dirty = $sourceDirty
    installable = $false
    contains_inf = $false
    contains_catalog = $false
    contains_certificate = $false
    target = 'Sony WH-1000XM5 Direct-PDO offline validation'
    component_abis = [ordered]@{
        direct_pdo_media = Get-DefineNumber `
            -Path $mediaHeader -Name 'NLD_DIRECT_PDO_MEDIA_ABI_VERSION'
        native_pcm = Get-DefineNumber `
            -Path $pcmHeader -Name 'NATIVE_LDAC_PCM_ABI_VERSION'
        preferred_format = Get-DefineNumber `
            -Path $pcmHeader -Name 'NATIVE_LDAC_FORMAT_ABI_VERSION'
        link_state = Get-DefineNumber `
            -Path $pcmHeader -Name 'NATIVE_LDAC_LINK_STATE_ABI_VERSION'
    }
    policy = [ordered]@{
        package_can_install = $false
        recovery_is_generation_bound = $true
        recovery_submits_bluetooth_open = $false
        remote_fault_requires_disconnect_reconnect_edge = $true
    }
    files = @($fileEntries)
}
$manifestPath = Join-Path $outputRoot 'manifest.json'
$manifest | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $manifestPath -Encoding UTF8

powershell.exe -NoProfile -ExecutionPolicy Bypass -File $verifyScript `
    -BundlePath $outputRoot
if ($LASTEXITCODE -ne 0) {
    throw "Validation bundle verification failed with exit code $LASTEXITCODE."
}

Write-Host "Built non-installable coordinated validation bundle: $outputRoot"
Write-Host "Manifest: $manifestPath"
Write-Host 'No INF, CAT, certificate, driver installation, process, device, or system setting was created or changed.'
