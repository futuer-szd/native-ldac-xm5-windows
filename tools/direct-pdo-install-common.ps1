# SPDX-License-Identifier: Apache-2.0

Set-StrictMode -Version Latest

$script:Xm5A2dpInstancePrefix =
    'BTHENUM\{0000110B-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'

function Assert-DirectPdoHardwareTestsEnabled {
    param([Parameter(Mandatory = $true)][string]$ProjectRoot)

    $markerPath = Join-Path $ProjectRoot `
        'direct-pdo\HARDWARE_TESTS_SUSPENDED.md'
    if (Test-Path -LiteralPath $markerPath -PathType Leaf) {
        throw "Direct-PDO hardware installation and trials are suspended by $markerPath. There is no command-line override; remove the marker only in a reviewed source commit after the documented offline gates pass."
    }
}

function Test-DirectPdoRuntimeStatusText {
    param(
        [Parameter(Mandatory = $true)][string]$StatusText,
        [Parameter(Mandatory = $true)][int]$ExitCode
    )

    if ($ExitCode -ne 0) {
        return $false
    }
    return $StatusText -match '(?m)^PCM ABI 2:' -and
        $StatusText -match `
            '(?m)^Direct-PDO Media ABI 3: (idle|open|streaming)\b' -and
        $StatusText -match '(?m)^Failure: none \(0\),' -and
        $StatusText -match '(?m)^Backend activity: idle\.\r?$'
}

function Test-DirectPdoRecoverableMediaTimeoutStatusText {
    param(
        [Parameter(Mandatory = $true)][string]$StatusText,
        [Parameter(Mandatory = $true)][int]$ExitCode
    )

    if ($ExitCode -ne 0) {
        return $false
    }
    return $StatusText -match '(?m)^PCM ABI 2:' -and
        $StatusText -match `
            '(?m)^Direct-PDO Media ABI 3: faulted \(5\)' -and
        $StatusText -match `
            '(?m)^Failure: media-timeout \(2\),' -and
        $StatusText -match '(?m)^Backend activity: idle\.\r?$'
}

function Get-DirectPdoArtifactKind {
    param([Parameter(Mandatory = $true)]$Manifest)

    if ($null -eq $Manifest.PSObject.Properties['installable']) {
        return 'invalid'
    }
    if ($Manifest.installable -eq $false) {
        return 'validation-bundle'
    }
    if ($Manifest.installable -eq $true -and
        $null -ne $Manifest.PSObject.Properties['staged_only'] -and
        $Manifest.staged_only -eq $true -and
        $null -ne $Manifest.PSObject.Properties['service_name'] -and
        [string]$Manifest.service_name -eq 'NativeLdacDirectPdo') {
        return 'candidate'
    }
    return 'invalid'
}

function Assert-DirectPdoAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script from an elevated Windows PowerShell.'
    }
}

function Write-DirectPdoJsonAtomic {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Path,
        [int]$Depth = 8
    )

    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $temporaryPath = "$Path.tmp.$PID"
    try {
        $Value | ConvertTo-Json -Depth $Depth |
            Set-Content -LiteralPath $temporaryPath -Encoding UTF8
        Move-Item -LiteralPath $temporaryPath -Destination $Path -Force
    } finally {
        if (Test-Path -LiteralPath $temporaryPath -PathType Leaf) {
            Remove-Item -LiteralPath $temporaryPath -Force
        }
    }
}

function Write-DirectPdoTextAtomic {
    param(
        [Parameter(Mandatory = $true)][string]$Value,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $temporaryPath = "$Path.tmp.$PID"
    try {
        Set-Content -LiteralPath $temporaryPath -Value $Value -Encoding UTF8
        Move-Item -LiteralPath $temporaryPath -Destination $Path -Force
    } finally {
        if (Test-Path -LiteralPath $temporaryPath -PathType Leaf) {
            Remove-Item -LiteralPath $temporaryPath -Force
        }
    }
}

function Get-Xm5A2dpDevice {
    $devices = @(Get-PnpDevice -PresentOnly | Where-Object {
        ([string]$_.InstanceId).StartsWith(
            $script:Xm5A2dpInstancePrefix,
            [StringComparison]::OrdinalIgnoreCase)
    })
    return $devices
}

function Get-Xm5A2dpSnapshot {
    param([Parameter(Mandatory = $true)]$Device)

    $service = [string](Get-PnpDeviceProperty `
        -InstanceId $Device.InstanceId `
        -KeyName 'DEVPKEY_Device_Service').Data
    $publishedInf = [string](Get-PnpDeviceProperty `
        -InstanceId $Device.InstanceId `
        -KeyName 'DEVPKEY_Device_DriverInfPath').Data
    $problemProperty = Get-PnpDeviceProperty `
        -InstanceId $Device.InstanceId `
        -KeyName 'DEVPKEY_Device_ProblemCode' `
        -ErrorAction SilentlyContinue
    $problemCode = if ($null -eq $problemProperty) {
        0
    } else {
        [int]$problemProperty.Data
    }
    return [pscustomobject][ordered]@{
        instance_id = [string]$Device.InstanceId
        friendly_name = [string]$Device.FriendlyName
        service = $service
        published_inf = $publishedInf
        problem_code = $problemCode
    }
}

function Wait-Xm5A2dpService {
    param(
        [Parameter(Mandatory = $true)][string]$ExpectedService,
        [int]$TimeoutSeconds = 30
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $devices = @(Get-Xm5A2dpDevice)
        if ($devices.Count -eq 1) {
            $snapshot = Get-Xm5A2dpSnapshot -Device $devices[0]
            if ($snapshot.service -eq $ExpectedService) {
                return $snapshot
            }
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    return $null
}

function Wait-Xm5A2dpPackageTransition {
    param(
        [Parameter(Mandatory = $true)][string]$ExpectedService,
        [string]$PreviousPublishedInf,
        [int]$TimeoutSeconds = 30
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $devices = @(Get-Xm5A2dpDevice)
        if ($devices.Count -eq 1) {
            $snapshot = Get-Xm5A2dpSnapshot -Device $devices[0]
            $serviceMatches = $snapshot.service -eq $ExpectedService
            $packageChanged = [string]::IsNullOrWhiteSpace(
                $PreviousPublishedInf) -or
                -not $snapshot.published_inf.Equals(
                    $PreviousPublishedInf,
                    [StringComparison]::OrdinalIgnoreCase)
            if ($serviceMatches -and $packageChanged) {
                return $snapshot
            }
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    return $null
}

function Get-DirectPdoRollbackTarget {
    param([Parameter(Mandatory = $true)]$Transaction)

    $property = $Transaction.PSObject.Properties['rollback_target']
    if ($null -ne $property -and $null -ne $property.Value) {
        return $property.Value
    }
    return $Transaction.previous
}

function Read-DirectPdoTransaction {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Direct-PDO transaction file was not found: $fullPath"
    }
    $transaction = Get-Content -LiteralPath $fullPath -Raw |
        ConvertFrom-Json
    if ([int]$transaction.transaction_version -notin @(1, 2) -or
        [string]$transaction.target.hardware_id -ne
            $script:Xm5A2dpInstancePrefix) {
        throw "Direct-PDO transaction identity is invalid: $fullPath"
    }
    return [pscustomobject]@{
        path = $fullPath
        transaction = $transaction
        rollback_target = Get-DirectPdoRollbackTarget `
            -Transaction $transaction
    }
}

function Invoke-DirectPdoPnpUtil {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [string]$LogPath,
        [int[]]$AcceptedExitCodes = @(0, 3010)
    )

    $output = @()
    $exitCode = -1
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = @(& pnputil.exe @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if (-not [string]::IsNullOrWhiteSpace($LogPath)) {
        $parent = Split-Path -Parent $LogPath
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
        $output | Set-Content -LiteralPath $LogPath -Encoding UTF8
    }
    $output | ForEach-Object { Write-Host $_ }
    if ($exitCode -notin $AcceptedExitCodes) {
        throw "pnputil failed with exit code ${exitCode}: $($Arguments -join ' ')"
    }
    return [pscustomobject]@{
        exit_code = $exitCode
        reboot_required = $exitCode -eq 3010
        output = $output
    }
}

function Get-DriverPackagesByOriginalInf {
    param([Parameter(Mandatory = $true)][string[]]$InfNames)

    $drivers = @(Get-WindowsDriver -Online -All)
    return @($drivers | Where-Object {
        $leaf = Split-Path -Leaf ([string]$_.OriginalFileName)
        $leaf -in $InfNames
    })
}

function Remove-DriverPackagesByOriginalInf {
    param(
        [Parameter(Mandatory = $true)][string[]]$InfNames,
        [Parameter(Mandatory = $true)][string]$LogDirectory
    )

    $removed = @()
    $packages = @(Get-DriverPackagesByOriginalInf -InfNames $InfNames)
    foreach ($package in $packages) {
        $publishedInf = [string]$package.Driver
        if ([string]::IsNullOrWhiteSpace($publishedInf)) {
            continue
        }
        $safeName = $publishedInf -replace '[^A-Za-z0-9_.-]', '_'
        $logPath = Join-Path $LogDirectory "remove-$safeName.log"
        $null = Invoke-DirectPdoPnpUtil `
            -Arguments @('/delete-driver', $publishedInf, '/uninstall', '/force') `
            -LogPath $logPath
        $removed += $publishedInf
    }
    return @($removed)
}

function Restore-OriginalA2dpBackup {
    param(
        [Parameter(Mandatory = $true)][string]$BackupPath,
        [Parameter(Mandatory = $true)][string]$LogDirectory
    )

    $backupPathFull = [System.IO.Path]::GetFullPath($BackupPath)
    $statePath = Join-Path $backupPathFull 'state.json'
    if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
        throw "Rollback backup state is missing: $statePath"
    }
    $backupState = Get-Content -LiteralPath $statePath -Raw |
        ConvertFrom-Json
    if ([string]$backupState.service -in @(
            'LdacNative', 'NativeLdacDirectPdo')) {
        throw "Rollback backup is not an original A2DP driver: $backupPathFull"
    }
    $infs = @(Get-ChildItem -LiteralPath $backupPathFull -Recurse `
        -Filter '*.inf' -File)
    if ($infs.Count -eq 0) {
        throw "Rollback backup contains no INF: $backupPathFull"
    }
    foreach ($inf in $infs) {
        $safeName = $inf.Name -replace '[^A-Za-z0-9_.-]', '_'
        $logPath = Join-Path $LogDirectory "restore-$safeName.log"
        $restoreResult = Invoke-DirectPdoPnpUtil `
            -Arguments @('/add-driver', $inf.FullName, '/install') `
            -LogPath $logPath `
            -AcceptedExitCodes @(0, 259, 3010)
        if ($restoreResult.exit_code -eq 259) {
            $stagedPackages = @(Get-DriverPackagesByOriginalInf `
                -InfNames @($inf.Name))
            if ($stagedPackages.Count -eq 0) {
                throw "PnPUtil returned 259, but $($inf.Name) is not present in the Driver Store."
            }
        }
    }
    $null = Invoke-DirectPdoPnpUtil `
        -Arguments @('/scan-devices') `
        -LogPath (Join-Path $LogDirectory 'scan-devices.log')
    return $backupState
}

function Import-DirectPdoCertificate {
    param([Parameter(Mandatory = $true)][string]$CertificatePath)

    $certificate = Get-PfxCertificate -FilePath $CertificatePath
    $thumbprint = $certificate.Thumbprint
    $rootPath = "Cert:\LocalMachine\Root\$thumbprint"
    $publisherPath = "Cert:\LocalMachine\TrustedPublisher\$thumbprint"
    $rootAlreadyPresent = Test-Path -LiteralPath $rootPath
    $publisherAlreadyPresent = Test-Path -LiteralPath $publisherPath
    if (-not $rootAlreadyPresent) {
        $null = Import-Certificate -FilePath $CertificatePath `
            -CertStoreLocation 'Cert:\LocalMachine\Root'
    }
    if (-not $publisherAlreadyPresent) {
        $null = Import-Certificate -FilePath $CertificatePath `
            -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher'
    }
    return [pscustomobject][ordered]@{
        thumbprint = $thumbprint
        root_already_present = $rootAlreadyPresent
        publisher_already_present = $publisherAlreadyPresent
    }
}
