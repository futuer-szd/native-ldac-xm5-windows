# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest

$script:V1SwdChildTopologyPolicyVersion = 2
$script:V1SwdChildFriendlyName =
    'Native LDAC XM5 volume-sync topology probe'
$script:V1SwdChildInstanceId =
    'SWD\NativeLdacVolumeSyncProbe\Xm5TopologyProbe'
$script:V1SwdChildInboxInf = 'c_swdevice.inf'

function Test-V1SwdProperty {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Name
    )
    return $null -ne $Value -and
        $null -ne $Value.PSObject.Properties[$Name]
}

function Test-V1SwdStringArrayEqual {
    param(
        [Parameter(Mandatory = $true)]$Left,
        [Parameter(Mandatory = $true)]$Right
    )
    $leftValues = @($Left | ForEach-Object { [string]$_ } | Sort-Object)
    $rightValues = @($Right | ForEach-Object { [string]$_ } | Sort-Object)
    if ($leftValues.Count -ne $rightValues.Count) {
        return $false
    }
    for ($index = 0; $index -lt $leftValues.Count; ++$index) {
        if (-not $leftValues[$index].Equals(
                $rightValues[$index],
                [StringComparison]::OrdinalIgnoreCase)) {
            return $false
        }
    }
    return $true
}

function Test-V1SwdStableDevice {
    param(
        [Parameter(Mandatory = $true)]$Before,
        [Parameter(Mandatory = $true)]$After,
        [Parameter(Mandatory = $true)][string]$ExpectedService
    )
    foreach ($property in @(
            'instance_id', 'service', 'published_inf', 'container_id',
            'parent', 'problem_code')) {
        if (-not (Test-V1SwdProperty -Value $Before -Name $property) -or
            -not (Test-V1SwdProperty -Value $After -Name $property)) {
            return $false
        }
    }
    return [string]$Before.service -eq $ExpectedService -and
        [string]$After.service -eq $ExpectedService -and
        [int]$Before.problem_code -eq 0 -and
        [int]$After.problem_code -eq 0 -and
        [string]$Before.instance_id -ieq [string]$After.instance_id -and
        [string]$Before.published_inf -ieq [string]$After.published_inf -and
        [string]$Before.container_id -ieq [string]$After.container_id -and
        [string]$Before.parent -ieq [string]$After.parent
}

function Test-V1SwdChildLifecycleEvidence {
    param(
        [Parameter(Mandatory = $true)]$Before,
        [Parameter(Mandatory = $true)]$During,
        [Parameter(Mandatory = $true)]$After,
        [Parameter(Mandatory = $true)]$Probe,
        [Parameter(Mandatory = $true)][string]$ExpectedParent,
        [Parameter(Mandatory = $true)][string]$ExpectedContainer
    )

    $beforeChildren = @($Before.child_devices)
    $duringChildren = @($During.child_devices)
    $afterChildren = @($After.child_devices)
    if ($beforeChildren.Count -ne 0 -or
        $duringChildren.Count -ne 1 -or
        $afterChildren.Count -ne 0) {
        return $false
    }
    $child = $duringChildren[0]
    $hardwareIds = @($child.hardware_ids | Where-Object {
        -not [string]::IsNullOrWhiteSpace([string]$_)
    })
    foreach ($property in @(
            'friendly_name', 'present', 'parent', 'container_id', 'service',
            'published_inf', 'hardware_ids')) {
        if (-not (Test-V1SwdProperty -Value $child -Name $property)) {
            return $false
        }
    }
    if ($child.present -ne $true -or
        [string]$child.friendly_name -ne $script:V1SwdChildFriendlyName -or
        [string]$child.parent -ine $ExpectedParent -or
        [string]$child.container_id -ine $ExpectedContainer -or
        -not [string]::IsNullOrWhiteSpace([string]$child.service) -or
        [string]$child.published_inf -ine $script:V1SwdChildInboxInf -or
        $hardwareIds.Count -ne 0) {
        return $false
    }
    if (-not (Test-V1SwdStableDevice -Before $Before.transport `
            -After $After.transport -ExpectedService 'LdacNative') -or
        -not (Test-V1SwdStableDevice -Before $Before.endpoint `
            -After $After.endpoint -ExpectedService 'NativeLdacAudio') -or
        -not (Test-V1SwdStableDevice -Before $Before.transport `
            -After $During.transport -ExpectedService 'LdacNative') -or
        -not (Test-V1SwdStableDevice -Before $Before.endpoint `
            -After $During.endpoint -ExpectedService 'NativeLdacAudio')) {
        return $false
    }
    if (-not (Test-V1SwdStringArrayEqual -Left $Before.driver_packages `
            -Right $During.driver_packages) -or
        -not (Test-V1SwdStringArrayEqual -Left $Before.driver_packages `
            -Right $After.driver_packages) -or
        [string]$Before.endpoint_volume_sha256 -cne
            [string]$During.endpoint_volume_sha256 -or
        [string]$Before.endpoint_volume_sha256 -cne
            [string]$After.endpoint_volume_sha256) {
        return $false
    }
    return [int]$Probe.exit_code -eq 0 -and
        $Probe.completed -eq $true -and
        $Probe.timed_out -eq $false -and
        $Probe.forced_termination -eq $false
}

function Get-V1SwdChildTopologyCandidate {
    param([Parameter(Mandatory = $true)][string]$CandidatePath)

    $root = [IO.Path]::GetFullPath($CandidatePath)
    $manifestPath = Join-Path $root 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw 'The SWD child topology candidate manifest is missing.'
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw |
        ConvertFrom-Json
    if ([int]$manifest.manifest_version -ne 1 -or
        [int]$manifest.policy_version -ne
            $script:V1SwdChildTopologyPolicyVersion -or
        $manifest.source_dirty -ne $false -or
        $manifest.driverless -ne $true -or
        $manifest.custom_driver_binding -ne $false -or
        [string]$manifest.inbox_null_driver_inf -ine
            $script:V1SwdChildInboxInf -or
        -not [string]::IsNullOrWhiteSpace(
            [string]$manifest.function_service) -or
        $manifest.audio_endpoint_creation -ne $false -or
        $manifest.driver_install -ne $false -or
        [int]$manifest.maximum_duration_seconds -ne 30) {
        throw 'The SWD child topology candidate contract is invalid.'
    }
    foreach ($file in @($manifest.files)) {
        $path = Join-Path $root ([string]$file.path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "The SWD candidate file is missing: $path"
        }
        $item = Get-Item -LiteralPath $path
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        if ([long]$item.Length -ne [long]$file.length -or
            $hash -cne [string]$file.sha256) {
            throw "The SWD candidate file failed integrity validation: $path"
        }
    }
    return [pscustomobject]@{
        root = $root
        manifest = $manifest
    }
}
