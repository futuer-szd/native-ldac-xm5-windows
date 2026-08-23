# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateRange(30, 300)]
    [int]$DurationSeconds = 90
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'native-ldac-baseline-common.ps1')

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$candidateRoot = Join-Path $projectRoot `
    'artifacts\v1-endpoint-presence\candidate'
$manifest = Get-Content -LiteralPath `
    (Join-Path $candidateRoot 'manifest.json') -Raw | ConvertFrom-Json
foreach ($file in @($manifest.files)) {
    $path = Join-Path $candidateRoot ([string]$file.path)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-Item -LiteralPath $path).Length -ne [long]$file.length -or
        -not (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.Equals(
            [string]$file.sha256,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Candidate file failed its hash check: $($file.path)"
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
    throw 'The V1 endpoint-presence candidate is not installed in the expected safe state.'
}
if (@($baseline.workspace_processes).Count -ne 0 -or
    @($baseline.scheduled_tasks).Count -ne 0) {
    throw 'Stop existing Native LDAC processes or tasks before the bounded trial.'
}

$connectionProbe = Join-Path $candidateRoot 'xm5_connection_probe.exe'
if ((Get-NativeLdacXm5BluetoothState -ProbePath $connectionProbe) -ne
    'disconnected') {
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
    'artifacts\v1-endpoint-presence\trial'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$statePath = Join-Path $outputRoot "state-$stamp.json"
$logPath = Join-Path $outputRoot "agent-$stamp.log"
$resultPath = Join-Path $outputRoot "result-$stamp.json"
$agentPath = Join-Path $candidateRoot 'v1_presence_agent.exe'

Write-Host "V1 endpoint-presence source: $($manifest.source_commit)"
Write-Host 'The endpoint is unplugged and media LinkState is disconnected.'
Write-Host 'After the agent says it is armed, turn on the XM5 normally.'
Write-Host 'Confirm Native LDAC becomes available, then turn the XM5 off normally.'
Write-Host 'Do not select Native LDAC or play audio during this presence-only gate.'

$savedErrorActionPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    $captured = @(
        & $agentPath `
            --run-for-ms ($DurationSeconds * 1000) `
            --state $statePath `
            --endpoint-presence 2>&1 |
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
    [int]$state.connected_events -eq 1 -and
    [int]$state.disconnected_events -eq 1 -and
    [int]$state.transport_open_actions -eq 0 -and
    [int]$state.child_processes_started -eq 0 -and
    [bool]$state.endpoint_sink_enabled -and
    [int]$state.endpoint_presence_updates -ge 2 -and
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
    endpoint_presence_updates = [int]$state.endpoint_presence_updates
    endpoint_presence_failures = [int]$state.endpoint_presence_failures
    transport_open_actions = [int]$state.transport_open_actions
    child_processes_started = [int]$state.child_processes_started
    final_presence = @($finalPresence | ForEach-Object { [string]$_ })
    final_link = @($finalLink | ForEach-Object { [string]$_ })
    no_system_change_during_trial = $true
}
$result | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $resultPath -Encoding UTF8

if (-not $passed) {
    throw "V1 endpoint-presence trial failed. Media transport was never authorized. Result: $resultPath"
}

Write-Host 'V1 endpoint-presence trial passed.'
Write-Host "Endpoint presence updates: $($state.endpoint_presence_updates); failures: 0."
Write-Host 'Media LinkState stayed disconnected; transport OPEN and child-process counts stayed 0.'
Write-Host "Result: $resultPath"
