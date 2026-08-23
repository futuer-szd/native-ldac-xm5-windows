# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param([string]$CandidatePath)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-swd-endpoint-candidate-common.ps1')

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The transport-owned endpoint candidate verifier requires PowerShell 7.'
}

if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $CandidatePath = Join-Path $projectRoot `
        'artifacts\v1-volume-sync\endpoint-candidate'
}
$candidate = Get-V1SwdEndpointCandidate -CandidatePath $CandidatePath
$root = $candidate.root
$manifest = $candidate.manifest

$infPath = Join-Path $root 'package\NativeLdacSwdAudio.inf'
$infText = Get-Content -LiteralPath $infPath -Raw
foreach ($required in @(
        'Class       = MEDIA',
        'SWD\NativeLdacAudioXm5',
        'AddService=NativeLdacSwdAudio',
        'ServiceBinary=%13%\NativeLdacSwdAudio.sys',
        'PKEY_AudioDevice_NeverSetAsDefaultEndpoint',
        '0x00000107')) {
    if ($infText.IndexOf(
            $required,
            [StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw "The endpoint candidate INF is missing: $required"
    }
}
foreach ($forbidden in @(
        'ROOT\NativeLdacAudio',
        'BTHENUM\{0000110B')) {
    if ($infText.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The endpoint candidate INF contains a forbidden target: $forbidden"
    }
}

$hostPath = Join-Path $root 'v1_swd_endpoint_host.exe'
$help = @(& $hostPath --help 2>&1)
if ($LASTEXITCODE -ne 0 -or
    ($help -join "`n") -notmatch
        'reserved for a separately approved gate' -or
    ($help -join "`n") -notmatch
        'SWD\\NativeLdacAudioXm5' -or
    ($help -join "`n") -notmatch
        '--publish-presence --confirm-volume-observation' -or
    ($help -join "`n") -notmatch '--stop-event') {
    throw 'The endpoint host help contract failed.'
}
$refusal = @(& $hostPath --create 2>&1)
if ($LASTEXITCODE -eq 0 -or
    ($refusal -join "`n") -notmatch
        'confirm-endpoint-binding-probe') {
    throw 'The endpoint host creation path did not fail closed.'
}
$presenceRefusal = @(& $hostPath `
    --create `
    --parent 'BTHENUM\V1_REFUSAL_PROBE' `
    --container '{00112233-4455-6677-8899-AABBCCDDEEFF}' `
    --duration-seconds 10 `
    --confirm-endpoint-binding-probe `
    --publish-presence 2>&1)
if ($LASTEXITCODE -eq 0 -or
    ($presenceRefusal -join "`n") -notmatch
        'confirm-volume-observation') {
    throw 'The endpoint host presence path did not fail closed.'
}

$connectionProbe = Join-Path $root 'xm5_connection_probe.exe'
$connectionHelp = @(& $connectionProbe --help 2>&1)
if ($LASTEXITCODE -ne 0 -or
    ($connectionHelp -join "`n") -notmatch '--wait-acl-connect' -or
    ($connectionHelp -join "`n") -notmatch '--wait-acl-disconnect') {
    throw 'The endpoint candidate XM5 ACL observer contract failed.'
}

$transportProbe = Join-Path $root 'transport_probe.exe'
$transportHelp = @(& $transportProbe --help 2>&1)
if ($LASTEXITCODE -ne 0 -or
    ($transportHelp -join "`n") -notmatch
        '--hold-signaling-seconds 15-300' -or
    ($transportHelp -join "`n") -notmatch
        'no configuration, media channel, START') {
    throw 'The endpoint candidate capability-only signaling hold contract failed.'
}

$certificate = Get-PfxCertificate -FilePath (Join-Path $root `
    'package\NativeLdacSwdAudio.cer')
$catalogSignature = Get-AuthenticodeSignature -LiteralPath (Join-Path `
    $root 'package\NativeLdacSwdAudio.cat')
$driverSignature = Get-AuthenticodeSignature -LiteralPath (Join-Path `
    $root 'package\NativeLdacSwdAudio.sys')
foreach ($signature in @($catalogSignature, $driverSignature)) {
    if ($null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne
            $certificate.Thumbprint) {
        throw 'The endpoint candidate signer does not match its certificate.'
    }
}
if ([string]$manifest.certificate_thumbprint -cne
    $certificate.Thumbprint) {
    throw 'The endpoint candidate certificate thumbprint is invalid.'
}

Write-Host "V1 transport-owned endpoint candidate verified: $root"
Write-Host "Target: $($manifest.hardware_id), service $($manifest.service_name)"
Write-Host 'No installer is included and the endpoint host defaults to a read-only plan.'
