# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))

function Read-ProjectFile([string] $RelativePath) {
    return Get-Content -LiteralPath `
        (Join-Path $projectRoot $RelativePath) -Raw
}

$installer = Read-ProjectFile 'tools\install-v1-avrcp-handoff-host.ps1'

function Assert-Policy {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) { throw $Message }
}

foreach ($required in @(
        '[switch]$Remove',
        'The V1 AVRCP handoff host is retired and cannot be installed',
        'Stop-ScheduledTask',
        "Name = 'v1_avrcp_handoff_host.exe'",
        'ExecutablePath',
        'Refusing to stop unexpected handoff executable',
        'Stop-Process',
        'Wait-Process',
        'Unregister-ScheduledTask',
        'handoff-host.log',
        'avrcp-handoff-state.json')) {
    if (-not $installer.Contains($required)) {
        throw "The handoff host installer contract is missing: $required"
    }
}

foreach ($forbidden in @(
        'Disable-PnpDevice',
        'Set-Service',
        'Restart-Computer',
        'SetDefaultEndpoint',
        'SendInput')) {
    if ($installer.Contains($forbidden)) {
        throw "The handoff host installer contains a forbidden operation: $forbidden"
    }
}

Assert-Policy ($installer.Contains('[switch]$Remove') -and
               $installer.Contains('Unregister-ScheduledTask') -and
               -not $installer.Contains('Register-ScheduledTask')) `
    'The retired handoff host installer still exposes an install path.'

$tokens = $null
$errors = $null
[void][Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $projectRoot 'tools\install-v1-avrcp-handoff-host.ps1'),
    [ref]$tokens,
    [ref]$errors)
Assert-Policy (@($errors).Count -eq 0) `
    "The handoff host installer does not parse: " +
    (@($errors | ForEach-Object { $_.Message }) -join '; ')

Write-Host 'V1 AVRCP handoff host installer policy tests passed.'
