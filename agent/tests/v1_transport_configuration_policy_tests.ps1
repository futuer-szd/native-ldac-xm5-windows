# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    return Get-Content -LiteralPath `
        (Join-Path $projectRoot $RelativePath) -Raw
}

$header = Read-ProjectFile `
    'agent\v1_transport_configuration_session.h'
$core = Read-ProjectFile `
    'agent\v1_transport_configuration_session.cpp'
$backend = Read-ProjectFile `
    'agent\v1_transport_configuration_driver_backend.cpp'
$worker = Read-ProjectFile `
    'agent\v1_transport_configuration_worker.cpp'
$silenceWrapper = Read-ProjectFile `
    'agent\v1_transport_silence_worker.cpp'
$cmake = Read-ProjectFile 'CMakeLists.txt'
$agent = Read-ProjectFile 'agent\v1_presence_agent.cpp'
$post = Read-ProjectFile `
    'tools\run-v1-post-reboot-discovery-gate.ps1'

foreach ($required in @(
        'RunV1TransportConfigurationOnce',
        'V1TransportConfigurationBackend',
        'set_configuration_accepted',
        'avdtp_open_accepted',
        'media_opened',
        'avdtp_close_accepted')) {
    if (-not ($header.Contains($required) -or $core.Contains($required))) {
        throw "V1 configuration session is missing: $required"
    }
}
foreach ($required in @(
        'AVDTP_SIGNAL_SET_CONFIGURATION',
        'AVDTP_SIGNAL_OPEN',
        'AVDTP_SIGNAL_CLOSE',
        'avdtp_source_media_channel_opened',
        'OpenMedia')) {
    if (-not $core.Contains($required)) {
        throw "V1 zero-packet sequence is missing: $required"
    }
}
foreach ($forbidden in @(
        'AVDTP_SIGNAL_START',
        'AVDTP_SIGNAL_SUSPEND',
        'WRITE_MEDIA',
        'Sleep(')) {
    if ($core.Contains($forbidden)) {
        throw "V1 configuration core exceeds zero-packet scope: $forbidden"
    }
}
foreach ($required in @(
        'IOCTL_LDAC_NATIVE_OPEN_MEDIA',
        'LDAC_NATIVE_CHANNEL_CONNECTED',
        'OutgoingMtu')) {
    if (-not $backend.Contains($required)) {
        throw "V1 configuration backend is missing: $required"
    }
}
foreach ($forbidden in @(
        'IOCTL_LDAC_NATIVE_WRITE_MEDIA',
        'IOCTL_LDAC_NATIVE_WRITE_SIGNALING')) {
    if ($backend.Contains($forbidden)) {
        throw "V1 configuration backend exceeds its IOCTL scope: $forbidden"
    }
}
foreach ($required in @(
        'strictly_retryable_open_failure',
        'media_start_commands',
        'media_packets_written',
        'RunV1TransportConfigurationOnce')) {
    if (-not $worker.Contains($required)) {
        throw "V1 configuration worker result is missing: $required"
    }
}
foreach ($forbidden in @(
        'SetEvent(media_started)',
        'IOCTL_LDAC_NATIVE_WRITE_MEDIA',
        'Sleep(')) {
    if ($worker.Contains($forbidden)) {
        throw "V1 configuration worker exceeds zero-packet scope: $forbidden"
    }
}
foreach ($required in @(
        '#ifdef V1_TRANSPORT_SILENCE_WORKER',
        '#if !defined(V1_TRANSPORT_SILENCE_WORKER)',
        '!defined(V1_TRANSPORT_PCM_WORKER)',
        'V1TransportSilenceOptions')) {
    if (-not $worker.Contains($required)) {
        throw "Shared worker lost compile-time silence isolation: $required"
    }
}
if (-not $silenceWrapper.Contains(
        '#define V1_TRANSPORT_SILENCE_WORKER 1') -or
    -not $silenceWrapper.Contains(
        '#include "v1_transport_configuration_worker.cpp"')) {
    throw 'The silence worker wrapper no longer owns the silence compile-time switch.'
}
$configurationTarget = [regex]::Match(
    $cmake,
    '(?s)add_executable\(v1_transport_configuration_worker.*?(?=add_executable\(v1_transport_configuration_mock_worker)').Value
if ([string]::IsNullOrWhiteSpace($configurationTarget) -or
    $configurationTarget.Contains('V1_TRANSPORT_SILENCE_WORKER') -or
    $configurationTarget.Contains('V1_TRANSPORT_PCM_WORKER')) {
    throw 'The normal zero-packet worker target enables a streaming path.'
}
foreach ($required in @(
        '--exercise-transport-configuration',
        'transport-configuration-exercise',
        'configuration_sessions_completed')) {
    if (-not $agent.Contains($required)) {
        throw "V1 agent configuration mode is missing: $required"
    }
}
foreach ($required in @(
        'Test-V1RebootConfigurationEvidence',
        'v1_transport_configuration_worker.exe',
        '--exercise-transport-configuration',
        'AVDTP START, media payload')) {
    if (-not $post.Contains($required)) {
        throw "V1 zero-packet hardware gate is missing: $required"
    }
}

Write-Host 'V1 transport zero-packet configuration policy tests passed.'
