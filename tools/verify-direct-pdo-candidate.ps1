# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$CandidatePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-FileEntry {
    param(
        [Parameter(Mandatory = $true)]$Entry,
        [Parameter(Mandatory = $true)][string]$Root
    )

    $path = Join-Path $Root ([string]$Entry.name)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Candidate component is missing: $path"
    }
    $file = Get-Item -LiteralPath $path
    if ($file.Length -ne [long]$Entry.length) {
        throw "Candidate component length mismatch: $($Entry.name)"
    }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if (-not $hash.Equals(
            [string]$Entry.sha256,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Candidate component hash mismatch: $($Entry.name)"
    }
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $projectRoot 'artifacts\direct-pdo\candidate'
}
$CandidatePath = [System.IO.Path]::GetFullPath($CandidatePath)
$manifestPath = Join-Path $CandidatePath 'manifest.json'
$packagePath = Join-Path $CandidatePath 'package'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Direct-PDO candidate manifest was not found: $manifestPath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ([int]$manifest.manifest_version -ne 1 -or
    $manifest.installable -ne $true -or
    $manifest.staged_only -ne $true -or
    $manifest.install_script_included -ne $false) {
    throw 'Candidate staging policy is invalid.'
}
$expectedHardwareId = 'BTHENUM\{0000110B-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
if (-not ([string]$manifest.hardware_id).Equals(
        $expectedHardwareId,
        [StringComparison]::OrdinalIgnoreCase) -or
    [string]$manifest.service_name -ne 'NativeLdacDirectPdo') {
    throw 'Candidate target hardware ID or service name is invalid.'
}
if ([int]$manifest.component_abis.direct_pdo_media -ne 3 -or
    [int]$manifest.component_abis.native_pcm -ne 2 -or
    [int]$manifest.component_abis.preferred_format -ne 1 -or
    [int]$manifest.component_abis.link_state -ne 1) {
    throw 'Candidate component ABI set is unsupported.'
}
if ($manifest.policy.recovery_is_generation_bound -ne $true -or
    $manifest.policy.recovery_submits_bluetooth_open -ne $false -or
    $manifest.policy.remote_fault_requires_disconnect_reconnect_edge -ne $true) {
    throw 'Candidate safety policy is incomplete.'
}
$parsedContainerId = [Guid]::Empty
if (-not [Guid]::TryParse(
        [string]$manifest.remote_container_id,
        [ref]$parsedContainerId) -or
    $parsedContainerId -eq [Guid]::Empty -or
    [string]$manifest.source_commit -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'Candidate source commit or remote Container ID is invalid.'
}

$expectedPackageNames = @(
    'NativeLdacDirectPdo.inf',
    'NativeLdacDirectPdo.sys',
    'NativeLdacDirectPdo.cat',
    'NativeLdacDirectPdo.cer'
)
$manifestPackageNames = @($manifest.package_files | ForEach-Object {
    [string]$_.name
})
if ($manifestPackageNames.Count -ne $expectedPackageNames.Count -or
    @($manifestPackageNames | Where-Object {
        $_ -notin $expectedPackageNames
    }).Count -ne 0 -or
    @($expectedPackageNames | Where-Object {
        $_ -notin $manifestPackageNames
    }).Count -ne 0) {
    throw 'Candidate manifest does not contain the exact package file set.'
}
$packageFiles = @(Get-ChildItem -LiteralPath $packagePath -File)
$unexpectedPackageFiles = @($packageFiles | Where-Object {
    $_.Name -notin $expectedPackageNames
})
$missingPackageFiles = @($expectedPackageNames | Where-Object {
    $_ -notin $packageFiles.Name
})
if ($unexpectedPackageFiles.Count -ne 0 -or
    $missingPackageFiles.Count -ne 0 -or
    $packageFiles.Count -ne $expectedPackageNames.Count) {
    throw 'Candidate driver package does not contain the exact four-file set.'
}
if (@(Get-ChildItem -LiteralPath $packagePath -Directory).Count -ne 0) {
    throw 'Candidate driver package must not contain directories.'
}

$expectedCompanionNames = @(
    'ldac_agent.exe',
    'ldac_direct_engine.exe',
    'audio_endpoint_probe.exe',
    'endpoint_volume_probe.exe'
)
$manifestCompanionNames = @($manifest.companion_files | ForEach-Object {
    [string]$_.name
})
if ($manifestCompanionNames.Count -ne $expectedCompanionNames.Count -or
    @($manifestCompanionNames | Where-Object {
        $_ -notin $expectedCompanionNames
    }).Count -ne 0 -or
    @($expectedCompanionNames | Where-Object {
        $_ -notin $manifestCompanionNames
    }).Count -ne 0) {
    throw 'Candidate manifest does not contain the exact companion file set.'
}
$rootFiles = @(Get-ChildItem -LiteralPath $CandidatePath -File)
$allowedRootNames = @($expectedCompanionNames + 'manifest.json')
if (@($rootFiles | Where-Object { $_.Name -notin $allowedRootNames }).Count -ne 0 -or
    @($expectedCompanionNames | Where-Object { $_ -notin $rootFiles.Name }).Count -ne 0) {
    throw 'Candidate root does not contain the exact companion tool set.'
}
$rootDirectories = @(Get-ChildItem -LiteralPath $CandidatePath -Directory)
if ($rootDirectories.Count -ne 1 -or $rootDirectories[0].Name -ne 'package') {
    throw 'Candidate root must contain only the package directory.'
}

foreach ($entry in $manifest.package_files) {
    Test-FileEntry -Entry $entry -Root $packagePath
}
foreach ($entry in $manifest.companion_files) {
    Test-FileEntry -Entry $entry -Root $CandidatePath
}

$infPath = Join-Path $packagePath 'NativeLdacDirectPdo.inf'
$infText = Get-Content -LiteralPath $infPath -Raw
if ($infText -notmatch '(?im)^Class\s*=\s*Bluetooth\s*$' -or
    $infText -notmatch '(?im)^AddService\s*=\s*NativeLdacDirectPdo\s*,' -or
    $infText.IndexOf($expectedHardwareId, [StringComparison]::OrdinalIgnoreCase) -lt 0 -or
    $infText -match '(?im)ROOT\\NativeLdac') {
    throw 'Candidate INF target identity is invalid.'
}

$certificatePath = Join-Path $packagePath 'NativeLdacDirectPdo.cer'
$catalogPath = Join-Path $packagePath 'NativeLdacDirectPdo.cat'
$driverPath = Join-Path $packagePath 'NativeLdacDirectPdo.sys'
$certificate = Get-PfxCertificate -FilePath $certificatePath
$catalogSignature = Get-AuthenticodeSignature -LiteralPath $catalogPath
$driverSignature = Get-AuthenticodeSignature -LiteralPath $driverPath
foreach ($signature in @($catalogSignature, $driverSignature)) {
    if ($null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint) {
        throw 'Candidate SYS/CAT signer does not match its staged certificate.'
    }
}
if ([string]$manifest.certificate_thumbprint -ne $certificate.Thumbprint) {
    throw 'Candidate manifest certificate thumbprint is invalid.'
}

Write-Host "Direct-PDO candidate verified: $CandidatePath"
Write-Host "Source commit: $($manifest.source_commit), dirty: $($manifest.source_dirty)"
Write-Host "Target: $($manifest.hardware_id), service $($manifest.service_name)"
Write-Host 'The package is signed and hash-complete; no installer is included.'
