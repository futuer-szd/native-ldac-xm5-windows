# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'legacy-install-common.ps1')

$script:NativeLdacRootHardwareId = 'ROOT\NativeLdacAudio'
$script:NativeLdacMediaOriginalInf = 'NativeLdacAudio.inf'
$script:NativeLdacTransportOriginalInfs = @(
    'LdacNative.inf',
    'NativeLdacDirectPdo.inf'
)

function Get-NativeLdacRootDevices {
    $matches = @()
    $devices = @(Get-PnpDevice -Class MEDIA -ErrorAction SilentlyContinue)
    foreach ($device in $devices) {
        $hardwareIdProperty = Get-PnpDeviceProperty `
            -InstanceId $device.InstanceId `
            -KeyName 'DEVPKEY_Device_HardwareIds' `
            -ErrorAction SilentlyContinue
        if ($null -eq $hardwareIdProperty -or
            $script:NativeLdacRootHardwareId -notin
                @($hardwareIdProperty.Data)) {
            continue
        }
        $service = Get-PnpDeviceProperty -InstanceId $device.InstanceId `
            -KeyName 'DEVPKEY_Device_Service' `
            -ErrorAction SilentlyContinue
        $publishedInf = Get-PnpDeviceProperty `
            -InstanceId $device.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverInfPath' `
            -ErrorAction SilentlyContinue
        $problem = Get-PnpDeviceProperty -InstanceId $device.InstanceId `
            -KeyName 'DEVPKEY_Device_ProblemCode' `
            -ErrorAction SilentlyContinue
        $matches += [pscustomobject][ordered]@{
            instance_id = [string]$device.InstanceId
            friendly_name = [string]$device.FriendlyName
            present = [bool]$device.Present
            status = [string]$device.Status
            service = if ($null -eq $service) {
                ''
            } else {
                [string]$service.Data
            }
            published_inf = if ($null -eq $publishedInf) {
                ''
            } else {
                [string]$publishedInf.Data
            }
            problem_code = if ($null -eq $problem) {
                0
            } else {
                [int]$problem.Data
            }
        }
    }
    return @($matches)
}

function Get-NativeLdacWorkspaceProcesses {
    $names = @(
        'ldac_agent.exe',
        'transport_probe.exe',
        'ldac_direct_engine.exe',
        'audio_endpoint_probe.exe',
        'v1_presence_agent.exe',
        'v1_engine_ready_stub.exe',
        'v1_transport_worker_stub.exe',
        'v1_transport_discovery_worker.exe',
        'v1_pcm_encode_engine.exe'
    )
    $processes = @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -in $names
    })
    $results = foreach ($process in $processes) {
        [pscustomobject][ordered]@{
            name = [string]$process.Name
            process_id = [int]$process.ProcessId
            executable_path = [string]$process.ExecutablePath
            command_line = [string]$process.CommandLine
        }
    }
    return @($results)
}

function Get-NativeLdacAltA2dpUserService {
    $services = @(Get-CimInstance Win32_Service -Filter `
        "Name='AltA2dpSVC'" -ErrorAction SilentlyContinue)
    if ($services.Count -eq 0) {
        return $null
    }
    if ($services.Count -ne 1) {
        throw 'Multiple Alternative A2DP services were returned.'
    }
    $service = $services[0]
    return [pscustomobject][ordered]@{
        name = [string]$service.Name
        display_name = [string]$service.DisplayName
        state = [string]$service.State
        start_mode = [string]$service.StartMode
        process_id = [int]$service.ProcessId
        path = [string]$service.PathName
    }
}

function Get-NativeLdacCurrentBootTime {
    $operatingSystems = @(Get-CimInstance Win32_OperatingSystem)
    if ($operatingSystems.Count -ne 1) {
        throw 'Could not determine the current Windows boot time.'
    }
    return ([datetime]$operatingSystems[0].LastBootUpTime).ToUniversalTime()
}

function Get-NativeLdacBaselineSnapshot {
    param([Parameter(Mandatory = $true)][string]$BackupPath)

    $backupRoot = [System.IO.Path]::GetFullPath($BackupPath)
    $backupStatePath = Join-Path $backupRoot 'state.json'
    if (-not (Test-Path -LiteralPath $backupStatePath -PathType Leaf)) {
        throw "Original A2DP backup state is missing: $backupStatePath"
    }
    $backupState = Get-Content -LiteralPath $backupStatePath -Raw |
        ConvertFrom-Json
    if ([string]$backupState.service -in
        @('LdacNative', 'NativeLdacDirectPdo')) {
        throw 'The selected backup is not the original A2DP driver.'
    }

    $a2dpDevices = @(Get-LegacyXm5A2dpDevices)
    $a2dpSnapshots = @(foreach ($device in $a2dpDevices) {
        Get-LegacyXm5A2dpSnapshot -Device $device
    })
    $transportPackages = @(Get-LegacyDriverPackages `
        -OriginalInfNames $script:NativeLdacTransportOriginalInfs)
    $originalPackages = @(Get-LegacyDriverPackages `
        -OriginalInfNames @('alta2dp.inf'))
    $audioPackages = @(Get-LegacyDriverPackages `
        -OriginalInfNames @($script:NativeLdacMediaOriginalInf))
    $audioPackageSnapshots = @(foreach ($package in $audioPackages) {
        [pscustomobject][ordered]@{
            published_inf = [string]$package.Driver
            original_inf = Split-Path -Leaf `
                ([string]$package.OriginalFileName)
            provider = [string]$package.ProviderName
            version = [string]$package.Version
        }
    })
    $transportPackageSnapshots = @(foreach ($package in $transportPackages) {
        [pscustomobject][ordered]@{
            published_inf = [string]$package.Driver
            original_inf = Split-Path -Leaf `
                ([string]$package.OriginalFileName)
            provider = [string]$package.ProviderName
            version = [string]$package.Version
        }
    })
    $rootDevices = @(Get-NativeLdacRootDevices)
    $presentRootDevices = @($rootDevices | Where-Object { $_.present })
    $processes = @(Get-NativeLdacWorkspaceProcesses)
    $tasks = @(Get-ScheduledTask -TaskName 'Native LDAC Agent' `
        -ErrorAction SilentlyContinue)
    $taskSnapshots = @(foreach ($task in $tasks) {
        [pscustomobject][ordered]@{
            task_name = [string]$task.TaskName
            state = [string]$task.State
        }
    })
    $originalPackagePresent = @($originalPackages | Where-Object {
        ([string]$_.Driver).Equals(
            [string]$backupState.published_inf,
            [StringComparison]::OrdinalIgnoreCase)
    }).Count -eq 1
    $originalBindingHealthy = $false
    if ($a2dpSnapshots.Count -eq 1) {
        $candidate = $a2dpSnapshots[0]
        $originalBindingHealthy =
            $candidate.service -eq [string]$backupState.service -and
            $candidate.published_inf.Equals(
                [string]$backupState.published_inf,
                [StringComparison]::OrdinalIgnoreCase) -and
            $candidate.problem_code -eq 0
    }
    $originalBindingSafe = $originalBindingHealthy -or
        ($a2dpSnapshots.Count -eq 0 -and $originalPackagePresent)
    $safeOriginalA2dp = $originalBindingSafe -and
        $transportPackageSnapshots.Count -eq 0 -and
        $processes.Count -eq 0 -and
        $taskSnapshots.Count -eq 0
    $cleanOriginalA2dp = $safeOriginalA2dp -and
        $presentRootDevices.Count -eq 0 -and
        $audioPackageSnapshots.Count -eq 0

    $control = Get-ItemProperty `
        -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control'
    return [pscustomobject][ordered]@{
        schema_version = 1
        captured_at = (Get-Date).ToString('o')
        boot_time_utc = (Get-NativeLdacCurrentBootTime).ToString('o')
        test_signing_active = [string]$control.SystemStartOptions -match
            '(^|\s)TESTSIGNING(\s|$)'
        backup_path = $backupRoot
        expected_original_service = [string]$backupState.service
        expected_original_inf = [string]$backupState.published_inf
        original_package_present = $originalPackagePresent
        original_binding_healthy = $originalBindingHealthy
        original_binding_safe = $originalBindingSafe
        safe_original_a2dp = $safeOriginalA2dp
        clean_original_a2dp = $cleanOriginalA2dp
        a2dp_devices = @($a2dpSnapshots)
        original_a2dp_user_service = Get-NativeLdacAltA2dpUserService
        transport_test_packages = @($transportPackageSnapshots)
        native_audio_devices = @($rootDevices)
        native_audio_packages = @($audioPackageSnapshots)
        workspace_processes = @($processes)
        scheduled_tasks = @($taskSnapshots)
    }
}

function Write-NativeLdacBaselineSummary {
    param([Parameter(Mandatory = $true)]$Snapshot)

    Write-Host "Original A2DP healthy: $($Snapshot.original_binding_healthy)"
    Write-Host "Safe original-A2DP baseline: $($Snapshot.safe_original_a2dp)"
    Write-Host "Clean original-A2DP baseline: $($Snapshot.clean_original_a2dp)"
    Write-Host "Transport test packages: $(@($Snapshot.transport_test_packages).Count)"
    Write-Host "Native audio devices: $(@($Snapshot.native_audio_devices).Count)"
    Write-Host "Native audio packages: $(@($Snapshot.native_audio_packages).Count)"
    Write-Host "Workspace media processes: $(@($Snapshot.workspace_processes).Count)"
    Write-Host "Native LDAC scheduled tasks: $(@($Snapshot.scheduled_tasks).Count)"
}

function Assert-NativeLdacXm5ConnectionProbe {
    param(
        [Parameter(Mandatory = $true)][string]$ProbePath,
        [string]$ExpectedSourceCommit
    )

    if (-not (Test-Path -LiteralPath $ProbePath -PathType Leaf)) {
        throw "The read-only XM5 connection probe is missing: $ProbePath"
    }
    $manifestPath = Join-Path (Split-Path -Parent $ProbePath) `
        'xm5_connection_probe.manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "The read-only XM5 connection probe manifest is missing: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw |
        ConvertFrom-Json
    $hash = (Get-FileHash -LiteralPath $ProbePath `
        -Algorithm SHA256).Hash
    $capabilities = @($manifest.capabilities | ForEach-Object {
        [string]$_
    })
    if ([int]$manifest.manifest_version -ne 3 -or
        $manifest.source_dirty -ne $false -or
        [string]$manifest.file_name -ne 'xm5_connection_probe.exe' -or
        'BluetoothFindFirstDevice_fConnected_no_inquiry' -notin
            $capabilities -or
        'GUID_BLUETOOTH_HCI_EVENT_acl_transition' -notin
            $capabilities -or
        'BluetoothIsConnectable_radio_state' -notin
            $capabilities) {
        throw 'The read-only XM5 connection probe manifest is invalid.'
    }
    if (-not $hash.Equals(
            [string]$manifest.sha256,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The read-only XM5 connection probe hash does not match its manifest.'
    }
    if (-not [string]::IsNullOrWhiteSpace($ExpectedSourceCommit) -and
        [string]$manifest.source_commit -ne $ExpectedSourceCommit) {
        throw "The read-only XM5 connection probe came from source $($manifest.source_commit), but this gate requires $ExpectedSourceCommit."
    }
}

function Get-NativeLdacXm5BluetoothState {
    param(
        [Parameter(Mandatory = $true)][string]$ProbePath,
        [string]$ExpectedSourceCommit
    )

    Assert-NativeLdacXm5ConnectionProbe `
        -ProbePath $ProbePath `
        -ExpectedSourceCommit $ExpectedSourceCommit
    $result = Invoke-LegacyNativeCapture -FilePath $ProbePath `
        -Arguments @('--state')
    $text = $result.stdout + $result.stderr
    if ($result.exit_code -eq 0 -and
        $text -match '(?m)^XM5 Bluetooth state: connected\.\r?$') {
        return 'connected'
    }
    if ($result.exit_code -eq 10 -and
        $text -match '(?m)^XM5 Bluetooth state: disconnected\.\r?$') {
        return 'disconnected'
    }
    throw "Could not establish the XM5 Bluetooth state: $($text.Trim())"
}

function Get-NativeLdacBluetoothRadioState {
    param(
        [Parameter(Mandatory = $true)][string]$ProbePath,
        [string]$ExpectedSourceCommit
    )

    Assert-NativeLdacXm5ConnectionProbe `
        -ProbePath $ProbePath `
        -ExpectedSourceCommit $ExpectedSourceCommit
    $result = Invoke-LegacyNativeCapture -FilePath $ProbePath `
        -Arguments @('--radio-state')
    $text = $result.stdout + $result.stderr
    if ($result.exit_code -eq 0 -and
        $text -match '(?m)^Bluetooth radio state: ready\.\r?$') {
        return 'ready'
    }
    if ($result.exit_code -eq 10 -and
        $text -match
            '(?m)^Bluetooth radio state: (unavailable|not-connectable)\.\r?$') {
        return 'off-or-unavailable'
    }
    throw "Could not establish Bluetooth radio readiness: $($text.Trim())"
}

function Test-NativeLdacPhysicalPowerOnEvidence {
    param([Parameter(Mandatory = $true)]$Transaction)

    $postRebootProperty = $Transaction.PSObject.Properties['post_reboot']
    if ($null -eq $postRebootProperty -or
        $null -eq $postRebootProperty.Value) {
        return $false
    }
    $postReboot = $postRebootProperty.Value
    $aclProperty =
        $postReboot.PSObject.Properties['acl_event_observed_at']
    $powerProperty =
        $postReboot.PSObject.Properties[
            'physical_power_on_confirmed_at']
    return $null -ne $aclProperty -and
        $null -ne $powerProperty -and
        -not [string]::IsNullOrWhiteSpace(
            [string]$aclProperty.Value) -and
        -not [string]::IsNullOrWhiteSpace(
            [string]$powerProperty.Value)
}
