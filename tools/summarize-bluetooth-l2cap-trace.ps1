# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [string]$RemoteAddress
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-U16([byte[]]$Bytes, [int]$Offset) {
    return [int]$Bytes[$Offset] -bor ([int]$Bytes[$Offset + 1] -shl 8)
}

function Convert-HexBytes([string]$Hex) {
    if ([string]::IsNullOrWhiteSpace($Hex) -or
        ($Hex.Length % 2) -ne 0 -or $Hex -notmatch '^[0-9A-Fa-f]+$') {
        return $null
    }
    $bytes = [byte[]]::new($Hex.Length / 2)
    for ($index = 0; $index -lt $bytes.Length; ++$index) {
        $bytes[$index] = [Convert]::ToByte($Hex.Substring($index * 2, 2), 16)
    }
    return $bytes
}

function Get-ConnectionRequestOutcomes {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [object[]]$Requests,
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [object[]]$Commands,
        [Parameter(Mandatory = $true)]
        [ValidateSet('inbound', 'outbound')]
        [string]$ResponseDirection
    )

    $orderedRequests = @($Requests | Sort-Object {
        [datetime]$_.time_utc
    })
    $outcomes = @()
    for ($requestIndex = 0; $requestIndex -lt
            $orderedRequests.Count; ++$requestIndex) {
        $request = $orderedRequests[$requestIndex]
        $nextRequestTime = $null
        for ($laterIndex = $requestIndex + 1; $laterIndex -lt
                $orderedRequests.Count; ++$laterIndex) {
            if ($orderedRequests[$laterIndex].identifier -eq
                $request.identifier -and
                $orderedRequests[$laterIndex].connection_handle -eq
                $request.connection_handle) {
                $nextRequestTime =
                    [datetime]$orderedRequests[$laterIndex].time_utc
                break
            }
        }
        $responses = @($Commands | Where-Object {
            $_.direction -eq $ResponseDirection -and $_.code -eq 3 -and
            $_.identifier -eq $request.identifier -and
            $_.connection_handle -eq $request.connection_handle -and
            [datetime]$_.time_utc -ge [datetime]$request.time_utc -and
            ($null -eq $nextRequestTime -or
                [datetime]$_.time_utc -lt $nextRequestTime)
        })
        $pending = @($responses | Where-Object { $_.result -eq 1 })
        $terminal = @($responses | Where-Object { $_.result -ne 1 } |
            Sort-Object { [datetime]$_.time_utc })
        $last = if ($terminal.Count -gt 0) { $terminal[-1] } else { $null }
        $statusName = if ($null -eq $last) { 'unresolved' } else {
            switch ([int]$last.result) {
                0 { 'success' }
                2 { 'psm_not_supported' }
                3 { 'security_block' }
                4 { 'no_resources' }
                default { 'rejected' }
            }
        }
        $outcomes += [pscustomobject][ordered]@{
            direction = [string]$request.direction
            connection_handle = [int]$request.connection_handle
            psm = [int]$request.psm
            identifier = [int]$request.identifier
            source_cid = [int]$request.source_cid
            request_time_utc = [string]$request.time_utc
            pending_response_count = $pending.Count
            terminal_result = if ($null -eq $last) {
                $null
            } else { [int]$last.result }
            terminal_status = if ($null -eq $last) {
                $null
            } else { [int]$last.status }
            terminal_time_utc = if ($null -eq $last) {
                $null
            } else { [string]$last.time_utc }
            status = $statusName
        }
    }
    return @($outcomes)
}

$input = [System.IO.Path]::GetFullPath($InputPath)
$output = [System.IO.Path]::GetFullPath($OutputPath)
$normalizedRemoteAddress = $RemoteAddress.Replace(':', '').Replace('-', '')
if (-not [string]::IsNullOrWhiteSpace($normalizedRemoteAddress) -and
    $normalizedRemoteAddress -notmatch '^[0-9A-Fa-f]{12}$') {
    throw 'RemoteAddress must contain exactly 12 hexadecimal digits.'
}
$normalizedRemoteAddress = $normalizedRemoteAddress.ToUpperInvariant()
$commands = @()
$connections = @()
foreach ($line in @(Get-Content -LiteralPath $input)) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    try { [xml]$event = $line } catch { continue }
    $eventNode = $event.DocumentElement
    if ($null -eq $eventNode) { continue }
    $eventData = $eventNode.SelectSingleNode(
        "*[local-name()='EventData']")
    if ($null -eq $eventData) { continue }
    $data = @{}
    foreach ($item in @($eventData.SelectNodes(
                "*[local-name()='Data']"))) {
        $data[[string]$item.GetAttribute('Name')] = [string]$item.InnerText
    }
    if (-not $data.ContainsKey('BIP_Type') -or
        -not $data.ContainsKey('BIP_Data')) { continue }
    $type = [int]$data['BIP_Type']
    [byte[]]$bytes = Convert-HexBytes $data['BIP_Data']
    if ($null -eq $bytes) { continue }
    if ($type -eq 2 -and $bytes.Length -ge 13 -and
        $bytes[0] -eq 3 -and $bytes[1] -eq 11) {
        $addressParts = @()
        for ($addressIndex = 10; $addressIndex -ge 5; --$addressIndex) {
            $addressParts += $bytes[$addressIndex].ToString('X2')
        }
        $connections += [pscustomobject][ordered]@{
            time_utc = [string]$eventNode.SelectSingleNode(
                "*[local-name()='System']/*[local-name()='TimeCreated']").GetAttribute(
                    'SystemTime')
            status = [int]$bytes[2]
            connection_handle = (Get-U16 $bytes 3) -band 0x0FFF
            remote_address = $addressParts -join ''
            link_type = [int]$bytes[11]
            encryption_enabled = [int]$bytes[12]
        }
        continue
    }
    if ($type -ne 3 -and $type -ne 4) { continue }
    if ($null -eq $bytes -or $bytes.Length -lt 12) { continue }
    $connectionHandle = (Get-U16 $bytes 0) -band 0x0FFF
    $aclLength = Get-U16 $bytes 2
    $l2capLength = Get-U16 $bytes 4
    $cid = Get-U16 $bytes 6
    if ($aclLength -lt 8 -or $l2capLength -lt 4 -or $cid -ne 1) { continue }
    $offset = 8
    $limit = [Math]::Min($bytes.Length, 8 + $l2capLength)
    while ($offset + 4 -le $limit) {
        $code = [int]$bytes[$offset]
        $identifier = [int]$bytes[$offset + 1]
        $length = Get-U16 $bytes ($offset + 2)
        $payload = $offset + 4
        if ($payload + $length -gt $limit) { break }
        $entry = [ordered]@{
            time_utc = [string]$eventNode.SelectSingleNode(
                "*[local-name()='System']/*[local-name()='TimeCreated']").GetAttribute(
                    'SystemTime')
            direction = if ($type -eq 4) { 'outbound' } else { 'inbound' }
            connection_handle = $connectionHandle
            code = $code
            identifier = $identifier
            payload_length = $length
            psm = 0
            source_cid = 0
            destination_cid = 0
            result = 0
            status = 0
        }
        if ($code -eq 2 -and $length -ge 4) {
            $entry.psm = Get-U16 $bytes $payload
            $entry.source_cid = Get-U16 $bytes ($payload + 2)
        } elseif ($code -eq 3 -and $length -ge 8) {
            $entry.destination_cid = Get-U16 $bytes $payload
            $entry.source_cid = Get-U16 $bytes ($payload + 2)
            $entry.result = Get-U16 $bytes ($payload + 4)
            $entry.status = Get-U16 $bytes ($payload + 6)
        }
        $commands += [pscustomobject]$entry
        $offset = $payload + $length
    }
}

$allCommandCount = $commands.Count
$targetConnections = @(if ([string]::IsNullOrWhiteSpace(
        $normalizedRemoteAddress)) {
    $connections | Where-Object { $_.status -eq 0 }
} else {
    $connections | Where-Object {
        $_.status -eq 0 -and
        $_.remote_address -ceq $normalizedRemoteAddress
    }
})
$targetHandles = @($targetConnections | ForEach-Object {
    [int]$_.connection_handle
} | Sort-Object -Unique)
if (-not [string]::IsNullOrWhiteSpace($normalizedRemoteAddress)) {
    $commands = @($commands | Where-Object {
        [int]$_.connection_handle -in $targetHandles
    })
}

$avdtpOutbound = @($commands | Where-Object {
    $_.direction -eq 'outbound' -and $_.code -eq 2 -and $_.psm -eq 25
})
$avdtpInbound = @($commands | Where-Object {
    $_.direction -eq 'inbound' -and $_.code -eq 2 -and $_.psm -eq 25
})
$avctpOutbound = @($commands | Where-Object {
    $_.direction -eq 'outbound' -and $_.code -eq 2 -and $_.psm -eq 23
})
$avctpInbound = @($commands | Where-Object {
    $_.direction -eq 'inbound' -and $_.code -eq 2 -and $_.psm -eq 23
})
$sdpOutbound = @($commands | Where-Object {
    $_.direction -eq 'outbound' -and $_.code -eq 2 -and $_.psm -eq 1
})
$sdpInbound = @($commands | Where-Object {
    $_.direction -eq 'inbound' -and $_.code -eq 2 -and $_.psm -eq 1
})
$outboundRequestIds = @($avdtpOutbound | ForEach-Object { $_.identifier })
$inboundRequestIds = @($avdtpInbound | ForEach-Object { $_.identifier })
$noResources = @($commands | Where-Object {
    $_.direction -eq 'inbound' -and $_.code -eq 3 -and
    $_.identifier -in $outboundRequestIds -and $_.result -eq 4
})
$inboundPending = @($commands | Where-Object {
    $_.direction -eq 'outbound' -and $_.code -eq 3 -and
    $_.identifier -in $inboundRequestIds -and $_.result -eq 1
})
$inboundSuccess = @($commands | Where-Object {
    $_.direction -eq 'outbound' -and $_.code -eq 3 -and
    $_.identifier -in $inboundRequestIds -and $_.result -eq 0
})
$inboundRejected = @($commands | Where-Object {
    $_.direction -eq 'outbound' -and $_.code -eq 3 -and
    $_.identifier -in $inboundRequestIds -and $_.result -notin @(0, 1)
})
$inboundRequestOutcomes = @(Get-ConnectionRequestOutcomes `
    -Requests $avdtpInbound -Commands $commands `
    -ResponseDirection 'outbound')
$inboundAccepted = @($inboundRequestOutcomes | Where-Object { $_.status -eq 'success' })
$inboundPsmNotSupported = @($inboundRequestOutcomes | Where-Object {
    $_.status -eq 'psm_not_supported'
})
$firstAcceptedTime = if ($inboundAccepted.Count -gt 0) {
    [datetime](($inboundAccepted | Sort-Object {
        [datetime]$_.terminal_time_utc
    })[0].terminal_time_utc)
} else { $null }
$postSuccessPsmNotSupported = @($inboundPsmNotSupported | Where-Object {
    $null -ne $firstAcceptedTime -and
    [datetime]$_.request_time_utc -gt $firstAcceptedTime
})
$unresolvedInbound = @($inboundRequestOutcomes | Where-Object {
    $_.status -eq 'unresolved'
})
$outboundAvctpRequestOutcomes = @(Get-ConnectionRequestOutcomes `
    -Requests $avctpOutbound -Commands $commands `
    -ResponseDirection 'inbound')
$outboundAvctpAccepted = @($outboundAvctpRequestOutcomes |
    Where-Object { $_.status -eq 'success' })
$outboundAvctpRejected = @($outboundAvctpRequestOutcomes |
    Where-Object { $_.status -notin @('success', 'unresolved') })
$outboundAvctpUnresolved = @($outboundAvctpRequestOutcomes |
    Where-Object { $_.status -eq 'unresolved' })
$inboundAvctpRequestOutcomes = @(Get-ConnectionRequestOutcomes `
    -Requests $avctpInbound -Commands $commands `
    -ResponseDirection 'outbound')
$inboundAvctpAccepted = @($inboundAvctpRequestOutcomes |
    Where-Object { $_.status -eq 'success' })
$inboundAvctpRejected = @($inboundAvctpRequestOutcomes |
    Where-Object { $_.status -notin @('success', 'unresolved') })
$inboundAvctpUnresolved = @($inboundAvctpRequestOutcomes |
    Where-Object { $_.status -eq 'unresolved' })
$summary = [ordered]@{
    schema_version = 2
    input = $input
    target_remote_address = [string]$normalizedRemoteAddress
    target_connection_handles = @($targetHandles)
    target_connection_complete_count = $targetConnections.Count
    target_handle_mapping_complete =
        [string]::IsNullOrWhiteSpace($normalizedRemoteAddress) -or
        $targetHandles.Count -gt 0
    all_l2cap_signaling_command_count = $allCommandCount
    l2cap_signaling_command_count = $commands.Count
    outbound_sdp_connection_requests = $sdpOutbound.Count
    inbound_sdp_connection_requests = $sdpInbound.Count
    outbound_avctp_connection_requests = $avctpOutbound.Count
    inbound_avctp_connection_requests = $avctpInbound.Count
    outbound_avctp_successful_connections = $outboundAvctpAccepted.Count
    outbound_avctp_rejected_connections = $outboundAvctpRejected.Count
    outbound_avctp_unresolved_connections = $outboundAvctpUnresolved.Count
    outbound_avctp_request_outcomes = @($outboundAvctpRequestOutcomes)
    inbound_avctp_successful_connections = $inboundAvctpAccepted.Count
    inbound_avctp_rejected_connections = $inboundAvctpRejected.Count
    inbound_avctp_unresolved_connections = $inboundAvctpUnresolved.Count
    inbound_avctp_request_outcomes = @($inboundAvctpRequestOutcomes)
    outbound_avdtp_connection_requests = $avdtpOutbound.Count
    inbound_avdtp_connection_requests = $avdtpInbound.Count
    inbound_no_resources_responses = $noResources.Count
    outbound_pending_responses_to_inbound_avdtp = $inboundPending.Count
    outbound_success_responses_to_inbound_avdtp = $inboundSuccess.Count
    outbound_rejections_to_inbound_avdtp = $inboundRejected.Count
    inbound_avdtp_pending_without_success = $unresolvedInbound.Count -gt 0
    inbound_avdtp_unresolved_requests = $unresolvedInbound.Count
    inbound_avdtp_request_outcomes = @($inboundRequestOutcomes)
    inbound_avdtp_psm_not_supported_responses = $inboundPsmNotSupported.Count
    inbound_avdtp_psm_not_supported_after_success =
        $postSuccessPsmNotSupported.Count
    inbound_avdtp_collision_observed = $avdtpInbound.Count -gt 0
    commands = @($commands)
}
$parent = Split-Path -Parent $output
if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
}
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $output `
    -Encoding UTF8
$summary | ConvertTo-Json -Depth 5
