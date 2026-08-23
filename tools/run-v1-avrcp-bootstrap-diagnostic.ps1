# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1AvrcpBootstrapCapture,
    [ValidateSet('Microsoft', 'Native')]
    [string]$ExpectedOwner = 'Microsoft',
    [ValidateRange(5, 60)]
    [int]$ObservationSeconds = 20,
    [ValidateRange(30, 180)]
    [int]$ConnectTimeoutSeconds = 90,
    [string]$ConnectionProbePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:TargetPrefix =
    'BTHENUM\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
$script:MicrosoftInf = 'microsoft_bluetooth_avrcptransport.inf'
$script:MicrosoftService = 'Microsoft_Bluetooth_AvrcpTransport'
$script:NativeService = 'NativeLdacAvrcpObserver'
$script:Channels = @(
    'Microsoft-Windows-BTH-BTHPORT/HCI',
    'Microsoft-Windows-BTH-BTHPORT/L2CAP'
)
. (Join-Path $PSScriptRoot 'v1-native-process-live.ps1')

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'The AVRCP bootstrap diagnostic requires an elevated PowerShell 7 terminal.'
    }
}

function Get-PropertyData {
    param(
        [Parameter(Mandatory = $true)][string]$InstanceId,
        [Parameter(Mandatory = $true)][string]$KeyName
    )

    $property = Get-PnpDeviceProperty -InstanceId $InstanceId `
        -KeyName $KeyName -ErrorAction SilentlyContinue
    if ($null -eq $property) { return $null }
    return $property.Data
}

function Get-TargetDevice {
    $matches = @(Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object {
        $_.InstanceId.StartsWith(
            $script:TargetPrefix,
            [StringComparison]::OrdinalIgnoreCase)
    })
    if ($matches.Count -ne 1) {
        throw 'Exactly one paired XM5 AVRCP 0x110E PDO is required.'
    }
    return $matches[0]
}

function Get-DeviceSnapshot {
    param([Parameter(Mandatory = $true)]$Device)

    return [pscustomobject][ordered]@{
        instance_id = [string]$Device.InstanceId
        present = [bool]$Device.Present
        status = [string]$Device.Status
        problem = [string]$Device.Problem
        problem_code = [int](Get-PropertyData `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_ProblemCode')
        inf = [string](Get-PropertyData -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverInfPath')
        service = [string](Get-PropertyData -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Service')
        parent = [string](Get-PropertyData -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Parent')
        container_id = [string](Get-PropertyData `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_ContainerId')
    }
}

function Test-ExpectedOwner {
    param(
        [Parameter(Mandatory = $true)]$Snapshot,
        [Parameter(Mandatory = $true)][string]$Owner
    )

    if (-not [bool]$Snapshot.present -or
        [int]$Snapshot.problem_code -ne 0) {
        return $false
    }
    if ($Owner -eq 'Microsoft') {
        return $Snapshot.inf -ieq $script:MicrosoftInf -and
            $Snapshot.service -ieq $script:MicrosoftService
    }
    return $Snapshot.inf -match '^oem\d+\.inf$' -and
        $Snapshot.service -ieq $script:NativeService
}

function Get-ChannelEnabled {
    param([Parameter(Mandatory = $true)][string]$Channel)

    $configuration = @(& wevtutil.exe gl $Channel 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to query Bluetooth analytic channel: $Channel"
    }
    $match = [regex]::Match(
        ($configuration -join "`n"),
        '(?im)^enabled:\s*(?<value>true|false)\s*$')
    if (-not $match.Success) {
        throw "Bluetooth analytic channel state was not reported: $Channel"
    }
    return $match.Groups['value'].Value -ieq 'true'
}

function Set-ChannelEnabled {
    param(
        [Parameter(Mandatory = $true)][string]$Channel,
        [Parameter(Mandatory = $true)][bool]$Enabled
    )

    $value = $Enabled ? 'true' : 'false'
    $lines = @(& wevtutil.exe sl $Channel "/e:$value" /q:true 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to set Bluetooth analytic channel $Channel to $value`: $($lines -join ' ')"
    }
}

function Write-AtomicJson {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $temporary = "$Path.tmp.$PID"
    $Value | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $temporary -Encoding utf8NoBOM
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The AVRCP bootstrap diagnostic requires PowerShell 7.'
}
Assert-Administrator
if (-not $ConfirmV1AvrcpBootstrapCapture) {
    throw 'Refusing to enable the bounded Bluetooth trace without -ConfirmV1AvrcpBootstrapCapture.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($ConnectionProbePath)) {
    $ConnectionProbePath = Join-Path $projectRoot `
        'build\protocol\Release\xm5_connection_probe.exe'
}
$ConnectionProbePath = [IO.Path]::GetFullPath($ConnectionProbePath)
if (-not (Test-Path -LiteralPath $ConnectionProbePath -PathType Leaf)) {
    throw "XM5 connection probe is missing: $ConnectionProbePath"
}

$sourceCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$dirty = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or
    $sourceCommit -notmatch '^[0-9a-fA-F]{40}$' -or
    $dirty.Count -ne 0) {
    throw 'The AVRCP bootstrap diagnostic requires a clean Git HEAD.'
}

$before = Get-DeviceSnapshot -Device (Get-TargetDevice)
if (-not (Test-ExpectedOwner -Snapshot $before -Owner $ExpectedOwner)) {
    throw "The XM5 AVRCP PDO is not healthy under the expected $ExpectedOwner owner."
}
$addressMatch = [regex]::Match(
    $before.instance_id,
    '(?i)(?<address>[0-9A-F]{12})_C')
if (-not $addressMatch.Success) {
    throw 'The XM5 Bluetooth address was not present in the AVRCP instance ID.'
}
$remoteAddress = $addressMatch.Groups['address'].Value.ToUpperInvariant()
$radioLines = @(& $ConnectionProbePath --radio-state 2>&1)
if ($LASTEXITCODE -ne 0 -or
    ($radioLines -join "`n") -notmatch 'ready') {
    throw 'Windows Bluetooth must be on and connectable.'
}
$stateLines = @(& $ConnectionProbePath --state 2>&1)
if ($LASTEXITCODE -ne 10 -or
    ($stateLines -join "`n") -notmatch 'disconnected') {
    throw 'XM5 must be physically off and disconnected before capture starts.'
}

$channelStates = [ordered]@{}
foreach ($channel in $script:Channels) {
    $channelStates[$channel] = Get-ChannelEnabled -Channel $channel
}

Write-Host "V1 AVRCP $ExpectedOwner bootstrap capture preflight passed."
Write-Host 'Keep XM5 off until the ACL watcher reports that it is armed.'
Write-Host 'After connection, leave XM5 on and idle; do not start playback or use touch controls.'
Write-Host "The trace observes the first $ObservationSeconds seconds after physical ACL connect."
Write-Host 'No driver, endpoint, Bluetooth radio, pairing, audio, or volume setting will be changed.'

$target = "one physical XM5 connection under the healthy $ExpectedOwner AVRCP owner"
if (-not $PSCmdlet.ShouldProcess(
        $target,
        'Temporarily enable Bluetooth HCI/L2CAP analytic channels, capture the initial connection, then restore both channel states')) {
    return
}

$trialRoot = Join-Path $projectRoot (
    'artifacts\v1-volume-sync\trial\avrcp-bootstrap-' +
    $ExpectedOwner.ToLowerInvariant() + '-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $trialRoot -Force | Out-Null
$resultPath = Join-Path $trialRoot 'result.json'
$captureStart = $null
$captureEnd = $null
$connectLines = @()
$connectExit = -1
$after = $null
$failure = $null
$channelResults = @()
$restorationErrors = @()
$enabledByGate = @()

try {
    foreach ($channel in $script:Channels) {
        if (-not [bool]$channelStates[$channel]) {
            Set-ChannelEnabled -Channel $channel -Enabled $true
            $enabledByGate += $channel
        }
    }
    $captureStart = Get-Date
    $connectResult = Invoke-V1NativeProcessLive `
        -FilePath $ConnectionProbePath `
        -ArgumentList @(
            '--wait-acl-connect',
            [string]$ConnectTimeoutSeconds)
    $connectLines = @($connectResult.lines)
    $connectExit = [int]$connectResult.exit_code
    if ($connectExit -ne 0) {
        throw 'One physical XM5 ACL connect was not observed.'
    }

    Start-Sleep -Seconds $ObservationSeconds
    $after = Get-DeviceSnapshot -Device (Get-TargetDevice)
    if ($after.instance_id -ine $before.instance_id -or
        -not (Test-ExpectedOwner -Snapshot $after -Owner $ExpectedOwner)) {
        throw 'The exact AVRCP PDO owner or health changed during capture.'
    }
} catch {
    $failure = $_.Exception.Message
} finally {
    $captureEnd = Get-Date
    try {
        if ($null -ne $captureStart) {
            foreach ($channel in $script:Channels) {
                $safeName = $channel -replace '[^A-Za-z0-9.-]', '_'
                $evtxPath = Join-Path $trialRoot "$safeName.evtx"
                $xmlPath = Join-Path $trialRoot "$safeName.xml"
                & wevtutil.exe epl $channel $evtxPath /ow:true
                $evtxExit = $LASTEXITCODE
                $events = @(Get-WinEvent -FilterHashtable @{
                    LogName = $channel
                    StartTime = $captureStart
                    EndTime = $captureEnd
                } -Oldest -ErrorAction SilentlyContinue)
                @($events | ForEach-Object { $_.ToXml() }) |
                    Set-Content -LiteralPath $xmlPath -Encoding utf8NoBOM
                $channelResults += [pscustomobject][ordered]@{
                    channel = $channel
                    enabled_before = [bool]$channelStates[$channel]
                    enabled_by_gate = $channel -in $enabledByGate
                    event_count = $events.Count
                    evtx_export_exit = $evtxExit
                    evtx = $evtxPath
                    xml = $xmlPath
                }
            }
        }
    } catch {
        if ([string]::IsNullOrWhiteSpace($failure)) {
            $failure = 'Bluetooth trace export failed: ' +
                $_.Exception.Message
        }
    } finally {
        foreach ($channel in $enabledByGate) {
            try {
                Set-ChannelEnabled -Channel $channel -Enabled $false
            } catch {
                $restorationErrors += $_.Exception.Message
            }
        }
        foreach ($channel in $script:Channels) {
            try {
                $restored = Get-ChannelEnabled -Channel $channel
                if ($restored -ne [bool]$channelStates[$channel]) {
                    $restorationErrors +=
                        "Bluetooth analytic channel state mismatch: $channel"
                }
            } catch {
                $restorationErrors += $_.Exception.Message
            }
        }
    }
}

$summaryPath = Join-Path $trialRoot 'l2cap-summary.json'
$sdpSummaryPath = Join-Path $trialRoot 'sdp-summary.json'
$summary = $null
$sdpSummary = $null
$hciResult = @($channelResults | Where-Object {
    $_.channel -eq 'Microsoft-Windows-BTH-BTHPORT/HCI'
})
if ($hciResult.Count -eq 1 -and
    (Test-Path -LiteralPath $hciResult[0].xml -PathType Leaf)) {
    try {
        & (Join-Path $PSScriptRoot `
            'summarize-bluetooth-l2cap-trace.ps1') `
            -InputPath $hciResult[0].xml -OutputPath $summaryPath `
            -RemoteAddress $remoteAddress |
            Out-Null
        $summary = Get-Content -LiteralPath $summaryPath -Raw |
            ConvertFrom-Json
    } catch {
        if ([string]::IsNullOrWhiteSpace($failure)) {
            $failure = 'Bluetooth trace summary failed: ' +
                $_.Exception.Message
        }
    }
}
if ($hciResult.Count -eq 1 -and
    (Test-Path -LiteralPath $hciResult[0].xml -PathType Leaf)) {
    try {
        & (Join-Path $PSScriptRoot `
            'summarize-bluetooth-sdp-trace.ps1') `
            -InputPath $hciResult[0].xml -OutputPath $sdpSummaryPath `
            -RemoteAddress $remoteAddress |
            Out-Null
        $sdpSummary = Get-Content -LiteralPath $sdpSummaryPath -Raw |
            ConvertFrom-Json
    } catch {
        if ([string]::IsNullOrWhiteSpace($failure)) {
            $failure = 'Bluetooth SDP summary failed: ' +
                $_.Exception.Message
        }
    }
}

$channelsRestored = $restorationErrors.Count -eq 0
$evtxExportsComplete = $channelResults.Count -eq $script:Channels.Count -and
    @($channelResults | Where-Object {
        [int]$_.evtx_export_exit -ne 0
    }).Count -eq 0
$captureComplete = $connectExit -eq 0 -and
    $null -ne $summary -and $null -ne $sdpSummary -and
    $hciResult.Count -eq 1 -and
    [int]$hciResult[0].event_count -gt 0 -and
    [bool]$summary.target_handle_mapping_complete -and
    [bool]$sdpSummary.target_handle_mapping_complete -and
    $evtxExportsComplete
if (-not $channelsRestored -and [string]::IsNullOrWhiteSpace($failure)) {
    $failure = 'One or more Bluetooth analytic channel states were not restored.'
}
if (-not $captureComplete -and [string]::IsNullOrWhiteSpace($failure)) {
    $failure = 'The bounded Bluetooth trace did not contain a complete physical connection capture.'
}

$passed = [string]::IsNullOrWhiteSpace($failure) -and
    $channelsRestored -and $captureComplete
$result = [ordered]@{
    result_version = 2
    created_at = (Get-Date).ToString('o')
    passed = $passed
    status = $passed ? 'captured' : 'capture-failed'
    failure = $failure
    source_commit = $sourceCommit
    expected_owner = $ExpectedOwner.ToLowerInvariant()
    remote_address = $remoteAddress
    observation_seconds = $ObservationSeconds
    connect_timeout_seconds = $ConnectTimeoutSeconds
    connection_probe = [ordered]@{
        path = $ConnectionProbePath
        sha256 = (Get-FileHash -LiteralPath $ConnectionProbePath `
            -Algorithm SHA256).Hash
    }
    capture_started_at = $null -eq $captureStart `
        ? $null : $captureStart.ToString('o')
    capture_ended_at = $captureEnd.ToString('o')
    connect_exit = $connectExit
    connect_lines = @($connectLines)
    pdo_before = $before
    pdo_after = $after
    channels_restored = $channelsRestored
    channel_restoration_errors = @($restorationErrors)
    channels = @($channelResults)
    l2cap_summary_path = $summaryPath
    l2cap_summary = $summary
    sdp_summary_path = $sdpSummaryPath
    sdp_summary = $sdpSummary
    initial_avctp_observed = $null -ne $summary -and
        ([int]$summary.outbound_avctp_connection_requests -gt 0 -or
         [int]$summary.inbound_avctp_connection_requests -gt 0)
    initial_avrcp_sdp_observed = $null -ne $sdpSummary -and
        [bool]$sdpSummary.avrcp_uuid_observed
    persistent_system_modified = -not $channelsRestored
    driver_or_audio_operation_issued = $false
}
Write-AtomicJson -Value $result -Path $resultPath

if (-not $passed) {
    throw "V1 AVRCP bootstrap capture failed: $failure Result: $resultPath"
}

Write-Host 'V1 AVRCP bootstrap capture completed.'
Write-Host ("Initial L2CAP requests: SDP out/in {0}/{1}; AVCTP out/in {2}/{3}; AVDTP out/in {4}/{5}." -f
    [int]$summary.outbound_sdp_connection_requests,
    [int]$summary.inbound_sdp_connection_requests,
    [int]$summary.outbound_avctp_connection_requests,
    [int]$summary.inbound_avctp_connection_requests,
    [int]$summary.outbound_avdtp_connection_requests,
    [int]$summary.inbound_avdtp_connection_requests)
Write-Host ("Initial SDP service searches: {0}; AVRCP UUID observed: {1}." -f
    (@($sdpSummary.service_search_uuids) -join ', '),
    [bool]$sdpSummary.avrcp_uuid_observed)
Write-Host 'The Bluetooth analytic channel states were restored. You may turn off XM5 normally now.'
Write-Host "Result: $resultPath"
