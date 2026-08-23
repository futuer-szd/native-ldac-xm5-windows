# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\..\tools\direct-pdo-install-common.ps1')

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("NativeLdacHardwareSuspension-" + [Guid]::NewGuid().ToString('N'))
$markerDirectory = Join-Path $temporaryRoot 'direct-pdo'
$markerPath = Join-Path $markerDirectory 'HARDWARE_TESTS_SUSPENDED.md'

try {
    New-Item -ItemType Directory -Path $markerDirectory -Force | Out-Null
    Set-Content -LiteralPath $markerPath -Value 'suspended' -Encoding UTF8
    $blocked = $false
    try {
        Assert-DirectPdoHardwareTestsEnabled -ProjectRoot $temporaryRoot
    } catch {
        $blocked = $_.Exception.Message.Contains(
            'There is no command-line override')
    }
    if (-not $blocked) {
        throw 'The tracked suspension marker did not block hardware tests.'
    }

    Remove-Item -LiteralPath $markerPath -Force
    Assert-DirectPdoHardwareTestsEnabled -ProjectRoot $temporaryRoot

    $projectRoot = [System.IO.Path]::GetFullPath(
        (Join-Path $PSScriptRoot '..\..'))
    foreach ($scriptName in @(
            'install-direct-pdo-candidate.ps1',
            'run-direct-pdo-trial.ps1')) {
        $scriptPath = Join-Path (Join-Path $projectRoot 'tools') $scriptName
        $scriptText = Get-Content -LiteralPath $scriptPath -Raw
        if (-not $scriptText.Contains(
                'Assert-DirectPdoHardwareTestsEnabled')) {
            throw "$scriptName does not enforce the suspension gate."
        }
    }
} finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}

Write-Host 'Direct-PDO hardware suspension tests passed.'
