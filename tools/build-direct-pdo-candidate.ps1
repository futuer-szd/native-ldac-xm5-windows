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
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Name
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
    return [ordered]@{
        name = $file.Name
        length = $file.Length
        sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    }
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
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
    throw 'Refusing to stage an installable candidate from a dirty Git worktree.'
}

$coreScript = Join-Path $PSScriptRoot 'build-direct-pdo-core.ps1'
$agentScript = Join-Path $PSScriptRoot 'build-agent.ps1'
$verifyScript = Join-Path $PSScriptRoot 'verify-direct-pdo-candidate.ps1'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $coreScript `
    -Configuration $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "Direct-PDO core build failed with exit code $LASTEXITCODE."
}

$msbuildCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe'
)
$msbuildPath = $msbuildCandidates |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $msbuildPath) {
    throw '64-bit Visual Studio 2022 MSBuild was not found.'
}
$solutionPath = Join-Path $projectRoot 'audio-endpoint\SimpleAudioSample.sln'
& $msbuildPath $solutionPath /m /t:Rebuild `
    "/p:Configuration=$Configuration" /p:Platform=x64 `
    /p:NativeLdacDirectPdoCandidate=true
if ($LASTEXITCODE -ne 0) {
    throw "Direct-PDO candidate build failed with exit code $LASTEXITCODE."
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
$cmakeRoot = Join-Path $projectRoot 'build\protocol'
& $cmakePath --build $cmakeRoot --config $Configuration `
    --target audio_endpoint_probe endpoint_volume_probe
if ($LASTEXITCODE -ne 0) {
    throw "Endpoint probe build failed with exit code $LASTEXITCODE."
}

$postBuildCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$postBuildStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or $postBuildCommit -ne $gitCommit) {
    throw 'The source Git commit changed during the candidate build.'
}
if (-not $AllowDirtySource -and $postBuildStatus.Count -ne 0) {
    throw 'The candidate build changed the clean source worktree.'
}

$sourcePackage = Join-Path $projectRoot `
    "audio-endpoint\x64\$Configuration\package"
$sourceCertificate = Join-Path $projectRoot `
    "audio-endpoint\x64\$Configuration\package.cer"
$sourceFiles = [ordered]@{
    'NativeLdacDirectPdo.inf' = Join-Path $sourcePackage 'NativeLdacDirectPdo.inf'
    'NativeLdacDirectPdo.sys' = Join-Path $sourcePackage 'NativeLdacDirectPdo.sys'
    'NativeLdacDirectPdo.cat' = Join-Path $sourcePackage 'nativeldacdirectpdo.cat'
    'NativeLdacDirectPdo.cer' = $sourceCertificate
}
$companionFiles = [ordered]@{
    'ldac_agent.exe' = Join-Path $projectRoot 'artifacts\agent\ldac_agent.exe'
    'ldac_direct_engine.exe' = Join-Path $projectRoot 'artifacts\agent\ldac_direct_engine.exe'
    'audio_endpoint_probe.exe' = Join-Path $cmakeRoot `
        "$Configuration\audio_endpoint_probe.exe"
    'endpoint_volume_probe.exe' = Join-Path $cmakeRoot `
        "$Configuration\endpoint_volume_probe.exe"
}
$missing = @(@($sourceFiles.Values) + @($companionFiles.Values) |
    Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
if ($missing.Count -ne 0) {
    throw "Candidate inputs are missing: $($missing -join ', ')"
}

$outputRoot = Join-Path $projectRoot 'artifacts\direct-pdo\candidate'
$outputPackage = Join-Path $outputRoot 'package'
New-Item -ItemType Directory -Path $outputPackage -Force | Out-Null
$unexpectedDirectories = @(Get-ChildItem -LiteralPath $outputRoot -Directory |
    Where-Object { $_.Name -ne 'package' })
if ($unexpectedDirectories.Count -ne 0) {
    throw "Refusing stale candidate directories: $($unexpectedDirectories.Name -join ', ')"
}
Get-ChildItem -LiteralPath $outputRoot -File -ErrorAction SilentlyContinue |
    Remove-Item -Force
Get-ChildItem -LiteralPath $outputPackage -File -ErrorAction SilentlyContinue |
    Remove-Item -Force
foreach ($entry in $sourceFiles.GetEnumerator()) {
    Copy-Item -LiteralPath $entry.Value `
        -Destination (Join-Path $outputPackage $entry.Key) -Force
}
foreach ($entry in $companionFiles.GetEnumerator()) {
    Copy-Item -LiteralPath $entry.Value `
        -Destination (Join-Path $outputRoot $entry.Key) -Force
}

$certificatePath = Join-Path $outputPackage 'NativeLdacDirectPdo.cer'
$certificate = Get-PfxCertificate -FilePath $certificatePath
$catalogSignature = Get-AuthenticodeSignature -LiteralPath `
    (Join-Path $outputPackage 'NativeLdacDirectPdo.cat')
$driverSignature = Get-AuthenticodeSignature -LiteralPath `
    (Join-Path $outputPackage 'NativeLdacDirectPdo.sys')
if ($null -eq $catalogSignature.SignerCertificate -or
    $null -eq $driverSignature.SignerCertificate -or
    $catalogSignature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint -or
    $driverSignature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint) {
    throw 'Candidate driver/catalog signature verification failed.'
}

$containerHeader = Get-Content -LiteralPath (Join-Path $projectRoot `
    'audio-endpoint\Source\Inc\nativeldac_remote_container.h') -Raw
$containerMatch = [regex]::Match(
    $containerHeader,
    'NATIVE_LDAC_REMOTE_CONTAINER_ID_STRING\s+\\\s*\r?\n\s*"([0-9A-Fa-f-]{36})"')
if (-not $containerMatch.Success) {
    throw 'Could not read the XM5 Container ID compiled into the candidate.'
}
$mediaHeader = Join-Path $projectRoot `
    'direct-pdo\include\nativeldac_direct_pdo_media_abi.h'
$pcmHeader = Join-Path $projectRoot `
    'audio-endpoint\Source\Inc\nativeldac_pcm_abi.h'
$packageEntries = foreach ($name in $sourceFiles.Keys) {
    Get-FileEntry -Path (Join-Path $outputPackage $name)
}
$companionEntries = foreach ($name in $companionFiles.Keys) {
    Get-FileEntry -Path (Join-Path $outputRoot $name)
}
$manifest = [ordered]@{
    manifest_version = 1
    created_at = (Get-Date).ToString('o')
    configuration = $Configuration
    source_commit = $gitCommit
    source_dirty = $sourceDirty
    installable = $true
    staged_only = $true
    install_script_included = $false
    hardware_id = 'BTHENUM\{0000110B-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
    service_name = 'NativeLdacDirectPdo'
    remote_container_id = $containerMatch.Groups[1].Value.ToUpperInvariant()
    certificate_subject = $certificate.Subject
    certificate_thumbprint = $certificate.Thumbprint
    driver_signature_status = [string]$driverSignature.Status
    catalog_signature_status = [string]$catalogSignature.Status
    component_abis = [ordered]@{
        direct_pdo_media = Get-DefineNumber $mediaHeader `
            'NLD_DIRECT_PDO_MEDIA_ABI_VERSION'
        native_pcm = Get-DefineNumber $pcmHeader 'NATIVE_LDAC_PCM_ABI_VERSION'
        preferred_format = Get-DefineNumber $pcmHeader `
            'NATIVE_LDAC_FORMAT_ABI_VERSION'
        link_state = Get-DefineNumber $pcmHeader `
            'NATIVE_LDAC_LINK_STATE_ABI_VERSION'
    }
    policy = [ordered]@{
        recovery_is_generation_bound = $true
        recovery_submits_bluetooth_open = $false
        remote_fault_requires_disconnect_reconnect_edge = $true
    }
    package_files = @($packageEntries)
    companion_files = @($companionEntries)
}
$manifestPath = Join-Path $outputRoot 'manifest.json'
$manifest | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $manifestPath -Encoding UTF8

powershell.exe -NoProfile -ExecutionPolicy Bypass -File $verifyScript `
    -CandidatePath $outputRoot
if ($LASTEXITCODE -ne 0) {
    throw "Candidate verification failed with exit code $LASTEXITCODE."
}

Write-Host "Built and staged Direct-PDO candidate: $outputRoot"
Write-Host 'The package is installable but no installer was created or run.'
Write-Host 'No driver, certificate store, device, process, or system setting was changed.'
