# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    Get-Content -LiteralPath (Join-Path $root $RelativePath) -Raw
}

$completion = Read-ProjectFile 'tools\complete-v1-stability-report.ps1'
$common = Read-ProjectFile 'tools\v1-stability-burst-common.ps1'
foreach ($required in @(
        '[Parameter(Mandatory = $true)]',
        '[ValidateSet(''generally-clear'', ''muffled-bass'')]',
        '-ConfirmV1StabilityReport',
        'transport-verified-awaiting-user-stability-report',
        'user-report-required',
        'Test-V1StabilityCompletionEvidence',
        'ExpectedDurationMs 60000',
        'consumer_lease_acquire_count',
        'consumer_lease_release_count',
        'avdtp_suspend_accepted',
        'avdtp_close_accepted',
        'Manifest.source_commit',
        'Manifest.driver_tree',
        'user-reported-generally-clear-with-muffled-bass',
        '''not-reported''',
        'Write-LegacyJsonAtomic -Value $result -Path $ResultPath',
        'Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath')) {
    if (-not (($completion + $common).Contains($required))) {
        throw "The policy v8 completion contract is missing: $required"
    }
}
foreach ($forbidden in @(
        'pnputil', 'devcon', 'Restart-Computer', 'Disable-PnpDevice',
        'Enable-PnpDevice', 'Stop-Service', 'Start-Service',
        'SetDefaultEndpoint', 'audio_endpoint_probe',
        'xm5_connection_probe', 'Assert-LegacyAdministrator')) {
    if ($completion.IndexOf(
            $forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The policy v8 completion mutates or probes the baseline: $forbidden"
    }
}

$tokens = $null
$errors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $root 'tools\complete-v1-stability-report.ps1'),
    [ref]$tokens,
    [ref]$errors)
if (@($errors).Count -ne 0) {
    throw "The policy v8 completion helper does not parse: $errors"
}

. (Join-Path $root 'tools\v1-stability-burst-common.ps1')
. (Join-Path $root 'agent\tests\v1_transport_stability_evidence_tests.ps1')
$transactionPath = Join-Path $root 'tmp\synthetic-stability-transaction.json'
$resultPath = Join-Path $root 'tmp\synthetic-stability-result.json'
$statePath = Join-Path $root 'tmp\synthetic-stability-state.json'
$sessionPath = Join-Path $root 'tmp\synthetic-stability-session.json'
$state | Add-Member -NotePropertyName __evidence_path `
    -NotePropertyValue $statePath -Force
$session | Add-Member -NotePropertyName __evidence_path `
    -NotePropertyValue $sessionPath -Force
$manifest = [pscustomobject]@{
    source_commit = ('a' * 40)
    driver_tree = ('b' * 40)
}
$transaction = [pscustomobject]@{
    schema_version = 1
    status = 'transport-verified-awaiting-user-stability-report'
    result = $resultPath
    state = $statePath
    session = $sessionPath
    source_commit = $manifest.source_commit
    driver_tree = $manifest.driver_tree
}
$result = [pscustomobject]@{
    schema_version = 1
    transport_passed = $true
    stability_observation = 'user-report-required'
    transaction = $transactionPath
    source_commit = $manifest.source_commit
    target_duration_ms = 60000
    actual_duration_ms = 60003
    consumer_lease_acquire_count = 2
    consumer_lease_release_count = 2
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
$completionArguments = @{
    Transaction = $transaction
    Result = $result
    Manifest = $manifest
    State = $state
    Session = $session
    Attempts = $attempts
    TransactionPath = $transactionPath
    ResultPath = $resultPath
}
if (-not (Test-V1StabilityCompletionEvidence @completionArguments)) {
    throw 'Valid policy v8 user-report completion evidence was rejected.'
}
$wrongStatus = $result | ConvertTo-Json | ConvertFrom-Json
$wrongStatus.stability_observation = 'already-finalized'
$completionArguments.Result = $wrongStatus
if (Test-V1StabilityCompletionEvidence @completionArguments) {
    throw 'An already-finalized policy v8 result was accepted.'
}
$leakedLease = $result | ConvertTo-Json | ConvertFrom-Json
$leakedLease.consumer_lease_release_count = 1
$completionArguments.Result = $leakedLease
if (Test-V1StabilityCompletionEvidence @completionArguments) {
    throw 'A policy v8 result with an unbalanced ConsumerLease was accepted.'
}
$wrongDriver = $transaction | ConvertTo-Json | ConvertFrom-Json
$wrongDriver.driver_tree = ('c' * 40)
$completionArguments.Result = $result
$completionArguments.Transaction = $wrongDriver
if (Test-V1StabilityCompletionEvidence @completionArguments) {
    throw 'A policy v8 transaction from another driver tree was accepted.'
}

Write-Host 'V1 stability user-report completion policy tests passed.'
