# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    return Get-Content -LiteralPath `
        (Join-Path $projectRoot $RelativePath) -Raw
}

$abi = Read-ProjectFile 'audio-endpoint\Source\Inc\nativeldac_pcm_abi.h'
$common = Read-ProjectFile 'audio-endpoint\Source\Main\common.cpp'
$miniport = Read-ProjectFile 'audio-endpoint\Source\Main\minwavert.cpp'
$table = Read-ProjectFile 'audio-endpoint\Source\Filters\speakerwavtable.h'
$sink = Read-ProjectFile 'agent\v1_endpoint_presence_sink.cpp'
$agent = Read-ProjectFile 'agent\v1_presence_agent.cpp'

foreach ($required in @(
        'NativeLdacPcmPropertyPhysicalPresence = 4',
        'NATIVE_LDAC_PRESENCE_STATE_ABI_VERSION',
        'PresenceGeneration')) {
    if (-not $abi.Contains($required)) {
        throw "Physical-presence ABI is missing: $required"
    }
}
foreach ($required in @(
        'NativeLdacSetPhysicalPresence',
        'NativeLdacPresenceStateIsFreshPresent',
        'NativeLdacExpirePresenceState')) {
    if (-not $common.Contains($required)) {
        throw "Adapter presence implementation is missing: $required"
    }
}
$linkStart = $common.IndexOf(
    'CAdapterCommon::NativeLdacSetLinkState',
    [StringComparison]::Ordinal)
$presenceStart = $common.IndexOf(
    'CAdapterCommon::NativeLdacGetPhysicalPresence',
    $linkStart,
    [StringComparison]::Ordinal)
if ($linkStart -lt 0 -or $presenceStart -le $linkStart) {
    throw 'Could not isolate the media link-state setter.'
}
$linkSetter = $common.Substring($linkStart, $presenceStart - $linkStart)
foreach ($forbidden in @(
        'm_NativeLdacJackConnected',
        'GenerateNativeLdacJackInfoChange')) {
    if ($linkSetter.Contains($forbidden)) {
        throw "Media link state still controls jack presence: $forbidden"
    }
}
foreach ($required in @(
        'PropertyHandlerPhysicalPresence',
        'NativeLdacPresenceOwnerSetState')) {
    if (-not $miniport.Contains($required)) {
        throw "Miniport presence ownership is missing: $required"
    }
}
if (-not $table.Contains('NativeLdacPcmPropertyPhysicalPresence')) {
    throw 'Wave filter does not expose the physical-presence property.'
}
foreach ($required in @(
        'NativeLdacPcmPropertyInfo',
        'NativeLdacPcmPropertyPhysicalPresence',
        'IOCTL_KS_PROPERTY')) {
    if (-not $sink.Contains($required)) {
        throw "Endpoint presence sink is missing: $required"
    }
}
foreach ($forbidden in @(
        'NativeLdacPcmPropertyLinkState',
        'NativeLdacPcmPropertyRead',
        'NativeLdacPcmPropertyPreferredFormat',
        'NldDirectPdo',
        'BluetoothFind',
        'CreateProcess')) {
    if ($sink.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "Endpoint presence sink exceeds its authority: $forbidden"
    }
}
foreach ($required in @(
        '--endpoint-presence',
        'kPresenceHeartbeatMs = 5000u',
        'WatcherLeaseExpired')) {
    if (-not $agent.Contains($required)) {
        throw "V1 agent presence lease is missing: $required"
    }
}

Write-Host 'V1 endpoint physical-presence policy tests passed.'
