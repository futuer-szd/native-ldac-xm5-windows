# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1InboundDriverUpdate,
    [string]$CandidatePath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'v1-inbound-signaling-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1InboundDriverUpdate) {
    throw 'Refusing to update LdacNative. Re-run with -ConfirmV1InboundDriverUpdate.'
}
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $root `
        'artifacts\v1-inbound-signaling\candidate'
}
$candidate = Get-V1InboundSignalingCandidate -CandidatePath $CandidatePath
$manifest = $candidate.manifest
$head = (& git.exe -C $root rev-parse HEAD).Trim()
$gitStatus = @(& git.exe -C $root status --porcelain)
if ($LASTEXITCODE -ne 0 -or $gitStatus.Count -ne 0 -or
    $head -ne [string]$manifest.source_commit) {
    throw 'The inbound-signaling candidate must match the current clean Git HEAD.'
}

$trialRoot = Join-Path $root 'artifacts\v1-inbound-signaling\trial'
$latestPath = Join-Path $trialRoot 'latest-transaction.txt'
if (Test-Path -LiteralPath $latestPath -PathType Leaf) {
    $previousPath = (Get-Content -LiteralPath $latestPath -Raw).Trim()
    if (Test-Path -LiteralPath $previousPath -PathType Leaf) {
        $previous = Get-Content -LiteralPath $previousPath -Raw |
            ConvertFrom-Json
        if ([string]$previous.status -in @(
                'preparing', 'driver-updated-ready', 'running-validation',
                'rollback-required', 'rollback-failed')) {
            throw "An inbound-signaling transaction is still active: $previousPath"
        }
    }
}

$devices = @(Get-LegacyXm5A2dpDevices)
if ($devices.Count -ne 1) {
    throw 'Exactly one present XM5 A2DP Sink service PDO is required.'
}
$before = Get-LegacyXm5A2dpSnapshot -Device $devices[0]
if ([string]$before.service -ne 'LdacNative' -or
    [int]$before.problem_code -ne 0) {
    throw 'The current XM5 A2DP Sink PDO must be healthy LdacNative before a same-service update.'
}
if (@(Get-NativeLdacWorkspaceProcesses).Count -ne 0) {
    throw 'Close all workspace media/agent processes before updating the driver.'
}
$alt = Get-NativeLdacAltA2dpUserService
if ($null -ne $alt -and [string]$alt.state -ne 'Stopped') {
    throw 'Alternative A2DP Service must remain stopped during this update.'
}
$builtIn = Get-Service -Name 'BthA2dp' -ErrorAction SilentlyContinue
if ($null -ne $builtIn -and [string]$builtIn.Status -ne 'Stopped') {
    throw 'The Windows BthA2dp service must remain stopped during this update.'
}
$connectionProbe = Join-Path $candidate.root 'xm5_connection_probe.exe'
if ((Get-NativeLdacBluetoothRadioState -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne 'ready') {
    throw 'Windows Bluetooth is off or unavailable.'
}
if ((Get-NativeLdacXm5BluetoothState -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne
    'disconnected') {
    throw 'Turn off XM5 and wait until it is physically disconnected.'
}
$transportProbe = Join-Path $candidate.root 'transport_probe.exe'
$oldCapture = Invoke-V1InboundTransportProbe -ProbePath $transportProbe `
    -Arguments @('--info')
$oldText = $oldCapture.stdout + $oldCapture.stderr
$oldFlags = Get-V1InboundReadyFlags -Text $oldText
if ($oldCapture.exit_code -ne 0 -or $null -eq $oldFlags -or
    [uint32]$oldFlags -ne 0x00000007) {
    throw 'The installed driver is not the expected healthy outbound-only ABI 0.5 baseline (ready flags 0x7).'
}

Write-Host 'V1 inbound-signaling driver update preflight passed.'
Write-Host "Candidate source: $($manifest.source_commit)"
Write-Host "Current binding: $($before.service)/$($before.published_inf), ready flags 0x00000007."
Write-Host 'XM5 is off; no workspace transport process is running.'
Write-Host 'The update will first try one same-service PnP reload of only the XM5 A2DP Sink PDO.'
Write-Host 'No Windows reboot or Bluetooth radio toggle is authorized. Any failure triggers restoration of the exported current driver.'

$target = 'Sony WH-1000XM5 A2DP Sink service PDO'
$action = 'Export the current LdacNative package, install the inbound-signaling build, restart only this PDO if needed, require ready flags 0xF, and rollback on failure'
if (-not $PSCmdlet.ShouldProcess($target, $action)) { return }

New-Item -ItemType Directory -Path $trialRoot -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$transactionPath = Join-Path $trialRoot "transaction-$stamp.json"
$logDirectory = Join-Path $trialRoot "logs-$stamp"
$backupDirectory = Join-Path $trialRoot "previous-driver-$stamp"
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null
$transaction = [ordered]@{
    schema_version = 1
    transport_policy_version = $script:V1InboundSignalingPolicyVersion
    source_commit = [string]$manifest.source_commit
    driver_tree = [string]$manifest.driver_tree
    created_at = (Get-Date).ToString('o')
    updated_at = (Get-Date).ToString('o')
    status = 'preparing'
    phase = 'confirmed'
    candidate_path = $candidate.root
    device_instance_id = [string]$before.instance_id
    previous_inf = [string]$before.published_inf
    previous_ready_flags = [uint32]$oldFlags
    previous_driver_backup_inf = $null
    installed_inf = $null
    pdo_restart_attempted = $false
    validation = $null
    rollback = [ordered]@{
        attempted = $false
        succeeded = $false
        restored_inf = $null
        error = $null
    }
    rebooted = $false
    bluetooth_toggled = $false
    error = $null
}
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
$transactionPath | Set-Content -LiteralPath $latestPath -Encoding UTF8

$mutationStarted = $false
try {
    $transaction.phase = 'exporting-current-driver'
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    $null = Invoke-LegacyPnpUtil -Arguments @(
            '/export-driver', [string]$before.published_inf,
            $backupDirectory) `
        -LogPath (Join-Path $logDirectory 'export-current-driver.log') `
        -AcceptedExitCodes @(0)
    $backupInfs = @(Get-ChildItem -LiteralPath $backupDirectory -Recurse `
        -Filter 'LdacNative.inf' -File)
    if ($backupInfs.Count -ne 1) {
        throw 'The current LdacNative package export did not produce exactly one INF.'
    }
    $transaction.previous_driver_backup_inf = $backupInfs[0].FullName

    $certificatePath = Join-Path $candidate.root 'package\LdacNative.cer'
    $certificate = Get-PfxCertificate -FilePath $certificatePath
    foreach ($store in @(
            'Cert:\LocalMachine\Root',
            'Cert:\LocalMachine\TrustedPublisher')) {
        if (-not (Test-Path -LiteralPath `
                (Join-Path $store $certificate.Thumbprint))) {
            $null = Import-Certificate -FilePath $certificatePath `
                -CertStoreLocation $store
        }
    }

    $transaction.phase = 'installing-inbound-driver'
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    $mutationStarted = $true
    $install = Invoke-LegacyPnpUtil -Arguments @(
            '/add-driver',
            (Join-Path $candidate.root 'package\LdacNative.inf'),
            '/install') `
        -LogPath (Join-Path $logDirectory 'install-inbound-driver.log')
    $bound = Wait-LegacyXm5A2dpService -ExpectedService 'LdacNative' `
        -TimeoutSeconds 30
    if ($null -eq $bound) {
        throw 'LdacNative was not bound after the package update.'
    }
    $transaction.installed_inf = [string]$bound.published_inf
    if ($install.reboot_required) {
        throw 'PnP reported that the driver update requires a Windows reboot; this transaction does not authorize one.'
    }

    $ready = Wait-V1InboundTransportInfo -ProbePath $transportProbe `
        -ExpectedFlags $script:V1InboundReadyFlags -TimeoutSeconds 5
    if ($null -eq $ready) {
        $transaction.pdo_restart_attempted = $true
        $transaction.phase = 'restarting-a2dp-service-pdo'
        $transaction.updated_at = (Get-Date).ToString('o')
        Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
        $restart = Invoke-LegacyPnpUtil -Arguments @(
                '/restart-device', [string]$before.instance_id) `
            -LogPath (Join-Path $logDirectory 'restart-a2dp-pdo.log')
        if ($restart.reboot_required) {
            throw 'Restarting the XM5 A2DP service PDO unexpectedly requires a Windows reboot.'
        }
        $ready = Wait-V1InboundTransportInfo -ProbePath $transportProbe `
            -ExpectedFlags $script:V1InboundReadyFlags -TimeoutSeconds 20
    }
    if ($null -eq $ready) {
        throw 'The updated driver did not expose ABI 0.5 ready flags 0xF after its bounded same-service reload.'
    }
    $afterDevices = @(Get-LegacyXm5A2dpDevices)
    if ($afterDevices.Count -ne 1) {
        throw 'The updated XM5 A2DP PDO is not unique.'
    }
    $after = Get-LegacyXm5A2dpSnapshot -Device $afterDevices[0]
    if ([string]$after.service -ne 'LdacNative' -or
        [int]$after.problem_code -ne 0) {
        throw 'The updated XM5 A2DP PDO is not healthy.'
    }
    $transaction.installed_inf = [string]$after.published_inf
    $transaction.status = 'driver-updated-ready'
    $transaction.phase = 'awaiting-inbound-discovery-validation'
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
} catch {
    $transaction.error = $_.Exception.Message
    if ($mutationStarted) {
        $transaction.rollback.attempted = $true
        try {
            $restored = Restore-V1InboundPreviousDriver `
                -Transaction $transaction -LogDirectory $logDirectory
            $transaction.rollback.succeeded = $true
            $transaction.rollback.restored_inf =
                [string]$restored.published_inf
            $transaction.status = 'prepare-failed-and-restored'
            $transaction.phase = 'prepare-failed-and-restored'
        } catch {
            $transaction.rollback.error = $_.Exception.Message
            $transaction.status = 'rollback-failed'
            $transaction.phase = 'rollback-failed'
        }
    } else {
        $transaction.status = 'prepare-failed-no-driver-mutation'
        $transaction.phase = 'prepare-failed-no-driver-mutation'
    }
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    throw "V1 inbound-signaling driver update failed: $($transaction.error) Transaction: $transactionPath"
}

Write-Host 'V1 inbound-signaling driver update completed: ABI 0.5 ready flags 0xF.'
Write-Host 'No reboot and no Windows Bluetooth toggle occurred. Keep XM5 off.'
Write-Host 'Next run:'
Write-Host 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\run-v1-inbound-signaling-discovery-gate.ps1 -ConfirmV1InboundSignalingDiscovery -DurationSeconds 180'
Write-Host "Transaction: $transactionPath"
