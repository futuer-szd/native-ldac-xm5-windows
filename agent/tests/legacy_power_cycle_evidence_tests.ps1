# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
. (Join-Path $projectRoot 'tools\native-ldac-baseline-common.ps1')

function Assert-Evidence {
    param(
        [Parameter(Mandatory = $true)]$Transaction,
        [Parameter(Mandatory = $true)][bool]$Expected,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $actual = Test-NativeLdacPhysicalPowerOnEvidence `
        -Transaction $Transaction
    if ($actual -ne $Expected) {
        throw "$Label`: expected $Expected, got $actual."
    }
}

Assert-Evidence -Transaction ([pscustomobject]@{}) `
    -Expected $false -Label 'missing post-reboot object'
Assert-Evidence -Transaction ([pscustomobject]@{
        post_reboot = [pscustomobject]@{
            connected_at = '2026-07-21T21:13:01+08:00'
        }
    }) -Expected $false -Label 'legacy fConnected-only evidence'
Assert-Evidence -Transaction ([pscustomobject]@{
        post_reboot = [pscustomobject]@{
            acl_event_observed_at = '2026-07-21T22:00:00+08:00'
        }
    }) -Expected $false -Label 'ACL event without operator confirmation'
Assert-Evidence -Transaction ([pscustomobject]@{
        post_reboot = [pscustomobject]@{
            physical_power_on_confirmed_at =
                '2026-07-21T22:00:01+08:00'
        }
    }) -Expected $false -Label 'operator confirmation without ACL event'
Assert-Evidence -Transaction ([pscustomobject]@{
        post_reboot = [pscustomobject]@{
            acl_event_observed_at = '2026-07-21T22:00:00+08:00'
            physical_power_on_confirmed_at =
                '2026-07-21T22:00:01+08:00'
        }
    }) -Expected $true -Label 'complete evidence'
Assert-Evidence -Transaction ([pscustomobject]@{
        post_reboot = [pscustomobject]@{
            acl_event_observed_at = ' '
            physical_power_on_confirmed_at =
                '2026-07-21T22:00:01+08:00'
        }
    }) -Expected $false -Label 'blank ACL evidence'

Write-Host 'Legacy physical power-cycle evidence tests passed.'
