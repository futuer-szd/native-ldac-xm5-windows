# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$InstallRoot = (Join-Path $env:ProgramFiles 'NativeLdac\V1'),
    [string]$RuntimeRoot = (Join-Path $env:LOCALAPPDATA 'NativeLdac\V1')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$install = [IO.Path]::GetFullPath($InstallRoot)
$runtime = [IO.Path]::GetFullPath($RuntimeRoot)
$uiPath = Join-Path $install 'ldac_control.py'
if (-not (Test-Path -LiteralPath $uiPath -PathType Leaf)) {
    throw "The V1 daily UI is missing: $uiPath"
}

$python = $null
if ($env:CONDA_PREFIX) {
    $candidate = Join-Path $env:CONDA_PREFIX 'pythonw.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $python = $candidate
    }
}
if (-not $python) {
    $command = Get-Command pythonw.exe -ErrorAction SilentlyContinue
    if ($command) {
        $python = $command.Source
    }
}
if (-not $python) {
    throw 'Python 3 with tkinter was not found.'
}

$env:NATIVE_LDAC_V1_STATE_PATH =
    Join-Path $runtime 'state\daily-state.json'
$env:NATIVE_LDAC_V1_RESULT_PATH =
    Join-Path $runtime 'results\latest-session.json'
Start-Process -FilePath $python `
    -ArgumentList @('"' + $uiPath + '"') `
    -WorkingDirectory $install
