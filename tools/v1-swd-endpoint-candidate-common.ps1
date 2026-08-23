# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest

$script:V1SwdEndpointCandidatePolicyVersion = 4
$script:V1SwdEndpointHardwareId = 'SWD\NativeLdacAudioXm5'
$script:V1SwdEndpointService = 'NativeLdacSwdAudio'
$script:V1SwdEndpointInstanceId =
    'SWD\NativeLdacSwdEndpoint\Xm5EndpointCandidate'

function Get-V1SwdEndpointCandidate {
    param([Parameter(Mandatory = $true)][string]$CandidatePath)

    $root = [IO.Path]::GetFullPath($CandidatePath)
    $manifestPath = Join-Path $root 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw 'The transport-owned endpoint candidate manifest is missing.'
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw |
        ConvertFrom-Json
    if ([int]$manifest.manifest_version -ne 1 -or
        [int]$manifest.policy_version -ne
            $script:V1SwdEndpointCandidatePolicyVersion -or
        $manifest.source_dirty -ne $false -or
        $manifest.installable -ne $true -or
        $manifest.staged_only -ne $true -or
        $manifest.install_script_included -ne $false -or
        $manifest.device_creation_default -ne $false -or
        $manifest.current_root_endpoint_preserved -ne $true -or
        $manifest.volume_observation_presence_supported -ne $true -or
        $manifest.stop_event_supported -ne $true -or
        $manifest.xm5_connection_probe_included -ne $true -or
        $manifest.capability_only_signaling_hold_supported -ne $true -or
        [int]$manifest.never_default_render_role_mask -ne 0x00000107 -or
        [string]$manifest.hardware_id -ine
            $script:V1SwdEndpointHardwareId -or
        [string]$manifest.service_name -cne
            $script:V1SwdEndpointService) {
        throw 'The transport-owned endpoint candidate contract is invalid.'
    }

    foreach ($file in @($manifest.files)) {
        $path = Join-Path $root ([string]$file.path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "The endpoint candidate file is missing: $path"
        }
        $item = Get-Item -LiteralPath $path
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        if ([long]$item.Length -ne [long]$file.length -or
            $hash -cne [string]$file.sha256) {
            throw "The endpoint candidate file failed integrity validation: $path"
        }
    }

    return [pscustomobject]@{
        root = $root
        manifest = $manifest
    }
}
