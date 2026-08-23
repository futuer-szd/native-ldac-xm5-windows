# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1InboundPnpRundownUpdate,
    [switch]$ConfirmPinImpactAndReboot,
    [switch]$ConfirmKnownCode38Recovery,
    [string]$CandidatePath,
    [string]$PrerequisiteTransactionPath,
    [string]$FailedTransactionPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'v1-inbound-pnp-rundown-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1InboundPnpRundownUpdate -or
    -not $ConfirmPinImpactAndReboot) {
    throw 'Refusing to replace the old inbound driver. Re-run with both -ConfirmV1InboundPnpRundownUpdate and -ConfirmPinImpactAndReboot.'
}

$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$trialRoot = Join-Path $root 'artifacts\v1-inbound-pnp-rundown\trial'
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $root `
        'artifacts\v1-inbound-pnp-rundown\candidate'
}
if ([string]::IsNullOrWhiteSpace($PrerequisiteTransactionPath)) {
    $latestInbound = Join-Path $root `
        'artifacts\v1-inbound-signaling\trial\latest-transaction.txt'
    if (-not (Test-Path -LiteralPath $latestInbound -PathType Leaf)) {
        throw 'The completed inbound DISCOVER transaction was not found.'
    }
    $PrerequisiteTransactionPath =
        (Get-Content -LiteralPath $latestInbound -Raw).Trim()
}
$PrerequisiteTransactionPath =
    [System.IO.Path]::GetFullPath($PrerequisiteTransactionPath)

$candidate = Get-V1InboundPnpRundownCandidate `
    -CandidatePath $CandidatePath
$manifest = $candidate.manifest
$head = (& git.exe -C $root rev-parse HEAD).Trim()
$gitStatus = @(& git.exe -C $root status --porcelain)
if ($LASTEXITCODE -ne 0 -or $gitStatus.Count -ne 0 -or
    $head -ne [string]$manifest.source_commit) {
    throw 'The PnP-rundown candidate must match the current clean Git HEAD.'
}

$prerequisite = Get-Content -LiteralPath $PrerequisiteTransactionPath -Raw |
    ConvertFrom-Json
$prerequisiteResultPath =
    [System.IO.Path]::GetFullPath([string]$prerequisite.validation.result)
$prerequisiteResult =
    Get-Content -LiteralPath $prerequisiteResultPath -Raw | ConvertFrom-Json
if (-not (Test-V1InboundPnpPrerequisite `
        -Transaction $prerequisite `
        -TransactionPath $PrerequisiteTransactionPath `
        -Result $prerequisiteResult `
        -ResultPath $prerequisiteResultPath)) {
    throw 'The finalized inbound DISCOVER evidence is not a valid PnP-rundown prerequisite.'
}

$knownCode38Recovery = $false
$failedTransaction = $null
if ($ConfirmKnownCode38Recovery) {
    if ([string]::IsNullOrWhiteSpace($FailedTransactionPath)) {
        $FailedTransactionPath = Join-Path $trialRoot `
            'transaction-20260730-161718-212.json'
    }
    $FailedTransactionPath =
        [System.IO.Path]::GetFullPath($FailedTransactionPath)
    $failedTransaction = Get-Content -LiteralPath $FailedTransactionPath `
        -Raw | ConvertFrom-Json
    if (-not (Test-V1InboundPnpKnownCode38RecoveryTransaction `
            -Transaction $failedTransaction)) {
        throw 'The selected transaction is not the known rejected-tree Code 38 failure.'
    }
    $knownCode38Recovery = $true
}

$latestPath = Join-Path $trialRoot 'latest-transaction.txt'
if (Test-Path -LiteralPath $latestPath -PathType Leaf) {
    $previousPath = (Get-Content -LiteralPath $latestPath -Raw).Trim()
    if (Test-Path -LiteralPath $previousPath -PathType Leaf) {
        $previous = Get-Content -LiteralPath $previousPath -Raw |
            ConvertFrom-Json
        if ([string]$previous.status -notin @(
                'pnp-rundown-verified', 'rollback-verified',
                'prepare-failed-no-binding-change') -and
            (-not $knownCode38Recovery -or
             -not ([System.IO.Path]::GetFullPath($previousPath)).Equals(
                $FailedTransactionPath,
                [StringComparison]::OrdinalIgnoreCase))) {
            throw "A PnP-rundown transaction is still active: $previousPath"
        }
    }
}

$devices = @(Get-LegacyXm5A2dpDevices)
if ($devices.Count -ne 1) {
    throw 'Exactly one present XM5 A2DP Sink service PDO is required.'
}
$before = Get-LegacyXm5A2dpSnapshot -Device $devices[0]
if ($knownCode38Recovery) {
    if ([string]$before.service -ne 'LdacNative' -or
        [int]$before.problem_code -ne 38 -or
        -not ([string]$before.published_inf).Equals(
            [string]$failedTransaction.selected_inf,
            [StringComparison]::OrdinalIgnoreCase) -or
        -not ([string]$before.instance_id).Equals(
            [string]$failedTransaction.device_instance_id,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The current PDO does not match the known rejected-tree Code 38 binding.'
    }
} elseif ([string]$before.service -ne 'LdacNative' -or
    [int]$before.problem_code -ne 0 -or
    -not ([string]$before.published_inf).Equals(
        [string]$prerequisite.installed_inf,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The completed inbound DISCOVER driver must be the current healthy binding.'
}
if (@(Get-NativeLdacWorkspaceProcesses).Count -ne 0) {
    throw 'Close all workspace media and agent processes before the update.'
}
$tasks = @(Get-ScheduledTask -TaskName 'Native LDAC Agent' `
    -ErrorAction SilentlyContinue)
if ($tasks.Count -ne 0) {
    throw 'Remove the Native LDAC login task before the PnP-rundown update.'
}
$alt = Get-NativeLdacAltA2dpUserService
if ($null -ne $alt -and [string]$alt.state -ne 'Stopped') {
    throw 'Alternative A2DP Service must remain stopped.'
}
$builtIn = Get-Service -Name 'BthA2dp' -ErrorAction SilentlyContinue
if ($null -ne $builtIn -and [string]$builtIn.Status -ne 'Stopped') {
    throw 'The Windows BthA2dp service must remain stopped.'
}
$connectionProbe = Join-Path $candidate.root 'xm5_connection_probe.exe'
if ((Get-NativeLdacBluetoothRadioState -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne
    'ready') {
    throw 'Windows Bluetooth is off or unavailable.'
}
if ((Get-NativeLdacXm5BluetoothState -ProbePath $connectionProbe `
        -ExpectedSourceCommit ([string]$manifest.source_commit)) -ne
    'disconnected') {
    throw 'Keep XM5 off until the post-reboot validation gate is armed.'
}
$transportProbe = Join-Path $candidate.root 'transport_probe.exe'
if (-not $knownCode38Recovery) {
    $ready = Wait-V1InboundTransportInfo -ProbePath $transportProbe `
        -ExpectedFlags $script:V1InboundReadyFlags -TimeoutSeconds 2
    if ($null -eq $ready) {
        throw 'The current inbound driver does not expose ABI 0.5 ready flags 0xF.'
    }
}

Write-Host 'V1 inbound PnP-rundown update preflight passed.'
Write-Host "Candidate source: $($manifest.source_commit)"
Write-Host "Approved driver tree: $($manifest.driver_tree)"
if ($knownCode38Recovery) {
    Write-Host "Current binding: $($before.service)/$($before.published_inf), known rejected-tree Code 38."
    Write-Host "Recovery evidence: $FailedTransactionPath"
} else {
    Write-Host "Current binding: $($before.service)/$($before.published_inf), ready flags 0x0000000F."
}
Write-Host 'The old inbound build cannot be safely hot-restarted because it lacks the PnP rundown fix.'
Write-Host 'This transaction exports the old package, selects the fixed package, and then requires exactly one Windows restart.'
Write-Host 'A temporary Code 38 before that restart is an expected possible old-driver unload result; no same-boot rollback or PDO retry will be attempted.'
Write-Host 'The restart may trigger the known Windows Hello PIN recovery issue.'

$target = 'Sony WH-1000XM5 LdacNative binding across one explicit reboot'
$action = 'Export the verified old inbound package, install/select the frozen PnP-rundown package without transport I/O, and record one required Windows restart'
if (-not $PSCmdlet.ShouldProcess($target, $action)) { return }

New-Item -ItemType Directory -Path $trialRoot -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$transactionPath = Join-Path $trialRoot "transaction-$stamp.json"
$logDirectory = Join-Path $trialRoot "logs-$stamp"
$backupDirectory = Join-Path $trialRoot "previous-driver-$stamp"
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null

$beforePackages = @(Get-LegacyDriverPackages `
    -OriginalInfNames @('LdacNative.inf'))
$beforePackageNames = @($beforePackages | ForEach-Object {
    [string]$_.Driver
})
$transaction = [ordered]@{
    schema_version = 1
    transport_policy_version =
        $script:V1InboundPnpRundownPolicyVersion
    source_commit = [string]$manifest.source_commit
    driver_tree = [string]$manifest.driver_tree
    created_at = (Get-Date).ToString('o')
    updated_at = (Get-Date).ToString('o')
    status = 'preparing'
    phase = 'confirmed'
    candidate_path = $candidate.root
    prerequisite_transaction = $PrerequisiteTransactionPath
    prerequisite_result = $prerequisiteResultPath
    device_instance_id = [string]$before.instance_id
    previous_inf = [string]$before.published_inf
    previous_ready_flags = [uint32]$script:V1InboundReadyFlags
    known_code38_recovery = $knownCode38Recovery
    recovery_transaction = if ($knownCode38Recovery) {
        $FailedTransactionPath
    } else {
        $null
    }
    previous_driver_backup_inf = $null
    safe_fallback_backup_inf =
        [string]$prerequisite.previous_driver_backup_inf
    selected_inf = $null
    selected_before_restart = $false
    problem_code_before_restart = $null
    boot_time_before = Get-V1InboundPnpCurrentBootTime
    boot_time_after = $null
    reboot_verified = $false
    cycles = @()
    result = $null
    rollback = [ordered]@{
        attempted = $false
        succeeded = $false
        restored_inf = $null
        error = $null
    }
    bluetooth_toggled = $false
    error = $null
}
Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
$transactionPath | Set-Content -LiteralPath $latestPath -Encoding UTF8

$bindingChanged = $false
try {
    $transaction.phase = 'exporting-old-inbound-driver'
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    $null = Invoke-LegacyPnpUtil -Arguments @(
            '/export-driver', [string]$before.published_inf,
            $backupDirectory) `
        -LogPath (Join-Path $logDirectory 'export-old-driver.log') `
        -AcceptedExitCodes @(0)
    $backupInfs = @(Get-ChildItem -LiteralPath $backupDirectory -Recurse `
        -Filter 'LdacNative.inf' -File)
    if ($backupInfs.Count -ne 1) {
        throw 'The old inbound package export did not produce exactly one INF.'
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

    $transaction.phase = 'selecting-pnp-rundown-driver'
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    $install = Invoke-LegacyPnpUtil -Arguments @(
            '/add-driver',
            (Join-Path $candidate.root 'package\LdacNative.inf'),
            '/install') `
        -LogPath (Join-Path $logDirectory 'install-pnp-rundown-driver.log') `
        -AcceptedExitCodes @(0, 3010)

    $afterPackages = @(Get-LegacyDriverPackages `
        -OriginalInfNames @('LdacNative.inf'))
    $newPackages = @($afterPackages | Where-Object {
        [string]$_.Driver -notin $beforePackageNames
    })
    if ($newPackages.Count -ne 1) {
        throw 'The PnP-rundown update did not add exactly one new LdacNative package.'
    }
    $transaction.selected_inf = [string]$newPackages[0].Driver

    $deadline = (Get-Date).AddSeconds(15)
    $selected = $null
    do {
        $currentDevices = @(Get-LegacyXm5A2dpDevices)
        if ($currentDevices.Count -eq 1) {
            $selected = Get-LegacyXm5A2dpSnapshot `
                -Device $currentDevices[0]
            if ([string]$selected.service -eq 'LdacNative' -and
                ([string]$selected.published_inf).Equals(
                    [string]$transaction.selected_inf,
                    [StringComparison]::OrdinalIgnoreCase)) {
                break
            }
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    $selectedBeforeRestart = $null -ne $selected -and
        ([string]$selected.published_inf).Equals(
            [string]$transaction.selected_inf,
            [StringComparison]::OrdinalIgnoreCase)
    if (-not $selectedBeforeRestart -and -not $install.reboot_required) {
        throw 'Windows staged but did not select the PnP-rundown package, and PnPUtil did not report a pending reboot activation.'
    }
    $bindingChanged = $selectedBeforeRestart
    $transaction.problem_code_before_restart =
        if ($selectedBeforeRestart) {
            [int]$selected.problem_code
        } else {
            [int]$before.problem_code
        }
    $transaction.selected_before_restart = $selectedBeforeRestart
    $transaction.status = 'reboot-required'
    $transaction.phase = 'fixed-driver-selected-awaiting-reboot'
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
} catch {
    $transaction.error = $_.Exception.Message
    if ($bindingChanged) {
        $transaction.status = 'reboot-required-after-prepare-error'
        $transaction.phase = 'fixed-binding-selected-awaiting-reboot'
    } else {
        $transaction.status = 'prepare-failed-no-binding-change'
        $transaction.phase = 'prepare-failed-no-binding-change'
    }
    $transaction.updated_at = (Get-Date).ToString('o')
    Write-LegacyJsonAtomic -Value $transaction -Path $transactionPath
    throw "V1 inbound PnP-rundown preparation failed: $($transaction.error) Transaction: $transactionPath"
}

Write-Host 'V1 inbound PnP-rundown package selected.'
Write-Host "Selected INF: $($transaction.selected_inf); current pre-reboot problem code: $($transaction.problem_code_before_restart)."
Write-Host 'Keep XM5 off, close your work, and restart Windows exactly once.'
Write-Host 'After signing in, keep XM5 off and do not start audio.'
Write-Host 'Then run:'
Write-Host 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\run-v1-inbound-pnp-rundown-gate.ps1 -ConfirmV1InboundPnpRundown -DurationSeconds 360'
Write-Host "Transaction: $transactionPath"
