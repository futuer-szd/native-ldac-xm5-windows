# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'v1-inbound-signaling-common.ps1')

$script:V1InboundPnpRundownPolicyVersion = 14
$script:V1InboundPnpApprovedDriverTree =
    '85a0b46231ae2f3212e6616346e2d6905314f0ff'
$script:V1InboundPnpRejectedDriverTree =
    '6b4790f254149679f4c7e1e239ffc752f90c54ce'
$script:V1InboundPnpRejectedSourceCommit =
    '77a5f5e5f8798e3e1ecaf0c9e785b301352cd03a'

function Get-V1InboundPnpRundownCandidate {
    param([Parameter(Mandatory = $true)][string]$CandidatePath)

    $candidate = Get-V1InboundSignalingCandidate `
        -CandidatePath $CandidatePath
    $manifest = $candidate.manifest
    $capabilities = @($manifest.capabilities | ForEach-Object {
        [string]$_
    })
    $required = @(
        'self_managed_io_suspend_server_rundown',
        'self_managed_io_restart_server_registration',
        'failed_unregister_preserves_server_handle',
        'one_shot_inbound_listener_rundown',
        'atomic_connected_and_rundown_publish',
        'delayed_pnp_failure_observation',
        'known_code38_single_reboot_recovery',
        'activation_requires_one_windows_restart',
        'two_cycle_discover_only_pnp_validation')
    if ([string]$manifest.driver_tree -eq
            $script:V1InboundPnpRejectedDriverTree -or
        [string]$manifest.driver_tree -ne
            $script:V1InboundPnpApprovedDriverTree -or
        @($required | Where-Object { $_ -notin $capabilities }).Count -ne
            0) {
        throw 'The V1 inbound PnP-rundown candidate is not the approved driver tree or lacks its lifecycle contract.'
    }
    return $candidate
}

function Test-V1InboundPnpKnownCode38RecoveryTransaction {
    param([Parameter(Mandatory = $true)]$Transaction)

    $cycles = @($Transaction.cycles)
    return [int]$Transaction.schema_version -eq 1 -and
        [int]$Transaction.transport_policy_version -eq 13 -and
        [string]$Transaction.source_commit -eq
            $script:V1InboundPnpRejectedSourceCommit -and
        [string]$Transaction.driver_tree -eq
            $script:V1InboundPnpRejectedDriverTree -and
        [string]$Transaction.status -eq 'rollback-required' -and
        [string]$Transaction.phase -eq
            'validation-failed-xm5-disconnected' -and
        $Transaction.rollback.attempted -eq $false -and
        -not [string]::IsNullOrWhiteSpace(
            [string]$Transaction.device_instance_id) -and
        -not [string]::IsNullOrWhiteSpace(
            [string]$Transaction.selected_inf) -and
        $cycles.Count -eq 1 -and
        $cycles[0].passed -eq $false -and
        [int]$cycles[0].binding_after_disconnect.problem_code -eq 38 -and
        [string]$cycles[0].binding_after_disconnect.published_inf -eq
            [string]$Transaction.selected_inf -and
        [string]$cycles[0].failure -eq 'Cycle 1 left the PDO in Code 38.'
}

function Test-V1InboundPnpPrerequisite {
    param(
        [Parameter(Mandatory = $true)]$Transaction,
        [Parameter(Mandatory = $true)][string]$TransactionPath,
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)][string]$ResultPath
    )

    return [int]$Transaction.schema_version -eq 1 -and
        [int]$Transaction.transport_policy_version -eq
            $script:V1InboundSignalingPolicyVersion -and
        [string]$Transaction.status -eq 'inbound-discovery-verified' -and
        $Transaction.rollback.attempted -eq $false -and
        $Transaction.validation.passed -eq $true -and
        [string]$Transaction.validation.result -eq $ResultPath -and
        [int]$Result.schema_version -eq 1 -and
        [int]$Result.transport_policy_version -eq
            $script:V1InboundSignalingPolicyVersion -and
        $Result.passed -eq $true -and
        $Result.core_passed -eq $true -and
        [int]$Result.outbound_avdtp_connection_requests -eq 0 -and
        [int]$Result.inbound_no_resources_responses -eq 0 -and
        [int]$Result.set_configuration_commands -eq 0 -and
        [int]$Result.avdtp_start_commands -eq 0 -and
        [int]$Result.media_packets -eq 0 -and
        [string]$Result.transaction -eq $TransactionPath
}

function Get-V1InboundPnpKernelFailureEvents {
    param([Parameter(Mandatory = $true)][datetime]$StartTime)

    $events = @(Get-WinEvent -FilterHashtable @{
        LogName = 'System'
        ProviderName = 'Microsoft-Windows-Kernel-PnP'
        Id = 219
        StartTime = $StartTime
    } -ErrorAction SilentlyContinue)
    $matches = foreach ($event in $events) {
        $xml = [string]$event.ToXml()
        $message = [string]$event.Message
        $text = $xml + [Environment]::NewLine + $message
        if ($text -match '(?i)LdacNative' -and
            $text -match '(?i)(C000038E|DRIVER_FAILED_PRIOR_UNLOAD)') {
            [pscustomobject][ordered]@{
                time_created = $event.TimeCreated.ToString('o')
                id = [int]$event.Id
                record_id = [long]$event.RecordId
                xml = $xml
                message = $message
            }
        }
    }
    return @($matches)
}

function Test-V1InboundPnpCycleEvidence {
    param(
        [Parameter(Mandatory = $true)]$Cycle,
        [Parameter(Mandatory = $true)]$Summary
    )

    return $Cycle.acl_connect_observed -eq $true -and
        $Cycle.acl_disconnect_observed -eq $true -and
        $Cycle.public_disconnect_observed -eq $true -and
        $Cycle.binding_on_connect_healthy -eq $true -and
        [int]$Cycle.discover_exit_code -eq 0 -and
        [string]$Cycle.discover_text -match
            '(?m)^Selected LDAC audio sink SEID:\s*\d+\s*\r?$' -and
        [string]$Cycle.discover_text -match
            '(?m)^Signaling channel closed\.\r?$' -and
        [string]$Cycle.open_diagnostic -match
            '(?m)^Signaling channel direction: inbound\.\r?$' -and
        [string]$Cycle.open_diagnostic -match
            '(?m)^L2CAP OPEN state: completed, succeeded\.\r?$' -and
        [int]$Summary.inbound_avdtp_connection_requests -ge 1 -and
        [int]$Summary.outbound_avdtp_connection_requests -eq 0 -and
        [int]$Summary.outbound_success_responses_to_inbound_avdtp -ge 1 -and
        [int]$Summary.outbound_rejections_to_inbound_avdtp -eq
            [int]$Summary.inbound_avdtp_psm_not_supported_after_success -and
        [int]$Summary.inbound_avdtp_unresolved_requests -eq 0 -and
        [int]$Summary.inbound_no_resources_responses -eq 0 -and
        $Summary.inbound_avdtp_pending_without_success -eq $false -and
        [int]$Cycle.code38_event_count -eq 0
}

function Get-V1InboundPnpCurrentBootTime {
    return (Get-NativeLdacCurrentBootTime).ToString('o')
}
