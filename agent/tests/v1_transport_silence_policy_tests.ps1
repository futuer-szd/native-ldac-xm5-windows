# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    return Get-Content -LiteralPath `
        (Join-Path $projectRoot $RelativePath) -Raw
}

$header = Read-ProjectFile 'agent\v1_transport_silence_session.h'
$core = Read-ProjectFile 'agent\v1_transport_silence_session.cpp'
$backend = Read-ProjectFile `
    'agent\v1_transport_silence_driver_backend.cpp'
$wrapper = Read-ProjectFile 'agent\v1_transport_silence_worker.cpp'
$sharedWorker = Read-ProjectFile `
    'agent\v1_transport_configuration_worker.cpp'
$agent = Read-ProjectFile 'agent\v1_presence_agent.cpp'
$common = Read-ProjectFile 'tools\v1-silence-burst-common.ps1'
$build = Read-ProjectFile `
    'tools\build-v1-silence-burst-candidate.ps1'
$gate = Read-ProjectFile 'tools\run-v1-silence-burst-gate.ps1'
$tests = Read-ProjectFile `
    'agent\tests\v1_transport_silence_session_tests.cpp'

foreach ($required in @(
        'packet_limit = 4u',
        'packet_limit > 4u',
        'std::array<float',
        'LDAC_ENCODER_QUALITY_HQ',
        'ldac_encoder_encode_f32',
        'ldac_rtp_build_unfragmented',
        'AVDTP_SIGNAL_START',
        'avdtp_source_suspend',
        'avdtp_source_close',
        'remote_stream_cleanup_required')) {
    if (-not ($header.Contains($required) -or $core.Contains($required))) {
        throw "V1 silence core is missing: $required"
    }
}
$start = $core.IndexOf('avdtp_source_start')
$write = $core.LastIndexOf('SendZeroPackets')
$suspend = $core.LastIndexOf('avdtp_source_suspend')
$close = $core.LastIndexOf('avdtp_source_close')
if ($start -lt 0 -or $write -lt 0 -or $suspend -lt 0 -or $close -lt 0 -or
    $start -ge $write -or $write -ge $suspend -or $suspend -ge $close) {
    throw 'The silence transport sequence is not START/write/SUSPEND/CLOSE.'
}
foreach ($forbidden in @(
        'NativeLdacPcmPropertyRead',
        'IOCTL_LDAC_NATIVE_READ_PCM',
        'ReadPcm',
        'Sleep(')) {
    if (($core + $backend).IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "V1 silence transport gained forbidden authority: $forbidden"
    }
}
foreach ($required in @(
        'IOCTL_LDAC_NATIVE_WRITE_MEDIA',
        'LDAC_NATIVE_MAX_MEDIA_TRANSFER',
        'timeout_ms + 2000u')) {
    if (-not $backend.Contains($required)) {
        throw "V1 silence backend is missing: $required"
    }
}
if (-not $wrapper.Contains('#define V1_TRANSPORT_SILENCE_WORKER 1') -or
    -not $wrapper.Contains(
        '#include "v1_transport_configuration_worker.cpp"')) {
    throw 'The silence worker is not compile-time isolated from the zero-packet worker.'
}
foreach ($required in @(
        'V1TransportSilenceOptions',
        'RunV1TransportSilenceBurstOnce',
        'media_packets_written',
        'remote_stream_cleanup_required',
        'strictly_retryable_open_failure')) {
    if (-not $sharedWorker.Contains($required)) {
        throw "V1 silence worker result is missing: $required"
    }
}
foreach ($required in @(
        '--exercise-transport-silence',
        'transport-silence-exercise',
        'silence_sessions_completed',
        'only Win32 71 at',
        'OpenSignaling is retryable')) {
    if (-not $agent.Contains($required)) {
        throw "V1 silence agent mode is missing: $required"
    }
}
foreach ($required in @(
        'maximum_four_digital_zero_packets',
        'no_real_PCM',
        'no_driver_install',
        'no_reboot')) {
    if (-not $build.Contains($required)) {
        throw "V1 silence candidate contract is missing: $required"
    }
}
foreach ($required in @(
        'Test-V1SilenceBurstEvidence',
        'media_packets_written -eq 4',
        'remote_stream_cleanup_required -eq $false')) {
    if (-not $common.Contains($required)) {
        throw "V1 silence evidence is missing: $required"
    }
}
foreach ($required in @(
        '--exercise-transport-silence',
        'v1_transport_silence_worker.exe',
        'No audible sound is expected',
        'Stream idle,',
        'no install or reboot is required')) {
    if (-not $gate.Contains($required)) {
        throw "V1 silence hardware gate is missing: $required"
    }
}
foreach ($forbidden in @(
        'pnputil',
        'devcon',
        'Restart-Computer',
        'Disable-PnpDevice',
        'Enable-PnpDevice',
        'Stop-Service',
        'Start-Service')) {
    if ($gate.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "V1 silence runtime gate mutates the baseline: $forbidden"
    }
}
foreach ($required in @(
        'CancelAfterStartDoesNotSignalBlindly',
        'WriteFailureStopsBeforeFifthPacket',
        'OnePacketLimit',
        'TinyMtuNeverStarts')) {
    if (-not $tests.Contains($required)) {
        throw "V1 silence fault coverage is missing: $required"
    }
}

Write-Host 'V1 four-packet digital-zero transport policy tests passed.'
