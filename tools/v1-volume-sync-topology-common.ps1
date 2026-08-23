# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest

function Test-V1TopologyProperty {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Name
    )
    return $null -ne $Value.PSObject.Properties[$Name] -and
        -not [string]::IsNullOrWhiteSpace([string]$Value.$Name)
}

function Get-V1VolumeSyncTopologyDecision {
    param(
        [Parameter(Mandatory = $true)]$Transport,
        [Parameter(Mandatory = $true)]$Avrcp,
        [Parameter(Mandatory = $true)]$Endpoint,
        [string]$NativeMmDeviceContainerId,
        [Nullable[int]]$PrivateAvrcpOpenWin32
    )

    $required = @('instance_id', 'service', 'container_id', 'parent')
    $missing = @()
    foreach ($item in @(
            @('transport', $Transport),
            @('avrcp', $Avrcp),
            @('endpoint', $Endpoint))) {
        foreach ($property in $required) {
            if (-not (Test-V1TopologyProperty -Value $item[1] `
                    -Name $property)) {
                $missing += "$($item[0]).$property"
            }
        }
    }

    if ($missing.Count -ne 0) {
        return [pscustomobject][ordered]@{
            valid = $false
            topology = 'invalid'
            reason = 'required-device-property-missing'
            missing_properties = @($missing)
            transport_avrcp_same_container = $false
            endpoint_pnp_same_container = $false
            endpoint_mmdevice_same_container = $false
            endpoint_owned_by_transport = $false
            private_avrcp_accessible = $false
            swd_child_candidate_required = $false
            synchronization_proven = $false
            write_authorization = $false
        }
    }

    $sameTransportContainer = [string]$Transport.container_id -ieq
        [string]$Avrcp.container_id
    $sameEndpointPnpContainer = [string]$Endpoint.container_id -ieq
        [string]$Transport.container_id
    $sameEndpointMmDeviceContainer =
        -not [string]::IsNullOrWhiteSpace($NativeMmDeviceContainerId) -and
        $NativeMmDeviceContainerId -ieq [string]$Transport.container_id
    $ownedByTransport = [string]$Endpoint.parent -ieq
        [string]$Transport.instance_id
    $privateOpenKnown = $null -ne $PrivateAvrcpOpenWin32
    $privateOpenValue = if ($privateOpenKnown) {
        [int]$PrivateAvrcpOpenWin32
    } else {
        $null
    }
    $privateAccessible = $privateOpenKnown -and
        $privateOpenValue -eq 0

    $topology = if ($ownedByTransport -and $sameEndpointPnpContainer) {
        'transport-owned-endpoint'
    } elseif ($sameEndpointMmDeviceContainer) {
        'independent-root-endpoint-with-mmdevice-container-bridge'
    } else {
        'independent-root-endpoint'
    }
    $childRequired = -not $ownedByTransport
    $reason = if (-not $sameTransportContainer) {
        'xm5-transport-and-avrcp-container-mismatch'
    } elseif ($childRequired) {
        'native-endpoint-is-not-owned-by-the-xm5-a2dp-pdo'
    } elseif (-not $privateAccessible) {
        'transport-owned-shape-present-but-avrcp-binding-unproven'
    } else {
        'transport-owned-shape-ready-for-observe-only-validation'
    }

    return [pscustomobject][ordered]@{
        valid = $sameTransportContainer
        topology = $topology
        reason = $reason
        missing_properties = @()
        transport_avrcp_same_container = $sameTransportContainer
        endpoint_pnp_same_container = $sameEndpointPnpContainer
        endpoint_mmdevice_same_container = $sameEndpointMmDeviceContainer
        endpoint_owned_by_transport = $ownedByTransport
        private_avrcp_accessible = $privateAccessible
        private_avrcp_open_win32 = if ($privateOpenKnown) {
            $privateOpenValue
        } else {
            $null
        }
        swd_child_candidate_required = $childRequired
        synchronization_proven = $false
        write_authorization = $false
    }
}
