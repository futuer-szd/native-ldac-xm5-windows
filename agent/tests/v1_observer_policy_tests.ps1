# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
$script = Get-Content -LiteralPath `
    (Join-Path $projectRoot 'tools\run-v1-connection-observer.ps1') -Raw
$probe = Get-Content -LiteralPath `
    (Join-Path $projectRoot 'tools\xm5_connection_probe.cpp') -Raw
$aclEvent = Get-Content -LiteralPath `
    (Join-Path $projectRoot 'agent\xm5_acl_event.cpp') -Raw
$observerImplementation = $probe + $aclEvent

foreach ($required in @(
        'clean_original_a2dp',
        'read_only_acl_pdo_render_timeline',
        '--observe-acl',
        'ACL connected.',
        'ACL disconnected.',
        'Write-Host $line',
        'no_avdtp_open = $true',
        'no_system_change = $true')) {
    if (-not $script.Contains($required)) {
        throw "V1 observer policy is missing: $required"
    }
}
foreach ($required in @(
        'GUID_BLUETOOTH_HCI_EVENT',
        'HCI_CONNECTION_TYPE_ACL',
        'QueryA2dpPdoSummary',
        'QueryRenderEndpointSummary',
        'No inquiry, connection request, AVDTP OPEN')) {
    if (-not $observerImplementation.Contains($required)) {
        throw "V1 observer implementation is missing: $required"
    }
}
foreach ($forbidden in @(
        'pnputil',
        'devcon',
        'Install-PnpDevice',
        'Disable-PnpDevice',
        'Enable-PnpDevice',
        'Stop-Service',
        'Start-Service',
        'Set-Service',
        'IOCTL_LDAC_NATIVE_OPEN_SIGNALING',
        '--discover',
        '--play-endpoint')) {
    if ($script.IndexOf($forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "V1 observer includes a forbidden operation: $forbidden"
    }
}

Write-Host 'V1 read-only observer policy tests passed.'
