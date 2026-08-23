# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'native-ldac-baseline-common.ps1')

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$candidateRoot = Join-Path $projectRoot `
    'artifacts\v1-endpoint-presence\candidate'
$manifestPath = Join-Path $candidateRoot 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'The V1 endpoint-presence candidate manifest is missing.'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$capabilities = @($manifest.capabilities | ForEach-Object { [string]$_ })
if ([int]$manifest.manifest_version -ne 1 -or
    $manifest.source_dirty -ne $false -or
    [string]$manifest.hardware_id -ne 'ROOT\NativeLdacAudio' -or
    [int]$manifest.presence_abi -ne 1 -or
    [int]$manifest.pcm_consumer_lease_abi -ne 1 -or
    [int]$manifest.presence_lease_ms -ne 15000 -or
    [int]$manifest.presence_heartbeat_ms -ne 5000 -or
    'physical_presence_separate_from_media_link' -notin $capabilities -or
    'pcm_consumer_lease_separate_from_media_link' -notin $capabilities -or
    'exact_XM5_ACL_presence_lease' -notin $capabilities -or
    'no_default_output_change' -notin $capabilities -or
    'no_transport_open' -notin $capabilities) {
    throw 'The V1 endpoint-presence manifest contract is invalid.'
}
foreach ($file in @($manifest.files)) {
    $path = Join-Path $candidateRoot ([string]$file.path)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Candidate file is missing: $($file.path)"
    }
    $item = Get-Item -LiteralPath $path
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ([long]$item.Length -ne [long]$file.length -or
        -not $hash.Equals(
            [string]$file.sha256,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Candidate file failed its hash check: $($file.path)"
    }
}

$certificatePath = Join-Path $candidateRoot `
    'package\NativeLdacAudio.cer'
$certificate = Get-PfxCertificate -FilePath $certificatePath
if (-not $certificate.Thumbprint.Equals(
        [string]$manifest.certificate_thumbprint,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Candidate certificate thumbprint does not match its manifest.'
}

$latestBackupPath = Join-Path $projectRoot `
    'artifacts\driver-test\latest-backup.txt'
if (-not (Test-Path -LiteralPath $latestBackupPath -PathType Leaf)) {
    throw 'Original A2DP latest-backup.txt is missing.'
}
$backupPath = (Get-Content -LiteralPath $latestBackupPath -Raw).Trim()
$baseline = Get-NativeLdacBaselineSnapshot -BackupPath $backupPath
if (-not $baseline.clean_original_a2dp) {
    Write-NativeLdacBaselineSummary -Snapshot $baseline
    throw 'The candidate requires the clean original-A2DP baseline.'
}
if (-not $baseline.test_signing_active) {
    throw 'TESTSIGNING is not active for this test-signed candidate.'
}

$probePath = Join-Path $candidateRoot 'xm5_connection_probe.exe'
$xm5State = Get-NativeLdacXm5BluetoothState `
    -ProbePath $probePath `
    -ExpectedSourceCommit ([string]$manifest.source_commit)
if ($xm5State -ne 'disconnected') {
    throw 'Turn off the XM5 and wait until it is physically disconnected.'
}

Write-Host 'V1 endpoint-presence readiness preflight passed.'
Write-Host "Candidate source: $($manifest.source_commit)"
Write-Host 'Original A2DP: clean and healthy.'
Write-Host 'XM5: physically disconnected.'
Write-Host 'Native endpoint devices/packages/processes/tasks: 0.'
Write-Host 'This preflight was read-only; no driver, endpoint, Bluetooth request, process, or system setting was changed.'
