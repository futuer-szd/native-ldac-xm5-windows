// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_direct_pdo_diagnostic.h"

#define NLD_DIRECT_PDO_DIAGNOSTIC_POOL_TAG 'DddL'

static IO_WORKITEM_ROUTINE NldDirectPdoDiagnosticRuntimeWorker;

static NTSTATUS NldDirectPdoDiagnosticRuntimeExecute(
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT context,
    _In_ NLD_DIRECT_PDO_DIAGNOSTIC_ACTION action,
    _Out_ ULONG* response_length,
    _Out_ ULONG* payload_offset);

static NTSTATUS NldDirectPdoDiagnosticRuntimeExecute(
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT context,
    _In_ NLD_DIRECT_PDO_DIAGNOSTIC_ACTION action,
    _Out_ ULONG* response_length,
    _Out_ ULONG* payload_offset) {
    NLD_BTH_SIGNALING_SNAPSHOT signaling_snapshot;
    NTSTATUS status;

    *response_length = 0u;
    *payload_offset = 0u;
    switch (action) {
        case NldDirectPdoDiagnosticActionOpen:
            status = NldBthSignalingOpen(context->Signaling,
                                         (USHORT)L2CAP_DEFAULT_MTU);
            if (status == STATUS_PENDING) {
                status = NldBthSignalingWaitForRequestDrain(
                    context->Signaling,
                    NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);
            }
            if (NT_SUCCESS(status)) {
                NldBthSignalingGetSnapshot(context->Signaling,
                                           &signaling_snapshot);
                if (signaling_snapshot.State !=
                    NldBthSignalingChannelOpen) {
                    status = NT_SUCCESS(signaling_snapshot.LastOpenStatus)
                        ? STATUS_DEVICE_NOT_READY
                        : signaling_snapshot.LastOpenStatus;
                }
            }
            if (!NT_SUCCESS(status)) {
                (void)NldBthSignalingClose(
                    context->Signaling,
                    NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);
            }
            return status;

        case NldDirectPdoDiagnosticActionDiscover:
            return NldBthSignalingDiscover(
                context->Signaling,
                context->ResponseBuffer,
                NLD_BTH_MAX_SIGNALING_MTU,
                NLD_BTH_DISCOVER_TIMEOUT_MS,
                response_length,
                payload_offset);

        case NldDirectPdoDiagnosticActionClose:
        case NldDirectPdoDiagnosticActionCancelAndClose:
            return NldBthSignalingClose(
                context->Signaling,
                NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

static VOID NldDirectPdoDiagnosticRuntimeWorker(
    _In_ PDEVICE_OBJECT device_object,
    _In_opt_ PVOID work_item_context) {
    PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT context =
        (PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT)work_item_context;
    NLD_DIRECT_PDO_DIAGNOSTIC_ACTION action;
    ULONG generation;
    ULONG response_length;
    ULONG payload_offset;
    ULONG prefix_length;
    KIRQL old_irql;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(device_object);
    if (context == NULL) return;

    for (;;) {
        KeAcquireSpinLock(&context->Lock, &old_irql);
        action = NldDirectPdoDiagnosticTakeAction(&context->Owner,
                                                  &generation);
        if (action == NldDirectPdoDiagnosticActionNone) {
            if (context->Arbiter != NULL &&
                context->ArbiterGeneration != 0u) {
                NLD_BTH_SIGNALING_SNAPSHOT signaling_snapshot;

                NldBthSignalingGetSnapshot(context->Signaling,
                                           &signaling_snapshot);
                if (signaling_snapshot.State ==
                    NldBthSignalingClosed) {
                    (void)NldDirectPdoArbiterRuntimeRelease(
                        context->Arbiter,
                        NldDirectPdoArbiterClientDiagnostic,
                        context->ArbiterGeneration);
                    context->ArbiterGeneration = 0u;
                }
            }
            KeSetEvent(&context->IdleEvent, IO_NO_INCREMENT, FALSE);
            KeReleaseSpinLock(&context->Lock, old_irql);
            return;
        }
        KeReleaseSpinLock(&context->Lock, old_irql);

        status = NldDirectPdoDiagnosticRuntimeExecute(context,
                                                       action,
                                                       &response_length,
                                                       &payload_offset);

        KeAcquireSpinLock(&context->Lock, &old_irql);
        context->LastAction = action;
        context->LastStatus = status;
        if (action == NldDirectPdoDiagnosticActionDiscover) {
            context->DiscoverStatus = status;
            context->ResponseLength = 0u;
            context->PayloadOffset = 0u;
            context->ResponseTruncated = FALSE;
            RtlZeroMemory(context->ResponsePrefix,
                          sizeof(context->ResponsePrefix));
            if (NT_SUCCESS(status)) {
                prefix_length = response_length <
                        NLD_DIRECT_PDO_DIAGNOSTIC_RESPONSE_PREFIX
                    ? response_length
                    : NLD_DIRECT_PDO_DIAGNOSTIC_RESPONSE_PREFIX;
                RtlCopyMemory(context->ResponsePrefix,
                              context->ResponseBuffer,
                              prefix_length);
                context->ResponseLength = response_length;
                context->PayloadOffset = payload_offset;
                context->ResultGeneration = generation;
                context->ResponseTruncated = response_length >
                    NLD_DIRECT_PDO_DIAGNOSTIC_RESPONSE_PREFIX;
            }
        } else if (action == NldDirectPdoDiagnosticActionClose ||
                   action ==
                       NldDirectPdoDiagnosticActionCancelAndClose) {
            context->CloseStatus = status;
        }
        (void)NldDirectPdoDiagnosticCompleteAction(&context->Owner,
                                                    generation,
                                                    action,
                                                    NT_SUCCESS(status));
        KeReleaseSpinLock(&context->Lock, old_irql);
    }
}

void NldDirectPdoDiagnosticRuntimeInitialize(
    _Out_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT context) {
    if (context == NULL) return;
    RtlZeroMemory(context, sizeof(*context));
    KeInitializeSpinLock(&context->Lock);
    KeInitializeEvent(&context->IdleEvent, NotificationEvent, TRUE);
    NldDirectPdoDiagnosticInitialize(&context->Owner);
    context->LastAction = NldDirectPdoDiagnosticActionNone;
    context->LastStatus = STATUS_NOT_SUPPORTED;
    context->DiscoverStatus = STATUS_NOT_SUPPORTED;
    context->CloseStatus = STATUS_NOT_SUPPORTED;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldDirectPdoDiagnosticRuntimeStart(
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT context,
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT signaling,
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT arbiter,
    _In_ PDEVICE_OBJECT reference_device_object) {
    NLD_DIRECT_PDO_ARBITER_SNAPSHOT arbiter_snapshot;
    NLD_BTH_SIGNALING_SNAPSHOT signaling_snapshot;
    PIO_WORKITEM work_item;
    unsigned char* response_buffer;
    KIRQL old_irql;

    if (context == NULL || signaling == NULL || arbiter == NULL ||
        reference_device_object == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    NldDirectPdoArbiterRuntimeGetSnapshot(arbiter,
                                          &arbiter_snapshot);
    if (!arbiter_snapshot.Started || !arbiter_snapshot.PnpStarted ||
        arbiter_snapshot.StopRequested) {
        return STATUS_DEVICE_NOT_READY;
    }
    NldBthSignalingGetSnapshot(signaling, &signaling_snapshot);
    if (signaling_snapshot.State != NldBthSignalingClosed) {
        return STATUS_DEVICE_NOT_READY;
    }

    work_item = IoAllocateWorkItem(reference_device_object);
    response_buffer = (unsigned char*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        NLD_BTH_MAX_SIGNALING_MTU,
        NLD_DIRECT_PDO_DIAGNOSTIC_POOL_TAG);
    if (work_item == NULL || response_buffer == NULL) {
        if (work_item != NULL) IoFreeWorkItem(work_item);
        if (response_buffer != NULL) {
            ExFreePoolWithTag(response_buffer,
                              NLD_DIRECT_PDO_DIAGNOSTIC_POOL_TAG);
        }
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(response_buffer, NLD_BTH_MAX_SIGNALING_MTU);
    ObReferenceObject(reference_device_object);

    KeAcquireSpinLock(&context->Lock, &old_irql);
    if (context->Started || context->WorkItem != NULL ||
        context->ReferenceDeviceObject != NULL ||
        context->ResponseBuffer != NULL) {
        KeReleaseSpinLock(&context->Lock, old_irql);
        ObDereferenceObject(reference_device_object);
        ExFreePoolWithTag(response_buffer,
                          NLD_DIRECT_PDO_DIAGNOSTIC_POOL_TAG);
        IoFreeWorkItem(work_item);
        return STATUS_INVALID_DEVICE_STATE;
    }
    context->Signaling = signaling;
    context->Arbiter = arbiter;
    context->ReferenceDeviceObject = reference_device_object;
    context->WorkItem = work_item;
    context->ResponseBuffer = response_buffer;
    context->Started = TRUE;
    context->LastAction = NldDirectPdoDiagnosticActionNone;
    context->LastStatus = STATUS_NOT_SUPPORTED;
    context->DiscoverStatus = STATUS_NOT_SUPPORTED;
    context->CloseStatus = STATUS_NOT_SUPPORTED;
    context->ResponseLength = 0u;
    context->PayloadOffset = 0u;
    context->ResultGeneration = 0u;
    context->ArbiterGeneration = 0u;
    context->ResponseTruncated = FALSE;
    RtlZeroMemory(context->ResponsePrefix,
                  sizeof(context->ResponsePrefix));
    KeSetEvent(&context->IdleEvent, IO_NO_INCREMENT, FALSE);
    (void)NldDirectPdoDiagnosticOnPnpStart(&context->Owner);
    KeReleaseSpinLock(&context->Lock, old_irql);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS NldDirectPdoDiagnosticRuntimeRequestDiscover(
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT context) {
    NLD_DIRECT_PDO_DIAGNOSTIC_COMMAND command;
    NLD_DIRECT_PDO_ARBITER_ACQUIRE_RESULT acquire_result;
    ULONG arbiter_generation = 0u;
    PIO_WORKITEM work_item = NULL;
    KIRQL old_irql;

    if (context == NULL) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    if (!context->Started || context->WorkItem == NULL ||
        context->ResponseBuffer == NULL ||
        context->Owner.StopRequested) {
        KeReleaseSpinLock(&context->Lock, old_irql);
        return STATUS_DEVICE_NOT_READY;
    }
    if (context->ArbiterGeneration != 0u) {
        KeReleaseSpinLock(&context->Lock, old_irql);
        return STATUS_DEVICE_BUSY;
    }
    acquire_result = NldDirectPdoArbiterRuntimeTryAcquire(
        context->Arbiter,
        NldDirectPdoArbiterClientDiagnostic,
        &arbiter_generation);
    if (acquire_result == NldDirectPdoArbiterAcquireRejected) {
        KeReleaseSpinLock(&context->Lock, old_irql);
        return STATUS_DEVICE_BUSY;
    }
    context->ArbiterGeneration = arbiter_generation;
    command = NldDirectPdoDiagnosticRequestDiscover(&context->Owner);
    if (command == NldDirectPdoDiagnosticCommandQueueWorker) {
        context->LastStatus = STATUS_PENDING;
        context->DiscoverStatus = STATUS_PENDING;
        context->CloseStatus = STATUS_PENDING;
        context->ResponseLength = 0u;
        context->PayloadOffset = 0u;
        context->ResultGeneration = context->Owner.Generation;
        context->ResponseTruncated = FALSE;
        RtlZeroMemory(context->ResponsePrefix,
                      sizeof(context->ResponsePrefix));
        KeClearEvent(&context->IdleEvent);
        work_item = context->WorkItem;
    }
    if (command != NldDirectPdoDiagnosticCommandQueueWorker) {
        (void)NldDirectPdoArbiterRuntimeRelease(
            context->Arbiter,
            NldDirectPdoArbiterClientDiagnostic,
            context->ArbiterGeneration);
        context->ArbiterGeneration = 0u;
    }
    KeReleaseSpinLock(&context->Lock, old_irql);

    if (work_item == NULL) return STATUS_DEVICE_BUSY;
    IoQueueWorkItem(work_item,
                    NldDirectPdoDiagnosticRuntimeWorker,
                    DelayedWorkQueue,
                    context);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldDirectPdoDiagnosticRuntimePreempt(
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT context,
    _In_ ULONG timeout_ms) {
    NLD_DIRECT_PDO_DIAGNOSTIC_COMMAND command;
    LARGE_INTEGER timeout;
    PNLD_BTH_SIGNALING_CONTEXT signaling;
    PIO_WORKITEM work_item = NULL;
    KIRQL old_irql;
    NTSTATUS status;
    BOOLEAN cancel_active;

    if (context == NULL || timeout_ms == 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    KeAcquireSpinLock(&context->Lock, &old_irql);
    if (!context->Started || context->WorkItem == NULL ||
        context->Signaling == NULL || context->Owner.StopRequested) {
        KeReleaseSpinLock(&context->Lock, old_irql);
        return STATUS_DEVICE_NOT_READY;
    }
    signaling = context->Signaling;
    command = NldDirectPdoDiagnosticRequestCancel(&context->Owner);
    cancel_active = command ==
        NldDirectPdoDiagnosticCommandCancelActive;
    if (command == NldDirectPdoDiagnosticCommandQueueWorker) {
        KeClearEvent(&context->IdleEvent);
        work_item = context->WorkItem;
    }
    if (command == NldDirectPdoDiagnosticCommandNone &&
        !context->Owner.CancelRequested) {
        KeReleaseSpinLock(&context->Lock, old_irql);
        return STATUS_DEVICE_BUSY;
    }
    KeReleaseSpinLock(&context->Lock, old_irql);

    if (work_item != NULL) {
        IoQueueWorkItem(work_item,
                        NldDirectPdoDiagnosticRuntimeWorker,
                        DelayedWorkQueue,
                        context);
    }
    if (cancel_active) {
        (void)NldBthSignalingClose(signaling, timeout_ms);
    }
    timeout.QuadPart = -((LONGLONG)timeout_ms * 10000ll);
    status = KeWaitForSingleObject(&context->IdleEvent,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   &timeout);
    if (status == STATUS_TIMEOUT) return STATUS_IO_TIMEOUT;
    if (!NT_SUCCESS(status)) return status;

    KeAcquireSpinLock(&context->Lock, &old_irql);
    if (!context->Started || context->Owner.StopRequested) {
        status = STATUS_DEVICE_NOT_READY;
    } else if (context->Owner.State ==
                   NldDirectPdoDiagnosticIdle &&
               context->ArbiterGeneration == 0u) {
        status = STATUS_SUCCESS;
    } else if (!NT_SUCCESS(context->CloseStatus)) {
        status = context->CloseStatus;
    } else {
        status = STATUS_DEVICE_BUSY;
    }
    KeReleaseSpinLock(&context->Lock, old_irql);
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
void NldDirectPdoDiagnosticRuntimeStop(
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT context,
    _In_ ULONG timeout_ms) {
    NLD_DIRECT_PDO_DIAGNOSTIC_COMMAND command;
    PNLD_BTH_SIGNALING_CONTEXT signaling;
    PDEVICE_OBJECT reference_device_object;
    PIO_WORKITEM work_item;
    unsigned char* response_buffer;
    KIRQL old_irql;
    BOOLEAN cancel_active;
    BOOLEAN queue_worker;

    if (context == NULL) return;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    if (!context->Started) {
        KeReleaseSpinLock(&context->Lock, old_irql);
        return;
    }
    signaling = context->Signaling;
    work_item = context->WorkItem;
    command = NldDirectPdoDiagnosticOnPnpStop(&context->Owner);
    cancel_active = command ==
        NldDirectPdoDiagnosticCommandCancelActive;
    queue_worker = command ==
        NldDirectPdoDiagnosticCommandQueueWorker;
    if (queue_worker) KeClearEvent(&context->IdleEvent);
    KeReleaseSpinLock(&context->Lock, old_irql);

    if (queue_worker) {
        IoQueueWorkItem(work_item,
                        NldDirectPdoDiagnosticRuntimeWorker,
                        DelayedWorkQueue,
                        context);
    }
    if (cancel_active && signaling != NULL) {
        (void)NldBthSignalingClose(signaling, timeout_ms);
    }
    (void)KeWaitForSingleObject(&context->IdleEvent,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);

    KeAcquireSpinLock(&context->Lock, &old_irql);
    if (context->Arbiter != NULL &&
        context->ArbiterGeneration != 0u) {
        (void)NldDirectPdoArbiterRuntimeRelease(
            context->Arbiter,
            NldDirectPdoArbiterClientDiagnostic,
            context->ArbiterGeneration);
        context->ArbiterGeneration = 0u;
    }
    reference_device_object = context->ReferenceDeviceObject;
    work_item = context->WorkItem;
    response_buffer = context->ResponseBuffer;
    context->Signaling = NULL;
    context->Arbiter = NULL;
    context->ReferenceDeviceObject = NULL;
    context->WorkItem = NULL;
    context->ResponseBuffer = NULL;
    context->Started = FALSE;
    KeReleaseSpinLock(&context->Lock, old_irql);

    if (response_buffer != NULL) {
        ExFreePoolWithTag(response_buffer,
                          NLD_DIRECT_PDO_DIAGNOSTIC_POOL_TAG);
    }
    if (work_item != NULL) IoFreeWorkItem(work_item);
    if (reference_device_object != NULL) {
        ObDereferenceObject(reference_device_object);
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldDirectPdoDiagnosticRuntimeGetSnapshot(
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT context,
    _Out_ PNLD_DIRECT_PDO_DIAGNOSTIC_SNAPSHOT snapshot) {
    ULONG prefix_length;
    KIRQL old_irql;

    if (context == NULL || snapshot == NULL) return;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    snapshot->State = context->Owner.State;
    snapshot->PendingAction = context->Owner.PendingAction;
    snapshot->ActiveAction = context->Owner.ActiveAction;
    snapshot->LastAction = context->LastAction;
    snapshot->Generation = context->Owner.Generation;
    snapshot->ResultGeneration = context->ResultGeneration;
    snapshot->ArbiterGeneration = context->ArbiterGeneration;
    snapshot->ResponseLength = context->ResponseLength;
    snapshot->PayloadOffset = context->PayloadOffset;
    prefix_length = context->ResponseLength <
            NLD_DIRECT_PDO_DIAGNOSTIC_RESPONSE_PREFIX
        ? context->ResponseLength
        : NLD_DIRECT_PDO_DIAGNOSTIC_RESPONSE_PREFIX;
    snapshot->ResponsePrefixLength = prefix_length;
    snapshot->LastStatus = context->LastStatus;
    snapshot->DiscoverStatus = context->DiscoverStatus;
    snapshot->CloseStatus = context->CloseStatus;
    RtlCopyMemory(snapshot->ResponsePrefix,
                  context->ResponsePrefix,
                  sizeof(snapshot->ResponsePrefix));
    snapshot->WorkerOwned = context->Owner.WorkerOwned != 0;
    snapshot->StopRequested = context->Owner.StopRequested != 0;
    snapshot->CancelRequested = context->Owner.CancelRequested != 0;
    snapshot->ResponseTruncated = context->ResponseTruncated;
    snapshot->Started = context->Started;
    KeReleaseSpinLock(&context->Lock, old_irql);
}
