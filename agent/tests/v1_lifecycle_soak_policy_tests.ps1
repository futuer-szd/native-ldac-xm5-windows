# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-SoakProjectFile([string]$RelativePath) {
    Get-Content -LiteralPath (Join-Path $root $RelativePath) -Raw
}

$agent = Read-SoakProjectFile 'agent\v1_presence_agent.cpp'
$endpointProbe = Read-SoakProjectFile 'tools\endpoint_volume_probe.cpp'
$common = Read-SoakProjectFile 'tools\v1-lifecycle-soak-common.ps1'
$gate = Read-SoakProjectFile 'tools\run-v1-lifecycle-soak-gate.ps1'
$completion = Read-SoakProjectFile `
    'tools\complete-v1-lifecycle-soak-gate.ps1'
$builder = Read-SoakProjectFile 'tools\build-v1-normal-stop-candidate.ps1'
$cmake = Read-SoakProjectFile 'CMakeLists.txt'

foreach ($required in @(
        '--playback-reconnect-generations',
        'playback_reconnect_target_generations',
        'playback_reconnect_generations >= 2u',
        'playback_reconnect_generations <= 3u',
        'state.playback_reconnect_target_generations',
        'state.disconnected_events <',
        'state.disconnected_events + 1u')) {
    if (-not $agent.Contains($required)) {
        throw "The three-generation presence host is missing: $required"
    }
}
foreach ($required in @(
        '$script:V1LifecycleSoakPolicyVersion = 22',
        'Test-V1LifecycleSoakPrerequisite',
        'Test-V1LifecycleSoakEndpointTimeline',
        'Test-V1LifecycleSoakEndpointAclTimeline',
        'Test-V1LifecycleSoakAclTimeline',
        'Test-V1LifecycleSoakEvidence',
        'Test-V1NormalStopEvidence',
        'Test-V1PlaybackDisconnectEvidence',
        'transport_graceful_stop_actions -ne 2',
        'transport_cancel_actions -ne 1')) {
    if (-not $common.Contains($required)) {
        throw "The lifecycle-soak evidence contract is missing: $required"
    }
}
if (-not $endpointProbe.Contains('parsed > 600u')) {
    throw 'The read-only endpoint monitor does not cover the soak hard bound.'
}
foreach ($required in @(
        '-ConfirmV1LifecycleSoak',
        '[ValidateRange(540,600)][int]$DurationSeconds = 600',
        'requires PowerShell 7',
        'three_generation_lifecycle_soak_evidence',
        '--playback-reconnect-generations 3',
        '--render-start-timeout-ms 60000',
        "@('graceful-stop', 'physical-disconnect', 'graceful-stop')",
        'Test-V1LifecycleSoakPrerequisite',
        'Test-V1LifecycleSoakEvidence',
        'Start-Sleep -Seconds 20',
        'Get-V1NormalStopBaselineAssessment',
        "status = 'lifecycle-soak-verified'",
        'No reboot or rollback is required.')) {
    if (-not $gate.Contains($required)) {
        throw "The lifecycle-soak hardware gate is missing: $required"
    }
}
if (-not $builder.Contains(
        "'three_generation_lifecycle_soak_evidence'")) {
    throw 'The shared candidate does not advertise lifecycle-soak evidence support.'
}
foreach ($testName in @(
        'v1_lifecycle_soak_policy', 'v1_lifecycle_soak_evidence')) {
    $registration = "add_test(NAME $testName"
    $start = $cmake.IndexOf($registration)
    if ($start -lt 0) {
        throw "The lifecycle-soak CTest registration is missing: $testName"
    }
    $next = $cmake.IndexOf('add_test(NAME ', $start + $registration.Length)
    $length = if ($next -lt 0) { $cmake.Length - $start } else { $next - $start }
    if (-not $cmake.Substring($start, $length).Contains(
            'COMMAND pwsh.exe')) {
        throw "The lifecycle-soak CTest does not use PowerShell 7: $testName"
    }
}
foreach ($forbidden in @(
        'pnputil', 'devcon', 'Restart-Computer', 'Disable-PnpDevice',
        'Enable-PnpDevice', 'Stop-Service', 'Start-Service',
        'SetDefaultEndpoint', 'Set-NativeLdacBluetoothRadioState')) {
    if ($gate.IndexOf(
            $forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The lifecycle-soak gate mutates the frozen baseline: $forbidden"
    }
}
foreach ($required in @(
        '-ConfirmV1LifecycleSoakCompletion',
        'Test-V1LifecycleSoakEndpointAclTimeline',
        "'Invalid argument: --monitor-state'",
        "status = 'lifecycle-soak-verified'")) {
    if (-not $completion.Contains($required)) {
        throw "The lifecycle-soak evidence completion is missing: $required"
    }
}
foreach ($relative in @(
        'tools\v1-lifecycle-soak-common.ps1',
        'tools\run-v1-lifecycle-soak-gate.ps1',
        'tools\complete-v1-lifecycle-soak-gate.ps1',
        'agent\tests\v1_lifecycle_soak_evidence_tests.ps1',
        'agent\tests\v1_lifecycle_soak_policy_tests.ps1',
        'agent\tests\v1_lifecycle_soak_completion_policy_tests.ps1')) {
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $root $relative), [ref]$tokens, [ref]$errors)
    if (@($errors).Count -ne 0) {
        throw "The lifecycle-soak PowerShell file does not parse: $relative"
    }
}

Write-Host 'V1 lifecycle soak policy tests passed.'
