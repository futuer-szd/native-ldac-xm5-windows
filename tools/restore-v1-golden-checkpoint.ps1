# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [string]$CheckpointPath,
    [switch]$ConfirmV1GoldenRestore
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script from an elevated PowerShell 7 terminal.'
    }
}

function Find-DevCon {
    $candidates = @(
        'C:\Program Files (x86)\Windows Kits\10\Tools\10.0.26100.0\x64\devcon.exe',
        'C:\Program Files (x86)\Windows Kits\10\Tools\x64\devcon.exe'
    )
    return $candidates |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
}

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The V1 golden restore requires PowerShell 7.'
}
Assert-Administrator
if (-not $ConfirmV1GoldenRestore) {
    throw 'Refusing to modify drivers without -ConfirmV1GoldenRestore.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CheckpointPath)) {
    $latestPath = Join-Path $projectRoot 'artifacts\v1-golden\latest.txt'
    if (-not (Test-Path -LiteralPath $latestPath -PathType Leaf)) {
        throw 'No V1 golden latest.txt exists.'
    }
    $CheckpointPath = (Get-Content -LiteralPath $latestPath -Raw).Trim()
}
$CheckpointPath = [IO.Path]::GetFullPath($CheckpointPath)
& (Join-Path $PSScriptRoot 'verify-v1-golden-checkpoint.ps1') `
    -CheckpointPath $CheckpointPath
if ($LASTEXITCODE -ne 0) {
    throw 'The V1 golden checkpoint failed integrity verification.'
}
$manifest = Get-Content -LiteralPath `
    (Join-Path $CheckpointPath 'manifest.json') -Raw | ConvertFrom-Json

$workspaceProcesses = @(Get-CimInstance Win32_Process |
    Where-Object {
        $_.Name -in @(
            'ldac_agent.exe',
            'transport_probe.exe',
            'v1_presence_agent.exe',
            'v1_transport_daily_worker.exe',
            'v1_transport_normal_stop_worker.exe')
    })
if ($workspaceProcesses.Count -ne 0) {
    throw 'Stop every Native LDAC process before restoring the golden checkpoint.'
}

$transportPackage = @($manifest.packages | Where-Object {
    $_.role -eq 'transport-active'
})
$endpointPackage = @($manifest.packages | Where-Object {
    $_.role -eq 'endpoint-active'
})
if ($transportPackage.Count -ne 1 -or $endpointPackage.Count -ne 1) {
    throw 'The checkpoint does not contain one active transport and endpoint package.'
}
$transportInf = Get-ChildItem -LiteralPath `
    (Join-Path $CheckpointPath $transportPackage[0].package_path) `
    -Filter '*.inf' -File -Recurse
$endpointInf = Get-ChildItem -LiteralPath `
    (Join-Path $CheckpointPath $endpointPackage[0].package_path) `
    -Filter '*.inf' -File -Recurse
if (@($transportInf).Count -ne 1 -or @($endpointInf).Count -ne 1) {
    throw 'The checkpoint active package INF selection is ambiguous.'
}

if (-not $PSCmdlet.ShouldProcess(
        'XM5 LdacNative transport and NativeLdacAudio root endpoint',
        'Restore the V1 golden driver packages and bindings')) {
    return
}

foreach ($certificateFile in @(Get-ChildItem -LiteralPath `
        (Join-Path $CheckpointPath 'certificates') -Filter '*.cer' `
        -File -ErrorAction SilentlyContinue)) {
    $certificate = Get-PfxCertificate -FilePath $certificateFile.FullName
    foreach ($store in @('Root', 'TrustedPublisher')) {
        $target = "Cert:\LocalMachine\$store\$($certificate.Thumbprint)"
        if (-not (Test-Path -LiteralPath $target)) {
            Import-Certificate -FilePath $certificateFile.FullName `
                -CertStoreLocation "Cert:\LocalMachine\$store" | Out-Null
        }
    }
}

$installed = @(Get-WindowsDriver -Online -All | Where-Object {
    (Split-Path -Leaf ([string]$_.OriginalFileName)) -in @(
        'ldacnative.inf', 'nativeldacaudio.inf')
})
foreach ($driver in $installed) {
    $publishedInf = [string]$driver.Driver
    $output = @(& pnputil.exe /delete-driver $publishedInf `
        /uninstall /force 2>&1)
    $exitCode = $LASTEXITCODE
    $output | ForEach-Object { Write-Host $_ }
    if ($exitCode -ne 0) {
        throw "Failed to remove $publishedInf (exit $exitCode)."
    }
}

$transportOutput = @(& pnputil.exe /add-driver `
    $transportInf.FullName /install 2>&1)
$transportExit = $LASTEXITCODE
$transportOutput | ForEach-Object { Write-Host $_ }
if ($transportExit -notin @(0, 259)) {
    throw "Failed to restore the golden transport package (exit $transportExit)."
}

$devcon = Find-DevCon
if (-not $devcon) {
    throw 'The x64 WDK DevCon is required to restore the root audio endpoint.'
}
$endpointOutput = @(& $devcon update $endpointInf.FullName `
    'ROOT\NativeLdacAudio' 2>&1)
$endpointExit = $LASTEXITCODE
$endpointOutput | ForEach-Object { Write-Host $_ }
if ($endpointExit -notin @(0, 1)) {
    throw "Failed to restore the golden endpoint package (exit $endpointExit)."
}

& pnputil.exe /scan-devices | ForEach-Object { Write-Host $_ }
Write-Host 'V1 golden package restore completed.'
Write-Host 'Keep XM5 off until the post-restore verification finishes.'
if ($endpointExit -eq 1) {
    Write-Host 'DevCon reports that one Windows reboot is required.'
} else {
    Write-Host 'A reboot is only required if Windows reports Code 38 or retains an older loaded image.'
}
