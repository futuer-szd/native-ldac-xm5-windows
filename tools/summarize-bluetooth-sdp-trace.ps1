# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [string]$RemoteAddress
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:SdpUuidNames = @{
    '0003' = 'rfcomm'
    '0019' = 'avdtp'
    '0100' = 'l2cap'
    '110A' = 'audio-source'
    '110C' = 'av-remote-control-target'
    '110D' = 'advanced-audio-distribution'
    '110E' = 'av-remote-control'
    '110F' = 'av-remote-control-controller'
    '111E' = 'handsfree'
    '111F' = 'handsfree-audio-gateway'
}

function Get-U16Le([byte[]]$Bytes, [int]$Offset) {
    return [int]$Bytes[$Offset] -bor ([int]$Bytes[$Offset + 1] -shl 8)
}

function Get-U16Be([byte[]]$Bytes, [int]$Offset) {
    return ([int]$Bytes[$Offset] -shl 8) -bor [int]$Bytes[$Offset + 1]
}

function Get-U32Be([byte[]]$Bytes, [int]$Offset) {
    return ([long]$Bytes[$Offset] -shl 24) -bor
        ([long]$Bytes[$Offset + 1] -shl 16) -bor
        ([long]$Bytes[$Offset + 2] -shl 8) -bor
        [long]$Bytes[$Offset + 3]
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

function Get-HexRange {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][int]$Offset,
        [Parameter(Mandatory = $true)][int]$Count
    )

    if ($Count -le 0) { return '' }
    return [Convert]::ToHexString(
        [byte[]]$Bytes[$Offset..($Offset + $Count - 1)])
}

function Get-SdpUuidName([string]$Uuid) {
    $normalized = $Uuid.ToUpperInvariant()
    if ($script:SdpUuidNames.ContainsKey($normalized)) {
        return [string]$script:SdpUuidNames[$normalized]
    }
    return "uuid-$normalized"
}

function Read-SdpDataElement {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][int]$Offset,
        [Parameter(Mandatory = $true)][int]$Limit
    )

    if ($Offset -lt 0 -or $Offset -ge $Limit -or $Limit -gt $Bytes.Length) {
        return $null
    }
    $start = $Offset
    $header = [int]$Bytes[$Offset]
    $typeCode = ($header -shr 3) -band 0x1F
    $sizeIndex = $header -band 7
    $cursor = $Offset + 1
    $size = 0
    switch ($sizeIndex) {
        0 { $size = 1 }
        1 { $size = 2 }
        2 { $size = 4 }
        3 { $size = 8 }
        4 { $size = 16 }
        5 {
            if ($cursor + 1 -gt $Limit) { return $null }
            $size = [int]$Bytes[$cursor]
            ++$cursor
        }
        6 {
            if ($cursor + 2 -gt $Limit) { return $null }
            $size = Get-U16Be $Bytes $cursor
            $cursor += 2
        }
        7 {
            if ($cursor + 4 -gt $Limit) { return $null }
            $longSize = Get-U32Be $Bytes $cursor
            if ($longSize -gt [int]::MaxValue) { return $null }
            $size = [int]$longSize
            $cursor += 4
        }
    }
    $valueOffset = $cursor
    $nextOffset = $valueOffset + $size
    if ($size -lt 0 -or $nextOffset -gt $Limit) { return $null }

    $children = @()
    $childrenComplete = $true
    if ($typeCode -in @(6, 7)) {
        $childOffset = $valueOffset
        while ($childOffset -lt $nextOffset) {
            $child = Read-SdpDataElement `
                -Bytes $Bytes -Offset $childOffset -Limit $nextOffset
            if ($null -eq $child -or
                [int]$child.next_offset -le $childOffset) {
                $childrenComplete = $false
                break
            }
            $children += $child
            $childOffset = [int]$child.next_offset
        }
        if ($childOffset -ne $nextOffset) { $childrenComplete = $false }
    }

    $integerValue = $null
    if ($typeCode -eq 1) {
        if ($size -eq 1) {
            $integerValue = [long]$Bytes[$valueOffset]
        } elseif ($size -eq 2) {
            $integerValue = [long](Get-U16Be $Bytes $valueOffset)
        } elseif ($size -eq 4) {
            $integerValue = Get-U32Be $Bytes $valueOffset
        }
    }
    $uuid = $null
    if ($typeCode -eq 3 -and $size -in @(2, 4, 16)) {
        $uuid = Get-HexRange -Bytes $Bytes `
            -Offset $valueOffset -Count $size
    }

    [pscustomobject][ordered]@{
        type_code = $typeCode
        type_name = switch ($typeCode) {
            0 { 'nil' }
            1 { 'unsigned-integer' }
            2 { 'signed-integer' }
            3 { 'uuid' }
            4 { 'text-string' }
            5 { 'boolean' }
            6 { 'sequence' }
            7 { 'alternative' }
            8 { 'url' }
            default { 'reserved' }
        }
        size = $size
        integer_value = $integerValue
        uuid = $uuid
        uuid_name = if ($null -eq $uuid) {
            $null
        } else { Get-SdpUuidName -Uuid $uuid }
        children = @($children)
        children_complete = $childrenComplete
        raw_hex = Get-HexRange -Bytes $Bytes `
            -Offset $start -Count ($nextOffset - $start)
        next_offset = $nextOffset
    }
}

function Get-SdpElementUuids {
    param([AllowNull()]$Element)

    if ($null -eq $Element) { return @() }
    $result = @()
    if (-not [string]::IsNullOrWhiteSpace([string]$Element.uuid)) {
        $result += [string]$Element.uuid
    }
    foreach ($child in @($Element.children)) {
        $result += @(Get-SdpElementUuids -Element $child)
    }
    return @($result)
}

function Get-SdpAttributes {
    param([AllowNull()]$Element)

    if ($null -eq $Element -or [int]$Element.type_code -ne 6) {
        return @()
    }
    $children = @($Element.children)
    $attributes = @()
    for ($index = 0; $index + 1 -lt $children.Count; $index += 2) {
        $id = $children[$index]
        $value = $children[$index + 1]
        if ([int]$id.type_code -ne 1 -or
            [int]$id.size -ne 2 -or $null -eq $id.integer_value) {
            continue
        }
        $uuids = @(Get-SdpElementUuids -Element $value |
            Sort-Object -Unique)
        $attributes += [pscustomobject][ordered]@{
            attribute_id = [int]$id.integer_value
            attribute_id_hex = '{0:X4}' -f [int]$id.integer_value
            uuids = @($uuids)
            uuid_names = @($uuids | ForEach-Object {
                Get-SdpUuidName -Uuid $_
            })
            value_raw_hex = [string]$value.raw_hex
        }
    }
    return @($attributes)
}

function Get-SdpAttributeSelectors {
    param([AllowNull()]$Element)

    if ($null -eq $Element -or [int]$Element.type_code -ne 6) {
        return @()
    }
    $selectors = @()
    foreach ($child in @($Element.children)) {
        if ([int]$child.type_code -ne 1 -or
            $null -eq $child.integer_value) { continue }
        if ([int]$child.size -eq 2) {
            $selectors += '{0:X4}' -f [int]$child.integer_value
        } elseif ([int]$child.size -eq 4) {
            $value = [long]$child.integer_value
            $selectors += ('{0:X4}-{1:X4}' -f
                (($value -shr 16) -band 0xFFFF),
                ($value -band 0xFFFF))
        }
    }
    return @($selectors)
}

function Convert-SdpPdu {
    param(
        [Parameter(Mandatory = $true)]$Packet,
        [Parameter(Mandatory = $true)]$Channel
    )

    [byte[]]$payload = $Packet.payload_bytes
    if (-not [bool]$Packet.payload_complete -or $payload.Length -lt 5) {
        return [pscustomobject][ordered]@{
            time_utc = [string]$Packet.time_utc
            direction = [string]$Packet.direction
            channel_id = [int]$Channel.channel_id
            pdu_id = $null
            pdu_name = 'incomplete'
            transaction_id = $null
            parameter_length = $null
            parse_complete = $false
            raw_hex = [string]$Packet.payload_hex
            service_search_uuids = @()
            service_search_uuid_names = @()
            service_record_handles = @()
            service_record_handle = $null
            attribute_selectors = @()
            attributes = @()
            response_uuids = @()
        }
    }

    $pduId = [int]$payload[0]
    $transactionId = Get-U16Be $payload 1
    $parameterLength = Get-U16Be $payload 3
    $parameterLimit = 5 + $parameterLength
    $parseComplete = $parameterLimit -le $payload.Length
    $limit = [Math]::Min($payload.Length, $parameterLimit)
    $entry = [ordered]@{
        time_utc = [string]$Packet.time_utc
        direction = [string]$Packet.direction
        channel_id = [int]$Channel.channel_id
        pdu_id = $pduId
        pdu_name = switch ($pduId) {
            1 { 'error-response' }
            2 { 'service-search-request' }
            3 { 'service-search-response' }
            4 { 'service-attribute-request' }
            5 { 'service-attribute-response' }
            6 { 'service-search-attribute-request' }
            7 { 'service-search-attribute-response' }
            default { 'unknown' }
        }
        transaction_id = $transactionId
        parameter_length = $parameterLength
        parse_complete = $parseComplete
        raw_hex = [string]$Packet.payload_hex
        service_search_uuids = @()
        service_search_uuid_names = @()
        service_record_handles = @()
        service_record_handle = $null
        attribute_selectors = @()
        attributes = @()
        response_uuids = @()
    }
    if (-not $parseComplete) { return [pscustomobject]$entry }

    if ($pduId -eq 2) {
        $pattern = Read-SdpDataElement -Bytes $payload -Offset 5 -Limit $limit
        if ($null -eq $pattern) {
            $entry.parse_complete = $false
        } else {
            $uuids = @(Get-SdpElementUuids -Element $pattern |
                Sort-Object -Unique)
            $entry.service_search_uuids = @($uuids)
            $entry.service_search_uuid_names = @($uuids | ForEach-Object {
                Get-SdpUuidName -Uuid $_
            })
        }
    } elseif ($pduId -eq 3 -and $limit -ge 9) {
        $currentCount = Get-U16Be $payload 7
        $cursor = 9
        $handles = @()
        for ($index = 0; $index -lt $currentCount -and
                $cursor + 4 -le $limit; ++$index) {
            $handles += Get-U32Be $payload $cursor
            $cursor += 4
        }
        if ($handles.Count -ne $currentCount) {
            $entry.parse_complete = $false
        }
        $entry.service_record_handles = @($handles)
    } elseif ($pduId -eq 4 -and $limit -ge 11) {
        $entry.service_record_handle = Get-U32Be $payload 5
        $selectors = Read-SdpDataElement `
            -Bytes $payload -Offset 11 -Limit $limit
        if ($null -eq $selectors) {
            $entry.parse_complete = $false
        } else {
            $entry.attribute_selectors = @(
                Get-SdpAttributeSelectors -Element $selectors)
        }
    } elseif ($pduId -in @(5, 7) -and $limit -ge 7) {
        $attributeByteCount = Get-U16Be $payload 5
        $attributeEnd = 7 + $attributeByteCount
        if ($attributeEnd -gt $limit) {
            $entry.parse_complete = $false
        } else {
            $element = Read-SdpDataElement `
                -Bytes $payload -Offset 7 -Limit $attributeEnd
            if ($null -eq $element) {
                $entry.parse_complete = $false
            } else {
                $entry.attributes = @(Get-SdpAttributes -Element $element)
                $entry.response_uuids = @(
                    Get-SdpElementUuids -Element $element |
                    Sort-Object -Unique)
            }
        }
    } elseif ($pduId -eq 6) {
        $pattern = Read-SdpDataElement -Bytes $payload -Offset 5 -Limit $limit
        if ($null -eq $pattern -or
            [int]$pattern.next_offset + 2 -gt $limit) {
            $entry.parse_complete = $false
        } else {
            $uuids = @(Get-SdpElementUuids -Element $pattern |
                Sort-Object -Unique)
            $entry.service_search_uuids = @($uuids)
            $entry.service_search_uuid_names = @($uuids | ForEach-Object {
                Get-SdpUuidName -Uuid $_
            })
            $selectorsOffset = [int]$pattern.next_offset + 2
            $selectors = Read-SdpDataElement `
                -Bytes $payload -Offset $selectorsOffset -Limit $limit
            if ($null -eq $selectors) {
                $entry.parse_complete = $false
            } else {
                $entry.attribute_selectors = @(
                    Get-SdpAttributeSelectors -Element $selectors)
            }
        }
    }
    return [pscustomobject]$entry
}

$input = [IO.Path]::GetFullPath($InputPath)
$output = [IO.Path]::GetFullPath($OutputPath)
$normalizedRemoteAddress = ([string]$RemoteAddress).
    Replace(':', '').Replace('-', '').ToUpperInvariant()
if (-not [string]::IsNullOrWhiteSpace($normalizedRemoteAddress) -and
    $normalizedRemoteAddress -notmatch '^[0-9A-F]{12}$') {
    throw 'RemoteAddress must contain exactly 12 hexadecimal digits.'
}

$connections = @()
$commands = @()
$aclPackets = @()
$continuationPackets = @()
foreach ($line in @(Get-Content -LiteralPath $input)) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    try { [xml]$event = $line } catch { continue }
    $eventNode = $event.DocumentElement
    if ($null -eq $eventNode) { continue }
    $eventData = $eventNode.SelectSingleNode("*[local-name()='EventData']")
    if ($null -eq $eventData) { continue }
    $data = @{}
    foreach ($item in @($eventData.SelectNodes("*[local-name()='Data']"))) {
        $data[[string]$item.GetAttribute('Name')] = [string]$item.InnerText
    }
    if (-not $data.ContainsKey('BIP_Type') -or
        -not $data.ContainsKey('BIP_Data')) { continue }
    $type = [int]$data['BIP_Type']
    [byte[]]$bytes = Convert-HexBytes $data['BIP_Data']
    if ($null -eq $bytes) { continue }
    $time = [string]$eventNode.SelectSingleNode(
        "*[local-name()='System']/*[local-name()='TimeCreated']").GetAttribute(
            'SystemTime')

    if ($type -eq 2 -and $bytes.Length -ge 13 -and
        $bytes[0] -eq 3 -and $bytes[1] -eq 11) {
        $addressParts = @()
        for ($addressIndex = 10; $addressIndex -ge 5; --$addressIndex) {
            $addressParts += $bytes[$addressIndex].ToString('X2')
        }
        $connections += [pscustomobject][ordered]@{
            time_utc = $time
            status = [int]$bytes[2]
            connection_handle = (Get-U16Le $bytes 3) -band 0x0FFF
            remote_address = $addressParts -join ''
        }
        continue
    }
    if ($type -notin @(3, 4) -or $bytes.Length -lt 4) { continue }
    $handleFlags = Get-U16Le $bytes 0
    $connectionHandle = $handleFlags -band 0x0FFF
    $packetBoundary = ($handleFlags -shr 12) -band 3
    $direction = $type -eq 4 ? 'outbound' : 'inbound'
    if ($packetBoundary -eq 1) {
        $continuationPackets += [pscustomobject][ordered]@{
            time_utc = $time
            direction = $direction
            connection_handle = $connectionHandle
        }
        continue
    }
    if ($bytes.Length -lt 8) { continue }
    $l2capLength = Get-U16Le $bytes 4
    $cid = Get-U16Le $bytes 6
    $available = [Math]::Max(0, $bytes.Length - 8)
    $payloadCount = [Math]::Min($available, $l2capLength)
    [byte[]]$payloadBytes = if ($payloadCount -gt 0) {
        [byte[]]$bytes[8..(8 + $payloadCount - 1)]
    } else { [byte[]]::new(0) }
    $aclPackets += [pscustomobject][ordered]@{
        time_utc = $time
        direction = $direction
        connection_handle = $connectionHandle
        cid = $cid
        payload_complete = $available -ge $l2capLength
        payload_bytes = $payloadBytes
        payload_hex = if ($payloadCount -eq 0) {
            ''
        } else { [Convert]::ToHexString($payloadBytes) }
    }

    if ($cid -ne 1 -or $l2capLength -lt 4) { continue }
    $offset = 0
    while ($offset + 4 -le $payloadBytes.Length) {
        $code = [int]$payloadBytes[$offset]
        $identifier = [int]$payloadBytes[$offset + 1]
        $length = Get-U16Le $payloadBytes ($offset + 2)
        $valueOffset = $offset + 4
        if ($valueOffset + $length -gt $payloadBytes.Length) { break }
        $entry = [ordered]@{
            time_utc = $time
            direction = $direction
            connection_handle = $connectionHandle
            code = $code
            identifier = $identifier
            psm = 0
            source_cid = 0
            destination_cid = 0
            result = 0
            status = 0
        }
        if ($code -eq 2 -and $length -ge 4) {
            $entry.psm = Get-U16Le $payloadBytes $valueOffset
            $entry.source_cid = Get-U16Le $payloadBytes ($valueOffset + 2)
        } elseif ($code -eq 3 -and $length -ge 8) {
            $entry.destination_cid = Get-U16Le $payloadBytes $valueOffset
            $entry.source_cid = Get-U16Le $payloadBytes ($valueOffset + 2)
            $entry.result = Get-U16Le $payloadBytes ($valueOffset + 4)
            $entry.status = Get-U16Le $payloadBytes ($valueOffset + 6)
        } elseif ($code -in @(6, 7) -and $length -ge 4) {
            $entry.destination_cid = Get-U16Le $payloadBytes $valueOffset
            $entry.source_cid = Get-U16Le $payloadBytes ($valueOffset + 2)
        }
        $commands += [pscustomobject]$entry
        $offset = $valueOffset + $length
    }
}

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
$commands = @($commands | Where-Object {
    [int]$_.connection_handle -in $targetHandles
})
$aclPackets = @($aclPackets | Where-Object {
    [int]$_.connection_handle -in $targetHandles
})
$continuationPackets = @($continuationPackets | Where-Object {
    [int]$_.connection_handle -in $targetHandles
})

$sdpRequests = @($commands | Where-Object {
    $_.code -eq 2 -and $_.psm -eq 1
} | Sort-Object { [datetime]$_.time_utc })
$sdpChannels = @()
$channelIndex = 0
foreach ($request in $sdpRequests) {
    $responseDirection = $request.direction -eq 'inbound' `
        ? 'outbound' : 'inbound'
    $response = @($commands | Where-Object {
        $_.direction -eq $responseDirection -and $_.code -eq 3 -and
        $_.identifier -eq $request.identifier -and
        $_.connection_handle -eq $request.connection_handle -and
        $_.source_cid -eq $request.source_cid -and
        $_.result -eq 0 -and
        [datetime]$_.time_utc -ge [datetime]$request.time_utc
    } | Sort-Object { [datetime]$_.time_utc } | Select-Object -First 1)
    if ($response.Count -ne 1) { continue }
    $localCid = if ($request.direction -eq 'inbound') {
        [int]$response[0].destination_cid
    } else { [int]$request.source_cid }
    $remoteCid = if ($request.direction -eq 'inbound') {
        [int]$request.source_cid
    } else { [int]$response[0].destination_cid }
    $disconnect = @($commands | Where-Object {
        $_.code -eq 6 -and
        $_.connection_handle -eq $request.connection_handle -and
        [datetime]$_.time_utc -gt [datetime]$response[0].time_utc -and
        (($_.destination_cid -eq $localCid -and
          $_.source_cid -eq $remoteCid) -or
         ($_.destination_cid -eq $remoteCid -and
          $_.source_cid -eq $localCid))
    } | Sort-Object { [datetime]$_.time_utc } | Select-Object -First 1)
    ++$channelIndex
    $sdpChannels += [pscustomobject][ordered]@{
        channel_id = $channelIndex
        connection_handle = [int]$request.connection_handle
        request_direction = [string]$request.direction
        identifier = [int]$request.identifier
        local_cid = $localCid
        remote_cid = $remoteCid
        opened_at = [string]$response[0].time_utc
        closed_at = if ($disconnect.Count -eq 1) {
            [string]$disconnect[0].time_utc
        } else { $null }
    }
}

$sdpPdus = @()
$fragmentedSdpPackets = 0
foreach ($channel in $sdpChannels) {
    $packets = @($aclPackets | Where-Object {
        $_.connection_handle -eq $channel.connection_handle -and
        [datetime]$_.time_utc -ge [datetime]$channel.opened_at -and
        ($null -eq $channel.closed_at -or
         [datetime]$_.time_utc -lt [datetime]$channel.closed_at) -and
        (($_.direction -eq 'inbound' -and
          $_.cid -eq $channel.local_cid) -or
         ($_.direction -eq 'outbound' -and
          $_.cid -eq $channel.remote_cid))
    } | Sort-Object { [datetime]$_.time_utc })
    foreach ($packet in $packets) {
        if (-not [bool]$packet.payload_complete) {
            ++$fragmentedSdpPackets
        }
        $sdpPdus += Convert-SdpPdu -Packet $packet -Channel $channel
    }
}

$serviceSearches = @()
foreach ($request in @($sdpPdus | Where-Object {
            $_.pdu_id -eq 2 -and $_.parse_complete
        })) {
    $response = @($sdpPdus | Where-Object {
        $_.channel_id -eq $request.channel_id -and
        $_.pdu_id -eq 3 -and
        $_.transaction_id -eq $request.transaction_id -and
        [datetime]$_.time_utc -ge [datetime]$request.time_utc
    } | Sort-Object { [datetime]$_.time_utc } | Select-Object -First 1)
    $responseHandles = @()
    if ($response.Count -eq 1) {
        $responseHandles = @($response[0].service_record_handles)
    }
    $serviceSearches += [pscustomobject][ordered]@{
        channel_id = [int]$request.channel_id
        transaction_id = [int]$request.transaction_id
        request_time_utc = [string]$request.time_utc
        requested_uuids = @($request.service_search_uuids)
        requested_uuid_names = @($request.service_search_uuid_names)
        response_time_utc = if ($response.Count -eq 1) {
            [string]$response[0].time_utc
        } else { $null }
        service_record_handles = [object[]]$responseHandles
    }
}

$attributeTransactions = @()
foreach ($request in @($sdpPdus | Where-Object {
            $_.pdu_id -eq 4 -and $_.parse_complete
        })) {
    $response = @($sdpPdus | Where-Object {
        $_.channel_id -eq $request.channel_id -and
        $_.pdu_id -eq 5 -and
        $_.transaction_id -eq $request.transaction_id -and
        [datetime]$_.time_utc -ge [datetime]$request.time_utc
    } | Sort-Object { [datetime]$_.time_utc } | Select-Object -First 1)
    $responseAttributes = @()
    $responseUuids = @()
    if ($response.Count -eq 1) {
        $responseAttributes = @($response[0].attributes)
        $responseUuids = @($response[0].response_uuids)
    }
    $attributeTransactions += [pscustomobject][ordered]@{
        channel_id = [int]$request.channel_id
        transaction_id = [int]$request.transaction_id
        request_time_utc = [string]$request.time_utc
        service_record_handle = [long]$request.service_record_handle
        attribute_selectors = @($request.attribute_selectors)
        response_time_utc = if ($response.Count -eq 1) {
            [string]$response[0].time_utc
        } else { $null }
        attributes = [object[]]$responseAttributes
        response_uuids = [object[]]$responseUuids
    }
}

$serviceSearchUuids = @($serviceSearches | ForEach-Object {
    @($_.requested_uuids)
} | Sort-Object -Unique)
$attributeResponseUuids = @($attributeTransactions | ForEach-Object {
    @($_.response_uuids)
} | Sort-Object -Unique)
$allObservedUuids = @($serviceSearchUuids + $attributeResponseUuids |
    Sort-Object -Unique)
$avrcpUuids = @('110C', '110E', '110F')
$summary = [ordered]@{
    schema_version = 1
    input = $input
    target_remote_address = $normalizedRemoteAddress
    target_connection_handles = @($targetHandles)
    target_handle_mapping_complete =
        [string]::IsNullOrWhiteSpace($normalizedRemoteAddress) -or
        $targetHandles.Count -gt 0
    sdp_channel_count = $sdpChannels.Count
    sdp_channels = @($sdpChannels)
    sdp_pdu_count = $sdpPdus.Count
    fragmented_sdp_start_packet_count = $fragmentedSdpPackets
    target_acl_continuation_packet_count = $continuationPackets.Count
    service_searches = @($serviceSearches)
    service_attribute_transactions = @($attributeTransactions)
    service_search_uuids = @($serviceSearchUuids)
    service_search_uuid_names = @($serviceSearchUuids | ForEach-Object {
        Get-SdpUuidName -Uuid $_
    })
    attribute_response_uuids = @($attributeResponseUuids)
    attribute_response_uuid_names = @($attributeResponseUuids |
        ForEach-Object { Get-SdpUuidName -Uuid $_ })
    all_observed_uuids = @($allObservedUuids)
    avrcp_service_search_observed = @($serviceSearchUuids |
        Where-Object { $_ -in $avrcpUuids }).Count -gt 0
    avrcp_uuid_observed = @($allObservedUuids |
        Where-Object { $_ -in $avrcpUuids }).Count -gt 0
    pdus = @($sdpPdus)
}

$parent = Split-Path -Parent $output
if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
}
$summary | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $output -Encoding utf8
$summary | ConvertTo-Json -Depth 8
