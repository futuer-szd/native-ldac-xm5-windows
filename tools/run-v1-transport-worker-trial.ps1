# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateRange(90, 300)]
    [int]$DurationSeconds = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'native-ldac-baseline-common.ps1')

Assert-LegacyAdministrator
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$candidateRoot = Join-Path $projectRoot `
    'artifacts\v1-transport-worker-observer\candidate'
$manifestPath = Join-Path $candidateRoot 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'The V1 transport-worker observer manifest is missing.'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$capabilities = @($manifest.capabilities | ForEach-Object { [string]$_ })
foreach ($required in @(
        'job_object_containment',
        'event_only_transport_worker',
        'single_generation_open_authorization',
        'distinct_graceful_and_cancel_stop',
        'no_PCM_read',
        'no_media_LinkState_write',
        'no_Bluetooth_open')) {
    if ($required -notin $capabilities) {
        throw "The candidate lacks capability: $required"
    }
}
if ([int]$manifest.manifest_version -ne 1 -or
    $manifest.source_dirty -ne $false -or
    [int]$manifest.required_pcm_abi -ne 2 -or
    [int]$manifest.required_presence_abi -ne 1 -or
    [int]$manifest.render_poll_ms -ne 250 -or
    [int]$manifest.engine_ready_timeout_ms -ne 3000) {
    throw 'The V1 transport-worker observer manifest contract is invalid.'
}
foreach ($file in @($manifest.files)) {
    $path = Join-Path $candidateRoot ([string]$file.path)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-Item -LiteralPath $path).Length -ne [long]$file.length -or
        -not (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.Equals(
            [string]$file.sha256,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Observer file failed its hash check: $($file.path)"
    }
}

$latestBackupPath = Join-Path $projectRoot `
    'artifacts\driver-test\latest-backup.txt'
$backupPath = (Get-Content -LiteralPath $latestBackupPath -Raw).Trim()
$baseline = Get-NativeLdacBaselineSnapshot -BackupPath $backupPath
$presentDevices = @($baseline.native_audio_devices |
    Where-Object { $_.present })
if (-not $baseline.safe_original_a2dp -or
    $presentDevices.Count -ne 1 -or
    @($baseline.native_audio_packages).Count -lt 1 -or
    [string]$presentDevices[0].service -ne 'NativeLdacAudio' -or
    [int]$presentDevices[0].problem_code -ne 0) {
    throw 'The installed V1 endpoint is not in the expected safe original-A2DP state.'
}
if (@($baseline.workspace_processes).Count -ne 0 -or
    @($baseline.scheduled_tasks).Count -ne 0) {
    throw 'Stop existing Native LDAC processes or tasks before this bounded trial.'
}

$connectionProbe = Join-Path $candidateRoot 'xm5_connection_probe.exe'
$xm5State = Get-NativeLdacXm5BluetoothState `
    -ProbePath $connectionProbe `
    -ExpectedSourceCommit ([string]$manifest.source_commit)
if ($xm5State -ne 'disconnected') {
    throw 'Turn off the XM5 and wait until it is physically disconnected.'
}
$endpointProbe = Join-Path $candidateRoot 'audio_endpoint_probe.exe'
$initialPresence = @(& $endpointProbe --presence 2>&1)
if ($LASTEXITCODE -ne 0 -or
    ($initialPresence -join "`n") -notmatch
        '(?m)^Physical presence absent:') {
    throw 'The endpoint physical-presence lease is not initially absent.'
}
$initialLink = @(& $endpointProbe --link-state 2>&1)
if ($LASTEXITCODE -ne 0 -or
    ($initialLink -join "`n") -notmatch '(?m)^Link disconnected:') {
    throw 'The media link is not initially disconnected.'
}

$outputRoot = Join-Path $projectRoot `
    'artifacts\v1-transport-worker-observer\trial'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$statePath = Join-Path $outputRoot "state-$stamp.json"
$logPath = Join-Path $outputRoot "agent-$stamp.log"
$resultPath = Join-Path $outputRoot "result-$stamp.json"
$agentPath = Join-Path $candidateRoot 'v1_presence_agent.exe'
$workerPath = Join-Path $candidateRoot 'v1_transport_worker_stub.exe'

Write-Host "V1 event-only transport-worker source: $($manifest.source_commit)"
Write-Host 'After armed: turn on XM5, select Native LDAC, and start one audio source.'
Write-Host 'Do not toggle Windows Bluetooth during this gate.'
Write-Host "Wait for 'event-only transport worker reported media started', then stop or close the audio source."
Write-Host "After 'contained engine stopped cleanly', turn off XM5."
Write-Host 'The worker implements only named events: no PCM read, LinkState write, driver IO, AVDTP, or Bluetooth OPEN exists in this candidate.'

$savedErrorActionPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    $captured = @(
        & $agentPath `
            --run-for-ms ($DurationSeconds * 1000) `
            --state $statePath `
            --endpoint-presence `
            --observe-render-demand `
            --observe-engine-ready `
            --exercise-transport-worker `
            --engine-executable $workerPath 2>&1 |
            Tee-Object -FilePath $logPath |
            ForEach-Object {
                $line = [string]$_
                Write-Host $line
                $line
            }
    )
    $agentExit = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $savedErrorActionPreference
}
if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
    throw "The V1 transport-worker agent did not publish state. Log: $logPath"
}
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$finalPresence = @(& $endpointProbe --presence 2>&1)
$finalPresenceExit = $LASTEXITCODE
$finalLink = @(& $endpointProbe --link-state 2>&1)
$finalLinkExit = $LASTEXITCODE
$passed = $agentExit -eq 0 -and
    [string]$state.state -eq 'stopped' -and
    [string]$state.mode -eq 'transport-worker-exercise' -and
    [string]$state.physical_presence -eq 'absent' -and
    [string]$state.render_demand -eq 'idle' -and
    [int]$state.connected_events -eq 1 -and
    [int]$state.disconnected_events -eq 1 -and
    [int]$state.render_query_failures -eq 0 -and
    [int]$state.render_started_events -ge 1 -and
    [int]$state.render_stopped_events -ge 1 -and
    [int]$state.child_processes_started -eq 1 -and
    [int]$state.engine_ready_events -eq 1 -and
    [int]$state.engine_exit_events -eq 1 -and
    [int]$state.engine_graceful_stops -eq 1 -and
    [int]$state.engine_start_failures -eq 0 -and
    [int]$state.engine_ready_timeouts -eq 0 -and
    [int]$state.engine_stop_failures -eq 0 -and
    [int]$state.engine_unexpected_exits -eq 0 -and
    [int]$state.transport_open_actions -eq 1 -and
    [int]$state.transport_open_executed -eq 1 -and
    [int]$state.transport_graceful_stop_actions -eq 1 -and
    [int]$state.transport_cancel_actions -eq 0 -and
    [int]$state.media_started_events -eq 1 -and
    [int]$state.media_stopped_events -eq 1 -and
    [int]$state.media_failed_events -eq 0 -and
    [int]$state.transport_stop_acknowledgements -eq 1 -and
    [int]$state.endpoint_presence_failures -eq 0 -and
    $finalPresenceExit -eq 0 -and
    ($finalPresence -join "`n") -match '(?m)^Physical presence absent:' -and
    $finalLinkExit -eq 0 -and
    ($finalLink -join "`n") -match '(?m)^Link disconnected:'

$result = [ordered]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    source_commit = [string]$manifest.source_commit
    passed = $passed
    agent_exit_code = $agentExit
    state = $statePath
    log = $logPath
    connected_events = [int]$state.connected_events
    disconnected_events = [int]$state.disconnected_events
    child_processes_started = [int]$state.child_processes_started
    engine_ready_events = [int]$state.engine_ready_events
    engine_graceful_stops = [int]$state.engine_graceful_stops
    transport_open_actions = [int]$state.transport_open_actions
    event_only_open_authorizations = [int]$state.transport_open_executed
    media_started_events = [int]$state.media_started_events
    media_stopped_events = [int]$state.media_stopped_events
    transport_stop_acknowledgements =
        [int]$state.transport_stop_acknowledgements
    actual_bluetooth_open_executed = 0
    final_presence = @($finalPresence | ForEach-Object { [string]$_ })
    final_link = @($finalLink | ForEach-Object { [string]$_ })
    no_automated_system_change = $true
}
$result | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $resultPath -Encoding UTF8

if (-not $passed) {
    throw "V1 event-only transport-worker trial failed. Actual Bluetooth OPEN remained zero. Result: $resultPath"
}

Write-Host 'V1 event-only transport-worker trial passed.'
Write-Host 'One generation-bound OPEN authorization, media-start acknowledgement, and graceful-stop acknowledgement completed.'
Write-Host 'Actual Bluetooth OPEN remained zero; media LinkState remained disconnected.'
Write-Host "Result: $resultPath"
