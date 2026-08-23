# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1RebootDiscoveryRollback,
    [string]$TransactionPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'v1-reboot-discovery-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1RebootDiscoveryRollback) {
    throw 'Refusing to restore AltA2DP. Re-run with -ConfirmV1RebootDiscoveryRollback.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$trialRoot = Join-Path $projectRoot `
    'artifacts\v1-reboot-discovery\trial'
if ([string]::IsNullOrWhiteSpace($TransactionPath)) {
    $latestPath = Join-Path $trialRoot 'latest-transaction.txt'
    if (-not (Test-Path -LiteralPath $latestPath -PathType Leaf)) {
        throw 'No V1 reboot discovery transaction was found.'
    }
    $TransactionPath = (Get-Content -LiteralPath $latestPath -Raw).Trim()
}
$TransactionPath = [System.IO.Path]::GetFullPath($TransactionPath)
$transaction = Get-Content -LiteralPath $TransactionPath -Raw |
    ConvertFrom-Json
if ([int]$transaction.schema_version -ne 1 -or
    [string]$transaction.status -notin @(
        'awaiting-reboot',
        'running-post-reboot',
        'rollback-required',
        'finalization-required',
        'rollback-failed',
        'discovery-verified',
        'configuration-verified')) {
    throw 'The selected transaction is not rollback-eligible.'
}

$candidate = Get-V1RebootDiscoveryCandidate `
    -CandidatePath ([string]$transaction.candidate_path)
$connectionProbe = Join-Path $candidate.root 'xm5_connection_probe.exe'
if ((Get-NativeLdacXm5BluetoothState `
        -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$transaction.source_commit)) -ne
    'disconnected') {
    throw 'Turn off XM5 and wait for physical disconnection before rollback.'
}

$target = 'Sony WH-1000XM5 A2DP Sink service PDO'
$action = 'Stop contained V1 processes, remove LdacNative, restore original AltA2DP, and verify the safe baseline without reboot'
if (-not $PSCmdlet.ShouldProcess($target, $action)) {
    return
}

$logDirectory = Join-Path $trialRoot `
    ("rollback-" + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$transaction.rollback.attempted = $true
$transaction.phase = 'restoring-original-a2dp'
$transaction.updated_at = (Get-Date).ToString('o')
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
try {
    foreach ($process in @(Get-NativeLdacWorkspaceProcesses)) {
        Stop-Process -Id ([int]$process.process_id) -Force `
            -ErrorAction SilentlyContinue
    }
    $restored = Restore-V1RebootDiscoveryOriginalA2dp `
        -Transaction $transaction -LogDirectory $logDirectory
    $transaction.rollback.succeeded = $true
    $transaction.rollback.service = $restored.service
    $transaction.rollback.published_inf = $restored.published_inf
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
    throw "V1 zero-packet gate rollback failed: $($transaction.rollback.error) Transaction: $TransactionPath"
}
Write-Host "Original A2DP restored: $($transaction.rollback.service), $($transaction.rollback.published_inf)."
Write-Host 'Alternative A2DP Service is Automatic/Running.'
Write-Host 'The V1 root audio endpoint was retained; no reboot or Windows Bluetooth toggle was performed.'
Write-Host "Transaction: $TransactionPath"
