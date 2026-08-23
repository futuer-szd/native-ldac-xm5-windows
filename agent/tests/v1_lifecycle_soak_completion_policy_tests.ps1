# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$path = Join-Path $root 'tools\complete-v1-lifecycle-soak-gate.ps1'
$completion = Get-Content -LiteralPath $path -Raw

foreach ($required in @(
        '-ConfirmV1LifecycleSoakCompletion',
        'requires PowerShell 7',
        '$script:V1LifecycleSoakRecoverableTransactionRelativePath',
        "'Invalid argument: --monitor-state'",
        'Test-V1LifecycleSoakEndpointAclTimeline',
        'Test-V1LifecycleSoakAclTimeline',
        'Test-V1LifecycleSoakEvidence',
        '-AgentExitCode 0',
        'Get-NativeLdacXm5BluetoothState',
        'Get-V1NormalStopBaselineAssessment',
        "'completed-result.json'",
        "status = 'lifecycle-soak-verified'",
        'finalized_after_monitor_compatibility_failure',
        'Write-LegacyJsonAtomic')) {
    if (-not $completion.Contains($required)) {
        throw "The lifecycle-soak completion contract is missing: $required"
    }
}
foreach ($forbidden in @(
        'pnputil', 'devcon', 'Restart-Computer', 'Restart-PnpDevice',
        'Disable-PnpDevice', 'Enable-PnpDevice', 'Stop-Service',
        'Start-Service', 'SetDefaultEndpoint',
        'Set-NativeLdacBluetoothRadioState', 'Set-V1EndpointLinkState')) {
    if ($completion.IndexOf(
            $forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The lifecycle-soak completion mutates system state: $forbidden"
    }
}
$tokens = $null
$errors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile(
    $path, [ref]$tokens, [ref]$errors)
if (@($errors).Count -ne 0) {
    throw 'The lifecycle-soak completion script does not parse.'
}

Write-Host 'V1 lifecycle soak completion policy tests passed.'
