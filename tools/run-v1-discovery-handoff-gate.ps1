# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [switch]$ConfirmV1DiscoveryHandoff,
    [ValidateRange(90, 300)]
    [int]$DurationSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$trialRoot = Join-Path $projectRoot `
    'artifacts\v1-discovery-handoff\trial'
$latestPath = Join-Path $trialRoot 'latest-transaction.txt'
if (Test-Path -LiteralPath $latestPath -PathType Leaf) {
    $transactionPath = (Get-Content -LiteralPath $latestPath -Raw).Trim()
    if (Test-Path -LiteralPath $transactionPath -PathType Leaf) {
        $transaction = Get-Content -LiteralPath $transactionPath -Raw |
            ConvertFrom-Json
        $unfinishedStates = @(
            'starting',
            'running',
            'rollback-incomplete'
        )
        $rollbackProperty = $transaction.PSObject.Properties['rollback']
        $rollbackSucceeded = $null -ne $rollbackProperty -and
            $null -ne $rollbackProperty.Value -and
            $rollbackProperty.Value.succeeded -eq $true
        if ([string]$transaction.status -in $unfinishedStates -or
            ([string]$transaction.phase -eq
                'restoring-original-a2dp' -and
                -not $rollbackSucceeded)) {
            throw "An unfinished V1 discovery handoff transaction must be recovered first: $transactionPath`nKeep XM5 off and run: powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\rollback-v1-discovery-handoff.ps1 -ConfirmV1DiscoveryRollback"
        }
    }
}

throw @'
This same-boot driver handoff gate is retired and cannot modify the system.
Two hardware runs reached one generation-bound signaling OPEN and both failed before the first AVDTP exchange with Win32 71.
No timing change, retry, Bluetooth toggle, or user action can make this gate admissible.
Use the reviewed one-reboot V1 discovery transaction after its candidate is built:
  .\tools\prepare-v1-reboot-discovery-gate.ps1 -ConfirmV1RebootPreparation -ConfirmPinImpactAndReboot
'@
