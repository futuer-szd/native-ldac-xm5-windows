# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$coreBuildScript = Join-Path $PSScriptRoot 'build-direct-pdo-core.ps1'
$filtersProject = Join-Path $projectRoot 'audio-endpoint\Source\Filters\Filters.vcxproj'
$utilitiesProject = Join-Path $projectRoot 'audio-endpoint\Source\Utilities\Utilities.vcxproj'
$mainProject = Join-Path $projectRoot 'audio-endpoint\Source\Main\Main.vcxproj'
$mainOutput = Join-Path $projectRoot "audio-endpoint\Source\Main\x64\$Configuration"
$artifactRoot = Join-Path $projectRoot 'artifacts\direct-pdo\portcls-prototype'

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

powershell.exe -NoProfile -ExecutionPolicy Bypass -File $coreBuildScript `
    -Configuration $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "Direct-PDO core build failed with exit code $LASTEXITCODE."
}

foreach ($project in @($filtersProject, $utilitiesProject)) {
    & $msbuildPath $project /m /t:Build `
        "/p:Configuration=$Configuration" /p:Platform=x64
    if ($LASTEXITCODE -ne 0) {
        throw "Dependency build failed for $project with exit code $LASTEXITCODE."
    }
}

$prototypeCertificate = Join-Path $mainOutput 'NativeLdacDirectPdoPrototype.cer'
if (Test-Path -LiteralPath $prototypeCertificate -PathType Leaf) {
    Remove-Item -LiteralPath $prototypeCertificate -Force
}

& $msbuildPath $mainProject /m /t:Rebuild `
    "/p:Configuration=$Configuration" /p:Platform=x64 `
    /p:NativeLdacDirectPdoPrototype=true /p:SignMode=Off
if ($LASTEXITCODE -ne 0) {
    throw "PortCls direct-PDO prototype build failed with exit code $LASTEXITCODE."
}

$driverPath = Join-Path $mainOutput 'NativeLdacDirectPdoPrototype.sys'
if (-not (Test-Path -LiteralPath $driverPath -PathType Leaf)) {
    throw "PortCls prototype binary is missing: $driverPath"
}

$unexpectedPackageFiles = @(Get-ChildItem -LiteralPath $mainOutput -File |
    Where-Object {
        $_.BaseName -eq 'NativeLdacDirectPdoPrototype' -and
        $_.Extension -in @('.inf', '.cat', '.cer')
    })
if ($unexpectedPackageFiles.Count -ne 0) {
    throw "Refusing to stage an installable prototype: $($unexpectedPackageFiles.FullName -join ', ')"
}

New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
Get-ChildItem -LiteralPath $artifactRoot -File -ErrorAction SilentlyContinue |
    Remove-Item -Force
$artifactDriver = Join-Path $artifactRoot 'NativeLdacDirectPdoPrototype.sys'
Copy-Item -LiteralPath $driverPath -Destination $artifactDriver -Force

$manifest = [ordered]@{
    created_at = (Get-Date).ToString('o')
    configuration = $Configuration
    prototype = 'PortCls direct-PDO link skeleton'
    installable = $false
    contains_inf = $false
    contains_catalog = $false
    contains_certificate = $false
    hardware_id = $null
    service_name = $null
    driver = [ordered]@{
        name = 'NativeLdacDirectPdoPrototype.sys'
        length = (Get-Item -LiteralPath $artifactDriver).Length
        sha256 = (Get-FileHash -LiteralPath $artifactDriver -Algorithm SHA256).Hash
    }
}
$manifestPath = Join-Path $artifactRoot 'manifest.json'
$manifest | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "Built non-installable PortCls direct-PDO prototype: $artifactDriver"
Write-Host "Manifest: $manifestPath"
Write-Host 'No INF, CAT, certificate, hardware ID, service, device, driver binding, or system setting was created or changed.'
