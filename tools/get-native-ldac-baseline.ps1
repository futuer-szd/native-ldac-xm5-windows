# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$BackupPath,
    [switch]$AsJson
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'native-ldac-baseline-common.ps1')

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($BackupPath)) {
    $latestBackupPath = Join-Path $projectRoot `
        'artifacts\driver-test\latest-backup.txt'
    if (-not (Test-Path -LiteralPath $latestBackupPath -PathType Leaf)) {
        throw 'Original A2DP latest-backup.txt is missing.'
    }
    $BackupPath = (Get-Content -LiteralPath $latestBackupPath -Raw).Trim()
}

$snapshot = Get-NativeLdacBaselineSnapshot -BackupPath $BackupPath
if ($AsJson) {
    $snapshot | ConvertTo-Json -Depth 8
} else {
    Write-NativeLdacBaselineSummary -Snapshot $snapshot
    Write-Host "A2DP device: $(@($snapshot.a2dp_devices | ForEach-Object { `
        $_.service + '/' + $_.published_inf + '/problem ' + `
        $_.problem_code }) -join ', ')"
    if (@($snapshot.native_audio_packages).Count -ne 0) {
        $packageNames = @($snapshot.native_audio_packages |
            ForEach-Object { $_.published_inf })
        Write-Host "Native audio packages: $($packageNames -join ', ')"
    }
    Write-Host 'This command was read-only.'
}
