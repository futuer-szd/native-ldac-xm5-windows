# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    Get-Content -LiteralPath (Join-Path $root $RelativePath) -Raw
}

$common = Read-ProjectFile `
    'tools\v1-inbound-pnp-rundown-common.ps1'
$builder = Read-ProjectFile `
    'tools\build-v1-inbound-pnp-rundown-candidate.ps1'
$baseBuilder = Read-ProjectFile `
    'tools\build-v1-inbound-signaling-candidate.ps1'
$prepare = Read-ProjectFile `
    'tools\prepare-v1-inbound-pnp-rundown-gate.ps1'
$gate = Read-ProjectFile `
    'tools\run-v1-inbound-pnp-rundown-gate.ps1'
$rollback = Read-ProjectFile `
    'tools\rollback-v1-inbound-pnp-rundown-gate.ps1'

. (Join-Path $root 'tools\v1-inbound-pnp-rundown-common.ps1')

foreach ($required in @(
        '$script:V1InboundPnpRundownPolicyVersion = 14',
        '85a0b46231ae2f3212e6616346e2d6905314f0ff',
        '$script:V1InboundPnpRejectedDriverTree',
        '6b4790f254149679f4c7e1e239ffc752f90c54ce',
        'self_managed_io_suspend_server_rundown',
        'failed_unregister_preserves_server_handle',
        'one_shot_inbound_listener_rundown',
        'atomic_connected_and_rundown_publish',
        'known_code38_single_reboot_recovery',
        'function Test-V1InboundPnpKnownCode38RecoveryTransaction',
        'activation_requires_one_windows_restart',
        'function Test-V1InboundPnpPrerequisite',
        'function Test-V1InboundPnpCycleEvidence',
        'function Get-V1InboundPnpKernelFailureEvents',
        'C000038E')) {
    if (-not $common.Contains($required)) {
        throw "The PnP-rundown common contract is missing: $required"
    }
}

$transactionPath = 'C:\evidence\transaction.json'
$resultPath = 'C:\evidence\completed-result.json'
$prerequisite = [pscustomobject]@{
    schema_version = 1
    transport_policy_version = 12
    status = 'inbound-discovery-verified'
    rollback = [pscustomobject]@{ attempted = $false }
    validation = [pscustomobject]@{
        passed = $true
        result = $resultPath
    }
}
$prerequisiteResult = [pscustomobject]@{
    schema_version = 1
    transport_policy_version = 12
    passed = $true
    core_passed = $true
    outbound_avdtp_connection_requests = 0
    inbound_no_resources_responses = 0
    set_configuration_commands = 0
    avdtp_start_commands = 0
    media_packets = 0
    transaction = $transactionPath
}
$prerequisiteArguments = @{
    Transaction = $prerequisite
    TransactionPath = $transactionPath
    Result = $prerequisiteResult
    ResultPath = $resultPath
}
if (-not (Test-V1InboundPnpPrerequisite @prerequisiteArguments)) {
    throw 'Valid completed inbound DISCOVER evidence was rejected.'
}
$prerequisiteResult.media_packets = 1
if (Test-V1InboundPnpPrerequisite @prerequisiteArguments) {
    throw 'The PnP-rundown prerequisite accepted media evidence.'
}
$prerequisiteResult.media_packets = 0
$prerequisite.status = 'rollback-required'
if (Test-V1InboundPnpPrerequisite @prerequisiteArguments) {
    throw 'The PnP-rundown prerequisite accepted an unfinished transaction.'
}
$prerequisite.status = 'inbound-discovery-verified'

$summary = [pscustomobject]@{
    inbound_avdtp_connection_requests = 1
    outbound_avdtp_connection_requests = 0
    outbound_success_responses_to_inbound_avdtp = 1
    outbound_rejections_to_inbound_avdtp = 0
    inbound_avdtp_psm_not_supported_after_success = 0
    inbound_avdtp_unresolved_requests = 0
    inbound_no_resources_responses = 0
    inbound_avdtp_pending_without_success = $false
}
$cycle = [pscustomobject]@{
    acl_connect_observed = $true
    acl_disconnect_observed = $true
    public_disconnect_observed = $true
    binding_on_connect_healthy = $true
    discover_exit_code = 0
    discover_text = "Selected LDAC audio sink SEID: 3`r`n" +
        "Signaling channel closed.`r`n"
    open_diagnostic = "Signaling channel direction: inbound.`r`n" +
        "L2CAP OPEN state: completed, succeeded.`r`n"
    code38_event_count = 0
}
if (-not (Test-V1InboundPnpCycleEvidence `
        -Cycle $cycle -Summary $summary)) {
    throw 'Valid PnP-rundown cycle evidence was rejected.'
}
$summary.outbound_rejections_to_inbound_avdtp = 1
$summary.inbound_avdtp_psm_not_supported_after_success = 1
if (-not (Test-V1InboundPnpCycleEvidence `
        -Cycle $cycle -Summary $summary)) {
    throw 'A post-success one-shot-listener PSM_NOT_SUPPORTED response was rejected.'
}
$summary.inbound_avdtp_psm_not_supported_after_success = 0
if (Test-V1InboundPnpCycleEvidence `
        -Cycle $cycle -Summary $summary) {
    throw 'A rejection not proven to follow an accepted channel was accepted.'
}
$summary.outbound_rejections_to_inbound_avdtp = 0
$summary.inbound_avdtp_unresolved_requests = 1
$summary.inbound_avdtp_pending_without_success = $true
if (Test-V1InboundPnpCycleEvidence `
        -Cycle $cycle -Summary $summary) {
    throw 'An unresolved inbound PENDING response was accepted.'
}
$summary.inbound_avdtp_unresolved_requests = 0
$summary.inbound_avdtp_pending_without_success = $false
$cycle.code38_event_count = 1
if (Test-V1InboundPnpCycleEvidence `
        -Cycle $cycle -Summary $summary) {
    throw 'A PnP-rundown cycle accepted Code 38 evidence.'
}
$cycle.code38_event_count = 0
$summary.outbound_avdtp_connection_requests = 1
if (Test-V1InboundPnpCycleEvidence `
        -Cycle $cycle -Summary $summary) {
    throw 'A PnP-rundown cycle accepted outbound PSM 0x0019.'
}
$summary.outbound_avdtp_connection_requests = 0

$knownCode38 = [pscustomobject]@{
    schema_version = 1
    transport_policy_version = 13
    source_commit = '77a5f5e5f8798e3e1ecaf0c9e785b301352cd03a'
    driver_tree = '6b4790f254149679f4c7e1e239ffc752f90c54ce'
    status = 'rollback-required'
    phase = 'validation-failed-xm5-disconnected'
    device_instance_id = 'BTHENUM\known-code38'
    selected_inf = 'oem9101.inf'
    rollback = [pscustomobject]@{ attempted = $false }
    cycles = @([pscustomobject]@{
        passed = $false
        failure = 'Cycle 1 left the PDO in Code 38.'
        binding_after_disconnect = [pscustomobject]@{
            problem_code = 38
            published_inf = 'oem9101.inf'
        }
    })
}
if (-not (Test-V1InboundPnpKnownCode38RecoveryTransaction `
        -Transaction $knownCode38)) {
    throw 'The exact rejected-tree Code 38 recovery evidence was rejected.'
}
$knownCode38.rollback.attempted = $true
if (Test-V1InboundPnpKnownCode38RecoveryTransaction `
        -Transaction $knownCode38) {
    throw 'A previously mutated Code 38 transaction was accepted for recovery.'
}
$knownCode38.rollback.attempted = $false
$knownCode38.driver_tree = '0000000000000000000000000000000000000000'
if (Test-V1InboundPnpKnownCode38RecoveryTransaction `
        -Transaction $knownCode38) {
    throw 'An unknown Code 38 driver tree was accepted for recovery.'
}
$knownCode38.driver_tree = '6b4790f254149679f4c7e1e239ffc752f90c54ce'
$knownCode38.cycles[0].binding_after_disconnect.problem_code = 0
if (Test-V1InboundPnpKnownCode38RecoveryTransaction `
        -Transaction $knownCode38) {
    throw 'A transaction without the recorded Code 38 was accepted for recovery.'
}

foreach ($required in @(
        '$OutputPath',
        'candidate path escaped artifacts')) {
    if (-not $baseBuilder.Contains($required)) {
        throw "The reusable inbound candidate builder is missing: $required"
    }
}
foreach ($required in @(
        'build-v1-inbound-signaling-candidate.ps1',
        'self_managed_io_suspend_server_rundown',
        'activation_requires_one_windows_restart',
        'Get-V1InboundPnpRundownCandidate',
        'No driver, service, process, Bluetooth request, or system setting was changed.')) {
    if (-not $builder.Contains($required)) {
        throw "The PnP-rundown candidate builder is missing: $required"
    }
}

foreach ($required in @(
        'ConfirmV1InboundPnpRundownUpdate',
        'ConfirmPinImpactAndReboot',
        'ConfirmKnownCode38Recovery',
        'Test-V1InboundPnpKnownCode38RecoveryTransaction',
        'known rejected-tree Code 38 binding',
        'known_code38_recovery',
        'recovery_transaction',
        'Test-V1InboundPnpPrerequisite',
        "'/export-driver'",
        "'/add-driver'",
        "'/install'",
        "status = 'reboot-required'",
        'problem_code_before_restart',
        'A temporary Code 38 before that restart',
        'restart Windows exactly once')) {
    if (-not $prepare.Contains($required)) {
        throw "The PnP-rundown preparation contract is missing: $required"
    }
}
if (-not $prepare.Contains('-DurationSeconds 360')) {
    throw 'The prepare handoff does not budget both delayed PnP observation windows.'
}
foreach ($forbidden in @(
        'Restart-Computer', "'/restart-device'",
        "'--discover'", "'--configure'", "'--media-session'",
        'Disable-PnpDevice', 'Enable-PnpDevice')) {
    if ($prepare.IndexOf(
            $forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "PnP-rundown preparation contains a forbidden same-boot action: $forbidden"
    }
}

foreach ($required in @(
        'ConfirmV1InboundPnpRundown',
        'Restart Windows exactly once before running',
        'Get-V1InboundPnpKernelFailureEvents',
        'Kernel-PnP',
        'foreach ($cycleNumber in $cycleNumbers)',
        'ResumeAfterCycle1',
        'cycle-1-reclassification.json',
        'Recorded cycle 1 passed the corrected one-shot-listener evidence contract.',
        "@('--discover', '--open-attempts', '1')",
        'Test-V1InboundPnpCycleEvidence',
        'Wait-V1InboundPublicDisconnect',
        'observing a 20-second delayed PnP failure window',
        'Start-Sleep -Seconds 20',
        'code38_event_count',
        'pnp-rundown-verified',
        'No SET_CONFIGURATION, media OPEN, START, media packet')) {
    if (-not $gate.Contains($required)) {
        throw "The two-cycle PnP-rundown gate is missing: $required"
    }
}
foreach ($forbidden in @(
        'pnputil', 'Restart-Computer', "'/restart-device'",
        "'--configure'", "'--media-session'", "'--stream-silence'",
        "'--stream-tone'", "'--stream-system'",
        'Disable-PnpDevice', 'Enable-PnpDevice')) {
    if ($gate.IndexOf(
            $forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The two-cycle gate contains a forbidden mutation or media action: $forbidden"
    }
}

foreach ($required in @(
        'ConfirmV1InboundPnpRundownRollback',
        'safe_fallback_backup_inf',
        "'/delete-driver'",
        "'/restart-device'",
        'ready flags 0x00000007',
        'No Windows reboot or Bluetooth radio toggle occurred.')) {
    if (-not $rollback.Contains($required)) {
        throw "The PnP-rundown rollback contract is missing: $required"
    }
}
$disconnectCheck = $rollback.IndexOf(
    'Get-NativeLdacXm5BluetoothState')
$firstDelete = $rollback.IndexOf("'/delete-driver'")
if ($disconnectCheck -lt 0 -or $firstDelete -le $disconnectCheck) {
    throw 'PnP-rundown rollback can mutate the driver before XM5 disconnection.'
}

foreach ($relative in @(
        'tools\v1-inbound-pnp-rundown-common.ps1',
        'tools\build-v1-inbound-pnp-rundown-candidate.ps1',
        'tools\build-v1-inbound-signaling-candidate.ps1',
        'tools\prepare-v1-inbound-pnp-rundown-gate.ps1',
        'tools\run-v1-inbound-pnp-rundown-gate.ps1',
        'tools\rollback-v1-inbound-pnp-rundown-gate.ps1')) {
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $root $relative), [ref]$tokens, [ref]$errors)
    if (@($errors).Count -ne 0) {
        throw "The PnP-rundown PowerShell file does not parse: $relative"
    }
}

$temp = Join-Path ([System.IO.Path]::GetTempPath()) `
    "v1-pnp-rundown-l2cap-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $temp -Force | Out-Null
try {
    function Invoke-L2capFixture {
        param([Parameter(Mandatory = $true)][string[]]$Lines)

        $fixture = Join-Path $temp 'hci.xml'
        $output = Join-Path $temp 'summary.json'
        $Lines | Set-Content -LiteralPath $fixture -Encoding UTF8
        & (Join-Path $root 'tools\summarize-bluetooth-l2cap-trace.ps1') `
            -InputPath $fixture -OutputPath $output | Out-Null
        return Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
    }

    $request6 = "<Event><System><TimeCreated SystemTime='2026-07-31T00:00:00Z'/></System><EventData><Data Name='BIP_Type'>3</Data><Data Name='BIP_Data'>00210C00080001000206040019004500</Data></EventData></Event>"
    $pending6 = "<Event><System><TimeCreated SystemTime='2026-07-31T00:00:01Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>000110000C000100030608004500460001000000</Data></EventData></Event>"
    $success6 = "<Event><System><TimeCreated SystemTime='2026-07-31T00:00:02Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>000110000C000100030608004500460000000000</Data></EventData></Event>"
    $request7 = "<Event><System><TimeCreated SystemTime='2026-07-31T00:00:03Z'/></System><EventData><Data Name='BIP_Type'>3</Data><Data Name='BIP_Data'>00210C00080001000207040019004700</Data></EventData></Event>"
    $request6Again = "<Event><System><TimeCreated SystemTime='2026-07-31T00:00:03Z'/></System><EventData><Data Name='BIP_Type'>3</Data><Data Name='BIP_Data'>00210C00080001000206040019004700</Data></EventData></Event>"
    $pending7 = "<Event><System><TimeCreated SystemTime='2026-07-31T00:00:04Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>000110000C000100030708004700480001000000</Data></EventData></Event>"
    $psmNotSupported7 = "<Event><System><TimeCreated SystemTime='2026-07-31T00:00:05Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>000110000C000100030708004700480002000000</Data></EventData></Event>"
    $pending6Again = "<Event><System><TimeCreated SystemTime='2026-07-31T00:00:04Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>000110000C000100030608004700480001000000</Data></EventData></Event>"
    $psmNotSupported6Again = "<Event><System><TimeCreated SystemTime='2026-07-31T00:00:05Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>000110000C000100030608004700480002000000</Data></EventData></Event>"
    $noResources7 = "<Event><System><TimeCreated SystemTime='2026-07-31T00:00:05Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>000110000C000100030708004700480004000000</Data></EventData></Event>"

    $parsed = Invoke-L2capFixture @($request6, $pending6, $success6)
    if ([int]$parsed.inbound_avdtp_unresolved_requests -ne 0 -or
        [int]$parsed.outbound_success_responses_to_inbound_avdtp -ne 1) {
        throw 'The summarizer rejected one completed inbound SUCCESS request.'
    }
    $parsed = Invoke-L2capFixture @(
        $request6, $pending6, $success6,
        $request7, $pending7, $psmNotSupported7)
    if ([int]$parsed.inbound_avdtp_psm_not_supported_after_success -ne 1 -or
        [int]$parsed.inbound_avdtp_unresolved_requests -ne 0) {
        throw 'The summarizer did not classify post-success PSM_NOT_SUPPORTED.'
    }
    $parsed = Invoke-L2capFixture @(
        $request6, $pending6, $success6,
        $request6Again, $pending6Again, $psmNotSupported6Again)
    if (@($parsed.inbound_avdtp_request_outcomes).Count -ne 2 -or
        [string]$parsed.inbound_avdtp_request_outcomes[0].status -ne
            'success' -or
        [string]$parsed.inbound_avdtp_request_outcomes[1].status -ne
            'psm_not_supported') {
        throw 'The summarizer did not isolate a reused L2CAP identifier by request time.'
    }
    $parsed = Invoke-L2capFixture @($request7, $pending7, $noResources7)
    if ([string]$parsed.inbound_avdtp_request_outcomes[0].status -ne
        'no_resources') {
        throw 'The summarizer did not classify an inbound NO_RESOURCES terminal response.'
    }
    $parsed = Invoke-L2capFixture @($request7, $pending7)
    if ([int]$parsed.inbound_avdtp_unresolved_requests -ne 1 -or
        $parsed.inbound_avdtp_pending_without_success -ne $true) {
        throw 'The summarizer accepted an unresolved inbound PENDING response.'
    }
    $parsed = Invoke-L2capFixture @($request7, $pending7, $psmNotSupported7)
    if ([int]$parsed.inbound_avdtp_psm_not_supported_after_success -ne 0) {
        throw 'The summarizer treated a pre-success rejection as expected listener rundown.'
    }
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force
}

Write-Host 'V1 inbound PnP-rundown gate policy tests passed.'
