# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$gatePath = Join-Path $root `
    'tools\run-v1-avrcp-bootstrap-diagnostic.ps1'
$summaryPath = Join-Path $root `
    'tools\summarize-bluetooth-l2cap-trace.ps1'
$sdpParserPath = Join-Path $root `
    'tools\summarize-bluetooth-sdp-trace.ps1'
$liveProcessPath = Join-Path $root `
    'tools\v1-native-process-live.ps1'
$gate = Get-Content -LiteralPath $gatePath -Raw
$summary = Get-Content -LiteralPath $summaryPath -Raw
$sdpParser = Get-Content -LiteralPath $sdpParserPath -Raw
$liveProcess = Get-Content -LiteralPath $liveProcessPath -Raw
. $liveProcessPath

foreach ($required in @(
        'ConfirmV1AvrcpBootstrapCapture',
        "[ValidateSet('Microsoft', 'Native')]",
        'Test-ExpectedOwner',
        'microsoft_bluetooth_avrcptransport.inf',
        'NativeLdacAvrcpObserver',
        'Microsoft-Windows-BTH-BTHPORT/HCI',
        'Microsoft-Windows-BTH-BTHPORT/L2CAP',
        '--wait-acl-connect',
        'Invoke-V1NativeProcessLive',
        'XM5 must be physically off and disconnected',
        'Get-ChannelEnabled',
        'Set-ChannelEnabled -Channel $channel -Enabled $false',
        'channels_restored',
        'sdp_summary_path',
        'initial_avrcp_sdp_observed',
        'summarize-bluetooth-sdp-trace.ps1',
        'persistent_system_modified',
        'driver_or_audio_operation_issued',
        'You may turn off XM5 normally now.')) {
    if (-not $gate.Contains($required)) {
        throw "The AVRCP bootstrap capture contract is missing: $required"
    }
}

foreach ($forbidden in @(
        'pnputil', 'devcon', 'Restart-Computer',
        'Disable-PnpDevice', 'Enable-PnpDevice',
        'Stop-Service', 'Start-Service', 'Set-Service',
        'SetDefaultEndpoint', 'transport_probe',
        '--discover', '--stream-silence', '--stream-system',
        'IOCTL_NLD_AVRCP_OBSERVER_BEGIN_OBSERVATION')) {
    if ($gate.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The AVRCP bootstrap capture contains a forbidden operation: $forbidden"
    }
}

$enableIndex = $gate.IndexOf(
    'Set-ChannelEnabled -Channel $channel -Enabled $true')
$watchIndex = $gate.IndexOf('--wait-acl-connect')
$liveInvokeIndex = $gate.IndexOf('Invoke-V1NativeProcessLive')
$finallyIndex = $gate.IndexOf('} finally {', $watchIndex)
$restoreIndex = $gate.IndexOf(
    'Set-ChannelEnabled -Channel $channel -Enabled $false',
    $finallyIndex)
if ($enableIndex -lt 0 -or $liveInvokeIndex -le $enableIndex -or
    $watchIndex -le $liveInvokeIndex -or
    $finallyIndex -le $watchIndex -or $restoreIndex -le $finallyIndex) {
    throw 'The Bluetooth trace channels are not restored from the capture finally path.'
}

foreach ($required in @(
        'ForEach-Object',
        'Write-Host $Line',
        'exit_code = [int]$exitCode',
        'lines = @($lines')) {
    if (-not $liveProcess.Contains($required)) {
        throw "The live native process relay is missing: $required"
    }
}

foreach ($required in @(
        'schema_version = 2',
        'outbound_sdp_connection_requests',
        'inbound_sdp_connection_requests',
        'outbound_avctp_connection_requests',
        'inbound_avctp_connection_requests',
        'outbound_avctp_request_outcomes',
        'inbound_avctp_request_outcomes',
        'target_handle_mapping_complete',
        'outbound_avdtp_connection_requests',
        'inbound_avdtp_connection_requests')) {
    if (-not $summary.Contains($required)) {
        throw "The L2CAP bootstrap summary is missing: $required"
    }
}

foreach ($required in @(
        'schema_version = 1',
        'sdp_channel_count',
        'service_searches',
        'service_attribute_transactions',
        'service_search_uuids',
        'attribute_response_uuids',
        'avrcp_service_search_observed',
        'avrcp_uuid_observed',
        "'110C', '110E', '110F'")) {
    if (-not $sdpParser.Contains($required)) {
        throw "The SDP bootstrap summary is missing: $required"
    }
}

foreach ($path in @(
        $gatePath,
        $summaryPath,
        $sdpParserPath,
        $liveProcessPath)) {
    $tokens = $null
    $errors = $null
    [void][Management.Automation.Language.Parser]::ParseFile(
        $path, [ref]$tokens, [ref]$errors)
    if (@($errors).Count -ne 0) {
        throw "The AVRCP bootstrap PowerShell file does not parse: $path"
    }
}

$temp = Join-Path ([IO.Path]::GetTempPath()) `
    "v1-avrcp-bootstrap-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $temp -Force | Out-Null
try {
    $liveChildPath = Join-Path $temp 'live-child.ps1'
    @'
[Console]::Out.WriteLine('armed')
[Console]::Out.Flush()
Start-Sleep -Milliseconds 1500
[Console]::Out.WriteLine('timed-out')
[Console]::Out.Flush()
exit 10
'@ | Set-Content -LiteralPath $liveChildPath -Encoding utf8NoBOM
    $liveObservations = [System.Collections.Generic.List[object]]::new()
    $liveTimer = [Diagnostics.Stopwatch]::StartNew()
    $liveResult = Invoke-V1NativeProcessLive `
        -FilePath (Get-Command pwsh.exe).Source `
        -ArgumentList @(
            '-NoProfile',
            '-ExecutionPolicy', 'Bypass',
            '-File', $liveChildPath) `
        -LineSink {
            param([string]$Line)
            [void]$liveObservations.Add([pscustomobject]@{
                line = $Line
                elapsed_ms = [long]$liveTimer.ElapsedMilliseconds
            })
        }
    $liveTimer.Stop()
    if ([int]$liveResult.exit_code -ne 10 -or
        @($liveResult.lines).Count -ne 2 -or
        [string]$liveResult.lines[0] -cne 'armed' -or
        [string]$liveResult.lines[1] -cne 'timed-out' -or
        $liveObservations.Count -ne 2 -or
        [string]$liveObservations[0].line -cne 'armed' -or
        ([long]$liveTimer.ElapsedMilliseconds -
         [long]$liveObservations[0].elapsed_ms) -lt 1300) {
        throw 'The native process relay buffered output until child exit.'
    }

    $fixturePath = Join-Path $temp 'hci.xml'
    $outputPath = Join-Path $temp 'summary.json'
    $sdpOutputPath = Join-Path $temp 'sdp-summary.json'
    @(
        "<Event><System><TimeCreated SystemTime='2026-08-08T00:00:00Z'/></System><EventData><Data Name='BIP_Type'>2</Data><Data Name='BIP_Data'>030B0000015544332211000100</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-08-08T00:00:00Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>00010C00080001000207040017004700</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-08-08T00:00:01Z'/></System><EventData><Data Name='BIP_Type'>3</Data><Data Name='BIP_Data'>002110000C000100030708004800470000000000</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-08-08T00:00:02Z'/></System><EventData><Data Name='BIP_Type'>3</Data><Data Name='BIP_Data'>00210C00080001000208040017004900</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-08-08T00:00:03Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>000110000C000100030808004A00490000000000</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-08-08T00:00:04Z'/></System><EventData><Data Name='BIP_Type'>3</Data><Data Name='BIP_Data'>00210C00080001000209040001004B00</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-08-08T00:00:04.0100000Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>000110000C000100030908004A004B0000000000</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-08-08T00:00:04.0200000Z'/></System><EventData><Data Name='BIP_Type'>3</Data><Data Name='BIP_Data'>002111000D004A000200010008350319111F020000</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-08-08T00:00:04.0300000Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>000112000E004B000300010009000100010001000D00</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-08-08T00:00:04.0400000Z'/></System><EventData><Data Name='BIP_Type'>3</Data><Data Name='BIP_Data'>002111000D004A000200030008350319110A020000</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-08-08T00:00:04.0500000Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>000112000E004B000300030009000100010001000900</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-08-08T00:00:04.0600000Z'/></System><EventData><Data Name='BIP_Type'>3</Data><Data Name='BIP_Data'>0021180014004A00040009000F000100090064350609000409000900</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-08-08T00:00:04.0700000Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>000130002C004B000500090027002435220900043510350619010009001935061900190901030900093508350619110D09010300</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-08-08T00:00:05Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>00010C0008000100020A040019004C00</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-08-08T00:00:06Z'/></System><EventData><Data Name='BIP_Type'>2</Data><Data Name='BIP_Data'>030B0001016655443322110100</Data></EventData></Event>",
        "<Event><System><TimeCreated SystemTime='2026-08-08T00:00:07Z'/></System><EventData><Data Name='BIP_Type'>4</Data><Data Name='BIP_Data'>01010C0008000100020B040017004D00</Data></EventData></Event>"
    ) | Set-Content -LiteralPath $fixturePath -Encoding utf8NoBOM

    & $summaryPath -InputPath $fixturePath -OutputPath $outputPath `
        -RemoteAddress '001122334455' | Out-Null
    & $sdpParserPath -InputPath $fixturePath `
        -OutputPath $sdpOutputPath -RemoteAddress '001122334455' |
        Out-Null
    $parsed = Get-Content -LiteralPath $outputPath -Raw |
        ConvertFrom-Json
    $sdpParsed = Get-Content -LiteralPath $sdpOutputPath -Raw |
        ConvertFrom-Json
    $avctpOutcome = @($parsed.outbound_avctp_request_outcomes)
    $inboundAvctpOutcome = @($parsed.inbound_avctp_request_outcomes)
    if ([int]$parsed.schema_version -ne 2 -or
        $parsed.target_handle_mapping_complete -ne $true -or
        [int]$parsed.target_connection_complete_count -ne 1 -or
        @($parsed.target_connection_handles).Count -ne 1 -or
        [int]@($parsed.target_connection_handles)[0] -ne 256 -or
        [int]$parsed.all_l2cap_signaling_command_count -ne 8 -or
        [int]$parsed.l2cap_signaling_command_count -ne 7 -or
        [int]$parsed.outbound_avctp_connection_requests -ne 1 -or
        [int]$parsed.inbound_avctp_connection_requests -ne 1 -or
        [int]$parsed.outbound_avctp_successful_connections -ne 1 -or
        [int]$parsed.outbound_avctp_rejected_connections -ne 0 -or
        [int]$parsed.outbound_avctp_unresolved_connections -ne 0 -or
        [int]$parsed.outbound_sdp_connection_requests -ne 0 -or
        [int]$parsed.inbound_sdp_connection_requests -ne 1 -or
        [int]$parsed.outbound_avdtp_connection_requests -ne 1 -or
        $avctpOutcome.Count -ne 1 -or
        [int]$avctpOutcome[0].psm -ne 23 -or
        [string]$avctpOutcome[0].status -cne 'success' -or
        $inboundAvctpOutcome.Count -ne 1 -or
        [string]$inboundAvctpOutcome[0].status -cne 'success') {
        throw 'The L2CAP summary rejected the known AVRCP bootstrap fixture.'
    }
    $searches = @($sdpParsed.service_searches)
    $searchUuids = @($sdpParsed.service_search_uuids)
    $attributeTransactions = @(
        $sdpParsed.service_attribute_transactions)
    $attributeUuids = @($sdpParsed.attribute_response_uuids)
    if ([int]$sdpParsed.schema_version -ne 1 -or
        $sdpParsed.target_handle_mapping_complete -ne $true -or
        [int]$sdpParsed.sdp_channel_count -ne 1 -or
        [int]$sdpParsed.sdp_pdu_count -ne 6 -or
        $searches.Count -ne 2 -or
        '111F' -notin $searchUuids -or
        '110A' -notin $searchUuids -or
        @($searches[0].service_record_handles).Count -ne 1 -or
        [long]@($searches[0].service_record_handles)[0] -ne 65549 -or
        @($searches[1].service_record_handles).Count -ne 1 -or
        [long]@($searches[1].service_record_handles)[0] -ne 65545 -or
        $attributeTransactions.Count -ne 1 -or
        [long]$attributeTransactions[0].service_record_handle -ne 65545 -or
        @($attributeTransactions[0].attributes).Count -ne 2 -or
        '0019' -notin $attributeUuids -or
        '0100' -notin $attributeUuids -or
        '110D' -notin $attributeUuids -or
        $sdpParsed.avrcp_service_search_observed -ne $false -or
        $sdpParsed.avrcp_uuid_observed -ne $false) {
        throw 'The SDP summary rejected the known Microsoft bootstrap fixture.'
    }

    $emptyFixturePath = Join-Path $temp 'hci-no-l2cap.xml'
    $emptyOutputPath = Join-Path $temp 'summary-no-l2cap.json'
    $emptySdpOutputPath = Join-Path $temp 'sdp-summary-no-l2cap.json'
    "<Event><System><TimeCreated SystemTime='2026-08-08T00:00:00Z'/></System><EventData><Data Name='BIP_Type'>2</Data><Data Name='BIP_Data'>030B0000015544332211000100</Data></EventData></Event>" |
        Set-Content -LiteralPath $emptyFixturePath -Encoding utf8NoBOM
    & $summaryPath -InputPath $emptyFixturePath `
        -OutputPath $emptyOutputPath -RemoteAddress '001122334455' |
        Out-Null
    & $sdpParserPath -InputPath $emptyFixturePath `
        -OutputPath $emptySdpOutputPath -RemoteAddress '001122334455' |
        Out-Null
    $emptyParsed = Get-Content -LiteralPath $emptyOutputPath -Raw |
        ConvertFrom-Json
    $emptySdpParsed = Get-Content -LiteralPath $emptySdpOutputPath -Raw |
        ConvertFrom-Json
    if ($emptyParsed.target_handle_mapping_complete -ne $true -or
        [int]$emptyParsed.l2cap_signaling_command_count -ne 0 -or
        [int]$emptyParsed.outbound_avctp_connection_requests -ne 0 -or
        [int]$emptyParsed.inbound_avctp_connection_requests -ne 0) {
        throw 'The L2CAP summary rejected a valid zero-request capture.'
    }
    if ($emptySdpParsed.target_handle_mapping_complete -ne $true -or
        [int]$emptySdpParsed.sdp_channel_count -ne 0 -or
        [int]$emptySdpParsed.sdp_pdu_count -ne 0 -or
        $emptySdpParsed.avrcp_uuid_observed -ne $false) {
        throw 'The SDP summary rejected a valid zero-request capture.'
    }
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force
}

Write-Host 'V1 AVRCP bootstrap diagnostic policy tests passed.'
