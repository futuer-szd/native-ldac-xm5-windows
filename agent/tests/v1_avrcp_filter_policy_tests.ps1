# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'AVRCP filter policy tests require PowerShell 7.'
}

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-RepoFile([string]$Path) {
    Get-Content -LiteralPath (Join-Path $root $Path) -Raw
}
function Assert-Policy([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$inf = Read-RepoFile 'avrcp-filter\NativeLdacAvrcpIoFilter.inx'
$ioctl = Read-RepoFile `
    'avrcp-filter\include\nativeldac_avrcp_filter_ioctl.h'
$device = Read-RepoFile 'avrcp-filter\sys\device.c'
$project = Read-RepoFile `
    'avrcp-filter\NativeLdacAvrcpIoFilter.vcxproj'
$probe = Read-RepoFile 'tools\v1_avrcp_filter_probe.cpp'
$builder = Read-RepoFile 'tools\build-v1-avrcp-filter-candidate.ps1'
$verifier = Read-RepoFile 'tools\verify-v1-avrcp-filter-candidate.ps1'

$targetId =
    'BTHENUM\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
Assert-Policy ($inf -match '(?im)^Class\s*=\s*Extension\s*$' -and
               $inf -match '(?im)^ClassGuid\s*=\s*\{e2f84ce7-8efa-411c-aa69-97454ca4cb57\}\s*$' -and
               $inf -match '(?im)^ExtensionId\s*=\s*\{[0-9A-F-]+\}\s*$') `
    'The filter package is not a Windows extension INF.'
Assert-Policy ($inf -match [regex]::Escape($targetId)) `
    'The filter is not limited to the exact XM5 AVRCP 0x110E hardware ID.'
Assert-Policy ($inf -notmatch '0000110B|ROOT\\MEDIA|NativeLdacAudio') `
    'The filter package overlaps the A2DP or ROOT endpoint path.'
Assert-Policy ($inf -match '(?im)^\[FilterInstall\.NT\.Filters\]\s*$' -and
               $inf -match '(?im)^AddFilter\s*=\s*NativeLdacAvrcpIoFilter,,FilterPosition\s*$' -and
               $inf -match '(?im)^FilterPosition\s*=\s*Upper\s*$') `
    'The package does not use device-specific AddFilter upper ordering.'
Assert-Policy ($inf -notmatch '(?i)UpperFilters|LowerFilters|ClassInstall32') `
    'The package contains a legacy registry or class-wide filter path.'
Assert-Policy ($inf -match '(?im)^AddService\s*=\s*NativeLdacAvrcpIoFilter,0x00000000,ServiceInstall\s*$') `
    'The filter service must not claim the associated function-service role.'
Assert-Policy ($device -match 'WdfFdoInitSetFilter\s*\(') `
    'The KMDF device is not declared as a filter.'
Assert-Policy ($device -match 'EvtIoDeviceControl\s*=\s*NldAvrcpFilterEvtIoDeviceControl' -and
               $device -match 'EvtIoInternalDeviceControl\s*=\s*NldAvrcpFilterEvtIoInternalDeviceControl') `
    'The filter does not observe both normal and internal device control.'
Assert-Policy ($device -match 'WdfRequestFormatRequestUsingCurrentType' -and
               $device -match 'WdfDeviceGetIoTarget' -and
               $device -match 'WdfRequestSend' -and
               $device -match 'WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET') `
    'The fail-open forwarding path is incomplete.'
Assert-Policy ($device -notmatch 'BtaMpm|BthmpSetServiceStateEx|NldBth|IOCTL_BTH|WdfIoTargetOpen|NldAvrcpObserverEventPassThrough|AVRCP_PASS_THROUGH') `
    'The filter contains a private Bluetooth open or media-key path.'
Assert-Policy ($device -match 'NldAvrcpFilterSendAbsoluteVolume' -and
               $device -match 'NLD_AVRCP_MICROSOFT_AVRCP_WRITE_IOCTL' -and
               $device -match 'volume_request->Volume > 127u' -and
               $device -match 'WDF_REQUEST_SEND_OPTION_TIMEOUT') `
    'The filter absolute-volume write is missing its bounded validation.'
Assert-Policy ($project -notmatch 'direct-pdo|protocol\\src|avrcp-sideband|audio-endpoint|engine\\windows') `
    'The filter project imports an active Bluetooth or audio implementation.'
Assert-Policy (([regex]::Matches(
        $ioctl, 'IOCTL_NLD_AVRCP_FILTER_')).Count -eq 4) `
    'The public filter IOCTL surface changed.'
Assert-Policy (([regex]::Matches(
        $ioctl, 'FILE_READ_ACCESS')).Count -eq 3 -and
               ([regex]::Matches(
        $ioctl, 'FILE_WRITE_ACCESS')).Count -eq 1) `
    'The public filter ABI must expose exactly one write operation.'
Assert-Policy ($ioctl -match 'SET_ABSOLUTE_VOLUME' -and
               $ioctl -notmatch 'IOCTL_NLD_AVRCP_FILTER_.*(PASS_THROUGH|PLAYBACK_STATUS)') `
    'The filter write ABI expanded beyond absolute volume.'
Assert-Policy ($ioctl -match 'NLD_AVRCP_FILTER_RAW_PREFIX_CAPACITY\s+32u') `
    'The raw trace prefix is not bounded to 32 bytes.'
Assert-Policy (([regex]::Matches(
        $device, '!=\s*METHOD_NEITHER')).Count -eq 2) `
    'The filter can dereference METHOD_NEITHER input or output buffers.'
Assert-Policy ($probe -match 'GENERIC_READ' -and
               $probe -notmatch 'GENERIC_WRITE|SendInput|IAudioEndpointVolume|SET_ABSOLUTE_VOLUME') `
    'The filter probe contains a write or input-injection path.'
Assert-Policy ($probe -match 'v1_avrcp_filter_decoder\.h' -and
               $probe -match 'V1AvrcpFilterDecodePacket' -and
               $probe -match 'decoded status:') `
    'The filter probe does not publish decoded AVRCP semantics.'
Assert-Policy ($builder -match 'No driver was staged, installed, bound, restarted, or loaded' -and
               $builder -match 'driver-imports\.txt') `
    'The candidate builder does not preserve the offline-only/import-audit contract.'
Assert-Policy ($builder -match 'policy_version = 7' -and
               $builder -match 'read-write-volume-only' -and
               $builder -match 'SetAbsoluteVolume only') `
    'The candidate builder does not publish the volume-only write contract.'
Assert-Policy ($verifier -match 'btampm|BtaMpm' -and
               $verifier -match 'FilterPosition' -and
               $verifier -match 'UpperFilters') `
    'The candidate verifier does not enforce the private-import and filter-order policy.'

Write-Host 'V1 AVRCP upper-filter policy tests passed.'
