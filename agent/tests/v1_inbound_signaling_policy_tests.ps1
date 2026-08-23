# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    Get-Content -LiteralPath (Join-Path $root $RelativePath) -Raw
}

$device = Read-ProjectFile 'driver\sys\device.c'
$context = Read-ProjectFile 'driver\sys\device.h'
$connection = Read-ProjectFile 'driver\sys\connection.c'
$header = Read-ProjectFile 'driver\sys\connection.h'
$runtime = Read-ProjectFile 'agent\runtime_support.cpp'
$backend = Read-ProjectFile 'agent\v1_transport_driver_backend.cpp'
$probe = Read-ProjectFile 'tools\transport_probe.c'

foreach ($required in @(
        'SignalingServerHandle',
        'SignalingChannelIsIncoming',
        'IncomingSignalingRequest',
        'IncomingSignalingCompletedEvent',
        'SignalingServerDraining',
        'SignalingServerRundownWorkItem',
        'SignalingServerRundownStatus',
        'LDAC_NATIVE_FILE_CONTEXT',
        'OwnsTransport')) {
    if (-not $context.Contains($required)) {
        throw "The inbound signaling device context is missing: $required"
    }
}
foreach ($required in @(
        'LDAC_NATIVE_INCOMING_REQUEST_CONTEXT',
        'LdacNativeRegisterSignalingServer',
        'LdacNativeUnregisterSignalingServer')) {
    if (-not $header.Contains($required)) {
        throw "The inbound signaling header contract is missing: $required"
    }
}
foreach ($required in @(
        'BRB_L2CA_REGISTER_SERVER',
        'if (Context->SignalingServerHandle != NULL)',
        'LDAC_NATIVE_DEVICE_INFO_INBOUND_SIGNALING_READY',
        'server->PSM = LDAC_NATIVE_AVDTP_PSM',
        'server->IndicationCallback = LdacNativeSignalingServerIndication',
        'BRB_L2CA_OPEN_ENHANCED_CHANNEL_RESPONSE',
        'brb->Response = accepted',
        'CONNECT_RSP_RESULT_SUCCESS',
        'CONNECT_RSP_RESULT_NO_RESOURCES',
        'brb->IncomingQueueDepth = 8u',
        'Parameters->Parameters.Connect.Request.PSM ==',
        'Context->SignalingState == LdacNativeChannelDisconnected',
        'context->SignalingChannelIsIncoming = TRUE',
        'LdacNativeQueueSignalingServerRundownLocked(context)',
        'registerIncomingServer = Context->PnpStarted',
        'status = LdacNativeRegisterSignalingServer(Context)',
        'WdfWorkItemFlush(Context->SignalingServerRundownWorkItem)',
        'status = Context->SignalingServerRundownStatus',
        'Indication == IndicationAddReference',
        'Indication == IndicationReleaseReference',
        'LDAC_NATIVE_OPEN_DIAGNOSTIC_INBOUND_CHANNEL',
        'Context->SignalingState == LdacNativeChannelConnected',
        'KeWaitForSingleObject(',
        '&Context->IncomingSignalingCompletedEvent',
        'return STATUS_IO_TIMEOUT',
        'output->IncomingMtu = Context->SignalingIncomingMtu',
        'WdfRequestCancelSentRequest(incomingRequest)',
        'IncomingSignalingCompletedEvent',
        'BRB_L2CA_UNREGISTER_SERVER')) {
    if (-not $connection.Contains($required)) {
        throw "The inbound signaling runtime contract is missing: $required"
    }
}
$connectedReuse = $connection.IndexOf(
    'if (Context->SignalingState == LdacNativeChannelConnected)')
$outboundAllocate = $connection.IndexOf(
    'BRB_L2CA_OPEN_ENHANCED_CHANNEL,', $connectedReuse)
if ($connectedReuse -lt 0 -or $outboundAllocate -le $connectedReuse) {
    throw 'An accepted inbound signaling channel is not reused before outbound OPEN.'
}
$onDemandRegister = $connection.IndexOf(
    'status = LdacNativeRegisterSignalingServer(Context)')
$incomingWait = $connection.IndexOf(
    '&Context->IncomingSignalingCompletedEvent', $onDemandRegister)
if ($onDemandRegister -lt 0 -or $incomingWait -le $onDemandRegister) {
    throw 'A later playback cannot re-arm and wait for an inbound signaling channel.'
}
$rundownFlush = $connection.IndexOf(
    'WdfWorkItemFlush(Context->SignalingServerRundownWorkItem)')
if ($rundownFlush -lt 0 -or $onDemandRegister -le $rundownFlush) {
    throw 'A later playback can race an unfinished server rundown.'
}
$incomingCompletionStart = $connection.IndexOf(
    'static VOID LdacNativeIncomingSignalingCompletion')
$incomingResponseStart = $connection.IndexOf(
    'static NTSTATUS LdacNativeSendIncomingSignalingResponse',
    $incomingCompletionStart)
$incomingCompletion = $connection.Substring(
    $incomingCompletionStart,
    $incomingResponseStart - $incomingCompletionStart)
$publishConnected = $incomingCompletion.IndexOf(
    'context->SignalingState = LdacNativeChannelConnected;')
$queueRundown = $incomingCompletion.IndexOf(
    'LdacNativeQueueSignalingServerRundownLocked(context);',
    $publishConnected)
$publishUnlock = $incomingCompletion.IndexOf(
    'WdfSpinLockRelease(context->SignalingLock);', $queueRundown)
if ($incomingCompletionStart -lt 0 -or
    $incomingResponseStart -le $incomingCompletionStart -or
    $publishConnected -lt 0 -or $queueRundown -le $publishConnected -or
    $publishUnlock -le $queueRundown) {
    throw 'The accepted channel can become visible before listener rundown is queued under the same lock.'
}
$registerServer = $connection.IndexOf('BRB_L2CA_REGISTER_SERVER,')
if ($registerServer -lt 0 -or
    $connection.Contains('BRB_REGISTER_PSM') -or
    $connection.Contains('BRB_UNREGISTER_PSM')) {
    throw 'The fixed AVDTP PSM must be owned by the L2CAP server, not the dynamic-PSM BRBs.'
}
$suspendStart = $device.IndexOf(
    'NTSTATUS LdacNativeEvtSelfManagedIoSuspend')
$restartStart = $device.IndexOf(
    'NTSTATUS LdacNativeEvtSelfManagedIoRestart', $suspendStart)
$cleanupStart = $device.IndexOf(
    'VOID LdacNativeEvtSelfManagedIoCleanup', $restartStart)
if ($suspendStart -lt 0 -or $restartStart -le $suspendStart -or
    $cleanupStart -le $restartStart) {
    throw 'The PnP suspend/restart lifecycle callbacks are missing.'
}
$suspend = $device.Substring($suspendStart, $restartStart - $suspendStart)
$markStopped = $suspend.IndexOf(
    'LdacNativeSetLifecycleState(context, FALSE, TRUE);')
$unregister = $suspend.IndexOf(
    'status = LdacNativeUnregisterSignalingServer(context, FALSE);',
    $markStopped)
$shutdown = $suspend.IndexOf(
    'LdacNativeConnectionShutdown(context);', $unregister)
$flush = $suspend.IndexOf(
    'WdfWorkItemFlush(context->SignalingServerRundownWorkItem);',
    $shutdown)
if ($markStopped -lt 0 -or $unregister -le $markStopped -or
    $shutdown -le $unregister -or $flush -le $shutdown) {
    throw 'PnP suspend does not stop callbacks, unregister the server, close channels, then flush listener rundown.'
}
$restart = $device.Substring($restartStart, $cleanupStart - $restartStart)
if (-not $restart.Contains(
        'LdacNativeSetLifecycleState(context, TRUE, FALSE);') -or
    -not $restart.Contains('LdacNativeRegisterSignalingServer(context);') -or
    -not $restart.Contains(
        'LdacNativeSetLifecycleState(context, FALSE, TRUE);')) {
    throw 'PnP restart does not re-arm the server with fail-closed lifecycle state.'
}
foreach ($required in @(
        'pnpCallbacks.EvtDeviceSelfManagedIoSuspend =',
        'pnpCallbacks.EvtDeviceSelfManagedIoRestart =')) {
    if (-not $device.Contains($required)) {
        throw "The PnP callback registration is missing: $required"
    }
}
$cleanupEnd = $device.IndexOf('VOID LdacNativeEvtFileCleanup', $cleanupStart)
$cleanup = $device.Substring($cleanupStart, $cleanupEnd - $cleanupStart)
$cleanupUnregister = $cleanup.IndexOf(
    '(void)LdacNativeUnregisterSignalingServer(context, FALSE);')
$cleanupShutdown = $cleanup.IndexOf(
    'LdacNativeConnectionShutdown(context);', $cleanupUnregister)
$cleanupFlush = $cleanup.IndexOf(
    'WdfWorkItemFlush(context->SignalingServerRundownWorkItem);',
    $cleanupShutdown)
if ($cleanupUnregister -lt 0 -or
    $cleanupShutdown -le $cleanupUnregister -or
    $cleanupFlush -le $cleanupShutdown) {
    throw 'PnP cleanup does not retain an idempotent teardown fallback.'
}
$unregisterStart = $connection.IndexOf(
    'NTSTATUS LdacNativeUnregisterSignalingServer')
$diagnosticsStart = $connection.IndexOf(
    'VOID LdacNativeGetTransferDiagnostics', $unregisterStart)
$unregisterBody = $connection.Substring(
    $unregisterStart, $diagnosticsStart - $unregisterStart)
if (-not $unregisterBody.Contains('return status;') -or
    -not $unregisterBody.Contains('if (NT_SUCCESS(status)') -or
    $unregisterBody.IndexOf('Context->SignalingServerHandle = NULL;') -lt
        $unregisterBody.IndexOf('if (NT_SUCCESS(status)')) {
    throw 'A failed server unregister can discard its live server handle.'
}
foreach ($forbidden in @(
        'status == STATUS_DEVICE_NOT_CONNECTED',
        'status == STATUS_CONNECTION_DISCONNECTED')) {
    if ($unregisterBody.Contains($forbidden)) {
        throw 'An unprocessed server unregister failure is treated as successful.'
    }
}
$fileCleanupStart = $device.IndexOf('VOID LdacNativeEvtFileCleanup')
$ioctlStart = $device.IndexOf('VOID LdacNativeEvtIoDeviceControl')
$fileCleanup = $device.Substring(
    $fileCleanupStart, $ioctlStart - $fileCleanupStart)
if ($fileCleanup.Contains('UnregisterSignalingServer')) {
    throw 'Closing a user handle incorrectly unregisters the persistent server.'
}
if (-not $fileCleanup.Contains('if (fileContext->OwnsTransport)') -or
    -not $fileCleanup.Contains('LdacNativeConnectionShutdown(')) {
    throw 'File cleanup does not preserve inbound signaling for read-only handles.'
}
$claimStart = $device.IndexOf('if (fileContext != NULL &&')
$claimEnd = $device.IndexOf('if (IoControlCode == IOCTL_LDAC_NATIVE_GET_VERSION)',
    $claimStart)
if ($claimStart -lt 0 -or $claimEnd -le $claimStart) {
    throw 'The transport ownership claim block is missing.'
}
$claim = $device.Substring($claimStart, $claimEnd - $claimStart)
foreach ($required in @(
        'IOCTL_LDAC_NATIVE_OPEN_SIGNALING',
        'IOCTL_LDAC_NATIVE_CLOSE_CHANNELS',
        'IOCTL_LDAC_NATIVE_WRITE_SIGNALING',
        'IOCTL_LDAC_NATIVE_READ_SIGNALING',
        'IOCTL_LDAC_NATIVE_OPEN_MEDIA',
        'IOCTL_LDAC_NATIVE_WRITE_MEDIA',
        'fileContext->OwnsTransport = TRUE')) {
    if (-not $claim.Contains($required)) {
        throw "The transport ownership claim is missing: $required"
    }
}
if ($claim.Contains('IOCTL_LDAC_NATIVE_GET_')) {
    throw 'A read-only GET IOCTL incorrectly claims the transport connection.'
}
foreach ($consumer in @($runtime, $backend, $probe)) {
    if (-not $consumer.Contains(
            'LDAC_NATIVE_DEVICE_INFO_INBOUND_SIGNALING_READY')) {
        throw 'A transport consumer can run without the inbound signaling server.'
    }
}

$openDispatchStart = $device.IndexOf(
    '} else if (IoControlCode == IOCTL_LDAC_NATIVE_OPEN_SIGNALING) {')
$openDispatchEnd = $device.IndexOf(
    '} else if (IoControlCode == IOCTL_LDAC_NATIVE_CLOSE_CHANNELS) {',
    $openDispatchStart)
if ($openDispatchStart -lt 0 -or $openDispatchEnd -le $openDispatchStart) {
    throw 'The OPEN_SIGNALING dispatcher branch is missing.'
}
$openDispatch = $device.Substring(
    $openDispatchStart, $openDispatchEnd - $openDispatchStart)
foreach ($required in @(
        'if (status == STATUS_PENDING) return;',
        'if (NT_SUCCESS(status))',
        'information = sizeof(LDAC_NATIVE_CHANNEL_INFO);')) {
    if (-not $openDispatch.Contains($required)) {
        throw "Synchronous inbound OPEN does not return channel bytes: $required"
    }
}

Write-Host 'V1 inbound signaling policy tests passed.'
