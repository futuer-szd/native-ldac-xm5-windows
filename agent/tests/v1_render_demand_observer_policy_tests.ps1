# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    return Get-Content -LiteralPath `
        (Join-Path $projectRoot $RelativePath) -Raw
}

$sink = Read-ProjectFile 'agent\v1_endpoint_presence_sink.cpp'
$agent = Read-ProjectFile 'agent\v1_presence_agent.cpp'
$build = Read-ProjectFile `
    'tools\build-v1-render-demand-observer.ps1'
$trial = Read-ProjectFile 'tools\run-v1-render-demand-trial.ps1'

foreach ($required in @(
        'QueryRenderActive',
        'NativeLdacPcmPropertyInfo',
        'NATIVE_LDAC_PCM_FLAG_STREAM_ACTIVE')) {
    if (-not $sink.Contains($required)) {
        throw "Render-demand sink is missing: $required"
    }
}
$queryStart = $sink.IndexOf(
    'V1EndpointPresenceSink::QueryRenderActive',
    [StringComparison]::Ordinal)
if ($queryStart -lt 0) {
    throw 'Could not isolate QueryRenderActive.'
}
$querySection = $sink.Substring($queryStart)
foreach ($forbidden in @(
        'NativeLdacPcmPropertyRead',
        'NativeLdacPcmPropertyLinkState',
        'NativeLdacPcmPropertyPreferredFormat')) {
    if ($querySection.Contains($forbidden)) {
        throw "Render-demand query exceeds GET Info authority: $forbidden"
    }
}

foreach ($required in @(
        '--observe-render-demand',
        'kRenderPollMs = 250u',
        'PollRenderDemand',
        'engine actions are recorded but suppressed')) {
    if (-not $agent.Contains($required)) {
        throw "Render-demand agent contract is missing: $required"
    }
}
foreach ($forbidden in @(
        'CreateProcessW',
        'ShellExecuteW')) {
    if ($agent.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "Render-demand observer can start a process: $forbidden"
    }
}

foreach ($required in @(
        'status --porcelain',
        'connected_only_PCM_Info_GET',
        'render_actions_observed_not_executed',
        'no_PCM_read',
        'no_child_process',
        'no_transport_open')) {
    if (-not $build.Contains($required)) {
        throw "Render-demand observer build is missing: $required"
    }
}
foreach ($required in @(
        'Assert-LegacyAdministrator',
        '--observe-render-demand',
        'render_started_events -ge 1',
        'render_stopped_events -ge 1',
        'transport_open_actions -eq 0',
        'child_processes_started -eq 0',
        'Link disconnected:')) {
    if (-not $trial.Contains($required)) {
        throw "Render-demand trial is missing: $required"
    }
}
foreach ($forbidden in @(
        '--discover',
        '--play-endpoint',
        'transport_probe',
        'Start-Process',
        'SetDefaultEndpoint',
        'pnputil',
        'devcon')) {
    if ($trial.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "Render-demand trial exceeds observer scope: $forbidden"
    }
}

Write-Host 'V1 render-demand observer policy tests passed.'
