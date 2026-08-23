# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [string]$BackupPath,
    [switch]$ConfirmRestore,
    [switch]$RemoveTestCertificate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script from an elevated Windows PowerShell.'
    }
}

Assert-Administrator
if (-not $ConfirmRestore) {
    throw 'Refusing to modify driver packages. Re-run with -ConfirmRestore.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($BackupPath)) {
    $latestBackupFile = Join-Path $projectRoot 'artifacts\driver-test\latest-backup.txt'
    if (-not (Test-Path -LiteralPath $latestBackupFile)) {
        throw 'No latest-backup.txt exists; provide -BackupPath explicitly.'
    }
    $BackupPath = (Get-Content -LiteralPath $latestBackupFile -Raw).Trim()
}
$BackupPath = [System.IO.Path]::GetFullPath($BackupPath)
if (-not (Test-Path -LiteralPath (Join-Path $BackupPath 'state.json'))) {
    throw "Invalid backup path: $BackupPath"
}

if (-not $PSCmdlet.ShouldProcess('WH-1000XM5 A2DP profile PDO', 'Remove LdacNative and restore backed-up A2DP package')) {
    return
}

$installedDrivers = Get-WindowsDriver -Online -All
$nativeDrivers = @($installedDrivers | Where-Object {
    $leaf = Split-Path -Leaf ([string]$_.OriginalFileName)
    $leaf -ieq 'LdacNative.inf'
})
foreach ($nativeDriver in $nativeDrivers) {
    $publishedName = [string]$nativeDriver.Driver
    if (-not [string]::IsNullOrWhiteSpace($publishedName)) {
        $removeOutput = & pnputil.exe /delete-driver $publishedName /uninstall /force 2>&1
        $removeExitCode = $LASTEXITCODE
        $removeOutput | ForEach-Object { Write-Host $_ }
        if ($removeExitCode -ne 0) {
            throw "Failed to remove $publishedName (exit $removeExitCode)."
        }
    }
}

$backupInfs = @(Get-ChildItem -LiteralPath $BackupPath -Recurse -Filter '*.inf' -File)
if ($backupInfs.Count -eq 0) {
    throw "No exported INF was found under $BackupPath."
}
foreach ($backupInf in $backupInfs) {
    $restoreOutput = & pnputil.exe /add-driver $backupInf.FullName /install 2>&1
    $restoreExitCode = $LASTEXITCODE
    $restoreOutput | ForEach-Object { Write-Host $_ }
    if ($restoreExitCode -ne 0) {
        throw "Failed to restore $($backupInf.FullName) (exit $restoreExitCode)."
    }
}

& pnputil.exe /scan-devices | ForEach-Object { Write-Host $_ }

if ($RemoveTestCertificate) {
    $certificatePath = Join-Path $projectRoot 'artifacts\driver-test\package\LdacNative.cer'
    if (Test-Path -LiteralPath $certificatePath) {
        $certificate = Get-PfxCertificate -FilePath $certificatePath
        $storePaths = @(
            "Cert:\LocalMachine\Root\$($certificate.Thumbprint)",
            "Cert:\LocalMachine\TrustedPublisher\$($certificate.Thumbprint)"
        )
        foreach ($storePath in $storePaths) {
            if (Test-Path -LiteralPath $storePath) {
                Remove-Item -LiteralPath $storePath -Force
            }
        }
    }
}

Write-Host "Restore commands completed from: $BackupPath"
Write-Host 'Reconnect the XM5 or scan for hardware changes. A reboot may be needed if Windows held the old service open.'
