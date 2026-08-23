# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('Release')][string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'v1-inbound-pnp-rundown-common.ps1')

$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output = Join-Path $root 'artifacts\v1-inbound-pnp-rundown\candidate'
& (Join-Path $PSScriptRoot 'build-v1-inbound-signaling-candidate.ps1') `
    -Configuration $Configuration -OutputPath $output
if ($LASTEXITCODE -ne 0) {
    throw "The inbound PnP-rundown candidate build failed with exit code $LASTEXITCODE."
}

$manifestPath = Join-Path $output 'manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$capabilities = @($manifest.capabilities | ForEach-Object { [string]$_ })
foreach ($capability in @(
        'self_managed_io_suspend_server_rundown',
        'self_managed_io_restart_server_registration',
        'failed_unregister_preserves_server_handle',
        'one_shot_inbound_listener_rundown',
        'atomic_connected_and_rundown_publish',
        'delayed_pnp_failure_observation',
        'known_code38_single_reboot_recovery',
        'activation_requires_one_windows_restart',
        'two_cycle_discover_only_pnp_validation')) {
    if ($capability -notin $capabilities) {
        $capabilities += $capability
    }
}
$manifest.capabilities = @($capabilities)
$manifest | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $manifestPath -Encoding UTF8
$candidate = Get-V1InboundPnpRundownCandidate -CandidatePath $output

Write-Host "Built V1 inbound PnP-rundown candidate: $($candidate.root)"
Write-Host "Source commit: $($candidate.manifest.source_commit)"
Write-Host "Approved driver tree: $($candidate.manifest.driver_tree)"
Write-Host 'No driver, service, process, Bluetooth request, or system setting was changed.'
