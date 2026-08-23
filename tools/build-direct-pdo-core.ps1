# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$projectPath = Join-Path $projectRoot 'direct-pdo\NativeLdacDirectPdoCore.vcxproj'
$projectOutput = Join-Path $projectRoot "direct-pdo\x64\$Configuration"
$artifactRoot = Join-Path $projectRoot 'artifacts\direct-pdo'

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
if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
    throw "Direct-PDO core project was not found: $projectPath"
}

& $msbuildPath $projectPath /m /t:Rebuild `
    "/p:Configuration=$Configuration" /p:Platform=x64
if ($LASTEXITCODE -ne 0) {
    throw "Direct-PDO core build failed with exit code $LASTEXITCODE."
}

$libraryPath = Join-Path $projectOutput 'NativeLdacDirectPdoCore.lib'
$headerPaths = @(
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_avdtp_transaction.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_bth_indication_contract.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_bth_transfer_contract.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_direct_pdo_arbiter.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_direct_pdo_arbiter_contract.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_direct_pdo_control.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_direct_pdo_control_abi.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_direct_pdo_media_abi.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_media_watchdog_contract.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_direct_pdo_public.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_direct_pdo_contract.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_direct_pdo_diagnostic.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_direct_pdo_diagnostic_contract.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_direct_pdo_dispatch_contract.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_direct_pdo_dispatcher.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_direct_pdo_preemption_contract.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_bth_owner_contract.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_bth_address_contract.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_bth_signaling_contract.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_bth_profile.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_bth_request.h'),
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_bth_signaling.h'),
    (Join-Path $projectRoot 'protocol\include\ldac_native\avdtp.h'),
    (Join-Path $projectRoot 'protocol\include\ldac_native\ldac_codec.h')
)
if (-not (Test-Path -LiteralPath $libraryPath -PathType Leaf)) {
    throw "Direct-PDO core library is missing: $libraryPath"
}

$forbidden = @(Get-ChildItem -LiteralPath $projectOutput -File |
    Where-Object { $_.Extension -in @('.sys', '.inf', '.cat', '.cer') })
if ($forbidden.Count -ne 0) {
    throw "Refusing to stage installable direct-PDO output: $($forbidden.FullName -join ', ')"
}

New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
$artifactLibrary = Join-Path $artifactRoot 'NativeLdacDirectPdoCore.lib'
Copy-Item -LiteralPath $libraryPath -Destination $artifactLibrary -Force
$artifactHeaders = foreach ($headerPath in $headerPaths) {
    $artifactHeader = Join-Path $artifactRoot (Split-Path $headerPath -Leaf)
    Copy-Item -LiteralPath $headerPath -Destination $artifactHeader -Force
    [ordered]@{
        name = (Split-Path $artifactHeader -Leaf)
        length = (Get-Item -LiteralPath $artifactHeader).Length
        sha256 = (Get-FileHash -LiteralPath $artifactHeader -Algorithm SHA256).Hash
    }
}

$manifest = [ordered]@{
    created_at = (Get-Date).ToString('o')
    configuration = $Configuration
    installable = $false
    contains_driver_binary = $false
    contains_inf = $false
    library = [ordered]@{
        name = 'NativeLdacDirectPdoCore.lib'
        length = (Get-Item -LiteralPath $artifactLibrary).Length
        sha256 = (Get-FileHash -LiteralPath $artifactLibrary -Algorithm SHA256).Hash
    }
    headers = @($artifactHeaders)
}
$manifestPath = Join-Path $artifactRoot 'manifest.json'
$manifest | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "Built non-installable direct-PDO core: $artifactLibrary"
Write-Host "Manifest: $manifestPath"
Write-Host 'No INF, CAT, SYS, certificate, driver, service, device, or system setting was created or changed.'
