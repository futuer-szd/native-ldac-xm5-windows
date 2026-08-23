# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1SwdEndpointBinding,
    [ValidateRange(20, 60)][int]$DurationSeconds = 30,
    [string]$CandidatePath,
    [string]$GoldenCheckpointPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-swd-endpoint-candidate-common.ps1')
. (Join-Path $PSScriptRoot 'v1-swd-endpoint-binding-common.ps1')

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this gate from an elevated PowerShell 7 terminal.'
    }
}

function Get-DevicePropertyData {
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
    $problem = Get-DevicePropertyData -InstanceId $Device.InstanceId `
        -KeyName 'DEVPKEY_Device_ProblemCode'
    $hardwareIds = @(Get-DevicePropertyData `
        -InstanceId $Device.InstanceId `
        -KeyName 'DEVPKEY_Device_HardwareIds' | Where-Object {
        -not [string]::IsNullOrWhiteSpace([string]$_)
    })
    return [pscustomobject][ordered]@{
        instance_id = [string]$Device.InstanceId
        friendly_name = [string]$Device.FriendlyName
        present = [bool]$Device.Present
        service = [string](Get-DevicePropertyData `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Service')
        published_inf = [string](Get-DevicePropertyData `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverInfPath')
        container_id = [string](Get-DevicePropertyData `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_ContainerId')
        parent = [string](Get-DevicePropertyData `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Parent')
        problem_code = if ($null -eq $problem) { 0 } else { [int]$problem }
        hardware_ids = @($hardwareIds)
    }
}

function Get-ExactDeviceEvidence {
    param([Parameter(Mandatory = $true)][string]$InstanceId)
    $device = Get-PnpDevice -InstanceId $InstanceId `
        -ErrorAction SilentlyContinue
    if ($null -eq $device -or -not [bool]$device.Present) {
        return $null
    }
    return Get-DeviceEvidence -Device $device
}

function Get-RootEndpointEvidence {
    $matches = @(Get-PnpDevice -Class MEDIA -ErrorAction SilentlyContinue |
        Where-Object {
            $ids = @(Get-DevicePropertyData -InstanceId $_.InstanceId `
                -KeyName 'DEVPKEY_Device_HardwareIds')
            'ROOT\NativeLdacAudio' -in $ids
        })
    if ($matches.Count -ne 1 -or -not [bool]$matches[0].Present) {
        throw 'Exactly one present ROOT\NativeLdacAudio endpoint is required.'
    }
    return Get-DeviceEvidence -Device $matches[0]
}

function Get-CandidateChildren {
    return @(Get-CandidateRegisteredDevices | Where-Object {
        $_.present -eq $true
    })
}

function Get-CandidateRegisteredDevices {
    $device = Get-PnpDevice `
        -InstanceId $script:V1SwdEndpointBindingInstanceId `
        -ErrorAction SilentlyContinue
    if ($null -eq $device) {
        return @()
    }
    return @(Get-DeviceEvidence -Device $device)
}

function Get-CandidatePackages {
    return @(Get-WindowsDriver -Online -All | Where-Object {
        (Split-Path -Leaf ([string]$_.OriginalFileName)) -ieq
            $script:V1SwdEndpointBindingOriginalInf
    } | ForEach-Object {
        [pscustomobject][ordered]@{
            published_inf = [string]$_.Driver
            original_file_name =
                (Split-Path -Leaf ([string]$_.OriginalFileName))
            provider_name = [string]$_.ProviderName
            class_name = [string]$_.ClassName
            version = [string]$_.Version
        }
    })
}

function Get-EndpointObservationSnapshot {
    param([Parameter(Mandatory = $true)][string]$ProbePath)
    $lines = @(& $ProbePath --info --all 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "The endpoint volume probe failed with exit $exitCode."
    }
    return [pscustomobject]@{
        endpoints = @(ConvertFrom-V1SwdEndpointVolumeText -Lines $lines)
        lines = @($lines)
    }
}

function Get-BindingSnapshot {
    param(
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)][string]$VolumeProbePath
    )
    $transport = Get-ExactDeviceEvidence `
        -InstanceId ([string]$Manifest.expected_parent)
    if ($null -eq $transport) {
        throw 'The verified XM5 A2DP parent is not present.'
    }
    $endpointSnapshot = Get-EndpointObservationSnapshot `
        -ProbePath $VolumeProbePath
    $endpoints = @($endpointSnapshot.endpoints)
    $rootMm = @($endpoints | Where-Object {
        $hasRootMarker = Test-V1SwdEndpointNameContains `
            -Name ([string]$_.name) `
            -Marker $script:V1SwdEndpointRootNameMarker
        $hasCandidateMarker = Test-V1SwdEndpointNameContains `
            -Name ([string]$_.name) `
            -Marker $script:V1SwdEndpointBindingNameMarker
        $isPublished = Test-V1SwdEndpointPublishedState `
            -State ([string]$_.state)
        $hasRootMarker -and -not $hasCandidateMarker -and $isPublished -and
            [string]$_.container_id -ieq
                [string]$Manifest.remote_container_id
    })
    return [pscustomobject][ordered]@{
        captured_at = (Get-Date).ToString('o')
        transport = $transport
        root_endpoint = Get-RootEndpointEvidence
        root_mmdevice = if ($rootMm.Count -eq 1) { $rootMm[0] } else { $null }
        root_mmdevices = @($rootMm)
        candidate_children = @(Get-CandidateChildren)
        candidate_registered_devices = @(Get-CandidateRegisteredDevices)
        candidate_packages = @(Get-CandidatePackages)
        candidate_mmdevices = @($endpoints | Where-Object {
            Test-V1SwdEndpointNameContains `
                -Name ([string]$_.name) `
                -Marker $script:V1SwdEndpointBindingNameMarker
        })
        all_endpoint_text = @($endpointSnapshot.lines)
    }
}

function Invoke-PnpUtil {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $text = @(& pnputil.exe @Arguments 2>&1)
    return [pscustomobject][ordered]@{
        exit_code = $LASTEXITCODE
        text = @($text)
    }
}

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The SWD endpoint binding gate requires PowerShell 7.'
}
Assert-Administrator
if (-not $ConfirmV1SwdEndpointBinding) {
    throw 'Refusing to stage and bind the isolated endpoint without -ConfirmV1SwdEndpointBinding.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $projectRoot `
        'artifacts\v1-volume-sync\endpoint-candidate'
}
$candidate = Get-V1SwdEndpointCandidate -CandidatePath $CandidatePath
$manifest = $candidate.manifest
if ([string]$manifest.expected_instance_id -ine
        $script:V1SwdEndpointBindingInstanceId -or
    [string]$manifest.hardware_id -ine
        $script:V1SwdEndpointBindingHardwareId -or
    [string]$manifest.service_name -cne
        $script:V1SwdEndpointBindingService -or
    [string]::IsNullOrWhiteSpace([string]$manifest.expected_parent) -or
    [string]::IsNullOrWhiteSpace([string]$manifest.remote_container_id)) {
    throw 'The endpoint candidate binding identity is invalid.'
}
$head = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$status = @(& git.exe -C $projectRoot status --porcelain `
    --untracked-files=all)
if ($LASTEXITCODE -ne 0 -or $status.Count -ne 0 -or
    $head -cne [string]$candidate.manifest.source_commit) {
    throw 'The endpoint binding gate requires the exact clean candidate source.'
}
& (Join-Path $candidate.root 'verify-v1-swd-endpoint-candidate.ps1') `
    -CandidatePath $candidate.root

if ([string]::IsNullOrWhiteSpace($GoldenCheckpointPath)) {
    $GoldenCheckpointPath = [string]$candidate.manifest.golden_checkpoint
}
& (Join-Path $candidate.root 'verify-v1-golden-checkpoint.ps1') `
    -CheckpointPath $GoldenCheckpointPath
$candidateThumbprint = [string]$manifest.certificate_thumbprint
foreach ($store in @('Root', 'TrustedPublisher')) {
    if (-not (Test-Path -LiteralPath `
            "Cert:\LocalMachine\$store\$candidateThumbprint")) {
        throw "The existing candidate signer is not trusted in LocalMachine\$store."
    }
}

$volumeProbe = Join-Path $candidate.root 'endpoint_volume_probe.exe'
$before = Get-BindingSnapshot -Manifest $manifest `
    -VolumeProbePath $volumeProbe
if (@($before.candidate_children).Count -ne 0 -or
    @($before.candidate_registered_devices).Count -ne 0 -or
    @($before.candidate_packages).Count -ne 0 -or
    @($before.candidate_mmdevices | Where-Object {
        Test-V1SwdEndpointPublishedState -State ([string]$_.state)
    }).Count -ne 0) {
    throw 'The isolated SWD endpoint must be absent and its package unstaged before this gate.'
}
if ([string]$before.transport.service -cne 'LdacNative' -or
    [int]$before.transport.problem_code -ne 0 -or
    [string]$before.transport.container_id -ine
        [string]$manifest.remote_container_id -or
    [string]$before.root_endpoint.service -cne 'NativeLdacAudio' -or
    [int]$before.root_endpoint.problem_code -ne 0 -or
    @($before.root_mmdevices).Count -ne 1 -or
    -not (Test-V1SwdEndpointPublishedState `
        -State ([string]$before.root_mmdevice.state))) {
    throw 'The frozen transport plus ROOT Native endpoint baseline is not healthy.'
}

$trialRoot = Join-Path $projectRoot 'artifacts\v1-volume-sync\trial'
$directory = Join-Path $trialRoot `
    ('endpoint-binding-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$before | ConvertTo-Json -Depth 10 | Set-Content `
    -LiteralPath (Join-Path $directory 'before.json') -Encoding utf8NoBOM

Write-Host 'V1 isolated transport-owned endpoint binding preflight passed.'
Write-Host 'Stop playback and keep XM5 off. This gate does not change the current ROOT endpoint or default output.'
Write-Host "It stages one isolated package, creates one hidden child for at most $DurationSeconds seconds, then removes both."
Write-Host 'No Bluetooth toggle, PnP restart, endpoint write, certificate import, or audio playback is allowed.'
if (-not $PSCmdlet.ShouldProcess(
        'one isolated NativeLdacSwdAudio package and bounded XM5 child',
        'Stage, bind, observe one non-default endpoint, close, remove, and verify rollback')) {
    return
}

$addResult = $null
$deviceRemoveResult = $null
$removeResult = $null
$publishedInf = ''
$hostProcess = $null
$hostStarted = $false
$hostCompleted = $false
$hostTimedOut = $false
$hostForced = $false
$hostExit = -1
$hostOutput = ''
$hostError = ''
$stdoutTask = $null
$stderrTask = $null
$during = $null
$lastRuntimeSnapshot = $null
$after = $null
$operationError = ''
$cleanupErrors = [Collections.Generic.List[string]]::new()
$process = $null

try {
    $packageInf = Join-Path $candidate.root `
        'package\NativeLdacSwdAudio.inf'
    $addResult = Invoke-PnpUtil -Arguments @('/add-driver', $packageInf)
    $addResult.text | Set-Content `
        -LiteralPath (Join-Path $directory 'pnputil-add.log') `
        -Encoding utf8NoBOM
    if ([int]$addResult.exit_code -ne 0) {
        throw "The isolated package staging failed with exit $($addResult.exit_code)."
    }
    $staged = @(Get-CandidatePackages)
    if ($staged.Count -ne 1) {
        throw 'Package staging did not produce exactly one isolated driver package.'
    }
    $publishedInf = [string]$staged[0].published_inf
    if ($publishedInf -notmatch '^oem\d+\.inf$') {
        throw 'The isolated package did not receive a bounded OEM INF identity.'
    }

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = Join-Path $candidate.root `
        'v1_swd_endpoint_host.exe'
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.StandardOutputEncoding = [Text.Encoding]::UTF8
    $startInfo.StandardErrorEncoding = [Text.Encoding]::UTF8
    foreach ($argument in @(
            '--create',
            '--parent', [string]$manifest.expected_parent,
            '--container', [string]$manifest.remote_container_id,
            '--duration-seconds', [string]$DurationSeconds,
            '--confirm-endpoint-binding-probe')) {
        $startInfo.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $hostStarted = $process.Start()
    if (-not $hostStarted) {
        throw 'The bounded endpoint host did not start.'
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()

    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
        $snapshot = Get-BindingSnapshot -Manifest $manifest `
            -VolumeProbePath $volumeProbe
        $lastRuntimeSnapshot = $snapshot
        $publishedCandidate = @($snapshot.candidate_mmdevices |
            Where-Object {
                Test-V1SwdEndpointPublishedState -State ([string]$_.state)
            })
        if (@($snapshot.candidate_children).Count -eq 1 -and
            $publishedCandidate.Count -eq 1) {
            $during = $snapshot
            break
        }
        Start-Sleep -Milliseconds 250
    }
    if ($null -eq $during) {
        throw 'The isolated child did not publish one candidate endpoint within 15 seconds.'
    }

    $hostCompleted = $process.WaitForExit(($DurationSeconds + 10) * 1000)
    if (-not $hostCompleted) {
        $hostTimedOut = $true
        $hostForced = $true
        $process.Kill($true)
        $process.WaitForExit()
    }
    $hostExit = $process.ExitCode
} catch {
    $operationError = $_.Exception.Message
} finally {
    try {
        if ($null -ne $process) {
            if (-not $process.HasExited) {
                $hostForced = $true
                $process.Kill($true)
                $process.WaitForExit()
            }
            if ($null -ne $stdoutTask) {
                $hostOutput = $stdoutTask.GetAwaiter().GetResult()
            }
            if ($null -ne $stderrTask) {
                $hostError = $stderrTask.GetAwaiter().GetResult()
            }
            if ($hostExit -lt 0 -and $process.HasExited) {
                $hostExit = $process.ExitCode
            }
        }
    } catch {
        $cleanupErrors.Add('host cleanup: ' + $_.Exception.Message)
    } finally {
        if ($null -ne $process) {
            $process.Dispose()
        }
    }
    try {
        $hostOutput | Set-Content `
            -LiteralPath (Join-Path $directory 'endpoint-host.out.log') `
            -Encoding utf8NoBOM
        $hostError | Set-Content `
            -LiteralPath (Join-Path $directory 'endpoint-host.err.log') `
            -Encoding utf8NoBOM
    } catch {
        $cleanupErrors.Add('host log capture: ' + $_.Exception.Message)
    }

    try {
        $childDeadline = [DateTime]::UtcNow.AddSeconds(8)
        while ([DateTime]::UtcNow -lt $childDeadline -and
            @(Get-CandidateChildren).Count -ne 0) {
            Start-Sleep -Milliseconds 200
        }
    } catch {
        $cleanupErrors.Add('child rundown query: ' + $_.Exception.Message)
    }
    try {
        $registeredCandidates = @(Get-CandidateRegisteredDevices)
        if ($registeredCandidates.Count -gt 1) {
            throw 'More than one registered isolated SWD endpoint device exists.'
        }
        if ($registeredCandidates.Count -eq 1) {
            $registeredCandidate = $registeredCandidates[0]
            $registeredHardwareIds = @(
                $registeredCandidate.hardware_ids | Where-Object {
                    -not [string]::IsNullOrWhiteSpace([string]$_)
                })
            if ([string]$registeredCandidate.instance_id -ine
                    $script:V1SwdEndpointBindingInstanceId -or
                [string]$registeredCandidate.service -cne
                    $script:V1SwdEndpointBindingService -or
                [string]$registeredCandidate.published_inf -ine
                    $publishedInf -or
                [string]$registeredCandidate.parent -ine
                    [string]$manifest.expected_parent -or
                [string]$registeredCandidate.container_id -ine
                    [string]$manifest.remote_container_id -or
                $registeredHardwareIds.Count -ne 1 -or
                [string]$registeredHardwareIds[0] -ine
                    $script:V1SwdEndpointBindingHardwareId) {
                throw 'The registered SWD endpoint device does not match the exact isolated identity.'
            }
            $deviceRemoveResult = Invoke-PnpUtil -Arguments @(
                '/remove-device',
                $script:V1SwdEndpointBindingInstanceId)
            $deviceRemoveResult.text | Set-Content `
                -LiteralPath (Join-Path $directory `
                    'pnputil-remove-device.log') `
                -Encoding utf8NoBOM
            if ([int]$deviceRemoveResult.exit_code -ne 0) {
                throw "The isolated device-instance removal failed with exit $($deviceRemoveResult.exit_code)."
            }
        }
        $deviceDeadline = [DateTime]::UtcNow.AddSeconds(8)
        while ([DateTime]::UtcNow -lt $deviceDeadline -and
            @(Get-CandidateRegisteredDevices).Count -ne 0) {
            Start-Sleep -Milliseconds 200
        }
    } catch {
        $cleanupErrors.Add('device-instance rollback: ' + $_.Exception.Message)
    }
    try {
        if ([string]::IsNullOrWhiteSpace($publishedInf)) {
            $rollbackPackages = @(Get-CandidatePackages)
            if ($rollbackPackages.Count -eq 1) {
                $publishedInf = [string]$rollbackPackages[0].published_inf
            }
        }
        if (-not [string]::IsNullOrWhiteSpace($publishedInf)) {
            $removeResult = Invoke-PnpUtil `
                -Arguments @('/delete-driver', $publishedInf, '/force')
            $removeResult.text | Set-Content `
                -LiteralPath (Join-Path $directory 'pnputil-remove.log') `
                -Encoding utf8NoBOM
        }
        $packageDeadline = [DateTime]::UtcNow.AddSeconds(8)
        while ([DateTime]::UtcNow -lt $packageDeadline -and
            @(Get-CandidatePackages).Count -ne 0) {
            Start-Sleep -Milliseconds 200
        }
    } catch {
        $cleanupErrors.Add('package rollback: ' + $_.Exception.Message)
    }
    try {
        $endpointDeadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            $endpointState = Get-EndpointObservationSnapshot `
                -ProbePath $volumeProbe
            $publishedCandidate = @($endpointState.endpoints | Where-Object {
                $hasCandidateMarker = Test-V1SwdEndpointNameContains `
                    -Name ([string]$_.name) `
                    -Marker $script:V1SwdEndpointBindingNameMarker
                $isPublished = Test-V1SwdEndpointPublishedState `
                    -State ([string]$_.state)
                $hasCandidateMarker -and $isPublished
            })
            if ($publishedCandidate.Count -eq 0) {
                break
            }
            Start-Sleep -Milliseconds 250
        } while ([DateTime]::UtcNow -lt $endpointDeadline)
    } catch {
        $cleanupErrors.Add('endpoint rundown query: ' + $_.Exception.Message)
    }
    try {
        $after = Get-BindingSnapshot -Manifest $manifest `
            -VolumeProbePath $volumeProbe
    } catch {
        $cleanupErrors.Add('final snapshot: ' + $_.Exception.Message)
    }
}

if ($null -eq $during) {
    $during = if ($null -eq $lastRuntimeSnapshot) {
        $before
    } else {
        $lastRuntimeSnapshot
    }
}
$afterCaptured = $null -ne $after
if (-not $afterCaptured) {
    $after = [pscustomobject]@{
        capture_failed = $true
        errors = @($cleanupErrors)
        candidate_children = @()
        candidate_registered_devices = @()
        candidate_packages = @()
        candidate_mmdevices = @()
    }
}
$during | ConvertTo-Json -Depth 10 | Set-Content `
    -LiteralPath (Join-Path $directory 'during.json') -Encoding utf8NoBOM
$after | ConvertTo-Json -Depth 10 | Set-Content `
    -LiteralPath (Join-Path $directory 'after.json') -Encoding utf8NoBOM

$hostProcess = [pscustomobject][ordered]@{
    started = $hostStarted
    completed = $hostCompleted
    timed_out = $hostTimedOut
    forced_termination = $hostForced
    exit_code = $hostExit
}
$rollback = [pscustomobject][ordered]@{
    device_remove_attempted = $null -ne $deviceRemoveResult
    device_remove_exit_code = if ($null -eq $deviceRemoveResult) {
        $null
    } else {
        [int]$deviceRemoveResult.exit_code
    }
    device_instance_absent = $afterCaptured -and
        @($after.candidate_registered_devices).Count -eq 0
    package_remove_attempted = $null -ne $removeResult
    package_remove_exit_code = if ($null -eq $removeResult) {
        $null
    } else {
        [int]$removeResult.exit_code
    }
    package_remove_succeeded =
        $null -ne $removeResult -and
        [int]$removeResult.exit_code -eq 0 -and
        $afterCaptured -and @($after.candidate_packages).Count -eq 0
    child_absent = $afterCaptured -and
        @($after.candidate_children).Count -eq 0
    published_candidate_endpoint_absent = $afterCaptured -and @(
            $after.candidate_mmdevices | Where-Object {
                Test-V1SwdEndpointPublishedState -State ([string]$_.state)
            }).Count -eq 0
    cleanup_errors = @($cleanupErrors)
}
$evidencePassed = $false
if ($afterCaptured -and $cleanupErrors.Count -eq 0) {
    $evidencePassed = Test-V1SwdEndpointBindingEvidence -Before $before `
        -During $during -After $after -HostProcess $hostProcess `
        -Rollback $rollback `
        -ExpectedParent ([string]$manifest.expected_parent) `
        -ExpectedContainer ([string]$manifest.remote_container_id)
}
$passed = [string]::IsNullOrWhiteSpace($operationError) -and $evidencePassed
$result = [ordered]@{
    schema_version = 1
    policy_version = $script:V1SwdEndpointBindingPolicyVersion
    passed = $passed
    source_commit = [string]$candidate.manifest.source_commit
    candidate = $candidate.root
    golden_checkpoint = [IO.Path]::GetFullPath($GoldenCheckpointPath)
    duration_seconds = $DurationSeconds
    expected_parent = [string]$manifest.expected_parent
    expected_container = [string]$manifest.remote_container_id
    staged_published_inf = $publishedInf
    before = 'before.json'
    during = 'during.json'
    after = 'after.json'
    host_process = $hostProcess
    rollback = $rollback
    error = $operationError
    safety = [ordered]@{
        current_root_endpoint_preserved = $true
        default_endpoint_written = $false
        certificate_imported = $false
        pnp_restarted = $false
        bluetooth_toggled = $false
        audio_playback_started = $false
        isolated_device_instance_removed =
            [bool]$rollback.device_instance_absent
        isolated_package_removed = [bool]$rollback.package_remove_succeeded
    }
}
$resultPath = Join-Path $directory 'result.json'
$result | ConvertTo-Json -Depth 10 | Set-Content `
    -LiteralPath $resultPath -Encoding utf8NoBOM
if (-not $passed) {
    $reason = if ([string]::IsNullOrWhiteSpace($operationError)) {
        'The endpoint binding evidence contract failed.'
    } else {
        $operationError
    }
    throw "V1 isolated endpoint binding gate failed: $reason Result: $resultPath"
}

Write-Host 'V1 isolated transport-owned endpoint binding gate passed.'
Write-Host 'One non-default NativeLdacSwdAudio endpoint was published under the exact XM5 A2DP parent/container.'
Write-Host 'The child disappeared, the isolated package was removed, and the existing ROOT endpoint remained unchanged.'
Write-Host "Result: $resultPath"
