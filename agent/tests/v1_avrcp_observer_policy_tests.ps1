# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'AVRCP observer policy tests require PowerShell 7.'
}

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-RepoFile([string]$Path) {
    Get-Content -LiteralPath (Join-Path $root $Path) -Raw
}
function Assert-Policy([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$inf = Read-RepoFile 'avrcp-sideband\NativeLdacAvrcpObserver.inx'
$ioctl = Read-RepoFile `
    'avrcp-sideband\include\nativeldac_avrcp_observer_ioctl.h'
$device = Read-RepoFile 'avrcp-sideband\sys\device.c'
$deviceHeader = Read-RepoFile 'avrcp-sideband\sys\device.h'
$project = Read-RepoFile `
    'avrcp-sideband\NativeLdacAvrcpObserver.vcxproj'
$probe = Read-RepoFile 'tools\v1_avrcp_observer_probe.cpp'
$executor = Read-RepoFile 'tools\v1_avrcp_action_executor.cpp'
$observerHostSource = Read-RepoFile 'agent\v1_avrcp_observer_host.cpp'
$hostHeader = Read-RepoFile 'agent\v1_avrcp_observer_host.h'
$dailyHost = Read-RepoFile 'agent\v1_presence_agent.cpp'
$gate = Read-RepoFile 'tools\run-v1-avrcp-observer-gate.ps1'
$rollback = Read-RepoFile 'tools\rollback-v1-avrcp-observer-gate.ps1'
$builder = Read-RepoFile 'tools\build-v1-avrcp-observer-candidate.ps1'
$verifier = Read-RepoFile 'tools\verify-v1-avrcp-observer-candidate.ps1'

Assert-Policy ($inf -match [regex]::Escape(
    'BTHENUM\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0')) `
    'The candidate is not limited to the XM5 AVRCP 0x110E PDO.'
Assert-Policy ($inf -notmatch '0000110B|ROOT\\MEDIA|NativeLdacAudio') `
    'The AVRCP package overlaps the A2DP or ROOT audio endpoint path.'
Assert-Policy ($inf -match 'AddService\s*=\s*NativeLdacAvrcpObserver') `
    'The isolated AVRCP service name changed.'
Assert-Policy ($deviceHeader -match 'NLD_AVRCP_CONTROL_PSM\s+0x0017u') `
    'The candidate no longer targets AVCTP control PSM 0x0017.'
Assert-Policy (([regex]::Matches(
        $ioctl, 'FILE_WRITE_ACCESS')).Count -eq 1 -and
               $ioctl -match 'IOCTL_NLD_AVRCP_OBSERVER_SEND_COMMAND') `
    'The public AVRCP observer ABI write surface is not limited to the single authorized command IOCTL.'
Assert-Policy (([regex]::Matches($ioctl, 'IOCTL_NLD_AVRCP_')).Count -eq 5) `
    'The public AVRCP observer IOCTL surface changed.'
Assert-Policy ($ioctl -match 'GET_VERSION' -and
               $ioctl -match 'GET_STATUS' -and
               $ioctl -match 'DEQUEUE_EVENT' -and
               $ioctl -match 'BEGIN_OBSERVATION') `
    'The read-only observer IOCTL contract is incomplete.'
Assert-Policy ($device -notmatch 'SET_ABSOLUTE_VOLUME|IAudioEndpointVolume|SendInput|keybd_event') `
    'The observe-only driver contains a system volume or input write path.'
Assert-Policy ($project -notmatch 'audio-endpoint|engine\\windows|LdacNative\.inx') `
    'The isolated AVRCP project references the verified audio data plane.'
Assert-Policy ($probe -notmatch 'FILE_WRITE_ACCESS|SET_ABSOLUTE_VOLUME|IAudioEndpointVolume' -and
               $probe -match '--verify-same-channel-write' -and
               $probe.Contains('? (GENERIC_READ | GENERIC_WRITE)') -and
               $probe.Contains(': GENERIC_READ') -and
               $probe -match 'SubmitSameChannelVolumeWrite' -and
               $probe -match 'IOCTL_NLD_AVRCP_OBSERVER_SEND_COMMAND') `
    'The probe write verification is not confined to its explicit opt-in mode.'
Assert-Policy ($gate -notmatch '--verify-same-channel-write') `
    'The normal observer gate must keep the probe in its default read-only mode.'
Assert-Policy ($probe -match 'IOCTL_NLD_AVRCP_OBSERVER_BEGIN_OBSERVATION' -and
               $executor -match 'IOCTL_NLD_AVRCP_OBSERVER_BEGIN_OBSERVATION') `
    'The observer clients do not explicitly activate the one-shot session.'
Assert-Policy ($dailyHost -match
                   'options\.acl_generation\s*=\s*state->lifecycle\.acl_generation' -and
               $observerHostSource -match
                   'const std::uint64_t generation = options_\.acl_generation' -and
               $observerHostSource -match
                   'MapDriverEvent\(event, mapper_\.acl_generation, &mapped\)') `
    'Observer mapping is no longer anchored to the physical ACL generation.'
Assert-Policy ($hostHeader -match 'void ReleaseTransport\(\)' -and
               $hostHeader -match 'physical_acl_generation\(\)' -and
               $hostHeader -match 'headset_initial_sync_complete\(\)' -and
               $observerHostSource -match
                   'void V1AvrcpObserverHost::Close\(\)[\s\S]*ReleaseTransport\(\);[\s\S]*EndMapperGeneration\(\);') `
    'Transport release and physical-generation teardown are no longer distinct.'
Assert-Policy ($device -match 'AvrcpObserverEventPassThrough') `
    'PASS THROUGH observation is not wired into the event queue.'
Assert-Policy ($device -match 'AVRCP_RESPONSE_INTERIM' -and
               $device -match 'AVRCP_RESPONSE_CHANGED') `
    'Absolute-volume interim/changed evidence is not distinguished.'
Assert-Policy ($deviceHeader -match 'NLD_BTH_SIGNALING_CONTEXT' -and
               $project -notmatch 'inbound_channel') `
    'The observer did not return to the shared outbound signaling core.'
Assert-Policy (([regex]::Matches(
        $device, 'NldBthSignalingOpen\s*\(')).Count -eq 1) `
    'The observer must perform exactly one outbound AVCTP OPEN call.'
$startIndex = $device.IndexOf('static NTSTATUS NldAvrcpObserverStart')
$stopIndex = $device.IndexOf('static void NldAvrcpObserverStop', $startIndex)
$activationIndex = $device.IndexOf(
    'IOCTL_NLD_AVRCP_OBSERVER_BEGIN_OBSERVATION')
$enqueueIndex = $device.IndexOf('WdfWorkItemEnqueue')
$startBlock = $device.Substring($startIndex, $stopIndex - $startIndex)
Assert-Policy ($startIndex -ge 0 -and $stopIndex -gt $startIndex -and
               $startBlock -notmatch 'WdfWorkItemEnqueue|NldBthProfileStart|NldBthChannelStart|NldBthSignalingOpen' -and
               $activationIndex -gt $stopIndex -and $enqueueIndex -gt $activationIndex) `
    'BTH profile/channel acquisition is no longer deferred to the explicit post-media activation.'
$workerIndex = $device.IndexOf('VOID NldAvrcpObserverWorker')
$profileIndex = $device.IndexOf('NldBthProfileStart', $workerIndex)
$channelIndex = $device.IndexOf('NldBthChannelStart', $profileIndex)
$openIndex = $device.IndexOf('NldBthSignalingOpen', $channelIndex)
$pendingIndex = $device.IndexOf('STATUS_PENDING', $openIndex)
$drainIndex = $device.IndexOf(
    'NldBthSignalingWaitForRequestDrain', $pendingIndex)
$snapshotIndex = $device.IndexOf(
    'NldBthSignalingGetSnapshot', $drainIndex)
$stateIndex = $device.IndexOf(
    'NldBthSignalingChannelOpen', $snapshotIndex)
Assert-Policy ($workerIndex -ge 0 -and
               $profileIndex -gt $workerIndex -and
               $channelIndex -gt $profileIndex -and
               $openIndex -gt $channelIndex -and
               $pendingIndex -gt $openIndex -and
               $drainIndex -gt $pendingIndex -and
               $snapshotIndex -gt $drainIndex -and
               $stateIndex -gt $snapshotIndex) `
    'The one-shot OPEN lost its pending/drain/snapshot/channel-open proof.'
Assert-Policy ($device -match 'NldAvrcpObserverEvtFileCleanup' -and
               $device -match 'WdfDeviceInitSetFileObjectConfig' -and
               $device -match 'WdfWorkItemFlush' -and
               $device -match 'signaling_snapshot\.RemoteDisconnected') `
    'Observation cleanup no longer releases the current-ACL profile or rearms after physical disconnect.'
Assert-Policy ($deviceHeader -match 'WriteTransactionActive' -and
               $device -match 'context->WriteTransactionActive' -and
               $device -match 'AVRCP_OBSERVER_EVENT_WRITE_RESPONSE' -and
               $device -match 'STATUS_DEVICE_BUSY') `
    'Observer writes are not serialized until the remote AVRCP response completes.'
Assert-Policy ($probe -match 'CP_UTF8' -and
               $probe -notmatch '_O_U16TEXT') `
    'The observer probe output is not PowerShell-safe UTF-8.'
Assert-Policy ($gate -match [regex]::Escape(
    'BTHENUM\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0')) `
    'The true-device gate is not limited to the exact 0x110E PDO prefix.'
Assert-Policy ($gate -match 'microsoft_bluetooth_avrcptransport\.inf' -and
               $gate -match 'Microsoft_Bluetooth_AvrcpTransport') `
    'The gate does not pin the Microsoft rollback baseline.'
$stageIndex = $gate.IndexOf('$stage = Invoke-PnpUtil')
$connectIndex = $gate.IndexOf('--wait-acl-connect')
$holdIndex = $gate.IndexOf(
    "Write-Host 'Capability-only AVDTP signaling is active")
$applyIndex = $gate.IndexOf('$apply = Invoke-PnpUtil')
$restoreIndex = $gate.IndexOf('$rollback = Restore-Baseline')
$releaseIndex = $gate.IndexOf('[void]$signalingStopEvent.Set()')
Assert-Policy ($stageIndex -ge 0 -and
               $connectIndex -gt $stageIndex -and
               $holdIndex -gt $connectIndex -and
               $applyIndex -gt $holdIndex -and
               $restoreIndex -gt $applyIndex -and
               $releaseIndex -gt $restoreIndex) `
    'The stage/connect/AVDTP-hold/bind/restore/release ordering changed.'
$stageBlock = $gate.Substring($stageIndex, $connectIndex - $stageIndex)
$applyBlock = $gate.Substring($applyIndex, $restoreIndex - $applyIndex)
Assert-Policy ($stageBlock -notmatch "'/install'" -and
               $applyBlock -match "'/install'") `
    'The candidate must be staged only before ACL and bound only after the AVDTP hold is ready.'
Assert-Policy ($gate -notmatch '/disable-device|/enable-device|Set-NetAdapter|Restart-Service\s+bthserv') `
    'The gate acquired a Bluetooth radio, A2DP, or ROOT endpoint mutation.'
Assert-Policy ($gate -match '--discover' -and
               $gate -match '--open-attempts' -and
               $gate -match '--hold-signaling-seconds' -and
               $gate -match '--stop-event') `
    'The gate no longer establishes the bounded capability-only AVDTP prerequisite.'
Assert-Policy ($gate -notmatch '--configure|--play|--silence|--pcm|--media') `
    'The AVRCP gate acquired an AVDTP configuration or media path.'
Assert-Policy ($rollback -match '/delete-driver' -and
               $rollback -match '/scan-devices' -and
               $rollback -match '/restart-device') `
    'The exact rollback sequence is incomplete.'
Assert-Policy ($builder -match 'clean Git HEAD' -and
               $builder -match 'verify-v1-golden-checkpoint') `
    'The candidate builder is not bound to clean source and the golden checkpoint.'
Assert-Policy ($builder -match 'BEGIN_OBSERVATION' -and
               $builder -match 'profile_acquisition' -and
               $verifier -match 'observation_activation' -and
               $verifier -match 'profile_acquisition') `
    'The candidate manifest does not lock explicit post-media activation.'
Assert-Policy ($verifier -match 'manifest\.sha256' -and
               $verifier -match 'read-write') `
    'The candidate verifier does not lock integrity and the authorized write contract.'
Assert-Policy ((@($gate, $rollback, $builder, $verifier) -join "`n") `
               -notmatch 'powershell\.exe') `
    'A new AVRCP workflow regressed to Windows PowerShell 5.1.'

Write-Host 'V1 AVRCP observer static policy passed.'
Write-Host 'The package is XM5/0x110E-only; it acquires a fresh current-ACL profile for one post-media observation activation and retains one authorized command IOCTL.'
Write-Host 'The normal probe remains read-only; same-channel write verification requires an explicit experiment-only switch.'
Write-Host 'No A2DP, ROOT endpoint, Core Audio write, input injection, or arbitrary AVRCP write path is present.'
