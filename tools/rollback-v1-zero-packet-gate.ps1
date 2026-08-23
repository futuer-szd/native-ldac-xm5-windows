# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [switch]$ConfirmV1ZeroPacketRollback,
    [string]$TransactionPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $ConfirmV1ZeroPacketRollback) {
    throw 'Refusing to restore AltA2DP. Re-run with -ConfirmV1ZeroPacketRollback.'
}
$arguments = @{
    ConfirmV1RebootDiscoveryRollback = $true
}
if (-not [string]::IsNullOrWhiteSpace($TransactionPath)) {
    $arguments.TransactionPath = $TransactionPath
}
& (Join-Path $PSScriptRoot 'rollback-v1-reboot-discovery-gate.ps1') `
    @arguments
