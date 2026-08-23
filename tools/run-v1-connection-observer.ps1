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
$diagnosticRoot = Join-Path $projectRoot 'artifacts\diagnostics'
$probePath = Join-Path $diagnosticRoot 'xm5_connection_probe.exe'
$manifestPath = Join-Path $diagnosticRoot `
    'xm5_connection_probe.manifest.json'
if (-not (Test-Path -LiteralPath $probePath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'The staged read-only XM5 observer is missing. Build it from a clean commit first.'
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$capabilities = @($manifest.capabilities | ForEach-Object { [string]$_ })
$probeHash = (Get-FileHash -LiteralPath $probePath -Algorithm SHA256).Hash
if ([int]$manifest.manifest_version -ne 3 -or
    $manifest.source_dirty -ne $false -or
    [string]$manifest.file_name -ne 'xm5_connection_probe.exe' -or
    'BluetoothFindFirstDevice_fConnected_no_inquiry' -notin $capabilities -or
    'GUID_BLUETOOTH_HCI_EVENT_acl_transition' -notin $capabilities -or
    'BluetoothIsConnectable_radio_state' -notin $capabilities -or
    'read_only_acl_pdo_render_timeline' -notin $capabilities -or
    -not $probeHash.Equals(
        [string]$manifest.sha256,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The staged read-only XM5 observer failed its manifest or hash check.'
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
    throw 'The read-only observer requires the clean original-A2DP baseline.'
}

$initialOutput = @(& $probePath --state 2>&1)
$initialExit = $LASTEXITCODE
if ($initialExit -ne 10 -or
    ($initialOutput -join "`n") -notmatch
        '(?m)^XM5 Bluetooth state: disconnected\.$') {
    throw 'Turn the XM5 off normally, wait until it is disconnected, and run the observer again.'
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $projectRoot 'artifacts\v1-observer'
}
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$logPath = Join-Path $OutputRoot "xm5-acl-timeline-$stamp.log"
$resultPath = Join-Path $OutputRoot "xm5-acl-timeline-$stamp.json"

Write-Host "Read-only XM5 observer source: $($manifest.source_commit)"
Write-Host 'The original A2DP baseline is clean and the XM5 is disconnected.'
Write-Host 'After the observer says it is armed, turn on the XM5 normally.'
Write-Host 'After Windows finishes connecting, turn the XM5 off normally before the timer ends.'
Write-Host 'Do not toggle Windows Bluetooth and do not run any LDAC command.'

$captured = @(
    & $probePath --observe-acl $DurationSeconds 2>&1 |
        Tee-Object -FilePath $logPath |
        ForEach-Object {
            $line = [string]$_
            Write-Host $line
            $line
        }
)
$observerExit = $LASTEXITCODE
$text = $captured -join [Environment]::NewLine
$connectedIndex = $text.IndexOf(
    'ACL connected.', [StringComparison]::Ordinal)
$disconnectedIndex = $text.IndexOf(
    'ACL disconnected.', [StringComparison]::Ordinal)
$passed = $observerExit -eq 0 -and
    $connectedIndex -ge 0 -and
    $disconnectedIndex -gt $connectedIndex

$result = [ordered]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    source_commit = [string]$manifest.source_commit
    duration_seconds = $DurationSeconds
    observer_exit_code = $observerExit
    acl_connected_observed = $connectedIndex -ge 0
    acl_disconnected_after_connect =
        $disconnectedIndex -gt $connectedIndex
    passed = $passed
    log = $logPath
    no_inquiry = $true
    no_connection_request = $true
    no_avdtp_open = $true
    no_system_change = $true
}
$result | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $resultPath -Encoding UTF8

if (-not $passed) {
    throw "The observer did not record one ACL connect followed by disconnect. No Bluetooth request or system change was made. Log: $logPath"
}

Write-Host 'V1 read-only connection observation passed.'
Write-Host 'One real XM5 ACL connect and its later disconnect were observed.'
Write-Host "Log: $logPath"
Write-Host "Result: $resultPath"
Write-Host 'No inquiry, connection request, AVDTP OPEN, driver, endpoint, service, task, or system setting was changed.'
