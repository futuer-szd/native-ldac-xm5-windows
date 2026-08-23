# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$CheckpointName,
    [switch]$ConfirmV1GoldenCheckpoint
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-PowerShell7 {
    if ($PSVersionTable.PSEdition -ne 'Core' -or
        $PSVersionTable.PSVersion.Major -lt 7) {
        throw 'The V1 golden checkpoint requires PowerShell 7.'
    }
}

function Get-DevicePropertyText {
    param(
        [Parameter(Mandatory = $true)][string]$InstanceId,
        [Parameter(Mandatory = $true)][string]$KeyName
    )

    $property = Get-PnpDeviceProperty -InstanceId $InstanceId `
        -KeyName $KeyName -ErrorAction Stop
    return [string]$property.Data
}

function Get-DevicePropertyInt {
    param(
        [Parameter(Mandatory = $true)][string]$InstanceId,
        [Parameter(Mandatory = $true)][string]$KeyName
    )

    $property = Get-PnpDeviceProperty -InstanceId $InstanceId `
        -KeyName $KeyName -ErrorAction SilentlyContinue
    if ($null -eq $property) {
        return 0
    }
    return [int]$property.Data
}

function Get-DeviceSnapshot {
    param([Parameter(Mandatory = $true)]$Device)

    return [ordered]@{
        instance_id = [string]$Device.InstanceId
        friendly_name = [string]$Device.FriendlyName
        present = [bool]$Device.Present
        status = [string]$Device.Status
        service = Get-DevicePropertyText -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Service'
        published_inf = Get-DevicePropertyText `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverInfPath'
        driver_provider = Get-DevicePropertyText `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverProvider'
        driver_version = Get-DevicePropertyText `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverVersion'
        container_id = Get-DevicePropertyText `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_ContainerId'
        parent = Get-DevicePropertyText -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Parent'
        problem_code = Get-DevicePropertyInt -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_ProblemCode'
    }
}

function Export-DriverPackage {
    param(
        [Parameter(Mandatory = $true)][string]$Role,
        [Parameter(Mandatory = $true)]$DeviceSnapshot,
        [Parameter(Mandatory = $true)][string]$PackagesRoot,
        [Parameter(Mandatory = $true)][string]$CertificatesRoot
    )

    $packageRoot = Join-Path $PackagesRoot $Role
    New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
    $output = @(& pnputil.exe /export-driver `
        $DeviceSnapshot.published_inf $packageRoot 2>&1)
    $exitCode = $LASTEXITCODE
    $output | Set-Content -LiteralPath `
        (Join-Path $packageRoot 'pnputil-export.log') -Encoding utf8NoBOM
    if ($exitCode -ne 0) {
        throw "Failed to export $($DeviceSnapshot.published_inf) (exit $exitCode)."
    }

    $infs = @(Get-ChildItem -LiteralPath $packageRoot -Filter '*.inf' `
        -File -Recurse)
    if ($infs.Count -ne 1) {
        throw "The exported $Role package did not contain exactly one INF."
    }
    $files = @(Get-ChildItem -LiteralPath $packageRoot -File -Recurse |
        Where-Object { $_.Name -ne 'pnputil-export.log' } |
        Sort-Object FullName)
    if ($files.Count -lt 3) {
        throw "The exported $Role package is incomplete."
    }

    $entries = @()
    foreach ($file in $files) {
        $relative = [IO.Path]::GetRelativePath($packageRoot, $file.FullName)
        $entries += [ordered]@{
            path = $relative
            length = [long]$file.Length
            sha256 = (Get-FileHash -LiteralPath $file.FullName `
                -Algorithm SHA256).Hash
        }
    }

    $catalogs = @(Get-ChildItem -LiteralPath $packageRoot -Filter '*.cat' `
        -File -Recurse)
    $signatures = @()
    foreach ($catalog in $catalogs) {
        $signature = Get-AuthenticodeSignature -LiteralPath $catalog.FullName
        $certificate = $signature.SignerCertificate
        $certificatePath = $null
        if ($null -ne $certificate) {
            $certificatePath = Join-Path $CertificatesRoot `
                ($certificate.Thumbprint + '.cer')
            if (-not (Test-Path -LiteralPath $certificatePath -PathType Leaf)) {
                Export-Certificate -Cert $certificate -FilePath $certificatePath `
                    -Force | Out-Null
            }
        }
        $signatures += [ordered]@{
            catalog = [IO.Path]::GetRelativePath(
                $packageRoot, $catalog.FullName)
            status = [string]$signature.Status
            signer_thumbprint = if ($null -eq $certificate) {
                $null
            } else {
                [string]$certificate.Thumbprint
            }
            signer_subject = if ($null -eq $certificate) {
                $null
            } else {
                [string]$certificate.Subject
            }
            trusted_root_present = if ($null -eq $certificate) {
                $false
            } else {
                Test-Path -LiteralPath `
                    "Cert:\LocalMachine\Root\$($certificate.Thumbprint)"
            }
            trusted_publisher_present = if ($null -eq $certificate) {
                $false
            } else {
                Test-Path -LiteralPath `
                    "Cert:\LocalMachine\TrustedPublisher\$($certificate.Thumbprint)"
            }
            exported_certificate = if ($null -eq $certificatePath) {
                $null
            } else {
                [IO.Path]::GetRelativePath(
                    (Split-Path -Parent $PackagesRoot), $certificatePath)
            }
        }
    }

    return [ordered]@{
        role = $Role
        service = [string]$DeviceSnapshot.service
        published_inf_at_capture = [string]$DeviceSnapshot.published_inf
        original_inf = [string]$infs[0].Name
        driver_provider = [string]$DeviceSnapshot.driver_provider
        driver_version = [string]$DeviceSnapshot.driver_version
        package_path = [IO.Path]::GetRelativePath(
            (Split-Path -Parent $PackagesRoot), $packageRoot)
        files = @($entries)
        signatures = @($signatures)
    }
}

Assert-PowerShell7
if (-not $ConfirmV1GoldenCheckpoint) {
    throw 'Refusing to create a golden checkpoint without -ConfirmV1GoldenCheckpoint.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$sourceBranch = (& git.exe -C $projectRoot branch --show-current).Trim()
$sourceStatus = @(& git.exe -C $projectRoot status --porcelain `
    --untracked-files=all)
if ($LASTEXITCODE -ne 0 -or
    $sourceCommit -notmatch '^[0-9a-fA-F]{40}$' -or
    [string]::IsNullOrWhiteSpace($sourceBranch) -or
    $sourceStatus.Count -ne 0) {
    throw 'The V1 golden checkpoint requires a clean named Git branch.'
}

$a2dpPrefix = 'BTHENUM\{0000110B-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
$a2dpDevices = @(Get-PnpDevice -PresentOnly | Where-Object {
    $_.InstanceId.StartsWith($a2dpPrefix,
        [StringComparison]::OrdinalIgnoreCase)
})
if ($a2dpDevices.Count -ne 1) {
    throw 'Exactly one present XM5 A2DP Sink service PDO is required.'
}
$a2dp = Get-DeviceSnapshot -Device $a2dpDevices[0]
if ($a2dp.service -ne 'LdacNative' -or
    $a2dp.problem_code -ne 0) {
    throw 'The current XM5 LdacNative binding is not healthy.'
}

$rootDevices = @(Get-PnpDevice -Class MEDIA -ErrorAction SilentlyContinue |
    Where-Object {
        $property = Get-PnpDeviceProperty -InstanceId $_.InstanceId `
            -KeyName 'DEVPKEY_Device_HardwareIds' `
            -ErrorAction SilentlyContinue
        $null -ne $property -and
            'ROOT\NativeLdacAudio' -in @($property.Data)
    })
if ($rootDevices.Count -ne 1) {
    throw 'Exactly one NativeLdacAudio root device is required.'
}
$endpoint = Get-DeviceSnapshot -Device $rootDevices[0]
if ($endpoint.service -ne 'NativeLdacAudio' -or
    $endpoint.problem_code -ne 0) {
    throw 'The current NativeLdacAudio endpoint binding is not healthy.'
}

$altA2dpBackupPointer = Join-Path $projectRoot `
    'artifacts\driver-test\latest-backup.txt'
if (-not (Test-Path -LiteralPath $altA2dpBackupPointer -PathType Leaf)) {
    throw 'The original Alternative A2DP backup pointer is missing.'
}
$altA2dpBackup = (Get-Content -LiteralPath $altA2dpBackupPointer `
    -Raw).Trim()
$altA2dpStatePath = Join-Path $altA2dpBackup 'state.json'
if (-not (Test-Path -LiteralPath $altA2dpStatePath -PathType Leaf)) {
    throw 'The original Alternative A2DP backup state is missing.'
}
$altA2dpState = Get-Content -LiteralPath $altA2dpStatePath -Raw |
    ConvertFrom-Json
if ([string]$altA2dpState.service -ne 'AltA2DP') {
    throw 'The original Alternative A2DP backup is not valid.'
}

if ([string]::IsNullOrWhiteSpace($CheckpointName)) {
    $CheckpointName = 'v1-golden-' + (Get-Date -Format 'yyyyMMdd-HHmmss') +
        '-' + $sourceCommit.Substring(0, 8)
}
if ($CheckpointName -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{2,79}$') {
    throw 'CheckpointName contains unsupported characters.'
}
$goldenRoot = Join-Path $projectRoot 'artifacts\v1-golden'
$checkpointRoot = Join-Path $goldenRoot $CheckpointName
if (Test-Path -LiteralPath $checkpointRoot) {
    throw "The checkpoint already exists: $checkpointRoot"
}
$packagesRoot = Join-Path $checkpointRoot 'packages'
$certificatesRoot = Join-Path $checkpointRoot 'certificates'
$sourceRoot = Join-Path $checkpointRoot 'source'
$stateRoot = Join-Path $checkpointRoot 'state'
foreach ($directory in @($packagesRoot, $certificatesRoot,
        $sourceRoot, $stateRoot)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

try {
    $packages = @()
    $packages += Export-DriverPackage -Role 'transport-active' `
        -DeviceSnapshot $a2dp -PackagesRoot $packagesRoot `
        -CertificatesRoot $certificatesRoot
    $packages += Export-DriverPackage -Role 'endpoint-active' `
        -DeviceSnapshot $endpoint -PackagesRoot $packagesRoot `
        -CertificatesRoot $certificatesRoot

    $altPackageRoot = Join-Path $packagesRoot 'original-alta2dp'
    Copy-Item -LiteralPath $altA2dpBackup -Destination $altPackageRoot `
        -Recurse -Force
    $altFiles = @(Get-ChildItem -LiteralPath $altPackageRoot -File `
        -Recurse | Sort-Object FullName)
    $altEntries = @()
    foreach ($file in $altFiles) {
        $altEntries += [ordered]@{
            path = [IO.Path]::GetRelativePath(
                $altPackageRoot, $file.FullName)
            length = [long]$file.Length
            sha256 = (Get-FileHash -LiteralPath $file.FullName `
                -Algorithm SHA256).Hash
        }
    }
    $packages += [ordered]@{
        role = 'original-alta2dp'
        service = [string]$altA2dpState.service
        published_inf_at_capture = [string]$altA2dpState.published_inf
        original_inf = 'alta2dp.inf'
        driver_provider = [string]$altA2dpState.provider
        driver_version = [string]$altA2dpState.driver_version
        package_path = [IO.Path]::GetRelativePath(
            $checkpointRoot, $altPackageRoot)
        files = @($altEntries)
        signatures = @()
    }

    $bundlePath = Join-Path $sourceRoot 'repository.bundle'
    & git.exe -C $projectRoot bundle create $bundlePath --all
    if ($LASTEXITCODE -ne 0) {
        throw 'Git bundle creation failed.'
    }
    & git.exe bundle verify $bundlePath | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw 'Git bundle verification failed.'
    }
    $archivePath = Join-Path $sourceRoot 'working-tree.zip'
    & git.exe -C $projectRoot archive --format=zip `
        --output=$archivePath HEAD
    if ($LASTEXITCODE -ne 0) {
        throw 'Git working-tree archive creation failed.'
    }

    $candidateRoot = Join-Path $projectRoot `
        'artifacts\v1-normal-stop\candidate'
    $candidateManifestPath = Join-Path $candidateRoot 'manifest.json'
    if (-not (Test-Path -LiteralPath $candidateManifestPath `
            -PathType Leaf)) {
        throw 'The verified normal-stop candidate manifest is missing.'
    }
    $candidateManifest = Get-Content -LiteralPath `
        $candidateManifestPath -Raw | ConvertFrom-Json
    if ([string]$candidateManifest.source_commit -cne $sourceCommit -or
        $candidateManifest.source_dirty -ne $false -or
        [double]$candidateManifest.sample_peak_ceiling -ne 1.0) {
        throw 'The verified normal-stop candidate does not match the clean checkpoint source.'
    }
    Copy-Item -LiteralPath $candidateManifestPath -Destination `
        (Join-Path $stateRoot 'normal-stop-candidate-manifest.json')

    $volumeProbe = Join-Path $candidateRoot 'endpoint_volume_probe.exe'
    $volumeText = @(& $volumeProbe --info --all 2>&1)
    $volumeExit = $LASTEXITCODE
    $volumeText | Set-Content -LiteralPath `
        (Join-Path $stateRoot 'endpoint-volume.txt') `
        -Encoding utf8NoBOM
    if ($volumeExit -ne 0 -or
        ($volumeText -join "`n") -notmatch 'Native LDAC') {
        throw 'The Native LDAC endpoint volume snapshot failed.'
    }

    $control = Get-ItemProperty `
        -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control'
    $manifest = [ordered]@{
        manifest_version = 1
        checkpoint_name = $CheckpointName
        captured_at = (Get-Date).ToString('o')
        restore_contract = 'functional-equivalence-with-exact-driver-package-hashes'
        source = [ordered]@{
            branch = $sourceBranch
            commit = $sourceCommit
            dirty = $false
            bundle = 'source/repository.bundle'
            bundle_sha256 = (Get-FileHash -LiteralPath $bundlePath `
                -Algorithm SHA256).Hash
            archive = 'source/working-tree.zip'
            archive_sha256 = (Get-FileHash -LiteralPath $archivePath `
                -Algorithm SHA256).Hash
        }
        system = [ordered]@{
            computer_name = [Environment]::MachineName
            windows_build = [Environment]::OSVersion.Version.ToString()
            powershell = $PSVersionTable.PSVersion.ToString()
            test_signing_active = [string]$control.SystemStartOptions `
                -match '(^|\s)TESTSIGNING(\s|$)'
        }
        devices = [ordered]@{
            transport = $a2dp
            endpoint = $endpoint
        }
        packages = @($packages)
        audio_policy = [ordered]@{
            steady_state_gain = 'unity'
            sample_peak_ceiling = 1.0
            startup_encoded_silence_ms = 20.0
            fade_in_ms = 100.0
            post_start_pcm_rebind = $true
        }
        runtime_state = [ordered]@{
            endpoint_volume = 'state/endpoint-volume.txt'
            normal_stop_candidate = `
                'state/normal-stop-candidate-manifest.json'
            transport_runtime_probe_required_after_restore = $true
            endpoint_runtime_probe_required_after_restore = $true
        }
    }
    $manifestPath = Join-Path $checkpointRoot 'manifest.json'
    $manifest | ConvertTo-Json -Depth 10 | Set-Content `
        -LiteralPath $manifestPath -Encoding utf8NoBOM

    $manifestHash = (Get-FileHash -LiteralPath $manifestPath `
        -Algorithm SHA256).Hash
    Set-Content -LiteralPath (Join-Path $checkpointRoot `
        'manifest.sha256') -Value ($manifestHash + '  manifest.json') `
        -Encoding ascii
    New-Item -ItemType Directory -Path $goldenRoot -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $goldenRoot 'latest.txt') `
        -Value $checkpointRoot -Encoding utf8NoBOM
} catch {
    if (Test-Path -LiteralPath $checkpointRoot -PathType Container) {
        Remove-Item -LiteralPath $checkpointRoot -Recurse -Force
    }
    throw
}

Write-Host 'V1 golden checkpoint captured successfully.'
Write-Host "Checkpoint: $checkpointRoot"
Write-Host "Source: $sourceCommit"
Write-Host "Transport: $($a2dp.service)/$($a2dp.published_inf)"
Write-Host "Endpoint: $($endpoint.service)/$($endpoint.published_inf)"
Write-Host 'No driver, PnP device, Bluetooth radio, endpoint setting, or audio path was modified.'
