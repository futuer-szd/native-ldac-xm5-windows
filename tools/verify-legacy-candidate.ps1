# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param([string]$CandidatePath)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-FileEntry {
    param(
        [Parameter(Mandatory = $true)]$Entry,
        [Parameter(Mandatory = $true)][string]$Root
    )

    $path = Join-Path $Root ([string]$Entry.name)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Legacy candidate component is missing: $path"
    }
    $file = Get-Item -LiteralPath $path
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($file.Length -ne [long]$Entry.length -or
        -not $hash.Equals(
            [string]$Entry.sha256,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Legacy candidate component mismatch: $($Entry.name)"
    }
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $projectRoot 'artifacts\legacy-candidate'
}
$CandidatePath = [System.IO.Path]::GetFullPath($CandidatePath)
$packageRoot = Join-Path $CandidatePath 'package'
$manifestPath = Join-Path $CandidatePath 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Legacy candidate manifest was not found: $manifestPath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$expectedHardwareId = 'BTHENUM\{0000110B-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
if ([int]$manifest.manifest_version -ne 3 -or
    $manifest.installable -ne $true -or
    $manifest.staged_only -ne $true -or
    $manifest.install_script_included -ne $false -or
    [string]$manifest.architecture -ne 'legacy_split_user_mode_avdtp' -or
    -not ([string]$manifest.hardware_id).Equals(
        $expectedHardwareId,
        [StringComparison]::OrdinalIgnoreCase) -or
    [string]$manifest.service_name -ne 'LdacNative' -or
    [string]$manifest.driver_abi -ne '0.5' -or
    [string]$manifest.source_commit -notmatch '^[0-9a-fA-F]{40}$' -or
    $manifest.source_dirty -ne $false -or
    [string]$manifest.last_verified_driver_commit -ne
        '5ed098f8ebfd6e65fa119add3e86f15de8ad1a47' -or
    [string]$manifest.last_verified_driver_tree -ne
        '1e2706b4abaabd2abcaa5796b4be2bc11dfd36da' -or
    [string]$manifest.approved_diagnostic_driver_commit -ne
        'f3621916841ead3aff0342604712c21477b33a35' -or
    [string]$manifest.approved_diagnostic_driver_tree -ne
        '85a0b46231ae2f3212e6616346e2d6905314f0ff' -or
    [string]$manifest.current_driver_tree -ne
        [string]$manifest.approved_diagnostic_driver_tree) {
    throw 'Legacy candidate identity or clean-source policy is invalid.'
}
if ($manifest.policy.direct_pdo_included -ne $false -or
    [int]$manifest.policy.background_open_attempts -ne 1 -or
    $manifest.policy.unexpected_exit_requires_fresh_transport_generation -ne
        $true -or
    [string]$manifest.policy.first_hardware_gate -ne
        'clean_baseline_install_reboot_acl_confirmed_single_open_diagnostic' -or
    $manifest.policy.requires_clean_original_a2dp -ne $true -or
    $manifest.policy.requires_reboot_before_avdtp -ne $true -or
    $manifest.policy.hot_swap_playback_forbidden -ne $true -or
    $manifest.policy.open_failure_telemetry_required -ne $true -or
    $manifest.policy.requires_acl_connect_event -ne $true -or
    $manifest.policy.requires_operator_power_confirmation -ne $true) {
    throw 'Legacy candidate safety policy is incomplete.'
}

$expectedPackageNames = @(
    'LdacNative.inf',
    'LdacNative.sys',
    'ldacnative.cat',
    'LdacNative.cer'
)
$expectedCompanionNames = @(
    'ldac_agent.exe',
    'transport_probe.exe',
    'xm5_connection_probe.exe',
    'xm5_connection_probe.manifest.json'
)
$packageFiles = @(Get-ChildItem -LiteralPath $packageRoot -File)
$rootFiles = @(Get-ChildItem -LiteralPath $CandidatePath -File)
$rootDirectories = @(Get-ChildItem -LiteralPath $CandidatePath -Directory)
if ($packageFiles.Count -ne $expectedPackageNames.Count -or
    @($packageFiles | Where-Object {
        $_.Name -notin $expectedPackageNames
    }).Count -ne 0 -or
    $rootFiles.Count -ne ($expectedCompanionNames.Count + 1) -or
    @($rootFiles | Where-Object {
        $_.Name -notin ($expectedCompanionNames + 'manifest.json')
    }).Count -ne 0 -or
    $rootDirectories.Count -ne 1 -or
    $rootDirectories[0].Name -ne 'package') {
    throw 'Legacy candidate contains an unexpected file or directory.'
}
$manifestPackageNames = @($manifest.package_files | ForEach-Object {
    [string]$_.name
})
$manifestCompanionNames = @($manifest.companion_files | ForEach-Object {
    [string]$_.name
})
if ($manifestPackageNames.Count -ne $expectedPackageNames.Count -or
    @($expectedPackageNames | Where-Object {
        $_ -notin $manifestPackageNames
    }).Count -ne 0 -or
    $manifestCompanionNames.Count -ne $expectedCompanionNames.Count -or
    @($expectedCompanionNames | Where-Object {
        $_ -notin $manifestCompanionNames
    }).Count -ne 0) {
    throw 'Legacy candidate manifest file set is incomplete.'
}
foreach ($entry in $manifest.package_files) {
    Test-FileEntry -Entry $entry -Root $packageRoot
}
foreach ($entry in $manifest.companion_files) {
    Test-FileEntry -Entry $entry -Root $CandidatePath
}

$connectionProbePath = Join-Path $CandidatePath `
    'xm5_connection_probe.exe'
$connectionProbeManifest = Get-Content -LiteralPath `
    (Join-Path $CandidatePath 'xm5_connection_probe.manifest.json') `
    -Raw | ConvertFrom-Json
$connectionProbeHash = (Get-FileHash -LiteralPath $connectionProbePath `
    -Algorithm SHA256).Hash
$connectionProbeCapabilities = @(
    $connectionProbeManifest.capabilities | ForEach-Object {
        [string]$_
    })
if ([int]$connectionProbeManifest.manifest_version -ne 3 -or
    [string]$connectionProbeManifest.source_commit -ne
        [string]$manifest.source_commit -or
    $connectionProbeManifest.source_dirty -ne $false -or
    [string]$connectionProbeManifest.file_name -ne
        'xm5_connection_probe.exe' -or
    'BluetoothFindFirstDevice_fConnected_no_inquiry' -notin
        $connectionProbeCapabilities -or
    'GUID_BLUETOOTH_HCI_EVENT_acl_transition' -notin
        $connectionProbeCapabilities -or
    'BluetoothIsConnectable_radio_state' -notin
        $connectionProbeCapabilities -or
    -not $connectionProbeHash.Equals(
        [string]$connectionProbeManifest.sha256,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The bundled read-only XM5 connection probe is not from the candidate source.'
}

$infText = Get-Content -LiteralPath `
    (Join-Path $packageRoot 'LdacNative.inf') -Raw
if ($infText -notmatch '(?im)^Class\s*=\s*Bluetooth\s*$' -or
    $infText -notmatch '(?im)^AddService\s*=\s*LdacNative\s*,' -or
    $infText.IndexOf(
        $expectedHardwareId,
        [StringComparison]::OrdinalIgnoreCase) -lt 0 -or
    $infText -match '(?im)NativeLdacDirectPdo|ROOT\\NativeLdac') {
    throw 'Legacy candidate INF target identity is invalid.'
}
$certificate = Get-PfxCertificate -FilePath `
    (Join-Path $packageRoot 'LdacNative.cer')
foreach ($signaturePath in @(
        (Join-Path $packageRoot 'ldacnative.cat'),
        (Join-Path $packageRoot 'LdacNative.sys'))) {
    $signature = Get-AuthenticodeSignature -LiteralPath $signaturePath
    if ($null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint) {
        throw "Legacy candidate signer mismatch: $signaturePath"
    }
}
if ([string]$manifest.certificate_thumbprint -ne
    $certificate.Thumbprint) {
    throw 'Legacy candidate certificate thumbprint is invalid.'
}

Write-Host "Legacy candidate verified: $CandidatePath"
Write-Host "Source commit: $($manifest.source_commit)"
Write-Host "Target: $($manifest.hardware_id), service LdacNative"
Write-Host 'Direct-PDO is absent; package and companions are hash-complete.'
