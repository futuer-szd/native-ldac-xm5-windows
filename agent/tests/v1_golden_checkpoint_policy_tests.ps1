# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    return Get-Content -LiteralPath (Join-Path $projectRoot $RelativePath) `
        -Raw
}

$capture = Read-ProjectFile 'tools\capture-v1-golden-checkpoint.ps1'
$verify = Read-ProjectFile 'tools\verify-v1-golden-checkpoint.ps1'
$restore = Read-ProjectFile 'tools\restore-v1-golden-checkpoint.ps1'
$cmake = Read-ProjectFile 'CMakeLists.txt'

foreach ($required in @(
        'The V1 golden checkpoint requires PowerShell 7.',
        'status --porcelain',
        '/export-driver',
        'repository.bundle',
        'working-tree.zip',
        'endpoint-volume.txt',
        'sample_peak_ceiling = 1.0',
        'No driver, PnP device, Bluetooth radio')) {
    if (-not $capture.Contains($required)) {
        throw "V1 golden capture policy is missing: $required"
    }
}
foreach ($required in @(
        'manifest.sha256',
        'Get-FileHash',
        'git.exe bundle verify',
        'This verification was read-only.')) {
    if (-not $verify.Contains($required)) {
        throw "V1 golden verification policy is missing: $required"
    }
}
foreach ($required in @(
        'SupportsShouldProcess',
        'ConfirmV1GoldenRestore',
        'verify-v1-golden-checkpoint.ps1',
        'Import-Certificate',
        '/delete-driver',
        '/add-driver',
        'ROOT\NativeLdacAudio')) {
    if (-not $restore.Contains($required)) {
        throw "V1 golden restore policy is missing: $required"
    }
}
foreach ($forbidden in @(
        'Set-NetAdapter',
        'BluetoothSetServiceState',
        'SetDefaultEndpoint',
        'Stop-Process',
        'taskkill')) {
    if (($capture + $verify).IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "V1 golden capture/verify contains forbidden mutation: $forbidden"
    }
}
foreach ($required in @(
        'v1_golden_checkpoint_policy',
        'COMMAND pwsh.exe')) {
    if (-not $cmake.Contains($required)) {
        throw "V1 golden checkpoint CTest policy is missing: $required"
    }
}

Write-Host 'V1 golden checkpoint policy tests passed.'
