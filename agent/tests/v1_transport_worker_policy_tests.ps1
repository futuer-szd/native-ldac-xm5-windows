# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    return Get-Content -LiteralPath `
        (Join-Path $projectRoot $RelativePath) -Raw
}

$agent = Read-ProjectFile 'agent\v1_presence_agent.cpp'
$hostSource = Read-ProjectFile 'agent\v1_engine_ready_host.cpp'
$header = Read-ProjectFile 'agent\v1_engine_ready_host.h'
$worker = Read-ProjectFile 'agent\v1_transport_configuration_worker.cpp'
$pcmSource = Read-ProjectFile 'agent\v1_transport_pcm_source_adapter.cpp'
$observerHost = Read-ProjectFile 'agent\v1_avrcp_observer_host.cpp'
$windowsSink = Read-ProjectFile 'agent\v1_avrcp_windows_sink.cpp'
$stub = Read-ProjectFile 'agent\v1_transport_worker_stub.cpp'
$build = Read-ProjectFile 'tools\build-v1-transport-worker-observer.ps1'
$trial = Read-ProjectFile 'tools\run-v1-transport-worker-trial.ps1'

foreach ($required in @(
        '--exercise-transport-worker',
        'StartTransportWorker',
        'AuthorizeTransportOpen',
        'CapabilitiesDiscovered',
        'V1TransportWorkerEvent::MediaStarted',
        'V1LifecycleEvent::MediaStarted',
        'V1EngineStopMode::GracefulTransport',
        'V1EngineStopMode::CancelTransport',
        'StartTransportDiscoveryWorker',
        'apply_endpoint_volume')) {
    if (-not $agent.Contains($required)) {
        throw "V1 transport-worker agent contract is missing: $required"
    }
}
if (-not $observerHost.Contains('write_response_observed') -or
    -not $observerHost.Contains('NldAvrcpObserverEventWriteResponse') -or
    $windowsSink.Contains('next_pending_retry_tick_') -or
    $windowsSink.Contains('now + 1000u')) {
    throw 'Pending AVRCP writes are not released directly by write responses.'
}
foreach ($required in @(
        'single_gain_ready_event_',
        'WaitForSingleObject(single_gain_ready_event_, 0u)',
        'native_pcm_source_set_apply_endpoint_volume')) {
    if (-not $pcmSource.Contains($required)) {
        throw "V1 PCM fail-safe single-gain contract is missing: $required"
    }
}
foreach ($required in @(
        'JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE',
        'CREATE_SUSPENDED',
        'transport_open_authorized_',
        'capabilities_discovered_event_',
        'ERROR_ALREADY_EXISTS',
        'graceful_transport_stop_event_',
        'cancel_transport_event_',
        'last_transport_stop_acknowledged_',
        '--single-gain-ready-event',
        'SetSingleGainReady',
        'apply_endpoint_volume')) {
    if (-not ($hostSource.Contains($required) -or
              $header.Contains($required))) {
        throw "V1 transport-worker host contract is missing: $required"
    }
}
foreach ($required in @(
        '--apply-endpoint-volume',
        '--single-gain-ready-event',
        'apply_endpoint_volume',
        'bool transport_authorized = false;',
        'transport_authorized = true;',
        'if (transport_authorized &&',
        'single_gain_requested',
        'run_options.single_gain_mode = single_gain_requested;',
        'run_options.require_stable_volume = !single_gain_requested;')) {
    if (-not $worker.Contains($required)) {
        throw "V1 transport-worker PCM worker contract is missing: $required"
    }
}
foreach ($forbidden in @(
        'Bluetooth',
        'Bth',
        'SetupDi',
        'DeviceIoControl',
        'NativeLdacPcmProperty',
        'transport_probe',
        'socket')) {
    if ($stub.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "Event-only transport stub exceeds its authority: $forbidden"
    }
}
foreach ($required in @(
        'event_only_transport_worker',
        'single_generation_open_authorization',
        'distinct_graceful_and_cancel_stop',
        'no_PCM_read',
        'no_Bluetooth_open')) {
    if (-not $build.Contains($required)) {
        throw "Transport-worker build contract is missing: $required"
    }
}
foreach ($required in @(
        '--exercise-transport-worker',
        'transport_open_actions -eq 1',
        'transport_open_executed -eq 1',
        'media_started_events -eq 1',
        'media_stopped_events -eq 1',
        'transport_stop_acknowledgements -eq 1',
        'Link disconnected:')) {
    if (-not $trial.Contains($required)) {
        throw "Transport-worker trial contract is missing: $required"
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
        throw "Transport-worker trial exceeds observer scope: $forbidden"
    }
}

Write-Host 'V1 event-only transport-worker policy tests passed.'
