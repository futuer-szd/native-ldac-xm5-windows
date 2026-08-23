# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateRange(90, 300)]
    [int]$DurationSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'native-ldac-baseline-common.ps1')

function Get-ReadTotal([string[]]$Lines) {
    $match = [regex]::Match(
        ($Lines -join "`n"),
        '(?m)^Totals: written \d+, read (\d+), dropped \d+ bytes\.$')
    if (-not $match.Success) {
        throw 'Could not parse the Native PCM read total.'
    }
    return [UInt64]$match.Groups[1].Value
}

Assert-LegacyAdministrator
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$candidateRoot = Join-Path $projectRoot `
    'artifacts\v1-pcm-encode-observer\candidate'
$manifestPath = Join-Path $candidateRoot 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'The V1 PCM/encode observer manifest is missing.'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$capabilities = @($manifest.capabilities | ForEach-Object { [string]$_ })
foreach ($required in @(
        'job_object_containment',
        'Native_PCM_read',
        'LDAC_HQ_encode_discard',
        'independent_PCM_consumer_lease',
        'engine_ready_proves_PCM_read_and_encode',
        'ready_after_first_encoded_frame',
        'transport_OPEN_requested_not_executed',
        'no_media_LinkState_write',
        'no_Bluetooth_open')) {
    if ($required -notin $capabilities) {
        throw "The candidate lacks capability: $required"
    }
}
if ([int]$manifest.manifest_version -ne 1 -or
    $manifest.source_dirty -ne $false -or
    [int]$manifest.required_pcm_abi -ne 2 -or
    [int]$manifest.required_consumer_lease_abi -ne 1 -or
    [int]$manifest.required_presence_abi -ne 1 -or
    [int]$manifest.render_poll_ms -ne 250 -or
    [int]$manifest.engine_ready_timeout_ms -ne 3000) {
    throw 'The V1 PCM/encode observer manifest contract is invalid.'
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
$initialConsumer = @(& $endpointProbe --consumer-lease 2>&1)
if ($LASTEXITCODE -ne 0 -or
    ($initialConsumer -join "`n") -notmatch
        '(?m)^PCM consumer lease released: generation 0\.$') {
    throw 'The installed endpoint lacks an initially released PCM consumer lease. Update the V1 endpoint candidate first.'
}
$initialPresence = @(& $endpointProbe --presence 2>&1)
if ($LASTEXITCODE -ne 0 -or
    ($initialPresence -join "`n") -notmatch
        '(?m)^Physical presence absent:') {
    throw 'The endpoint physical-presence lease is not initially absent.'
}
$initialReadTotal = Get-ReadTotal -Lines $initialPresence
$initialLink = @(& $endpointProbe --link-state 2>&1)
if ($LASTEXITCODE -ne 0 -or
    ($initialLink -join "`n") -notmatch '(?m)^Link disconnected:') {
    throw 'The media link is not initially disconnected.'
}

$outputRoot = Join-Path $projectRoot `
    'artifacts\v1-pcm-encode-observer\trial'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$statePath = Join-Path $outputRoot "state-$stamp.json"
$logPath = Join-Path $outputRoot "agent-$stamp.log"
$resultPath = Join-Path $outputRoot "result-$stamp.json"
$agentPath = Join-Path $candidateRoot 'v1_presence_agent.exe'
$enginePath = Join-Path $candidateRoot 'v1_pcm_encode_engine.exe'

Write-Host "V1 PCM/encode observer source: $($manifest.source_commit)"
Write-Host "Initial Native PCM read total: $initialReadTotal bytes."
Write-Host 'After armed: turn on XM5, select Native LDAC, and play one source for at least five seconds.'
Write-Host 'Do not toggle Windows Bluetooth during this gate. If XM5 does not connect, let the bounded gate stop and report the result.'
Write-Host "Wait for 'V1 engine ready', then stop or close that source."
Write-Host "After 'V1 contained engine stopped cleanly', turn off XM5."
Write-Host 'PCM is read and encoded as HQ LDAC, then discarded. Bluetooth OPEN execution is disabled, so no sound will reach XM5.'

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
            --engine-executable $enginePath 2>&1 |
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
    throw "The V1 PCM/encode agent did not publish state. Log: $logPath"
}
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$finalPresence = @(& $endpointProbe --presence 2>&1)
$finalPresenceExit = $LASTEXITCODE
$finalReadTotal = Get-ReadTotal -Lines $finalPresence
$finalConsumer = @(& $endpointProbe --consumer-lease 2>&1)
$finalConsumerExit = $LASTEXITCODE
$finalLink = @(& $endpointProbe --link-state 2>&1)
$finalLinkExit = $LASTEXITCODE
$passed = $agentExit -eq 0 -and
    [string]$state.state -eq 'stopped' -and
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
    [int]$state.transport_open_executed -eq 0 -and
    [int]$state.endpoint_presence_failures -eq 0 -and
    $finalConsumerExit -eq 0 -and
    ($finalConsumer -join "`n") -match
        '(?m)^PCM consumer lease released: generation 0\.$' -and
    $finalPresenceExit -eq 0 -and
    ($finalPresence -join "`n") -match
        '(?m)^Physical presence absent:' -and
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
    initial_pcm_read_bytes = $initialReadTotal
    final_pcm_read_bytes = $finalReadTotal
    pcm_read_delta_bytes =
        ([Int64]$finalReadTotal - [Int64]$initialReadTotal)
    pcm_read_and_encode_proven_by_engine_ready =
        ([int]$state.engine_ready_events -eq 1 -and
         [int]$state.last_engine_exit_code -eq 0)
    latest_epoch_counter_reset_observed =
        ([int]$state.render_started_events -gt 1 -and
         $finalReadTotal -le $initialReadTotal)
    child_processes_started = [int]$state.child_processes_started
    engine_ready_events = [int]$state.engine_ready_events
    engine_graceful_stops = [int]$state.engine_graceful_stops
    last_engine_exit_code = [int64]$state.last_engine_exit_code
    transport_open_actions = [int]$state.transport_open_actions
    transport_open_executed = [int]$state.transport_open_executed
    final_presence = @($finalPresence | ForEach-Object { [string]$_ })
    final_consumer_lease = @($finalConsumer | ForEach-Object {
        [string]$_
    })
    final_link = @($finalLink | ForEach-Object { [string]$_ })
    no_automated_system_change = $true
}
$result | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $resultPath -Encoding UTF8

if (-not $passed) {
    throw "V1 PCM/encode observer trial failed. No Bluetooth OPEN was executed. Result: $resultPath"
}

Write-Host 'V1 PCM/encode observer trial passed.'
Write-Host 'Engine ready proves one complete Native PCM read and one successful LDAC frame.'
if ([int]$state.render_started_events -gt 1) {
    Write-Host 'Additional WaveRT RUN/STOP epochs were observed; per-epoch PCM counters may have reset.'
}
Write-Host 'The contained engine encoded LDAC and stopped cleanly; transport OPEN execution stayed zero.'
Write-Host "Result: $resultPath"
