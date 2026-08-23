# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmNativeLdacCleanup,
    [string]$BackupPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'native-ldac-baseline-common.ps1')

function Find-NativeLdacDevCon {
    $candidates = @(
        'C:\Program Files (x86)\Windows Kits\10\Tools\10.0.26100.0\x64\devcon.exe',
        'C:\Program Files (x86)\Windows Kits\10\Tools\x64\devcon.exe'
    )
    return $candidates | Where-Object {
        Test-Path -LiteralPath $_ -PathType Leaf
    } | Select-Object -First 1
}

Assert-LegacyAdministrator
if (-not $ConfirmNativeLdacCleanup) {
    throw 'Refusing to remove Native LDAC test endpoints and packages. Re-run with -ConfirmNativeLdacCleanup.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($BackupPath)) {
    $latestBackupPath = Join-Path $projectRoot `
        'artifacts\driver-test\latest-backup.txt'
    if (-not (Test-Path -LiteralPath $latestBackupPath -PathType Leaf)) {
        throw 'Original A2DP latest-backup.txt is missing.'
    }
    $BackupPath = (Get-Content -LiteralPath $latestBackupPath -Raw).Trim()
}
$BackupPath = [System.IO.Path]::GetFullPath($BackupPath)

$before = Get-NativeLdacBaselineSnapshot -BackupPath $BackupPath
if (-not $before.safe_original_a2dp) {
    throw 'The original A2DP baseline is not safe. Cleanup will not modify the system.'
}
if ($before.clean_original_a2dp) {
    Write-Host 'The system already has a clean original-A2DP baseline.'
    Write-NativeLdacBaselineSummary -Snapshot $before
    return
}

$connectionProbePath = Join-Path $projectRoot `
    'artifacts\diagnostics\xm5_connection_probe.exe'
$xm5State = Get-NativeLdacXm5BluetoothState -ProbePath $connectionProbePath
if ($xm5State -ne 'disconnected') {
    throw 'Turn off the XM5 and wait for its physical Bluetooth state to become disconnected before cleanup.'
}

$devconPath = Find-NativeLdacDevCon
if ([string]::IsNullOrWhiteSpace($devconPath)) {
    throw 'The x64 WDK devcon.exe was not found.'
}

$transactionRoot = Join-Path $projectRoot 'artifacts\maintenance'
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$transactionPath = Join-Path $transactionRoot `
    "native-audio-cleanup-$timestamp.json"
$logDirectory = Join-Path $transactionRoot `
    "native-audio-cleanup-$timestamp"
$transaction = [ordered]@{
    transaction_version = 1
    started_at = (Get-Date).ToString('o')
    completed_at = $null
    status = 'prepared'
    phase = 'preflight'
    backup_path = $BackupPath
    xm5_bluetooth_state = $xm5State
    before = $before
    removed_devices = @()
    removed_packages = @()
    reboot_required = $false
    after = $null
    error = $null
}
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath

$target = 'Native LDAC ROOT audio endpoint and NativeLdacAudio test packages only'
$action = 'Remove test audio endpoint state while preserving AltA2DP, Bluetooth pairing, the rollback backup, and the shared test certificate'
if (-not $PSCmdlet.ShouldProcess($target, $action)) {
    return
}

try {
    $transaction.status = 'running'
    $transaction.phase = 'remove_root_devices'
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    $presentRootDevices = @($before.native_audio_devices |
        Where-Object { $_.present })
    foreach ($device in $presentRootDevices) {
        $logPath = Join-Path $logDirectory `
            ("remove-device-" +
                ([string]$device.instance_id -replace '[^A-Za-z0-9.-]', '_') +
                '.log')
        New-Item -ItemType Directory -Path $logDirectory -Force |
            Out-Null
        $output = @(& $devconPath remove "@$($device.instance_id)" 2>&1)
        $exitCode = $LASTEXITCODE
        $output | Set-Content -LiteralPath $logPath -Encoding UTF8
        $output | ForEach-Object { Write-Host $_ }
        if ($exitCode -notin @(0, 1)) {
            throw "DevCon could not remove $($device.instance_id) (exit $exitCode)."
        }
        if ($exitCode -eq 1) {
            $transaction.reboot_required = $true
        }
        $transaction.removed_devices += [ordered]@{
            instance_id = [string]$device.instance_id
            published_inf = [string]$device.published_inf
            devcon_exit_code = $exitCode
            log = $logPath
        }
        Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    }

    $transaction.phase = 'remove_driver_packages'
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    $packages = @(Get-LegacyDriverPackages `
        -OriginalInfNames @('NativeLdacAudio.inf'))
    foreach ($package in $packages) {
        $publishedInf = [string]$package.Driver
        if ([string]::IsNullOrWhiteSpace($publishedInf)) {
            continue
        }
        $result = Invoke-LegacyPnpUtil -Arguments @(
                '/delete-driver',
                $publishedInf,
                '/uninstall',
                '/force') `
            -LogPath (Join-Path $logDirectory `
                "remove-package-$publishedInf.log") `
            -AcceptedExitCodes @(0, 3010)
        if ($result.reboot_required) {
            $transaction.reboot_required = $true
        }
        $transaction.removed_packages += $publishedInf
        Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    }

    $transaction.phase = 'verify'
    $after = Get-NativeLdacBaselineSnapshot -BackupPath $BackupPath
    $transaction.after = $after
    if (-not $after.safe_original_a2dp) {
        throw 'Cleanup changed or could not verify the original A2DP baseline.'
    }
    $remainingPackages = @($after.native_audio_packages)
    $remainingPresentDevices = @($after.native_audio_devices |
        Where-Object { $_.present })
    if ($remainingPackages.Count -ne 0) {
        throw 'One or more NativeLdacAudio packages remain in the Driver Store.'
    }
    if ($remainingPresentDevices.Count -ne 0 -and
        -not $transaction.reboot_required) {
        throw 'A Native LDAC root endpoint remains present without a reported reboot requirement.'
    }
    $transaction.status = if ($after.clean_original_a2dp) {
        'passed'
    } else {
        'pending_reboot'
    }
    $transaction.phase = 'complete'
} catch {
    $transaction.status = 'failed'
    $transaction.phase = 'failed'
    $transaction.error = $_.Exception.Message
} finally {
    $transaction.completed_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
}

if ($transaction.status -eq 'failed') {
    throw "Native LDAC cleanup failed: $($transaction.error) Transaction: $transactionPath"
}

Write-Host 'Native LDAC test endpoint cleanup completed without changing the original A2DP driver.'
Write-Host 'Bluetooth pairing, the original AltA2DP package, the rollback backup, and the shared test certificate were retained.'
Write-Host "Transaction: $transactionPath"
if ($transaction.status -eq 'pending_reboot') {
    Write-Host 'A reboot is required before the clean baseline can be verified.'
} else {
    Write-Host 'The clean original-A2DP baseline is already verified.'
}
