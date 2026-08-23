# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$CheckpointPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The V1 golden checkpoint verifier requires PowerShell 7.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CheckpointPath)) {
    $latestPath = Join-Path $projectRoot 'artifacts\v1-golden\latest.txt'
    if (-not (Test-Path -LiteralPath $latestPath -PathType Leaf)) {
        throw 'No V1 golden latest.txt exists.'
    }
    $CheckpointPath = (Get-Content -LiteralPath $latestPath -Raw).Trim()
}
$CheckpointPath = [IO.Path]::GetFullPath($CheckpointPath)
$manifestPath = Join-Path $CheckpointPath 'manifest.json'
$manifestHashPath = Join-Path $CheckpointPath 'manifest.sha256'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $manifestHashPath -PathType Leaf)) {
    throw 'The checkpoint manifest or detached hash is missing.'
}

$expectedManifestHash = ((Get-Content -LiteralPath $manifestHashPath `
    -Raw).Trim() -split '\s+')[0]
$actualManifestHash = (Get-FileHash -LiteralPath $manifestPath `
    -Algorithm SHA256).Hash
if ($actualManifestHash -cne $expectedManifestHash) {
    throw 'The checkpoint manifest hash does not match.'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw |
    ConvertFrom-Json
if ([int]$manifest.manifest_version -ne 1 -or
    [string]$manifest.restore_contract -cne `
        'functional-equivalence-with-exact-driver-package-hashes') {
    throw 'The checkpoint manifest contract is unsupported.'
}

foreach ($package in @($manifest.packages)) {
    $packageRoot = Join-Path $CheckpointPath ([string]$package.package_path)
    foreach ($file in @($package.files)) {
        $path = Join-Path $packageRoot ([string]$file.path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Checkpoint file is missing: $path"
        }
        $item = Get-Item -LiteralPath $path
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        if ([long]$item.Length -ne [long]$file.length -or
            $hash -cne [string]$file.sha256) {
            throw "Checkpoint file integrity failed: $path"
        }
    }
}

$bundlePath = Join-Path $CheckpointPath ([string]$manifest.source.bundle)
$archivePath = Join-Path $CheckpointPath ([string]$manifest.source.archive)
foreach ($sourceFile in @(
        @($bundlePath, [string]$manifest.source.bundle_sha256),
        @($archivePath, [string]$manifest.source.archive_sha256))) {
    if (-not (Test-Path -LiteralPath $sourceFile[0] -PathType Leaf)) {
        throw "Checkpoint source file is missing: $($sourceFile[0])"
    }
    $hash = (Get-FileHash -LiteralPath $sourceFile[0] `
        -Algorithm SHA256).Hash
    if ($hash -cne $sourceFile[1]) {
        throw "Checkpoint source integrity failed: $($sourceFile[0])"
    }
}
& git.exe bundle verify $bundlePath | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw 'The checkpoint Git bundle is invalid.'
}

Write-Host 'V1 golden checkpoint integrity verified.'
Write-Host "Checkpoint: $CheckpointPath"
Write-Host "Source: $($manifest.source.commit)"
Write-Host "Packages: $(@($manifest.packages).Count)"
Write-Host 'This verification was read-only.'
