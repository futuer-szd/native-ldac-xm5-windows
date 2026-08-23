# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1SwdChildTopology,
    [ValidateRange(5, 30)][int]$DurationSeconds = 10,
    [string]$CandidatePath,
    [string]$GoldenCheckpointPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-swd-child-topology-common.ps1')

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this gate from an elevated PowerShell 7 terminal.'
    }
}

function Get-PropertyValue {
    param(
        [Parameter(Mandatory = $true)][string]$InstanceId,
        [Parameter(Mandatory = $true)][string]$KeyName
    )
    $property = Get-PnpDeviceProperty -InstanceId $InstanceId `
        -KeyName $KeyName -ErrorAction SilentlyContinue
    if ($null -eq $property -or
        $null -eq $property.PSObject.Properties['Data']) {
        return $null
    }
    return $property.Data
}

function Get-DeviceEvidence {
    param([Parameter(Mandatory = $true)]$Device)
    $problem = Get-PropertyValue -InstanceId $Device.InstanceId `
        -KeyName 'DEVPKEY_Device_ProblemCode'
    $hardwareIdData = Get-PropertyValue -InstanceId $Device.InstanceId `
        -KeyName 'DEVPKEY_Device_HardwareIds'
    $hardwareIds = @($hardwareIdData | Where-Object {
        -not [string]::IsNullOrWhiteSpace([string]$_)
    })
    return [pscustomobject][ordered]@{
        instance_id = [string]$Device.InstanceId
        friendly_name = [string]$Device.FriendlyName
        present = [bool]$Device.Present
        service = [string](Get-PropertyValue -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Service')
        published_inf = [string](Get-PropertyValue `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverInfPath')
        container_id = [string](Get-PropertyValue `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_ContainerId')
        parent = [string](Get-PropertyValue -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Parent')
        problem_code = if ($null -eq $problem) { 0 } else { [int]$problem }
        hardware_ids = @($hardwareIds)
    }
}

function Get-ChildProbeDevices {
    $devices = @(Get-PnpDevice `
        -InstanceId $script:V1SwdChildInstanceId `
        -ErrorAction SilentlyContinue | Where-Object {
        [bool]$_.Present -and
        [string]$_.FriendlyName -eq $script:V1SwdChildFriendlyName
    })
    return @($devices | ForEach-Object { Get-DeviceEvidence -Device $_ })
}

function Get-GateSnapshot {
    param(
        [Parameter(Mandatory = $true)]$Topology,
        [Parameter(Mandatory = $true)][string]$VolumeProbe,
        [object[]]$ChildDevices
    )
    $capturedAt = (Get-Date).ToString('o')
    $capturedChildren = if ($null -eq $ChildDevices) {
        @(Get-ChildProbeDevices)
    } else {
        @($ChildDevices)
    }
    $transportDevice = Get-PnpDevice -InstanceId `
        ([string]$Topology.transport.instance_id) -ErrorAction Stop
    $endpointDevice = Get-PnpDevice -InstanceId `
        ([string]$Topology.endpoint.instance_id) -ErrorAction Stop
    $volumeText = @(& $VolumeProbe --info --all 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw 'The endpoint volume snapshot failed.'
    }
    $volumeBytes = [Text.Encoding]::UTF8.GetBytes($volumeText -join "`n")
    $volumeHash = [Convert]::ToHexString(
        [Security.Cryptography.SHA256]::HashData($volumeBytes))
    $packages = @(Get-WindowsDriver -Online -All |
        Where-Object {
            (Split-Path -Leaf ([string]$_.OriginalFileName)) -in @(
                'ldacnative.inf', 'nativeldacaudio.inf')
        } | ForEach-Object {
            [string]$_.Driver + '|' +
            (Split-Path -Leaf ([string]$_.OriginalFileName)) + '|' +
            [string]$_.Version
    })
    return [pscustomobject][ordered]@{
        captured_at = $capturedAt
        transport = Get-DeviceEvidence -Device $transportDevice
        endpoint = Get-DeviceEvidence -Device $endpointDevice
        child_devices = @($capturedChildren)
        driver_packages = @($packages)
        endpoint_volume_sha256 = $volumeHash
        endpoint_volume_text = @($volumeText)
    }
}

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The SWD child topology gate requires PowerShell 7.'
}
Assert-Administrator
if (-not $ConfirmV1SwdChildTopology) {
    throw 'Refusing to create a bounded software child without -ConfirmV1SwdChildTopology.'
}
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $projectRoot `
        'artifacts\v1-volume-sync\candidate'
}
$candidate = Get-V1SwdChildTopologyCandidate `
    -CandidatePath $CandidatePath
$head = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$status = @(& git.exe -C $projectRoot status --porcelain `
    --untracked-files=all)
if ($LASTEXITCODE -ne 0 -or $status.Count -ne 0 -or
    $head -cne [string]$candidate.manifest.source_commit) {
    throw 'The SWD topology gate requires the exact clean candidate source.'
}
if ([string]::IsNullOrWhiteSpace($GoldenCheckpointPath)) {
    $latestGolden = Join-Path $projectRoot `
        'artifacts\v1-golden\latest.txt'
    $GoldenCheckpointPath = (Get-Content -LiteralPath $latestGolden `
        -Raw).Trim()
}
& (Join-Path $candidate.root 'verify-v1-golden-checkpoint.ps1') `
    -CheckpointPath $GoldenCheckpointPath
if ($LASTEXITCODE -ne 0) {
    throw 'The golden checkpoint failed integrity verification.'
}

$topologyJson = & (Join-Path $candidate.root `
    'get-v1-volume-sync-topology.ps1') -AsJson `
    -ProbeRoot $candidate.root -ProjectRoot $projectRoot
$topology = ($topologyJson -join "`n") | ConvertFrom-Json
if (-not $topology.decision.valid -or
    $topology.decision.swd_child_candidate_required -ne $true -or
    $topology.decision.write_authorization -ne $false -or
    [string]$topology.transport.service -ne 'LdacNative' -or
    [string]$topology.endpoint.service -ne 'NativeLdacAudio') {
    throw 'The current topology is not eligible for the driverless child gate.'
}
if (@(Get-ChildProbeDevices).Count -ne 0) {
    throw 'A stale SWD topology probe is already present.'
}

$trialRoot = Join-Path $projectRoot 'artifacts\v1-volume-sync\trial'
$directory = Join-Path $trialRoot `
    ('driverless-child-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$before = Get-GateSnapshot -Topology $topology `
    -VolumeProbe (Join-Path $candidate.root 'endpoint_volume_probe.exe')
$before | ConvertTo-Json -Depth 8 | Set-Content `
    -LiteralPath (Join-Path $directory 'before.json') -Encoding utf8NoBOM

Write-Host 'V1 driverless SWD child topology preflight passed.'
Write-Host 'This gate creates no audio endpoint and binds no driver.'
Write-Host "The hidden child exists for at most $DurationSeconds seconds and is removed by closing its handle."
Write-Host 'Playback is not needed. Do not toggle Bluetooth or change endpoint settings.'
if (-not $PSCmdlet.ShouldProcess(
        'one bounded driverless child of the XM5 A2DP PDO',
        'Create, observe, close, and prove zero driver/audio side effects')) {
    return
}

$probePath = Join-Path $candidate.root 'v1_swd_child_probe.exe'
$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $probePath
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$startInfo.StandardOutputEncoding = [Text.Encoding]::UTF8
$startInfo.StandardErrorEncoding = [Text.Encoding]::UTF8
foreach ($argument in @(
        '--create',
        '--parent', [string]$topology.transport.instance_id,
        '--container', [string]$topology.transport.container_id,
        '--duration-seconds', [string]$DurationSeconds,
        '--confirm-driverless-probe')) {
    $startInfo.ArgumentList.Add($argument)
}
$process = [Diagnostics.Process]::new()
$process.StartInfo = $startInfo
$started = $process.Start()
if (-not $started) {
    throw 'The bounded SWD child probe did not start.'
}
$stdoutTask = $process.StandardOutput.ReadToEndAsync()
$stderrTask = $process.StandardError.ReadToEndAsync()
$during = $null
$deadline = [DateTime]::UtcNow.AddSeconds(5)
while ([DateTime]::UtcNow -lt $deadline) {
    $liveChildren = @(Get-ChildProbeDevices)
    if ($liveChildren.Count -eq 1) {
        $during = Get-GateSnapshot -Topology $topology `
            -VolumeProbe (Join-Path $candidate.root `
                'endpoint_volume_probe.exe') -ChildDevices $liveChildren
        break
    }
    Start-Sleep -Milliseconds 100
}
$completed = $process.WaitForExit(($DurationSeconds + 10) * 1000)
$forced = $false
if (-not $completed) {
    $forced = $true
    $process.Kill($true)
    $process.WaitForExit()
}
$stdout = $stdoutTask.GetAwaiter().GetResult()
$stderr = $stderrTask.GetAwaiter().GetResult()
$probeExit = $process.ExitCode
$process.Dispose()
$stdout | Set-Content -LiteralPath (Join-Path $directory 'probe.out.log') `
    -Encoding utf8NoBOM
$stderr | Set-Content -LiteralPath (Join-Path $directory 'probe.err.log') `
    -Encoding utf8NoBOM

$absenceDeadline = [DateTime]::UtcNow.AddSeconds(5)
while ([DateTime]::UtcNow -lt $absenceDeadline -and
    @(Get-ChildProbeDevices).Count -ne 0) {
    Start-Sleep -Milliseconds 100
}
$after = Get-GateSnapshot -Topology $topology `
    -VolumeProbe (Join-Path $candidate.root 'endpoint_volume_probe.exe')
if ($null -eq $during) {
    $during = $after
}
$during | ConvertTo-Json -Depth 8 | Set-Content `
    -LiteralPath (Join-Path $directory 'during.json') -Encoding utf8NoBOM
$after | ConvertTo-Json -Depth 8 | Set-Content `
    -LiteralPath (Join-Path $directory 'after.json') -Encoding utf8NoBOM
$probe = [pscustomobject][ordered]@{
    completed = $completed
    timed_out = -not $completed
    forced_termination = $forced
    exit_code = $probeExit
}
$passed = Test-V1SwdChildLifecycleEvidence -Before $before `
    -During $during -After $after -Probe $probe `
    -ExpectedParent ([string]$topology.transport.instance_id) `
    -ExpectedContainer ([string]$topology.transport.container_id)
$result = [ordered]@{
    schema_version = 1
    policy_version = $script:V1SwdChildTopologyPolicyVersion
    passed = $passed
    source_commit = [string]$candidate.manifest.source_commit
    candidate = $candidate.root
    golden_checkpoint = [IO.Path]::GetFullPath($GoldenCheckpointPath)
    duration_seconds = $DurationSeconds
    topology = $topology.decision
    before = 'before.json'
    during = 'during.json'
    after = 'after.json'
    probe = $probe
    safety = [ordered]@{
        driver_installed = $false
        audio_endpoint_created = $false
        pnp_restarted = $false
        bluetooth_toggled = $false
        endpoint_written = $false
        avrcp_written = $false
    }
}
$resultPath = Join-Path $directory 'result.json'
$result | ConvertTo-Json -Depth 8 | Set-Content `
    -LiteralPath $resultPath -Encoding utf8NoBOM
if (-not $passed) {
    throw "V1 driverless SWD child topology gate failed. Result: $resultPath"
}
Write-Host 'V1 driverless SWD child topology gate passed.'
Write-Host 'One child used the exact XM5 A2DP parent/container and disappeared after handle close.'
Write-Host 'No driver package, Native endpoint binding, endpoint volume state, Bluetooth state, or audio path changed.'
Write-Host "Result: $resultPath"
