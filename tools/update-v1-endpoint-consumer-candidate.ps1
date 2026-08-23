# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param([switch]$ConfirmV1EndpointUpdate)

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

function Assert-V1CandidateBundle(
    [string]$Root,
    [switch]$RequireConsumerLease) {
    $manifestPath = Join-Path $Root 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Candidate manifest is missing: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw |
        ConvertFrom-Json
    $capabilities = @($manifest.capabilities | ForEach-Object {
        [string]$_
    })
    if ([int]$manifest.manifest_version -ne 1 -or
        $manifest.source_dirty -ne $false -or
        [string]$manifest.hardware_id -ne 'ROOT\NativeLdacAudio' -or
        [int]$manifest.presence_abi -ne 1 -or
        'physical_presence_separate_from_media_link' -notin $capabilities -or
        'no_transport_open' -notin $capabilities) {
        throw "Candidate manifest contract is invalid: $manifestPath"
    }
    if ($RequireConsumerLease -and
        ($null -eq $manifest.PSObject.Properties[
            'pcm_consumer_lease_abi'] -or
         [int]$manifest.pcm_consumer_lease_abi -ne 1 -or
         'pcm_consumer_lease_separate_from_media_link' -notin
            $capabilities)) {
        throw "Candidate consumer-lease contract is invalid: $manifestPath"
    }
    foreach ($file in @($manifest.files)) {
        $path = Join-Path $Root ([string]$file.path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-Item -LiteralPath $path).Length -ne [long]$file.length -or
            -not (Get-FileHash -LiteralPath $path `
                -Algorithm SHA256).Hash.Equals(
                    [string]$file.sha256,
                    [StringComparison]::OrdinalIgnoreCase)) {
            throw "Candidate file failed its hash check: $path"
        }
    }
    return $manifest
}

Assert-LegacyAdministrator
if (-not $ConfirmV1EndpointUpdate) {
    throw 'Refusing to update the V1 endpoint candidate. Re-run with -ConfirmV1EndpointUpdate.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$candidateRoot = Join-Path $projectRoot `
    'artifacts\v1-endpoint-presence\candidate'
$manifest = Assert-V1CandidateBundle -Root $candidateRoot `
    -RequireConsumerLease
$latestBackupPath = Join-Path $projectRoot `
    'artifacts\driver-test\latest-backup.txt'
$backupPath = (Get-Content -LiteralPath $latestBackupPath -Raw).Trim()
$before = Get-NativeLdacBaselineSnapshot -BackupPath $backupPath
$presentDevices = @($before.native_audio_devices | Where-Object {
    $_.present
})
if (-not $before.safe_original_a2dp -or
    $presentDevices.Count -ne 1 -or
    [string]$presentDevices[0].service -ne 'NativeLdacAudio' -or
    [int]$presentDevices[0].problem_code -ne 0 -or
    @($before.native_audio_packages).Count -lt 1 -or
    @($before.workspace_processes).Count -ne 0 -or
    @($before.scheduled_tasks).Count -ne 0) {
    throw 'The installed V1 endpoint is not a safe, idle, single-device update target.'
}

$connectionProbe = Join-Path $candidateRoot 'xm5_connection_probe.exe'
$xm5State = Get-NativeLdacXm5BluetoothState `
    -ProbePath $connectionProbe `
    -ExpectedSourceCommit ([string]$manifest.source_commit)
if ($xm5State -ne 'disconnected') {
    throw 'Turn off the XM5 and wait until it is physically disconnected.'
}

$installRoot = Join-Path $projectRoot `
    'artifacts\v1-endpoint-presence\install'
$previousTransactions = @(Get-ChildItem -LiteralPath $installRoot `
    -Filter 'transaction-*.json' -File | ForEach-Object {
        try {
            Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
        } catch {
            $null
        }
    } | Where-Object {
        $null -ne $_ -and
        [string]$_.status -eq 'passed' -and
        [string]$_.device.instance_id -eq
            [string]$presentDevices[0].instance_id -and
        [string]$_.device.published_inf -eq
            [string]$presentDevices[0].published_inf
    })
if ($previousTransactions.Count -eq 0) {
    throw 'No passed install/update transaction matches the currently bound endpoint.'
}
$previous = @($previousTransactions | Sort-Object {
    [datetimeoffset]$_.completed_at
})[-1]
$previousSource = [string]$previous.source_commit
if ($previousSource.Equals(
        [string]$manifest.source_commit,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The installed endpoint already matches this candidate source.'
}
$rollbackRoot = Join-Path $projectRoot `
    "artifacts\v1-endpoint-presence\rollback\$previousSource"
$rollbackManifest = Assert-V1CandidateBundle -Root $rollbackRoot
if (-not ([string]$rollbackManifest.source_commit).Equals(
        $previousSource,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The rollback bundle source does not match the installed transaction.'
}

$devconPath = Find-V1DevCon
if ([string]::IsNullOrWhiteSpace($devconPath)) {
    throw 'The x64 WDK devcon.exe was not found.'
}
$candidateInf = Join-Path $candidateRoot `
    'package\NativeLdacAudio.inf'
$candidateCertificate = Join-Path $candidateRoot `
    'package\NativeLdacAudio.cer'
$rollbackInf = Join-Path $rollbackRoot `
    'package\NativeLdacAudio.inf'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$transactionPath = Join-Path $installRoot `
    "transaction-$stamp.json"
$logRoot = Join-Path $installRoot "update-$stamp"
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$transaction = [ordered]@{
    transaction_version = 2
    started_at = (Get-Date).ToString('o')
    completed_at = $null
    status = 'prepared'
    source_commit = [string]$manifest.source_commit
    previous_source_commit = $previousSource
    phase = 'preflight'
    previous_device = $presentDevices[0]
    device = $null
    devcon_exit_code = $null
    consumer_probe = @()
    presence_probe = @()
    link_probe = @()
    rollback_attempted = $false
    rollback_succeeded = $false
    error = $null
}
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath

Write-Host 'V1 PCM consumer-lease endpoint update preflight passed.'
Write-Host "Candidate source: $($manifest.source_commit)"
Write-Host "Rollback source: $previousSource"
Write-Host 'Original AltA2DP remains bound and healthy; XM5 is physically disconnected.'

$target = "NativeLdacAudio endpoint $($presentDevices[0].instance_id)"
$action = 'Update only the V1 root endpoint with an independent PCM consumer lease and automatically restore the previous endpoint package on failure'
if (-not $PSCmdlet.ShouldProcess($target, $action)) {
    return
}

$mutationStarted = $false
try {
    $transaction.status = 'running'
    $transaction.phase = 'trust_certificate'
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    [void](Import-Certificate -FilePath $candidateCertificate `
        -CertStoreLocation 'Cert:\LocalMachine\Root')
    [void](Import-Certificate -FilePath $candidateCertificate `
        -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher')

    $transaction.phase = 'update_endpoint'
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    $mutationStarted = $true
    $updateOutput = @(& $devconPath update $candidateInf `
        'ROOT\NativeLdacAudio' 2>&1)
    $updateExit = $LASTEXITCODE
    $transaction.devcon_exit_code = $updateExit
    $updateOutput | Set-Content -LiteralPath `
        (Join-Path $logRoot 'devcon-update.log') -Encoding UTF8
    $updateOutput | ForEach-Object { Write-Host $_ }
    if ($updateExit -ne 0) {
        throw "DevCon update returned $updateExit; this bounded V1 update does not accept a reboot requirement."
    }

    Start-Sleep -Seconds 3
    $transaction.phase = 'verify'
    $after = Get-NativeLdacBaselineSnapshot -BackupPath $backupPath
    $afterDevices = @($after.native_audio_devices | Where-Object {
        $_.present
    })
    if (-not $after.safe_original_a2dp -or
        $afterDevices.Count -ne 1 -or
        [string]$afterDevices[0].service -ne 'NativeLdacAudio' -or
        [int]$afterDevices[0].problem_code -ne 0 -or
        [string]$afterDevices[0].published_inf -eq
            [string]$presentDevices[0].published_inf) {
        throw 'The endpoint did not bind a new healthy package while preserving original A2DP.'
    }
    $transaction.device = $afterDevices[0]

    $endpointProbe = Join-Path $candidateRoot 'audio_endpoint_probe.exe'
    $consumerOutput = @(& $endpointProbe --consumer-lease 2>&1)
    $consumerExit = $LASTEXITCODE
    $transaction.consumer_probe = @($consumerOutput | ForEach-Object {
        [string]$_
    })
    if ($consumerExit -ne 0 -or
        ($consumerOutput -join "`n") -notmatch
            '(?m)^PCM consumer lease released: generation 0\.$') {
        throw 'The independent PCM consumer lease ABI is unavailable or not initially released.'
    }
    $presenceOutput = @(& $endpointProbe --presence 2>&1)
    $presenceExit = $LASTEXITCODE
    $transaction.presence_probe = @($presenceOutput | ForEach-Object {
        [string]$_
    })
    if ($presenceExit -ne 0 -or
        ($presenceOutput -join "`n") -notmatch
            '(?m)^Physical presence absent:') {
        throw 'Physical presence was not absent after the endpoint update.'
    }
    $linkOutput = @(& $endpointProbe --link-state 2>&1)
    $linkExit = $LASTEXITCODE
    $transaction.link_probe = @($linkOutput | ForEach-Object {
        [string]$_
    })
    if ($linkExit -ne 0 -or
        ($linkOutput -join "`n") -notmatch '(?m)^Link disconnected:') {
        throw 'Media LinkState was not disconnected after the endpoint update.'
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
            $rollbackOutput = @(& $devconPath update $rollbackInf `
                'ROOT\NativeLdacAudio' 2>&1)
            $rollbackExit = $LASTEXITCODE
            $rollbackOutput | Set-Content -LiteralPath `
                (Join-Path $logRoot 'devcon-rollback.log') -Encoding UTF8
            $rollbackOutput | ForEach-Object { Write-Host $_ }
            if ($rollbackExit -ne 0) {
                throw "DevCon rollback returned $rollbackExit."
            }
            Start-Sleep -Seconds 3
            $rolledBack = Get-NativeLdacBaselineSnapshot `
                -BackupPath $backupPath
            $rolledBackDevices = @($rolledBack.native_audio_devices |
                Where-Object { $_.present })
            $transaction.rollback_succeeded =
                $rolledBack.safe_original_a2dp -and
                $rolledBackDevices.Count -eq 1 -and
                [string]$rolledBackDevices[0].published_inf -eq
                    [string]$presentDevices[0].published_inf -and
                [int]$rolledBackDevices[0].problem_code -eq 0
        } catch {
            $transaction.error +=
                " Automatic endpoint rollback also failed: $($_.Exception.Message)"
        }
    }
} finally {
    $transaction.completed_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
}

if ($transaction.status -ne 'passed') {
    throw "V1 endpoint update failed: $($transaction.error) Rollback succeeded: $($transaction.rollback_succeeded). Transaction: $transactionPath"
}

Write-Host 'V1 PCM consumer-lease endpoint update passed.'
Write-Host "Bound package: $($transaction.device.published_inf)"
Write-Host "Transaction: $transactionPath"
Write-Host 'The endpoint is unplugged, PCM consumer lease is released, and media LinkState is disconnected.'
Write-Host 'No reboot, Bluetooth request, default-output change, transport OPEN, or AltA2DP change was performed.'
