# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    return Get-Content -LiteralPath `
        (Join-Path $projectRoot $RelativePath) -Raw
}

$engine = Read-ProjectFile 'engine\windows\v1_pcm_encode_engine.cpp'
$source = Read-ProjectFile 'engine\windows\native_pcm_source.cpp'
$abi = Read-ProjectFile 'audio-endpoint\Source\Inc\nativeldac_pcm_abi.h'
$miniport = Read-ProjectFile 'audio-endpoint\Source\Main\minwavert.cpp'
$agent = Read-ProjectFile 'agent\v1_presence_agent.cpp'
$build = Read-ProjectFile 'tools\build-v1-pcm-encode-observer.ps1'
$trial = Read-ProjectFile 'tools\run-v1-pcm-encode-trial.ps1'
$update = Read-ProjectFile `
    'tools\update-v1-endpoint-consumer-candidate.ps1'

foreach ($required in @(
        'native_pcm_source_create',
        'native_pcm_source_acquire_consumer',
        'native_pcm_source_release_consumer',
        'native_pcm_source_read_f32_stereo',
        'ldac_encoder_create',
        'ldac_encoder_encode_f32',
        'SetEvent(ready_event)')) {
    if (-not $engine.Contains($required)) {
        throw "PCM/encode engine is missing: $required"
    }
}
foreach ($required in @(
        'NativeLdacPcmPropertyConsumerLease = 5',
        'NATIVE_LDAC_PCM_CONSUMER_LEASE_ABI_VERSION')) {
    if (-not $abi.Contains($required)) {
        throw "PCM consumer-lease ABI is missing: $required"
    }
}
foreach ($required in @(
        'NativeLdacPcmPropertyConsumerLease',
        'NativeLdacPcmOwnerAcquireConsumer',
        'NativeLdacPcmOwnerReleaseConsumer')) {
    if (-not $miniport.Contains($required)) {
        throw "PCM consumer-lease miniport path is missing: $required"
    }
}
foreach ($required in @(
        'native_pcm_source_acquire_consumer',
        'native_pcm_source_release_consumer',
        'NativeLdacPcmPropertyConsumerLease')) {
    if (-not $source.Contains($required)) {
        throw "PCM source consumer-lease path is missing: $required"
    }
}
foreach ($forbidden in @(
        'Bluetooth',
        'Bth',
        'direct_pdo_media_sink',
        'ldac_rtp',
        'socket',
        'native_pcm_source_report_link_state',
        'NativeLdacPcmPropertyLinkState')) {
    if ($engine.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "PCM/encode engine exceeds dry-run authority: $forbidden"
    }
}
if (-not $agent.Contains('--engine-executable')) {
    throw 'The V1 agent does not accept the contained engine executable.'
}

foreach ($required in @(
        'Native_PCM_read',
        'LDAC_HQ_encode_discard',
        'independent_PCM_consumer_lease',
        'engine_ready_proves_PCM_read_and_encode',
        'ready_after_first_encoded_frame',
        'no_media_LinkState_write',
        'no_Bluetooth_open')) {
    if (-not $build.Contains($required)) {
        throw "PCM/encode build contract is missing: $required"
    }
}
foreach ($required in @(
        '--engine-executable',
        "`$ErrorActionPreference = 'Continue'",
        'pcm_read_and_encode_proven_by_engine_ready',
        'render_started_events -ge 1',
        'render_stopped_events -ge 1',
        'last_engine_exit_code',
        'transport_open_actions -eq 1',
        'transport_open_executed -eq 0',
        'Link disconnected:')) {
    if (-not $trial.Contains($required)) {
        throw "PCM/encode trial contract is missing: $required"
    }
}
foreach ($forbidden in @(
        '--discover',
        '--play-endpoint',
        'transport_probe',
        'SetDefaultEndpoint',
        'pnputil',
        'devcon')) {
    if ($trial.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "PCM/encode trial exceeds observer scope: $forbidden"
    }
}

foreach ($required in @(
        'ConfirmV1EndpointUpdate',
        'pcm_consumer_lease_separate_from_media_link',
        '--consumer-lease',
        'devconPath update',
        'rollbackInf',
        'safe_original_a2dp',
        'Get-NativeLdacXm5BluetoothState')) {
    if (-not $update.Contains($required)) {
        throw "V1 endpoint update safety contract is missing: $required"
    }
}
foreach ($forbidden in @(
        'ROOT\MEDIA\0001',
        'SetDefaultEndpoint',
        '--discover',
        '--play-endpoint',
        'transport_probe')) {
    if ($update.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "V1 endpoint update exceeds its scope: $forbidden"
    }
}

Write-Host 'V1 PCM/encode observer policy tests passed.'
