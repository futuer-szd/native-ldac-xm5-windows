# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$BundlePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($BundlePath)) {
    $BundlePath = Join-Path $projectRoot `
        'artifacts\direct-pdo\validation-bundle'
}
$BundlePath = [System.IO.Path]::GetFullPath($BundlePath)
$manifestPath = Join-Path $BundlePath 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Validation bundle manifest was not found: $manifestPath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ([int]$manifest.manifest_version -ne 1) {
    throw "Unsupported validation manifest version: $($manifest.manifest_version)"
}
if ($manifest.installable -ne $false -or
    $manifest.contains_inf -ne $false -or
    $manifest.contains_catalog -ne $false -or
    $manifest.contains_certificate -ne $false) {
    throw 'Validation bundle is not explicitly marked non-installable.'
}
if ([int]$manifest.component_abis.direct_pdo_media -ne 3 -or
    [int]$manifest.component_abis.native_pcm -ne 2 -or
    [int]$manifest.component_abis.preferred_format -ne 1 -or
    [int]$manifest.component_abis.link_state -ne 1) {
    throw 'Validation bundle component ABI set is unsupported.'
}
if ($manifest.policy.package_can_install -ne $false -or
    $manifest.policy.recovery_is_generation_bound -ne $true -or
    $manifest.policy.recovery_submits_bluetooth_open -ne $false -or
    $manifest.policy.remote_fault_requires_disconnect_reconnect_edge -ne $true) {
    throw 'Validation bundle safety policy is incomplete.'
}

$expectedNames = @(
    'NativeLdacDirectPdoPrototype.sys',
    'ldac_agent.exe',
    'ldac_direct_engine.exe',
    'audio_endpoint_probe.exe',
    'endpoint_volume_probe.exe'
)
$manifestNames = @($manifest.files | ForEach-Object { [string]$_.name })
$unexpectedManifestNames = @($manifestNames | Where-Object {
    $_ -notin $expectedNames
})
$missingManifestNames = @($expectedNames | Where-Object {
    $_ -notin $manifestNames
})
if ($unexpectedManifestNames.Count -ne 0 -or
    $missingManifestNames.Count -ne 0 -or
    $manifestNames.Count -ne $expectedNames.Count) {
    throw 'Validation bundle manifest does not contain the exact component set.'
}

$allowedNames = @($expectedNames + 'manifest.json')
$unexpectedDirectories = @(Get-ChildItem -LiteralPath $BundlePath -Directory)
if ($unexpectedDirectories.Count -ne 0) {
    throw "Validation bundle must not contain directories: $($unexpectedDirectories.Name -join ', ')"
}
$diskFiles = @(Get-ChildItem -LiteralPath $BundlePath -File)
$unexpectedDiskFiles = @($diskFiles | Where-Object {
    $_.Name -notin $allowedNames
})
if ($unexpectedDiskFiles.Count -ne 0) {
    throw "Unexpected validation bundle files: $($unexpectedDiskFiles.Name -join ', ')"
}
$forbiddenFiles = @(Get-ChildItem -LiteralPath $BundlePath -File -Recurse |
    Where-Object {
    $_.Extension -in @('.inf', '.cat', '.cer')
})
if ($forbiddenFiles.Count -ne 0) {
    throw "Installable material is forbidden in this bundle: $($forbiddenFiles.Name -join ', ')"
}

foreach ($entry in $manifest.files) {
    $path = Join-Path $BundlePath ([string]$entry.name)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Validation component is missing: $path"
    }
    $file = Get-Item -LiteralPath $path
    if ([long]$entry.length -ne $file.Length) {
        throw "Validation component length mismatch: $($entry.name)"
    }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if (-not $hash.Equals(
            [string]$entry.sha256,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Validation component hash mismatch: $($entry.name)"
    }
}

Write-Host "Validation bundle verified: $BundlePath"
Write-Host "Source commit: $($manifest.source_commit), dirty: $($manifest.source_dirty)"
Write-Host "ABI set: Direct-PDO $($manifest.component_abis.direct_pdo_media), PCM $($manifest.component_abis.native_pcm), format $($manifest.component_abis.preferred_format), link $($manifest.component_abis.link_state)"
Write-Host 'The bundle is hash-complete and contains no installation material.'
