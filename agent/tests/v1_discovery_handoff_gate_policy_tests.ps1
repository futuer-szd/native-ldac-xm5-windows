# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    $path = Join-Path $projectRoot $RelativePath
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        $path,
        [ref]$tokens,
        [ref]$errors)
    if ($errors.Count -ne 0) {
        throw "PowerShell parse failure in ${RelativePath}: $($errors[0].Message)"
    }
    return Get-Content -LiteralPath $path -Raw
}

$retired = Read-ProjectFile `
    'tools\run-v1-discovery-handoff-gate.ps1'
$build = Read-ProjectFile `
    'tools\build-v1-reboot-discovery-candidate.ps1'
$common = Read-ProjectFile `
    'tools\v1-reboot-discovery-common.ps1'
$prepare = Read-ProjectFile `
    'tools\prepare-v1-reboot-discovery-gate.ps1'
$postReboot = Read-ProjectFile `
    'tools\run-v1-post-reboot-discovery-gate.ps1'
$rollback = Read-ProjectFile `
    'tools\rollback-v1-reboot-discovery-gate.ps1'
$prepareAlias = Read-ProjectFile `
    'tools\prepare-v1-zero-packet-gate.ps1'
$postAlias = Read-ProjectFile `
    'tools\run-v1-post-reboot-zero-packet-gate.ps1'
$rollbackAlias = Read-ProjectFile `
    'tools\rollback-v1-zero-packet-gate.ps1'
$completion = Read-ProjectFile `
    'tools\complete-v1-zero-packet-gate.ps1'

foreach ($required in @(
        'same-boot driver handoff gate is retired',
        'Win32 71',
        'rollback-v1-discovery-handoff.ps1',
        'prepare-v1-reboot-discovery-gate.ps1')) {
    if (-not $retired.Contains($required)) {
        throw "Retired V1 handoff gate is missing: $required"
    }
}
foreach ($forbidden in @(
        'Stop-Service',
        'Start-Service',
        'Set-Service',
        'Import-Certificate',
        'Invoke-LegacyPnpUtil',
        'v1_presence_agent.exe')) {
    if ($retired.Contains($forbidden)) {
        throw "Retired V1 handoff gate can still mutate: $forbidden"
    }
}

foreach ($required in @(
        'persistent_LdacNative_function_driver_architecture',
        'cross_boot_driver_activation',
        'Bluetooth_radio_ready_precondition',
        'maximum_three_signaling_open_attempts_per_ACL_generation',
        'DISCOVER_and_capabilities_before_configuration',
        'distinct_capabilities_discovered_event',
        'retry_only_OpenSignaling_Win32_71',
        'retry_backoff_15s_30s',
        'cancel_retry_on_ACL_or_RenderStop',
        'SET_CONFIGURATION_then_AVDTP_OPEN',
        'open_media_L2CAP_then_immediate_AVDTP_CLOSE',
        'no_AVDTP_START',
        'no_media_payload',
        'restore_original_A2DP_on_failure',
        'no_Bluetooth_toggle')) {
    if (-not $build.Contains($required) -or
        -not $common.Contains($required)) {
        throw "V1 reboot discovery contract is missing: $required"
    }
}
if (-not $build.Contains('transport_policy_version = 3') -or
    -not $common.Contains(
        '[int]$manifest.transport_policy_version -ne 3')) {
    throw 'V1 reboot transport policy version is not frozen at 3.'
}
if (-not $build.Contains('v1_transport_configuration_worker.exe')) {
    throw 'V1 reboot zero-packet candidate omits its contained worker.'
}

foreach ($required in @(
        'ConfirmV1RebootPreparation',
        'ConfirmPinImpactAndReboot',
        "rev-parse",
        "':driver'",
        'Test-V1RebootDiscoveryEvidence',
        'Test-V1CapabilityPrerequisiteTransaction',
        'prerequisite_capability_transaction',
        'zero-packet configuration gate requires one verified capability-only hardware result',
        'zero-packet transport policy already failed',
        'invalidate Windows Hello PIN state',
        'Get-NativeLdacBluetoothRadioState',
        'original_binding_healthy',
        "Get-NativeLdacXm5BluetoothState",
        "Set-Service -Name 'AltA2dpSVC' -StartupType Manual",
        "-ExpectedService 'LdacNative'",
        "status = 'awaiting-reboot'",
        "phase = 'installed-awaiting-reboot'",
        'prepared_boot_time_utc',
        'Restore-V1RebootDiscoveryOriginalA2dp')) {
    if (-not $prepare.Contains($required) -and
        -not $common.Contains($required)) {
        throw "V1 reboot preparation is missing: $required"
    }
}
$prepareConfirmation = $prepare.IndexOf('$PSCmdlet.ShouldProcess')
foreach ($mutation in @(
        'Stop-Service',
        'Import-Certificate',
        'Invoke-LegacyPnpUtil')) {
    if ($prepare.IndexOf($mutation) -le $prepareConfirmation) {
        throw "V1 reboot preparation mutates before confirmation: $mutation"
    }
}

foreach ($required in @(
        'ConfirmV1PostRebootDiscovery',
        '$currentBoot -le $preparedBoot.AddSeconds(1)',
        'Get-NativeLdacBluetoothRadioState',
        "XM5 must be powered off at post-reboot preflight",
        '--exercise-transport-configuration',
        '--transport-result',
        'capabilities_discovered_events -eq 1',
        'configuration_sessions_completed -eq 1',
        'media_started_events -eq 0',
        'transport_retryable_failures',
        'transport_retries_scheduled',
        'transport_retry_budget_exhausted -eq 0',
        '$completedAttempts -gt 3',
        '$Session.open_attempts -eq 1',
        '$Session.close_succeeded -eq $true',
        'Restore-V1RebootDiscoveryOriginalA2dp',
        "status = 'configuration-verified'",
        'actual_media_channel_opened = 1',
        'actual_avdtp_start_commands = 0',
        'actual_media_packets_sent = 0',
        'reboot_count = 1',
        'bluetooth_toggled = $false',
        'Only OpenSignaling Win32 71 may retry',
        'media_start_commands',
        'media_packets_written')) {
    if (-not $postReboot.Contains($required) -and
        -not $common.Contains($required)) {
        throw "V1 post-reboot discovery gate is missing: $required"
    }
}
foreach ($required in @(
        'Test-V1RebootDiscoveryEvidence',
        'Test-V1RebootConfigurationEvidence',
        'Test-V1RebootConfigurationCoreEvidence',
        'transport_open_attempts_for_generation -eq 0',
        'transport_open_executed',
        'strictly_retryable_open_failure')) {
    if (-not $common.Contains($required)) {
        throw "V1 discovery evidence validator is missing: $required"
    }
}
foreach ($required in @(
        'ConfirmV1ZeroPacketCompletion',
        'Test-V1RebootConfigurationCoreEvidence',
        'current_physical_disconnect_verified',
        'finalized_after_agent_deadline',
        "status = 'configuration-verified'",
        'leaves LdacNative installed')) {
    if (-not $completion.Contains($required)) {
        throw "V1 late zero-packet completion is missing: $required"
    }
}
if ($postReboot.Contains(
        'state.transport_open_attempts_for_generation -eq')) {
    throw 'The post-reboot gate still compares a reset generation-local counter directly.'
}
$postConfirmation = $postReboot.IndexOf('$PSCmdlet.ShouldProcess')
$agentIndex = $postReboot.IndexOf('& $agentPath')
if ($postConfirmation -lt 0 -or $agentIndex -le $postConfirmation) {
    throw 'The post-reboot gate can authorize transport before confirmation.'
}

foreach ($required in @(
        'ConfirmV1RebootDiscoveryRollback',
        'Get-NativeLdacXm5BluetoothState',
        'Restore-V1RebootDiscoveryOriginalA2dp',
        "status = 'rollback-verified'",
        'no reboot or Windows Bluetooth toggle')) {
    if (-not $rollback.Contains($required)) {
        throw "V1 reboot discovery rollback is missing: $required"
    }
}
foreach ($script in @($prepare, $postReboot, $rollback)) {
    foreach ($forbidden in @(
            'Restart-Computer',
            'shutdown.exe',
            'Restart-Service',
            'BluetoothEnableIncomingConnections',
            'BluetoothEnableDiscovery',
            'transport_probe.exe',
            '--media-session',
            '--play-endpoint',
            'Start-Sleep')) {
        if ($script.IndexOf(
                $forbidden,
                [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "V1 reboot discovery script exceeds scope: $forbidden"
        }
    }
}
foreach ($aliasContract in @(
        [pscustomobject]@{
            text = $prepareAlias
            confirm = 'ConfirmV1ZeroPacketPreparation'
            implementation = 'prepare-v1-reboot-discovery-gate.ps1'
        },
        [pscustomobject]@{
            text = $postAlias
            confirm = 'ConfirmV1ZeroPacketGate'
            implementation = 'run-v1-post-reboot-discovery-gate.ps1'
        },
        [pscustomobject]@{
            text = $rollbackAlias
            confirm = 'ConfirmV1ZeroPacketRollback'
            implementation = 'rollback-v1-reboot-discovery-gate.ps1'
        })) {
    if (-not $aliasContract.text.Contains($aliasContract.confirm) -or
        -not $aliasContract.text.Contains($aliasContract.implementation)) {
        throw "V1 zero-packet command alias is incomplete: $($aliasContract.confirm)"
    }
}

Write-Host 'V1 cross-reboot discovery gate policy tests passed.'
