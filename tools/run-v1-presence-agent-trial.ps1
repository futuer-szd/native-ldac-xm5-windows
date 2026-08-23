# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateRange(15, 300)]
    [int]$DurationSeconds = 90,

    [string]$OutputRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'native-ldac-baseline-common.ps1')

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$bundleRoot = Join-Path $projectRoot 'artifacts\v1-presence'
$manifestPath = Join-Path $bundleRoot 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'The staged V1 presence bundle is missing. Build it from a clean commit first.'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$capabilities = @($manifest.capabilities | ForEach-Object { [string]$_ })
$agentPath = Join-Path $bundleRoot ([string]$manifest.agent_file)
$probePath = Join-Path $bundleRoot `
    ([string]$manifest.connection_probe_file)
if (-not (Test-Path -LiteralPath $agentPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $probePath -PathType Leaf)) {
    throw 'The staged V1 presence bundle is incomplete.'
}
$agentHash = (Get-FileHash -LiteralPath $agentPath -Algorithm SHA256).Hash
$probeHash = (Get-FileHash -LiteralPath $probePath -Algorithm SHA256).Hash
if ([int]$manifest.manifest_version -ne 1 -or
    $manifest.source_dirty -ne $false -or
    'exact_XM5_ACL_event_presence' -notin $capabilities -or
    'V1_lifecycle_reducer_presence_only' -notin $capabilities -or
    'no_child_process' -notin $capabilities -or
    'no_transport_open' -notin $capabilities -or
    -not $agentHash.Equals(
        [string]$manifest.agent_sha256,
        [StringComparison]::OrdinalIgnoreCase) -or
    -not $probeHash.Equals(
        [string]$manifest.connection_probe_sha256,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The staged V1 presence bundle failed its manifest or hash check.'
}

$latestBackupPath = Join-Path $projectRoot `
    'artifacts\driver-test\latest-backup.txt'
if (-not (Test-Path -LiteralPath $latestBackupPath -PathType Leaf)) {
    throw 'Original A2DP latest-backup.txt is missing.'
}
$backupPath = (Get-Content -LiteralPath $latestBackupPath -Raw).Trim()
$baseline = Get-NativeLdacBaselineSnapshot -BackupPath $backupPath
if (-not $baseline.clean_original_a2dp) {
    Write-NativeLdacBaselineSummary -Snapshot $baseline
    throw 'The V1 presence trial requires the clean original-A2DP baseline.'
}

$initialOutput = @(& $probePath --state 2>&1)
$initialExit = $LASTEXITCODE
if ($initialExit -ne 10 -or
    ($initialOutput -join "`n") -notmatch
        '(?m)^XM5 Bluetooth state: disconnected\.$') {
    throw 'Turn the XM5 off normally, wait until it is disconnected, and run the trial again.'
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $projectRoot 'artifacts\v1-presence-trial'
}
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$statePath = Join-Path $OutputRoot "state-$stamp.json"
$logPath = Join-Path $OutputRoot "agent-$stamp.log"
$resultPath = Join-Path $OutputRoot "result-$stamp.json"

Write-Host "V1 presence-only source: $($manifest.source_commit)"
Write-Host 'The original A2DP baseline is clean and the XM5 is disconnected.'
Write-Host 'After the agent says it is armed, turn on the XM5 normally.'
Write-Host 'After Windows finishes connecting, turn the XM5 off normally before the timer ends.'
Write-Host 'Do not play audio, toggle Windows Bluetooth, or run another LDAC command.'

$savedErrorActionPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    $captured = @(
        & $agentPath `
            --run-for-ms ($DurationSeconds * 1000) `
            --state $statePath 2>&1 |
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
    throw "The V1 presence agent did not publish state. Log: $logPath"
}
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$passed = $agentExit -eq 0 -and
    [string]$state.state -eq 'stopped' -and
    [string]$state.physical_presence -eq 'absent' -and
    [int]$state.connected_events -ge 1 -and
    [int]$state.disconnected_events -ge 1 -and
    [int]$state.publish_present_actions -ge 1 -and
    [int]$state.publish_absent_actions -ge 1 -and
    [int]$state.transport_open_actions -eq 0 -and
    [int]$state.child_processes_started -eq 0 -and
    ($captured -join "`n") -match '(?m)^V1 presence agent armed'

$result = [ordered]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    source_commit = [string]$manifest.source_commit
    duration_seconds = $DurationSeconds
    agent_exit_code = $agentExit
    passed = $passed
    state = $statePath
    log = $logPath
    connected_events = [int]$state.connected_events
    disconnected_events = [int]$state.disconnected_events
    transport_open_actions = [int]$state.transport_open_actions
    child_processes_started = [int]$state.child_processes_started
    no_system_change = $true
}
$result | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $resultPath -Encoding UTF8

if (-not $passed) {
    throw "The bounded V1 presence-only trial did not pass. No transport or child process was started. Log: $logPath"
}

Write-Host 'V1 presence-only agent trial passed.'
Write-Host 'The reducer consumed one real connect/disconnect generation.'
Write-Host 'Transport OPEN actions: 0; child processes started: 0.'
Write-Host "Log: $logPath"
Write-Host "State: $statePath"
Write-Host "Result: $resultPath"
Write-Host 'No Bluetooth request, driver, endpoint, service, task, or system setting was changed.'
