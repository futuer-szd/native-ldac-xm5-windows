# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'native-ldac-baseline-common.ps1')

$script:V1InboundSignalingPolicyVersion = 12
$script:V1InboundReadyFlags = 0x0000000F

function Get-V1InboundSignalingCandidate {
    param([Parameter(Mandatory = $true)][string]$CandidatePath)

    $root = [System.IO.Path]::GetFullPath($CandidatePath)
    $manifestPath = Join-Path $root 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "The V1 inbound-signaling manifest is missing: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw |
        ConvertFrom-Json
    $requiredCapabilities = @(
        'fixed_avdtp_psm_inbound_server',
        'reuse_inbound_signaling_channel',
        'read_only_handles_preserve_inbound_channel',
        'single_discover_no_media',
        'hci_proves_no_outbound_psm25_request',
        'same_service_pnp_update_with_rollback',
        'no_windows_bluetooth_toggle',
        'no_reboot_by_default')
    $capabilities = @($manifest.capabilities | ForEach-Object { [string]$_ })
    $expectedFiles = @(
        'package\LdacNative.inf',
        'package\LdacNative.sys',
        'package\ldacnative.cat',
        'package\LdacNative.cer',
        'transport_probe.exe',
        'xm5_connection_probe.exe',
        'xm5_connection_probe.manifest.json')
    $files = @($manifest.files)
    $paths = @($files | ForEach-Object { [string]$_.path })
    if ([int]$manifest.manifest_version -ne 1 -or
        [int]$manifest.transport_policy_version -ne
            $script:V1InboundSignalingPolicyVersion -or
        [string]$manifest.configuration -cne 'Release' -or
        $manifest.source_dirty -ne $false -or
        [string]$manifest.source_commit -notmatch '^[0-9a-fA-F]{40}$' -or
        [string]$manifest.driver_tree -notmatch '^[0-9a-fA-F]{40}$' -or
        [string]$manifest.driver_abi -ne '0.5' -or
        [int]$manifest.required_ready_flags -ne
            $script:V1InboundReadyFlags -or
        @($requiredCapabilities | Where-Object {
            $_ -notin $capabilities
        }).Count -ne 0 -or
        $files.Count -ne $expectedFiles.Count -or
        @($expectedFiles | Where-Object { $_ -notin $paths }).Count -ne 0 -or
        @($paths | Select-Object -Unique).Count -ne $expectedFiles.Count) {
        throw 'The V1 inbound-signaling candidate contract is invalid.'
    }
    foreach ($file in $files) {
        $path = Join-Path $root ([string]$file.path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-Item -LiteralPath $path).Length -ne [long]$file.length -or
            -not (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.Equals(
                [string]$file.sha256,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "V1 inbound-signaling file failed its hash check: $($file.path)"
        }
    }
    [pscustomobject]@{ root = $root; manifest = $manifest }
}

function Invoke-V1InboundTransportProbe {
    param(
        [Parameter(Mandatory = $true)][string]$ProbePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    Invoke-LegacyNativeCapture -FilePath $ProbePath -Arguments $Arguments
}

function Invoke-V1InboundStreamingAclProbe {
    param(
        [Parameter(Mandatory = $true)][string]$ProbePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $lines = @()
    $exitCode = -1
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(
            & $ProbePath @Arguments 2>&1 |
                ForEach-Object {
                    $line = [string]$_
                    Write-Host $line
                    $line
                }
        )
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    return [pscustomobject][ordered]@{
        exit_code = $exitCode
        stdout = if ($lines.Count -eq 0) {
            ''
        } else {
            ($lines -join [Environment]::NewLine) +
                [Environment]::NewLine
        }
        stderr = ''
    }
}

function Test-V1InboundPublicDisconnectedCapture {
    param([Parameter(Mandatory = $true)]$Capture)

    $text = [string]$Capture.stdout + [string]$Capture.stderr
    return [int]$Capture.exit_code -eq 10 -and
        $text -match
            '(?m)^XM5 Bluetooth state: disconnected\.\r?$'
}

function Wait-V1InboundPublicDisconnect {
    param(
        [Parameter(Mandatory = $true)][string]$ProbePath,
        [Parameter(Mandatory = $true)][string]$ExpectedSourceCommit,
        [ValidateRange(1, 60)][int]$TimeoutSeconds = 30,
        [ValidateRange(50, 2000)][int]$PollMilliseconds = 250
    )

    Assert-NativeLdacXm5ConnectionProbe `
        -ProbePath $ProbePath `
        -ExpectedSourceCommit $ExpectedSourceCommit
    $startedAt = Get-Date
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $attempts = 0
    $lastCapture = $null
    do {
        $attempts++
        $lastCapture = Invoke-V1InboundTransportProbe `
            -ProbePath $ProbePath -Arguments @('--state')
        if (Test-V1InboundPublicDisconnectedCapture `
                -Capture $lastCapture) {
            $timer.Stop()
            return [pscustomobject][ordered]@{
                disconnected = $true
                started_at = $startedAt.ToString('o')
                converged_at = (Get-Date).ToString('o')
                elapsed_ms = [long]$timer.ElapsedMilliseconds
                poll_attempts = $attempts
                last_exit_code = [int]$lastCapture.exit_code
                last_text = [string]$lastCapture.stdout +
                    [string]$lastCapture.stderr
            }
        }
        if ($timer.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
            break
        }
        Start-Sleep -Milliseconds $PollMilliseconds
    } while ($true)
    $timer.Stop()
    return [pscustomobject][ordered]@{
        disconnected = $false
        started_at = $startedAt.ToString('o')
        converged_at = $null
        elapsed_ms = [long]$timer.ElapsedMilliseconds
        poll_attempts = $attempts
        last_exit_code = if ($null -eq $lastCapture) {
            $null
        } else {
            [int]$lastCapture.exit_code
        }
        last_text = if ($null -eq $lastCapture) {
            ''
        } else {
            [string]$lastCapture.stdout + [string]$lastCapture.stderr
        }
    }
}

function Get-V1InboundReadyFlags {
    param([Parameter(Mandatory = $true)][string]$Text)

    $match = [regex]::Match(
        $Text,
        '(?im)^Ready flags:\s*0x([0-9A-F]{8})\s*$')
    if (-not $match.Success) { return $null }
    return [Convert]::ToUInt32($match.Groups[1].Value, 16)
}

function Wait-V1InboundTransportInfo {
    param(
        [Parameter(Mandatory = $true)][string]$ProbePath,
        [Parameter(Mandatory = $true)][uint32]$ExpectedFlags,
        [int]$TimeoutSeconds = 20
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $capture = Invoke-V1InboundTransportProbe -ProbePath $ProbePath `
            -Arguments @('--info')
        $text = $capture.stdout + $capture.stderr
        $flags = Get-V1InboundReadyFlags -Text $text
        if ($capture.exit_code -eq 0 -and $null -ne $flags -and
            [uint32]$flags -eq $ExpectedFlags) {
            return [pscustomobject]@{
                flags = [uint32]$flags
                text = $text
            }
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    return $null
}

function Test-V1InboundDiscoveryCoreEvidence {
    param(
        [AllowNull()]$CaptureFailure,
        [Parameter(Mandatory = $true)][int]$ConnectExitCode,
        [Parameter(Mandatory = $true)][int]$DiscoverExitCode,
        [Parameter(Mandatory = $true)][AllowEmptyString()]
        [string]$DiscoverText,
        [Parameter(Mandatory = $true)][AllowEmptyString()]
        [string]$FinalDiagnosticText,
        [Parameter(Mandatory = $true)]$Summary,
        [Parameter(Mandatory = $true)][int]$SetConfigurationCommands,
        [Parameter(Mandatory = $true)][int]$AvdtpOpenCommands,
        [Parameter(Mandatory = $true)][int]$AvdtpStartCommands,
        [Parameter(Mandatory = $true)][int]$MediaL2capOpenCommands,
        [Parameter(Mandatory = $true)][int]$MediaPackets
    )

    return $null -eq $CaptureFailure -and
        $ConnectExitCode -eq 0 -and
        $DiscoverExitCode -eq 0 -and
        $DiscoverText -match
            '(?m)^Selected LDAC audio sink SEID:\s*\d+\s*\r?$' -and
        $DiscoverText -match
            '(?m)^Signaling channel closed\.\r?$' -and
        $FinalDiagnosticText -match
            '(?m)^Signaling channel direction: inbound\.\r?$' -and
        $FinalDiagnosticText -match
            '(?m)^L2CAP OPEN state: completed, succeeded\.\r?$' -and
        [int]$Summary.inbound_avdtp_connection_requests -ge 1 -and
        [int]$Summary.outbound_avdtp_connection_requests -eq 0 -and
        [int]$Summary.outbound_success_responses_to_inbound_avdtp -ge 1 -and
        [int]$Summary.outbound_rejections_to_inbound_avdtp -eq 0 -and
        [int]$Summary.inbound_no_resources_responses -eq 0 -and
        $Summary.inbound_avdtp_pending_without_success -eq $false -and
        $SetConfigurationCommands -eq 0 -and
        $AvdtpOpenCommands -eq 0 -and
        $AvdtpStartCommands -eq 0 -and
        $MediaL2capOpenCommands -eq 0 -and
        $MediaPackets -eq 0
}

function Restore-V1InboundPreviousDriver {
    param(
        [Parameter(Mandatory = $true)]$Transaction,
        [Parameter(Mandatory = $true)][string]$LogDirectory
    )

    $candidate = Get-V1InboundSignalingCandidate `
        -CandidatePath ([string]$Transaction.candidate_path)
    $probe = Join-Path $candidate.root 'transport_probe.exe'
    $newInf = [string]$Transaction.installed_inf
    $previousInf = [string]$Transaction.previous_inf
    if (-not [string]::IsNullOrWhiteSpace($newInf) -and
        -not $newInf.Equals($previousInf,
            [StringComparison]::OrdinalIgnoreCase)) {
        $remove = Invoke-LegacyPnpUtil -Arguments @(
                '/delete-driver', $newInf, '/uninstall', '/force') `
            -LogPath (Join-Path $LogDirectory "remove-$newInf.log")
        if ($remove.reboot_required) {
            throw 'Removing the inbound-signaling package unexpectedly requires a reboot.'
        }
    }
    $backupInf = [string]$Transaction.previous_driver_backup_inf
    if (-not (Test-Path -LiteralPath $backupInf -PathType Leaf)) {
        throw "The previous LdacNative backup INF is missing: $backupInf"
    }
    $restore = Invoke-LegacyPnpUtil -Arguments @(
            '/add-driver', $backupInf, '/install') `
        -LogPath (Join-Path $LogDirectory 'restore-previous-driver.log') `
        -AcceptedExitCodes @(0, 259, 3010)
    if ($restore.reboot_required) {
        throw 'Restoring the previous LdacNative package unexpectedly requires a reboot.'
    }
    $restart = Invoke-LegacyPnpUtil -Arguments @(
            '/restart-device', [string]$Transaction.device_instance_id) `
        -LogPath (Join-Path $LogDirectory 'restart-previous-driver.log')
    if ($restart.reboot_required) {
        throw 'Restarting the restored A2DP service PDO unexpectedly requires a reboot.'
    }
    $restored = Wait-LegacyXm5A2dpService -ExpectedService 'LdacNative' `
        -TimeoutSeconds 30
    if ($null -eq $restored -or [int]$restored.problem_code -ne 0 -or
        -not ([string]$restored.published_inf).Equals(
            $previousInf, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The previous LdacNative binding was not restored exactly.'
    }
    $info = Wait-V1InboundTransportInfo -ProbePath $probe `
        -ExpectedFlags ([uint32]$Transaction.previous_ready_flags) `
        -TimeoutSeconds 20
    if ($null -eq $info) {
        throw 'The previous LdacNative ABI/ready flags did not return after rollback.'
    }
    return $restored
}
