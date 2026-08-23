# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-Entry([string]$Path, [string]$Relative) {
    $item = Get-Item -LiteralPath $Path
    [ordered]@{ path=$Relative; length=[long]$item.Length;
        sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
}
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$commit = (& git.exe -C $root rev-parse HEAD).Trim()
$driverTree = (& git.exe -C $root rev-parse HEAD:driver).Trim()
$status = @(& git.exe -C $root status --porcelain)
if ($LASTEXITCODE -ne 0 -or $status.Count -ne 0) {
    throw 'Refusing to build the silence-burst candidate from dirty Git source.'
}
& (Join-Path $PSScriptRoot 'build-v1-engine-ready-observer.ps1') `
    -Configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw 'V1 observer base build failed.' }
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$build = Join-Path $root 'build\protocol'
& $cmake --build $build --config $Configuration `
    --target v1_transport_silence_worker v1_presence_agent
if ($LASTEXITCODE -ne 0) { throw 'V1 silence worker build failed.' }
if ((& git.exe -C $root rev-parse HEAD).Trim() -ne $commit -or
    @(& git.exe -C $root status --porcelain).Count -ne 0) {
    throw 'Git source changed during silence-burst build.'
}
$base = Join-Path $root 'artifacts\v1-engine-ready-observer\candidate'
$out = Join-Path $root 'artifacts\v1-silence-burst\candidate'
if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
New-Item -ItemType Directory -Path $out -Force | Out-Null
$files = @(
    @{s=Join-Path $build "$Configuration\v1_presence_agent.exe"; r='v1_presence_agent.exe'},
    @{s=Join-Path $build "$Configuration\v1_transport_silence_worker.exe"; r='v1_transport_silence_worker.exe'},
    @{s=Join-Path $base 'audio_endpoint_probe.exe'; r='audio_endpoint_probe.exe'},
    @{s=Join-Path $base 'xm5_connection_probe.exe'; r='xm5_connection_probe.exe'},
    @{s=Join-Path $base 'xm5_connection_probe.manifest.json'; r='xm5_connection_probe.manifest.json'})
$entries = @()
foreach ($file in $files) {
    $destination = Join-Path $out $file.r
    Copy-Item -LiteralPath $file.s -Destination $destination -Force
    $entries += Get-Entry $destination $file.r
}
$manifest = [ordered]@{
    manifest_version=1; transport_policy_version=4; source_commit=$commit;
    driver_tree=$driverTree; source_dirty=$false; configuration=$Configuration;
    capabilities=@('installed_LdacNative_driver_tree_prerequisite',
        'exact_XM5_ACL_generation','render_demand_authorized',
        'maximum_four_digital_zero_packets','AVDTP_START_then_SUSPEND_CLOSE',
        'retry_only_OpenSignaling_Win32_71','no_real_PCM',
        'no_driver_install','no_reboot'); files=@($entries) }
$manifest | ConvertTo-Json -Depth 6 | Set-Content `
    -LiteralPath (Join-Path $out 'manifest.json') -Encoding UTF8
Write-Host "Built V1 silence-burst candidate: $out"
Write-Host "Source commit: $commit"
Write-Host 'No driver, service, Bluetooth request, process, or system setting was changed.'
