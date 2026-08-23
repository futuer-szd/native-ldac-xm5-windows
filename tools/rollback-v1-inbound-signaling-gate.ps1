# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1InboundSignalingRollback,
    [string]$TransactionPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'v1-inbound-signaling-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1InboundSignalingRollback) {
    throw 'Refusing to restore the previous LdacNative driver. Re-run with -ConfirmV1InboundSignalingRollback.'
}
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$trialRoot = Join-Path $root 'artifacts\v1-inbound-signaling\trial'
if ([string]::IsNullOrWhiteSpace($TransactionPath)) {
    $latest = Join-Path $trialRoot 'latest-transaction.txt'
    if (-not (Test-Path -LiteralPath $latest -PathType Leaf)) {
        throw 'No inbound-signaling transaction was found.'
    }
    $TransactionPath = (Get-Content -LiteralPath $latest -Raw).Trim()
}
$TransactionPath = [System.IO.Path]::GetFullPath($TransactionPath)
$transaction = Get-Content -LiteralPath $TransactionPath -Raw |
    ConvertFrom-Json
if ([int]$transaction.schema_version -ne 1 -or
    [int]$transaction.transport_policy_version -ne
        $script:V1InboundSignalingPolicyVersion -or
    [string]$transaction.status -notin @(
        'driver-updated-ready', 'running-validation',
        'validation-failed', 'rollback-required', 'rollback-failed')) {
    throw 'The selected inbound-signaling transaction is not rollback-eligible.'
}
$candidate = Get-V1InboundSignalingCandidate `
    -CandidatePath ([string]$transaction.candidate_path)
$connectionProbe = Join-Path $candidate.root 'xm5_connection_probe.exe'
if ((Get-NativeLdacXm5BluetoothState -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$transaction.source_commit)) -ne
    'disconnected') {
    throw 'Turn off XM5 and wait for physical disconnection before rollback.'
}
if (-not $PSCmdlet.ShouldProcess(
        'Sony WH-1000XM5 A2DP Sink service PDO',
        'Remove the inbound-signaling package, restore the exported previous LdacNative package, restart only this PDO, and verify its original ready flags')) {
    return
}

$logDirectory = Join-Path $trialRoot `
    ('rollback-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$transaction.rollback.attempted = $true
$transaction.status = 'rollback-required'
$transaction.phase = 'restoring-previous-driver'
$transaction.updated_at = (Get-Date).ToString('o')
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
try {
    $restored = Restore-V1InboundPreviousDriver `
        -Transaction $transaction -LogDirectory $logDirectory
    $transaction.rollback.succeeded = $true
    $transaction.rollback.restored_inf = [string]$restored.published_inf
    $transaction.rollback.error = $null
    $transaction.status = 'rollback-verified'
    $transaction.phase = 'rollback-verified'
    $transaction.error = $null
} catch {
    $transaction.rollback.error = $_.Exception.Message
    $transaction.status = 'rollback-failed'
    $transaction.phase = 'rollback-failed'
} finally {
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
}
if (-not $transaction.rollback.succeeded) {
    throw "V1 inbound-signaling rollback failed: $($transaction.rollback.error) Transaction: $TransactionPath"
}
Write-Host "Previous LdacNative restored: $($transaction.rollback.restored_inf), ready flags 0x$('{0:X8}' -f [uint32]$transaction.previous_ready_flags)."
Write-Host 'No reboot or Windows Bluetooth toggle occurred.'
Write-Host "Transaction: $TransactionPath"
