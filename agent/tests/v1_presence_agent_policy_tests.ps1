# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
$agent = Get-Content -LiteralPath `
    (Join-Path $projectRoot 'agent\v1_presence_agent.cpp') -Raw
$watcher = Get-Content -LiteralPath `
    (Join-Path $projectRoot 'agent\xm5_acl_watcher.cpp') -Raw
$trial = Get-Content -LiteralPath `
    (Join-Path $projectRoot 'tools\run-v1-presence-agent-trial.ps1') -Raw
$implementation = $agent + $watcher
$avrcpControl = Get-Content -LiteralPath `
    (Join-Path $projectRoot 'direct-pdo\include\nativeldac_avrcp_control.h') -Raw
$avrcpControlSource = Get-Content -LiteralPath `
    (Join-Path $projectRoot 'direct-pdo\src\nativeldac_avrcp_control.c') -Raw

foreach ($required in @(
        'Xm5AclWatcher',
        'ParseXm5AclDeviceChange',
        'V1LifecycleEvent::AclConnected',
        'V1LifecycleEvent::AclDisconnected',
        'WaitForSingleObject(watcher.change_event()',
        'transport_open_actions',
        'child_processes_started')) {
    if (-not $implementation.Contains($required)) {
        throw "V1 presence agent is missing: $required"
    }
}
foreach ($required in @(
        'NLD_AVRCP_CONTROL_CONTEXT',
        'NldAvrcpControlPercentToXm5',
        'NldAvrcpControlXm5ToPercent',
        'NldAvrcpControlSetWindowsPercent',
        'NldAvrcpControlIsReady')) {
    if (-not ($avrcpControl.Contains($required) -or
              $avrcpControlSource.Contains($required))) {
        throw "Direct-PDO AVRCP control contract is missing: $required"
    }
}
foreach ($required in @(
        'clean_original_a2dp',
        'V1 presence agent armed',
        'transport_open_actions',
        'child_processes_started',
        'no_system_change = $true')) {
    if (-not $trial.Contains($required)) {
        throw "V1 presence trial is missing: $required"
    }
}
foreach ($forbidden in @(
        'CreateProcessW',
        'DeviceIoControl',
        'IOCTL_LDAC',
        'transport_probe',
        'ldac_direct_engine',
        'QueryXm5Connection')) {
    if ($implementation.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "V1 presence agent includes a forbidden operation: $forbidden"
    }
}
foreach ($forbidden in @(
        'pnputil',
        'devcon',
        'Disable-PnpDevice',
        'Enable-PnpDevice',
        'Stop-Service',
        'Start-Service',
        '--discover',
        '--play-endpoint')) {
    if ($trial.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "V1 presence trial includes a forbidden operation: $forbidden"
    }
}

Write-Host 'V1 presence agent policy tests passed.'
