# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$source = Get-Content -LiteralPath (Join-Path $projectRoot `
    'tools\v1_swd_child_probe.cpp') -Raw
$cmake = Get-Content -LiteralPath (Join-Path $projectRoot `
    'CMakeLists.txt') -Raw

foreach ($required in @(
        'create_info.pszzHardwareIds = nullptr',
        'create_info.pszzCompatibleIds = nullptr',
        'SWDeviceCapabilitiesNoDisplayInUI',
        'SWDeviceLifetimeHandle',
        '--confirm-driverless-probe',
        'parsed > 30ul',
        'SwDeviceClose(device)',
        'driver or interface is')) {
    if (-not $source.Contains($required)) {
        throw "The driverless SWD probe policy is missing: $required"
    }
}
foreach ($forbidden in @(
        'SWDeviceCapabilitiesDriverRequired',
        'SwDeviceLifetimeParentPresent',
        'SwDeviceInterfaceRegister',
        'ROOT\\NativeLdacAudio',
        'pnputil',
        'devcon')) {
    if ($source.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The driverless SWD probe contains a forbidden path: $forbidden"
    }
}
foreach ($required in @(
        'add_executable(v1_swd_child_probe',
        'target_link_libraries(v1_swd_child_probe PRIVATE onecoreuap ole32)',
        'v1_swd_child_probe_help',
        'v1_swd_child_probe_requires_confirmation',
        'v1_swd_child_probe_policy')) {
    if (-not $cmake.Contains($required)) {
        throw "The SWD probe CMake policy is missing: $required"
    }
}

Write-Host 'V1 driverless SWD child probe policy tests passed.'
