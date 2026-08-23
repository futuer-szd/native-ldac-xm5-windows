# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('Release')][string]$Configuration = 'Release',
    [string]$OutputPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'v1-inbound-signaling-common.ps1')

function Get-V1InboundFileEntry {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$RelativePath
    )
    $item = Get-Item -LiteralPath $Path
    [ordered]@{
        path = $RelativePath
        length = [long]$item.Length
        sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    }
}

$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$commit = (& git.exe -C $root rev-parse HEAD).Trim()
$driverTree = (& git.exe -C $root rev-parse HEAD:driver).Trim()
$status = @(& git.exe -C $root status --porcelain)
if ($LASTEXITCODE -ne 0 -or $status.Count -ne 0 -or
    $commit -notmatch '^[0-9a-fA-F]{40}$' -or
    $driverTree -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'Refusing to build the inbound-signaling candidate from a dirty or unknown Git source.'
}

& (Join-Path $PSScriptRoot 'build-legacy-candidate.ps1') `
    -Configuration $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "The signed LdacNative package build failed with exit code $LASTEXITCODE."
}
$postCommit = (& git.exe -C $root rev-parse HEAD).Trim()
$postStatus = @(& git.exe -C $root status --porcelain)
if ($postCommit -ne $commit -or $postStatus.Count -ne 0) {
    throw 'The Git source changed during the inbound-signaling candidate build.'
}

$legacy = Join-Path $root 'artifacts\legacy-candidate'
$output = if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    Join-Path $root 'artifacts\v1-inbound-signaling\candidate'
} else {
    [System.IO.Path]::GetFullPath($OutputPath)
}
$artifactPrefix = (Join-Path $root 'artifacts').TrimEnd('\') + '\'
if (-not $output.StartsWith(
        $artifactPrefix,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Inbound-signaling candidate path escaped artifacts: $output"
}
if (Test-Path -LiteralPath $output -PathType Container) {
    Remove-Item -LiteralPath $output -Recurse -Force
}
New-Item -ItemType Directory -Path (Join-Path $output 'package') `
    -Force | Out-Null
$files = @(
    @{ source = Join-Path $legacy 'package\LdacNative.inf'; relative = 'package\LdacNative.inf' },
    @{ source = Join-Path $legacy 'package\LdacNative.sys'; relative = 'package\LdacNative.sys' },
    @{ source = Join-Path $legacy 'package\ldacnative.cat'; relative = 'package\ldacnative.cat' },
    @{ source = Join-Path $legacy 'package\LdacNative.cer'; relative = 'package\LdacNative.cer' },
    @{ source = Join-Path $legacy 'transport_probe.exe'; relative = 'transport_probe.exe' },
    @{ source = Join-Path $legacy 'xm5_connection_probe.exe'; relative = 'xm5_connection_probe.exe' },
    @{ source = Join-Path $legacy 'xm5_connection_probe.manifest.json'; relative = 'xm5_connection_probe.manifest.json' })
$entries = @()
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath $file.source -PathType Leaf)) {
        throw "Inbound-signaling candidate input is missing: $($file.source)"
    }
    $destination = Join-Path $output $file.relative
    Copy-Item -LiteralPath $file.source -Destination $destination -Force
    $entries += Get-V1InboundFileEntry -Path $destination `
        -RelativePath ([string]$file.relative)
}

$certificate = Get-PfxCertificate -FilePath `
    (Join-Path $output 'package\LdacNative.cer')
foreach ($signed in @(
        (Join-Path $output 'package\LdacNative.sys'),
        (Join-Path $output 'package\ldacnative.cat'))) {
    $signature = Get-AuthenticodeSignature -LiteralPath $signed
    if ($null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne
            $certificate.Thumbprint) {
        throw "Inbound-signaling candidate signature mismatch: $signed"
    }
}

$manifest = [ordered]@{
    manifest_version = 1
    transport_policy_version = $script:V1InboundSignalingPolicyVersion
    source_commit = $commit
    driver_tree = $driverTree
    source_dirty = $false
    configuration = $Configuration
    service_name = 'LdacNative'
    driver_abi = '0.5'
    required_ready_flags = $script:V1InboundReadyFlags
    certificate_thumbprint = $certificate.Thumbprint
    capabilities = @(
        'fixed_avdtp_psm_inbound_server',
        'reuse_inbound_signaling_channel',
        'read_only_handles_preserve_inbound_channel',
        'single_discover_no_media',
        'hci_proves_no_outbound_psm25_request',
        'same_service_pnp_update_with_rollback',
        'no_windows_bluetooth_toggle',
        'no_reboot_by_default')
    files = @($entries)
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content `
    -LiteralPath (Join-Path $output 'manifest.json') -Encoding UTF8
$null = Get-V1InboundSignalingCandidate -CandidatePath $output

Write-Host "Built V1 inbound-signaling candidate: $output"
Write-Host "Source commit: $commit"
Write-Host 'No driver, service, process, Bluetooth request, or system setting was changed.'
