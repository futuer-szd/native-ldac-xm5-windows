# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'Resident AVRCP install policy tests require PowerShell 7.'
}

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $root 'tools\v1-avrcp-observer-resident-common.ps1')

function Assert-Policy([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$englishInf = Get-ResidentPublishedInfFromPnpUtilOutput -Lines @(
    'Microsoft PnP Utility',
    'Published Name:     oem9702.inf')
Assert-Policy ($englishInf -eq 'oem9702.inf') `
    'The installer cannot parse the English pnputil published INF.'

$chineseInf = Get-ResidentPublishedInfFromPnpUtilOutput -Lines @(
    'Microsoft PnP Tool',
    '发布名称:         oem9702.inf')
Assert-Policy ($chineseInf -eq 'oem9702.inf') `
    'The installer cannot parse the Chinese pnputil published INF.'

$multipleRejected = $false
try {
    [void](Get-ResidentPublishedInfFromPnpUtilOutput -Lines @(
        'Published Name: oem9701.inf',
        'Published Name: oem9702.inf'))
} catch {
    $multipleRejected = $true
}
Assert-Policy $multipleRejected `
    'The installer accepted ambiguous pnputil published INF output.'

$state = @'
{
  "state_version": 2,
  "published_inf": "oem9702.inf",
  "current_package": { "published_inf": "oem9702.inf" },
  "previous_observer_packages": [
    { "published_inf": "oem9701.inf" },
    { "published_inf": "oem9701.inf" }
  ]
}
'@ | ConvertFrom-Json
Assert-Policy ((Get-ResidentCurrentPublishedInf -State $state) -eq 'oem9702.inf') `
    'The rollback cannot select the current v2 resident package.'
$history = @(Get-ResidentHistoricalPublishedInfs -State $state)
Assert-Policy ($history.Count -eq 1 -and $history[0] -eq 'oem9701.inf') `
    'The rollback did not preserve a unique historical resident package list.'

$legacyState = '{ "published_inf": "oem9701.inf" }' | ConvertFrom-Json
Assert-Policy ((Get-ResidentCurrentPublishedInf -State $legacyState) -eq 'oem9701.inf') `
    'The rollback no longer accepts a legacy resident install state.'

foreach ($acceptedDeleteExit in @(0, 259, -536870340, 3758096956)) {
    Assert-Policy `
        (Test-ResidentPnpUtilDeleteExitCode -ExitCode $acceptedDeleteExit) `
        "Rollback rejected idempotent pnputil delete exit $acceptedDeleteExit."
}
foreach ($rejectedDeleteExit in @(-1, 1, 5, 3010)) {
    Assert-Policy `
        (-not (Test-ResidentPnpUtilDeleteExitCode `
            -ExitCode $rejectedDeleteExit)) `
        "Rollback accepted unexpected pnputil delete exit $rejectedDeleteExit."
}

$interruptedState = @'
{
  "state_version": 2,
  "published_inf": "oem9703.inf",
  "current_package": { "published_inf": "oem9703.inf" },
  "previous_observer_packages": [
    { "published_inf": "oem9701.inf" },
    { "published_inf": "oem9702.inf" }
  ]
}
'@ | ConvertFrom-Json
$interruptedPlan = Get-ResidentRollbackPackagePlan `
    -State $interruptedState `
    -CandidatePackages @(
        [pscustomobject]@{ published_inf = 'oem9701.inf' },
        [pscustomobject]@{ published_inf = 'oem9702.inf' }) `
    -ActiveObserverInf 'oem9702.inf'
Assert-Policy ($interruptedPlan.known_packages.Count -eq 3 -and
               $interruptedPlan.known_packages[0] -eq 'oem9703.inf' -and
               $interruptedPlan.inactive_packages.Count -eq 2 -and
               $interruptedPlan.inactive_packages[0] -eq 'oem9703.inf' -and
               $interruptedPlan.inactive_packages[1] -eq 'oem9701.inf' -and
               $interruptedPlan.active_package -eq 'oem9702.inf' -and
               $interruptedPlan.present_packages.Count -eq 2 -and
               'oem9703.inf' -notin $interruptedPlan.present_packages) `
    'Interrupted rollback cannot delete inactive known packages before the historical active package.'

$unknownActiveRejected = $false
try {
    [void](Get-ResidentRollbackPackagePlan `
        -State $interruptedState `
        -CandidatePackages @('oem9701.inf', 'oem9702.inf') `
        -ActiveObserverInf 'oem9706.inf')
} catch {
    $unknownActiveRejected = $true
}
Assert-Policy $unknownActiveRejected `
    'Rollback accepted an active Native observer package outside install state.'

$unmanagedPackageRejected = $false
try {
    [void](Get-ResidentRollbackPackagePlan `
        -State $interruptedState `
        -CandidatePackages @('oem9701.inf', 'oem9702.inf', 'oem9706.inf') `
        -ActiveObserverInf 'oem9702.inf')
} catch {
    $unmanagedPackageRejected = $true
}
Assert-Policy $unmanagedPackageRejected `
    'Rollback accepted an unmanaged Native observer Driver Store package.'

$emptyStorePlan = Get-ResidentRollbackPackagePlan `
    -State $interruptedState `
    -CandidatePackages @() `
    -ActiveObserverInf $null
Assert-Policy ($emptyStorePlan.inactive_packages.Count -eq 3 -and
               $null -eq $emptyStorePlan.active_package -and
               $emptyStorePlan.present_packages.Count -eq 0) `
    'Rollback cannot resume after every recorded package is already absent.'

$install = Get-Content -LiteralPath `
    (Join-Path $root 'tools\install-v1-avrcp-observer-resident.ps1') -Raw
$rollback = Get-Content -LiteralPath `
    (Join-Path $root 'tools\rollback-v1-avrcp-observer-resident.ps1') -Raw
$common = Get-Content -LiteralPath `
    (Join-Path $root 'tools\v1-avrcp-observer-resident-common.ps1') -Raw
$live = Get-Content -LiteralPath `
    (Join-Path $root 'tools\verify-v1-avrcp-resident-live.ps1') -Raw

$repairIndex = $install.IndexOf('if ($RepairInterruptedInstall)')
$normalIndex = $install.IndexOf('$timestamp = Get-Date', $repairIndex)
$repairBlock = $install.Substring($repairIndex, $normalIndex - $repairIndex)
Assert-Policy ($repairIndex -ge 0 -and $normalIndex -gt $repairIndex -and
               $repairBlock -notmatch '/add-driver|/delete-driver|/scan-devices|/restart-device') `
    'Interrupted install repair can mutate the Driver Store or PnP state.'
Assert-Policy ($repairBlock -match 'requires the previous install state' -and
               $repairBlock -match 'knownPreviousInfs' -and
               $repairBlock -match 'unmanagedPackages') `
    'Interrupted install repair can infer or silently adopt unmanaged package history.'
Assert-Policy ($install -match 'Get-ResidentPublishedInfFromPnpUtilOutput' -and
               $install -match 'Test-ResidentPublishedInfMatchesCandidate' -and
               $install -match 'Write-ResidentJsonAtomically' -and
               $install -match 'state_version = 2') `
    'The installer does not bind its transaction to the pnputil result and atomic state.'
$packagesBeforeIndex = $install.IndexOf('$packagesBeforeInstall =')
$addDriverIndex = $install.IndexOf("@('/add-driver', `$infPath)")
Assert-Policy ($packagesBeforeIndex -ge 0 -and
               $addDriverIndex -gt $packagesBeforeIndex) `
    'Normal upgrade does not capture historical packages before pnputil mutates the Driver Store.'
Assert-Policy ($rollback -match 'Get-ResidentRollbackPackagePlan' -and
               $rollback -match 'unexpected active driver' -and
               $rollback -match 'RequirePresent') `
    'Rollback does not fail closed on package ownership or baseline recovery.'
$planIndex = $rollback.IndexOf('$packagePlan = Get-ResidentRollbackPackagePlan')
$mutationIndex = $rollback.IndexOf('$PSCmdlet.ShouldProcess')
$inactiveDeleteIndex = $rollback.IndexOf(
    "action = 'delete-inactive-known-resident-package'")
$absentSkipIndex = $rollback.IndexOf(
    "action = 'skip-absent-known-resident-package'")
$inactiveInvokeIndex = $rollback.IndexOf(
    '$inactiveDelete = Invoke-PnpUtil', $absentSkipIndex)
$activeDeleteIndex = $rollback.IndexOf(
    "action = 'delete-active-known-resident-package'")
$scanIndex = $rollback.IndexOf("action = 'scan-devices'")
$baselineIndex = $rollback.IndexOf(
    '$snapshot = Wait-ForBaselineSnapshot', $scanIndex)
Assert-Policy ($planIndex -ge 0 -and $mutationIndex -gt $planIndex -and
               $absentSkipIndex -gt $mutationIndex -and
               $inactiveInvokeIndex -gt $absentSkipIndex -and
               $inactiveDeleteIndex -gt $inactiveInvokeIndex -and
               $activeDeleteIndex -gt $inactiveDeleteIndex -and
               $scanIndex -gt $activeDeleteIndex -and
               $baselineIndex -gt $scanIndex) `
    'Rollback can verify Microsoft or scan before every known exact-match package is removed.'
Assert-Policy ($rollback.Contains(
                   "'/delete-driver', `$activeInf, '/uninstall', '/force'") -and
               $rollback -match 'Assert-PackageDeleteCompleted' -and
               $rollback -match 'but the package remains in the Driver Store' -and
               $rollback -match 'Write-ResidentJsonAtomically') `
    'Rollback lost active-package uninstall, immediate inventory verification, or atomic rollback state.'
Assert-Policy ($common -match 'Published\\s\+Name\|发布名称' -and
               $common -match 'Test-ResidentPnpUtilDeleteExitCode' -and
               $common -match '-536870340' -and
               $common -match 'Get-ResidentRollbackPackagePlan' -and
               $common -match 'Unmanaged Native observer packages block rollback' -and
               $common -match 'Move-Item -LiteralPath \$temporaryPath') `
    'The shared resident transaction helpers lost localized parsing or atomic state replacement.'

$mediaReadyIndex = $live.IndexOf("`$transportReady = `$true")
$restartIndex = $live.IndexOf("'/restart-device'", $mediaReadyIndex)
$sameInfIndex = $live.IndexOf(
    '$afterRestart.inf -ieq $snapshot.inf', $restartIndex)
$probeIndex = $live.IndexOf('$probeLines = @(& $ProbePath', $sameInfIndex)
Assert-Policy ($live -match 'RestartExactPdoAfterMediaReady' -and
               $mediaReadyIndex -ge 0 -and
               $restartIndex -gt $mediaReadyIndex -and
               $sameInfIndex -gt $restartIndex -and
               $probeIndex -gt $sameInfIndex -and
               ([regex]::Matches($live, "'/restart-device'")).Count -eq 1) `
    'The resident live diagnostic lost its explicit one-restart, post-media, same-package boundary.'

foreach ($path in @(
        'tools\v1-avrcp-observer-resident-common.ps1',
        'tools\install-v1-avrcp-observer-resident.ps1',
        'tools\rollback-v1-avrcp-observer-resident.ps1',
        'tools\verify-v1-avrcp-resident-live.ps1')) {
    $tokens = $null
    $errors = $null
    [void][Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $root $path), [ref]$tokens, [ref]$errors)
    Assert-Policy ($errors.Count -eq 0) "PowerShell parser errors in $path"
}

Write-Host 'V1 AVRCP resident install policy passed.'
Write-Host 'Upgrade, interrupted-install repair, and resumable rollback package ordering are fail-closed.'
