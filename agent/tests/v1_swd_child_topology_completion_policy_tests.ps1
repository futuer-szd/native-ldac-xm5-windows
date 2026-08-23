# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$completion = Get-Content -LiteralPath (Join-Path $projectRoot `
    'tools\complete-v1-swd-child-topology-gate.ps1') -Raw
$common = Get-Content -LiteralPath (Join-Path $projectRoot `
    'tools\v1-swd-child-topology-common.ps1') -Raw
$cmake = Get-Content -LiteralPath (Join-Path $projectRoot `
    'CMakeLists.txt') -Raw

foreach ($required in @(
        'ConfirmV1SwdChildTopologyCompletion',
        '[int]$original.policy_version -ne 1',
        'Test-V1SwdChildLifecycleEvidence',
        'merge-base --is-ancestor',
        'active_child_absent_after_close = $true',
        'system_experiment_rerun_required = $false',
        'No third system-level creation is required')) {
    if (-not $completion.Contains($required)) {
        throw "The SWD completion policy is missing: $required"
    }
}
foreach ($forbidden in @(
        'SwDeviceCreate',
        'pnputil.exe',
        'Disable-PnpDevice',
        'Enable-PnpDevice',
        'BluetoothSetServiceState',
        'SetDefaultEndpoint')) {
    if ($completion.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The SWD completion contains a forbidden mutation: $forbidden"
    }
}
foreach ($required in @(
        'V1SwdChildTopologyPolicyVersion = 2',
        "V1SwdChildInboxInf = 'c_swdevice.inf'",
        'custom_driver_binding -ne $false')) {
    if (-not $common.Contains($required)) {
        throw "The corrected SWD evidence contract is missing: $required"
    }
}
if (-not $cmake.Contains('v1_swd_child_topology_completion_policy')) {
    throw 'The SWD topology completion policy is not registered in CTest.'
}

Write-Host 'V1 SWD child topology completion policy tests passed.'
