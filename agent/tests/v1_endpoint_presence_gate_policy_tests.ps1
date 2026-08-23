# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    return Get-Content -LiteralPath `
        (Join-Path $projectRoot $RelativePath) -Raw
}

$build = Read-ProjectFile `
    'tools\build-v1-endpoint-presence-candidate.ps1'
$readiness = Read-ProjectFile `
    'tools\test-v1-endpoint-presence-readiness.ps1'
$install = Read-ProjectFile `
    'tools\install-v1-endpoint-presence-candidate.ps1'
$trial = Read-ProjectFile `
    'tools\run-v1-endpoint-presence-trial.ps1'

foreach ($required in @(
        'status --porcelain',
        'source_dirty = $false',
        'presence_lease_ms = 15000',
        'presence_heartbeat_ms = 5000',
        'build-xm5-connection-probe.ps1',
        'xm5_connection_probe.manifest.json',
        'no_transport_open')) {
    if (-not $build.Contains($required)) {
        throw "V1 endpoint candidate build is missing: $required"
    }
}
foreach ($required in @(
        'clean_original_a2dp',
        'test_signing_active',
        'ExpectedSourceCommit',
        "xm5State -ne 'disconnected'",
        'This preflight was read-only')) {
    if (-not $readiness.Contains($required)) {
        throw "V1 endpoint readiness is missing: $required"
    }
}
foreach ($required in @(
        'ConfirmV1EndpointInstall',
        'ShouldProcess',
        "'ROOT\NativeLdacAudio'",
        'cleanup-native-ldac-test-state.ps1',
        'safe_original_a2dp',
        'Physical presence absent:',
        'Link disconnected:',
        'does not accept a reboot-required')) {
    if (-not $install.Contains($required)) {
        throw "V1 endpoint installer is missing: $required"
    }
}
foreach ($forbidden in @(
        'LdacNative.inf',
        'NativeLdacDirectPdo.inf',
        'alta2dp.inf',
        'Set-Service',
        'Stop-Service',
        'Start-Service',
        'SetDefaultEndpoint')) {
    if ($install.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "V1 endpoint installer exceeds scope: $forbidden"
    }
}
foreach ($required in @(
        '--endpoint-presence',
        'Do not select Native LDAC or play audio',
        'transport_open_actions -eq 0',
        'child_processes_started -eq 0',
        'endpoint_presence_failures -eq 0',
        'Link disconnected:')) {
    if (-not $trial.Contains($required)) {
        throw "V1 endpoint trial is missing: $required"
    }
}
foreach ($forbidden in @(
        '--discover',
        '--play-endpoint',
        '--monitor',
        'transport_probe',
        'Start-Process')) {
    if ($trial.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "V1 endpoint trial can start media: $forbidden"
    }
}

Write-Host 'V1 endpoint presence gate policy tests passed.'
