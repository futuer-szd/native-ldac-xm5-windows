# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmResidentRollback
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:TargetPrefix =
    'BTHENUM\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
$script:OriginalInf = 'NativeLdacAvrcpObserver.inf'
$script:BaselineInf = 'microsoft_bluetooth_avrcptransport.inf'
$script:BaselineService = 'Microsoft_Bluetooth_AvrcpTransport'
. (Join-Path $PSScriptRoot 'v1-avrcp-observer-resident-common.ps1')

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Resident AVRCP observer rollback requires an elevated PowerShell 7 terminal.'
    }
}

function Get-PropertyData {
    param([string]$InstanceId, [string]$KeyName)
    $property = Get-PnpDeviceProperty -InstanceId $InstanceId `
        -KeyName $KeyName -ErrorAction SilentlyContinue
    if ($null -eq $property) { return $null }
    return $property.Data
}

function Get-TargetDevice {
    $matches = @(Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object {
        $_.InstanceId.StartsWith(
            $script:TargetPrefix,
            [StringComparison]::OrdinalIgnoreCase)
    })
    if ($matches.Count -eq 0) { return $null }
    if ($matches.Count -ne 1) {
        throw 'Expected at most one paired XM5 AVRCP 0x110E PDO.'
    }
    return $matches[0]
}

function Get-DeviceSnapshot {
    param([Parameter(Mandatory = $true)]$Device)
    [pscustomobject][ordered]@{
        instance_id = [string]$Device.InstanceId
        present = [bool]$Device.Present
        status = [string]$Device.Status
        problem_code = [int](Get-PropertyData `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_ProblemCode')
        inf = [string](Get-PropertyData -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverInfPath')
        service = [string](Get-PropertyData -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Service')
    }
}

function Get-CandidatePackages {
    return @(Get-WindowsDriver -Online -All | Where-Object {
        (Split-Path -Leaf ([string]$_.OriginalFileName)) -ieq
            $script:OriginalInf
    } | ForEach-Object {
        [pscustomobject][ordered]@{
            published_inf = [string]$_.Driver
            original_inf = Split-Path -Leaf ([string]$_.OriginalFileName)
        }
    })
}

function Invoke-PnpUtil {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $lines = @(& pnputil.exe @Arguments 2>&1)
    [pscustomobject][ordered]@{
        exit_code = $LASTEXITCODE
        lines = @($lines)
    }
}

function Assert-PackageDeleteCompleted {
    param(
        [Parameter(Mandatory = $true)][string]$PublishedInf,
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)][string]$Role
    )

    if (-not (Test-ResidentPnpUtilDeleteExitCode `
            -ExitCode $Result.exit_code)) {
        throw "pnputil delete failed for $Role package $PublishedInf with exit $($Result.exit_code)."
    }
    $stillPresent = @(Get-CandidatePackages | Where-Object {
        $_.published_inf -ieq $PublishedInf
    })
    if ($stillPresent.Count -ne 0) {
        throw "pnputil returned exit $($Result.exit_code) for $Role package $PublishedInf, but the package remains in the Driver Store."
    }
}

function Test-BaselineSnapshot {
    param($Snapshot)

    return $null -eq $Snapshot -or -not [bool]$Snapshot.present -or
        ($Snapshot.inf -ieq $script:BaselineInf -and
         $Snapshot.service -ieq $script:BaselineService -and
         $Snapshot.problem_code -eq 0)
}

function Get-CurrentSnapshot {
    $device = Get-TargetDevice
    if ($null -eq $device) { return $null }
    return Get-DeviceSnapshot -Device $device
}

function Wait-ForBaselineSnapshot {
    param(
        [int]$TimeoutSeconds = 15,
        [bool]$RequirePresent = $false
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $snapshot = Get-CurrentSnapshot
        if ((Test-BaselineSnapshot -Snapshot $snapshot) -and
            (-not $RequirePresent -or
             ($null -ne $snapshot -and [bool]$snapshot.present))) {
            return $snapshot
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    return Get-CurrentSnapshot
}

Assert-Administrator
if (-not $ConfirmResidentRollback) {
    throw 'Refusing resident rollback. Re-run with -ConfirmResidentRollback.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$stateRoot = Join-Path $projectRoot 'artifacts\v1-volume-sync\resident'
$statePath = Join-Path $stateRoot 'install-state.json'
if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
    throw "Resident install state is missing: $statePath"
}
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$beforeSnapshot = Get-CurrentSnapshot
$requirePresentBaseline = $null -ne $beforeSnapshot -and
    [bool]$beforeSnapshot.present
$activeObserverInf = $null
if ($null -ne $beforeSnapshot -and
    $beforeSnapshot.service -ieq 'NativeLdacAvrcpObserver') {
    $activeObserverInf = [string]$beforeSnapshot.inf
} elseif ($null -ne $beforeSnapshot -and [bool]$beforeSnapshot.present -and
          -not (Test-BaselineSnapshot -Snapshot $beforeSnapshot)) {
    throw "The exact XM5 AVRCP PDO has an unexpected active driver: $($beforeSnapshot.inf) / $($beforeSnapshot.service)"
}
$packagesBeforeRollback = @(Get-CandidatePackages)
$packagePlan = Get-ResidentRollbackPackagePlan `
    -State $state `
    -CandidatePackages $packagesBeforeRollback `
    -ActiveObserverInf $activeObserverInf
$knownInfs = @($packagePlan.known_packages)

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$logDirectory = Join-Path $stateRoot "rollback-$timestamp"
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null

$targetDescription = 'XM5 AVRCP 0x110E PDO resident observer binding'
$actionDescription = 'Remove the resident AVRCP observer package and restore Microsoft AVRCP on the exact PDO'
if (-not $PSCmdlet.ShouldProcess($targetDescription, $actionDescription)) {
    return
}

$steps = @()
foreach ($inactiveInf in @($packagePlan.inactive_packages)) {
    if ($inactiveInf -notin @($packagePlan.present_packages)) {
        $steps += [pscustomobject]@{
            action = 'skip-absent-known-resident-package'
            published_inf = $inactiveInf
        }
        continue
    }
    $inactiveDelete = Invoke-PnpUtil -Arguments @(
        '/delete-driver', $inactiveInf, '/force')
    $inactiveDelete.lines | Set-Content -LiteralPath `
        (Join-Path $logDirectory "delete-$inactiveInf.log") -Encoding utf8
    $steps += [pscustomobject]@{
        action = 'delete-inactive-known-resident-package'
        published_inf = $inactiveInf
        exit_code = $inactiveDelete.exit_code
    }
    Assert-PackageDeleteCompleted `
        -PublishedInf $inactiveInf `
        -Result $inactiveDelete `
        -Role 'inactive'
}

if ($null -ne $packagePlan.active_package) {
    $activeInf = [string]$packagePlan.active_package
    $activeDelete = Invoke-PnpUtil -Arguments @(
        '/delete-driver', $activeInf, '/uninstall', '/force')
    $activeDelete.lines | Set-Content -LiteralPath `
        (Join-Path $logDirectory "delete-active-$activeInf.log") -Encoding utf8
    $steps += [pscustomobject]@{
        action = 'delete-active-known-resident-package'
        published_inf = $activeInf
        exit_code = $activeDelete.exit_code
    }
    Assert-PackageDeleteCompleted `
        -PublishedInf $activeInf `
        -Result $activeDelete `
        -Role 'active'
}

$scan = Invoke-PnpUtil -Arguments @('/scan-devices')
$scan.lines | Set-Content -LiteralPath `
    (Join-Path $logDirectory 'scan.log') -Encoding utf8
$steps += [pscustomobject]@{
    action = 'scan-devices'
    exit_code = $scan.exit_code
}
if ($scan.exit_code -ne 0) {
    throw "pnputil scan failed with exit $($scan.exit_code)."
}

Start-Sleep -Seconds 2
$snapshot = Wait-ForBaselineSnapshot `
    -RequirePresent $requirePresentBaseline
if (-not (Test-BaselineSnapshot -Snapshot $snapshot) -and
    $null -ne $snapshot -and [bool]$snapshot.present) {
    $device = Get-TargetDevice
    $restart = Invoke-PnpUtil -Arguments @('/restart-device', $device.InstanceId)
    $restart.lines | Set-Content -LiteralPath `
        (Join-Path $logDirectory 'restart.log') -Encoding utf8
    $steps += [pscustomobject]@{
        action = 'restart-exact-avrcp-pdo'
        exit_code = $restart.exit_code
    }
    if ($restart.exit_code -ne 0) {
        throw "pnputil restart failed with exit $($restart.exit_code)."
    }
    $snapshot = Wait-ForBaselineSnapshot `
        -RequirePresent $requirePresentBaseline
}

$remaining = @(Get-CandidatePackages)
$remainingKnown = @($remaining | Where-Object {
    $_.published_inf -in $knownInfs
})
$remainingUnmanaged = @($remaining | Where-Object {
    $_.published_inf -notin $knownInfs
})
$snapshot = Get-CurrentSnapshot
$passed = $remainingKnown.Count -eq 0 -and
    $remainingUnmanaged.Count -eq 0 -and
    (Test-BaselineSnapshot -Snapshot $snapshot) -and
    (-not $requirePresentBaseline -or
     ($null -ne $snapshot -and [bool]$snapshot.present))

$rollbackState = [ordered]@{
    completed_at = (Get-Date).ToString('o')
    passed = $passed
    steps = $steps
    known_packages = $knownInfs
    remaining_known_packages = @($remainingKnown | ForEach-Object {
        [string]$_.published_inf
    })
    remaining_unmanaged_packages = @($remainingUnmanaged | ForEach-Object {
        [string]$_.published_inf
    })
    final_device = $snapshot
}
$rollbackPath = Join-Path $stateRoot 'rollback-state.json'
Write-ResidentJsonAtomically -Path $rollbackPath -Value $rollbackState

if (-not $passed) {
    throw "Resident rollback incomplete. State: $rollbackPath"
}
Write-Host 'Resident AVRCP observer rollback passed.'
Write-Host "Microsoft AVRCP restored on the exact XM5 PDO ($($script:BaselineInf) / $($script:BaselineService))."
Write-Host "State: $rollbackPath"
