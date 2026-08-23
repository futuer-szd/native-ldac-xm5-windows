# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [switch]$ConfirmBootChange
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
if (-not $ConfirmBootChange) {
    throw 'Refusing to change BCD. Re-run with -ConfirmBootChange after checking BitLocker recovery access.'
}

$control = Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control'
if ([string]$control.SystemStartOptions -match '(^|\s)TESTSIGNING(\s|$)') {
    Write-Host 'Test signing is already active in the current boot.'
    exit 0
}

try {
    $secureBoot = Confirm-SecureBootUEFI
} catch {
    $secureBoot = $null
}
if ($secureBoot -eq $true) {
    throw 'Secure Boot is enabled. BCDEdit normally refuses TESTSIGNING; do not disable it until BitLocker recovery is prepared.'
}

$output = & bcdedit.exe /set testsigning on 2>&1
$exitCode = $LASTEXITCODE
$output | ForEach-Object { Write-Host $_ }
if ($exitCode -ne 0) {
    throw "BCDEdit failed with exit code $exitCode."
}
Write-Host 'Test signing was enabled in BCD. Restart Windows manually before installing the test driver.'
