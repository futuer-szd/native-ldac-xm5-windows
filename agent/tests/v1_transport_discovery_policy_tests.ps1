# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    return Get-Content -LiteralPath `
        (Join-Path $projectRoot $RelativePath) -Raw
}

$header = Read-ProjectFile 'agent\v1_transport_session.h'
$core = Read-ProjectFile 'agent\v1_transport_session.cpp'
$backend = Read-ProjectFile 'agent\v1_transport_driver_backend.cpp'
$backendHeader = Read-ProjectFile `
    'agent\v1_transport_driver_backend.h'
$worker = Read-ProjectFile 'agent\v1_transport_discovery_worker.cpp'
$agent = Read-ProjectFile 'agent\v1_presence_agent.cpp'
$tests = Read-ProjectFile `
    'agent\tests\v1_transport_session_tests.cpp'

foreach ($required in @(
        'RunV1TransportDiscoveryOnce',
        'V1TransportCancelProbe',
        'primary_disposition',
        'open_attempts',
        'signaling_exchanges',
        'close_attempted',
        'close_succeeded')) {
    if (-not ($header.Contains($required) -or
              $core.Contains($required))) {
        throw "V1 discovery session contract is missing: $required"
    }
}

foreach ($required in @(
        '--session-result',
        '--capabilities-discovered-event',
        '--retryable-open-failure-event',
        'IsStrictlyRetryableOpenFailure',
        'ERROR_REQ_NOT_ACCEP',
        'V1TransportDiscoveryStage::OpenSignaling',
        'result.signaling_exchanges == 0u',
        'SetEvent(retryable_open_failure)',
        'RunV1TransportDiscoveryOnce',
        'WriteResult',
        'SetEvent(discovered)',
        'SetEvent(media_failed)',
        'SetEvent(media_stopped)',
        'V1TransportDiscoveryDisposition::Succeeded')) {
    if (-not $worker.Contains($required)) {
        throw "V1 contained discovery worker is missing: $required"
    }
}
foreach ($forbidden in @(
        'IOCTL_LDAC_NATIVE_OPEN_MEDIA',
        'IOCTL_LDAC_NATIVE_WRITE_MEDIA',
        'AVDTP_SIGNAL_SET_CONFIGURATION',
        'AVDTP_SIGNAL_OPEN',
        'AVDTP_SIGNAL_START',
        'SetEvent(media_started)',
        'pnputil',
        'devcon')) {
    if ($worker.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "V1 contained discovery worker exceeds scope: $forbidden"
    }
}
foreach ($required in @(
        '--exercise-transport-discovery',
        '--transport-result',
        'StartTransportDiscoveryWorker',
        'CapabilitiesDiscovered',
        'TransportOpenSuppressed',
        'discovery_sessions_completed',
        'TransportRetryableFailure',
        'TransportRetryDue',
        'GetV1TransportRetryDelayMs',
        'ArchiveTransportAttemptResult')) {
    if (-not $agent.Contains($required)) {
        throw "V1 discovery-only agent path is missing: $required"
    }
}
foreach ($forbidden in @(
        'Sleep(',
        'ERROR_BUSY',
        'ERROR_NOT_READY',
        'ERROR_DEVICE_NOT_CONNECTED',
        'ERROR_TIMEOUT')) {
    if ($worker.Contains($forbidden)) {
        throw "V1 contained discovery worker broadens retry scope: $forbidden"
    }
}
foreach ($required in @(
        'AVDTP_SIGNAL_DISCOVER',
        'AVDTP_SIGNAL_GET_ALL_CAPABILITIES',
        'AVDTP_SIGNAL_GET_CAPABILITIES',
        'ldac_find_in_service_capabilities',
        'ldac_choose_configuration',
        'V1TransportDiscoveryDisposition::Cancelled',
        'V1TransportDiscoveryDisposition::CleanupFailure')) {
    if (-not $core.Contains($required)) {
        throw "V1 discovery core is missing: $required"
    }
}
foreach ($forbidden in @(
        'windows.h',
        'SetupDi',
        'DeviceIoControl',
        'CreateFile',
        'IOCTL_LDAC_NATIVE',
        'Sleep(',
        'AVDTP_SIGNAL_SET_CONFIGURATION',
        'AVDTP_SIGNAL_OPEN',
        'AVDTP_SIGNAL_START',
        'AVDTP_SIGNAL_SUSPEND')) {
    if ($core.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "V1 discovery core exceeds capability-only scope: $forbidden"
    }
}

foreach ($required in @(
        'GUID_DEVINTERFACE_LDAC_NATIVE_TRANSPORT',
        'FILE_FLAG_OVERLAPPED',
        'IOCTL_LDAC_NATIVE_GET_VERSION',
        'IOCTL_LDAC_NATIVE_GET_DEVICE_INFO',
        'IOCTL_LDAC_NATIVE_OPEN_SIGNALING',
        'IOCTL_LDAC_NATIVE_READ_SIGNALING',
        'IOCTL_LDAC_NATIVE_WRITE_SIGNALING',
        'IOCTL_LDAC_NATIVE_CLOSE_CHANNELS',
        'WaitForMultipleObjects',
        'CancelIoEx')) {
    if (-not ($backend.Contains($required) -or
              $backendHeader.Contains($required))) {
        throw "V1 driver backend is missing: $required"
    }
}
foreach ($forbidden in @(
        'IOCTL_LDAC_NATIVE_OPEN_MEDIA',
        'IOCTL_LDAC_NATIVE_WRITE_MEDIA',
        'Sleep(',
        'open_signaling_with_retry',
        'SET_CONFIGURATION')) {
    if ($backend.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "V1 driver backend exceeds DISCOVER scope: $forbidden"
    }
}
$openCount = [regex]::Matches(
    $backend,
    'IOCTL_LDAC_NATIVE_OPEN_SIGNALING').Count
if ($openCount -ne 1) {
    throw "V1 driver backend must contain one OPEN call site; found $openCount."
}
$readIndex = $backend.IndexOf('IOCTL_LDAC_NATIVE_READ_SIGNALING')
$writeIndex = $backend.IndexOf('IOCTL_LDAC_NATIVE_WRITE_SIGNALING')
if ($readIndex -lt 0 -or $writeIndex -lt 0 -or
    $readIndex -ge $writeIndex) {
    throw 'V1 driver backend must submit READ before WRITE.'
}

foreach ($required in @(
        'Scenario::Happy',
        'Scenario::LegacyFallback',
        'Scenario::NoLdac',
        'Scenario::WrongLabel',
        'Scenario::RejectDiscover',
        'Scenario::FailOpen',
        'Scenario::FailSecondExchange',
        'Scenario::FailClose',
        'Scenario::CancelDuringSecondExchange')) {
    if (-not $tests.Contains($required)) {
        throw "V1 discovery fault coverage is missing: $required"
    }
}

Write-Host 'V1 transport DISCOVER policy tests passed.'
