# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1InboundPnpRundownRollback,
    [string]$TransactionPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'v1-inbound-pnp-rundown-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1InboundPnpRundownRollback) {
    throw 'Refusing to restore the safe outbound-only LdacNative package. Re-run with -ConfirmV1InboundPnpRundownRollback.'
}
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$trialRoot = Join-Path $root 'artifacts\v1-inbound-pnp-rundown\trial'
if ([string]::IsNullOrWhiteSpace($TransactionPath)) {
    $latestPath = Join-Path $trialRoot 'latest-transaction.txt'
    if (-not (Test-Path -LiteralPath $latestPath -PathType Leaf)) {
        throw 'No PnP-rundown transaction was found.'
    }
    $TransactionPath = (Get-Content -LiteralPath $latestPath -Raw).Trim()
}
$TransactionPath = [System.IO.Path]::GetFullPath($TransactionPath)
$transaction = Get-Content -LiteralPath $TransactionPath -Raw |
    ConvertFrom-Json
if ([int]$transaction.schema_version -ne 1 -or
    [int]$transaction.transport_policy_version -ne
        $script:V1InboundPnpRundownPolicyVersion -or
    [string]$transaction.status -notin @(
        'rollback-required', 'operator-disconnect-required',
        'rollback-failed', 'reboot-required-after-prepare-error')) {
    throw 'The selected PnP-rundown transaction is not rollback-eligible.'
}
$candidate = Get-V1InboundPnpRundownCandidate `
    -CandidatePath ([string]$transaction.candidate_path)
$connectionProbe = Join-Path $candidate.root 'xm5_connection_probe.exe'
if ((Get-NativeLdacXm5BluetoothState -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$transaction.source_commit)) -ne
    'disconnected') {
    throw 'Keep XM5 off and wait for its public Bluetooth state to become disconnected.'
}
if (@(Get-NativeLdacWorkspaceProcesses).Count -ne 0) {
    throw 'Close all workspace media and agent processes before rollback.'
}
$fallbackInfPath = [string]$transaction.safe_fallback_backup_inf
if (-not (Test-Path -LiteralPath $fallbackInfPath -PathType Leaf)) {
    throw "The safe outbound-only fallback INF is missing: $fallbackInfPath"
}
$prerequisite = Get-Content -LiteralPath `
    ([string]$transaction.prerequisite_transaction) -Raw |
    ConvertFrom-Json
$expectedFallbackInf = [string]$prerequisite.previous_inf
if ([string]::IsNullOrWhiteSpace($expectedFallbackInf)) {
    throw 'The prerequisite does not identify its outbound-only fallback INF.'
}

Write-Host 'V1 inbound PnP-rundown rollback preflight passed.'
Write-Host 'This rollback removes both inbound-server packages before restoring the verified outbound-only ABI 0.5 package.'
Write-Host 'It does not toggle the Bluetooth radio or modify the Native audio endpoint.'
if (-not $PSCmdlet.ShouldProcess(
        'Sony WH-1000XM5 A2DP Sink service PDO',
        'Remove the fixed and old inbound-server packages, restore the exported outbound-only LdacNative package, restart only this PDO, and verify ready flags 0x7')) {
    return
}

$logDirectory = Join-Path $trialRoot `
    ('rollback-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$transaction.rollback.attempted = $true
$transaction.status = 'rollback-required'
$transaction.phase = 'restoring-safe-outbound-only-driver'
$transaction.updated_at = (Get-Date).ToString('o')
Write-LegacyJsonAtomic -Value $transaction -Path $TransactionPath
try {
    $oldInboundInf = [string]$transaction.previous_inf
    $selectedInf = [string]$transaction.selected_inf
    if (-not [string]::IsNullOrWhiteSpace($oldInboundInf) -and
        -not $oldInboundInf.Equals(
            $expectedFallbackInf,
            [StringComparison]::OrdinalIgnoreCase)) {
        $oldPackages = @(Get-LegacyDriverPackages `
            -OriginalInfNames @('LdacNative.inf') | Where-Object {
                ([string]$_.Driver).Equals(
                    $oldInboundInf,
                    [StringComparison]::OrdinalIgnoreCase)
        })
        if ($oldPackages.Count -ne 0) {
            $removeOld = Invoke-LegacyPnpUtil -Arguments @(
                    '/delete-driver', $oldInboundInf, '/force') `
                -LogPath (Join-Path $logDirectory `
                    "remove-old-inbound-$oldInboundInf.log")
            if ($removeOld.reboot_required) {
                throw 'Removing the inactive old inbound package unexpectedly requires a Windows reboot.'
            }
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($selectedInf) -and
        -not $selectedInf.Equals(
            $expectedFallbackInf,
            [StringComparison]::OrdinalIgnoreCase)) {
        $selectedPackages = @(Get-LegacyDriverPackages `
            -OriginalInfNames @('LdacNative.inf') | Where-Object {
                ([string]$_.Driver).Equals(
                    $selectedInf,
                    [StringComparison]::OrdinalIgnoreCase)
            })
        if ($selectedPackages.Count -ne 0) {
            $remove = Invoke-LegacyPnpUtil -Arguments @(
                    '/delete-driver', $selectedInf,
                    '/uninstall', '/force') `
                -LogPath (Join-Path $logDirectory `
                    "remove-fixed-$selectedInf.log")
            if ($remove.reboot_required) {
                throw 'Removing the fixed PnP-rundown package unexpectedly requires a Windows reboot.'
            }
        }
    }

    $restore = Invoke-LegacyPnpUtil -Arguments @(
            '/add-driver', $fallbackInfPath, '/install') `
        -LogPath (Join-Path $logDirectory `
            'restore-safe-outbound-only.log') `
        -AcceptedExitCodes @(0, 259, 3010)
    if ($restore.reboot_required) {
        throw 'Restoring the safe outbound-only package unexpectedly requires a Windows reboot.'
    }
    $restart = Invoke-LegacyPnpUtil -Arguments @(
            '/restart-device', [string]$transaction.device_instance_id) `
        -LogPath (Join-Path $logDirectory 'restart-safe-fallback.log')
    if ($restart.reboot_required) {
        throw 'Restarting the safe fallback PDO unexpectedly requires a Windows reboot.'
    }

    $restored = Wait-LegacyXm5A2dpService `
        -ExpectedService 'LdacNative' -TimeoutSeconds 30
    if ($null -eq $restored -or [int]$restored.problem_code -ne 0 -or
        -not ([string]$restored.published_inf).Equals(
            $expectedFallbackInf,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The safe outbound-only LdacNative binding was not restored exactly.'
    }
    $probe = Join-Path $candidate.root 'transport_probe.exe'
    $ready = Wait-V1InboundTransportInfo -ProbePath $probe `
        -ExpectedFlags 0x00000007 -TimeoutSeconds 20
    if ($null -eq $ready) {
        throw 'The safe outbound-only driver did not return ready flags 0x7.'
    }
    $transaction.rollback.succeeded = $true
    $transaction.rollback.restored_inf = $expectedFallbackInf
    $transaction.rollback.error = $null
    $transaction.status = 'rollback-verified'
    $transaction.phase = 'safe-outbound-only-driver-restored'
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
    throw "V1 inbound PnP-rundown rollback failed: $($transaction.rollback.error) Transaction: $TransactionPath"
}
Write-Host "Safe outbound-only LdacNative restored: $expectedFallbackInf, ready flags 0x00000007."
Write-Host 'No Windows reboot or Bluetooth radio toggle occurred.'
Write-Host "Transaction: $TransactionPath"
