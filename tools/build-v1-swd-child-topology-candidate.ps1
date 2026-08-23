# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('Release')][string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-swd-child-topology-common.ps1')

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The SWD child topology candidate requires PowerShell 7.'
}
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$sourceStatus = @(& git.exe -C $projectRoot status --porcelain `
    --untracked-files=all)
if ($LASTEXITCODE -ne 0 -or
    $sourceCommit -notmatch '^[0-9a-fA-F]{40}$' -or
    $sourceStatus.Count -ne 0) {
    throw 'The SWD child topology candidate requires clean Git source.'
}

$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake -PathType Leaf)) {
    throw 'The configured Visual Studio CMake executable is missing.'
}
$buildRoot = Join-Path $projectRoot 'build\protocol'
& $cmake -S $projectRoot -B $buildRoot -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) {
    throw 'CMake configure failed.'
}
& $cmake --build $buildRoot --config $Configuration --target `
    v1_swd_child_probe endpoint_volume_probe avrcp_transport_probe
if ($LASTEXITCODE -ne 0) {
    throw 'The SWD child topology candidate build failed.'
}

$output = Join-Path $projectRoot 'artifacts\v1-volume-sync\candidate'
if (Test-Path -LiteralPath $output -PathType Container) {
    Remove-Item -LiteralPath $output -Recurse -Force
}
New-Item -ItemType Directory -Path $output -Force | Out-Null
$sources = [ordered]@{
    'v1_swd_child_probe.exe' = Join-Path $buildRoot `
        "$Configuration\v1_swd_child_probe.exe"
    'endpoint_volume_probe.exe' = Join-Path $buildRoot `
        "$Configuration\endpoint_volume_probe.exe"
    'avrcp_transport_probe.exe' = Join-Path $buildRoot `
        "$Configuration\avrcp_transport_probe.exe"
    'get-v1-volume-sync-topology.ps1' = Join-Path $PSScriptRoot `
        'get-v1-volume-sync-topology.ps1'
    'v1-volume-sync-topology-common.ps1' = Join-Path $PSScriptRoot `
        'v1-volume-sync-topology-common.ps1'
    'v1-swd-child-topology-common.ps1' = Join-Path $PSScriptRoot `
        'v1-swd-child-topology-common.ps1'
    'run-v1-swd-child-topology-gate.ps1' = Join-Path $PSScriptRoot `
        'run-v1-swd-child-topology-gate.ps1'
    'complete-v1-swd-child-topology-gate.ps1' = Join-Path $PSScriptRoot `
        'complete-v1-swd-child-topology-gate.ps1'
    'verify-v1-golden-checkpoint.ps1' = Join-Path $PSScriptRoot `
        'verify-v1-golden-checkpoint.ps1'
}
$files = @()
foreach ($name in $sources.Keys) {
    $source = [string]$sources[$name]
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "The candidate source file is missing: $source"
    }
    $destination = Join-Path $output $name
    Copy-Item -LiteralPath $source -Destination $destination -Force
    $item = Get-Item -LiteralPath $destination
    $files += [ordered]@{
        path = $name
        length = [long]$item.Length
        sha256 = (Get-FileHash -LiteralPath $destination `
            -Algorithm SHA256).Hash
    }
}
$help = @(& (Join-Path $output 'v1_swd_child_probe.exe') --help 2>&1)
if ($LASTEXITCODE -ne 0 -or
    ($help -join "`n") -notmatch 'driverless software child') {
    throw 'The SWD child probe help contract failed.'
}
$manifest = [ordered]@{
    manifest_version = 1
    policy_version = $script:V1SwdChildTopologyPolicyVersion
    source_commit = $sourceCommit
    source_dirty = $false
    configuration = $Configuration
    driverless = $true
    inbox_null_driver_inf = $script:V1SwdChildInboxInf
    function_service = ''
    custom_driver_binding = $false
    driver_install = $false
    audio_endpoint_creation = $false
    hardware_ids = @()
    compatible_ids = @()
    lifetime = 'handle'
    maximum_duration_seconds = 30
    requires_golden_checkpoint = $true
    write_authorization = $false
    files = @($files)
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content `
    -LiteralPath (Join-Path $output 'manifest.json') `
    -Encoding utf8NoBOM
$null = Get-V1SwdChildTopologyCandidate -CandidatePath $output

Write-Host "Built V1 driverless SWD child topology candidate: $output"
Write-Host "Source commit: $sourceCommit"
Write-Host 'No software device was created and no driver, PnP, Bluetooth, endpoint, or audio setting was changed.'
