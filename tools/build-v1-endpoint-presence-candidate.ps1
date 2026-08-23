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
    throw 'Refusing to build the V1 endpoint-presence candidate from a dirty or unknown Git source.'
}

& (Join-Path $PSScriptRoot 'build-audio-endpoint.ps1') `
    -Configuration $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "Audio endpoint build failed with exit code $LASTEXITCODE."
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
    throw "V1 companion build failed with exit code $LASTEXITCODE."
}

$postBuildCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$postBuildStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or
    $postBuildCommit -ne $sourceCommit -or
    $postBuildStatus.Count -ne 0) {
    throw 'The Git source changed during the V1 endpoint-presence build.'
}

$outputRoot = Join-Path $projectRoot `
    'artifacts\v1-endpoint-presence\candidate'
$previousManifestPath = Join-Path $outputRoot 'manifest.json'
if (Test-Path -LiteralPath $previousManifestPath -PathType Leaf) {
    $previousManifest = Get-Content -LiteralPath $previousManifestPath `
        -Raw | ConvertFrom-Json
    $previousSource = [string]$previousManifest.source_commit
    if ($previousSource -match '^[0-9a-fA-F]{40}$' -and
        -not $previousSource.Equals(
            $sourceCommit,
            [StringComparison]::OrdinalIgnoreCase)) {
        $rollbackRoot = Join-Path $projectRoot `
            "artifacts\v1-endpoint-presence\rollback\$previousSource"
        if (-not (Test-Path -LiteralPath $rollbackRoot)) {
            New-Item -ItemType Directory -Path $rollbackRoot -Force |
                Out-Null
            $previousItems = @(Get-ChildItem -LiteralPath $outputRoot -Force)
            foreach ($previousItem in $previousItems) {
                Copy-Item -LiteralPath $previousItem.FullName `
                    -Destination $rollbackRoot -Recurse -Force
            }
            Write-Host "Preserved previous endpoint candidate: $rollbackRoot"
        }
    }
}
$packageRoot = Join-Path $outputRoot 'package'
New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
$audioArtifactRoot = Join-Path $projectRoot 'artifacts\audio-endpoint'
$sourcePackage = Join-Path $audioArtifactRoot 'package'
$files = @(
    [ordered]@{
        source = Join-Path $sourcePackage 'NativeLdacAudio.inf'
        relative = 'package\NativeLdacAudio.inf'
    },
    [ordered]@{
        source = Join-Path $sourcePackage 'NativeLdacAudio.sys'
        relative = 'package\NativeLdacAudio.sys'
    },
    [ordered]@{
        source = Join-Path $sourcePackage 'NativeLdacAudio.cat'
        relative = 'package\NativeLdacAudio.cat'
    },
    [ordered]@{
        source = Join-Path $sourcePackage 'NativeLdacAudio.cer'
        relative = 'package\NativeLdacAudio.cer'
    },
    [ordered]@{
        source = Join-Path $buildRoot `
            "$Configuration\v1_presence_agent.exe"
        relative = 'v1_presence_agent.exe'
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
    },
    [ordered]@{
        source = Join-Path $buildRoot `
            "$Configuration\audio_endpoint_probe.exe"
        relative = 'audio_endpoint_probe.exe'
    }
)

$entries = @()
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath $file.source -PathType Leaf)) {
        throw "Candidate input is missing: $($file.source)"
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

$certificate = Get-PfxCertificate -FilePath `
    (Join-Path $packageRoot 'NativeLdacAudio.cer')
$driverSignature = Get-AuthenticodeSignature -LiteralPath `
    (Join-Path $packageRoot 'NativeLdacAudio.sys')
$catalogSignature = Get-AuthenticodeSignature -LiteralPath `
    (Join-Path $packageRoot 'NativeLdacAudio.cat')
if ($driverSignature.Status -ne 'Valid' -or
    $catalogSignature.Status -ne 'Valid' -or
    $driverSignature.SignerCertificate.Thumbprint -ne
        $certificate.Thumbprint -or
    $catalogSignature.SignerCertificate.Thumbprint -ne
        $certificate.Thumbprint) {
    throw 'The staged V1 endpoint-presence driver signatures are invalid.'
}

$manifest = [ordered]@{
    manifest_version = 1
    source_commit = $sourceCommit
    source_dirty = $false
    configuration = $Configuration
    hardware_id = 'ROOT\NativeLdacAudio'
    presence_abi = 1
    pcm_consumer_lease_abi = 1
    presence_lease_ms = 15000
    presence_heartbeat_ms = 5000
    certificate_thumbprint = $certificate.Thumbprint
    capabilities = @(
        'physical_presence_separate_from_media_link',
        'pcm_consumer_lease_separate_from_media_link',
        'exact_XM5_ACL_presence_lease',
        'no_default_output_change',
        'no_transport_open'
    )
    files = @($entries)
}
$manifestPath = Join-Path $outputRoot 'manifest.json'
$manifest | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "Built V1 endpoint-presence candidate: $outputRoot"
Write-Host "Source commit: $sourceCommit"
Write-Host "Certificate: $($certificate.Thumbprint)"
Write-Host "Manifest: $manifestPath"
Write-Host 'No driver was installed and no Bluetooth or system setting was changed.'
