# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
. (Join-Path $projectRoot 'tools\legacy-open-diagnostic-common.ps1')

function Assert-Equal {
    param($Expected, $Actual, [string]$Label)
    if ($Expected -ne $Actual) {
        throw "$Label`: expected '$Expected', got '$Actual'."
    }
}

$base = 'L2CAP OPEN diagnostic #1: signaling, IO 0xC00000D0, BRB 0xC00000D0, Bluetooth 0x00000000, PSM 0x0019, channel flags 0x00000031.'
$cases = @(
    [pscustomobject]@{ code = 2; name = 'PSM not supported'; expected = 'remote_psm_not_supported' },
    [pscustomobject]@{ code = 3; name = 'security block'; expected = 'remote_security_block' },
    [pscustomobject]@{ code = 4; name = 'no resources'; expected = 'remote_no_resources' },
    [pscustomobject]@{ code = 9; name = 'unknown'; expected = 'remote_unclassified_response' }
)
foreach ($case in $cases) {
    $text = "$base`r`nRemote L2CAP response: $($case.code) ($($case.name)), status 0.`r`n"
    $summary = Get-LegacyOpenDiagnosticSummary -Text $text `
        -DiscoveryPassed $false
    Assert-Equal $case.expected $summary.diagnostic_disposition `
        "response $($case.code) disposition"
    Assert-Equal $case.code $summary.remote_response.code `
        "response $($case.code) code"
}

$local = Get-LegacyOpenDiagnosticSummary `
    -Text "$base`nNo valid negative remote L2CAP response was reported.`n" `
    -DiscoveryPassed $false
Assert-Equal 'no_valid_remote_response' $local.diagnostic_disposition `
    'local disposition'

$missing = Get-LegacyOpenDiagnosticSummary -Text 'OPEN_SIGNALING failed.' `
    -DiscoveryPassed $false
Assert-Equal 'open_diagnostic_missing' $missing.diagnostic_disposition `
    'missing disposition'

$passed = Get-LegacyOpenDiagnosticSummary -Text '' -DiscoveryPassed $true
Assert-Equal 'avdtp_discover_passed' $passed.diagnostic_disposition `
    'passed disposition'

Write-Host 'Legacy L2CAP OPEN diagnostic classification tests passed.'
