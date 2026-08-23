# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest

$script:V1SwdEndpointBindingPolicyVersion = 2
$script:V1SwdEndpointBindingInstanceId =
    'SWD\NativeLdacSwdEndpoint\Xm5EndpointCandidate'
$script:V1SwdEndpointBindingHardwareId = 'SWD\NativeLdacAudioXm5'
$script:V1SwdEndpointBindingService = 'NativeLdacSwdAudio'
$script:V1SwdEndpointBindingOriginalInf = 'NativeLdacSwdAudio.inf'
$script:V1SwdEndpointBindingNameMarker =
    'Native LDAC SWD Speaker Topology'
$script:V1SwdEndpointRootNameMarker = 'Native LDAC'

function Test-V1SwdEndpointNameContains {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Marker
    )
    return $Name.IndexOf(
        $Marker,
        [StringComparison]::OrdinalIgnoreCase) -ge 0
}

function Test-V1SwdEndpointPublishedState {
    param([Parameter(Mandatory = $true)][string]$State)
    return $State -ceq 'active' -or $State -ceq 'unplugged'
}

function Test-V1SwdEndpointProperty {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Name
    )
    return $null -ne $Value -and
        $null -ne $Value.PSObject.Properties[$Name]
}

function ConvertFrom-V1SwdEndpointVolumeText {
    param([Parameter(Mandatory = $true)][string[]]$Lines)

    $endpoints = [Collections.Generic.List[object]]::new()
    $current = $null
    foreach ($line in $Lines) {
        if ($line -match '^Endpoint:\s*(.+)$') {
            if ($null -ne $current) {
                $endpoints.Add([pscustomobject]$current)
            }
            $current = [ordered]@{
                name = $Matches[1].Trim()
                state = ''
                id = ''
                container_id = ''
                default_roles = ''
                volume_available = $false
                volume_percent = 0.0
                level_db = 0.0
                muted = $false
                step_available = $false
                step_index = 0
                step_count = 0
            }
            continue
        }
        if ($null -eq $current) {
            continue
        }
        if ($line -match '^\s{2}state:\s*(.+)$') {
            $current.state = $Matches[1].Trim()
        } elseif ($line -match '^\s{2}id:\s*(.+)$') {
            $current.id = $Matches[1].Trim()
        } elseif ($line -match '^\s{2}container:\s*(.+)$') {
            $current.container_id = $Matches[1].Trim()
        } elseif ($line -match '^\s{2}default roles:\s*(.+)$') {
            $current.default_roles = $Matches[1].Trim()
        } elseif ($line -match
            '^\s{2}volume:\s*([0-9]+(?:\.[0-9]+)?)%,\s*' +
            '(-?[0-9]+(?:\.[0-9]+)?)\s+dB(\s+\(muted\))?$') {
            $current.volume_available = $true
            $current.volume_percent = [double]::Parse(
                $Matches[1], [Globalization.CultureInfo]::InvariantCulture)
            $current.level_db = [double]::Parse(
                $Matches[2], [Globalization.CultureInfo]::InvariantCulture)
            $current.muted = -not [string]::IsNullOrWhiteSpace($Matches[3])
        } elseif ($line -match
            '^\s{2}volume step:\s*([0-9]+)/([0-9]+)$') {
            $current.step_available = $true
            $current.step_index = [int]$Matches[1]
            $current.step_count = [int]$Matches[2]
        }
    }
    if ($null -ne $current) {
        $endpoints.Add([pscustomobject]$current)
    }
    return @($endpoints)
}

function Test-V1SwdEndpointStableDevice {
    param(
        [Parameter(Mandatory = $true)]$Before,
        [Parameter(Mandatory = $true)]$During,
        [Parameter(Mandatory = $true)]$After,
        [Parameter(Mandatory = $true)][string]$ExpectedService
    )
    foreach ($item in @($Before, $During, $After)) {
        foreach ($property in @(
                'instance_id', 'service', 'published_inf', 'container_id',
                'parent', 'problem_code', 'present')) {
            if (-not (Test-V1SwdEndpointProperty -Value $item `
                    -Name $property)) {
                return $false
            }
        }
        if ($item.present -ne $true -or
            [string]$item.service -cne $ExpectedService -or
            [int]$item.problem_code -ne 0) {
            return $false
        }
    }
    return [string]$Before.instance_id -ieq [string]$During.instance_id -and
        [string]$Before.instance_id -ieq [string]$After.instance_id -and
        [string]$Before.published_inf -ieq [string]$During.published_inf -and
        [string]$Before.published_inf -ieq [string]$After.published_inf -and
        [string]$Before.container_id -ieq [string]$During.container_id -and
        [string]$Before.container_id -ieq [string]$After.container_id -and
        [string]$Before.parent -ieq [string]$During.parent -and
        [string]$Before.parent -ieq [string]$After.parent
}

function Test-V1SwdEndpointStableRootMmDevice {
    param(
        [Parameter(Mandatory = $true)]$Before,
        [Parameter(Mandatory = $true)]$During,
        [Parameter(Mandatory = $true)]$After
    )
    foreach ($item in @($Before, $During, $After)) {
        $hasRootMarker = Test-V1SwdEndpointNameContains `
            -Name ([string]$item.name) `
            -Marker $script:V1SwdEndpointRootNameMarker
        $hasCandidateMarker = Test-V1SwdEndpointNameContains `
            -Name ([string]$item.name) `
            -Marker $script:V1SwdEndpointBindingNameMarker
        $isPublished = Test-V1SwdEndpointPublishedState `
            -State ([string]$item.state)
        if (-not $hasRootMarker -or $hasCandidateMarker -or
            -not $isPublished -or
            [string]::IsNullOrWhiteSpace([string]$item.id) -or
            [string]::IsNullOrWhiteSpace([string]$item.container_id)) {
            return $false
        }
    }
    return [string]$Before.id -ceq [string]$During.id -and
        [string]$Before.id -ceq [string]$After.id -and
        [string]$Before.container_id -ieq [string]$During.container_id -and
        [string]$Before.container_id -ieq [string]$After.container_id -and
        [string]$Before.state -ceq [string]$During.state -and
        [string]$Before.state -ceq [string]$After.state -and
        [string]$Before.default_roles -ceq [string]$During.default_roles -and
        [string]$Before.default_roles -ceq [string]$After.default_roles
}

function Test-V1SwdEndpointBindingEvidence {
    param(
        [Parameter(Mandatory = $true)]$Before,
        [Parameter(Mandatory = $true)]$During,
        [Parameter(Mandatory = $true)]$After,
        [Parameter(Mandatory = $true)]$HostProcess,
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
        -not (Test-V1SwdEndpointStableRootMmDevice `
            -Before $Before.root_mmdevice -During $During.root_mmdevice `
            -After $After.root_mmdevice)) {
        return $false
    }

    $beforeChildren = @($Before.candidate_children)
    $duringChildren = @($During.candidate_children)
    $afterChildren = @($After.candidate_children)
    $beforePackages = @($Before.candidate_packages)
    $duringPackages = @($During.candidate_packages)
    $afterPackages = @($After.candidate_packages)
    if ($beforeChildren.Count -ne 0 -or
        $duringChildren.Count -ne 1 -or
        $afterChildren.Count -ne 0 -or
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

    $beforePublished = @($Before.candidate_mmdevices | Where-Object {
        Test-V1SwdEndpointPublishedState -State ([string]$_.state)
    })
    $duringPublished = @($During.candidate_mmdevices | Where-Object {
        Test-V1SwdEndpointPublishedState -State ([string]$_.state)
    })
    $afterPublished = @($After.candidate_mmdevices | Where-Object {
        Test-V1SwdEndpointPublishedState -State ([string]$_.state)
    })
    if ($beforePublished.Count -ne 0 -or
        $duringPublished.Count -ne 1 -or
        $afterPublished.Count -ne 0) {
        return $false
    }
    $candidateEndpoint = $duringPublished[0]
    if (-not (Test-V1SwdEndpointNameContains `
            -Name ([string]$candidateEndpoint.name) `
            -Marker $script:V1SwdEndpointBindingNameMarker) -or
        [string]$candidateEndpoint.container_id -ine $ExpectedContainer -or
        [string]$candidateEndpoint.default_roles -cne '(none)') {
        return $false
    }

    return $HostProcess.completed -eq $true -and
        $HostProcess.timed_out -eq $false -and
        $HostProcess.forced_termination -eq $false -and
        [int]$HostProcess.exit_code -eq 0 -and
        $Rollback.device_instance_absent -eq $true -and
        $Rollback.package_remove_attempted -eq $true -and
        $Rollback.package_remove_succeeded -eq $true -and
        $Rollback.child_absent -eq $true -and
        $Rollback.published_candidate_endpoint_absent -eq $true
}
