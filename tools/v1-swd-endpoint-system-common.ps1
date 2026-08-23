# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'v1-swd-endpoint-binding-common.ps1')

function Assert-V1SwdAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this gate from an elevated PowerShell 7 terminal.'
    }
}

function Get-V1SwdDevicePropertyData {
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

function Get-V1SwdDeviceEvidence {
    param([Parameter(Mandatory = $true)]$Device)
    $problem = Get-V1SwdDevicePropertyData `
        -InstanceId $Device.InstanceId `
        -KeyName 'DEVPKEY_Device_ProblemCode'
    $hardwareIds = @(Get-V1SwdDevicePropertyData `
        -InstanceId $Device.InstanceId `
        -KeyName 'DEVPKEY_Device_HardwareIds' | Where-Object {
            -not [string]::IsNullOrWhiteSpace([string]$_)
        })
    return [pscustomobject][ordered]@{
        instance_id = [string]$Device.InstanceId
        friendly_name = [string]$Device.FriendlyName
        present = [bool]$Device.Present
        service = [string](Get-V1SwdDevicePropertyData `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Service')
        published_inf = [string](Get-V1SwdDevicePropertyData `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverInfPath')
        container_id = [string](Get-V1SwdDevicePropertyData `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_ContainerId')
        parent = [string](Get-V1SwdDevicePropertyData `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Parent')
        problem_code = if ($null -eq $problem) { 0 } else { [int]$problem }
        hardware_ids = @($hardwareIds)
    }
}

function Get-V1SwdExactDeviceEvidence {
    param([Parameter(Mandatory = $true)][string]$InstanceId)
    $device = Get-PnpDevice -InstanceId $InstanceId `
        -ErrorAction SilentlyContinue
    if ($null -eq $device -or -not [bool]$device.Present) {
        return $null
    }
    return Get-V1SwdDeviceEvidence -Device $device
}

function Get-V1SwdRootEndpointEvidence {
    $matches = @(Get-PnpDevice -Class MEDIA -ErrorAction SilentlyContinue |
        Where-Object {
            $ids = @(Get-V1SwdDevicePropertyData `
                -InstanceId $_.InstanceId `
                -KeyName 'DEVPKEY_Device_HardwareIds')
            'ROOT\NativeLdacAudio' -in $ids
        })
    if ($matches.Count -ne 1 -or -not [bool]$matches[0].Present) {
        throw 'Exactly one present ROOT\NativeLdacAudio endpoint is required.'
    }
    return Get-V1SwdDeviceEvidence -Device $matches[0]
}

function Get-V1SwdCandidateRegisteredDevices {
    $device = Get-PnpDevice `
        -InstanceId $script:V1SwdEndpointBindingInstanceId `
        -ErrorAction SilentlyContinue
    if ($null -eq $device) {
        return @()
    }
    return @(Get-V1SwdDeviceEvidence -Device $device)
}

function Get-V1SwdCandidateChildren {
    return @(Get-V1SwdCandidateRegisteredDevices | Where-Object {
        $_.present -eq $true
    })
}

function Get-V1SwdCandidatePackages {
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

function Get-V1SwdEndpointObservationSnapshot {
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

function Get-V1SwdBindingSnapshot {
    param(
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)][string]$VolumeProbePath
    )
    $transport = Get-V1SwdExactDeviceEvidence `
        -InstanceId ([string]$Manifest.expected_parent)
    if ($null -eq $transport) {
        throw 'The verified XM5 A2DP parent is not present.'
    }
    $endpointSnapshot = Get-V1SwdEndpointObservationSnapshot `
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
        root_endpoint = Get-V1SwdRootEndpointEvidence
        root_mmdevice = if ($rootMm.Count -eq 1) { $rootMm[0] } else { $null }
        root_mmdevices = @($rootMm)
        default_endpoints = @($endpoints | Where-Object {
            [string]$_.default_roles -cne '(none)'
        })
        candidate_children = @(Get-V1SwdCandidateChildren)
        candidate_registered_devices =
            @(Get-V1SwdCandidateRegisteredDevices)
        candidate_packages = @(Get-V1SwdCandidatePackages)
        candidate_mmdevices = @($endpoints | Where-Object {
            Test-V1SwdEndpointNameContains `
                -Name ([string]$_.name) `
                -Marker $script:V1SwdEndpointBindingNameMarker
        })
        all_endpoint_text = @($endpointSnapshot.lines)
    }
}

function Invoke-V1SwdPnpUtil {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $text = @(& pnputil.exe @Arguments 2>&1)
    return [pscustomobject][ordered]@{
        exit_code = $LASTEXITCODE
        text = @($text)
    }
}

function Test-V1SwdExactRegisteredCandidate {
    param(
        [Parameter(Mandatory = $true)]$Device,
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)][string]$PublishedInf
    )
    $hardwareIds = @($Device.hardware_ids | Where-Object {
        -not [string]::IsNullOrWhiteSpace([string]$_)
    })
    return [string]$Device.instance_id -ieq
            $script:V1SwdEndpointBindingInstanceId -and
        [string]$Device.service -ceq
            $script:V1SwdEndpointBindingService -and
        [string]$Device.published_inf -ieq $PublishedInf -and
        [string]$Device.parent -ieq [string]$Manifest.expected_parent -and
        [string]$Device.container_id -ieq
            [string]$Manifest.remote_container_id -and
        $hardwareIds.Count -eq 1 -and
        [string]$hardwareIds[0] -ieq
            $script:V1SwdEndpointBindingHardwareId
}
