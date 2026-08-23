# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-normal-stop-common.ps1')

if ($Configuration -cne 'Release') {
    throw 'The policy v20 hardware candidate must use Release configuration.'
}

function Get-Entry([string]$Path, [string]$Relative) {
    $item = Get-Item -LiteralPath $Path
    [ordered]@{ path=$Relative; length=[long]$item.Length;
        sha256=(Get-V1FileSha256 -Path $Path) }
}

$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$commit = (& git.exe -C $root rev-parse HEAD).Trim()
$driverTree = (& git.exe -C $root rev-parse HEAD:driver).Trim()
$status = @(& git.exe -C $root status --porcelain)
if ($LASTEXITCODE -ne 0 -or $status.Count -ne 0) {
    throw 'Refusing to build the normal-stop candidate from dirty Git source.'
}
$fidelityPrerequisitePath = Join-Path $root `
    $script:V1NormalStopFidelityPrerequisiteRelativePath
$pnpPrerequisitePath = Join-Path $root `
    $script:V1NormalStopPnpPrerequisiteRelativePath
if ($driverTree -ne $script:V1NormalStopApprovedDriverTree) {
    throw 'The normal-stop candidate does not use the approved policy v14 driver tree.'
}
if (-not (Test-Path -LiteralPath $fidelityPrerequisitePath -PathType Leaf)) {
    throw 'The completed policy v10 prerequisite is missing.'
}
$fidelityPrerequisite = Get-Content `
    -LiteralPath $fidelityPrerequisitePath -Raw |
    ConvertFrom-Json
$fidelityResultPath = [string]$fidelityPrerequisite.result
if (-not (Test-Path -LiteralPath $fidelityResultPath -PathType Leaf)) {
    throw 'The completed policy v10 result is missing.'
}
$fidelityResult = Get-Content -LiteralPath $fidelityResultPath -Raw |
    ConvertFrom-Json
if (-not (Test-V1NormalStopFidelityPrerequisite `
        -Transaction $fidelityPrerequisite -Result $fidelityResult)) {
    throw 'Policy v10 is not a completed transparent-path prerequisite.'
}
if (-not (Test-Path -LiteralPath $pnpPrerequisitePath -PathType Leaf)) {
    throw 'The completed policy v14 PnP-rundown prerequisite is missing.'
}
$pnpPrerequisite = Get-Content -LiteralPath $pnpPrerequisitePath -Raw |
    ConvertFrom-Json
$pnpResultPath = [string]$pnpPrerequisite.result
if (-not (Test-Path -LiteralPath $pnpResultPath -PathType Leaf)) {
    throw 'The completed policy v14 PnP-rundown result is missing.'
}
$pnpResult = Get-Content -LiteralPath $pnpResultPath -Raw |
    ConvertFrom-Json
if (-not (Test-V1NormalStopPnpPrerequisite `
        -Transaction $pnpPrerequisite `
        -TransactionPath $pnpPrerequisitePath `
        -Result $pnpResult -ResultPath $pnpResultPath `
        -ExpectedDriverTree $driverTree)) {
    throw 'Policy v14 is not a completed inbound PnP-rundown prerequisite.'
}

& (Join-Path $PSScriptRoot 'build-v1-engine-ready-observer.ps1') `
    -Configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw 'V1 observer base build failed.' }
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$build = Join-Path $root 'build\protocol'
& $cmake --build $build --config $Configuration `
    --target endpoint_volume_probe --clean-first
if ($LASTEXITCODE -ne 0) { throw 'V1 endpoint monitor build failed.' }
& $cmake --build $build --config $Configuration `
    --target v1_transport_normal_stop_worker v1_presence_agent transport_probe
if ($LASTEXITCODE -ne 0) { throw 'V1 normal-stop worker build failed.' }
if ((& git.exe -C $root rev-parse HEAD).Trim() -ne $commit -or
    @(& git.exe -C $root status --porcelain).Count -ne 0) {
    throw 'Git source changed during normal-stop candidate build.'
}

$base = Join-Path $root 'artifacts\v1-engine-ready-observer\candidate'
$out = Join-Path $root 'artifacts\v1-normal-stop\candidate'
if (Test-Path -LiteralPath $out) {
    Remove-Item -LiteralPath $out -Recurse -Force
}
New-Item -ItemType Directory -Path $out -Force | Out-Null
$files = @(
    @{s=Join-Path $build "$Configuration\v1_presence_agent.exe";
        r='v1_presence_agent.exe'},
    @{s=Join-Path $build `
        "$Configuration\v1_transport_normal_stop_worker.exe";
        r='v1_transport_normal_stop_worker.exe'},
    @{s=Join-Path $base 'audio_endpoint_probe.exe';
        r='audio_endpoint_probe.exe'},
    @{s=Join-Path $build "$Configuration\endpoint_volume_probe.exe";
        r='endpoint_volume_probe.exe'},
    @{s=Join-Path $build "$Configuration\transport_probe.exe";
        r='transport_probe.exe'},
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
$monitorCheck = @(& (Join-Path $out 'endpoint_volume_probe.exe') `
    --monitor-state 1 2>&1)
if ($LASTEXITCODE -ne 0 -or ($monitorCheck -join "`n") -notmatch
        '(?m)^Monitoring read-only endpoint state changes for 1 seconds\.') {
    throw 'The candidate endpoint_volume_probe.exe does not support --monitor-state.'
}
$manifest = [ordered]@{
    manifest_version = 1
    transport_policy_version = $script:V1NormalStopPolicyVersion
    source_commit = $commit
    driver_tree = $driverTree
    source_dirty = $false
    configuration = $Configuration
    fidelity_prerequisite = $fidelityPrerequisitePath
    fidelity_prerequisite_source_commit =
        [string]$fidelityPrerequisite.source_commit
    fidelity_driver_tree = [string]$fidelityPrerequisite.driver_tree
    pnp_prerequisite = $pnpPrerequisitePath
    pnp_prerequisite_source_commit =
        [string]$pnpPrerequisite.source_commit
    output_policy = 'transparent-unity-full-scale-boundary'
    peak_measurement = 'digital-sample-peak'
    peak_unit = 'dBFS'
    sample_peak_ceiling = $script:V1NormalStopSamplePeakCeiling
    maximum_gain_scalar = 1.0
    maximum_duration_ms = $script:V1NormalStopMaximumDurationMs
    minimum_stop_duration_ms = $script:V1NormalStopMinimumMediaDurationMs
    startup_silence_ms = $script:V1NormalStopStartupSilenceMs
    fade_in_ms = $script:V1NormalStopFadeInMs
    ceiling_ramp_ms = 0.0
    transport_open_render_stability_ms =
        $script:V1NormalStopTransportOpenRenderStabilityMs
    required_transport_open_attempts = 1
    capabilities = @(
        'verified_policy_v10_transparent_transport_prerequisite',
        'verified_policy_v14_inbound_pnp_rundown_prerequisite',
        'one_shot_inbound_listener',
        'inbound_signaling_ready_flags_0xF',
        'successful_open_diagnostics_required',
        'inbound_signaling_channel_required',
        'single_transport_attempt_required',
        'zero_transport_retry_success_contract',
        'exact_XM5_ACL_generation',
        'render_stop_required_after_media_started',
        'graceful_STOP_uses_SUSPEND_CLOSE',
        'ACL_disconnect_uses_local_cancel_only',
        'streaming_peer_close_observer',
        'remote_close_accept_local_cleanup',
        'pre_media_render_gap_accounting',
        'pre_start_pcm_discard_accounting',
        'continuous_render_epoch_before_transport_open',
        'two_generation_playback_reconnect_evidence',
        'three_generation_lifecycle_soak_evidence',
        'dynamic_post_volume_tracking',
        'bounded_post_start_render_rebind',
        'public_endpoint_state_capture',
        'unity_post_volume_gain',
        'unity_full_scale_sample_boundary_only',
        'no_post_start_ceiling_ramp',
        'sent_frame_fade_in_100_ms',
        'encoded_startup_silence_20_ms',
        'transient_resume_encoded_silence_20_ms',
        'transient_resume_fade_in_100_ms',
        'minimum_5000_ms_before_render_stop',
        'maximum_60000_ms_hard_bound',
        'consumer_lease_release_required',
        'physical_ACL_disconnect_required',
        'no_LinkState_write',
        'no_driver_install',
        'no_reboot')
    files = @($entries)
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content `
    -LiteralPath (Join-Path $out 'manifest.json') -Encoding UTF8
Write-Host "Built V1 normal-stop candidate: $out"
Write-Host "Source commit: $commit"
Write-Host 'This candidate has unity steady-state gain, no sub-full-scale ceiling, and only the final +/-1.0 sample boundary.'
Write-Host 'It adds 20 ms encoded startup silence and silence/fade-in on a bounded resume; STOP and ACL cancellation add no synthetic tail.'
Write-Host 'No driver, service, Bluetooth request, process, or system setting was changed.'
