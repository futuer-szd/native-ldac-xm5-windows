# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-V1FileEntry {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$RelativePath
    )
    $item = Get-Item -LiteralPath $Path
    return [ordered]@{
        path = $RelativePath
        length = [long]$item.Length
        sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    }
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$sourceStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or
    $sourceCommit -notmatch '^[0-9a-fA-F]{40}$' -or
    $sourceStatus.Count -ne 0) {
    throw 'Refusing to build the V1 discovery handoff candidate from a dirty or unknown Git source.'
}

& (Join-Path $PSScriptRoot 'build-legacy-candidate.ps1') `
    -Configuration $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "Frozen LdacNative package build failed with exit code $LASTEXITCODE."
}
& (Join-Path $PSScriptRoot 'build-v1-engine-ready-observer.ps1') `
    -Configuration $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "V1 observer base build failed with exit code $LASTEXITCODE."
}

$cmakePath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$buildRoot = Join-Path $projectRoot 'build\protocol'
& $cmakePath --build $buildRoot --config $Configuration `
    --target v1_transport_discovery_worker v1_presence_agent
if ($LASTEXITCODE -ne 0) {
    throw "V1 discovery worker build failed with exit code $LASTEXITCODE."
}
$postCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$postStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or $postCommit -ne $sourceCommit -or
    $postStatus.Count -ne 0) {
    throw 'The Git source changed during the V1 discovery handoff build.'
}

$legacyRoot = Join-Path $projectRoot 'artifacts\legacy-candidate'
$baseRoot = Join-Path $projectRoot `
    'artifacts\v1-engine-ready-observer\candidate'
$outputRoot = Join-Path $projectRoot `
    'artifacts\v1-discovery-handoff\candidate'
$packageRoot = Join-Path $outputRoot 'package'
if (Test-Path -LiteralPath $outputRoot -PathType Container) {
    Remove-Item -LiteralPath $outputRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null

$files = @(
    [ordered]@{ source = Join-Path $legacyRoot 'package\LdacNative.inf'; relative = 'package\LdacNative.inf' },
    [ordered]@{ source = Join-Path $legacyRoot 'package\LdacNative.sys'; relative = 'package\LdacNative.sys' },
    [ordered]@{ source = Join-Path $legacyRoot 'package\ldacnative.cat'; relative = 'package\ldacnative.cat' },
    [ordered]@{ source = Join-Path $legacyRoot 'package\LdacNative.cer'; relative = 'package\LdacNative.cer' },
    [ordered]@{ source = Join-Path $buildRoot "$Configuration\v1_presence_agent.exe"; relative = 'v1_presence_agent.exe' },
    [ordered]@{ source = Join-Path $buildRoot "$Configuration\v1_transport_discovery_worker.exe"; relative = 'v1_transport_discovery_worker.exe' },
    [ordered]@{ source = Join-Path $baseRoot 'audio_endpoint_probe.exe'; relative = 'audio_endpoint_probe.exe' },
    [ordered]@{ source = Join-Path $baseRoot 'xm5_connection_probe.exe'; relative = 'xm5_connection_probe.exe' },
    [ordered]@{ source = Join-Path $baseRoot 'xm5_connection_probe.manifest.json'; relative = 'xm5_connection_probe.manifest.json' }
)
$entries = @()
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath $file.source -PathType Leaf)) {
        throw "Discovery handoff input is missing: $($file.source)"
    }
    $destination = Join-Path $outputRoot $file.relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) `
        -Force | Out-Null
    Copy-Item -LiteralPath $file.source -Destination $destination -Force
    $entries += Get-V1FileEntry -Path $destination `
        -RelativePath ([string]$file.relative)
}

$certificate = Get-PfxCertificate -FilePath `
    (Join-Path $packageRoot 'LdacNative.cer')
foreach ($signedPath in @(
        (Join-Path $packageRoot 'LdacNative.sys'),
        (Join-Path $packageRoot 'ldacnative.cat'))) {
    $signature = Get-AuthenticodeSignature -LiteralPath $signedPath
    if ($null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne
            $certificate.Thumbprint) {
        throw "Discovery handoff signature mismatch: $signedPath"
    }
}

$manifest = [ordered]@{
    manifest_version = 1
    source_commit = $sourceCommit
    source_dirty = $false
    configuration = $Configuration
    hardware_id = 'BTHENUM\{0000110B-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
    service_name = 'LdacNative'
    driver_abi = '0.5'
    certificate_thumbprint = $certificate.Thumbprint
    required_pcm_abi = 2
    required_presence_abi = 1
    capabilities = @(
        'persistent_LdacNative_function_driver_architecture',
        'transactional_temporary_handoff_for_gate',
        'exact_XM5_ACL_generation',
        'job_object_contained_discovery_worker',
        'single_signaling_open_attempt',
        'DISCOVER_and_capabilities_only',
        'distinct_capabilities_discovered_event',
        'no_retry',
        'no_SET_CONFIGURATION',
        'no_media_channel',
        'no_media_LinkState_write',
        'always_restore_original_A2DP',
        'no_reboot',
        'no_Bluetooth_toggle'
    )
    files = @($entries)
}
$manifestPath = Join-Path $outputRoot 'manifest.json'
$manifest | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "Built V1 discovery handoff candidate: $outputRoot"
Write-Host "Source commit: $sourceCommit"
Write-Host "Manifest: $manifestPath"
Write-Host 'No driver, certificate, process, Bluetooth request, or system setting was changed.'
