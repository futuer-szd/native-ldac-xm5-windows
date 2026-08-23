// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_bth_request.h"

#include <bthioctl.h>

typedef struct _NLD_BTH_SYNC_REQUEST_CONTEXT {
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
} NLD_BTH_SYNC_REQUEST_CONTEXT, *PNLD_BTH_SYNC_REQUEST_CONTEXT;

static NTSTATUS NldBthSyncRequestCompletion(
    _In_ PDEVICE_OBJECT device_object,
    _In_ PIRP irp,
    _In_ PVOID completion_context);

static NTSTATUS NldBthSendInternalRequestSynchronously(
    _In_ PDEVICE_OBJECT target_device_object,
    _In_ ULONG io_control_code,
    _In_opt_ PVOID argument1,
    _Out_writes_bytes_opt_(output_buffer_length) PVOID output_buffer,
    _In_ ULONG output_buffer_length,
    _In_ ULONG timeout_ms);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, NldBthQueryRemoteDeviceInfoSynchronously)
#pragma alloc_text(PAGE, NldBthSubmitBrbSynchronously)
#pragma alloc_text(PAGE, NldBthSendInternalRequestSynchronously)
#endif

static NTSTATUS NldBthSyncRequestCompletion(
    _In_ PDEVICE_OBJECT device_object,
    _In_ PIRP irp,
    _In_ PVOID completion_context) {
    PNLD_BTH_SYNC_REQUEST_CONTEXT context =
        (PNLD_BTH_SYNC_REQUEST_CONTEXT)completion_context;

    UNREFERENCED_PARAMETER(device_object);
    context->IoStatus = irp->IoStatus;
    KeSetEvent(&context->Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS NldBthSendInternalRequestSynchronously(
    _In_ PDEVICE_OBJECT target_device_object,
    _In_ ULONG io_control_code,
    _In_opt_ PVOID argument1,
    _Out_writes_bytes_opt_(output_buffer_length) PVOID output_buffer,
    _In_ ULONG output_buffer_length,
    _In_ ULONG timeout_ms) {
    NLD_BTH_SYNC_REQUEST_CONTEXT context;
    PIO_STACK_LOCATION stack;
    LARGE_INTEGER timeout;
    PIRP irp;
    NTSTATUS call_status;
    NTSTATUS wait_status;
    NTSTATUS result;

    PAGED_CODE();
    if (target_device_object == NULL ||
        (output_buffer_length != 0u && output_buffer == NULL)) {
        return STATUS_INVALID_PARAMETER;
    }
    if (timeout_ms == 0u) timeout_ms = NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS;
    if (timeout_ms > NLD_BTH_MAX_REQUEST_TIMEOUT_MS) {
        return STATUS_INVALID_PARAMETER;
    }

    KeInitializeEvent(&context.Event, NotificationEvent, FALSE);
    context.IoStatus.Status = STATUS_PENDING;
    context.IoStatus.Information = 0u;
    irp = IoAllocateIrp(target_device_object->StackSize, FALSE);
    if (irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    irp->RequestorMode = KernelMode;
    irp->Tail.Overlay.Thread = PsGetCurrentThread();
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0u;
    irp->UserBuffer = output_buffer;
    stack = IoGetNextIrpStackLocation(irp);
    stack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    stack->Parameters.DeviceIoControl.IoControlCode = io_control_code;
    if (argument1 != NULL) {
        stack->Parameters.Others.Argument1 = argument1;
    } else {
        stack->Parameters.DeviceIoControl.OutputBufferLength =
            output_buffer_length;
        stack->Parameters.DeviceIoControl.InputBufferLength = 0u;
        stack->Parameters.DeviceIoControl.Type3InputBuffer = NULL;
    }

    IoSetCompletionRoutine(irp,
                           NldBthSyncRequestCompletion,
                           &context,
                           TRUE,
                           TRUE,
                           TRUE);
    call_status = IoCallDriver(target_device_object, irp);
    timeout.QuadPart = -((LONGLONG)timeout_ms * 10ll * 1000ll);
    wait_status = KeWaitForSingleObject(&context.Event,
                                        Executive,
                                        KernelMode,
                                        FALSE,
                                        &timeout);
    if (wait_status == STATUS_TIMEOUT) {
        (void)IoCancelIrp(irp);
        (void)KeWaitForSingleObject(&context.Event,
                                    Executive,
                                    KernelMode,
                                    FALSE,
                                    NULL);
        result = STATUS_IO_TIMEOUT;
    } else if (!NT_SUCCESS(wait_status)) {
        (void)IoCancelIrp(irp);
        (void)KeWaitForSingleObject(&context.Event,
                                    Executive,
                                    KernelMode,
                                    FALSE,
                                    NULL);
        result = wait_status;
    } else {
        result = context.IoStatus.Status;
        if (result == STATUS_PENDING) result = call_status;
    }

    IoFreeIrp(irp);
    return result;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthQueryRemoteDeviceInfoSynchronously(
    _In_ PDEVICE_OBJECT target_device_object,
    _Out_ PBTH_DEVICE_INFO device_info,
    _In_ ULONG timeout_ms) {
    PAGED_CODE();
    if (device_info == NULL) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(device_info, sizeof(*device_info));
    return NldBthSendInternalRequestSynchronously(
        target_device_object,
        IOCTL_INTERNAL_BTHENUM_GET_DEVINFO,
        NULL,
        device_info,
        (ULONG)sizeof(*device_info),
        timeout_ms);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSubmitBrbSynchronously(
    _In_ PDEVICE_OBJECT target_device_object,
    _Inout_ PBRB brb,
    _In_ SIZE_T brb_size,
    _In_ ULONG timeout_ms) {
    PAGED_CODE();
    if (brb == NULL || brb_size == 0u || brb_size > MAXULONG ||
        brb->BrbHeader.Length < (ULONG)brb_size) {
        return STATUS_INVALID_PARAMETER;
    }
    return NldBthSendInternalRequestSynchronously(
        target_device_object,
        IOCTL_INTERNAL_BTH_SUBMIT_BRB,
        brb,
        NULL,
        0u,
        timeout_ms);
}
