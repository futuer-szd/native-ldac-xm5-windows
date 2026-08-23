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
$stub = Read-ProjectFile 'agent\v1_engine_ready_stub.cpp'
$sink = Read-ProjectFile 'agent\v1_endpoint_presence_sink.cpp'
$build = Read-ProjectFile 'tools\build-v1-engine-ready-observer.ps1'
$trial = Read-ProjectFile 'tools\run-v1-engine-ready-trial.ps1'

foreach ($required in @(
        '--observe-engine-ready',
        '--engine-stub',
        'TransportOpenSuppressed',
        'transport OPEN was requested by',
        'transport_open_executed')) {
    if (-not $agent.Contains($required)) {
        throw "Engine-ready agent contract is missing: $required"
    }
}
foreach ($required in @(
        'JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE',
        'CREATE_SUSPENDED',
        'AssignProcessToJobObject',
        'ResumeThread',
        'CREATE_NO_WINDOW')) {
    if (-not $hostSource.Contains($required)) {
        throw "Contained engine host is missing: $required"
    }
}
foreach ($forbidden in @(
        'Bluetooth',
        'SetupDi',
        'NativeLdacPcmPropertyRead',
        'NativeLdacPcmPropertyLinkState')) {
    if ($stub.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "No-media stub exceeds its authority: $forbidden"
    }
}
if ($sink.Contains('CreateProcessW')) {
    throw 'Endpoint presence sink gained process-start authority.'
}

foreach ($required in @(
        'job_object_containment',
        'engine_ready_event',
        'transport_OPEN_requested_not_executed',
        'no_PCM_read',
        'no_Bluetooth_open')) {
    if (-not $build.Contains($required)) {
        throw "Engine-ready build contract is missing: $required"
    }
}
foreach ($required in @(
        '--observe-engine-ready',
        '--engine-stub',
        'engine_ready_events -eq 1',
        'engine_graceful_stops -eq 1',
        'transport_open_actions -eq 1',
        'transport_open_executed -eq 0',
        'Link disconnected:')) {
    if (-not $trial.Contains($required)) {
        throw "Engine-ready trial contract is missing: $required"
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
        throw "Engine-ready trial exceeds observer scope: $forbidden"
    }
}

Write-Host 'V1 engine-ready observer policy tests passed.'
