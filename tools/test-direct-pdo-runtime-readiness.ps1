# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$BundlePath,
    [switch]$AllowDirtyBundle,
    [switch]$AllowRecoverableMediaTimeout
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'direct-pdo-install-common.ps1')

function Invoke-ReadOnlyStatusProbe {
    param(
        [Parameter(Mandatory = $true)][string]$Path
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Path
    $startInfo.Arguments = '--direct-status'
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Could not start the Direct-PDO status probe: $Path"
    }
    $standardOutput = $process.StandardOutput.ReadToEnd().TrimEnd()
    $standardError = $process.StandardError.ReadToEnd().TrimEnd()
    $process.WaitForExit()
    $output = @($standardOutput, $standardError) | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_)
    }
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Text = $output -join [Environment]::NewLine
    }
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($BundlePath)) {
    $BundlePath = Join-Path $projectRoot `
        'artifacts\direct-pdo\validation-bundle'
}
$BundlePath = [System.IO.Path]::GetFullPath($BundlePath)
$manifestPath = Join-Path $BundlePath 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Direct-PDO artifact manifest is missing: $manifestPath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$artifactKind = Get-DirectPdoArtifactKind -Manifest $manifest
if ($artifactKind -eq 'candidate') {
    $verifyScript = Join-Path $PSScriptRoot `
        'verify-direct-pdo-candidate.ps1'
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File $verifyScript `
        -CandidatePath $BundlePath
} elseif ($artifactKind -eq 'validation-bundle') {
    $verifyScript = Join-Path $PSScriptRoot `
        'verify-direct-pdo-validation-bundle.ps1'
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File $verifyScript `
        -BundlePath $BundlePath
} else {
    throw "Unsupported Direct-PDO artifact manifest: $manifestPath"
}
if ($LASTEXITCODE -ne 0) {
    throw "Direct-PDO $artifactKind verification failed with exit code $LASTEXITCODE."
}
if ($manifest.source_dirty -eq $true -and -not $AllowDirtyBundle) {
    throw 'The validation bundle was built from a dirty Git worktree. Rebuild it from the reviewed commit before a hardware trial.'
}

$probePath = Join-Path $BundlePath 'audio_endpoint_probe.exe'
$probeResult = Invoke-ReadOnlyStatusProbe -Path $probePath
$probeExitCode = $probeResult.ExitCode
$statusText = $probeResult.Text
if ($probeExitCode -ne 0) {
    if ($statusText -match 'Win32 1168') {
        throw 'The XM5 Direct-PDO is not present. Turn on the XM5 and wait for Windows Bluetooth to connect, then run the trial again. No recovery or Bluetooth command was submitted.'
    }
    if ($statusText -match 'Win32 5') {
        throw 'The Direct-PDO interface is present but access was denied. Run this check from an elevated Windows PowerShell. No recovery or Bluetooth command was submitted.'
    }
    throw "The installed endpoint did not expose the coordinated Direct-PDO status ABI (probe exit $probeExitCode). No recovery or Bluetooth command was submitted.`n$statusText"
}
if (-not (Test-DirectPdoRuntimeStatusText `
        -StatusText $statusText `
        -ExitCode $probeExitCode) -and
    -not ($AllowRecoverableMediaTimeout -and
        (Test-DirectPdoRecoverableMediaTimeoutStatusText `
            -StatusText $statusText `
            -ExitCode $probeExitCode))) {
    throw "The installed Direct-PDO runtime is not ready.`n$statusText"
}

Write-Host 'Direct-PDO runtime readiness preflight passed.'
Write-Host "Artifact kind: $artifactKind"
Write-Host "Bundle source commit: $($manifest.source_commit)"
Write-Host $statusText
if ($AllowRecoverableMediaTimeout -and
    (Test-DirectPdoRecoverableMediaTimeoutStatusText `
        -StatusText $statusText `
        -ExitCode $probeExitCode)) {
    Write-Host 'A quiescent media-timeout fault is present; the bounded agent may submit one generation-bound local recovery.'
}
Write-Host 'This check was read-only: no recovery SET, AVDTP DISCOVER, Bluetooth OPEN, process, driver, or system setting was changed.'
