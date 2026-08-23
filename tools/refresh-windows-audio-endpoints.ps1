# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmAudioServiceRestart,
    [switch]$InspectOnly
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

function Get-Xm5CaptureEndpoint {
    $endpoints = @(Get-PnpDevice -Class AudioEndpoint -ErrorAction SilentlyContinue)
    return @($endpoints | Where-Object {
        $_.Present -eq $true -and
        $_.InstanceId -like 'SWD\MMDEVAPI\{0.0.1*' -and
        $_.FriendlyName -like '*WH-1000XM5*'
    })
}

function Get-ServiceSnapshot {
    $endpointBuilder = Get-Service -Name AudioEndpointBuilder
    $runningDependents = @($endpointBuilder.DependentServices | Where-Object {
        $_.Status -eq [System.ServiceProcess.ServiceControllerStatus]::Running
    })
    return [pscustomobject]@{
        EndpointBuilderStatus = $endpointBuilder.Status
        RunningDependentNames = @($runningDependents.Name)
        AudioServiceStatus = (Get-Service -Name Audiosrv).Status
    }
}

$beforeCapture = @(Get-Xm5CaptureEndpoint)
$beforeServices = Get-ServiceSnapshot
if ($InspectOnly) {
    Write-Host "AudioEndpointBuilder: $($beforeServices.EndpointBuilderStatus)"
    Write-Host "Audiosrv: $($beforeServices.AudioServiceStatus)"
    Write-Host "Running dependent services: $(@($beforeServices.RunningDependentNames) -join ', ')"
    Write-Host "Present XM5 capture endpoints: $($beforeCapture.Count)"
    foreach ($endpoint in $beforeCapture) {
        Write-Host "Capture: $($endpoint.FriendlyName) [$($endpoint.InstanceId)]"
    }
    return
}

Assert-Administrator
if (-not $ConfirmAudioServiceRestart) {
    throw 'Refusing to interrupt Windows audio. Re-run with -ConfirmAudioServiceRestart.'
}

$activeProbe = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('transport_probe.exe', 'audio_endpoint_probe.exe')
})
if ($activeProbe.Count -ne 0) {
    $processSummary = @($activeProbe | ForEach-Object {
        "$($_.Name) (PID $($_.ProcessId))"
    })
    throw "Stop the LDAC/probe session first: $($processSummary -join ', ')"
}

$dependentNames = @($beforeServices.RunningDependentNames)
$target = 'Windows Audio Endpoint Builder and its currently running dependent services'
$action = 'Restart audio endpoint enumeration; all system audio will be interrupted briefly'
if (-not $PSCmdlet.ShouldProcess($target, $action)) {
    return
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputRoot = Join-Path $projectRoot 'artifacts\hfp-recovery'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logPath = Join-Path $outputRoot "refresh-audio-endpoints-$timestamp.log"
$logLines = @(
    "Started: $((Get-Date).ToString('o'))",
    "Before AudioEndpointBuilder: $($beforeServices.EndpointBuilderStatus)",
    "Before Audiosrv: $($beforeServices.AudioServiceStatus)",
    "Running dependents to restore: $($dependentNames -join ', ')",
    "Before capture endpoints: $($beforeCapture.Count)"
)

$restartError = $null
try {
    Stop-Service -Name AudioEndpointBuilder -Force -ErrorAction Stop
    (Get-Service -Name AudioEndpointBuilder).WaitForStatus(
        [System.ServiceProcess.ServiceControllerStatus]::Stopped,
        [timespan]::FromSeconds(15)
    )

    Start-Service -Name AudioEndpointBuilder -ErrorAction Stop
    (Get-Service -Name AudioEndpointBuilder).WaitForStatus(
        [System.ServiceProcess.ServiceControllerStatus]::Running,
        [timespan]::FromSeconds(15)
    )

    foreach ($dependentName in $dependentNames) {
        Start-Service -Name $dependentName -ErrorAction Stop
        (Get-Service -Name $dependentName).WaitForStatus(
            [System.ServiceProcess.ServiceControllerStatus]::Running,
            [timespan]::FromSeconds(15)
        )
    }
} catch {
    $restartError = $_.Exception.Message
} finally {
    $builder = Get-Service -Name AudioEndpointBuilder -ErrorAction SilentlyContinue
    if ($builder -and $builder.Status -ne [System.ServiceProcess.ServiceControllerStatus]::Running) {
        try {
            Start-Service -Name AudioEndpointBuilder -ErrorAction Stop
        } catch {
            $logLines += "Emergency AudioEndpointBuilder start failed: $($_.Exception.Message)"
        }
    }
    foreach ($dependentName in $dependentNames) {
        $dependent = Get-Service -Name $dependentName -ErrorAction SilentlyContinue
        if ($dependent -and $dependent.Status -ne [System.ServiceProcess.ServiceControllerStatus]::Running) {
            try {
                Start-Service -Name $dependentName -ErrorAction Stop
            } catch {
                $logLines += "Emergency $dependentName start failed: $($_.Exception.Message)"
            }
        }
    }
}

$afterCapture = @()
if (-not $restartError) {
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        Start-Sleep -Seconds 1
        $afterCapture = @(Get-Xm5CaptureEndpoint)
        if ($afterCapture.Count -ne 0) {
            break
        }
    }
}

$afterBuilder = Get-Service -Name AudioEndpointBuilder -ErrorAction SilentlyContinue
$afterAudio = Get-Service -Name Audiosrv -ErrorAction SilentlyContinue
$logLines += "Completed: $((Get-Date).ToString('o'))"
$logLines += "After AudioEndpointBuilder: $($afterBuilder.Status)"
$logLines += "After Audiosrv: $($afterAudio.Status)"
$logLines += "After capture endpoints: $($afterCapture.Count)"
if ($restartError) {
    $logLines += "Restart error: $restartError"
}
foreach ($endpoint in $afterCapture) {
    $logLines += "Capture: $($endpoint.FriendlyName) [$($endpoint.InstanceId)]"
}
$logLines | Set-Content -LiteralPath $logPath -Encoding UTF8

Write-Host "Log: $logPath"
if ($restartError) {
    throw "Windows audio services could not be restarted cleanly: $restartError"
}
Write-Host 'Windows audio services restarted and returned to Running.'
if ($afterCapture.Count -eq 0) {
    Write-Warning 'Windows audio endpoint enumeration restarted successfully, but the XM5 capture endpoint did not return within 20 seconds.'
    exit 2
}
Write-Host "Restored microphone endpoint: $($afterCapture[0].FriendlyName)"
