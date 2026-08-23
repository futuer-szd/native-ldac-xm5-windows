# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    Get-Content -LiteralPath (Join-Path $root $RelativePath) -Raw
}

$completion = Read-ProjectFile `
    'tools\complete-v1-linked-limiter-report.ps1'
$common = Read-ProjectFile 'tools\v1-linked-limiter-common.ps1'
foreach ($required in @(
        '[Parameter(Mandatory = $true)]',
        '[ValidateSet(''not-assessed-by-user'')]',
        '-ConfirmV1LinkedLimiterReport',
        'transport-verified-awaiting-linked-limiter-report',
        'quality_comparison_observation -eq',
        '''user-report-required''',
        'Test-V1LinkedLimiterCompletionEvidence',
        'transport_policy_version -eq 9',
        'Manifest.source_commit',
        'Manifest.driver_tree',
        'limiter_algorithm',
        'limiter_algorithm_version',
        'limiter_minimum_gain',
        'limiter_gain_reduced_frames',
        'limiter_gain_reduced_samples',
        'limiter_fallback_clamp_count',
        'quality_comparison_observation = $Observation',
        'quality_assessed_by_user',
        'careful_listening_reported',
        '''not-assessed''',
        'Write-LegacyJsonAtomic -Value $result -Path $ResultPath',
        'Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath')) {
    if (-not (($completion + $common).Contains($required))) {
        throw "The policy v9 completion contract is missing: $required"
    }
}
foreach ($field in @(
        'bass', 'clarity', 'pumping', 'noise', 'speed', 'distortion')) {
    if (-not $completion.Contains("'${field}'")) {
        throw "The policy v9 completion omits the not-assessed field: $field"
    }
}
foreach ($forbidden in @(
        'pnputil', 'devcon', 'Restart-Computer', 'Disable-PnpDevice',
        'Enable-PnpDevice', 'Stop-Service', 'Start-Service',
        'SetDefaultEndpoint', 'audio_endpoint_probe',
        'xm5_connection_probe', 'Assert-LegacyAdministrator')) {
    if ($completion.IndexOf(
            $forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The policy v9 completion probes or mutates the system: $forbidden"
    }
}

$tokens = $null
$errors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $root 'tools\complete-v1-linked-limiter-report.ps1'),
    [ref]$tokens,
    [ref]$errors)
if (@($errors).Count -ne 0) {
    throw "The policy v9 completion helper does not parse: $errors"
}

. (Join-Path $root 'tools\v1-linked-limiter-common.ps1')
. (Join-Path $root 'agent\tests\v1_linked_limiter_evidence_tests.ps1')
$transactionPath = Join-Path $root 'tmp\synthetic-v9-transaction.json'
$resultPath = Join-Path $root 'tmp\synthetic-v9-result.json'
$statePath = Join-Path $root 'tmp\synthetic-v9-state.json'
$sessionPath = Join-Path $root 'tmp\synthetic-v9-session.json'
$prerequisitePath = Join-Path $root 'tmp\synthetic-v8-transaction.json'
$state | Add-Member -NotePropertyName __evidence_path `
    -NotePropertyValue $statePath -Force
$session | Add-Member -NotePropertyName __evidence_path `
    -NotePropertyValue $sessionPath -Force
$manifest = [pscustomobject]@{
    source_commit = ('a' * 40)
    driver_tree = ('b' * 40)
    prerequisite = $prerequisitePath
}
$transaction = [pscustomobject]@{
    schema_version = 1
    transport_policy_version = 9
    status = 'transport-verified-awaiting-linked-limiter-report'
    source_commit = $manifest.source_commit
    driver_tree = $manifest.driver_tree
    prerequisite = $prerequisitePath
    result = $resultPath
    state = $statePath
    session = $sessionPath
}
$result = [pscustomobject]@{
    schema_version = 1
    transport_policy_version = 9
    transport_passed = $true
    quality_comparison_observation = 'user-report-required'
    source_commit = $manifest.source_commit
    driver_tree = $manifest.driver_tree
    prerequisite = $prerequisitePath
    transaction = $transactionPath
    limiter_algorithm = $session.limiter_algorithm
    limiter_algorithm_version = $session.limiter_algorithm_version
    limiter_release_ms = $session.limiter_release_ms
    limiter_minimum_gain = $session.limiter_minimum_gain
    limiter_gain_reduced_frames = $session.limiter_gain_reduced_frames
    limiter_gain_reduced_samples = $session.limiter_gain_reduced_samples
    limiter_fallback_clamp_count = 0
    target_duration_ms = 60000
    actual_duration_ms = $session.actual_duration_ms
    consumer_lease_acquire_count = $session.consumer_lease_acquire_count
    consumer_lease_release_count = $session.consumer_lease_release_count
    consumer_lease_released = $true
    start_accepted = $true
    suspend_accepted = $true
    close_accepted = $true
    driver_installed_or_updated = $false
    rebooted = $false
    bluetooth_toggled = $false
    default_output_changed = $false
    link_state_written = $false
}
$arguments = @{
    Transaction = $transaction
    Result = $result
    Manifest = $manifest
    State = $state
    Session = $session
    Attempts = $attempts
    TransactionPath = $transactionPath
    ResultPath = $resultPath
}
if (-not (Test-V1LinkedLimiterCompletionEvidence @arguments)) {
    throw 'Valid policy v9 completion evidence was rejected.'
}
$alreadyReported = $result | ConvertTo-Json | ConvertFrom-Json
$alreadyReported.quality_comparison_observation = 'not-assessed-by-user'
$arguments.Result = $alreadyReported
if (Test-V1LinkedLimiterCompletionEvidence @arguments) {
    throw 'An already-finalized policy v9 result was accepted.'
}
$wrongAlgorithm = $result | ConvertTo-Json | ConvertFrom-Json
$wrongAlgorithm.limiter_algorithm = 'hard-clip'
$arguments.Result = $wrongAlgorithm
if (Test-V1LinkedLimiterCompletionEvidence @arguments) {
    throw 'A policy v9 result from another limiter was accepted.'
}
$wrongDriver = $transaction | ConvertTo-Json | ConvertFrom-Json
$wrongDriver.driver_tree = ('c' * 40)
$arguments.Result = $result
$arguments.Transaction = $wrongDriver
if (Test-V1LinkedLimiterCompletionEvidence @arguments) {
    throw 'A policy v9 result from another driver tree was accepted.'
}

Write-Host 'V1 linked-limiter user-report completion policy tests passed.'
