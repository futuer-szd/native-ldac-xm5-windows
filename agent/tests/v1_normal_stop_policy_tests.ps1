# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    Get-Content -LiteralPath (Join-Path $root $RelativePath) -Raw
}

$worker = Read-ProjectFile 'agent\v1_transport_normal_stop_worker.cpp'
$pcm = Read-ProjectFile 'agent\v1_transport_pcm_session.cpp'
$tests = Read-ProjectFile 'agent\tests\v1_transport_pcm_session_tests.cpp'
$shared = Read-ProjectFile 'agent\v1_transport_configuration_worker.cpp'
$silenceDriver = Read-ProjectFile `
    'agent\v1_transport_silence_driver_backend.cpp'
$configurationDriver = Read-ProjectFile `
    'agent\v1_transport_configuration_driver_backend.cpp'
$lifecycle = Read-ProjectFile 'agent\v1_lifecycle.cpp'
$presenceAgent = Read-ProjectFile 'agent\v1_presence_agent.cpp'
$openStability = Read-ProjectFile 'agent\v1_transport_open_stability.cpp'
$openStabilityTests = Read-ProjectFile `
    'agent\tests\v1_transport_open_stability_tests.cpp'
$cmake = Read-ProjectFile 'CMakeLists.txt'
$common = Read-ProjectFile 'tools\v1-normal-stop-common.ps1'
$build = Read-ProjectFile 'tools\build-v1-normal-stop-candidate.ps1'
$gate = Read-ProjectFile 'tools\run-v1-normal-stop-gate.ps1'
$collision = Read-ProjectFile `
    'tools\run-v1-signaling-collision-diagnostic.ps1'
$traceSummary = Read-ProjectFile `
    'tools\summarize-bluetooth-l2cap-trace.ps1'

. (Join-Path $root 'tools\v1-normal-stop-common.ps1')
$pnpTransactionPath = 'C:\evidence\pnp-transaction.json'
$pnpResultPath = 'C:\evidence\pnp-result.json'
$pnpCycle = [pscustomobject]@{
    passed = $true; failure = $null
    acl_connect_observed = $true; acl_disconnect_observed = $true
    public_disconnect_observed = $true
    binding_on_connect_healthy = $true
    binding_after_disconnect = [pscustomobject]@{
        service = 'LdacNative'; published_inf = 'oem9103.inf'
        problem_code = 0
    }
    discover_exit_code = 0
    open_diagnostic = "Signaling channel direction: inbound.`r`n" +
        "L2CAP OPEN state: completed, succeeded.`r`n"
    code38_event_count = 0; set_configuration_commands = 0
    avdtp_open_commands = 0; avdtp_start_commands = 0
    media_l2cap_open_commands = 0; media_packets = 0
}
$pnpTransaction = [pscustomobject]@{
    schema_version = 1; transport_policy_version = 14
    status = 'pnp-rundown-verified'; phase = 'complete'
    source_commit = ('1' * 40)
    driver_tree = $script:V1NormalStopApprovedDriverTree
    selected_inf = 'oem9103.inf'; result = $pnpResultPath
    rollback = [pscustomobject]@{ attempted = $false }
}
$pnpResult = [pscustomobject]@{
    schema_version = 1; transport_policy_version = 14; passed = $true
    source_commit = ('1' * 40)
    driver_tree = $script:V1NormalStopApprovedDriverTree
    transaction = $pnpTransactionPath; binding_inf = 'oem9103.inf'
    reboot_verified = $true; cycle_count = 2
    cycles = @($pnpCycle, $pnpCycle); code38_event_count = 0
    set_configuration_commands = 0; avdtp_open_commands = 0
    avdtp_start_commands = 0; media_l2cap_open_commands = 0
    media_packets = 0; bluetooth_toggled = $false
    pnp_restarted = $false
}
$pnpArguments = @{
    Transaction = $pnpTransaction
    TransactionPath = $pnpTransactionPath
    Result = $pnpResult
    ResultPath = $pnpResultPath
    ExpectedDriverTree = $script:V1NormalStopApprovedDriverTree
}
if (-not (Test-V1NormalStopPnpPrerequisite @pnpArguments)) {
    throw 'Valid policy v14 PnP prerequisite was rejected.'
}
$pnpCycle.binding_after_disconnect.problem_code = 38
if (Test-V1NormalStopPnpPrerequisite @pnpArguments) {
    throw 'A Code 38 PnP prerequisite was accepted.'
}
$pnpCycle.binding_after_disconnect.problem_code = 0
$pnpCycle.open_diagnostic = 'Signaling channel direction: outbound.'
if (Test-V1NormalStopPnpPrerequisite @pnpArguments) {
    throw 'An outbound signaling PnP prerequisite was accepted.'
}
$pnpCycle.open_diagnostic = "Signaling channel direction: inbound.`r`n" +
    "L2CAP OPEN state: completed, succeeded.`r`n"

$baseline = [pscustomobject]@{
    a2dp_devices = @([pscustomobject]@{
        service = 'LdacNative'; published_inf = 'oem9103.inf'
        problem_code = 0
    })
    transport_test_packages = @(
        [pscustomobject]@{ published_inf = 'oem9101.inf' },
        [pscustomobject]@{ published_inf = 'oem9202.inf' },
        [pscustomobject]@{ published_inf = 'oem9102.inf' },
        [pscustomobject]@{ published_inf = 'oem9103.inf' })
    native_audio_devices = @([pscustomobject]@{
        present = $true; service = 'NativeLdacAudio'; problem_code = 0
    })
    workspace_processes = @(); scheduled_tasks = @()
    original_a2dp_user_service = [pscustomobject]@{
        start_mode = 'Manual'; state = 'Stopped'
    }
}
$baselineAssessment = Get-V1NormalStopBaselineAssessment `
    -Baseline $baseline -ExpectedTransportInf 'oem9103.inf'
if (-not $baselineAssessment.healthy -or
    [int]$baselineAssessment.transport_package_count -ne 4 -or
    [int]$baselineAssessment.selected_transport_package_count -ne 1) {
    throw 'A healthy binding with historical unbound packages was rejected.'
}
$baseline.a2dp_devices[0].published_inf = 'oem9102.inf'
$baselineAssessment = Get-V1NormalStopBaselineAssessment `
    -Baseline $baseline -ExpectedTransportInf 'oem9103.inf'
if ($baselineAssessment.healthy) {
    throw 'A baseline bound to a stale transport package was accepted.'
}
$baseline.a2dp_devices[0].published_inf = 'oem9103.inf'
$baseline.transport_test_packages +=
    [pscustomobject]@{ published_inf = 'oem9103.inf' }
$baselineAssessment = Get-V1NormalStopBaselineAssessment `
    -Baseline $baseline -ExpectedTransportInf 'oem9103.inf'
if ($baselineAssessment.healthy) {
    throw 'Duplicate selected transport packages were accepted.'
}

foreach ($required in @(
        '#define V1_TRANSPORT_PCM_DURATION_MS 60000u',
        '#define V1_TRANSPORT_PCM_MAXIMUM_GAIN_SCALAR 1.0f',
        '#define V1_TRANSPORT_PCM_MAXIMUM_OUTPUT_PEAK 1.0f',
        '#define V1_TRANSPORT_PCM_MAXIMUM_PACKETS 32768u',
        '#define V1_TRANSPORT_PCM_POST_START_STOP_CLASSIFICATION_TIMEOUT_MS 30000u',
        '#define V1_TRANSPORT_PCM_SAMPLE_PEAK_FIDELITY 1',
        '#define V1_TRANSPORT_PCM_REQUIRE_STABLE_VOLUME 1',
        '#define V1_TRANSPORT_PCM_ALLOW_DYNAMIC_VOLUME 1',
        '#define V1_TRANSPORT_PCM_OBSERVE_PEER_CLOSE 1',
        '#define V1_TRANSPORT_PCM_STARTUP_SILENCE_MS 20.0f',
        '#define V1_TRANSPORT_PCM_FADE_IN_MS 100.0f',
        '#define V1_TRANSPORT_PCM_CEILING_RAMP_START 1.0f',
        '#define V1_TRANSPORT_PCM_CEILING_RAMP_MS 0.0f')) {
    if (-not $worker.Contains($required)) {
        throw "The normal-stop worker profile is missing: $required"
    }
}
foreach ($required in @(
        'options.ceiling_ramp_ms == 0.0f',
        'options.ceiling_ramp_start -',
        'options.maximum_output_peak',
        'WaitForExplicitStop',
        'post_start_stop_classification_timeout_ms',
        'graceful_stop = true',
        'result.ended_by_graceful_stop = graceful_stop',
        'SendBoundarySilence',
        'ObservePeerStreamControl',
        'BuildPeerCloseAccept',
        'avdtp_source_suspend',
        'avdtp_source_close')) {
    if (-not $pcm.Contains($required)) {
        throw "The normal-stop PCM contract is missing: $required"
    }
}
foreach ($required in @(
        'FidelityFixedCeilingGracefulStopIsBounded',
        'FidelityFixedCeilingRequiresFinalCeiling',
        'ContinuousStartupSilenceIsTransportOnly',
        'StreamStopAfterStartSuspendsAndCloses',
        'CancelWinsConcurrentStreamStopWithoutSignaling',
        'DelayedCancelWinsStreamStopClassificationWithoutSignaling',
        'UnclassifiedStreamStopFailsLocallyWithoutSignaling',
        'SuccessfulOpenDiagnosticsArePreserved',
        'limiter_attack_count == 0u',
        'limiter_gain_reduced_frames == 0u',
        'fade_committed_sent_frames == result.pcm_frames_sent',
        'result.avdtp_suspend_accepted',
        'result.avdtp_close_accepted',
        'OpenFailureDiagnosticsArePreserved')) {
    if (-not $tests.Contains($required)) {
        throw "The normal-stop unit evidence is missing: $required"
    }
}
foreach ($required in @(
        'V1TransportPcmStopDisposition::Cancel',
        'V1TransportPcmStopDisposition::Graceful',
        'open_diagnostic_remote_no_resources',
        'IsConfirmedRemoteNoResources')) {
    if (-not $shared.Contains($required)) {
        throw "The worker stop distinction is missing: $required"
    }
}
foreach ($driverAdapter in @($silenceDriver, $configurationDriver)) {
    foreach ($required in @(
            'GetLastOpenDiagnostics(',
            'return backend_.GetLastOpenDiagnostics(diagnostics);')) {
        if (-not $driverAdapter.Contains($required)) {
            throw "A transport driver adapter drops OPEN diagnostics: $required"
        }
    }
}
if (-not (Read-ProjectFile `
        'agent\v1_transport_driver_backend.cpp').Contains(
        'CaptureLastOpenDiagnostics();')) {
    throw 'A successful signaling OPEN does not capture ABI diagnostics.'
}
foreach ($required in @(
        'V1ActionGracefulStopTransport',
        'V1ActionCancelTransport',
        'GetV1PcmTransportRetryDelayMs')) {
    if (-not $lifecycle.Contains($required)) {
        throw "The reducer stop distinction is missing: $required"
    }
}
foreach ($required in @(
        'StopContainedEngine(engine',
        'V1ActionGracefulStopTransport',
        'ArchiveTransportAttemptResult(options, *state, error)',
        'state.transport_stop_acknowledgements ==',
        'state.engine_exit_events == state.child_processes_started',
        'state.lifecycle.open_attempts_for_generation == 0u',
        'options.pcm_fast_signaling_acquisition',
        'GetV1PcmTransportRetryDelayMs')) {
    if (-not $presenceAgent.Contains($required)) {
        throw "The graceful final-attempt archive is missing: $required"
    }
}
foreach ($required in @(
        'add_executable(v1_transport_normal_stop_worker',
        'v1_transport_normal_stop_worker_help',
        'v1_normal_stop_policy',
        'v1_normal_stop_evidence',
        'v1_transport_open_stability_tests')) {
    if (-not $cmake.Contains($required)) {
        throw "The normal-stop build/test target is missing: $required"
    }
}

foreach ($required in @(
        'transport_policy_version -ne',
        'transparent-unity-full-scale-boundary',
        'render_stop_required_after_media_started',
        'graceful_STOP_uses_SUSPEND_CLOSE',
        'ACL_disconnect_uses_local_cancel_only',
        'minimum_5000_ms_before_render_stop',
        '$script:V1NormalStopPolicyVersion = 20',
        '$script:V1NormalStopTransportOpenRenderStabilityMs = 1000',
        'continuous_render_epoch_before_transport_open',
        'dynamic_post_volume_tracking',
        'bounded_post_start_render_rebind',
        'physical_ACL_disconnect_required',
        'Test-V1NormalStopFidelityPrerequisite',
        'Test-V1NormalStopPnpPrerequisite',
        'Get-V1NormalStopBaselineAssessment',
        'Test-V1NormalStopEvidence',
        'inbound_signaling_channel_required',
        '$count -ne 1',
        'open_diagnostic_available -eq $true',
        'open_diagnostic_flags -band 0x17',
        'disconnected_events -eq 1',
        'ended_by_graceful_stop -eq $true',
        'completed_full_duration -eq $false',
        'limiter_attack_count -eq 0',
        'ceiling_ramp_ms -eq 0.0')) {
    if (-not $common.Contains($required)) {
        throw "The normal-stop evidence policy is missing: $required"
    }
}
foreach ($required in @(
        '--target endpoint_volume_probe --clean-first',
        '--target v1_transport_normal_stop_worker v1_presence_agent',
        'transport_open_render_stability_ms',
        'transport_probe',
        'does not support --monitor-state',
        'Refusing to build the normal-stop candidate from dirty Git source.',
        'Policy v14 is not a completed inbound PnP-rundown prerequisite.',
        'No driver, service, Bluetooth request, process, or system setting was changed.')) {
    if (-not $build.Contains($required)) {
        throw "The normal-stop candidate policy is missing: $required"
    }
}
foreach ($required in @(
        '[ValidateRange(300,420)][int]$DurationSeconds = 360',
        '-ConfirmV1NormalStop',
        '--exercise-transport-pcm-burst',
        '--pcm-fast-signaling-acquisition',
        '--transport-open-render-stability-ms',
        'completely exit the player',
        'keep playing for at least 10 seconds',
        'Successful evidence requires exactly one inbound signaling OPEN',
        'The installed LdacNative driver is not the inbound-ready ABI 0.5 build.',
        'Get-V1NormalStopBaselineAssessment',
        'Historical unbound LdacNative packages retained:',
        'Test-V1NormalStopPnpPrerequisite',
        'open_diagnostic_query_attempts',
        'open_diagnostic_query_error',
        'open_diagnostic_query_bytes',
        'volume state became unavailable or muted before Bluetooth OPEN',
        'Pause alone may leave WaveRT RUN',
        'V1 contained engine stopped cleanly',
        'V1 ACL disconnected',
        'One inbound signaling OPEN completed with zero transport retry.',
        'normal-stop-verified',
        "stop_reason = 'render-stop'",
        'graceful_stop_actions',
        'cancel_actions',
        'media_duration_ms',
        'final_attempt_archived = $true',
        'resources_released = $true',
        "lifecycle_outcome = 'graceful-stop'",
        'No audio-quality comparison is required',
        'No reboot or rollback is required.')) {
    if (-not $gate.Contains($required)) {
        throw "The normal-stop hardware gate is missing: $required"
    }
}
foreach ($required in @(
        'ObserveV1TransportOpenStability',
        'render_epoch != gate->render_epoch',
        'acl_generation != gate->acl_generation')) {
    if (-not $openStability.Contains($required)) {
        throw "The transport OPEN stability gate is missing: $required"
    }
}
foreach ($required in @(
        'short RUN STOP resets',
        'stable RUN authorizes',
        'authorization occurs exactly once',
        'ACL generation change cancels')) {
    if (-not $openStabilityTests.Contains($required)) {
        throw "The transport OPEN stability test is missing: $required"
    }
}
foreach ($forbidden in @(
        'pnputil', 'devcon', 'Restart-Computer', 'Disable-PnpDevice',
        'Enable-PnpDevice', 'Stop-Service', 'Start-Service',
        'SetDefaultEndpoint')) {
    if ($gate.IndexOf(
            $forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The normal-stop gate mutates the baseline: $forbidden"
    }
}
foreach ($required in @(
        'Microsoft-Windows-BTH-BTHPORT/HCI',
        'Microsoft-Windows-BTH-BTHPORT/L2CAP',
        'run-v1-normal-stop-gate.ps1',
        '-Confirm:$false',
        'summarize-bluetooth-l2cap-trace.ps1',
        'inbound_avdtp_collision_observed',
        '/e:false')) {
    if (-not $collision.Contains($required)) {
        throw "The signaling-collision capture contract is missing: $required"
    }
}
foreach ($forbidden in @(
        'pnputil', 'devcon', 'Restart-Computer', 'Disable-PnpDevice',
        'Enable-PnpDevice', 'Stop-Service', 'Start-Service',
        'SetDefaultEndpoint')) {
    if ($collision.IndexOf(
            $forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The signaling-collision capture mutates the baseline: $forbidden"
    }
}
foreach ($required in @(
        "direction = if (`$type -eq 4) { 'outbound' } else { 'inbound' }",
        '$code -eq 2 -and $length -ge 4',
        '$code -eq 3 -and $length -ge 8',
        'outbound_avdtp_connection_requests',
        'inbound_avdtp_connection_requests',
        'inbound_no_resources_responses')) {
    if (-not $traceSummary.Contains($required)) {
        throw "The L2CAP trace summary contract is missing: $required"
    }
}
foreach ($relative in @(
        'tools\v1-normal-stop-common.ps1',
        'tools\build-v1-normal-stop-candidate.ps1',
        'tools\run-v1-normal-stop-gate.ps1',
        'tools\run-v1-signaling-collision-diagnostic.ps1',
        'tools\summarize-bluetooth-l2cap-trace.ps1')) {
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $root $relative), [ref]$tokens, [ref]$errors)
    if (@($errors).Count -ne 0) {
        throw "The normal-stop PowerShell file does not parse: $relative"
    }
}

$temp = Join-Path ([System.IO.Path]::GetTempPath()) `
    "v1-l2cap-trace-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $temp -Force | Out-Null
try {
    $fixture = Join-Path $temp 'hci.xml'
    $summary = Join-Path $temp 'summary.json'
    @(
        "<Event><System><TimeCreated SystemTime='2026-07-27T23:59:59Z'/></System><ProcessingErrorData><ErrorCode>15003</ErrorCode></ProcessingErrorData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-07-28T00:00:00Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>00010C00080001000205040019004400</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-07-28T00:00:01Z'/></System><EventData><Data Name='BIP_Type'>3</Data><Data Name='BIP_Data'>00210C00080001000206040019004500</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-07-28T00:00:02Z'/></System><EventData><Data Name='BIP_Type'>3</Data><Data Name='BIP_Data'>002110000C000100030508000000440004000000</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-07-28T00:00:03Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>000110000C000100030608004500460001000000</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-07-28T00:00:04Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>000110000C000100030608004500460000000000</Data></EventData></Event>"
    ) | Set-Content -LiteralPath $fixture -Encoding UTF8
    & (Join-Path $root 'tools\summarize-bluetooth-l2cap-trace.ps1') `
        -InputPath $fixture -OutputPath $summary | Out-Null
    $parsed = Get-Content -LiteralPath $summary -Raw | ConvertFrom-Json
    if ([int]$parsed.outbound_avdtp_connection_requests -ne 1 -or
        [int]$parsed.inbound_avdtp_connection_requests -ne 1 -or
        [int]$parsed.inbound_no_resources_responses -ne 1 -or
        [int]$parsed.outbound_pending_responses_to_inbound_avdtp -ne 1 -or
        [int]$parsed.outbound_success_responses_to_inbound_avdtp -ne 1 -or
        [int]$parsed.outbound_rejections_to_inbound_avdtp -ne 0 -or
        $parsed.inbound_avdtp_pending_without_success -ne $false -or
        $parsed.inbound_avdtp_collision_observed -ne $true) {
        throw 'The L2CAP trace summary rejected a known collision fixture.'
    }
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force
}

Write-Host 'V1 transparent normal-stop policy tests passed.'
