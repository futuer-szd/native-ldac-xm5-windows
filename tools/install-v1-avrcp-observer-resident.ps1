# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [string]$CandidatePath,
    [switch]$ConfirmResidentInstall,
    [switch]$RepairInterruptedInstall
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
        throw 'Resident AVRCP observer install requires an elevated PowerShell 7 terminal.'
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
            version = [string]$_.Version
            provider = [string]$_.ProviderName
        }
    })
}

function Get-CandidatePackage {
    param([Parameter(Mandatory = $true)][string]$PublishedInf)

    $matches = @(Get-CandidatePackages | Where-Object {
        $_.published_inf -ieq $PublishedInf
    })
    if ($matches.Count -ne 1) {
        throw "Expected exactly one resident package record for $PublishedInf, found $($matches.Count)."
    }
    return $matches[0]
}

function Get-ExistingInstallState {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

function New-InstallState {
    param(
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)]$CurrentPackage,
        [Parameter(Mandatory = $true)][string]$CandidateInfHash,
        [Parameter(Mandatory = $true)][object[]]$PreviousPackages,
        [object[]]$UnmanagedPackages = @(),
        $Device,
        [Parameter(Mandatory = $true)][bool]$StagedOnly,
        [string]$CertificateThumbprint,
        [string]$TransactionState,
        $PreviousState
    )

    $currentInf = Assert-ResidentPublishedInfName `
        -PublishedInf ([string]$CurrentPackage.published_inf)
    $state = [ordered]@{
        state_version = 2
        transaction_state = $TransactionState
        completed_at = (Get-Date).ToString('o')
        source_commit = [string]$Manifest.source_commit
        published_inf = $currentInf
        current_package = [ordered]@{
            published_inf = $currentInf
            inf_sha256 = $CandidateInfHash
            version = [string]$CurrentPackage.version
            provider = [string]$CurrentPackage.provider
        }
        staged_only = $StagedOnly
        device = $Device
        previous_observer_packages = @($PreviousPackages | ForEach-Object {
            [ordered]@{
                published_inf = Assert-ResidentPublishedInfName `
                    -PublishedInf ([string]$_.published_inf)
                version = [string]$_.version
                provider = [string]$_.provider
            }
        })
        unmanaged_observer_packages = @($UnmanagedPackages | ForEach-Object {
            [ordered]@{
                published_inf = Assert-ResidentPublishedInfName `
                    -PublishedInf ([string]$_.published_inf)
                version = [string]$_.version
                provider = [string]$_.provider
            }
        })
        baseline = [ordered]@{
            inf = $script:BaselineInf
            service = $script:BaselineService
        }
        certificate_thumbprint = $CertificateThumbprint
    }
    if ($null -ne $PreviousState) {
        $state.recovered_from = [ordered]@{
            prior_state_version = [int](Get-ResidentStateProperty `
                -State $PreviousState -Name 'state_version')
            prior_published_inf = [string](Get-ResidentStateProperty `
                -State $PreviousState -Name 'published_inf')
            prior_source_commit = [string](Get-ResidentStateProperty `
                -State $PreviousState -Name 'source_commit')
        }
    }
    return $state
}

function Invoke-PnpUtil {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $lines = @(& pnputil.exe @Arguments 2>&1)
    [pscustomobject][ordered]@{
        exit_code = $LASTEXITCODE
        lines = @($lines)
    }
}

Assert-Administrator
if ($ConfirmResidentInstall -and $RepairInterruptedInstall) {
    throw 'Choose either -ConfirmResidentInstall or -RepairInterruptedInstall, not both.'
}
if (-not $ConfirmResidentInstall -and -not $RepairInterruptedInstall) {
    throw 'Refusing resident install. Re-run with -ConfirmResidentInstall or -RepairInterruptedInstall.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $projectRoot `
        'artifacts\v1-volume-sync\avrcp-observer-candidate'
}
$CandidatePath = [IO.Path]::GetFullPath($CandidatePath)
$verifyScript = Join-Path $PSScriptRoot 'verify-v1-avrcp-observer-candidate.ps1'
& pwsh.exe -NoProfile -ExecutionPolicy Bypass -File $verifyScript `
    -CandidatePath $CandidatePath
if ($LASTEXITCODE -ne 0) {
    throw "Resident candidate verification failed with exit $LASTEXITCODE."
}

$control = Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control'
if ([string]$control.SystemStartOptions -notmatch '(^|\s)TESTSIGNING(\s|$)') {
    throw 'The current boot is not in TESTSIGNING mode.'
}

$packageRoot = Join-Path $CandidatePath 'package'
$infPath = Join-Path $packageRoot 'NativeLdacAvrcpObserver.inf'
$certificatePath = Join-Path $packageRoot 'NativeLdacAvrcpObserver.cer'
if (-not (Test-Path -LiteralPath $infPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $certificatePath -PathType Leaf)) {
    throw 'The candidate package is incomplete.'
}
$candidateManifest = Get-Content -LiteralPath `
    (Join-Path $CandidatePath 'manifest.json') -Raw | ConvertFrom-Json
$candidateInfHash = Get-ResidentCandidateInfHash -CandidateInfPath $infPath

$stateRoot = Join-Path $projectRoot 'artifacts\v1-volume-sync\resident'
$statePath = Join-Path $stateRoot 'install-state.json'

if ($RepairInterruptedInstall) {
    $targetDescription = 'interrupted XM5 AVRCP resident observer install state'
    $actionDescription = 'Reconcile the already-bound observer package without changing PnP state'
    if (-not $PSCmdlet.ShouldProcess($targetDescription, $actionDescription)) {
        return
    }

    $device = Get-TargetDevice
    if ($null -eq $device -or -not [bool]$device.Present) {
        throw 'Interrupted install repair requires the exact XM5 AVRCP PDO to be present.'
    }
    $bound = Get-DeviceSnapshot -Device $device
    $publishedInf = Assert-ResidentPublishedInfName -PublishedInf $bound.inf
    if ($bound.service -ne 'NativeLdacAvrcpObserver' -or
        $bound.problem_code -ne 0) {
        throw 'Interrupted install repair requires a healthy NativeLdacAvrcpObserver binding.'
    }
    if (-not (Test-ResidentPublishedInfMatchesCandidate `
            -PublishedInf $publishedInf -CandidateInfPath $infPath)) {
        throw "The bound package $publishedInf does not match the verified candidate INF."
    }
    $previousState = Get-ExistingInstallState -Path $statePath
    if ($null -eq $previousState) {
        throw 'Interrupted install repair requires the previous install state; package history will not be inferred.'
    }
    $currentPackage = Get-CandidatePackage -PublishedInf $publishedInf
    $knownPreviousInfs = @(
        Get-ResidentCurrentPublishedInf -State $previousState
        Get-ResidentHistoricalPublishedInfs -State $previousState
    ) | Sort-Object -Unique
    $availablePackages = @(Get-CandidatePackages)
    $previousPackages = @($availablePackages | Where-Object {
        $_.published_inf -in $knownPreviousInfs -and
        $_.published_inf -ine $publishedInf
    })
    $unmanagedPackages = @($availablePackages | Where-Object {
        $_.published_inf -notin $knownPreviousInfs -and
        $_.published_inf -ine $publishedInf
    })
    $previousCertificateThumbprint = [string](Get-ResidentStateProperty `
        -State $previousState -Name 'certificate_thumbprint')
    $state = New-InstallState `
        -Manifest $candidateManifest `
        -CurrentPackage $currentPackage `
        -CandidateInfHash $candidateInfHash `
        -PreviousPackages $previousPackages `
        -UnmanagedPackages $unmanagedPackages `
        -Device $bound `
        -StagedOnly $false `
        -CertificateThumbprint $previousCertificateThumbprint `
        -TransactionState 'reconciled-interrupted-install' `
        -PreviousState $previousState
    Write-ResidentJsonAtomically -Path $statePath -Value $state

    Write-Host "Resident install state reconciled for $publishedInf; no PnP command was issued."
    Write-Host "State: $statePath"
    return
}

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$logDirectory = Join-Path $stateRoot "logs-$timestamp"
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null

$targetDescription = 'XM5 AVRCP 0x110E PDO resident observer binding'
$actionDescription = 'Install the AVRCP observer as the resident XM5 AVRCP driver with Microsoft AVRCP preserved as rollback baseline'
if (-not $PSCmdlet.ShouldProcess($targetDescription, $actionDescription)) {
    return
}

$rootCertificate = Import-Certificate -FilePath $certificatePath `
    -CertStoreLocation 'Cert:\LocalMachine\Root'
$publisherCertificate = Import-Certificate -FilePath $certificatePath `
    -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher'

$device = Get-TargetDevice
$packagesBeforeInstall = @(Get-CandidatePackages)
$installArguments = @('/add-driver', $infPath)
if ($null -ne $device -and [bool]$device.Present) {
    $installArguments += '/install'
}
$install = Invoke-PnpUtil -Arguments $installArguments
$install.lines | Set-Content -LiteralPath `
    (Join-Path $logDirectory 'install.log') -Encoding utf8
if ($install.exit_code -notin @(0, 259)) {
    throw "pnputil failed with exit $($install.exit_code)."
}

$publishedInf = Get-ResidentPublishedInfFromPnpUtilOutput -Lines $install.lines
if (-not (Test-ResidentPublishedInfMatchesCandidate `
        -PublishedInf $publishedInf -CandidateInfPath $infPath)) {
    throw "pnputil reported $publishedInf, but its Driver Store INF does not match the verified candidate."
}
$currentPackage = Get-CandidatePackage -PublishedInf $publishedInf
$previousPackages = @($packagesBeforeInstall | Where-Object {
    $_.published_inf -ine $publishedInf
})

$bound = $null
$boundHealthy = $false
if ($null -ne $device -and [bool]$device.Present) {
    $deadline = (Get-Date).AddSeconds(30)
    do {
        $current = Get-TargetDevice
        if ($null -ne $current) {
            $bound = Get-DeviceSnapshot -Device $current
            if ($bound.service -eq 'NativeLdacAvrcpObserver' -and
                $bound.inf -ieq $publishedInf -and
                $bound.problem_code -eq 0) {
                $boundHealthy = $true
                break
            }
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    if (-not $boundHealthy) {
        throw "The observer PDO did not reach a healthy resident binding (service=$($bound.service), problem=$($bound.problem_code)). Rollback is available."
    }
}

$state = New-InstallState `
    -Manifest $candidateManifest `
    -CurrentPackage $currentPackage `
    -CandidateInfHash $candidateInfHash `
    -PreviousPackages $previousPackages `
    -Device $bound `
    -StagedOnly (-not $boundHealthy) `
    -CertificateThumbprint $rootCertificate.Thumbprint `
    -TransactionState 'installed' `
    -PreviousState (Get-ExistingInstallState -Path $statePath)
Write-ResidentJsonAtomically -Path $statePath -Value $state

if ($boundHealthy) {
    Write-Host "Resident AVRCP observer bound as $publishedInf; service NativeLdacAvrcpObserver, problem code 0."
    Write-Host "The XM5 0x110E PDO is now owned by the resident observer. Microsoft AVRCP remains available as the rollback baseline."
} else {
    Write-Host "Resident AVRCP observer staged as $publishedInf (XM5 off or PDO absent)."
    Write-Host 'It will bind automatically when the XM5 AVRCP PDO next appears.'
}
Write-Host "State: $statePath"
