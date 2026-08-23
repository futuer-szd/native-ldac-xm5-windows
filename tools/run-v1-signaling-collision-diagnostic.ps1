# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1SignalingCollisionCapture,
    [ValidateRange(300,420)][int]$DurationSeconds = 360,
    [string]$CandidatePath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-normal-stop-common.ps1')

Assert-LegacyAdministrator
if (-not $ConfirmV1SignalingCollisionCapture) {
    throw 'Refusing to authorize the signaling-collision diagnostic. Re-run with -ConfirmV1SignalingCollisionCapture.'
}
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $root 'artifacts\v1-normal-stop\candidate'
}
$prerequisitePath = Join-Path $root `
    $script:V1NormalStopPrerequisiteRelativePath
$candidate = Get-V1NormalStopCandidate -CandidatePath $CandidatePath `
    -ExpectedPrerequisitePath $prerequisitePath
if ((& git.exe -C $root rev-parse HEAD).Trim() -ne
        [string]$candidate.manifest.source_commit -or
    @(& git.exe -C $root status --porcelain).Count -ne 0) {
    throw 'The signaling-collision diagnostic candidate must match clean Git HEAD.'
}

$channels = @(
    'Microsoft-Windows-BTH-BTHPORT/HCI',
    'Microsoft-Windows-BTH-BTHPORT/L2CAP'
)
$states = @{}
foreach ($channel in $channels) {
    $configuration = @(& wevtutil.exe gl $channel 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to query Bluetooth analytic channel: $channel"
    }
    $states[$channel] = [bool]($configuration -match '^enabled:\s*true\s*$')
}

Write-Host 'V1 signaling-collision capture preflight passed.'
Write-Host 'This diagnostic temporarily enables the Windows Bluetooth HCI/L2CAP analytic channels, runs the same bounded normal-stop gate, exports only events from this run, and restores both channel states.'
Write-Host 'No driver, service, endpoint, Bluetooth radio, default output, or pairing setting is changed.'
if (-not $PSCmdlet.ShouldProcess(
        'one bounded XM5 signaling acquisition with HCI/L2CAP capture',
        'Capture the physical ACL startup and at most four policy-v11 OPEN attempts')) {
    return
}

$trialRoot = Join-Path $root 'artifacts\v1-signaling-collision\trial'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$directory = Join-Path $trialRoot "capture-$stamp"
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$captureStart = Get-Date
$gateError = $null
$gatePassed = $false
$channelResults = @()
try {
    foreach ($channel in $channels) {
        if (-not $states[$channel]) {
            & wevtutil.exe sl $channel /e:true /q:true
            if ($LASTEXITCODE -ne 0) {
                throw "Unable to enable Bluetooth analytic channel: $channel"
            }
        }
    }
    try {
        & (Join-Path $PSScriptRoot 'run-v1-normal-stop-gate.ps1') `
            -ConfirmV1NormalStop -DurationSeconds $DurationSeconds `
            -CandidatePath $CandidatePath -Confirm:$false
        $gatePassed = $true
    } catch {
        $gateError = $_.Exception.Message
    }
} finally {
    foreach ($channel in $channels) {
        $safe = $channel -replace '[^A-Za-z0-9.-]', '_'
        $evtx = Join-Path $directory "$safe.evtx"
        $xml = Join-Path $directory "$safe.xml"
        & wevtutil.exe epl $channel $evtx /ow:true
        $events = @(Get-WinEvent -FilterHashtable @{
            LogName = $channel
            StartTime = $captureStart
        } -Oldest -ErrorAction SilentlyContinue)
        @($events | ForEach-Object { $_.ToXml() }) |
            Set-Content -LiteralPath $xml -Encoding UTF8
        $channelResults += [pscustomobject][ordered]@{
            channel = $channel
            enabled_before = [bool]$states[$channel]
            event_count = $events.Count
            evtx = $evtx
            xml = $xml
        }
    }
    foreach ($channel in $channels) {
        if (-not $states[$channel]) {
            & wevtutil.exe sl $channel /e:false | Out-Null
        }
    }
}

$hci = @($channelResults | Where-Object { $_.channel -like '*/HCI' })[0]
$summaryPath = Join-Path $directory 'l2cap-summary.json'
& (Join-Path $PSScriptRoot 'summarize-bluetooth-l2cap-trace.ps1') `
    -InputPath $hci.xml -OutputPath $summaryPath | Out-Null
$summary = Get-Content -Raw -LiteralPath $summaryPath | ConvertFrom-Json
$capture = [ordered]@{
    schema_version = 1
    source_commit = (& git.exe -C $root rev-parse HEAD).Trim()
    captured_at = (Get-Date).ToString('o')
    capture_started_at = $captureStart.ToString('o')
    gate_passed = $gatePassed
    gate_error = $gateError
    channels = @($channelResults)
    l2cap_summary = $summaryPath
    outbound_avdtp_connection_requests =
        [int]$summary.outbound_avdtp_connection_requests
    inbound_avdtp_connection_requests =
        [int]$summary.inbound_avdtp_connection_requests
    inbound_no_resources_responses =
        [int]$summary.inbound_no_resources_responses
    inbound_avdtp_collision_observed =
        [bool]$summary.inbound_avdtp_collision_observed
    system_modified = $false
}
$capturePath = Join-Path $directory 'capture.json'
$capture | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $capturePath `
    -Encoding UTF8
Write-Host "V1 signaling-collision capture completed: $capturePath"
Write-Host "AVDTP connection requests: outbound $($capture.outbound_avdtp_connection_requests), inbound $($capture.inbound_avdtp_connection_requests); remote no-resources responses $($capture.inbound_no_resources_responses)."
if ($gatePassed) {
    Write-Host 'The bounded normal-stop gate passed while the diagnostic trace was captured.'
    return
}
throw "The bounded gate did not pass; its failure was preserved and the collision trace is ready for offline analysis. Do not retry. Capture: $capturePath"

