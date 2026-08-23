[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$uiScript = Join-Path $repoRoot 'ui\ldac_control.py'

if (-not (Test-Path -LiteralPath $uiScript -PathType Leaf)) {
    throw "UI script not found: $uiScript"
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
    $command = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($command) {
        $python = $command.Source
    }
}
if (-not $python) {
    throw 'Python 3 was not found. Run this script from the Conda environment that has tkinter.'
}

$quotedUiScript = '"' + $uiScript + '"'
Start-Process -FilePath $python `
    -ArgumentList @($quotedUiScript) `
    -WorkingDirectory $repoRoot `
    -WindowStyle Hidden
