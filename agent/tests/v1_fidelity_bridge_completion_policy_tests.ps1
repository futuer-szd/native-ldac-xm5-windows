# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    Get-Content -LiteralPath (Join-Path $root $RelativePath) -Raw
}

$completion = Read-ProjectFile `
    'tools\complete-v1-fidelity-bridge-report.ps1'
$common = Read-ProjectFile 'tools\v1-fidelity-bridge-common.ps1'
foreach ($required in @(
        '[Parameter(Mandatory = $true)]',
        '[ValidateSet(''overall-normal'', ''subtle-differences-not-discernible'')]',
        '-ConfirmV1FidelityBridgeReport',
        'transport-verified-awaiting-fidelity-report',
        'Test-V1FidelityBridgeCompletionEvidence',
        'transport_policy_version -eq 10',
        'Manifest.source_commit',
        'Manifest.driver_tree',
        'session_generation',
        'volume_query_count',
        'volume_change_count',
        'fade_committed_sent_frames',
        'consumer_lease_acquire_count',
        'limiter_attack_count -eq 0',
        'limiter_gain_reduced_frames -eq 0',
        'limiter_gain_reduced_samples -eq 0',
        'limiter_fallback_clamp_count -eq 0',
        'limiter_sanitized_sample_count -eq 0',
        '0.03015155',
        'user-reported-overall-normal-subtle-differences-not-discernible',
        'ten_second_playback_observation',
        '''not-separately-assessed''',
        'Write-LegacyJsonAtomic -Value $result -Path $ResultPath',
        'Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath')) {
    if (-not (($completion + $common).Contains($required))) {
        throw "The policy v10 completion contract is missing: $required"
    }
}
foreach ($field in @(
        'bass', 'clarity', 'pumping', 'noise', 'speed', 'distortion')) {
    if (-not $completion.Contains("'${field}'")) {
        throw "The policy v10 completion invents or omits a field: $field"
    }
}
foreach ($forbidden in @(
        'pnputil', 'devcon', 'Restart-Computer', 'Disable-PnpDevice',
        'Enable-PnpDevice', 'Stop-Service', 'Start-Service',
        'SetDefaultEndpoint', 'audio_endpoint_probe',
        'xm5_connection_probe', 'Assert-LegacyAdministrator')) {
    if ($completion.IndexOf(
            $forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The policy v10 completion probes or mutates the system: $forbidden"
    }
}
$tokens = $null
$errors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $root 'tools\complete-v1-fidelity-bridge-report.ps1'),
    [ref]$tokens,
    [ref]$errors)
if (@($errors).Count -ne 0) {
    throw "The policy v10 completion helper does not parse: $errors"
}

. (Join-Path $root 'tools\v1-fidelity-bridge-common.ps1')
. (Join-Path $root 'agent\tests\v1_fidelity_bridge_evidence_tests.ps1')
$transactionPath = Join-Path $root 'tmp\synthetic-v10-transaction.json'
$resultPath = Join-Path $root 'tmp\synthetic-v10-result.json'
$statePath = Join-Path $root 'tmp\synthetic-v10-state.json'
$sessionPath = Join-Path $root 'tmp\synthetic-v10-session.json'
$prerequisitePath = Join-Path $root 'tmp\synthetic-v9-transaction.json'
$state | Add-Member -NotePropertyName __evidence_path `
    -NotePropertyValue $statePath -Force
$completionSession = $session | ConvertTo-Json -Depth 12 | ConvertFrom-Json
$completionSession | Add-Member -NotePropertyName __evidence_path `
    -NotePropertyValue $sessionPath -Force
foreach ($field in @(
        'maximum_pre_gain_peak',
        'maximum_unlimited_post_gain_peak',
        'maximum_post_gain_peak')) {
    $completionSession.$field = 0.03015155
}
$completionSession | Add-Member -NotePropertyName limiter_attack_count `
    -NotePropertyValue 0 -Force
$completionSession | Add-Member `
    -NotePropertyName limiter_gain_reduced_frames -NotePropertyValue 0 -Force
$completionSession | Add-Member `
    -NotePropertyName limiter_gain_reduced_samples -NotePropertyValue 0 -Force
$completionSession | Add-Member `
    -NotePropertyName limiter_sanitized_sample_count `
    -NotePropertyValue 0 -Force
$manifest = [pscustomobject]@{
    source_commit = ('a' * 40)
    driver_tree = ('b' * 40)
    prerequisite = $prerequisitePath
}
$transaction = [pscustomobject]@{
    schema_version = 1
    transport_policy_version = 10
    status = 'transport-verified-awaiting-fidelity-report'
    source_commit = $manifest.source_commit
    driver_tree = $manifest.driver_tree
    prerequisite = $prerequisitePath
    result = $resultPath
    state = $statePath
    session = $sessionPath
}
$result = [pscustomobject]@{
    schema_version = 1
    transport_policy_version = 10
    transport_passed = $true
    fidelity_observation = 'user-report-required'
    source_commit = $manifest.source_commit
    driver_tree = $manifest.driver_tree
    prerequisite = $prerequisitePath
    transaction = $transactionPath
    session_generation = $completionSession.session_generation
    acl_generation = $state.acl_generation
    stream_epoch = $completionSession.stream_epoch
    volume_query_count = $completionSession.volume_query_count
    volume_change_count = 0
    volume_stable = $true
    fade_committed_sent_frames =
        $completionSession.fade_committed_sent_frames
    fade_blocks_prepared = $completionSession.fade_blocks_prepared
    fade_blocks_committed = $completionSession.fade_blocks_committed
    fade_commit_failures = 0
    fade_sanitized_sample_count = 0
    consumer_lease_acquire_count =
        $completionSession.consumer_lease_acquire_count
    consumer_lease_release_count =
        $completionSession.consumer_lease_release_count
    consumer_lease_released = $true
    maximum_pre_gain_peak = 0.03015155
    maximum_unlimited_post_gain_peak = 0.03015155
    maximum_post_gain_peak = 0.03015155
    target_duration_ms = 10000
    actual_duration_ms = $completionSession.actual_duration_ms
    start_accepted = $true
    suspend_accepted = $true
    close_accepted = $true
    driver_installed_or_updated = $false
    rebooted = $false
    bluetooth_toggled = $false
    default_output_changed = $false
    link_state_written = $false
}
$attempts = @($retry, $retry, $completionSession)
$arguments = @{
    Transaction = $transaction
    Result = $result
    Manifest = $manifest
    State = $state
    Session = $completionSession
    Attempts = $attempts
    TransactionPath = $transactionPath
    ResultPath = $resultPath
}
if (-not (Test-V1FidelityBridgeCompletionEvidence @arguments)) {
    throw 'Valid policy v10 completion evidence was rejected.'
}
$wrongPeak = $result | ConvertTo-Json | ConvertFrom-Json
$wrongPeak.maximum_post_gain_peak = 0.0302
$arguments.Result = $wrongPeak
if (Test-V1FidelityBridgeCompletionEvidence @arguments) {
    throw 'A policy v10 result with another peak was accepted.'
}
$limited = $completionSession | ConvertTo-Json | ConvertFrom-Json
$limited.limiter_attack_count = 1
$arguments.Result = $result
$arguments.Session = $limited
$arguments.Attempts = @($retry, $retry, $limited)
if (Test-V1FidelityBridgeCompletionEvidence @arguments) {
    throw 'A policy v10 session with limiter attack was accepted.'
}
$alreadyReported = $result | ConvertTo-Json | ConvertFrom-Json
$alreadyReported.fidelity_observation = 'already-reported'
$arguments.Result = $alreadyReported
$arguments.Session = $completionSession
$arguments.Attempts = $attempts
if (Test-V1FidelityBridgeCompletionEvidence @arguments) {
    throw 'An already-finalized policy v10 result was accepted.'
}
$wrongDriver = $transaction | ConvertTo-Json | ConvertFrom-Json
$wrongDriver.driver_tree = ('c' * 40)
$arguments.Result = $result
$arguments.Transaction = $wrongDriver
if (Test-V1FidelityBridgeCompletionEvidence @arguments) {
    throw 'A policy v10 result from another driver tree was accepted.'
}

Write-Host 'V1 fidelity-bridge user-report completion policy tests passed.'
