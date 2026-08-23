# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1RebootPreparation,
    [switch]$ConfirmPinImpactAndReboot,
    [string]$CandidatePath,
    [string]$BackupPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'v1-reboot-discovery-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1RebootPreparation -or
    -not $ConfirmPinImpactAndReboot) {
    throw 'This gate requires -ConfirmV1RebootPreparation and -ConfirmPinImpactAndReboot because exactly one Windows reboot is required and may invalidate Windows Hello PIN state on this machine.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $projectRoot `
        'artifacts\v1-reboot-discovery\candidate'
}
$candidate = Get-V1RebootDiscoveryCandidate -CandidatePath $CandidatePath
$manifest = $candidate.manifest
$CandidatePath = $candidate.root

$headCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$gitStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or $gitStatus.Count -ne 0 -or
    $headCommit -ne [string]$manifest.source_commit) {
    throw 'The candidate must match the current clean Git HEAD.'
}

$transactionRoot = Join-Path $projectRoot `
    'artifacts\v1-reboot-discovery\trial'
$latestTransactionPath = Join-Path $transactionRoot `
    'latest-transaction.txt'
if (Test-Path -LiteralPath $latestTransactionPath -PathType Leaf) {
    $previousPath = (Get-Content -LiteralPath $latestTransactionPath `
        -Raw).Trim()
    if (Test-Path -LiteralPath $previousPath -PathType Leaf) {
        $previous = Get-Content -LiteralPath $previousPath -Raw |
            ConvertFrom-Json
        if ([string]$previous.status -in @(
                'preparing',
                'awaiting-reboot',
                'running-post-reboot',
                'rollback-required',
                'rollback-failed',
                'configuration-verified')) {
            throw "A V1 zero-packet configuration transaction is still active: $previousPath`nDo not prepare another installation. Use its post-reboot gate or rollback command."
        }
    }
}

$priorCapabilityEvidencePath = $null
$priorTransactions = @(Get-ChildItem -LiteralPath $transactionRoot `
    -Filter 'transaction-*.json' -File -ErrorAction SilentlyContinue)
foreach ($priorPath in $priorTransactions) {
    $prior = Get-Content -LiteralPath $priorPath.FullName -Raw |
        ConvertFrom-Json
    $gateKind = $prior.PSObject.Properties['gate_kind']
    $driverTree = $prior.PSObject.Properties['driver_tree']
    if ($null -ne $gateKind -and
        [string]$gateKind.Value -eq 'zero-packet-configuration' -and
        $null -ne $driverTree -and
        [string]$driverTree.Value -eq [string]$manifest.driver_tree -and
        [string]$prior.status -in @(
            'failed-and-restored',
            'rollback-verified') -and
        $null -ne $prior.configuration -and
        $prior.configuration.passed -eq $false) {
        throw "This exact driver tree and zero-packet transport policy already failed and were restored safely: $($priorPath.FullName)`nA rebuild, retry, or another reboot is not authorized."
    }
}
foreach ($priorPath in $priorTransactions) {
    $prior = Get-Content -LiteralPath $priorPath.FullName -Raw |
        ConvertFrom-Json
    if (Test-V1CapabilityPrerequisiteTransaction `
            -Transaction $prior `
            -ExpectedDriverTree ([string]$manifest.driver_tree) `
            -ProjectRoot $projectRoot) {
        $priorCapabilityEvidencePath = $priorPath.FullName
        break
    }
}
if ([string]::IsNullOrWhiteSpace($priorCapabilityEvidencePath)) {
    throw 'The zero-packet configuration gate requires one verified capability-only hardware result for this exact driver tree. No driver or system setting was changed.'
}

if ([string]::IsNullOrWhiteSpace($BackupPath)) {
    $latestBackupPath = Join-Path $projectRoot `
        'artifacts\driver-test\latest-backup.txt'
    $BackupPath = (Get-Content -LiteralPath $latestBackupPath -Raw).Trim()
}
$BackupPath = [System.IO.Path]::GetFullPath($BackupPath)
$before = Get-NativeLdacBaselineSnapshot -BackupPath $BackupPath
$nativeEndpoints = @($before.native_audio_devices | Where-Object {
    $_.present
})
$altService = $before.original_a2dp_user_service
if (-not $before.safe_original_a2dp -or
    -not $before.original_binding_healthy -or
    -not $before.test_signing_active -or
    @($before.a2dp_devices).Count -ne 1 -or
    $nativeEndpoints.Count -ne 1 -or
    [string]$nativeEndpoints[0].service -ne 'NativeLdacAudio' -or
    [int]$nativeEndpoints[0].problem_code -ne 0 -or
    $null -eq $altService -or
    [string]$altService.start_mode -ne 'Auto' -or
    [string]$altService.state -ne 'Running') {
    Write-NativeLdacBaselineSummary -Snapshot $before
    throw 'Preparation requires healthy AltA2DP plus one healthy V1 endpoint. The retained Native audio packages are allowed.'
}

$connectionProbe = Join-Path $CandidatePath 'xm5_connection_probe.exe'
if ((Get-NativeLdacBluetoothRadioState `
        -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne
    'ready') {
    throw 'Windows Bluetooth is off or the local radio is unavailable. Turn Bluetooth on before any driver mutation.'
}
if ((Get-NativeLdacXm5BluetoothState `
        -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne
    'disconnected') {
    throw 'Turn off XM5 and wait until it is physically disconnected.'
}
$endpointProbe = Join-Path $CandidatePath 'audio_endpoint_probe.exe'
$presence = @(& $endpointProbe --presence 2>&1)
$link = @(& $endpointProbe --link-state 2>&1)
if (($presence -join "`n") -notmatch
        '(?m)^Physical presence absent:' -or
    ($link -join "`n") -notmatch '(?m)^Link disconnected:') {
    throw 'The V1 endpoint must be unplugged and media LinkState disconnected.'
}

Write-Host 'V1 zero-packet configuration preparation preflight passed.'
Write-Host "Candidate source: $($manifest.source_commit)"
Write-Host 'Windows Bluetooth is on; XM5 is physically disconnected.'
Write-Host 'Exactly one Windows reboot will be required after installation.'
Write-Host 'That reboot may trigger the known Windows Hello PIN recovery issue.'
Write-Host 'This preflight was read-only.'
Write-Host 'The same driver tree already completed capability-only discovery.'
Write-Host "Capability evidence: $priorCapabilityEvidencePath"

$target = 'Sony WH-1000XM5 A2DP Sink service PDO across one explicit reboot'
$action = 'Stop Alternative A2DP Service, bind LdacNative without opening Bluetooth transport, and prepare one recoverable zero-packet configuration gate'
if (-not $PSCmdlet.ShouldProcess($target, $action)) {
    return
}

New-Item -ItemType Directory -Path $transactionRoot -Force |
    Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$transactionPath = Join-Path $transactionRoot `
    "transaction-$stamp.json"
$logDirectory = Join-Path $transactionRoot "logs-$stamp"
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$transaction = [ordered]@{
    schema_version = 1
    source_commit = [string]$manifest.source_commit
    driver_tree = [string]$manifest.driver_tree
    transport_policy_version = [int]$manifest.transport_policy_version
    gate_kind = 'zero-packet-configuration'
    prerequisite_capability_transaction = $priorCapabilityEvidencePath
    created_at = (Get-Date).ToString('o')
    updated_at = (Get-Date).ToString('o')
    prepared_boot_time_utc = [string]$before.boot_time_utc
    status = 'preparing'
    phase = 'confirmed'
    candidate_path = $CandidatePath
    backup_path = $BackupPath
    original_service = [string]$before.expected_original_service
    original_inf = [string]$before.expected_original_inf
    installed_inf = $null
    post_reboot = $null
    configuration = $null
    rollback = [ordered]@{
        attempted = $false
        succeeded = $false
        service = $null
        published_inf = $null
        error = $null
    }
    error = $null
}
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
$transactionPath | Set-Content -LiteralPath $latestTransactionPath `
    -Encoding UTF8

$mutationStarted = $false
try {
    $mutationStarted = $true
    $transaction.phase = 'quiescing-alta2dp-service'
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    Stop-Service -Name 'AltA2dpSVC' -ErrorAction Stop
    Set-Service -Name 'AltA2dpSVC' -StartupType Manual
    $quiesced = Get-NativeLdacAltA2dpUserService
    if ($null -eq $quiesced -or
        [string]$quiesced.start_mode -ne 'Manual' -or
        [string]$quiesced.state -ne 'Stopped') {
        throw 'Alternative A2DP Service did not enter Manual/Stopped.'
    }

    $certificatePath = Join-Path $CandidatePath `
        'package\LdacNative.cer'
    $certificate = Get-PfxCertificate -FilePath $certificatePath
    foreach ($storePath in @(
            'Cert:\LocalMachine\Root',
            'Cert:\LocalMachine\TrustedPublisher')) {
        if (-not (Test-Path -LiteralPath `
                (Join-Path $storePath $certificate.Thumbprint))) {
            $null = Import-Certificate -FilePath $certificatePath `
                -CertStoreLocation $storePath
        }
    }

    $transaction.phase = 'installing-ldacnative'
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    $install = Invoke-LegacyPnpUtil -Arguments @(
            '/add-driver',
            (Join-Path $CandidatePath 'package\LdacNative.inf'),
            '/install') `
        -LogPath (Join-Path $logDirectory 'install.log') `
        -AcceptedExitCodes @(0, 3010)
    $installed = Wait-LegacyXm5A2dpService `
        -ExpectedService 'LdacNative' -TimeoutSeconds 30
    if ($null -eq $installed -or
        [int]$installed.problem_code -notin @(0, 38)) {
        throw 'LdacNative did not bind in a healthy or reboot-pending state.'
    }
    $transaction.installed_inf = [string]$installed.published_inf
    $transaction.install_reboot_reported = $install.reboot_required
    $transaction.status = 'awaiting-reboot'
    $transaction.phase = 'installed-awaiting-reboot'
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
} catch {
    $transaction.error = $_.Exception.Message
    if ($mutationStarted) {
        $transaction.rollback.attempted = $true
        try {
            $restored = Restore-V1RebootDiscoveryOriginalA2dp `
                -Transaction $transaction -LogDirectory $logDirectory
            $transaction.rollback.succeeded = $true
            $transaction.rollback.service = $restored.service
            $transaction.rollback.published_inf = $restored.published_inf
            $transaction.status = 'prepare-failed-and-restored'
            $transaction.phase = 'prepare-failed-and-restored'
        } catch {
            $transaction.rollback.error = $_.Exception.Message
            $transaction.status = 'rollback-failed'
            $transaction.phase = 'rollback-failed'
        }
    }
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    throw "V1 zero-packet configuration preparation failed: $($transaction.error) Transaction: $transactionPath"
}

Write-Host 'V1 zero-packet configuration preparation completed without Bluetooth OPEN.'
Write-Host 'Keep XM5 off, close your work, and reboot Windows exactly once.'
Write-Host 'After signing in, keep XM5 off and do not start audio.'
Write-Host 'Then run:'
Write-Host 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\run-v1-post-reboot-zero-packet-gate.ps1 -ConfirmV1ZeroPacketGate -DurationSeconds 180'
Write-Host "Transaction: $transactionPath"
