# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'v1-swd-endpoint-binding-common.ps1')

$script:V1SwdVolumeObservationPolicyVersion = 4

function Get-V1SwdVolumePair {
    param(
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)][object[]]$Endpoints
    )
    $root = @($Endpoints | Where-Object {
        (Test-V1SwdEndpointNameContains `
            -Name ([string]$_.name) `
            -Marker $script:V1SwdEndpointRootNameMarker) -and
        -not (Test-V1SwdEndpointNameContains `
            -Name ([string]$_.name) `
            -Marker $script:V1SwdEndpointBindingNameMarker) -and
        (Test-V1SwdEndpointPublishedState -State ([string]$_.state)) -and
        [string]$_.container_id -ieq
            [string]$Manifest.remote_container_id
    })
    $candidate = @($Endpoints | Where-Object {
        (Test-V1SwdEndpointNameContains `
            -Name ([string]$_.name) `
            -Marker $script:V1SwdEndpointBindingNameMarker) -and
        (Test-V1SwdEndpointPublishedState -State ([string]$_.state)) -and
        [string]$_.container_id -ieq
            [string]$Manifest.remote_container_id
    })
    return [pscustomobject]@{
        root = if ($root.Count -eq 1) { $root[0] } else { $null }
        root_count = $root.Count
        candidate = if ($candidate.Count -eq 1) {
            $candidate[0]
        } else {
            $null
        }
        candidate_count = $candidate.Count
    }
}

function Test-V1SwdVolumeRootMmIdentity {
    param(
        [Parameter(Mandatory = $true)]$Before,
        [Parameter(Mandatory = $true)]$During,
        [Parameter(Mandatory = $true)]$After
    )
    foreach ($item in @($Before, $During, $After)) {
        foreach ($property in @(
                'name', 'state', 'id', 'container_id', 'default_roles')) {
            if (-not (Test-V1SwdEndpointProperty -Value $item `
                    -Name $property)) {
                return $false
            }
        }
        if (-not (Test-V1SwdEndpointNameContains `
                -Name ([string]$item.name) `
                -Marker $script:V1SwdEndpointRootNameMarker) -or
            (Test-V1SwdEndpointNameContains `
                -Name ([string]$item.name) `
                -Marker $script:V1SwdEndpointBindingNameMarker) -or
            -not (Test-V1SwdEndpointPublishedState `
                -State ([string]$item.state)) -or
            [string]::IsNullOrWhiteSpace([string]$item.id) -or
            [string]::IsNullOrWhiteSpace([string]$item.container_id)) {
            return $false
        }
    }
    return [string]$Before.id -ceq [string]$During.id -and
        [string]$Before.id -ceq [string]$After.id -and
        [string]$Before.container_id -ieq [string]$During.container_id -and
        [string]$Before.container_id -ieq [string]$After.container_id -and
        [string]$Before.default_roles -ceq
            [string]$During.default_roles -and
        [string]$Before.default_roles -ceq [string]$After.default_roles
}

function Get-V1SwdDefaultEndpointSignatures {
    param([Parameter(Mandatory = $true)]$Snapshot)

    if (-not (Test-V1SwdEndpointProperty `
            -Value $Snapshot -Name 'default_endpoints')) {
        return $null
    }
    $signatures = [Collections.Generic.List[string]]::new()
    foreach ($endpoint in @($Snapshot.default_endpoints)) {
        if (-not (Test-V1SwdEndpointProperty -Value $endpoint -Name 'id') -or
            -not (Test-V1SwdEndpointProperty `
                -Value $endpoint -Name 'default_roles') -or
            [string]::IsNullOrWhiteSpace([string]$endpoint.id) -or
            [string]$endpoint.default_roles -ceq '(none)') {
            return $null
        }
        $signatures.Add(('{0}|{1}' -f
            ([string]$endpoint.id).ToUpperInvariant(),
            [string]$endpoint.default_roles))
    }
    return @($signatures | Sort-Object -Unique)
}

function Test-V1SwdDefaultEndpointRolesStable {
    param(
        [Parameter(Mandatory = $true)]$Before,
        [Parameter(Mandatory = $true)]$During,
        [Parameter(Mandatory = $true)]$After
    )

    $beforeSignatures = Get-V1SwdDefaultEndpointSignatures `
        -Snapshot $Before
    $duringSignatures = Get-V1SwdDefaultEndpointSignatures `
        -Snapshot $During
    $afterSignatures = Get-V1SwdDefaultEndpointSignatures `
        -Snapshot $After
    if ($null -eq $beforeSignatures -or
        $null -eq $duringSignatures -or
        $null -eq $afterSignatures) {
        return $false
    }
    return (@($beforeSignatures) -join "`n") -ceq
            (@($duringSignatures) -join "`n") -and
        (@($beforeSignatures) -join "`n") -ceq
            (@($afterSignatures) -join "`n")
}

function Get-V1SwdVolumeObservationClassification {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [object[]]$Samples
    )

    $candidateSignatures = @($Samples | ForEach-Object {
        if ($null -eq $_.candidate -or
            $_.candidate.volume_available -ne $true) {
            return
        }
        '{0:F3}|{1}|{2}' -f
            [double]$_.candidate.volume_percent,
            [bool]$_.candidate.muted,
            [int]$_.candidate.step_index
    } | Sort-Object -Unique)
    $rootSignatures = @($Samples | ForEach-Object {
        if ($null -eq $_.root -or $_.root.volume_available -ne $true) {
            return
        }
        '{0:F3}|{1}|{2}' -f
            [double]$_.root.volume_percent,
            [bool]$_.root.muted,
            [int]$_.root.step_index
    } | Sort-Object -Unique)
    $candidateChanged = $candidateSignatures.Count -gt 1
    $rootChanged = $rootSignatures.Count -gt 1
    $classification = if ($candidateChanged -and $rootChanged) {
        'candidate-and-root-changed'
    } elseif ($candidateChanged) {
        'candidate-only-changed'
    } elseif ($rootChanged) {
        'root-only-changed'
    } else {
        'no-public-endpoint-change'
    }
    return [pscustomobject][ordered]@{
        classification = $classification
        candidate_volume_changed = $candidateChanged
        root_volume_changed = $rootChanged
        candidate_distinct_values = $candidateSignatures.Count
        root_distinct_values = $rootSignatures.Count
        candidate_signatures = @($candidateSignatures)
        root_signatures = @($rootSignatures)
    }
}

function Test-V1SwdVolumeObservationEvidence {
    param(
        [Parameter(Mandatory = $true)]$Before,
        [Parameter(Mandatory = $true)]$During,
        [Parameter(Mandatory = $true)]$After,
        [Parameter(Mandatory = $true)]$Observation,
        [Parameter(Mandatory = $true)]$HostProcess,
        [Parameter(Mandatory = $true)]$SignalingProcess,
        [Parameter(Mandatory = $true)]$Acl,
        [Parameter(Mandatory = $true)]$Rollback,
        [Parameter(Mandatory = $true)][string]$ExpectedParent,
        [Parameter(Mandatory = $true)][string]$ExpectedContainer
    )

    if (-not (Test-V1SwdEndpointStableDevice `
            -Before $Before.transport -During $During.transport `
            -After $After.transport -ExpectedService 'LdacNative') -or
        -not (Test-V1SwdEndpointStableDevice `
            -Before $Before.root_endpoint -During $During.root_endpoint `
            -After $After.root_endpoint -ExpectedService 'NativeLdacAudio') -or
        -not (Test-V1SwdVolumeRootMmIdentity `
            -Before $Before.root_mmdevice -During $During.root_mmdevice `
            -After $After.root_mmdevice) -or
        -not (Test-V1SwdDefaultEndpointRolesStable `
            -Before $Before -During $During -After $After)) {
        return $false
    }

    $beforeChildren = @($Before.candidate_children)
    $duringChildren = @($During.candidate_children)
    $afterRegistered = @($After.candidate_registered_devices)
    $beforePackages = @($Before.candidate_packages)
    $duringPackages = @($During.candidate_packages)
    $afterPackages = @($After.candidate_packages)
    if ($beforeChildren.Count -ne 0 -or
        @($Before.candidate_registered_devices).Count -ne 0 -or
        $duringChildren.Count -ne 1 -or
        $afterRegistered.Count -ne 0 -or
        $beforePackages.Count -ne 0 -or
        $duringPackages.Count -ne 1 -or
        $afterPackages.Count -ne 0) {
        return $false
    }

    $child = $duringChildren[0]
    $package = $duringPackages[0]
    $hardwareIds = @($child.hardware_ids | Where-Object {
        -not [string]::IsNullOrWhiteSpace([string]$_)
    })
    if ([string]$child.instance_id -ine
            $script:V1SwdEndpointBindingInstanceId -or
        $child.present -ne $true -or
        [int]$child.problem_code -ne 0 -or
        [string]$child.parent -ine $ExpectedParent -or
        [string]$child.container_id -ine $ExpectedContainer -or
        [string]$child.service -cne
            $script:V1SwdEndpointBindingService -or
        $hardwareIds.Count -ne 1 -or
        [string]$hardwareIds[0] -ine
            $script:V1SwdEndpointBindingHardwareId -or
        [string]$package.original_file_name -ine
            $script:V1SwdEndpointBindingOriginalInf -or
        [string]$package.published_inf -ine [string]$child.published_inf) {
        return $false
    }

    $duringPublished = @($During.candidate_mmdevices | Where-Object {
        Test-V1SwdEndpointPublishedState -State ([string]$_.state)
    })
    $afterPublished = @($After.candidate_mmdevices | Where-Object {
        Test-V1SwdEndpointPublishedState -State ([string]$_.state)
    })
    if ($duringPublished.Count -ne 1 -or
        [string]$duringPublished[0].state -cne 'active' -or
        [string]$duringPublished[0].container_id -ine $ExpectedContainer -or
        [string]$duringPublished[0].default_roles -cne '(none)' -or
        $duringPublished[0].volume_available -ne $true -or
        $afterPublished.Count -ne 0) {
        return $false
    }

    return $Observation.completed -eq $true -and
        [int]$Observation.sample_count -ge 10 -and
        [int]$Observation.query_failures -eq 0 -and
        [int]$Observation.candidate_active_samples -eq
            [int]$Observation.sample_count -and
        [int]$Observation.candidate_default_role_violations -eq 0 -and
        $Acl.connect_observed -eq $true -and
        $Acl.disconnect_observed -eq $false -and
        $Acl.disconnect_required -eq $false -and
        $Acl.transport_released_before_power_off -eq $true -and
        $HostProcess.completed -eq $true -and
        $HostProcess.timed_out -eq $false -and
        $HostProcess.forced_termination -eq $false -and
        $HostProcess.stop_event_observed -eq $true -and
        [int]$HostProcess.exit_code -eq 0 -and
        $SignalingProcess.started -eq $true -and
        $SignalingProcess.ready -eq $true -and
        $SignalingProcess.completed -eq $true -and
        $SignalingProcess.timed_out -eq $false -and
        $SignalingProcess.forced_termination -eq $false -and
        $SignalingProcess.capability_discovery_completed -eq $true -and
        $SignalingProcess.hold_started -eq $true -and
        $SignalingProcess.stop_event_observed -eq $true -and
        $SignalingProcess.channel_closed -eq $true -and
        [int]$SignalingProcess.exit_code -eq 0 -and
        $Rollback.device_instance_absent -eq $true -and
        $Rollback.package_remove_succeeded -eq $true -and
        $Rollback.published_candidate_endpoint_absent -eq $true
}
