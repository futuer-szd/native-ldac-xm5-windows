# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-linked-limiter-common.ps1')

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
    throw 'Refusing to build the linked-limiter candidate from dirty Git source.'
}
$prerequisitePath = Join-Path $root `
    $script:V1LinkedLimiterPrerequisiteRelativePath
if (-not (Test-Path -LiteralPath $prerequisitePath -PathType Leaf)) {
    throw 'The completed policy v8 comparison prerequisite is missing.'
}
$prerequisite = Get-Content -LiteralPath $prerequisitePath -Raw |
    ConvertFrom-Json
$prerequisiteResultPath = [string]$prerequisite.result
if (-not (Test-Path -LiteralPath $prerequisiteResultPath -PathType Leaf)) {
    throw 'The completed policy v8 result is missing.'
}
$prerequisiteResult = Get-Content -LiteralPath $prerequisiteResultPath -Raw |
    ConvertFrom-Json
if (-not (Test-V1LinkedLimiterPrerequisite `
        -Transaction $prerequisite -Result $prerequisiteResult `
        -ExpectedDriverTree $driverTree)) {
    throw 'Policy v8 is not the verified generally-clear/muffled-bass prerequisite.'
}

& (Join-Path $PSScriptRoot 'build-v1-engine-ready-observer.ps1') `
    -Configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw 'V1 observer base build failed.' }
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$build = Join-Path $root 'build\protocol'
& $cmake --build $build --config $Configuration `
    --target v1_transport_linked_limiter_worker v1_presence_agent
if ($LASTEXITCODE -ne 0) { throw 'V1 linked-limiter worker build failed.' }
if ((& git.exe -C $root rev-parse HEAD).Trim() -ne $commit -or
    @(& git.exe -C $root status --porcelain).Count -ne 0) {
    throw 'Git source changed during linked-limiter candidate build.'
}

$base = Join-Path $root 'artifacts\v1-engine-ready-observer\candidate'
$out = Join-Path $root 'artifacts\v1-linked-limiter\candidate'
if (Test-Path -LiteralPath $out) {
    Remove-Item -LiteralPath $out -Recurse -Force
}
New-Item -ItemType Directory -Path $out -Force | Out-Null
$files = @(
    @{s=Join-Path $build "$Configuration\v1_presence_agent.exe";
        r='v1_presence_agent.exe'},
    @{s=Join-Path $build `
        "$Configuration\v1_transport_linked_limiter_worker.exe";
        r='v1_transport_linked_limiter_worker.exe'},
    @{s=Join-Path $base 'audio_endpoint_probe.exe';
        r='audio_endpoint_probe.exe'},
    @{s=Join-Path $base 'xm5_connection_probe.exe';
        r='xm5_connection_probe.exe'},
    @{s=Join-Path $base 'xm5_connection_probe.manifest.json';
        r='xm5_connection_probe.manifest.json'})
$entries = @()
foreach ($file in $files) {
    $destination = Join-Path $out $file.r
    Copy-Item -LiteralPath $file.s -Destination $destination -Force
    $entries += Get-Entry $destination $file.r
}
$manifest = [ordered]@{
    manifest_version = 1
    transport_policy_version = 9
    source_commit = $commit
    driver_tree = $driverTree
    source_dirty = $false
    configuration = $Configuration
    prerequisite = $prerequisitePath
    prerequisite_source_commit = [string]$prerequisite.source_commit
    limiter_algorithm = $script:V1LinkedLimiterAlgorithm
    limiter_algorithm_version = $script:V1LinkedLimiterAlgorithmVersion
    limiter_release_ms = 50.0
    maximum_gain_scalar = 1.0
    maximum_output_peak = 0.25
    target_duration_ms = 60000
    maximum_transport_open_attempts = 4
    capabilities = @(
        'verified_policy_v8_generally_clear_muffled_bass_prerequisite',
        'installed_LdacNative_driver_tree_prerequisite',
        'exact_XM5_ACL_generation',
        'render_demand_authorized',
        'pretransport_render_gap_tolerance',
        'audible_PCM_before_Bluetooth_OPEN',
        'unity_post_volume_gain',
        'linked_stereo_block_limiter_v1',
        'independent_hard_output_ceiling_0_25',
        'linked_limiter_gain_reduction_telemetry',
        'bounded_120000_ms_pretransport_PCM_wait',
        'bounded_60000_ms_PCM_clock_pacing',
        'AVDTP_START_then_SUSPEND_CLOSE',
        'retry_only_OpenSignaling_Win32_71_zero_exchange',
        'maximum_four_zero_exchange_open_attempts',
        'consumer_lease_release_required',
        'no_LinkState_write',
        'no_driver_install',
        'no_reboot')
    files = @($entries)
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content `
    -LiteralPath (Join-Path $out 'manifest.json') -Encoding UTF8
Write-Host "Built V1 linked-limiter candidate: $out"
Write-Host "Source commit: $commit"
Write-Host 'Policy v8 was used only as frozen evidence; it was not re-run.'
Write-Host 'No driver, service, Bluetooth request, process, or system setting was changed.'
