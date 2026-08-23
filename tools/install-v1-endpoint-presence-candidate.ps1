# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param([switch]$ConfirmV1EndpointInstall)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'native-ldac-baseline-common.ps1')

function Find-V1DevCon {
    $candidates = @(
        'C:\Program Files (x86)\Windows Kits\10\Tools\10.0.26100.0\x64\devcon.exe',
        'C:\Program Files (x86)\Windows Kits\10\Tools\x64\devcon.exe'
    )
    return $candidates | Where-Object {
        Test-Path -LiteralPath $_ -PathType Leaf
    } | Select-Object -First 1
}

Assert-LegacyAdministrator
if (-not $ConfirmV1EndpointInstall) {
    throw 'Refusing to install the V1 endpoint-presence candidate. Re-run with -ConfirmV1EndpointInstall.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
& (Join-Path $PSScriptRoot 'test-v1-endpoint-presence-readiness.ps1')
$candidateRoot = Join-Path $projectRoot `
    'artifacts\v1-endpoint-presence\candidate'
$manifest = Get-Content -LiteralPath `
    (Join-Path $candidateRoot 'manifest.json') -Raw | ConvertFrom-Json
$packageRoot = Join-Path $candidateRoot 'package'
$infPath = Join-Path $packageRoot 'NativeLdacAudio.inf'
$certificatePath = Join-Path $packageRoot 'NativeLdacAudio.cer'
$devconPath = Find-V1DevCon
if ([string]::IsNullOrWhiteSpace($devconPath)) {
    throw 'The x64 WDK devcon.exe was not found.'
}

$latestBackupPath = Join-Path $projectRoot `
    'artifacts\driver-test\latest-backup.txt'
$backupPath = (Get-Content -LiteralPath $latestBackupPath -Raw).Trim()
$transactionRoot = Join-Path $projectRoot `
    'artifacts\v1-endpoint-presence\install'
New-Item -ItemType Directory -Path $transactionRoot -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$transactionPath = Join-Path $transactionRoot "transaction-$stamp.json"
$installLog = Join-Path $transactionRoot "devcon-install-$stamp.log"
$transaction = [ordered]@{
    transaction_version = 1
    started_at = (Get-Date).ToString('o')
    completed_at = $null
    status = 'prepared'
    source_commit = [string]$manifest.source_commit
    phase = 'preflight'
    devcon_exit_code = $null
    device = $null
    presence_probe = @()
    link_probe = @()
    rollback_attempted = $false
    rollback_succeeded = $false
    error = $null
}
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath

$target = 'ROOT\NativeLdacAudio V1 physical-presence endpoint'
$action = 'Install one test-signed root audio endpoint while preserving the original A2DP driver and leaving it unplugged'
if (-not $PSCmdlet.ShouldProcess($target, $action)) {
    return
}

$mutationStarted = $false
try {
    $transaction.status = 'running'
    $transaction.phase = 'trust_certificate'
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    [void](Import-Certificate -FilePath $certificatePath `
        -CertStoreLocation 'Cert:\LocalMachine\Root')
    [void](Import-Certificate -FilePath $certificatePath `
        -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher')

    $transaction.phase = 'install_root_endpoint'
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    $mutationStarted = $true
    $installOutput = @(& $devconPath install $infPath `
        'ROOT\NativeLdacAudio' 2>&1)
    $installExit = $LASTEXITCODE
    $transaction.devcon_exit_code = $installExit
    $installOutput | Set-Content -LiteralPath $installLog -Encoding UTF8
    $installOutput | ForEach-Object { Write-Host $_ }
    if ($installExit -ne 0) {
        throw "DevCon install returned $installExit. This V1 gate does not accept a reboot-required or incomplete installation."
    }

    Start-Sleep -Seconds 3
    $transaction.phase = 'verify'
    $after = Get-NativeLdacBaselineSnapshot -BackupPath $backupPath
    $presentDevices = @($after.native_audio_devices |
        Where-Object { $_.present })
    if (-not $after.safe_original_a2dp -or
        $presentDevices.Count -ne 1 -or
        @($after.native_audio_packages).Count -ne 1) {
        throw 'The installed endpoint did not preserve the safe original-A2DP baseline or did not enumerate exactly once.'
    }
    $device = $presentDevices[0]
    if ([int]$device.problem_code -ne 0 -or
        [string]$device.service -ne 'NativeLdacAudio') {
        throw "The root endpoint is unhealthy: $($device.service)/problem $($device.problem_code)."
    }
    $transaction.device = $device

    $endpointProbe = Join-Path $candidateRoot 'audio_endpoint_probe.exe'
    $presenceOutput = @(& $endpointProbe --presence 2>&1)
    $presenceExit = $LASTEXITCODE
    $transaction.presence_probe = @($presenceOutput | ForEach-Object {
        [string]$_
    })
    if ($presenceExit -ne 0 -or
        ($presenceOutput -join "`n") -notmatch
            '(?m)^Physical presence absent:') {
        throw 'The new physical-presence ABI did not report absent after installation.'
    }
    $linkOutput = @(& $endpointProbe --link-state 2>&1)
    $linkExit = $LASTEXITCODE
    $transaction.link_probe = @($linkOutput | ForEach-Object {
        [string]$_
    })
    if ($linkExit -ne 0 -or
        ($linkOutput -join "`n") -notmatch '(?m)^Link disconnected:') {
        throw 'The independent media link was not disconnected after installation.'
    }

    $transaction.status = 'passed'
    $transaction.phase = 'complete'
} catch {
    $transaction.status = 'failed'
    $transaction.phase = 'rollback'
    $transaction.error = $_.Exception.Message
    if ($mutationStarted) {
        $transaction.rollback_attempted = $true
        try {
            & (Join-Path $PSScriptRoot `
                'cleanup-native-ldac-test-state.ps1') `
                -ConfirmNativeLdacCleanup `
                -BackupPath $backupPath `
                -Confirm:$false
            $rollback = Get-NativeLdacBaselineSnapshot `
                -BackupPath $backupPath
            $transaction.rollback_succeeded =
                [bool]$rollback.clean_original_a2dp
        } catch {
            $transaction.error += " Automatic rollback also failed: $($_.Exception.Message)"
        }
    }
} finally {
    $transaction.completed_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
}

if ($transaction.status -ne 'passed') {
    throw "V1 endpoint-presence installation failed: $($transaction.error) Transaction: $transactionPath"
}

Write-Host 'V1 endpoint-presence candidate installation passed.'
Write-Host 'The endpoint is installed but physically absent/unplugged.'
Write-Host 'The original AltA2DP binding remains healthy; media LinkState remains disconnected.'
Write-Host "Transaction: $transactionPath"
Write-Host 'No reboot, Bluetooth request, default-output change, or LDAC transport OPEN was performed.'
