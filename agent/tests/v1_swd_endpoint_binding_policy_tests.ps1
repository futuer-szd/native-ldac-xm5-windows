# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-File([string]$Path) {
    Get-Content -LiteralPath (Join-Path $projectRoot $Path) -Raw
}

$gate = Read-File 'tools\run-v1-swd-endpoint-binding-gate.ps1'
$common = Read-File 'tools\v1-swd-endpoint-binding-common.ps1'
$build = Read-File 'tools\build-v1-swd-endpoint-candidate.ps1'
$candidateCommon = Read-File `
    'tools\v1-swd-endpoint-candidate-common.ps1'
$cmake = Read-File 'CMakeLists.txt'

foreach ($required in @(
        "SupportsShouldProcess, ConfirmImpact = 'High'",
        'ConfirmV1SwdEndpointBinding',
        'requires PowerShell 7',
        'Assert-Administrator',
        "@('Root', 'TrustedPublisher')",
        "'/add-driver', `$packageInf",
        "'/remove-device',",
        '$script:V1SwdEndpointBindingInstanceId',
        "'/delete-driver', `$publishedInf, '/force'",
        'finally {',
        'package_remove_succeeded',
        'device_instance_absent',
        'published_candidate_endpoint_absent',
        'default_endpoint_written = $false',
        'certificate_imported = $false',
        'pnp_restarted = $false',
        'bluetooth_toggled = $false')) {
    if (-not $gate.Contains($required)) {
        throw "The SWD endpoint binding gate policy is missing: $required"
    }
}

foreach ($required in @(
        'V1SwdEndpointBindingPolicyVersion',
        'SWD\NativeLdacSwdEndpoint\Xm5EndpointCandidate',
        'SWD\NativeLdacAudioXm5',
        'NativeLdacSwdAudio',
        'Native LDAC SWD Speaker Topology',
        'Test-V1SwdEndpointBindingEvidence',
        'Test-V1SwdEndpointPublishedState',
        "default_roles -cne '(none)'")) {
    if (-not $common.Contains($required)) {
        throw "The SWD endpoint binding evidence contract is missing: $required"
    }
}

foreach ($forbiddenPackagedFile in @(
        "'v1-swd-endpoint-binding-common.ps1' =",
        "'run-v1-swd-endpoint-binding-gate.ps1' =")) {
    if ($build.Contains($forbiddenPackagedFile)) {
        throw "The staged-only endpoint candidate packages a system gate: $forbiddenPackagedFile"
    }
}

foreach ($forbidden in @(
        'Import-Certificate',
        '/install',
        '/uninstall',
        'Disable-PnpDevice',
        'Enable-PnpDevice',
        'Restart-PnpDevice',
        'BluetoothSetServiceState',
        'SetDefaultEndpoint',
        'SetMasterVolume')) {
    if (($gate + $common + $build).IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The isolated endpoint path contains a forbidden mutation: $forbidden"
    }
}

foreach ($required in @(
        'install_script_included',
        'device_creation_default',
        'current_root_endpoint_preserved')) {
    if (-not $candidateCommon.Contains($required)) {
        throw "The endpoint candidate contract is missing: $required"
    }
}

foreach ($required in @(
        'v1_swd_endpoint_binding_policy',
        'v1_swd_endpoint_binding_evidence')) {
    if (-not $cmake.Contains($required)) {
        throw "The endpoint binding CMake contract is missing: $required"
    }
}

Write-Host 'V1 SWD endpoint binding policy tests passed.'
