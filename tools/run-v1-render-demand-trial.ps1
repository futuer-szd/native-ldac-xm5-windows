# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateRange(60, 300)]
    [int]$DurationSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'native-ldac-baseline-common.ps1')

Assert-LegacyAdministrator
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$candidateRoot = Join-Path $projectRoot `
    'artifacts\v1-render-demand-observer\candidate'
$manifestPath = Join-Path $candidateRoot 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'The V1 render-demand observer manifest is missing.'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$capabilities = @($manifest.capabilities | ForEach-Object { [string]$_ })
if ([int]$manifest.manifest_version -ne 1 -or
    $manifest.source_dirty -ne $false -or
    [int]$manifest.required_pcm_abi -ne 2 -or
    [int]$manifest.required_presence_abi -ne 1 -or
    [int]$manifest.render_poll_ms -ne 250 -or
    [int]$manifest.render_confirmation_samples -ne 2 -or
    'connected_only_PCM_Info_GET' -notin $capabilities -or
    'render_actions_observed_not_executed' -notin $capabilities -or
    'no_PCM_read' -notin $capabilities -or
    'no_child_process' -notin $capabilities -or
    'no_transport_open' -notin $capabilities) {
    throw 'The V1 render-demand observer manifest contract is invalid.'
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
    @($baseline.native_audio_packages).Count -ne 1 -or
    [string]$presentDevices[0].service -ne 'NativeLdacAudio' -or
    [int]$presentDevices[0].problem_code -ne 0) {
    throw 'The installed V1 endpoint is not in the expected safe original-A2DP state.'
}
if (@($baseline.workspace_processes).Count -ne 0 -or
    @($baseline.scheduled_tasks).Count -ne 0) {
    throw 'Stop existing Native LDAC processes or tasks before the bounded observer trial.'
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
    'artifacts\v1-render-demand-observer\trial'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$statePath = Join-Path $outputRoot "state-$stamp.json"
$logPath = Join-Path $outputRoot "agent-$stamp.log"
$resultPath = Join-Path $outputRoot "result-$stamp.json"
$agentPath = Join-Path $candidateRoot 'v1_presence_agent.exe'

Write-Host "V1 render-demand observer source: $($manifest.source_commit)"
Write-Host 'The endpoint is unplugged and media LinkState is disconnected.'
Write-Host 'After armed: turn on XM5, select Native LDAC, and play one source for at least five seconds.'
Write-Host "Then stop or close that source and wait for 'V1 render stopped'."
Write-Host "After render stopped, turn off XM5 and wait for 'V1 ACL disconnected'."
Write-Host 'No audio will reach XM5 in this observer gate; no engine or Bluetooth transport can start.'

$savedErrorActionPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    $captured = @(
        & $agentPath `
            --run-for-ms ($DurationSeconds * 1000) `
            --state $statePath `
            --endpoint-presence `
            --observe-render-demand 2>&1 |
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
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$finalPresence = @(& $endpointProbe --presence 2>&1)
$finalPresenceExit = $LASTEXITCODE
$finalLink = @(& $endpointProbe --link-state 2>&1)
$finalLinkExit = $LASTEXITCODE
$passed = $agentExit -eq 0 -and
    [string]$state.state -eq 'stopped' -and
    [string]$state.physical_presence -eq 'absent' -and
    [string]$state.render_demand -eq 'idle' -and
    [int]$state.connected_events -eq 1 -and
    [int]$state.disconnected_events -eq 1 -and
    [bool]$state.endpoint_sink_enabled -and
    [bool]$state.render_observer_enabled -and
    [int]$state.render_query_count -gt 0 -and
    [int]$state.render_query_failures -eq 0 -and
    [int]$state.render_started_events -ge 1 -and
    [int]$state.render_stopped_events -ge 1 -and
    [int]$state.engine_start_requests -ge 1 -and
    [int]$state.engine_stop_requests -ge 1 -and
    [int]$state.transport_open_actions -eq 0 -and
    [int]$state.child_processes_started -eq 0 -and
    [int]$state.endpoint_presence_failures -eq 0 -and
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
    render_query_count = [int]$state.render_query_count
    render_query_failures = [int]$state.render_query_failures
    render_started_events = [int]$state.render_started_events
    render_stopped_events = [int]$state.render_stopped_events
    engine_start_requests = [int]$state.engine_start_requests
    engine_stop_requests = [int]$state.engine_stop_requests
    transport_open_actions = [int]$state.transport_open_actions
    child_processes_started = [int]$state.child_processes_started
    final_presence = @($finalPresence | ForEach-Object { [string]$_ })
    final_link = @($finalLink | ForEach-Object { [string]$_ })
    no_automated_system_change = $true
}
$result | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $resultPath -Encoding UTF8

if (-not $passed) {
    throw "V1 render-demand observer trial failed. No engine or transport action was executed. Result: $resultPath"
}

Write-Host 'V1 render-demand observer trial passed.'
Write-Host "Render transitions: $($state.render_started_events) start / $($state.render_stopped_events) stop; query failures: 0."
Write-Host 'Engine actions were observed only; transport OPEN and child-process counts stayed 0.'
Write-Host "Result: $resultPath"
