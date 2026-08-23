# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [switch]$ConfirmV1ZeroPacketGate,
    [ValidateRange(120, 300)]
    [int]$DurationSeconds = 180,
    [string]$TransactionPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $ConfirmV1ZeroPacketGate) {
    throw 'Refusing to authorize the zero-packet gate. Re-run with -ConfirmV1ZeroPacketGate.'
}
$arguments = @{
    ConfirmV1PostRebootDiscovery = $true
    DurationSeconds = $DurationSeconds
}
if (-not [string]::IsNullOrWhiteSpace($TransactionPath)) {
    $arguments.TransactionPath = $TransactionPath
}
& (Join-Path $PSScriptRoot 'run-v1-post-reboot-discovery-gate.ps1') `
    @arguments
