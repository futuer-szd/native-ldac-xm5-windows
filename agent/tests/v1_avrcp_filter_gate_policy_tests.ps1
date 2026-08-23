# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'AVRCP filter gate policy tests require PowerShell 7.'
}

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-RepoFile([string]$Path) {
    Get-Content -LiteralPath (Join-Path $root $Path) -Raw
}
function Assert-Policy([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$commonPath = Join-Path $root `
    'tools\v1-avrcp-filter-gate-common.ps1'
. $commonPath
$common = Get-Content -LiteralPath $commonPath -Raw
$gate = Read-RepoFile 'tools\run-v1-avrcp-filter-install-gate.ps1'
$rollback = Read-RepoFile `
    'tools\rollback-v1-avrcp-filter-install-gate.ps1'
$probe = Read-RepoFile 'tools\v1_avrcp_filter_probe.cpp'
$builder = Read-RepoFile 'tools\build-v1-avrcp-filter-candidate.ps1'
$verifier = Read-RepoFile 'tools\verify-v1-avrcp-filter-candidate.ps1'

Assert-Policy ($common -match [regex]::Escape(
    'BTHENUM\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0')) `
    'The gate common helper is not limited to the exact XM5 AVRCP PDO.'
Assert-Policy ($common -match "NativeLdacAvrcpIoFilter\.inf" -and
               $common -match "microsoft_bluetooth_avrcptransport\.inf" -and
               $common -match "Microsoft_Bluetooth_AvrcpTransport") `
    'The filter package or Microsoft baseline identifiers changed.'
Assert-Policy ($gate -match 'SupportsShouldProcess\s*=\s*\$true' -and
               $gate -match "ConfirmImpact\s*=\s*'High'" -and
               $gate -match 'ConfirmV1AvrcpFilterInstall') `
    'The live install gate does not require high-impact confirmation.'
Assert-Policy ($rollback -match 'SupportsShouldProcess\s*=\s*\$true' -and
               $rollback -match 'ConfirmV1AvrcpFilterRollback') `
    'The rollback command does not require explicit confirmation.'
Assert-Policy ($gate -match 'verify-v1-avrcp-filter-candidate' -and
               $gate -match 'verify-v1-golden-checkpoint' -and
               $gate -match 'requires a policy 7 candidate' -and
               $gate -match 'candidate must match clean Git HEAD') `
    'The gate does not bind only a verified clean-HEAD candidate.'
Assert-Policy ($verifier -match '\$policyVersion -notin @\(2, 3, 4, 5, 6, 7\)' -and
               $verifier -match '\$policyVersion -eq 3' -and
               $verifier -match '\$policyVersion -eq 4' -and
               $verifier -match '\$policyVersion -eq 5' -and
               $verifier -match '\$policyVersion -eq 6' -and
               $verifier -match '\$policyVersion -eq 7') `
    'The verifier cannot roll back frozen packages while enforcing policy 7 for new installs.'
Assert-Policy ($verifier -match '(?s)if \(\$policyVersion -ge 4 -and\s+-not \(Test-Path -LiteralPath \$transportProbePath -PathType Leaf\)\)' -and
               $verifier -notmatch '(?s)\$aclProbePath.*?-or\s+-not \(Test-Path -LiteralPath \$transportProbePath -PathType Leaf\).*?\$importsPath') `
    'The verifier requires the policy 4 transport probe from frozen policy 2/3 rollback candidates.'
Assert-Policy ($gate -match 'TESTSIGNING' -and
               $gate -match 'LocalMachine\\Root' -and
               $gate -match 'LocalMachine\\TrustedPublisher' -and
               $gate -notmatch 'Import-Certificate') `
    'The gate does not require pre-existing test trust or imports a certificate.'
Assert-Policy ($gate -match [regex]::Escape(
                   "'/add-driver', `$candidateInf, '/install'") -and
               $gate -match [regex]::Escape(
                   "'/restart-device', `$baseline.instance_id")) `
    'The exact extension-INF install/restart path changed.'
Assert-Policy (([regex]::Matches(
        $gate, "'/restart-device'")).Count -eq 1) `
    'The install gate can issue more than one exact PDO restart.'
Assert-Policy ($gate.IndexOf("'/add-driver', `$candidateInf, '/install'") -lt
               $gate.IndexOf(
                   "action = 'restart-exact-avrcp-pdo-after-extension-install'") -and
               $gate -match 'Recreate the exact Microsoft function stack once on every fresh install' -and
               $gate -match 'will restart the exact XM5 AVRCP PDO once' -and
               $gate -notmatch 'if \(-not \[bool\]\$controlCheck\.healthy -or \$install\.exit_code -eq 3010\)') `
    'Every extension-INF install must be followed by the one exact PDO restart.'
Assert-Policy ($gate -match 'verify-microsoft-owner-after-required-restart' -and
               $gate -match 'Wait-V1AvrcpFilterMicrosoftBaseline' -and
               $gate -match 'Microsoft AVRCP owner or PDO health changed while the upper filter loaded') `
    'The gate does not verify Microsoft ownership after the required restart.'
Assert-Policy ($gate -match '-AllowExactPdoRestart \$false' -and
               $gate -match "transaction_state = 'exact-pdo-restart-issued'" -and
               $gate.IndexOf("`$restartCount = 1") -lt
                   $gate.IndexOf("if (`$restart.exit_code -ne 0)") -and
               $gate -match 'exact_pdo_restart_count') `
    'Automatic rollback can exceed the one-restart bound.'
Assert-Policy ($gate -match "--wait-for-first-request-seconds', '90'" -and
               $probe -match 'AVRCP filter trace watcher armed' -and
               $probe -match 'First AVRCP filter request observed' -and
               $probe -match 'AVRCP filter pre-arm drain failed' -and
               $probe -match 'first_request_wait_expired' -and
               $probe -match 'window status: requests') `
    'The filter observation is not armed before physical power-on.'
Assert-Policy ($gate -match 'No post-connect AVRCP request reached the upper filter' -and
               $gate -match "failureCode = 'no-post-connect-filter-request'" -and
               $gate -match 'filter_probe_error_lines') `
    'The gate does not preserve and classify a post-connect request timeout.'
Assert-Policy ($builder -match 'transport_probe' -and
               $verifier -match 'transport_probe\.exe' -and
               $gate -match 'does not allow probe path overrides' -and
               $gate -match '--info' -and
               $gate -match '--stream-silence-continuous' -and
               $gate -match "'--open-attempts', '1'" -and
               $gate -match '--stop-event' -and
               $gate -match 'encoded-silence media session was released cleanly' -and
               $gate -notmatch '--stream-tone|--stream-system|--play-system|--play-endpoint') `
    'The filter gate does not hold one bounded encoded-silence media prerequisite.'
Assert-Policy ($gate -notmatch 'requestBeforeAcl' -and
               $gate.IndexOf('XM5 ACL event: connected') -lt
                   $gate.IndexOf("`$mediaProcess = Start-Process") -and
               $gate.IndexOf("`$mediaProcess = Start-Process") -lt
                   $gate.IndexOf("`$filterProcess = Start-Process") -and
               $gate.IndexOf("`$filterProcess = Start-Process") -lt
                   $gate.IndexOf('=== XM5 ACTION WINDOW READY ===') -and
               $gate -match 'filterStartedAfterAcl' -and
               $gate -match 'filterStartedAfterMediaReady') `
    'The filter observation can start before ACL connect or media readiness.'
Assert-Policy ($gate -match 'decoded status: capability=' -and
               $gate -match 'filterProbeDecodedVolumeChanged -lt 1' -and
               $gate -match 'filterProbeDecodedPassThrough -lt 1' -and
               $gate -match 'Decoded AVRCP gesture evidence was incomplete') `
    'The gate does not require decoded volume and PASS THROUGH evidence.'
Assert-Policy ($gate -match 'Get-Content -LiteralPath \$Path -Raw' -and
               $gate -match '\$pending\.LastIndexOf\("`n"\)' -and
               $gate -match '\$Offset\.Value = \[int\]\(\$offsetValue \+ \$lastNewline \+ 1\)' -and
               $gate -match '\$line -notmatch .*event sequence=' -and
               $gate -match '\$line -notmatch .*Live:') `
    'Live evidence forwarding can emit partial lines or scroll the gesture prompt away.'
Assert-Policy ($gate -match '\$content = \[string\]\(Get-Content -LiteralPath \$Path -Raw\)' -and
               $gate -match '\$offsetValue = \[int\]\$Offset\.Value' -and
               $gate -match '\$complete = \$pending\.Substring\(0, \[int\]\$lastNewline \+ 1\)' -and
               $gate -match '(?s)function Get-NewProcessLines \{.*?catch \{.*?return @\(\)') `
    'The live line reader can crash the gate on a transient native-output read.'
$lineReaderMatch = [regex]::Match(
    $gate,
    '(?ms)^function Get-NewProcessLines \{.*?^\}')
Assert-Policy $lineReaderMatch.Success `
    'The complete-line live reader could not be isolated for regression testing.'
Invoke-Expression $lineReaderMatch.Value
$lineReaderRoot = Join-Path $root 'tmp'
New-Item -ItemType Directory -Path $lineReaderRoot -Force | Out-Null
$lineReaderPath = Join-Path $lineReaderRoot `
    ('v1-filter-line-reader-' + [guid]::NewGuid().ToString('N') + '.log')
try {
    Set-Content -LiteralPath $lineReaderPath -Value 'first partial' `
        -NoNewline -Encoding utf8
    $lineReaderOffset = 0
    $partial = @(Get-NewProcessLines -Path $lineReaderPath `
        -Offset ([ref]$lineReaderOffset))
    Assert-Policy ($partial.Count -eq 0 -and $lineReaderOffset -eq 0) `
        'The live reader consumed a native process line before its newline.'
    Add-Content -LiteralPath $lineReaderPath `
        -Value " line`r`nsecond" -NoNewline -Encoding utf8
    $firstComplete = @(Get-NewProcessLines -Path $lineReaderPath `
        -Offset ([ref]$lineReaderOffset))
    Assert-Policy ($firstComplete.Count -eq 1 -and
                   $firstComplete[0] -ceq 'first partial line') `
        'The live reader did not publish the first completed line intact.'
    Add-Content -LiteralPath $lineReaderPath `
        -Value " complete`r`n" -NoNewline -Encoding utf8
    $secondComplete = @(Get-NewProcessLines -Path $lineReaderPath `
        -Offset ([ref]$lineReaderOffset))
    Assert-Policy ($secondComplete.Count -eq 1 -and
                   $secondComplete[0] -ceq 'second complete') `
        'The live reader lost the saved partial tail on the next write.'
} finally {
    Remove-Item -LiteralPath $lineReaderPath -Force `
        -ErrorAction SilentlyContinue
}
Assert-Policy ($gate -match '@\(0, 259, 3010\)') `
    'The install gate does not handle the bounded restart-required result.'
Assert-Policy ($gate -match 'XM5 ACL event: disconnected' -and
               $gate -match 'safeToRollback' -and
               $gate -match 'rollback-required') `
    'The gate can remove the filter without physical disconnect evidence.'
Assert-Policy ($gate -match 'Test-V1AvrcpFilterMicrosoftBaseline' -and
               $gate -match 'Microsoft AVRCP owner or PDO health changed') `
    'The gate does not continuously protect the Microsoft function driver.'
Assert-Policy ($common -match 'absent\s*=\s*\$exitCode\s*-eq\s*3' -and
               $common -match 'healthy\s*=\s*\$exitCode\s*-eq\s*0' -and
               $common -match '\[void\]\$probeLines\.Add\(\[string\]\$line\)') `
    'The filter control verifier conflates missing, healthy, and broken control devices.'
Assert-Policy ($common -match [regex]::Escape(
                   "'/delete-driver', `$publishedInf, '/uninstall'") -and
               $common -notmatch "'/uninstall', '/force'") `
    'Filter rollback still requests the ignored pnputil /force option.'
Assert-Policy ($gate -match 'Test-V1AvrcpFilterPublishedInfMatchesCandidate' -and
               $rollback -match 'Test-V1AvrcpFilterPublishedInfMatchesCandidate') `
    'Install or rollback can operate on an unverified Driver Store INF.'
Assert-Policy ($gate -notmatch 'Disable-PnpDevice|Enable-PnpDevice|Set-PnpDevice|/disable-device|/enable-device|UpperFilters|LowerFilters|ROOT\\MEDIA|0000110B') `
    'The gate contains a radio, class-wide, audio, or unrelated PnP mutation.'
Assert-Policy ($rollback -match 'Unmanaged Native AVRCP filter packages block rollback' -or
               $common -match 'Unmanaged Native AVRCP filter packages block rollback') `
    'Rollback does not fail closed on unmanaged filter packages.'
Assert-Policy ($builder -match 'xm5_connection_probe' -and
               $verifier -match 'xm5_connection_probe' -and
               $builder -match 'bounded_ldac_silence_media_prerequisite' -and
               $builder -match 'probe_overrides_allowed = \$false' -and
               $builder -match 'transport_info_preflight = \$true' -and
               $builder -match 'filter_probe_starts_after_acl_connect = \$true' -and
               $builder -match 'filter_probe_starts_after_silence_media_ready = \$true' -and
               $builder -match 'gesture_prompt_after_filter_probe_armed = \$true' -and
               $builder -match 'complete_line_live_forwarding = \$true' -and
               $builder -match 'v1_avrcp_filter_gate_policy_tests') `
    'The filter candidate does not carry and verify its matching ACL probe.'

$english = Get-V1AvrcpFilterPublishedInfFromOutput -Lines @(
    'Published Name : oem9704.inf')
$chinese = Get-V1AvrcpFilterPublishedInfFromOutput -Lines @(
    '发布名称: oem9705.inf')
Assert-Policy ($english -ceq 'oem9704.inf' -and
               $chinese -ceq 'oem9705.inf') `
    'Localized pnputil published INF parsing failed.'
foreach ($accepted in @(0, 259, -536870340, 3758096956)) {
    Assert-Policy (Test-V1AvrcpFilterDeleteExitCode -ExitCode $accepted) `
        "Expected idempotent delete code was rejected: $accepted"
}
foreach ($rejected in @(1, 2, 5, 3010)) {
    Assert-Policy (-not (Test-V1AvrcpFilterDeleteExitCode `
            -ExitCode $rejected)) `
        "Unexpected delete code was accepted: $rejected"
}

Write-Host 'V1 AVRCP upper-filter gate policy tests passed.'
