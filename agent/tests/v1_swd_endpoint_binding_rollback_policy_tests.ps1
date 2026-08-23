# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$rollback = Get-Content -LiteralPath (Join-Path $projectRoot `
    'tools\rollback-v1-swd-endpoint-binding-gate.ps1') -Raw

foreach ($required in @(
        "SupportsShouldProcess, ConfirmImpact = 'High'",
        'ConfirmV1SwdEndpointBindingRollback',
        'requires PowerShell 7',
        'Assert-Administrator',
        '$failed.staged_published_inf',
        '$script:V1SwdEndpointBindingInstanceId',
        "'/remove-device',",
        "'/delete-driver', `$publishedInf, '/force'",
        'exact_swd_instance_only = $true',
        'exact_failed_oem_inf_only = $true',
        'current_root_endpoint_touched = $false',
        'transport_driver_touched = $false',
        'pnp_restarted = $false',
        'bluetooth_toggled = $false')) {
    if (-not $rollback.Contains($required)) {
        throw "The endpoint rollback policy is missing: $required"
    }
}

foreach ($forbidden in @(
        '/uninstall',
        '/install',
        'Import-Certificate',
        'Disable-PnpDevice',
        'Enable-PnpDevice',
        'Restart-PnpDevice',
        'BluetoothSetServiceState',
        'SetDefaultEndpoint',
        'SetMasterVolume')) {
    if ($rollback.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The endpoint rollback contains a forbidden mutation: $forbidden"
    }
}

Write-Host 'V1 SWD endpoint binding rollback policy tests passed.'
