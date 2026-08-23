// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_bth_signaling.h"
#include "nativeldac_avdtp_transaction.h"

#include <bthioctl.h>

#define NLD_BTH_SIGNALING_POOL_TAG 'ScdL'

struct _NLD_BTH_ASYNC_OPEN_REQUEST {
    volatile LONG ReferenceCount;
    PNLD_BTH_SIGNALING_CONTEXT Owner;
    const NLD_BTH_PROFILE_CONTEXT* Profile;
    PIRP Irp;
    PIO_WORKITEM WorkItem;
    PBRB OpenBrb;
    PBRB CloseBrb;
    ULONG Generation;
    IO_STATUS_BLOCK IoStatus;
    volatile LONG RemoteDisconnected;
};

static void NldBthSignalingRequestAddReference(
    _Inout_ NLD_BTH_ASYNC_OPEN_REQUEST* request);

static void NldBthSignalingRequestRelease(
    _Inout_ NLD_BTH_ASYNC_OPEN_REQUEST* request);

static NTSTATUS NldBthSignalingOpenCompletion(
    _In_ PDEVICE_OBJECT device_object,
    _In_ PIRP irp,
    _In_ PVOID completion_context);

#if (NTDDI_VERSION >= NTDDI_WIN8)
static void NldBthSignalingIndicationCallback(
    _In_opt_ PVOID callback_context,
    _In_ INDICATION_CODE indication,
    _In_ PINDICATION_PARAMETERS_ENHANCED parameters);
#endif

static IO_WORKITEM_ROUTINE NldBthSignalingOpenWorker;

static NTSTATUS NldBthSignalingSubmitCloseBrb(
    _In_ const NLD_BTH_PROFILE_CONTEXT* profile,
    _Inout_ PBRB close_brb,
    _In_ L2CAP_CHANNEL_HANDLE channel_handle,
    _In_ ULONG timeout_ms);

static NTSTATUS NldBthSignalingCloseInternal(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ ULONG timeout_ms,
    _In_ BOOLEAN target_offline);

static NTSTATUS NldBthSignalingTransferInternal(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ NLD_BTH_TRANSFER_DIRECTION direction,
    void* buffer,
    _In_ ULONG buffer_length,
    _In_ ULONG timeout_ms,
    _Out_opt_ ULONG* transferred);

static NLD_BTH_TRANSFER_RESULT NldBthSignalingBeginTransfer(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ NLD_BTH_TRANSFER_DIRECTION direction,
    _Out_ const NLD_BTH_PROFILE_CONTEXT** profile,
    _Out_ L2CAP_CHANNEL_HANDLE* channel_handle,
    _Out_ ULONG* mtu,
    _Out_ ULONG* channel_generation,
    _Out_ ULONG* request_generation);

static void NldBthSignalingCompleteTransfer(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ NLD_BTH_TRANSFER_DIRECTION direction,
    _In_ ULONG channel_generation,
    _In_ ULONG request_generation);

static NTSTATUS NldBthSignalingMapAvdtpStatus(
    _In_ NLD_AVDTP_TRANSACTION_STATUS status);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, NldBthSignalingTransferInternal)
#pragma alloc_text(PAGE, NldBthSignalingWrite)
#pragma alloc_text(PAGE, NldBthSignalingWriteGeneration)
#pragma alloc_text(PAGE, NldBthSignalingRead)
#pragma alloc_text(PAGE, NldBthSignalingDiscover)
#endif

static NTSTATUS NldBthSignalingMapAvdtpStatus(
    _In_ NLD_AVDTP_TRANSACTION_STATUS status) {
    switch (status) {
        case NldAvdtpTransactionOk:
            return STATUS_SUCCESS;
        case NldAvdtpTransactionInvalidArgument:
            return STATUS_INVALID_PARAMETER;
        case NldAvdtpTransactionBusy:
            return STATUS_DEVICE_BUSY;
        case NldAvdtpTransactionUnsupportedFragment:
            return STATUS_NOT_SUPPORTED;
        case NldAvdtpTransactionRejected:
            return STATUS_REQUEST_NOT_ACCEPTED;
        case NldAvdtpTransactionStale:
            return STATUS_RETRY;
        case NldAvdtpTransactionFaulted:
            return STATUS_INVALID_DEVICE_STATE;
        case NldAvdtpTransactionTruncated:
        case NldAvdtpTransactionUnexpectedResponse:
        default:
            return STATUS_DEVICE_PROTOCOL_ERROR;
    }
}

static NLD_BTH_TRANSFER_RESULT NldBthSignalingBeginTransfer(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ NLD_BTH_TRANSFER_DIRECTION direction,
    _Out_ const NLD_BTH_PROFILE_CONTEXT** profile,
    _Out_ L2CAP_CHANNEL_HANDLE* channel_handle,
    _Out_ ULONG* mtu,
    _Out_ ULONG* channel_generation,
    _Out_ ULONG* request_generation) {
    KIRQL old_irql;
    NLD_BTH_TRANSFER_RESULT result;

    if (profile == NULL || channel_handle == NULL || mtu == NULL ||
        channel_generation == NULL || request_generation == NULL) {
        return NldBthTransferInvalidArgument;
    }
    *profile = NULL;
    *channel_handle = NULL;
    *mtu = 0u;
    *channel_generation = 0u;
    *request_generation = 0u;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    result = NldBthTransferBegin(&context->TransferOwner,
                                 direction,
                                 channel_generation,
                                 request_generation);
    *profile = context->Profile;
    *channel_handle = context->ChannelHandle;
    *mtu = direction == NldBthTransferRead
        ? context->IncomingMtu
        : context->OutgoingMtu;
    KeReleaseSpinLock(&context->Lock, old_irql);
    return result;
}

static void NldBthSignalingCompleteTransfer(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ NLD_BTH_TRANSFER_DIRECTION direction,
    _In_ ULONG channel_generation,
    _In_ ULONG request_generation) {
    KIRQL old_irql;

    KeAcquireSpinLock(&context->Lock, &old_irql);
    (void)NldBthTransferComplete(&context->TransferOwner,
                                 direction,
                                 channel_generation,
                                 request_generation);
    KeReleaseSpinLock(&context->Lock, old_irql);
}

static void NldBthSignalingRequestAddReference(
    _Inout_ NLD_BTH_ASYNC_OPEN_REQUEST* request) {
    (void)InterlockedIncrement(&request->ReferenceCount);
}

static void NldBthSignalingRequestRelease(
    _Inout_ NLD_BTH_ASYNC_OPEN_REQUEST* request) {
    if (InterlockedDecrement(&request->ReferenceCount) != 0) return;
    if (request->OpenBrb != NULL) {
        request->Profile->Interface.BthFreeBrb(request->OpenBrb);
    }
    if (request->CloseBrb != NULL) {
        request->Profile->Interface.BthFreeBrb(request->CloseBrb);
    }
    if (request->WorkItem != NULL) IoFreeWorkItem(request->WorkItem);
    if (request->Irp != NULL) IoFreeIrp(request->Irp);
    ExFreePoolWithTag(request, NLD_BTH_SIGNALING_POOL_TAG);
}

#if (NTDDI_VERSION >= NTDDI_WIN8)
static void NldBthSignalingIndicationCallback(
    _In_opt_ PVOID callback_context,
    _In_ INDICATION_CODE indication,
    _In_ PINDICATION_PARAMETERS_ENHANCED parameters) {
    NLD_BTH_ASYNC_OPEN_REQUEST* request =
        (NLD_BTH_ASYNC_OPEN_REQUEST*)callback_context;
    PNLD_BTH_SIGNALING_CONTEXT context;
    NLD_BTH_DISCONNECT_CALLBACK disconnect_callback = NULL;
    PVOID disconnect_callback_context = NULL;
    ULONG channel_generation = 0u;
    KIRQL old_irql;

    UNREFERENCED_PARAMETER(parameters);
    if (request == NULL) return;
    if (indication == IndicationAddReference) {
        NldBthSignalingRequestAddReference(request);
        return;
    }
    if (indication == IndicationReleaseReference) {
        NldBthSignalingRequestRelease(request);
        return;
    }
    if (indication != IndicationRemoteDisconnect) return;

    (void)InterlockedExchange(&request->RemoteDisconnected, 1);
    context = request->Owner;
    if (context == NULL) return;

    KeAcquireSpinLock(&context->Lock, &old_irql);
    if (context->ActiveChannel == request &&
        NldBthSignalingOwnerOnRemoteDisconnect(
            &context->Owner,
            request->Generation)) {
        (void)NldBthTransferRequestClose(&context->TransferOwner, TRUE);
        context->ChannelHandle = NULL;
        context->IncomingMtu = 0u;
        context->OutgoingMtu = 0u;
        context->LastCloseStatus = STATUS_CONNECTION_DISCONNECTED;
        disconnect_callback = context->DisconnectCallback;
        disconnect_callback_context =
            context->DisconnectCallbackContext;
        channel_generation = request->Generation;
    }
    KeReleaseSpinLock(&context->Lock, old_irql);

    if (disconnect_callback != NULL) {
        disconnect_callback(disconnect_callback_context,
                            channel_generation);
    }
}
#endif

static NTSTATUS NldBthSignalingOpenCompletion(
    _In_ PDEVICE_OBJECT device_object,
    _In_ PIRP irp,
    _In_ PVOID completion_context) {
    NLD_BTH_ASYNC_OPEN_REQUEST* request =
        (NLD_BTH_ASYNC_OPEN_REQUEST*)completion_context;

    UNREFERENCED_PARAMETER(device_object);
    request->IoStatus = irp->IoStatus;
    IoQueueWorkItem(request->WorkItem,
                    NldBthSignalingOpenWorker,
                    DelayedWorkQueue,
                    request);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS NldBthSignalingSubmitCloseBrb(
    _In_ const NLD_BTH_PROFILE_CONTEXT* profile,
    _Inout_ PBRB close_brb,
    _In_ L2CAP_CHANNEL_HANDLE channel_handle,
    _In_ ULONG timeout_ms) {
    struct _BRB_L2CA_CLOSE_CHANNEL* close_channel =
        (struct _BRB_L2CA_CLOSE_CHANNEL*)close_brb;

    if (profile == NULL || close_brb == NULL || channel_handle == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    close_channel->BtAddress = profile->RemoteAddress;
    close_channel->ChannelHandle = channel_handle;
    return NldBthProfileSubmitBrb(profile,
                                  close_brb,
                                  sizeof(*close_channel),
                                  timeout_ms);
}

static VOID NldBthSignalingOpenWorker(
    _In_ PDEVICE_OBJECT device_object,
    _In_opt_ PVOID work_item_context) {
    NLD_BTH_ASYNC_OPEN_REQUEST* request =
        (NLD_BTH_ASYNC_OPEN_REQUEST*)work_item_context;
    PNLD_BTH_SIGNALING_CONTEXT context;
    struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL* open_channel;
    NLD_BTH_SIGNALING_ACTION action;
    L2CAP_CHANNEL_HANDLE channel_handle = NULL;
    PBRB close_brb = NULL;
    NLD_BTH_DISCONNECT_CALLBACK disconnect_callback = NULL;
    PVOID disconnect_callback_context = NULL;
    KIRQL old_irql;
    NTSTATUS close_status = STATUS_SUCCESS;
    BOOLEAN channel_acquired;
    BOOLEAN retain_request = FALSE;

    UNREFERENCED_PARAMETER(device_object);
    if (request == NULL) return;
    context = request->Owner;
    open_channel = (struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL*)request->OpenBrb;
    channel_acquired = NT_SUCCESS(request->IoStatus.Status) &&
                       open_channel->ChannelHandle != NULL;

    KeAcquireSpinLock(&context->Lock, &old_irql);
    if (context->PendingOpen == request) {
        context->PendingOpen = NULL;
    }
    context->LastOpenStatus = request->IoStatus.Status;
    action = NldBthSignalingOwnerOnOpenComplete(
        &context->Owner,
        request->Generation,
        NT_SUCCESS(request->IoStatus.Status),
        channel_acquired);
    if (channel_acquired) {
        channel_handle = open_channel->ChannelHandle;
        if (action == NldBthSignalingActionNone &&
            context->Owner.State == NldBthSignalingChannelOpen) {
            if (InterlockedCompareExchange(
                    &request->RemoteDisconnected,
                    0,
                    0) != 0) {
                (void)NldBthSignalingOwnerOnRemoteDisconnect(
                    &context->Owner,
                    request->Generation);
                (void)NldBthTransferRequestClose(
                    &context->TransferOwner,
                    TRUE);
                (void)NldBthTransferFinishClose(
                    &context->TransferOwner);
                context->LastOpenStatus = STATUS_CONNECTION_DISCONNECTED;
                disconnect_callback = context->DisconnectCallback;
                disconnect_callback_context =
                    context->DisconnectCallbackContext;
            } else {
                context->ChannelHandle = channel_handle;
                context->IncomingMtu = open_channel->InResults.Params.Mtu;
                context->OutgoingMtu = open_channel->OutResults.Params.Mtu;
                context->ActiveChannel = request;
                retain_request = TRUE;
            }
            if (retain_request &&
                NldBthTransferOpenChannel(&context->TransferOwner) == 0ul) {
                context->LastOpenStatus = STATUS_INVALID_DEVICE_STATE;
                context->ActiveChannel = NULL;
                retain_request = FALSE;
                close_brb = request->CloseBrb;
                request->CloseBrb = NULL;
                action = NldBthSignalingOwnerRequestClose(
                    &context->Owner,
                    FALSE);
            }
        } else if (action == NldBthSignalingActionSubmitClose) {
            close_brb = request->CloseBrb;
            request->CloseBrb = NULL;
        }
    }
    KeReleaseSpinLock(&context->Lock, old_irql);

    /*
     * The open BRB is no longer needed once the OPEN has completed.  Free
     * it here while the profile interface is still valid.  BTHport can
     * invoke IndicationReleaseReference asynchronously after profile
     * teardown, and the final NldBthSignalingRequestRelease must never
     * dereference a stale request->Profile to free this BRB.
     */
    if (request->OpenBrb != NULL) {
        request->Profile->Interface.BthFreeBrb(request->OpenBrb);
        request->OpenBrb = NULL;
    }

    if (disconnect_callback != NULL) {
        disconnect_callback(disconnect_callback_context,
                            request->Generation);
    }

    if (action == NldBthSignalingActionSubmitClose &&
        close_brb != NULL && channel_handle != NULL) {
        close_status = NldBthSignalingSubmitCloseBrb(
            request->Profile,
            close_brb,
            channel_handle,
            NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);
        request->Profile->Interface.BthFreeBrb(close_brb);
        KeAcquireSpinLock(&context->Lock, &old_irql);
        context->LastCloseStatus = close_status;
        NldBthSignalingOwnerOnCloseComplete(&context->Owner,
                                            NT_SUCCESS(close_status));
        context->ChannelHandle = NULL;
        context->IncomingMtu = 0u;
        context->OutgoingMtu = 0u;
        KeReleaseSpinLock(&context->Lock, old_irql);
    }

    if (!retain_request) NldBthSignalingRequestRelease(request);
    KeSetEvent(&context->RequestDrainedEvent, IO_NO_INCREMENT, FALSE);
}

void NldBthSignalingInitialize(
    _Out_ PNLD_BTH_SIGNALING_CONTEXT context) {
    if (context == NULL) return;
    RtlZeroMemory(context, sizeof(*context));
    KeInitializeMutex(&context->OperationMutex, 0u);
    KeInitializeSpinLock(&context->Lock);
    KeInitializeEvent(&context->RequestDrainedEvent,
                      NotificationEvent,
                      TRUE);
    NldBthSignalingOwnerInitialize(&context->Owner);
    NldBthTransferInitialize(&context->TransferOwner);
    context->LastOpenStatus = STATUS_NOT_SUPPORTED;
    context->LastCloseStatus = STATUS_NOT_SUPPORTED;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSignalingStart(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ const NLD_BTH_PROFILE_CONTEXT* profile,
    _In_ PDEVICE_OBJECT reference_device_object) {
    return NldBthChannelStart(context,
                              profile,
                              reference_device_object,
                              NLD_BTH_AVDTP_PSM);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthChannelStart(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ const NLD_BTH_PROFILE_CONTEXT* profile,
    _In_ PDEVICE_OBJECT reference_device_object,
    _In_ USHORT psm) {
    KIRQL old_irql;

    if (context == NULL || profile == NULL ||
        reference_device_object == NULL || psm == 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!NldBthProfileIsReady(profile)) return STATUS_DEVICE_NOT_READY;

    (void)KeWaitForSingleObject(&context->OperationMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    ObReferenceObject(reference_device_object);
    KeAcquireSpinLock(&context->Lock, &old_irql);
    if (context->Profile != NULL || context->ReferenceDeviceObject != NULL ||
        context->Owner.State != NldBthSignalingOffline) {
        KeReleaseSpinLock(&context->Lock, old_irql);
        ObDereferenceObject(reference_device_object);
        (void)KeReleaseMutex(&context->OperationMutex, FALSE);
        return STATUS_INVALID_DEVICE_STATE;
    }
    context->Profile = profile;
    context->ReferenceDeviceObject = reference_device_object;
    context->Psm = psm;
    (void)NldBthSignalingOwnerOnPnpStart(&context->Owner);
    context->LastOpenStatus = STATUS_NOT_SUPPORTED;
    context->LastCloseStatus = STATUS_NOT_SUPPORTED;
    KeReleaseSpinLock(&context->Lock, old_irql);
    (void)KeReleaseMutex(&context->OperationMutex, FALSE);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldBthSignalingSetDisconnectCallback(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_opt_ NLD_BTH_DISCONNECT_CALLBACK callback,
    _In_opt_ PVOID callback_context) {
    KIRQL old_irql;

    if (context == NULL) return;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    context->DisconnectCallback = callback;
    context->DisconnectCallbackContext = callback_context;
    KeReleaseSpinLock(&context->Lock, old_irql);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSignalingOpen(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ USHORT preferred_mtu) {
#if (NTDDI_VERSION >= NTDDI_WIN8)
    NLD_BTH_ASYNC_OPEN_REQUEST* request;
    struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL* open_channel;
    PIO_STACK_LOCATION stack;
    NLD_BTH_SIGNALING_ACTION action;
    const NLD_BTH_PROFILE_CONTEXT* profile;
    PDEVICE_OBJECT target_device_object;
    PDEVICE_OBJECT reference_device_object;
    KIRQL old_irql;

    if (context == NULL) return STATUS_INVALID_PARAMETER;
    if (preferred_mtu == 0u) preferred_mtu = (USHORT)L2CAP_DEFAULT_MTU;
    if (preferred_mtu < L2CAP_MIN_MTU ||
        preferred_mtu > NLD_BTH_MAX_SIGNALING_MTU) {
        return STATUS_INVALID_PARAMETER;
    }

    (void)KeWaitForSingleObject(&context->OperationMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    KeAcquireSpinLock(&context->Lock, &old_irql);
    profile = context->Profile;
    reference_device_object = context->ReferenceDeviceObject;
    target_device_object = profile == NULL
        ? NULL
        : profile->TargetDeviceObject;
    KeReleaseSpinLock(&context->Lock, old_irql);
    if (profile == NULL || target_device_object == NULL ||
        reference_device_object == NULL ||
        !NldBthProfileIsReady(profile)) {
        (void)KeReleaseMutex(&context->OperationMutex, FALSE);
        return STATUS_DEVICE_NOT_READY;
    }

    request = (NLD_BTH_ASYNC_OPEN_REQUEST*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(*request),
        NLD_BTH_SIGNALING_POOL_TAG);
    if (request == NULL) {
        (void)KeReleaseMutex(&context->OperationMutex, FALSE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(request, sizeof(*request));
    request->ReferenceCount = 1;
    request->Owner = context;
    request->Profile = profile;
    request->IoStatus.Status = STATUS_PENDING;
    request->OpenBrb = profile->Interface.BthAllocateBrb(
        BRB_L2CA_OPEN_ENHANCED_CHANNEL,
        NLD_BTH_SIGNALING_POOL_TAG);
    request->CloseBrb = profile->Interface.BthAllocateBrb(
        BRB_L2CA_CLOSE_CHANNEL,
        NLD_BTH_SIGNALING_POOL_TAG);
    request->Irp = IoAllocateIrp(target_device_object->StackSize, FALSE);
    request->WorkItem = IoAllocateWorkItem(reference_device_object);
    if (request->OpenBrb == NULL || request->CloseBrb == NULL ||
        request->Irp == NULL || request->WorkItem == NULL) {
        NldBthSignalingRequestRelease(request);
        (void)KeReleaseMutex(&context->OperationMutex, FALSE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    open_channel = (struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL*)
        request->OpenBrb;
    open_channel->BtAddress = profile->RemoteAddress;
    open_channel->Psm = context->Psm;
    open_channel->ChannelFlags = CF_ROLE_EITHER |
                                 CF_LINK_AUTHENTICATED |
                                 CF_LINK_ENCRYPTED;
    open_channel->ConfigOut.Flags = CFG_MTU;
    open_channel->ConfigOut.Mtu.Min = L2CAP_MIN_MTU;
    open_channel->ConfigOut.Mtu.Max = preferred_mtu;
    open_channel->ConfigOut.Mtu.Preferred = preferred_mtu;
    open_channel->ConfigIn.Flags = CFG_MTU;
    open_channel->ConfigIn.Mtu.Min = L2CAP_MIN_MTU;
    open_channel->ConfigIn.Mtu.Max = preferred_mtu;
    open_channel->ConfigIn.Mtu.Preferred = preferred_mtu;
    open_channel->CallbackFlags = CALLBACK_DISCONNECT;
    open_channel->Callback = NldBthSignalingIndicationCallback;
    open_channel->CallbackContext = request;
    open_channel->ReferenceObject = reference_device_object;
    open_channel->IncomingQueueDepth = 8u;

    request->Irp->RequestorMode = KernelMode;
    request->Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    request->Irp->IoStatus.Information = 0u;
    stack = IoGetNextIrpStackLocation(request->Irp);
    stack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    stack->Parameters.DeviceIoControl.IoControlCode =
        IOCTL_INTERNAL_BTH_SUBMIT_BRB;
    stack->Parameters.Others.Argument1 = request->OpenBrb;
    IoSetCompletionRoutine(request->Irp,
                           NldBthSignalingOpenCompletion,
                           request,
                           TRUE,
                           TRUE,
                           TRUE);

    KeAcquireSpinLock(&context->Lock, &old_irql);
    action = NldBthSignalingOwnerRequestOpen(&context->Owner);
    if (action != NldBthSignalingActionSubmitOpen ||
        context->PendingOpen != NULL || context->Profile != profile) {
        KeReleaseSpinLock(&context->Lock, old_irql);
        NldBthSignalingRequestRelease(request);
        (void)KeReleaseMutex(&context->OperationMutex, FALSE);
        return STATUS_DEVICE_BUSY;
    }
    request->Generation = context->Owner.Generation;
    context->PendingOpen = request;
    KeClearEvent(&context->RequestDrainedEvent);
    KeReleaseSpinLock(&context->Lock, old_irql);
    (void)IoCallDriver(target_device_object, request->Irp);
    (void)KeReleaseMutex(&context->OperationMutex, FALSE);
    return STATUS_PENDING;
#else
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(preferred_mtu);
    return STATUS_NOT_SUPPORTED;
#endif
}

static NTSTATUS NldBthSignalingTransferInternal(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ NLD_BTH_TRANSFER_DIRECTION direction,
    void* buffer,
    _In_ ULONG buffer_length,
    _In_ ULONG timeout_ms,
    _Out_opt_ ULONG* transferred) {
    struct _BRB_L2CA_ACL_TRANSFER* transfer_brb = NULL;
    const NLD_BTH_PROFILE_CONTEXT* profile;
    L2CAP_CHANNEL_HANDLE channel_handle;
    ULONG channel_generation;
    ULONG request_generation;
    ULONG mtu;
    ULONG actual = 0u;
    PVOID transfer_buffer = NULL;
    NTSTATUS status;
    NLD_BTH_TRANSFER_RESULT result;

    PAGED_CODE();
    if (transferred != NULL) *transferred = 0u;
    if (context == NULL || buffer == NULL || buffer_length == 0u ||
        buffer_length > NLD_BTH_MAX_SIGNALING_MTU ||
        (direction != NldBthTransferRead &&
         direction != NldBthTransferWrite)) {
        return STATUS_INVALID_PARAMETER;
    }

    result = NldBthSignalingBeginTransfer(context,
                                          direction,
                                          &profile,
                                          &channel_handle,
                                          &mtu,
                                          &channel_generation,
                                          &request_generation);
    if (result == NldBthTransferDisconnected) {
        return STATUS_CONNECTION_DISCONNECTED;
    }
    if (result == NldBthTransferBusy) return STATUS_DEVICE_BUSY;
    if (result != NldBthTransferOk || profile == NULL ||
        channel_handle == NULL || mtu == 0u) {
        return STATUS_DEVICE_NOT_READY;
    }
    if ((direction == NldBthTransferRead && buffer_length < mtu) ||
        (direction == NldBthTransferWrite && buffer_length > mtu)) {
        status = STATUS_BUFFER_TOO_SMALL;
        goto complete;
    }

    transfer_buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                      buffer_length,
                                      NLD_BTH_SIGNALING_POOL_TAG);
    if (transfer_buffer == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto complete;
    }
    if (direction == NldBthTransferWrite) {
        RtlCopyMemory(transfer_buffer, buffer, buffer_length);
    }
    transfer_brb = (struct _BRB_L2CA_ACL_TRANSFER*)
        profile->Interface.BthAllocateBrb(BRB_L2CA_ACL_TRANSFER,
                                          NLD_BTH_SIGNALING_POOL_TAG);
    if (transfer_brb == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto complete;
    }
    transfer_brb->BtAddress = profile->RemoteAddress;
    transfer_brb->ChannelHandle = channel_handle;
    transfer_brb->Buffer = transfer_buffer;
    transfer_brb->BufferMDL = NULL;
    transfer_brb->BufferSize = buffer_length;
    transfer_brb->RemainingBufferSize = buffer_length;
    transfer_brb->TransferFlags = direction == NldBthTransferRead
        ? ACL_TRANSFER_DIRECTION_IN | ACL_SHORT_TRANSFER_OK
        : ACL_TRANSFER_DIRECTION_OUT;
    /*
     * Keep the BthPort read semantics identical to the proven LdacNative
     * transport. NldBthProfileSubmitBrb supplies the bounded IRP wait and
     * cancellation policy; a second ACL_TRANSFER_TIMEOUT changes when a
     * short AVDTP response completes on some Bluetooth stacks.
     */
    transfer_brb->Timeout = 0ll;
    status = NldBthProfileSubmitBrb(profile,
                                    (PBRB)transfer_brb,
                                    sizeof(*transfer_brb),
                                    timeout_ms);
    if (NT_SUCCESS(status)) {
        if (transfer_brb->RemainingBufferSize > buffer_length) {
            status = STATUS_DEVICE_DATA_ERROR;
        } else {
            actual = buffer_length - transfer_brb->RemainingBufferSize;
            if (direction == NldBthTransferRead) {
                RtlCopyMemory(buffer, transfer_buffer, actual);
            }
            if (transferred != NULL) *transferred = actual;
        }
    }

complete:
    if (transfer_brb != NULL) {
        profile->Interface.BthFreeBrb((PBRB)transfer_brb);
    }
    if (transfer_buffer != NULL) {
        ExFreePoolWithTag(transfer_buffer, NLD_BTH_SIGNALING_POOL_TAG);
    }
    NldBthSignalingCompleteTransfer(context,
                                    direction,
                                    channel_generation,
                                    request_generation);
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSignalingWrite(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_reads_bytes_(buffer_length) const void* buffer,
    _In_ ULONG buffer_length,
    _In_ ULONG timeout_ms) {
    NTSTATUS status;

    PAGED_CODE();
    if (context == NULL || buffer == NULL || buffer_length == 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    (void)KeWaitForSingleObject(&context->OperationMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    status = NldBthSignalingTransferInternal(context,
                                             NldBthTransferWrite,
                                             (void*)buffer,
                                             buffer_length,
                                             timeout_ms,
                                             NULL);
    (void)KeReleaseMutex(&context->OperationMutex, FALSE);
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSignalingWriteGeneration(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ ULONG expected_generation,
    _In_reads_bytes_(buffer_length) const void* buffer,
    _In_ ULONG buffer_length,
    _In_ ULONG timeout_ms) {
    NLD_BTH_SIGNALING_SNAPSHOT snapshot;
    NTSTATUS status;

    PAGED_CODE();
    if (context == NULL || expected_generation == 0u ||
        buffer == NULL || buffer_length == 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    (void)KeWaitForSingleObject(&context->OperationMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    NldBthSignalingGetSnapshot(context, &snapshot);
    if (snapshot.State != NldBthSignalingChannelOpen ||
        snapshot.Generation != expected_generation) {
        status = STATUS_RETRY;
    } else if (snapshot.OutgoingMtu == 0u ||
               buffer_length > snapshot.OutgoingMtu) {
        status = STATUS_INVALID_BUFFER_SIZE;
    } else {
        status = NldBthSignalingTransferInternal(context,
                                                 NldBthTransferWrite,
                                                 (void*)buffer,
                                                 buffer_length,
                                                 timeout_ms,
                                                 NULL);
    }
    (void)KeReleaseMutex(&context->OperationMutex, FALSE);
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSignalingRead(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _Out_writes_bytes_(buffer_capacity) void* buffer,
    _In_ ULONG buffer_capacity,
    _In_ ULONG timeout_ms,
    _Out_ ULONG* bytes_read) {
    NTSTATUS status;

    PAGED_CODE();
    if (context == NULL || buffer == NULL || buffer_capacity == 0u ||
        bytes_read == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    (void)KeWaitForSingleObject(&context->OperationMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    status = NldBthSignalingTransferInternal(context,
                                             NldBthTransferRead,
                                             buffer,
                                             buffer_capacity,
                                             timeout_ms,
                                             bytes_read);
    (void)KeReleaseMutex(&context->OperationMutex, FALSE);
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSignalingDiscover(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _Out_writes_bytes_(response_capacity) unsigned char* response,
    _In_ ULONG response_capacity,
    _In_ ULONG timeout_ms,
    _Out_ ULONG* response_length,
    _Out_ ULONG* payload_offset) {
    NLD_AVDTP_TRANSACTION transaction;
    unsigned char command[2];
    size_t command_size = 0u;
    size_t parsed_payload_offset = 0u;
    unsigned long transaction_generation = 0ul;
    ULONG received = 0u;
    NTSTATUS status;
    NLD_AVDTP_TRANSACTION_STATUS transaction_status;

    PAGED_CODE();
    if (response_length != NULL) *response_length = 0u;
    if (payload_offset != NULL) *payload_offset = 0u;
    if (context == NULL || response == NULL || response_length == NULL ||
        payload_offset == NULL || response_capacity < 2u ||
        response_capacity > NLD_BTH_MAX_SIGNALING_MTU) {
        return STATUS_INVALID_PARAMETER;
    }
    if (timeout_ms == 0u) timeout_ms = NLD_BTH_DISCOVER_TIMEOUT_MS;
    if (timeout_ms > NLD_BTH_MAX_REQUEST_TIMEOUT_MS) {
        return STATUS_INVALID_PARAMETER;
    }

    NldAvdtpTransactionInitialize(&transaction);
    NldAvdtpTransactionReset(&transaction);
    transaction_status = NldAvdtpTransactionBegin(
        &transaction,
        NLD_AVDTP_SIGNAL_DISCOVER,
        command,
        sizeof(command),
        &command_size,
        &transaction_generation);
    status = NldBthSignalingMapAvdtpStatus(transaction_status);
    if (!NT_SUCCESS(status) || command_size != sizeof(command)) {
        return NT_SUCCESS(status) ? STATUS_DEVICE_PROTOCOL_ERROR : status;
    }

    (void)KeWaitForSingleObject(&context->OperationMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    status = NldBthSignalingTransferInternal(context,
                                             NldBthTransferWrite,
                                             command,
                                             (ULONG)command_size,
                                             timeout_ms,
                                             NULL);
    if (NT_SUCCESS(status)) {
        status = NldBthSignalingTransferInternal(context,
                                                 NldBthTransferRead,
                                                 response,
                                                 response_capacity,
                                                 timeout_ms,
                                                 &received);
    }
    (void)KeReleaseMutex(&context->OperationMutex, FALSE);
    if (!NT_SUCCESS(status)) return status;

    transaction_status = NldAvdtpTransactionAcceptResponse(
        &transaction,
        transaction_generation,
        response,
        (size_t)received,
        &parsed_payload_offset);
    status = NldBthSignalingMapAvdtpStatus(transaction_status);
    if (!NT_SUCCESS(status) || parsed_payload_offset > received) {
        return NT_SUCCESS(status) ? STATUS_DEVICE_PROTOCOL_ERROR : status;
    }
    *response_length = received;
    *payload_offset = (ULONG)parsed_payload_offset;
    return STATUS_SUCCESS;
}

static NTSTATUS NldBthSignalingCloseInternal(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ ULONG timeout_ms,
    _In_ BOOLEAN target_offline) {
    NLD_BTH_ASYNC_OPEN_REQUEST* pending_request = NULL;
    NLD_BTH_ASYNC_OPEN_REQUEST* active_request = NULL;
    const NLD_BTH_PROFILE_CONTEXT* profile;
    const NLD_BTH_PROFILE_CONTEXT* close_profile;
    NLD_BTH_SIGNALING_ACTION action;
    L2CAP_CHANNEL_HANDLE channel_handle = NULL;
    PBRB close_brb = NULL;
    KIRQL old_irql;
    NTSTATUS status = STATUS_SUCCESS;
    BOOLEAN wait_for_drain;

    if (context == NULL) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    (void)NldBthTransferRequestClose(&context->TransferOwner, FALSE);
    (void)NldBthTransferFinishClose(&context->TransferOwner);
    profile = context->Profile;
    action = NldBthSignalingOwnerRequestClose(&context->Owner,
                                              target_offline);
    if (action == NldBthSignalingActionCancelOpen &&
        context->PendingOpen != NULL) {
        pending_request = context->PendingOpen;
        NldBthSignalingRequestAddReference(pending_request);
    } else if (action == NldBthSignalingActionSubmitClose) {
        channel_handle = context->ChannelHandle;
        active_request = context->ActiveChannel;
        context->ActiveChannel = NULL;
        if (active_request != NULL) {
            close_brb = active_request->CloseBrb;
            active_request->CloseBrb = NULL;
        }
        context->ChannelHandle = NULL;
        context->IncomingMtu = 0u;
        context->OutgoingMtu = 0u;
    } else if (context->ActiveChannel != NULL &&
               context->ChannelHandle == NULL) {
        active_request = context->ActiveChannel;
        context->ActiveChannel = NULL;
        close_brb = active_request->CloseBrb;
        active_request->CloseBrb = NULL;
    }
    wait_for_drain = KeReadStateEvent(&context->RequestDrainedEvent) == 0;
    KeReleaseSpinLock(&context->Lock, old_irql);

    if (pending_request != NULL) {
        (void)IoCancelIrp(pending_request->Irp);
        (void)KeWaitForSingleObject(&context->RequestDrainedEvent,
                                    Executive,
                                    KernelMode,
                                    FALSE,
                                    NULL);
        NldBthSignalingRequestRelease(pending_request);
        wait_for_drain = FALSE;
    }

    close_profile = active_request != NULL
        ? active_request->Profile
        : profile;
    if (close_brb != NULL) {
        if (action == NldBthSignalingActionSubmitClose &&
            channel_handle != NULL && close_profile != NULL) {
            status = NldBthSignalingSubmitCloseBrb(close_profile,
                                                   close_brb,
                                                   channel_handle,
                                                   timeout_ms);
        }
        if (close_profile != NULL) {
            close_profile->Interface.BthFreeBrb(close_brb);
        }
    }
    if (active_request != NULL && active_request->OpenBrb != NULL) {
        /*
         * Free the retained OPEN BRB while the profile interface is still
         * valid so the final request release (possibly triggered later by
         * BTHport's IndicationReleaseReference) never touches a stale
         * profile pointer.
         */
        if (close_profile != NULL) {
            close_profile->Interface.BthFreeBrb(active_request->OpenBrb);
        }
        active_request->OpenBrb = NULL;
    }
    if (action == NldBthSignalingActionSubmitClose) {
        KeAcquireSpinLock(&context->Lock, &old_irql);
        context->LastCloseStatus = status;
        NldBthSignalingOwnerOnCloseComplete(&context->Owner,
                                            NT_SUCCESS(status));
        context->ChannelHandle = NULL;
        context->IncomingMtu = 0u;
        context->OutgoingMtu = 0u;
        KeReleaseSpinLock(&context->Lock, old_irql);
    }
    if (active_request != NULL) {
        NldBthSignalingRequestRelease(active_request);
    }

    if (wait_for_drain) {
        (void)KeWaitForSingleObject(&context->RequestDrainedEvent,
                                    Executive,
                                    KernelMode,
                                    FALSE,
                                    NULL);
    }
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSignalingClose(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ ULONG timeout_ms) {
    NTSTATUS status;

    if (context == NULL) return STATUS_INVALID_PARAMETER;
    (void)KeWaitForSingleObject(&context->OperationMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    status = NldBthSignalingCloseInternal(context,
                                          timeout_ms,
                                          FALSE);
    (void)KeReleaseMutex(&context->OperationMutex, FALSE);
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSignalingWaitForRequestDrain(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ ULONG timeout_ms) {
    LARGE_INTEGER timeout;
    NTSTATUS status;

    if (context == NULL) return STATUS_INVALID_PARAMETER;
    if (timeout_ms == 0u) timeout_ms = NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS;
    if (timeout_ms > NLD_BTH_MAX_REQUEST_TIMEOUT_MS) {
        return STATUS_INVALID_PARAMETER;
    }
    timeout.QuadPart = -((LONGLONG)timeout_ms * 10ll * 1000ll);
    status = KeWaitForSingleObject(&context->RequestDrainedEvent,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   &timeout);
    return status == STATUS_TIMEOUT ? STATUS_IO_TIMEOUT : status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
void NldBthSignalingStop(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ ULONG timeout_ms) {
    PDEVICE_OBJECT reference_device_object;
    KIRQL old_irql;

    if (context == NULL) return;
    (void)KeWaitForSingleObject(&context->OperationMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    (void)NldBthSignalingCloseInternal(context,
                                       timeout_ms,
                                       TRUE);
    KeAcquireSpinLock(&context->Lock, &old_irql);
    reference_device_object = context->ReferenceDeviceObject;
    context->ReferenceDeviceObject = NULL;
    context->Profile = NULL;
    context->Psm = 0u;
    context->DisconnectCallback = NULL;
    context->DisconnectCallbackContext = NULL;
    KeReleaseSpinLock(&context->Lock, old_irql);
    if (reference_device_object != NULL) {
        ObDereferenceObject(reference_device_object);
    }
    (void)KeReleaseMutex(&context->OperationMutex, FALSE);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldBthSignalingGetSnapshot(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _Out_ PNLD_BTH_SIGNALING_SNAPSHOT snapshot) {
    KIRQL old_irql;

    if (context == NULL || snapshot == NULL) return;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    snapshot->State = context->Owner.State;
    snapshot->Generation = context->Owner.Generation;
    snapshot->Psm = context->Psm;
    snapshot->IncomingMtu = context->IncomingMtu;
    snapshot->OutgoingMtu = context->OutgoingMtu;
    snapshot->OpenPending = context->PendingOpen != NULL;
    snapshot->ChannelHeld = context->ChannelHandle != NULL;
    snapshot->RemoteDisconnected =
        context->TransferOwner.RemoteDisconnected != 0;
    snapshot->LastOpenStatus = context->LastOpenStatus;
    snapshot->LastCloseStatus = context->LastCloseStatus;
    KeReleaseSpinLock(&context->Lock, old_irql);
}
