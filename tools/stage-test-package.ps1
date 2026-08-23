# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$driverBuild = Join-Path $projectRoot "driver\x64\$Configuration"
$driverPackage = Join-Path $driverBuild 'LdacNative'
$probePath = Join-Path $projectRoot "build\protocol\$Configuration\transport_probe.exe"
$outputRoot = Join-Path $projectRoot 'artifacts\driver-test'
$packageRoot = Join-Path $outputRoot 'package'

$requiredFiles = @(
    (Join-Path $driverPackage 'LdacNative.inf'),
    (Join-Path $driverPackage 'LdacNative.sys'),
    (Join-Path $driverPackage 'ldacnative.cat'),
    (Join-Path $driverBuild 'LdacNative.cer'),
    $probePath
)

$missingFiles = @($requiredFiles | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
if ($missingFiles.Count -ne 0) {
    throw "Build outputs are missing. Rebuild $Configuration first: $($missingFiles -join ', ')"
}

New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $driverPackage 'LdacNative.inf') -Destination $packageRoot -Force
Copy-Item -LiteralPath (Join-Path $driverPackage 'LdacNative.sys') -Destination $packageRoot -Force
Copy-Item -LiteralPath (Join-Path $driverPackage 'ldacnative.cat') -Destination $packageRoot -Force
Copy-Item -LiteralPath (Join-Path $driverBuild 'LdacNative.cer') -Destination $packageRoot -Force
Copy-Item -LiteralPath $probePath -Destination $outputRoot -Force

$certificate = Get-PfxCertificate -FilePath (Join-Path $packageRoot 'LdacNative.cer')
$catalogSignature = Get-AuthenticodeSignature -LiteralPath (Join-Path $packageRoot 'ldacnative.cat')
if ($null -eq $catalogSignature.SignerCertificate) {
    throw 'The staged catalog does not contain a signer certificate.'
}
if ($catalogSignature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint) {
    throw 'The catalog signer does not match LdacNative.cer.'
}

$packageFiles = @(Get-ChildItem -LiteralPath $packageRoot -File | Sort-Object Name)
$fileEntries = @($packageFiles | ForEach-Object {
    $hash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
    [ordered]@{
        name = $_.Name
        length = $_.Length
        sha256 = $hash.Hash
    }
})
$probeHash = Get-FileHash -LiteralPath (Join-Path $outputRoot 'transport_probe.exe') -Algorithm SHA256
$manifest = [ordered]@{
    created_at = (Get-Date).ToString('o')
    configuration = $Configuration
    certificate_subject = $certificate.Subject
    certificate_thumbprint = $certificate.Thumbprint
    package_files = @($fileEntries)
    probe_sha256 = $probeHash.Hash
}
$manifestPath = Join-Path $outputRoot 'manifest.json'
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "Staged test package: $packageRoot"
Write-Host "Probe: $(Join-Path $outputRoot 'transport_probe.exe')"
Write-Host "Certificate: $($certificate.Thumbprint)"
