# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-ReconnectProjectFile([string]$RelativePath) {
    Get-Content -LiteralPath (Join-Path $root $RelativePath) -Raw
}

$agent = Read-ReconnectProjectFile 'agent\v1_presence_agent.cpp'
$common = Read-ReconnectProjectFile `
    'tools\v1-playback-reconnect-common.ps1'
$gate = Read-ReconnectProjectFile `
    'tools\run-v1-playback-reconnect-gate.ps1'
$builder = Read-ReconnectProjectFile `
    'tools\build-v1-normal-stop-candidate.ps1'
$cmake = Read-ReconnectProjectFile 'CMakeLists.txt'

foreach ($required in @(
        '--await-playback-reconnect',
        'playback_reconnect_wait_enabled',
        'playback_reconnect_target_generations',
        '--playback-reconnect-generations',
        'playback_disconnect_target',
        'ArchiveGenerationState',
        '.generation-',
        'V1 reconnect checkpoint reached',
        'state.connected_events == playback_disconnect_target',
        'state.disconnected_events == playback_disconnect_target')) {
    if (-not $agent.Contains($required)) {
        throw "The two-generation presence host is missing: $required"
    }
}
foreach ($required in @(
        '$script:V1PlaybackReconnectPolicyVersion = 21',
        'Test-V1PlaybackReconnectPrerequisite',
        'Get-V1PlaybackReconnectGenerationState',
        'Test-V1PlaybackReconnectEvidence',
        'Test-V1PlaybackDisconnectEvidence',
        'playback_reconnect_wait_enabled',
        'playback_reconnect_target_generations -ne 2',
        'transport_open_stable_authorizations -ne 2',
        'transport_retries_scheduled -ne 0',
        'Test-V1PlaybackDisconnectSameArchive')) {
    if (-not $common.Contains($required)) {
        throw "The reconnect evidence contract is missing: $required"
    }
}
foreach ($required in @(
        '-ConfirmV1PlaybackReconnect',
        '[ValidateRange(360,420)][int]$DurationSeconds = 420',
        '$PSVersionTable.PSEdition -ne ''Core''',
        'requires PowerShell 7',
        "pattern='(?m)^Stream idle[:,]'",
        'two_generation_playback_reconnect_evidence',
        'Test-V1PlaybackReconnectPrerequisite',
        '--observe-acl',
        '--monitor-state',
        'endpointPreflightFailures',
        'Native endpoint preflight failed:',
        '--await-playback-reconnect',
        '--transport-open-render-stability-ms',
        '--render-start-timeout-ms 45000',
        '.generation-$generation.attempt-1.json',
        'IntermediatePublicDisconnectObserved',
        'EndpointReconnectObserved',
        'FinalPublicDisconnectObserved',
        'finalPublicDisconnectTimer.ElapsedMilliseconds -ge 30000',
        "status = 'playback-reconnect-verified'",
        'No reboot or rollback is required.')) {
    if (-not $gate.Contains($required)) {
        throw "The reconnect hardware gate is missing: $required"
    }
}
foreach ($testName in @(
        'v1_playback_reconnect_policy',
        'v1_playback_reconnect_evidence')) {
    $registration = "add_test(NAME $testName"
    $start = $cmake.IndexOf($registration)
    if ($start -lt 0) {
        throw "The reconnect CTest registration is missing: $testName"
    }
    $next = $cmake.IndexOf('add_test(NAME ', $start + $registration.Length)
    $length = if ($next -lt 0) { $cmake.Length - $start } else { $next - $start }
    if (-not $cmake.Substring($start, $length).Contains(
            'COMMAND pwsh.exe')) {
        throw "The reconnect CTest does not use PowerShell 7: $testName"
    }
}
if (-not $builder.Contains(
        "'two_generation_playback_reconnect_evidence'")) {
    throw 'The shared candidate does not advertise reconnect evidence support.'
}
foreach ($forbidden in @(
        'pnputil', 'devcon', 'Restart-Computer', 'Disable-PnpDevice',
        'Enable-PnpDevice', 'Stop-Service', 'Start-Service',
        'SetDefaultEndpoint', 'Set-NativeLdacBluetoothRadioState')) {
    if ($gate.IndexOf(
            $forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The reconnect gate mutates the verified baseline: $forbidden"
    }
}
foreach ($relative in @(
        'tools\v1-playback-reconnect-common.ps1',
        'tools\run-v1-playback-reconnect-gate.ps1',
        'agent\tests\v1_playback_reconnect_evidence_tests.ps1',
        'agent\tests\v1_playback_reconnect_policy_tests.ps1')) {
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $root $relative), [ref]$tokens, [ref]$errors)
    if (@($errors).Count -ne 0) {
        throw "The reconnect PowerShell file does not parse: $relative"
    }
}

Write-Host 'V1 playback reconnect policy tests passed.'
