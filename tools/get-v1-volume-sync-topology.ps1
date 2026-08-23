# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [switch]$AsJson,
    [string]$OutputPath,
    [string]$ProbeRoot,
    [string]$ProjectRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-volume-sync-topology-common.ps1')

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The V1 volume-sync topology probe requires PowerShell 7.'
}

function Get-PropertyText {
    param(
        [Parameter(Mandatory = $true)][string]$InstanceId,
        [Parameter(Mandatory = $true)][string]$KeyName
    )
    $property = Get-PnpDeviceProperty -InstanceId $InstanceId `
        -KeyName $KeyName -ErrorAction SilentlyContinue
    if ($null -eq $property -or
        $null -eq $property.PSObject.Properties['Data']) {
        return ''
    }
    return [string]$property.Data
}

function Get-PropertyValues {
    param(
        [Parameter(Mandatory = $true)][string]$InstanceId,
        [Parameter(Mandatory = $true)][string]$KeyName
    )
    $property = Get-PnpDeviceProperty -InstanceId $InstanceId `
        -KeyName $KeyName -ErrorAction SilentlyContinue
    if ($null -eq $property -or
        $null -eq $property.PSObject.Properties['Data']) {
        return @()
    }
    return @($property.Data)
}

function Get-TopologyDevice {
    param([Parameter(Mandatory = $true)]$Device)
    return [pscustomobject][ordered]@{
        instance_id = [string]$Device.InstanceId
        friendly_name = [string]$Device.FriendlyName
        present = [bool]$Device.Present
        status = [string]$Device.Status
        service = Get-PropertyText -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Service'
        container_id = Get-PropertyText -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_ContainerId'
        parent = Get-PropertyText -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Parent'
        published_inf = Get-PropertyText -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverInfPath'
    }
}

function Invoke-BoundedReadOnlyProbe {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [ValidateRange(100, 10000)][int]$TimeoutMilliseconds = 3000
    )
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.StandardOutputEncoding = [Text.Encoding]::Unicode
    $startInfo.StandardErrorEncoding = [Text.Encoding]::Unicode
    foreach ($argument in $Arguments) {
        $startInfo.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Failed to start the read-only probe: $Executable"
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $completed = $process.WaitForExit($TimeoutMilliseconds)
    if (-not $completed) {
        $process.Kill($true)
        $process.WaitForExit()
    }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    $exitCode = if ($completed) { $process.ExitCode } else { 124 }
    $process.Dispose()
    $lines = @(($stdout + $stderr) -split '\r?\n' |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    return [pscustomobject][ordered]@{
        completed = $completed
        exit_code = $exitCode
        lines = @($lines)
    }
}

$projectRoot = if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
} else {
    [IO.Path]::GetFullPath($ProjectRoot)
}
$allDevices = @(Get-PnpDevice -ErrorAction SilentlyContinue)
$mediaDevices = @(Get-PnpDevice -Class MEDIA `
    -ErrorAction SilentlyContinue)
$a2dpPrefix = 'BTHENUM\{0000110B-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
$avrcpPrefix = 'BTHENUM\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
$transportMatches = @($allDevices | Where-Object {
    $_.InstanceId.StartsWith($a2dpPrefix,
        [StringComparison]::OrdinalIgnoreCase)
})
$avrcpMatches = @($allDevices | Where-Object {
    $_.InstanceId.StartsWith($avrcpPrefix,
        [StringComparison]::OrdinalIgnoreCase)
})
$endpointMatches = @($mediaDevices | Where-Object {
    $hardwareIds = @(Get-PropertyValues -InstanceId $_.InstanceId `
        -KeyName 'DEVPKEY_Device_HardwareIds')
    'ROOT\NativeLdacAudio' -in $hardwareIds
})
if ($transportMatches.Count -ne 1 -or
    $avrcpMatches.Count -ne 1 -or
    $endpointMatches.Count -ne 1) {
    throw 'The topology probe requires exactly one XM5 A2DP PDO, one XM5 AVRCP PDO, and one NativeLdacAudio endpoint.'
}

$transport = Get-TopologyDevice -Device $transportMatches[0]
$avrcp = Get-TopologyDevice -Device $avrcpMatches[0]
$endpoint = Get-TopologyDevice -Device $endpointMatches[0]

$candidateRoot = if ([string]::IsNullOrWhiteSpace($ProbeRoot)) {
    Join-Path $projectRoot 'artifacts\v1-normal-stop\candidate'
} else {
    [IO.Path]::GetFullPath($ProbeRoot)
}
$candidateManifestPath = Join-Path $candidateRoot 'manifest.json'
$volumeProbePath = Join-Path $candidateRoot 'endpoint_volume_probe.exe'
if (-not (Test-Path -LiteralPath $candidateManifestPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $volumeProbePath -PathType Leaf)) {
    throw 'The clean normal-stop candidate endpoint probe is missing.'
}
$sourceCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$sourceStatus = @(& git.exe -C $projectRoot status --porcelain `
    --untracked-files=all)
if ($LASTEXITCODE -ne 0 -or
    $sourceCommit -notmatch '^[0-9a-fA-F]{40}$' -or
    $sourceStatus.Count -ne 0) {
    throw 'The topology probe requires clean Git source.'
}
$candidateManifest = Get-Content -LiteralPath $candidateManifestPath `
    -Raw | ConvertFrom-Json
if ([string]$candidateManifest.source_commit -cne $sourceCommit -or
    $candidateManifest.source_dirty -ne $false) {
    throw 'The normal-stop endpoint probe does not match clean Git HEAD.'
}

$volumeText = @(& $volumeProbePath --info --all 2>&1)
$volumeExit = $LASTEXITCODE
if ($volumeExit -ne 0) {
    throw "The endpoint volume probe failed with exit $volumeExit."
}
$volumeJoined = $volumeText -join "`n"
$nativeMatch = [regex]::Match(
    $volumeJoined,
    '(?ms)^Endpoint:.*Native LDAC.*?^  container:\s*(\{[0-9A-Fa-f-]+\})\s*$')
if (-not $nativeMatch.Success) {
    throw 'The Native LDAC MMDevice Container ID could not be read.'
}
$nativeMmDeviceContainer = $nativeMatch.Groups[1].Value

$privateProbePath = if ([string]::IsNullOrWhiteSpace($ProbeRoot)) {
    Join-Path $projectRoot `
        'artifacts\diagnostics\avrcp_transport_probe.exe'
} else {
    Join-Path $candidateRoot 'avrcp_transport_probe.exe'
}
$privateOpenWin32 = $null
$privateProbeText = @()
$privateProbeCompleted = $false
$privateProbeExit = $null
if (Test-Path -LiteralPath $privateProbePath -PathType Leaf) {
    $privateResult = Invoke-BoundedReadOnlyProbe `
        -Executable $privateProbePath -Arguments @('--open') `
        -TimeoutMilliseconds 3000
    $privateProbeCompleted = [bool]$privateResult.completed
    $privateProbeExit = [int]$privateResult.exit_code
    $privateProbeText = @($privateResult.lines)
    $privateJoined = $privateProbeText -join "`n"
    $win32Match = [regex]::Match($privateJoined, 'Win32\s+(\d+)')
    if ($win32Match.Success) {
        $privateOpenWin32 = [int]$win32Match.Groups[1].Value
    } elseif ($privateResult.exit_code -eq 0) {
        $privateOpenWin32 = 0
    }
}

$absoluteVolumeDisabled = 0
$avrcpRegistryPath = `
    'HKLM:\SYSTEM\CurrentControlSet\Control\Bluetooth\Audio\AVRCP\CT'
$avrcpRegistry = Get-ItemProperty -LiteralPath $avrcpRegistryPath `
    -ErrorAction SilentlyContinue
if ($null -ne $avrcpRegistry -and
    $null -ne $avrcpRegistry.PSObject.Properties['DisableAbsoluteVolume']) {
    $absoluteVolumeDisabled = [int]$avrcpRegistry.DisableAbsoluteVolume
}

$decision = Get-V1VolumeSyncTopologyDecision -Transport $transport `
    -Avrcp $avrcp -Endpoint $endpoint `
    -NativeMmDeviceContainerId $nativeMmDeviceContainer `
    -PrivateAvrcpOpenWin32 $privateOpenWin32
$result = [pscustomobject][ordered]@{
    schema_version = 1
    captured_at = (Get-Date).ToString('o')
    source_commit = $sourceCommit
    transport = $transport
    avrcp = $avrcp
    endpoint = $endpoint
    native_mmdevice_container_id = $nativeMmDeviceContainer
    windows_absolute_volume_enabled = ($absoluteVolumeDisabled -eq 0)
    disable_absolute_volume = $absoluteVolumeDisabled
    private_avrcp_probe = [pscustomobject][ordered]@{
        attempted = (Test-Path -LiteralPath $privateProbePath -PathType Leaf)
        completed = $privateProbeCompleted
        exit_code = $privateProbeExit
        open_win32 = $privateOpenWin32
        text = @($privateProbeText)
    }
    decision = $decision
    safety = [pscustomobject][ordered]@{
        read_only = $true
        driver_install = $false
        pnp_restart = $false
        bluetooth_toggle = $false
        endpoint_write = $false
        avrcp_write_authorization = $false
    }
}

if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $resolvedOutput = [IO.Path]::GetFullPath($OutputPath)
    $parent = Split-Path -Parent $resolvedOutput
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $result | ConvertTo-Json -Depth 8 | Set-Content `
        -LiteralPath $resolvedOutput -Encoding utf8NoBOM
}
if ($AsJson) {
    $result | ConvertTo-Json -Depth 8
} else {
    Write-Host "Topology: $($decision.topology)"
    Write-Host "Reason: $($decision.reason)"
    Write-Host "XM5 transport/AVRCP same container: $($decision.transport_avrcp_same_container)"
    Write-Host "Native MMDevice same container: $($decision.endpoint_mmdevice_same_container)"
    Write-Host "Native PnP child of A2DP PDO: $($decision.endpoint_owned_by_transport)"
    Write-Host "Microsoft private AVRCP open Win32: $privateOpenWin32"
    Write-Host "SWD child candidate required: $($decision.swd_child_candidate_required)"
    Write-Host 'Synchronization proven: False; write authorization remains disabled.'
    Write-Host 'This command was read-only.'
}
