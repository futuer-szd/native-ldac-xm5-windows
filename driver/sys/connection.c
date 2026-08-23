// SPDX-License-Identifier: MS-PL
// Derived in part from Microsoft's Windows-driver-samples BthEcho sample.
#include "connection.h"

#define LDAC_NATIVE_MIN_TIMEOUT_MS 1000u

static ULONG LdacNativeNormalizeTimeout(_In_ ULONG TimeoutMs,
                                        _In_ ULONG DefaultTimeoutMs);
static VOID LdacNativeResetSignalingLocked(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context);
static VOID LdacNativeResetMediaLocked(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context);
static VOID LdacNativeReleaseRequestBrb(_In_ WDFREQUEST Request);
static NTSTATUS LdacNativeSendBrbAsynchronously(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ WDFREQUEST Request,
    _In_ PBRB Brb,
    _In_ size_t BrbSize,
    _In_ LDAC_NATIVE_BRB_OPERATION Operation,
    _In_ ULONG TimeoutMs,
    _In_ PFN_WDF_REQUEST_COMPLETION_ROUTINE CompletionRoutine);
static EVT_WDF_REQUEST_COMPLETION_ROUTINE LdacNativeOpenSignalingCompletion;
static EVT_WDF_REQUEST_COMPLETION_ROUTINE LdacNativeOpenMediaCompletion;
static EVT_WDF_REQUEST_COMPLETION_ROUTINE LdacNativeTransferCompletion;
static EVT_WDF_REQUEST_COMPLETION_ROUTINE
    LdacNativeIncomingSignalingCompletion;
static EVT_WDF_WORKITEM LdacNativeSignalingServerRundownWorker;

#if (NTDDI_VERSION >= NTDDI_WIN8)
static VOID LdacNativeSignalingServerIndication(
    _In_opt_ PVOID CallbackContext,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS Parameters);
static NTSTATUS LdacNativeSendIncomingSignalingResponse(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ PINDICATION_PARAMETERS Parameters);
static VOID LdacNativeSignalingIndication(
    _In_opt_ PVOID CallbackContext,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS_ENHANCED Parameters);
static VOID LdacNativeMediaIndication(
    _In_opt_ PVOID CallbackContext,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS_ENHANCED Parameters);
#endif

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, LdacNativeSendBrbSynchronously)
#endif

static ULONG LdacNativeNormalizeTimeout(_In_ ULONG TimeoutMs,
                                        _In_ ULONG DefaultTimeoutMs) {
    if (TimeoutMs == 0u) return DefaultTimeoutMs;
    if (TimeoutMs < LDAC_NATIVE_MIN_TIMEOUT_MS) return LDAC_NATIVE_MIN_TIMEOUT_MS;
    if (TimeoutMs > LDAC_NATIVE_MAX_TIMEOUT_MS) return LDAC_NATIVE_MAX_TIMEOUT_MS;
    return TimeoutMs;
}

static VOID LdacNativeResetSignalingLocked(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context) {
    Context->SignalingState = LdacNativeChannelDisconnected;
    Context->SignalingChannelIsIncoming = FALSE;
    Context->SignalingChannelHandle = NULL;
    Context->SignalingIncomingMtu = 0u;
    Context->SignalingOutgoingMtu = 0u;
}

static VOID LdacNativeResetMediaLocked(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context) {
    Context->MediaState = LdacNativeChannelDisconnected;
    Context->MediaChannelHandle = NULL;
    Context->MediaIncomingMtu = 0u;
    Context->MediaOutgoingMtu = 0u;
}

NTSTATUS LdacNativeConnectionInitialize(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context) {
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_WORKITEM_CONFIG workItemConfig;
    NTSTATUS status;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Context->Device;
    status = WdfSpinLockCreate(&attributes, &Context->SignalingLock);
    if (!NT_SUCCESS(status)) return status;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Context->Device;
    status = WdfWaitLockCreate(&attributes, &Context->OperationLock);
    if (!NT_SUCCESS(status)) return status;

    WDF_WORKITEM_CONFIG_INIT(&workItemConfig,
                             LdacNativeSignalingServerRundownWorker);
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Context->Device;
    status = WdfWorkItemCreate(&workItemConfig,
                               &attributes,
                               &Context->SignalingServerRundownWorkItem);
    if (!NT_SUCCESS(status)) return status;

    Context->SignalingState = LdacNativeChannelDisconnected;
    Context->MediaState = LdacNativeChannelDisconnected;
    Context->SignalingServerRundownStatus = STATUS_SUCCESS;
    KeInitializeEvent(&Context->SignalingDisconnectedEvent,
                      NotificationEvent,
                      TRUE);
    KeInitializeEvent(&Context->SignalingOpenCompletedEvent,
                      NotificationEvent,
                      TRUE);
    KeInitializeEvent(&Context->IncomingSignalingCompletedEvent,
                      NotificationEvent,
                      TRUE);
    KeInitializeEvent(&Context->SignalingReadCompletedEvent,
                      NotificationEvent,
                      TRUE);
    KeInitializeEvent(&Context->SignalingWriteCompletedEvent,
                      NotificationEvent,
                      TRUE);
    KeInitializeEvent(&Context->MediaDisconnectedEvent,
                      NotificationEvent,
                      TRUE);
    KeInitializeEvent(&Context->MediaOpenCompletedEvent,
                      NotificationEvent,
                      TRUE);
    KeInitializeEvent(&Context->MediaWriteCompletedEvent,
                      NotificationEvent,
                      TRUE);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID LdacNativeSetLifecycleState(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ BOOLEAN PnpStarted,
    _In_ BOOLEAN ShuttingDown) {
    WdfSpinLockAcquire(Context->SignalingLock);
    Context->PnpStarted = PnpStarted;
    Context->ShuttingDown = ShuttingDown;
    WdfSpinLockRelease(Context->SignalingLock);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeRegisterSignalingServer(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context) {
#if (NTDDI_VERSION >= NTDDI_WIN8)
    struct _BRB_L2CA_REGISTER_SERVER *server;
    NTSTATUS status;

    WdfWaitLockAcquire(Context->OperationLock, NULL);
    WdfSpinLockAcquire(Context->SignalingLock);
    if (Context->SignalingServerHandle != NULL) {
        Context->SignalingServerDraining = FALSE;
        Context->SignalingServerRundownStatus = STATUS_SUCCESS;
        WdfSpinLockRelease(Context->SignalingLock);
        Context->InfoFlags |=
            LDAC_NATIVE_DEVICE_INFO_INBOUND_SIGNALING_READY;
        WdfWaitLockRelease(Context->OperationLock);
        return STATUS_SUCCESS;
    }
    WdfSpinLockRelease(Context->SignalingLock);
    server = (struct _BRB_L2CA_REGISTER_SERVER *)
        Context->ProfileInterface.BthAllocateBrb(
            BRB_L2CA_REGISTER_SERVER,
            LDAC_NATIVE_POOL_TAG);
    if (server == NULL) {
        WdfWaitLockRelease(Context->OperationLock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    server->BtAddress = Context->RemoteAddress;
    server->PSM = LDAC_NATIVE_AVDTP_PSM;
    server->IndicationFlags = 0u;
    server->IndicationCallback = LdacNativeSignalingServerIndication;
    server->IndicationCallbackContext = Context;
    server->ReferenceObject =
        WdfDeviceWdmGetDeviceObject(Context->Device);
    KeClearEvent(&Context->IncomingSignalingCompletedEvent);
    status = LdacNativeSendBrbSynchronously(
        Context, (PBRB)server, sizeof(*server),
        LDAC_NATIVE_DEFAULT_TRANSFER_TIMEOUT_MS);
    if (NT_SUCCESS(status)) {
        WdfSpinLockAcquire(Context->SignalingLock);
        Context->SignalingServerHandle = server->ServerHandle;
        Context->SignalingServerDraining = FALSE;
        Context->SignalingServerRundownStatus = STATUS_SUCCESS;
        WdfSpinLockRelease(Context->SignalingLock);
    }
    Context->ProfileInterface.BthFreeBrb((PBRB)server);
    if (NT_SUCCESS(status)) {
        Context->InfoFlags |=
            LDAC_NATIVE_DEVICE_INFO_INBOUND_SIGNALING_READY;
        WdfWaitLockRelease(Context->OperationLock);
        return STATUS_SUCCESS;
    }
    WdfSpinLockAcquire(Context->SignalingLock);
    Context->SignalingServerHandle = NULL;
    WdfSpinLockRelease(Context->SignalingLock);
    WdfWaitLockRelease(Context->OperationLock);
    return status;
#else
    UNREFERENCED_PARAMETER(Context);
    return STATUS_NOT_SUPPORTED;
#endif
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeUnregisterSignalingServer(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ BOOLEAN PreserveReadyFlag) {
#if (NTDDI_VERSION >= NTDDI_WIN8)
    struct _BRB_L2CA_UNREGISTER_SERVER *server;
    NTSTATUS status;

    WdfWaitLockAcquire(Context->OperationLock, NULL);
    WdfSpinLockAcquire(Context->SignalingLock);
    Context->SignalingServerDraining = TRUE;
    if (!PreserveReadyFlag) {
        Context->InfoFlags &=
            ~LDAC_NATIVE_DEVICE_INFO_INBOUND_SIGNALING_READY;
    }
    if (Context->SignalingServerHandle == NULL) {
        WdfSpinLockRelease(Context->SignalingLock);
        WdfWaitLockRelease(Context->OperationLock);
        return STATUS_SUCCESS;
    }
    WdfSpinLockRelease(Context->SignalingLock);

    server = (struct _BRB_L2CA_UNREGISTER_SERVER *)
        Context->ProfileInterface.BthAllocateBrb(
            BRB_L2CA_UNREGISTER_SERVER,
            LDAC_NATIVE_POOL_TAG);
    if (server == NULL) {
        WdfWaitLockRelease(Context->OperationLock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    server->BtAddress = Context->RemoteAddress;
    server->Psm = LDAC_NATIVE_AVDTP_PSM;
    WdfSpinLockAcquire(Context->SignalingLock);
    server->ServerHandle = Context->SignalingServerHandle;
    WdfSpinLockRelease(Context->SignalingLock);
    status = LdacNativeSendBrbSynchronously(
        Context, (PBRB)server, sizeof(*server),
        LDAC_NATIVE_DEFAULT_TRANSFER_TIMEOUT_MS);
    Context->ProfileInterface.BthFreeBrb((PBRB)server);
    if (NT_SUCCESS(status)) {
        WdfSpinLockAcquire(Context->SignalingLock);
        Context->SignalingServerHandle = NULL;
        WdfSpinLockRelease(Context->SignalingLock);
        WdfWaitLockRelease(Context->OperationLock);
        return STATUS_SUCCESS;
    }
    WdfWaitLockRelease(Context->OperationLock);
    return status;
#else
    UNREFERENCED_PARAMETER(Context);
    return STATUS_NOT_SUPPORTED;
#endif
}

static VOID LdacNativeQueueSignalingServerRundownLocked(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context) {
    Context->SignalingServerDraining = TRUE;
    if (!Context->SignalingServerRundownQueued) {
        Context->SignalingServerRundownQueued = TRUE;
        Context->SignalingServerRundownStatus = STATUS_PENDING;
        WdfWorkItemEnqueue(Context->SignalingServerRundownWorkItem);
    }
}

static VOID LdacNativeSignalingServerRundownWorker(
    _In_ WDFWORKITEM WorkItem) {
    PLDAC_NATIVE_DEVICE_CONTEXT context = LdacNativeGetDeviceContext(
        (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem));
    NTSTATUS status;

    status = LdacNativeUnregisterSignalingServer(context, TRUE);
    WdfSpinLockAcquire(context->SignalingLock);
    context->SignalingServerRundownStatus = status;
    context->SignalingServerRundownQueued = FALSE;
    WdfSpinLockRelease(context->SignalingLock);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID LdacNativeGetTransferDiagnostics(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _Out_ PLDAC_NATIVE_TRANSFER_DIAGNOSTICS Diagnostics) {
    RtlZeroMemory(Diagnostics, sizeof(*Diagnostics));
    Diagnostics->Size = sizeof(*Diagnostics);
    WdfSpinLockAcquire(Context->SignalingLock);
    Diagnostics->Read = Context->SignalingReadDiagnostics;
    Diagnostics->Write = Context->SignalingWriteDiagnostics;
    Diagnostics->MediaWrite = Context->MediaWriteDiagnostics;
    WdfSpinLockRelease(Context->SignalingLock);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID LdacNativeGetOpenDiagnostics(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _Out_ PLDAC_NATIVE_OPEN_DIAGNOSTICS Diagnostics) {
    WdfSpinLockAcquire(Context->SignalingLock);
    *Diagnostics = Context->OpenDiagnostics;
    WdfSpinLockRelease(Context->SignalingLock);
    Diagnostics->Size = sizeof(*Diagnostics);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeSendBrbSynchronously(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ PBRB Brb,
    _In_ size_t BrbSize,
    _In_ ULONG TimeoutMs) {
    NTSTATUS status;
    WDFMEMORY brbMemory;
    WDF_OBJECT_ATTRIBUTES memoryAttributes;
    WDF_REQUEST_REUSE_PARAMS reuse;
    WDF_REQUEST_SEND_OPTIONS options;

    PAGED_CODE();
    if (Brb == NULL || BrbSize == 0u) return STATUS_INVALID_PARAMETER;

    WDF_REQUEST_REUSE_PARAMS_INIT(&reuse,
                                  WDF_REQUEST_REUSE_NO_FLAGS,
                                  STATUS_NOT_SUPPORTED);
    status = WdfRequestReuse(Context->InitializationRequest, &reuse);
    if (!NT_SUCCESS(status)) return status;

    WDF_OBJECT_ATTRIBUTES_INIT(&memoryAttributes);
    memoryAttributes.ParentObject = Context->InitializationRequest;
    status = WdfMemoryCreatePreallocated(&memoryAttributes,
                                         Brb,
                                         BrbSize,
                                         &brbMemory);
    if (!NT_SUCCESS(status)) return status;

    status = WdfIoTargetFormatRequestForInternalIoctlOthers(
        Context->IoTarget,
        Context->InitializationRequest,
        IOCTL_INTERNAL_BTH_SUBMIT_BRB,
        brbMemory,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL);
    if (NT_SUCCESS(status)) {
        WDF_REQUEST_SEND_OPTIONS_INIT(
            &options,
            WDF_REQUEST_SEND_OPTION_SYNCHRONOUS |
            WDF_REQUEST_SEND_OPTION_TIMEOUT);
        WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(
            &options,
            WDF_REL_TIMEOUT_IN_MS(LdacNativeNormalizeTimeout(
                TimeoutMs,
                LDAC_NATIVE_DEFAULT_TRANSFER_TIMEOUT_MS)));
        if (!WdfRequestSend(Context->InitializationRequest,
                            Context->IoTarget,
                            &options)) {
            status = WdfRequestGetStatus(Context->InitializationRequest);
        } else {
            status = WdfRequestGetStatus(Context->InitializationRequest);
        }
    }
    WdfObjectDelete(brbMemory);
    return status;
}

static VOID LdacNativeReleaseRequestBrb(_In_ WDFREQUEST Request) {
    PLDAC_NATIVE_BRB_REQUEST_CONTEXT requestContext =
        LdacNativeGetBrbRequestContext(Request);

    if (requestContext->BrbMemory != NULL) {
        WdfObjectDelete(requestContext->BrbMemory);
        requestContext->BrbMemory = NULL;
    }
    if (requestContext->TransferMemory != NULL) {
        WdfObjectDelete(requestContext->TransferMemory);
        requestContext->TransferMemory = NULL;
    }
    if (requestContext->Brb != NULL) {
        requestContext->DeviceContext->ProfileInterface.BthFreeBrb(
            requestContext->Brb);
        requestContext->Brb = NULL;
    }
    requestContext->Operation = LdacNativeBrbOperationNone;
    requestContext->OutputBuffer = NULL;
    requestContext->OutputBufferLength = 0u;
}

static NTSTATUS LdacNativeSendBrbAsynchronously(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ WDFREQUEST Request,
    _In_ PBRB Brb,
    _In_ size_t BrbSize,
    _In_ LDAC_NATIVE_BRB_OPERATION Operation,
    _In_ ULONG TimeoutMs,
    _In_ PFN_WDF_REQUEST_COMPLETION_ROUTINE CompletionRoutine) {
    PLDAC_NATIVE_BRB_REQUEST_CONTEXT requestContext;
    WDF_OBJECT_ATTRIBUTES memoryAttributes;
    WDF_REQUEST_SEND_OPTIONS options;
    NTSTATUS status;

    if (Brb == NULL || BrbSize == 0u || CompletionRoutine == NULL) {
        if (Brb != NULL) Context->ProfileInterface.BthFreeBrb(Brb);
        return STATUS_INVALID_PARAMETER;
    }

    requestContext = LdacNativeGetBrbRequestContext(Request);
    if (requestContext->Brb != NULL || requestContext->BrbMemory != NULL) {
        Context->ProfileInterface.BthFreeBrb(Brb);
        return STATUS_INVALID_DEVICE_STATE;
    }
    requestContext->DeviceContext = Context;
    requestContext->Brb = Brb;
    requestContext->Operation = Operation;

    WDF_OBJECT_ATTRIBUTES_INIT(&memoryAttributes);
    memoryAttributes.ParentObject = Request;
    status = WdfMemoryCreatePreallocated(&memoryAttributes,
                                         Brb,
                                         BrbSize,
                                         &requestContext->BrbMemory);
    if (!NT_SUCCESS(status)) goto fail;

    status = WdfIoTargetFormatRequestForInternalIoctlOthers(
        Context->IoTarget,
        Request,
        IOCTL_INTERNAL_BTH_SUBMIT_BRB,
        requestContext->BrbMemory,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL);
    if (!NT_SUCCESS(status)) goto fail;

    WdfRequestSetCompletionRoutine(Request, CompletionRoutine, requestContext);
    WDF_REQUEST_SEND_OPTIONS_INIT(
        &options,
        TimeoutMs == 0u ? 0u : WDF_REQUEST_SEND_OPTION_TIMEOUT);
    if (TimeoutMs != 0u) {
        WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(
            &options,
            WDF_REL_TIMEOUT_IN_MS(TimeoutMs));
    }
    if (!WdfRequestSend(Request, Context->IoTarget, &options)) {
        status = WdfRequestGetStatus(Request);
        goto fail;
    }
    return STATUS_PENDING;

fail:
    LdacNativeReleaseRequestBrb(Request);
    return status;
}

#if (NTDDI_VERSION >= NTDDI_WIN8)
static VOID LdacNativeDestroyIncomingRequest(
    _In_ WDFREQUEST Request,
    _In_ PLDAC_NATIVE_INCOMING_REQUEST_CONTEXT RequestContext) {
    if (RequestContext->BrbMemory != NULL) {
        WdfObjectDelete(RequestContext->BrbMemory);
        RequestContext->BrbMemory = NULL;
    }
    if (RequestContext->Brb != NULL) {
        RequestContext->DeviceContext->ProfileInterface.BthFreeBrb(
            RequestContext->Brb);
        RequestContext->Brb = NULL;
    }
    WdfObjectDelete(Request);
}

static VOID LdacNativeIncomingSignalingCompletion(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_ WDFCONTEXT CompletionContext) {
    PLDAC_NATIVE_INCOMING_REQUEST_CONTEXT requestContext =
        (PLDAC_NATIVE_INCOMING_REQUEST_CONTEXT)CompletionContext;
    PLDAC_NATIVE_DEVICE_CONTEXT context = requestContext->DeviceContext;
    struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL *brb =
        (struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL *)requestContext->Brb;
    NTSTATUS status = Params->IoStatus.Status;
    BOOLEAN tracked = FALSE;

    UNREFERENCED_PARAMETER(Target);
    if (requestContext->Accepted) {
        WdfSpinLockAcquire(context->SignalingLock);
        if (context->IncomingSignalingRequest == Request) {
            context->IncomingSignalingRequest = NULL;
            tracked = TRUE;
            context->OpenDiagnostics.IoStatus = (LONG)status;
            context->OpenDiagnostics.BrbStatus = (LONG)brb->Hdr.Status;
            context->OpenDiagnostics.BtStatus = (ULONG)brb->Hdr.BtStatus;
            context->OpenDiagnostics.Flags |=
                LDAC_NATIVE_OPEN_DIAGNOSTIC_COMPLETED;
            if (NT_SUCCESS(status) &&
                context->SignalingState == LdacNativeChannelConnecting) {
                context->SignalingChannelHandle = brb->ChannelHandle;
                context->SignalingIncomingMtu = brb->InResults.Params.Mtu;
                context->SignalingOutgoingMtu = brb->OutResults.Params.Mtu;
                context->SignalingChannelIsIncoming = TRUE;
                context->SignalingState = LdacNativeChannelConnected;
                context->OpenDiagnostics.Flags |=
                    LDAC_NATIVE_OPEN_DIAGNOSTIC_SUCCEEDED;
                LdacNativeQueueSignalingServerRundownLocked(context);
            } else if (context->SignalingState ==
                       LdacNativeChannelConnecting) {
                LdacNativeResetSignalingLocked(context);
            }
        }
        WdfSpinLockRelease(context->SignalingLock);
        if (tracked) {
            KeSetEvent(&context->IncomingSignalingCompletedEvent,
                       IO_NO_INCREMENT,
                       FALSE);
            KeSetEvent(&context->SignalingOpenCompletedEvent,
                       IO_NO_INCREMENT,
                       FALSE);
        }
    }
    LdacNativeDestroyIncomingRequest(Request, requestContext);
}

static NTSTATUS LdacNativeSendIncomingSignalingResponse(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ PINDICATION_PARAMETERS Parameters) {
    WDF_OBJECT_ATTRIBUTES requestAttributes;
    WDF_OBJECT_ATTRIBUTES memoryAttributes;
    WDFREQUEST request;
    PLDAC_NATIVE_INCOMING_REQUEST_CONTEXT requestContext;
    struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL *brb;
    NTSTATUS status;
    BOOLEAN accepted = FALSE;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &requestAttributes,
        LDAC_NATIVE_INCOMING_REQUEST_CONTEXT);
    status = WdfRequestCreate(
        &requestAttributes, Context->IoTarget, &request);
    if (!NT_SUCCESS(status)) return status;
    requestContext = LdacNativeGetIncomingRequestContext(request);
    RtlZeroMemory(requestContext, sizeof(*requestContext));
    requestContext->DeviceContext = Context;

    brb = (struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL *)
        Context->ProfileInterface.BthAllocateBrb(
            BRB_L2CA_OPEN_ENHANCED_CHANNEL_RESPONSE,
            LDAC_NATIVE_POOL_TAG);
    if (brb == NULL) {
        WdfObjectDelete(request);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    requestContext->Brb = (PBRB)brb;

    WdfSpinLockAcquire(Context->SignalingLock);
    if (!Context->ShuttingDown &&
        !Context->SignalingServerDraining &&
        Parameters->BtAddress == Context->RemoteAddress &&
        Parameters->Parameters.Connect.Request.PSM ==
            LDAC_NATIVE_AVDTP_PSM &&
        Context->SignalingState == LdacNativeChannelDisconnected &&
        Context->IncomingSignalingRequest == NULL) {
        accepted = TRUE;
        requestContext->Accepted = TRUE;
        Context->SignalingState = LdacNativeChannelConnecting;
        Context->SignalingChannelIsIncoming = TRUE;
        Context->IncomingSignalingRequest = request;
        KeClearEvent(&Context->IncomingSignalingCompletedEvent);
        KeClearEvent(&Context->SignalingDisconnectedEvent);
        KeClearEvent(&Context->SignalingOpenCompletedEvent);
        {
            ULONG sequence = Context->OpenDiagnostics.Sequence + 1u;
            RtlZeroMemory(&Context->OpenDiagnostics,
                          sizeof(Context->OpenDiagnostics));
            Context->OpenDiagnostics.Size =
                sizeof(Context->OpenDiagnostics);
            Context->OpenDiagnostics.Sequence = sequence;
            Context->OpenDiagnostics.Operation =
                LDAC_NATIVE_OPEN_OPERATION_SIGNALING;
            Context->OpenDiagnostics.IoStatus = (LONG)STATUS_PENDING;
            Context->OpenDiagnostics.BrbStatus = (LONG)STATUS_PENDING;
            Context->OpenDiagnostics.RemoteBluetoothAddress =
                Context->RemoteAddress;
            Context->OpenDiagnostics.ChannelFlags =
                CF_ROLE_EITHER | CF_LINK_AUTHENTICATED |
                CF_LINK_ENCRYPTED;
            Context->OpenDiagnostics.Flags =
                LDAC_NATIVE_OPEN_DIAGNOSTIC_ATTEMPTED |
                LDAC_NATIVE_OPEN_DIAGNOSTIC_INBOUND_CHANNEL;
            Context->OpenDiagnostics.Psm = LDAC_NATIVE_AVDTP_PSM;
        }
    }
    WdfSpinLockRelease(Context->SignalingLock);

    brb->BtAddress = Parameters->BtAddress;
    brb->Psm = Parameters->Parameters.Connect.Request.PSM;
    brb->ChannelHandle = Parameters->ConnectionHandle;
    brb->Response = accepted
        ? CONNECT_RSP_RESULT_SUCCESS
        : CONNECT_RSP_RESULT_NO_RESOURCES;
    brb->ResponseStatus = 0u;
    brb->ChannelFlags = CF_ROLE_EITHER |
                        CF_LINK_AUTHENTICATED |
                        CF_LINK_ENCRYPTED;
    brb->ConfigOut.Flags = CFG_MTU;
    brb->ConfigOut.Mtu.Min = L2CAP_MIN_MTU;
    brb->ConfigOut.Mtu.Max = L2CAP_DEFAULT_MTU;
    brb->ConfigOut.Mtu.Preferred = L2CAP_DEFAULT_MTU;
    brb->ConfigIn.Flags = CFG_MTU;
    brb->ConfigIn.Mtu.Min = L2CAP_MIN_MTU;
    brb->ConfigIn.Mtu.Max = L2CAP_DEFAULT_MTU;
    brb->ConfigIn.Mtu.Preferred = L2CAP_DEFAULT_MTU;
    brb->CallbackFlags = CALLBACK_DISCONNECT;
    brb->Callback = LdacNativeSignalingIndication;
    brb->CallbackContext = Context;
    brb->ReferenceObject =
        WdfDeviceWdmGetDeviceObject(Context->Device);
    brb->IncomingQueueDepth = 8u;

    WDF_OBJECT_ATTRIBUTES_INIT(&memoryAttributes);
    memoryAttributes.ParentObject = request;
    status = WdfMemoryCreatePreallocated(
        &memoryAttributes,
        brb,
        sizeof(*brb),
        &requestContext->BrbMemory);
    if (NT_SUCCESS(status)) {
        status = WdfIoTargetFormatRequestForInternalIoctlOthers(
            Context->IoTarget,
            request,
            IOCTL_INTERNAL_BTH_SUBMIT_BRB,
            requestContext->BrbMemory,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL);
    }
    if (NT_SUCCESS(status)) {
        WdfRequestSetCompletionRoutine(
            request,
            LdacNativeIncomingSignalingCompletion,
            requestContext);
        if (WdfRequestSend(request, Context->IoTarget, NULL)) {
            return STATUS_PENDING;
        }
        status = WdfRequestGetStatus(request);
    }

    if (accepted) {
        WdfSpinLockAcquire(Context->SignalingLock);
        if (Context->IncomingSignalingRequest == request) {
            Context->IncomingSignalingRequest = NULL;
            if (Context->SignalingState == LdacNativeChannelConnecting) {
                LdacNativeResetSignalingLocked(Context);
            }
        }
        WdfSpinLockRelease(Context->SignalingLock);
        KeSetEvent(&Context->IncomingSignalingCompletedEvent,
                   IO_NO_INCREMENT,
                   FALSE);
        KeSetEvent(&Context->SignalingOpenCompletedEvent,
                   IO_NO_INCREMENT,
                   FALSE);
    }
    LdacNativeDestroyIncomingRequest(request, requestContext);
    return status;
}

static VOID LdacNativeSignalingServerIndication(
    _In_opt_ PVOID CallbackContext,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS Parameters) {
    PLDAC_NATIVE_DEVICE_CONTEXT context =
        (PLDAC_NATIVE_DEVICE_CONTEXT)CallbackContext;

    if (context == NULL) return;
    if (Indication == IndicationAddReference) {
        WdfObjectReference(context->Device);
        return;
    }
    if (Indication == IndicationReleaseReference) {
        WdfObjectDereference(context->Device);
        return;
    }
    if (Parameters == NULL || Indication != IndicationRemoteConnect) return;
    (void)LdacNativeSendIncomingSignalingResponse(context, Parameters);
}

static VOID LdacNativeSignalingIndication(
    _In_opt_ PVOID CallbackContext,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS_ENHANCED Parameters) {
    PLDAC_NATIVE_DEVICE_CONTEXT context =
        (PLDAC_NATIVE_DEVICE_CONTEXT)CallbackContext;

    UNREFERENCED_PARAMETER(Parameters);
    if (context == NULL) return;
    if (Indication == IndicationAddReference) {
        WdfObjectReference(context->Device);
        return;
    }
    if (Indication == IndicationReleaseReference) {
        WdfObjectDereference(context->Device);
        return;
    }
    if (Indication != IndicationRemoteDisconnect) return;

    WdfSpinLockAcquire(context->SignalingLock);
    LdacNativeResetSignalingLocked(context);
    WdfSpinLockRelease(context->SignalingLock);
    KeSetEvent(&context->SignalingDisconnectedEvent, IO_NO_INCREMENT, FALSE);
}

static VOID LdacNativeMediaIndication(
    _In_opt_ PVOID CallbackContext,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS_ENHANCED Parameters) {
    PLDAC_NATIVE_DEVICE_CONTEXT context =
        (PLDAC_NATIVE_DEVICE_CONTEXT)CallbackContext;

    UNREFERENCED_PARAMETER(Parameters);
    if (context == NULL) return;
    if (Indication == IndicationAddReference) {
        WdfObjectReference(context->Device);
        return;
    }
    if (Indication == IndicationReleaseReference) {
        WdfObjectDereference(context->Device);
        return;
    }
    if (Indication != IndicationRemoteDisconnect) return;

    WdfSpinLockAcquire(context->SignalingLock);
    LdacNativeResetMediaLocked(context);
    WdfSpinLockRelease(context->SignalingLock);
    KeSetEvent(&context->MediaDisconnectedEvent, IO_NO_INCREMENT, FALSE);
}
#endif

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeOpenSignaling(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength) {
#if (NTDDI_VERSION >= NTDDI_WIN8)
    LDAC_NATIVE_OPEN_SIGNALING_REQUEST defaults = {
        sizeof(LDAC_NATIVE_OPEN_SIGNALING_REQUEST),
        LDAC_NATIVE_DEFAULT_OPEN_TIMEOUT_MS,
        L2CAP_DEFAULT_MTU,
        0u
    };
    PLDAC_NATIVE_OPEN_SIGNALING_REQUEST input = &defaults;
    PLDAC_NATIVE_CHANNEL_INFO output;
    struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL *brb;
    ULONG timeoutMs;
    USHORT preferredMtu;
    NTSTATUS status;
    LARGE_INTEGER incomingWait;
    BOOLEAN releaseOpenReference = FALSE;
    BOOLEAN waitForIncoming = FALSE;
    BOOLEAN registerIncomingServer = FALSE;
    BOOLEAN waitForServerRundown = FALSE;

    if (OutputBufferLength < sizeof(*output)) return STATUS_BUFFER_TOO_SMALL;
    if (InputBufferLength != 0u) {
        status = WdfRequestRetrieveInputBuffer(Request,
                                               sizeof(*input),
                                               (PVOID *)&input,
                                               NULL);
        if (!NT_SUCCESS(status)) return status;
        if (input->Size != sizeof(*input)) return STATUS_INFO_LENGTH_MISMATCH;
    }
    status = WdfRequestRetrieveOutputBuffer(Request,
                                            sizeof(*output),
                                            (PVOID *)&output,
                                            NULL);
    if (!NT_SUCCESS(status)) return status;

    timeoutMs = LdacNativeNormalizeTimeout(
        input->TimeoutMs,
        LDAC_NATIVE_DEFAULT_OPEN_TIMEOUT_MS);
    preferredMtu = input->PreferredMtu == 0u
        ? (USHORT)L2CAP_DEFAULT_MTU
        : input->PreferredMtu;
    if (preferredMtu < L2CAP_MIN_MTU ||
        preferredMtu > LDAC_NATIVE_MAX_SIGNALING_TRANSFER) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((Context->InfoFlags &
         (LDAC_NATIVE_DEVICE_INFO_PROFILE_READY |
          LDAC_NATIVE_DEVICE_INFO_REMOTE_READY |
          LDAC_NATIVE_DEVICE_INFO_INBOUND_SIGNALING_READY)) !=
        (LDAC_NATIVE_DEVICE_INFO_PROFILE_READY |
         LDAC_NATIVE_DEVICE_INFO_REMOTE_READY |
         LDAC_NATIVE_DEVICE_INFO_INBOUND_SIGNALING_READY)) {
        return STATUS_DEVICE_NOT_READY;
    }

    WdfSpinLockAcquire(Context->SignalingLock);
    waitForServerRundown = Context->SignalingServerRundownQueued;
    WdfSpinLockRelease(Context->SignalingLock);
    if (waitForServerRundown) {
        WdfWorkItemFlush(Context->SignalingServerRundownWorkItem);
        WdfSpinLockAcquire(Context->SignalingLock);
        status = Context->SignalingServerRundownStatus;
        WdfSpinLockRelease(Context->SignalingLock);
        if (!NT_SUCCESS(status)) return status;
    }

    WdfSpinLockAcquire(Context->SignalingLock);
    registerIncomingServer = Context->PnpStarted &&
        !Context->ShuttingDown &&
        Context->SignalingState == LdacNativeChannelDisconnected &&
        Context->SignalingServerHandle == NULL;
    WdfSpinLockRelease(Context->SignalingLock);
    if (registerIncomingServer) {
        status = LdacNativeRegisterSignalingServer(Context);
        if (!NT_SUCCESS(status)) return status;
    }

    WdfObjectReference(Request);
    WdfSpinLockAcquire(Context->SignalingLock);
    if (Context->ShuttingDown) {
        WdfSpinLockRelease(Context->SignalingLock);
        WdfObjectDereference(Request);
        return STATUS_DEVICE_NOT_READY;
    }
    if (Context->SignalingState == LdacNativeChannelConnected) {
        BOOLEAN serverRundownQueued =
            Context->SignalingServerRundownQueued;
        WdfSpinLockRelease(Context->SignalingLock);
        if (serverRundownQueued) {
            WdfWorkItemFlush(Context->SignalingServerRundownWorkItem);
        }
        WdfSpinLockAcquire(Context->SignalingLock);
        status = Context->SignalingServerRundownStatus;
        if (!NT_SUCCESS(status) ||
            Context->SignalingState != LdacNativeChannelConnected) {
            WdfSpinLockRelease(Context->SignalingLock);
            WdfObjectDereference(Request);
            return NT_SUCCESS(status)
                ? STATUS_CONNECTION_DISCONNECTED
                : status;
        }
        RtlZeroMemory(output, sizeof(*output));
        output->Size = sizeof(*output);
        output->State = LDAC_NATIVE_CHANNEL_CONNECTED;
        output->Psm = LDAC_NATIVE_AVDTP_PSM;
        output->IncomingMtu = Context->SignalingIncomingMtu;
        output->OutgoingMtu = Context->SignalingOutgoingMtu;
        WdfSpinLockRelease(Context->SignalingLock);
        WdfObjectDereference(Request);
        return STATUS_SUCCESS;
    }
    if ((Context->SignalingState == LdacNativeChannelConnecting &&
         Context->IncomingSignalingRequest != NULL) ||
        (Context->SignalingState == LdacNativeChannelDisconnected &&
         Context->SignalingServerHandle != NULL &&
         !Context->SignalingServerDraining)) {
        waitForIncoming = TRUE;
    }
    if (waitForIncoming) {
        WdfSpinLockRelease(Context->SignalingLock);
        incomingWait.QuadPart =
            -((LONGLONG)timeoutMs * 10 * 1000);
        status = KeWaitForSingleObject(
            &Context->IncomingSignalingCompletedEvent,
            Executive,
            KernelMode,
            FALSE,
            &incomingWait);
        WdfSpinLockAcquire(Context->SignalingLock);
        if (Context->ShuttingDown) {
            WdfSpinLockRelease(Context->SignalingLock);
            WdfObjectDereference(Request);
            return STATUS_DEVICE_NOT_READY;
        }
        if (Context->SignalingState == LdacNativeChannelConnected) {
            BOOLEAN serverRundownQueued =
                Context->SignalingServerRundownQueued;
            WdfSpinLockRelease(Context->SignalingLock);
            if (serverRundownQueued) {
                WdfWorkItemFlush(Context->SignalingServerRundownWorkItem);
            }
            WdfSpinLockAcquire(Context->SignalingLock);
            status = Context->SignalingServerRundownStatus;
            if (!NT_SUCCESS(status) ||
                Context->SignalingState != LdacNativeChannelConnected) {
                WdfSpinLockRelease(Context->SignalingLock);
                WdfObjectDereference(Request);
                return NT_SUCCESS(status)
                    ? STATUS_CONNECTION_DISCONNECTED
                    : status;
            }
            RtlZeroMemory(output, sizeof(*output));
            output->Size = sizeof(*output);
            output->State = LDAC_NATIVE_CHANNEL_CONNECTED;
            output->Psm = LDAC_NATIVE_AVDTP_PSM;
            output->IncomingMtu = Context->SignalingIncomingMtu;
            output->OutgoingMtu = Context->SignalingOutgoingMtu;
            WdfSpinLockRelease(Context->SignalingLock);
            WdfObjectDereference(Request);
            return STATUS_SUCCESS;
        }
        if (status == STATUS_TIMEOUT) {
            WdfSpinLockRelease(Context->SignalingLock);
            WdfObjectDereference(Request);
            return STATUS_IO_TIMEOUT;
        }
        WdfSpinLockRelease(Context->SignalingLock);
        WdfObjectDereference(Request);
        return STATUS_CONNECTION_DISCONNECTED;
    }
    if (Context->SignalingServerDraining) {
        WdfSpinLockRelease(Context->SignalingLock);
        WdfObjectDereference(Request);
        return STATUS_DEVICE_BUSY;
    }
    if (Context->SignalingState != LdacNativeChannelDisconnected) {
        WdfSpinLockRelease(Context->SignalingLock);
        WdfObjectDereference(Request);
        return STATUS_DEVICE_BUSY;
    }
    Context->SignalingState = LdacNativeChannelConnecting;
    Context->SignalingChannelIsIncoming = FALSE;
    Context->SignalingOpenRequest = Request;
    {
        ULONG sequence = Context->OpenDiagnostics.Sequence + 1u;
        RtlZeroMemory(&Context->OpenDiagnostics,
                      sizeof(Context->OpenDiagnostics));
        Context->OpenDiagnostics.Size = sizeof(Context->OpenDiagnostics);
        Context->OpenDiagnostics.Sequence = sequence;
        Context->OpenDiagnostics.Operation =
            LDAC_NATIVE_OPEN_OPERATION_SIGNALING;
        Context->OpenDiagnostics.IoStatus = (LONG)STATUS_PENDING;
        Context->OpenDiagnostics.BrbStatus = (LONG)STATUS_PENDING;
        Context->OpenDiagnostics.RemoteBluetoothAddress =
            Context->RemoteAddress;
        Context->OpenDiagnostics.ChannelFlags =
            CF_ROLE_EITHER | CF_LINK_AUTHENTICATED | CF_LINK_ENCRYPTED;
        Context->OpenDiagnostics.Flags =
            LDAC_NATIVE_OPEN_DIAGNOSTIC_ATTEMPTED;
        Context->OpenDiagnostics.Psm = LDAC_NATIVE_AVDTP_PSM;
    }
    KeClearEvent(&Context->SignalingDisconnectedEvent);
    KeClearEvent(&Context->SignalingOpenCompletedEvent);
    WdfSpinLockRelease(Context->SignalingLock);

    brb = (struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL *)
        Context->ProfileInterface.BthAllocateBrb(
            BRB_L2CA_OPEN_ENHANCED_CHANNEL,
            LDAC_NATIVE_POOL_TAG);
    if (brb == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto fail;
    }

    brb->BtAddress = Context->RemoteAddress;
    brb->Psm = LDAC_NATIVE_AVDTP_PSM;
    brb->ChannelFlags = CF_ROLE_EITHER |
                        CF_LINK_AUTHENTICATED |
                        CF_LINK_ENCRYPTED;
    brb->ConfigOut.Flags = CFG_MTU;
    brb->ConfigOut.Mtu.Min = L2CAP_MIN_MTU;
    brb->ConfigOut.Mtu.Max = preferredMtu;
    brb->ConfigOut.Mtu.Preferred = preferredMtu;
    brb->ConfigIn.Flags = CFG_MTU;
    brb->ConfigIn.Mtu.Min = L2CAP_MIN_MTU;
    brb->ConfigIn.Mtu.Max = preferredMtu;
    brb->ConfigIn.Mtu.Preferred = preferredMtu;
    brb->CallbackFlags = CALLBACK_DISCONNECT;
    brb->Callback = LdacNativeSignalingIndication;
    brb->CallbackContext = Context;
    brb->ReferenceObject = WdfDeviceWdmGetDeviceObject(Context->Device);
    brb->IncomingQueueDepth = 8u;

    {
        PLDAC_NATIVE_BRB_REQUEST_CONTEXT requestContext =
            LdacNativeGetBrbRequestContext(Request);
        requestContext->OutputBuffer = output;
        requestContext->OutputBufferLength = OutputBufferLength;
    }
    status = LdacNativeSendBrbAsynchronously(
        Context,
        Request,
        (PBRB)brb,
        sizeof(*brb),
        LdacNativeBrbOperationOpenSignaling,
        timeoutMs,
        LdacNativeOpenSignalingCompletion);
    if (status == STATUS_PENDING) return status;

fail:
    WdfSpinLockAcquire(Context->SignalingLock);
    Context->OpenDiagnostics.IoStatus = (LONG)status;
    Context->OpenDiagnostics.Flags |=
        LDAC_NATIVE_OPEN_DIAGNOSTIC_COMPLETED;
    if (Context->SignalingOpenRequest == Request) {
        Context->SignalingOpenRequest = NULL;
        releaseOpenReference = TRUE;
    }
    LdacNativeResetSignalingLocked(Context);
    WdfSpinLockRelease(Context->SignalingLock);
    KeSetEvent(&Context->SignalingDisconnectedEvent, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&Context->SignalingOpenCompletedEvent, IO_NO_INCREMENT, FALSE);
    if (releaseOpenReference) WdfObjectDereference(Request);
    return status;
#else
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Request);
    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    return STATUS_NOT_SUPPORTED;
#endif
}

static VOID LdacNativeOpenSignalingCompletion(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_ WDFCONTEXT CompletionContext) {
#if (NTDDI_VERSION >= NTDDI_WIN8)
    PLDAC_NATIVE_BRB_REQUEST_CONTEXT requestContext =
        (PLDAC_NATIVE_BRB_REQUEST_CONTEXT)CompletionContext;
    PLDAC_NATIVE_DEVICE_CONTEXT context = requestContext->DeviceContext;
    struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL *brb =
        (struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL *)requestContext->Brb;
    PLDAC_NATIVE_CHANNEL_INFO output =
        (PLDAC_NATIVE_CHANNEL_INFO)requestContext->OutputBuffer;
    NTSTATUS status = Params->IoStatus.Status;
    size_t information = 0u;
    USHORT incomingMtu = 0u;
    USHORT outgoingMtu = 0u;
    BOOLEAN releaseOpenReference = FALSE;

    UNREFERENCED_PARAMETER(Target);
    if (NT_SUCCESS(status)) {
        WdfSpinLockAcquire(context->SignalingLock);
        if (context->SignalingOpenRequest == Request) {
            context->SignalingOpenRequest = NULL;
            releaseOpenReference = TRUE;
        }
        if (context->SignalingState == LdacNativeChannelConnecting) {
            context->SignalingChannelHandle = brb->ChannelHandle;
            context->SignalingIncomingMtu = brb->InResults.Params.Mtu;
            context->SignalingOutgoingMtu = brb->OutResults.Params.Mtu;
            context->SignalingState = LdacNativeChannelConnected;
            incomingMtu = context->SignalingIncomingMtu;
            outgoingMtu = context->SignalingOutgoingMtu;
        } else {
            status = STATUS_CANCELLED;
        }
        WdfSpinLockRelease(context->SignalingLock);
    } else {
        WdfSpinLockAcquire(context->SignalingLock);
        if (context->SignalingOpenRequest == Request) {
            context->SignalingOpenRequest = NULL;
            releaseOpenReference = TRUE;
        }
        WdfSpinLockRelease(context->SignalingLock);
    }

    WdfSpinLockAcquire(context->SignalingLock);
    context->OpenDiagnostics.IoStatus = (LONG)status;
    context->OpenDiagnostics.BrbStatus = (LONG)brb->Hdr.Status;
    context->OpenDiagnostics.BtStatus = (ULONG)brb->Hdr.BtStatus;
    context->OpenDiagnostics.Response = brb->Response;
    context->OpenDiagnostics.ResponseStatus = brb->ResponseStatus;
    context->OpenDiagnostics.Flags |=
        LDAC_NATIVE_OPEN_DIAGNOSTIC_COMPLETED;
    if (NT_SUCCESS(status)) {
        context->OpenDiagnostics.Flags |=
            LDAC_NATIVE_OPEN_DIAGNOSTIC_SUCCEEDED;
    } else if (status == STATUS_REQUEST_NOT_ACCEPTED) {
        context->OpenDiagnostics.Flags |=
            LDAC_NATIVE_OPEN_DIAGNOSTIC_REMOTE_RESPONSE_VALID;
    }
    WdfSpinLockRelease(context->SignalingLock);

    if (NT_SUCCESS(status) && output != NULL &&
        requestContext->OutputBufferLength >= sizeof(*output)) {
        RtlZeroMemory(output, sizeof(*output));
        output->Size = sizeof(*output);
        output->State = LDAC_NATIVE_CHANNEL_CONNECTED;
        output->Psm = LDAC_NATIVE_AVDTP_PSM;
        output->IncomingMtu = incomingMtu;
        output->OutgoingMtu = outgoingMtu;
        information = sizeof(*output);
    } else {
        WdfSpinLockAcquire(context->SignalingLock);
        LdacNativeResetSignalingLocked(context);
        WdfSpinLockRelease(context->SignalingLock);
        KeSetEvent(&context->SignalingDisconnectedEvent,
                   IO_NO_INCREMENT,
                   FALSE);
    }

    LdacNativeReleaseRequestBrb(Request);
    if (releaseOpenReference) WdfObjectDereference(Request);
    WdfRequestCompleteWithInformation(Request, status, information);
    KeSetEvent(&context->SignalingOpenCompletedEvent,
               IO_NO_INCREMENT,
               FALSE);
#else
    UNREFERENCED_PARAMETER(Request);
    UNREFERENCED_PARAMETER(Target);
    UNREFERENCED_PARAMETER(Params);
    UNREFERENCED_PARAMETER(CompletionContext);
#endif
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeOpenMedia(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength) {
#if (NTDDI_VERSION >= NTDDI_WIN8)
    LDAC_NATIVE_OPEN_SIGNALING_REQUEST defaults = {
        sizeof(LDAC_NATIVE_OPEN_SIGNALING_REQUEST),
        LDAC_NATIVE_DEFAULT_OPEN_TIMEOUT_MS,
        L2CAP_DEFAULT_MTU,
        0u
    };
    PLDAC_NATIVE_OPEN_SIGNALING_REQUEST input = &defaults;
    PLDAC_NATIVE_CHANNEL_INFO output;
    struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL *brb;
    ULONG timeoutMs;
    USHORT preferredMtu;
    NTSTATUS status;
    BOOLEAN releaseOpenReference = FALSE;

    if (OutputBufferLength < sizeof(*output)) return STATUS_BUFFER_TOO_SMALL;
    if (InputBufferLength != 0u) {
        status = WdfRequestRetrieveInputBuffer(Request,
                                               sizeof(*input),
                                               (PVOID *)&input,
                                               NULL);
        if (!NT_SUCCESS(status)) return status;
        if (input->Size != sizeof(*input)) return STATUS_INFO_LENGTH_MISMATCH;
    }
    status = WdfRequestRetrieveOutputBuffer(Request,
                                            sizeof(*output),
                                            (PVOID *)&output,
                                            NULL);
    if (!NT_SUCCESS(status)) return status;

    timeoutMs = LdacNativeNormalizeTimeout(
        input->TimeoutMs,
        LDAC_NATIVE_DEFAULT_OPEN_TIMEOUT_MS);
    preferredMtu = input->PreferredMtu == 0u
        ? (USHORT)L2CAP_DEFAULT_MTU
        : input->PreferredMtu;
    if (preferredMtu < L2CAP_MIN_MTU ||
        preferredMtu > LDAC_NATIVE_MAX_MEDIA_TRANSFER) {
        return STATUS_INVALID_PARAMETER;
    }

    WdfObjectReference(Request);
    WdfSpinLockAcquire(Context->SignalingLock);
    if (Context->SignalingState != LdacNativeChannelConnected) {
        WdfSpinLockRelease(Context->SignalingLock);
        WdfObjectDereference(Request);
        return STATUS_CONNECTION_DISCONNECTED;
    }
    if (Context->ShuttingDown) {
        WdfSpinLockRelease(Context->SignalingLock);
        WdfObjectDereference(Request);
        return STATUS_DEVICE_NOT_READY;
    }
    if (Context->MediaState != LdacNativeChannelDisconnected) {
        WdfSpinLockRelease(Context->SignalingLock);
        WdfObjectDereference(Request);
        return STATUS_DEVICE_BUSY;
    }
    Context->MediaState = LdacNativeChannelConnecting;
    Context->MediaOpenRequest = Request;
    {
        ULONG sequence = Context->OpenDiagnostics.Sequence + 1u;
        RtlZeroMemory(&Context->OpenDiagnostics,
                      sizeof(Context->OpenDiagnostics));
        Context->OpenDiagnostics.Size = sizeof(Context->OpenDiagnostics);
        Context->OpenDiagnostics.Sequence = sequence;
        Context->OpenDiagnostics.Operation = LDAC_NATIVE_OPEN_OPERATION_MEDIA;
        Context->OpenDiagnostics.IoStatus = (LONG)STATUS_PENDING;
        Context->OpenDiagnostics.BrbStatus = (LONG)STATUS_PENDING;
        Context->OpenDiagnostics.RemoteBluetoothAddress =
            Context->RemoteAddress;
        Context->OpenDiagnostics.ChannelFlags =
            CF_ROLE_EITHER | CF_LINK_AUTHENTICATED | CF_LINK_ENCRYPTED;
        Context->OpenDiagnostics.Flags =
            LDAC_NATIVE_OPEN_DIAGNOSTIC_ATTEMPTED;
        Context->OpenDiagnostics.Psm = LDAC_NATIVE_AVDTP_PSM;
    }
    KeClearEvent(&Context->MediaDisconnectedEvent);
    KeClearEvent(&Context->MediaOpenCompletedEvent);
    WdfSpinLockRelease(Context->SignalingLock);

    brb = (struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL *)
        Context->ProfileInterface.BthAllocateBrb(
            BRB_L2CA_OPEN_ENHANCED_CHANNEL,
            LDAC_NATIVE_POOL_TAG);
    if (brb == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto fail;
    }

    brb->BtAddress = Context->RemoteAddress;
    brb->Psm = LDAC_NATIVE_AVDTP_PSM;
    brb->ChannelFlags = CF_ROLE_EITHER |
                        CF_LINK_AUTHENTICATED |
                        CF_LINK_ENCRYPTED;
    brb->ConfigOut.Flags = CFG_MTU;
    brb->ConfigOut.Mtu.Min = L2CAP_MIN_MTU;
    brb->ConfigOut.Mtu.Max = preferredMtu;
    brb->ConfigOut.Mtu.Preferred = preferredMtu;
    brb->ConfigIn.Flags = CFG_MTU;
    brb->ConfigIn.Mtu.Min = L2CAP_MIN_MTU;
    brb->ConfigIn.Mtu.Max = preferredMtu;
    brb->ConfigIn.Mtu.Preferred = preferredMtu;
    brb->CallbackFlags = CALLBACK_DISCONNECT;
    brb->Callback = LdacNativeMediaIndication;
    brb->CallbackContext = Context;
    brb->ReferenceObject = WdfDeviceWdmGetDeviceObject(Context->Device);
    brb->IncomingQueueDepth = 1u;

    {
        PLDAC_NATIVE_BRB_REQUEST_CONTEXT requestContext =
            LdacNativeGetBrbRequestContext(Request);
        requestContext->OutputBuffer = output;
        requestContext->OutputBufferLength = OutputBufferLength;
    }
    status = LdacNativeSendBrbAsynchronously(
        Context,
        Request,
        (PBRB)brb,
        sizeof(*brb),
        LdacNativeBrbOperationOpenMedia,
        timeoutMs,
        LdacNativeOpenMediaCompletion);
    if (status == STATUS_PENDING) return status;

fail:
    WdfSpinLockAcquire(Context->SignalingLock);
    Context->OpenDiagnostics.IoStatus = (LONG)status;
    Context->OpenDiagnostics.Flags |=
        LDAC_NATIVE_OPEN_DIAGNOSTIC_COMPLETED;
    if (Context->MediaOpenRequest == Request) {
        Context->MediaOpenRequest = NULL;
        releaseOpenReference = TRUE;
    }
    LdacNativeResetMediaLocked(Context);
    WdfSpinLockRelease(Context->SignalingLock);
    KeSetEvent(&Context->MediaDisconnectedEvent, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&Context->MediaOpenCompletedEvent, IO_NO_INCREMENT, FALSE);
    if (releaseOpenReference) WdfObjectDereference(Request);
    return status;
#else
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Request);
    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    return STATUS_NOT_SUPPORTED;
#endif
}

static VOID LdacNativeOpenMediaCompletion(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_ WDFCONTEXT CompletionContext) {
#if (NTDDI_VERSION >= NTDDI_WIN8)
    PLDAC_NATIVE_BRB_REQUEST_CONTEXT requestContext =
        (PLDAC_NATIVE_BRB_REQUEST_CONTEXT)CompletionContext;
    PLDAC_NATIVE_DEVICE_CONTEXT context = requestContext->DeviceContext;
    struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL *brb =
        (struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL *)requestContext->Brb;
    PLDAC_NATIVE_CHANNEL_INFO output =
        (PLDAC_NATIVE_CHANNEL_INFO)requestContext->OutputBuffer;
    NTSTATUS status = Params->IoStatus.Status;
    size_t information = 0u;
    USHORT incomingMtu = 0u;
    USHORT outgoingMtu = 0u;
    BOOLEAN releaseOpenReference = FALSE;

    UNREFERENCED_PARAMETER(Target);
    if (NT_SUCCESS(status)) {
        WdfSpinLockAcquire(context->SignalingLock);
        if (context->MediaOpenRequest == Request) {
            context->MediaOpenRequest = NULL;
            releaseOpenReference = TRUE;
        }
        if (context->MediaState == LdacNativeChannelConnecting) {
            context->MediaChannelHandle = brb->ChannelHandle;
            context->MediaIncomingMtu = brb->InResults.Params.Mtu;
            context->MediaOutgoingMtu = brb->OutResults.Params.Mtu;
            context->MediaState = LdacNativeChannelConnected;
            incomingMtu = context->MediaIncomingMtu;
            outgoingMtu = context->MediaOutgoingMtu;
        } else {
            status = STATUS_CANCELLED;
        }
        WdfSpinLockRelease(context->SignalingLock);
    } else {
        WdfSpinLockAcquire(context->SignalingLock);
        if (context->MediaOpenRequest == Request) {
            context->MediaOpenRequest = NULL;
            releaseOpenReference = TRUE;
        }
        WdfSpinLockRelease(context->SignalingLock);
    }

    WdfSpinLockAcquire(context->SignalingLock);
    context->OpenDiagnostics.IoStatus = (LONG)status;
    context->OpenDiagnostics.BrbStatus = (LONG)brb->Hdr.Status;
    context->OpenDiagnostics.BtStatus = (ULONG)brb->Hdr.BtStatus;
    context->OpenDiagnostics.Response = brb->Response;
    context->OpenDiagnostics.ResponseStatus = brb->ResponseStatus;
    context->OpenDiagnostics.Flags |=
        LDAC_NATIVE_OPEN_DIAGNOSTIC_COMPLETED;
    if (NT_SUCCESS(status)) {
        context->OpenDiagnostics.Flags |=
            LDAC_NATIVE_OPEN_DIAGNOSTIC_SUCCEEDED;
    } else if (status == STATUS_REQUEST_NOT_ACCEPTED) {
        context->OpenDiagnostics.Flags |=
            LDAC_NATIVE_OPEN_DIAGNOSTIC_REMOTE_RESPONSE_VALID;
    }
    WdfSpinLockRelease(context->SignalingLock);

    if (NT_SUCCESS(status) && output != NULL &&
        requestContext->OutputBufferLength >= sizeof(*output)) {
        RtlZeroMemory(output, sizeof(*output));
        output->Size = sizeof(*output);
        output->State = LDAC_NATIVE_CHANNEL_CONNECTED;
        output->Psm = LDAC_NATIVE_AVDTP_PSM;
        output->IncomingMtu = incomingMtu;
        output->OutgoingMtu = outgoingMtu;
        information = sizeof(*output);
    } else {
        WdfSpinLockAcquire(context->SignalingLock);
        LdacNativeResetMediaLocked(context);
        WdfSpinLockRelease(context->SignalingLock);
        KeSetEvent(&context->MediaDisconnectedEvent,
                   IO_NO_INCREMENT,
                   FALSE);
    }

    LdacNativeReleaseRequestBrb(Request);
    if (releaseOpenReference) WdfObjectDereference(Request);
    WdfRequestCompleteWithInformation(Request, status, information);
    KeSetEvent(&context->MediaOpenCompletedEvent,
               IO_NO_INCREMENT,
               FALSE);
#else
    UNREFERENCED_PARAMETER(Request);
    UNREFERENCED_PARAMETER(Target);
    UNREFERENCED_PARAMETER(Params);
    UNREFERENCED_PARAMETER(CompletionContext);
#endif
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeCloseSignaling(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ ULONG TimeoutMs) {
    struct _BRB_L2CA_CLOSE_CHANNEL *brb;
    L2CAP_CHANNEL_HANDLE channelHandle;
    NTSTATUS status;

    WdfSpinLockAcquire(Context->SignalingLock);
    if (Context->SignalingState == LdacNativeChannelDisconnected) {
        WdfSpinLockRelease(Context->SignalingLock);
        return STATUS_SUCCESS;
    }
    if (Context->SignalingState != LdacNativeChannelConnected) {
        WdfSpinLockRelease(Context->SignalingLock);
        return STATUS_DEVICE_BUSY;
    }
    Context->SignalingState = LdacNativeChannelDisconnecting;
    channelHandle = Context->SignalingChannelHandle;
    KeClearEvent(&Context->SignalingDisconnectedEvent);
    WdfSpinLockRelease(Context->SignalingLock);

    brb = (struct _BRB_L2CA_CLOSE_CHANNEL *)
        Context->ProfileInterface.BthAllocateBrb(
            BRB_L2CA_CLOSE_CHANNEL,
            LDAC_NATIVE_POOL_TAG);
    if (brb == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
    } else {
        brb->BtAddress = Context->RemoteAddress;
        brb->ChannelHandle = channelHandle;
        status = LdacNativeSendBrbSynchronously(Context,
                                                (PBRB)brb,
                                                sizeof(*brb),
                                                TimeoutMs);
        Context->ProfileInterface.BthFreeBrb((PBRB)brb);
    }

    WdfSpinLockAcquire(Context->SignalingLock);
    LdacNativeResetSignalingLocked(Context);
    WdfSpinLockRelease(Context->SignalingLock);
    KeSetEvent(&Context->SignalingDisconnectedEvent, IO_NO_INCREMENT, FALSE);

    if (status == STATUS_DEVICE_NOT_CONNECTED ||
        status == STATUS_CONNECTION_DISCONNECTED) {
        return STATUS_SUCCESS;
    }
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS LdacNativeCloseMedia(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ ULONG TimeoutMs) {
    struct _BRB_L2CA_CLOSE_CHANNEL *brb;
    L2CAP_CHANNEL_HANDLE channelHandle;
    NTSTATUS status;

    WdfSpinLockAcquire(Context->SignalingLock);
    if (Context->MediaState == LdacNativeChannelDisconnected) {
        WdfSpinLockRelease(Context->SignalingLock);
        return STATUS_SUCCESS;
    }
    if (Context->MediaState != LdacNativeChannelConnected) {
        WdfSpinLockRelease(Context->SignalingLock);
        return STATUS_DEVICE_BUSY;
    }
    Context->MediaState = LdacNativeChannelDisconnecting;
    channelHandle = Context->MediaChannelHandle;
    KeClearEvent(&Context->MediaDisconnectedEvent);
    WdfSpinLockRelease(Context->SignalingLock);

    brb = (struct _BRB_L2CA_CLOSE_CHANNEL *)
        Context->ProfileInterface.BthAllocateBrb(
            BRB_L2CA_CLOSE_CHANNEL,
            LDAC_NATIVE_POOL_TAG);
    if (brb == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
    } else {
        brb->BtAddress = Context->RemoteAddress;
        brb->ChannelHandle = channelHandle;
        status = LdacNativeSendBrbSynchronously(Context,
                                                (PBRB)brb,
                                                sizeof(*brb),
                                                TimeoutMs);
        Context->ProfileInterface.BthFreeBrb((PBRB)brb);
    }

    WdfSpinLockAcquire(Context->SignalingLock);
    LdacNativeResetMediaLocked(Context);
    WdfSpinLockRelease(Context->SignalingLock);
    KeSetEvent(&Context->MediaDisconnectedEvent, IO_NO_INCREMENT, FALSE);

    if (status == STATUS_DEVICE_NOT_CONNECTED ||
        status == STATUS_CONNECTION_DISCONNECTED) {
        return STATUS_SUCCESS;
    }
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeCloseChannels(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ ULONG TimeoutMs) {
    NTSTATUS mediaStatus;
    NTSTATUS signalingStatus;

    WdfWaitLockAcquire(Context->OperationLock, NULL);
    mediaStatus = LdacNativeCloseMedia(Context, TimeoutMs);
    signalingStatus = LdacNativeCloseSignaling(Context, TimeoutMs);
    WdfWaitLockRelease(Context->OperationLock);
    return NT_SUCCESS(mediaStatus) ? signalingStatus : mediaStatus;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeTransferSignaling(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength,
    _In_ BOOLEAN ReadTransfer) {
    PLDAC_NATIVE_SIGNALING_TRANSFER_REQUEST input;
    struct _BRB_L2CA_ACL_TRANSFER *brb;
    WDFMEMORY outputMemory;
    WDF_OBJECT_ATTRIBUTES transferMemoryAttributes;
    PVOID transferBuffer;
    size_t transferBufferLength;
    ULONG timeoutMs;
    ULONG transferFlags;
    USHORT mtu;
    L2CAP_CHANNEL_HANDLE channelHandle;
    NTSTATUS status;
    BOOLEAN releaseTransferReference = FALSE;

    if (InputBufferLength < sizeof(*input) ||
        OutputBufferLength == 0u ||
        OutputBufferLength > LDAC_NATIVE_MAX_SIGNALING_TRANSFER) {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    status = WdfRequestRetrieveInputBuffer(Request,
                                           sizeof(*input),
                                           (PVOID *)&input,
                                           NULL);
    if (!NT_SUCCESS(status)) return status;
    if (input->Size != sizeof(*input) || input->Flags != 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    status = WdfRequestRetrieveOutputMemory(Request, &outputMemory);
    if (!NT_SUCCESS(status)) return status;
    transferBuffer = WdfMemoryGetBuffer(outputMemory, &transferBufferLength);
    if (transferBuffer == NULL || transferBufferLength < OutputBufferLength) {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    timeoutMs = LdacNativeNormalizeTimeout(
        input->TimeoutMs,
        LDAC_NATIVE_DEFAULT_TRANSFER_TIMEOUT_MS);
    transferFlags = ReadTransfer
        ? ACL_TRANSFER_DIRECTION_IN | ACL_SHORT_TRANSFER_OK
        : ACL_TRANSFER_DIRECTION_OUT;

    WdfObjectReference(Request);
    WdfSpinLockAcquire(Context->SignalingLock);
    if (Context->ShuttingDown) {
        WdfSpinLockRelease(Context->SignalingLock);
        WdfObjectDereference(Request);
        return STATUS_DEVICE_NOT_READY;
    }
    if (Context->SignalingState != LdacNativeChannelConnected) {
        WdfSpinLockRelease(Context->SignalingLock);
        WdfObjectDereference(Request);
        return STATUS_CONNECTION_DISCONNECTED;
    }
    mtu = ReadTransfer
        ? Context->SignalingIncomingMtu
        : Context->SignalingOutgoingMtu;
    if ((ReadTransfer && Context->SignalingReadPending) ||
        (!ReadTransfer && Context->SignalingWritePending)) {
        WdfSpinLockRelease(Context->SignalingLock);
        WdfObjectDereference(Request);
        return STATUS_DEVICE_BUSY;
    }
    if ((!ReadTransfer && OutputBufferLength > mtu) ||
        (ReadTransfer && OutputBufferLength < mtu)) {
        WdfSpinLockRelease(Context->SignalingLock);
        WdfObjectDereference(Request);
        return STATUS_BUFFER_TOO_SMALL;
    }
    channelHandle = Context->SignalingChannelHandle;
    if (ReadTransfer) {
        Context->SignalingReadPending = TRUE;
        Context->SignalingReadRequest = Request;
        KeClearEvent(&Context->SignalingReadCompletedEvent);
    } else {
        Context->SignalingWritePending = TRUE;
        Context->SignalingWriteRequest = Request;
        KeClearEvent(&Context->SignalingWriteCompletedEvent);
    }
    {
        PLDAC_NATIVE_TRANSFER_RESULT diagnostics = ReadTransfer
            ? &Context->SignalingReadDiagnostics
            : &Context->SignalingWriteDiagnostics;
        diagnostics->Sequence++;
        diagnostics->Operation = ReadTransfer
            ? LDAC_NATIVE_TRANSFER_OPERATION_READ_SIGNALING
            : LDAC_NATIVE_TRANSFER_OPERATION_WRITE_SIGNALING;
        diagnostics->IoStatus = (LONG)STATUS_PENDING;
        diagnostics->BrbStatus = (LONG)STATUS_PENDING;
        diagnostics->BtStatus = 0u;
        diagnostics->RequestedBytes = (ULONG)OutputBufferLength;
        diagnostics->BrbBufferSize = (ULONG)OutputBufferLength;
        diagnostics->RemainingBytes = (ULONG)OutputBufferLength;
        diagnostics->TransferFlags = transferFlags;
    }
    WdfSpinLockRelease(Context->SignalingLock);

    {
        PLDAC_NATIVE_BRB_REQUEST_CONTEXT requestContext =
            LdacNativeGetBrbRequestContext(Request);
        PVOID nonPagedTransferBuffer = NULL;

        requestContext->OutputBuffer = transferBuffer;
        requestContext->OutputBufferLength = OutputBufferLength;
        WDF_OBJECT_ATTRIBUTES_INIT(&transferMemoryAttributes);
        transferMemoryAttributes.ParentObject = Request;
        status = WdfMemoryCreate(&transferMemoryAttributes,
                                 NonPagedPoolNx,
                                 LDAC_NATIVE_POOL_TAG,
                                 OutputBufferLength,
                                 &requestContext->TransferMemory,
                                 &nonPagedTransferBuffer);
        if (!NT_SUCCESS(status)) goto fail;
        if (!ReadTransfer) {
            RtlCopyMemory(nonPagedTransferBuffer,
                          transferBuffer,
                          OutputBufferLength);
        }
        transferBuffer = nonPagedTransferBuffer;
    }

    brb = (struct _BRB_L2CA_ACL_TRANSFER *)
        Context->ProfileInterface.BthAllocateBrb(
            BRB_L2CA_ACL_TRANSFER,
            LDAC_NATIVE_POOL_TAG);
    if (brb == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto fail;
    }
    brb->BtAddress = Context->RemoteAddress;
    brb->ChannelHandle = channelHandle;
    brb->BufferMDL = NULL;
    brb->Buffer = transferBuffer;
    brb->BufferSize = (ULONG)OutputBufferLength;
    brb->TransferFlags = transferFlags;
    brb->Timeout = 0;

    status = LdacNativeSendBrbAsynchronously(
        Context,
        Request,
        (PBRB)brb,
        sizeof(*brb),
        ReadTransfer
            ? LdacNativeBrbOperationReadSignaling
            : LdacNativeBrbOperationWriteSignaling,
        ReadTransfer ? 0u : timeoutMs,
        LdacNativeTransferCompletion);
    if (status == STATUS_PENDING) return status;

fail:
    WdfSpinLockAcquire(Context->SignalingLock);
    if (ReadTransfer) {
        Context->SignalingReadDiagnostics.IoStatus = (LONG)status;
        Context->SignalingReadDiagnostics.BrbStatus = (LONG)status;
        Context->SignalingReadDiagnostics.BrbBufferSize = 0u;
        Context->SignalingReadDiagnostics.RemainingBytes = 0u;
        Context->SignalingReadPending = FALSE;
        if (Context->SignalingReadRequest == Request) {
            Context->SignalingReadRequest = NULL;
            releaseTransferReference = TRUE;
        }
    } else {
        Context->SignalingWriteDiagnostics.IoStatus = (LONG)status;
        Context->SignalingWriteDiagnostics.BrbStatus = (LONG)status;
        Context->SignalingWriteDiagnostics.BrbBufferSize = 0u;
        Context->SignalingWriteDiagnostics.RemainingBytes = 0u;
        Context->SignalingWritePending = FALSE;
        if (Context->SignalingWriteRequest == Request) {
            Context->SignalingWriteRequest = NULL;
            releaseTransferReference = TRUE;
        }
    }
    WdfSpinLockRelease(Context->SignalingLock);
    LdacNativeReleaseRequestBrb(Request);
    KeSetEvent(ReadTransfer
                   ? &Context->SignalingReadCompletedEvent
                   : &Context->SignalingWriteCompletedEvent,
               IO_NO_INCREMENT,
               FALSE);
    if (releaseTransferReference) WdfObjectDereference(Request);
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeWriteMedia(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength) {
    PLDAC_NATIVE_SIGNALING_TRANSFER_REQUEST input;
    struct _BRB_L2CA_ACL_TRANSFER *brb;
    WDFMEMORY outputMemory;
    WDF_OBJECT_ATTRIBUTES transferMemoryAttributes;
    PVOID transferBuffer;
    size_t transferBufferLength;
    ULONG timeoutMs;
    L2CAP_CHANNEL_HANDLE channelHandle;
    NTSTATUS status;
    BOOLEAN releaseTransferReference = FALSE;

    if (InputBufferLength < sizeof(*input) ||
        OutputBufferLength == 0u ||
        OutputBufferLength > LDAC_NATIVE_MAX_MEDIA_TRANSFER) {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    status = WdfRequestRetrieveInputBuffer(Request,
                                           sizeof(*input),
                                           (PVOID *)&input,
                                           NULL);
    if (!NT_SUCCESS(status)) return status;
    if (input->Size != sizeof(*input) || input->Flags != 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    status = WdfRequestRetrieveOutputMemory(Request, &outputMemory);
    if (!NT_SUCCESS(status)) return status;
    transferBuffer = WdfMemoryGetBuffer(outputMemory, &transferBufferLength);
    if (transferBuffer == NULL || transferBufferLength < OutputBufferLength) {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    timeoutMs = LdacNativeNormalizeTimeout(
        input->TimeoutMs,
        LDAC_NATIVE_DEFAULT_TRANSFER_TIMEOUT_MS);

    WdfObjectReference(Request);
    WdfSpinLockAcquire(Context->SignalingLock);
    if (Context->ShuttingDown) {
        WdfSpinLockRelease(Context->SignalingLock);
        WdfObjectDereference(Request);
        return STATUS_DEVICE_NOT_READY;
    }
    if (Context->MediaState != LdacNativeChannelConnected) {
        WdfSpinLockRelease(Context->SignalingLock);
        WdfObjectDereference(Request);
        return STATUS_CONNECTION_DISCONNECTED;
    }
    if (Context->MediaWritePending) {
        WdfSpinLockRelease(Context->SignalingLock);
        WdfObjectDereference(Request);
        return STATUS_DEVICE_BUSY;
    }
    if (OutputBufferLength > Context->MediaOutgoingMtu) {
        WdfSpinLockRelease(Context->SignalingLock);
        WdfObjectDereference(Request);
        return STATUS_BUFFER_TOO_SMALL;
    }
    channelHandle = Context->MediaChannelHandle;
    Context->MediaWritePending = TRUE;
    Context->MediaWriteRequest = Request;
    KeClearEvent(&Context->MediaWriteCompletedEvent);
    Context->MediaWriteDiagnostics.Sequence++;
    Context->MediaWriteDiagnostics.Operation =
        LDAC_NATIVE_TRANSFER_OPERATION_WRITE_MEDIA;
    Context->MediaWriteDiagnostics.IoStatus = (LONG)STATUS_PENDING;
    Context->MediaWriteDiagnostics.BrbStatus = (LONG)STATUS_PENDING;
    Context->MediaWriteDiagnostics.BtStatus = 0u;
    Context->MediaWriteDiagnostics.RequestedBytes =
        (ULONG)OutputBufferLength;
    Context->MediaWriteDiagnostics.BrbBufferSize =
        (ULONG)OutputBufferLength;
    Context->MediaWriteDiagnostics.RemainingBytes =
        (ULONG)OutputBufferLength;
    Context->MediaWriteDiagnostics.TransferFlags =
        ACL_TRANSFER_DIRECTION_OUT;
    WdfSpinLockRelease(Context->SignalingLock);

    {
        PLDAC_NATIVE_BRB_REQUEST_CONTEXT requestContext =
            LdacNativeGetBrbRequestContext(Request);
        PVOID nonPagedTransferBuffer = NULL;

        requestContext->OutputBuffer = transferBuffer;
        requestContext->OutputBufferLength = OutputBufferLength;
        WDF_OBJECT_ATTRIBUTES_INIT(&transferMemoryAttributes);
        transferMemoryAttributes.ParentObject = Request;
        status = WdfMemoryCreate(&transferMemoryAttributes,
                                 NonPagedPoolNx,
                                 LDAC_NATIVE_POOL_TAG,
                                 OutputBufferLength,
                                 &requestContext->TransferMemory,
                                 &nonPagedTransferBuffer);
        if (!NT_SUCCESS(status)) goto fail;
        RtlCopyMemory(nonPagedTransferBuffer,
                      transferBuffer,
                      OutputBufferLength);
        transferBuffer = nonPagedTransferBuffer;
    }

    brb = (struct _BRB_L2CA_ACL_TRANSFER *)
        Context->ProfileInterface.BthAllocateBrb(
            BRB_L2CA_ACL_TRANSFER,
            LDAC_NATIVE_POOL_TAG);
    if (brb == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto fail;
    }
    brb->BtAddress = Context->RemoteAddress;
    brb->ChannelHandle = channelHandle;
    brb->BufferMDL = NULL;
    brb->Buffer = transferBuffer;
    brb->BufferSize = (ULONG)OutputBufferLength;
    brb->TransferFlags = ACL_TRANSFER_DIRECTION_OUT;
    brb->Timeout = 0;

    status = LdacNativeSendBrbAsynchronously(
        Context,
        Request,
        (PBRB)brb,
        sizeof(*brb),
        LdacNativeBrbOperationWriteMedia,
        timeoutMs,
        LdacNativeTransferCompletion);
    if (status == STATUS_PENDING) return status;

fail:
    WdfSpinLockAcquire(Context->SignalingLock);
    Context->MediaWriteDiagnostics.IoStatus = (LONG)status;
    Context->MediaWriteDiagnostics.BrbStatus = (LONG)status;
    Context->MediaWriteDiagnostics.BrbBufferSize = 0u;
    Context->MediaWriteDiagnostics.RemainingBytes = 0u;
    Context->MediaWritePending = FALSE;
    if (Context->MediaWriteRequest == Request) {
        Context->MediaWriteRequest = NULL;
        releaseTransferReference = TRUE;
    }
    WdfSpinLockRelease(Context->SignalingLock);
    LdacNativeReleaseRequestBrb(Request);
    KeSetEvent(&Context->MediaWriteCompletedEvent,
               IO_NO_INCREMENT,
               FALSE);
    if (releaseTransferReference) WdfObjectDereference(Request);
    return status;
}

static VOID LdacNativeTransferCompletion(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_ WDFCONTEXT CompletionContext) {
    PLDAC_NATIVE_BRB_REQUEST_CONTEXT requestContext =
        (PLDAC_NATIVE_BRB_REQUEST_CONTEXT)CompletionContext;
    PLDAC_NATIVE_DEVICE_CONTEXT context = requestContext->DeviceContext;
    struct _BRB_L2CA_ACL_TRANSFER *brb =
        (struct _BRB_L2CA_ACL_TRANSFER *)requestContext->Brb;
    LDAC_NATIVE_BRB_OPERATION operation = requestContext->Operation;
    NTSTATUS status = Params->IoStatus.Status;
    size_t information = NT_SUCCESS(status) ? brb->BufferSize : 0u;
    BOOLEAN releaseTransferReference = FALSE;

    UNREFERENCED_PARAMETER(Target);
    WdfSpinLockAcquire(context->SignalingLock);
    if (operation == LdacNativeBrbOperationReadSignaling) {
        context->SignalingReadDiagnostics.IoStatus = (LONG)status;
        context->SignalingReadDiagnostics.BrbStatus =
            (LONG)brb->Hdr.Status;
        context->SignalingReadDiagnostics.BtStatus =
            (ULONG)brb->Hdr.BtStatus;
        context->SignalingReadDiagnostics.BrbBufferSize = brb->BufferSize;
        context->SignalingReadDiagnostics.RemainingBytes =
            brb->RemainingBufferSize;
        context->SignalingReadDiagnostics.TransferFlags =
            brb->TransferFlags;
        context->SignalingReadPending = FALSE;
        if (context->SignalingReadRequest == Request) {
            context->SignalingReadRequest = NULL;
            releaseTransferReference = TRUE;
        }
    } else if (operation == LdacNativeBrbOperationWriteSignaling) {
        context->SignalingWriteDiagnostics.IoStatus = (LONG)status;
        context->SignalingWriteDiagnostics.BrbStatus =
            (LONG)brb->Hdr.Status;
        context->SignalingWriteDiagnostics.BtStatus =
            (ULONG)brb->Hdr.BtStatus;
        context->SignalingWriteDiagnostics.BrbBufferSize = brb->BufferSize;
        context->SignalingWriteDiagnostics.RemainingBytes =
            brb->RemainingBufferSize;
        context->SignalingWriteDiagnostics.TransferFlags =
            brb->TransferFlags;
        context->SignalingWritePending = FALSE;
        if (context->SignalingWriteRequest == Request) {
            context->SignalingWriteRequest = NULL;
            releaseTransferReference = TRUE;
        }
    } else if (operation == LdacNativeBrbOperationWriteMedia) {
        context->MediaWriteDiagnostics.IoStatus = (LONG)status;
        context->MediaWriteDiagnostics.BrbStatus =
            (LONG)brb->Hdr.Status;
        context->MediaWriteDiagnostics.BtStatus =
            (ULONG)brb->Hdr.BtStatus;
        context->MediaWriteDiagnostics.BrbBufferSize = brb->BufferSize;
        context->MediaWriteDiagnostics.RemainingBytes =
            brb->RemainingBufferSize;
        context->MediaWriteDiagnostics.TransferFlags =
            brb->TransferFlags;
        context->MediaWritePending = FALSE;
        if (context->MediaWriteRequest == Request) {
            context->MediaWriteRequest = NULL;
            releaseTransferReference = TRUE;
        }
    }
    WdfSpinLockRelease(context->SignalingLock);

    if (NT_SUCCESS(status) &&
        operation == LdacNativeBrbOperationReadSignaling) {
        if (requestContext->OutputBuffer == NULL ||
            requestContext->OutputBufferLength < information) {
            status = STATUS_BUFFER_OVERFLOW;
            information = 0u;
        } else {
            RtlCopyMemory(requestContext->OutputBuffer,
                          brb->Buffer,
                          information);
        }
    }

    LdacNativeReleaseRequestBrb(Request);
    if (releaseTransferReference) WdfObjectDereference(Request);
    WdfRequestCompleteWithInformation(Request, status, information);
    if (operation == LdacNativeBrbOperationReadSignaling) {
        KeSetEvent(&context->SignalingReadCompletedEvent,
                   IO_NO_INCREMENT,
                   FALSE);
    } else if (operation == LdacNativeBrbOperationWriteSignaling) {
        KeSetEvent(&context->SignalingWriteCompletedEvent,
                   IO_NO_INCREMENT,
                   FALSE);
    } else if (operation == LdacNativeBrbOperationWriteMedia) {
        KeSetEvent(&context->MediaWriteCompletedEvent,
                   IO_NO_INCREMENT,
                   FALSE);
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID LdacNativeConnectionShutdown(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context) {
    WDFREQUEST openRequest = NULL;
    WDFREQUEST incomingRequest = NULL;
    WDFREQUEST readRequest = NULL;
    WDFREQUEST writeRequest = NULL;
    WDFREQUEST mediaOpenRequest = NULL;
    WDFREQUEST mediaWriteRequest = NULL;

    WdfWaitLockAcquire(Context->OperationLock, NULL);

    WdfSpinLockAcquire(Context->SignalingLock);
    Context->ShuttingDown = TRUE;
    if (Context->SignalingOpenRequest != NULL) {
        openRequest = Context->SignalingOpenRequest;
        WdfObjectReference(openRequest);
    }
    if (Context->IncomingSignalingRequest != NULL) {
        incomingRequest = Context->IncomingSignalingRequest;
        WdfObjectReference(incomingRequest);
    }
    if (Context->SignalingReadRequest != NULL) {
        readRequest = Context->SignalingReadRequest;
        WdfObjectReference(readRequest);
    }
    if (Context->SignalingWriteRequest != NULL) {
        writeRequest = Context->SignalingWriteRequest;
        WdfObjectReference(writeRequest);
    }
    if (Context->MediaOpenRequest != NULL) {
        mediaOpenRequest = Context->MediaOpenRequest;
        WdfObjectReference(mediaOpenRequest);
    }
    if (Context->MediaWriteRequest != NULL) {
        mediaWriteRequest = Context->MediaWriteRequest;
        WdfObjectReference(mediaWriteRequest);
    }
    WdfSpinLockRelease(Context->SignalingLock);

    if (readRequest != NULL) (void)WdfRequestCancelSentRequest(readRequest);
    if (writeRequest != NULL) (void)WdfRequestCancelSentRequest(writeRequest);
    if (mediaWriteRequest != NULL) {
        (void)WdfRequestCancelSentRequest(mediaWriteRequest);
    }
    if (mediaOpenRequest != NULL) {
        (void)WdfRequestCancelSentRequest(mediaOpenRequest);
        (void)KeWaitForSingleObject(&Context->MediaOpenCompletedEvent,
                                    Executive,
                                    KernelMode,
                                    FALSE,
                                    NULL);
    }
    if (openRequest != NULL) {
        (void)WdfRequestCancelSentRequest(openRequest);
        (void)KeWaitForSingleObject(&Context->SignalingOpenCompletedEvent,
                                    Executive,
                                    KernelMode,
                                    FALSE,
                                    NULL);
    }
    if (incomingRequest != NULL) {
        (void)WdfRequestCancelSentRequest(incomingRequest);
        (void)KeWaitForSingleObject(&Context->IncomingSignalingCompletedEvent,
                                    Executive,
                                    KernelMode,
                                    FALSE,
                                    NULL);
    }

    if (readRequest != NULL) {
        (void)KeWaitForSingleObject(&Context->SignalingReadCompletedEvent,
                                    Executive,
                                    KernelMode,
                                    FALSE,
                                    NULL);
    }
    if (writeRequest != NULL) {
        (void)KeWaitForSingleObject(&Context->SignalingWriteCompletedEvent,
                                    Executive,
                                    KernelMode,
                                    FALSE,
                                    NULL);
    }
    if (mediaWriteRequest != NULL) {
        (void)KeWaitForSingleObject(&Context->MediaWriteCompletedEvent,
                                    Executive,
                                    KernelMode,
                                    FALSE,
                                    NULL);
    }

    if (openRequest != NULL) WdfObjectDereference(openRequest);
    if (incomingRequest != NULL) WdfObjectDereference(incomingRequest);
    if (readRequest != NULL) WdfObjectDereference(readRequest);
    if (writeRequest != NULL) WdfObjectDereference(writeRequest);
    if (mediaOpenRequest != NULL) WdfObjectDereference(mediaOpenRequest);
    if (mediaWriteRequest != NULL) WdfObjectDereference(mediaWriteRequest);
    (void)LdacNativeCloseMedia(
        Context,
        LDAC_NATIVE_DEFAULT_TRANSFER_TIMEOUT_MS);
    (void)LdacNativeCloseSignaling(
        Context,
        LDAC_NATIVE_DEFAULT_TRANSFER_TIMEOUT_MS);
    WdfSpinLockAcquire(Context->SignalingLock);
    if (Context->PnpStarted) {
        Context->ShuttingDown = FALSE;
    }
    WdfSpinLockRelease(Context->SignalingLock);
    WdfWaitLockRelease(Context->OperationLock);
}
