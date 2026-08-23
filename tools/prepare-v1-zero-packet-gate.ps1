# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [switch]$ConfirmV1ZeroPacketPreparation,
    [switch]$ConfirmPinImpactAndReboot,
    [string]$CandidatePath,
    [string]$BackupPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $ConfirmV1ZeroPacketPreparation) {
    throw 'Refusing to prepare the zero-packet gate. Re-run with -ConfirmV1ZeroPacketPreparation.'
}
$arguments = @{
    ConfirmV1RebootPreparation = $true
    ConfirmPinImpactAndReboot = $ConfirmPinImpactAndReboot
}
if (-not [string]::IsNullOrWhiteSpace($CandidatePath)) {
    $arguments.CandidatePath = $CandidatePath
}
if (-not [string]::IsNullOrWhiteSpace($BackupPath)) {
    $arguments.BackupPath = $BackupPath
}
& (Join-Path $PSScriptRoot 'prepare-v1-reboot-discovery-gate.ps1') `
    @arguments
