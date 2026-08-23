# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$ProbePath,
    [string]$OutputRoot,
    [string[]]$ProbeArguments = @('--discover', '--open-attempts', '1')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script from an elevated Windows PowerShell.'
    }
}

Assert-Administrator
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($ProbePath)) {
    $ProbePath = Join-Path $projectRoot 'artifacts\driver-test\transport_probe.exe'
}
$ProbePath = [System.IO.Path]::GetFullPath($ProbePath)
if (-not (Test-Path -LiteralPath $ProbePath -PathType Leaf)) {
    throw "Probe not found: $ProbePath"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $projectRoot 'artifacts\driver-test\trace'
}
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$capturePath = Join-Path $OutputRoot (Get-Date -Format 'yyyyMMdd-HHmmss')
New-Item -ItemType Directory -Path $capturePath -Force | Out-Null

$channels = @(
    'Microsoft-Windows-BTH-BTHPORT/HCI',
    'Microsoft-Windows-BTH-BTHPORT/L2CAP'
)
$originalStates = @{}
$captureStart = Get-Date
$probeExitCode = 1
$channelResults = @()

try {
    foreach ($channel in $channels) {
        $configuration = @(& wevtutil.exe gl $channel 2>&1)
        if ($LASTEXITCODE -ne 0) {
            throw "Unable to query event channel: $channel"
        }
        $originalStates[$channel] = [bool]($configuration -match '^enabled:\s*true\s*$')
        if (-not $originalStates[$channel]) {
            & wevtutil.exe sl $channel /e:true /q:true
            if ($LASTEXITCODE -ne 0) {
                throw "Unable to enable event channel: $channel"
            }
        }
    }

    $probeLog = Join-Path $capturePath 'probe-output.txt'
    $savedErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $probeOutput = @(& $ProbePath @ProbeArguments 2>&1)
        $probeExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    $probeOutput | Set-Content -LiteralPath $probeLog -Encoding UTF8
    $probeOutput | ForEach-Object { Write-Host $_ }
    Start-Sleep -Seconds 1

    foreach ($channel in $channels) {
        $safeName = ($channel -replace '[^A-Za-z0-9.-]', '_')
        $evtxPath = Join-Path $capturePath "$safeName.evtx"
        & wevtutil.exe epl $channel $evtxPath /ow:true
        if ($LASTEXITCODE -ne 0) {
            throw "Unable to export event channel: $channel"
        }

        $events = @(Get-WinEvent -FilterHashtable @{
            LogName = $channel
            StartTime = $captureStart
        } -Oldest -ErrorAction SilentlyContinue)
        $eventXml = @($events | ForEach-Object { $_.ToXml() })
        $eventXml | Set-Content -LiteralPath (Join-Path $capturePath "$safeName.xml") -Encoding UTF8
        $channelResults += [pscustomobject][ordered]@{
            channel = $channel
            enabled_before = [bool]$originalStates[$channel]
            event_count = $events.Count
            evtx = $evtxPath
            xml = Join-Path $capturePath "$safeName.xml"
        }
    }
} finally {
    foreach ($channel in $channels) {
        if ($originalStates.ContainsKey($channel) -and
            -not $originalStates[$channel]) {
            & wevtutil.exe sl $channel /e:false | Out-Null
        }
    }
}

[pscustomobject][ordered]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    capture_started_at = $captureStart.ToString('o')
    probe_path = $ProbePath
    probe_arguments = @($ProbeArguments)
    probe_exit_code = $probeExitCode
    channels = @($channelResults)
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath `
    (Join-Path $capturePath 'capture.json') -Encoding UTF8

Write-Host "Bluetooth trace captured at: $capturePath"
exit $probeExitCode
