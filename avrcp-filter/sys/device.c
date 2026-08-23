// SPDX-License-Identifier: Apache-2.0
#include "device.h"

#define NLD_AVRCP_FILTER_VERSION_FLAGS \
    (NLD_AVRCP_FILTER_VERSION_PASS_THROUGH | \
     NLD_AVRCP_FILTER_VERSION_READ_ONLY_CONTROL | \
     NLD_AVRCP_FILTER_VERSION_RAW_PREFIX | \
     NLD_AVRCP_FILTER_VERSION_DEVICE_CONTROL | \
     NLD_AVRCP_FILTER_VERSION_INTERNAL_CONTROL | \
     NLD_AVRCP_FILTER_VERSION_ABSOLUTE_VOLUME_WRITE)

#define NLD_AVRCP_MICROSOFT_AVRCP_WRITE_IOCTL 0x00414014u

static NTSTATUS NldAvrcpFilterSendAbsoluteVolume(
    _In_ WDFDEVICE filter_device,
    _In_ UCHAR volume) {
    PNLD_AVRCP_FILTER_DEVICE_CONTEXT filter_context;
    WDF_OBJECT_ATTRIBUTES request_attributes;
    WDF_OBJECT_ATTRIBUTES input_attributes;
    WDF_OBJECT_ATTRIBUTES output_attributes;
    WDFREQUEST request = NULL;
    WDFMEMORY input_memory = NULL;
    WDFMEMORY output_memory = NULL;
    WDF_REQUEST_SEND_OPTIONS options;
    UCHAR input[64] = {0u};
    UCHAR output[64] = {0u};
    ULONG length;
    UCHAR label;
    NTSTATUS status;

    filter_context = NldAvrcpFilterGetDeviceContext(filter_device);
    if (filter_context == NULL || !filter_context->Online ||
        filter_context->TraceLock == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    WdfSpinLockAcquire(filter_context->TraceLock);
    if (filter_context->WriteActive) {
        WdfSpinLockRelease(filter_context->TraceLock);
        return STATUS_DEVICE_BUSY;
    }
    filter_context->WriteActive = TRUE;
    label = (UCHAR)(filter_context->NextTransactionLabel & 0x0Fu);
    filter_context->NextTransactionLabel =
        (UCHAR)((filter_context->NextTransactionLabel + 1u) & 0x0Fu);
    WdfSpinLockRelease(filter_context->TraceLock);

    /* Microsoft.Bluetooth.AvrcpTransport's observed private write layout:
       8-byte private header followed by one AVCTP SetAbsoluteVolume frame. */
    input[0] = 1u;
    input[4] = 14u;
    input[8] = (UCHAR)(label << 4u);
    input[9] = 0x11u;
    input[10] = 0x0Eu;
    input[11] = 0x00u;
    input[12] = 0x48u;
    input[13] = 0x00u;
    input[14] = 0x00u;
    input[15] = 0x19u;
    input[16] = 0x58u;
    input[17] = 0x50u;
    input[18] = 0x00u;
    input[19] = 0x00u;
    input[20] = 0x01u;
    input[21] = (UCHAR)(volume & 0x7Fu);
    length = 22u;

    WDF_OBJECT_ATTRIBUTES_INIT(&request_attributes);
    status = WdfRequestCreate(
        &request_attributes,
        WdfDeviceGetIoTarget(filter_device),
        &request);
    if (!NT_SUCCESS(status)) goto Exit;
    WDF_OBJECT_ATTRIBUTES_INIT(&input_attributes);
    input_attributes.ParentObject = request;
    status = WdfMemoryCreatePreallocated(
        &input_attributes, input, length, &input_memory);
    if (!NT_SUCCESS(status)) goto Exit;
    WDF_OBJECT_ATTRIBUTES_INIT(&output_attributes);
    output_attributes.ParentObject = request;
    status = WdfMemoryCreatePreallocated(
        &output_attributes, output, sizeof(output), &output_memory);
    if (!NT_SUCCESS(status)) goto Exit;
    status = WdfIoTargetFormatRequestForIoctl(
        WdfDeviceGetIoTarget(filter_device),
        request,
        NLD_AVRCP_MICROSOFT_AVRCP_WRITE_IOCTL,
        input_memory,
        NULL,
        output_memory,
        NULL);
    if (!NT_SUCCESS(status)) goto Exit;
    WDF_REQUEST_SEND_OPTIONS_INIT(
        &options,
        WDF_REQUEST_SEND_OPTION_SYNCHRONOUS |
        WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(
        &options, WDF_REL_TIMEOUT_IN_MS(2000u));
    if (!WdfRequestSend(
            request, WdfDeviceGetIoTarget(filter_device), &options)) {
        status = WdfRequestGetStatus(request);
    } else {
        status = WdfRequestGetStatus(request);
    }

Exit:
    WdfSpinLockAcquire(filter_context->TraceLock);
    filter_context->WriteActive = FALSE;
    WdfSpinLockRelease(filter_context->TraceLock);
    if (request != NULL) WdfObjectDelete(request);
    return status;
}

static ULONG NldAvrcpFilterSaturateSize(_In_ size_t value) {
    return value > MAXULONG ? MAXULONG : (ULONG)value;
}

static ULONGLONG NldAvrcpFilterTimestamp100ns(void) {
    return KeQueryInterruptTime();
}

static void NldAvrcpFilterSetOnline(
    _In_ WDFDEVICE device,
    _In_ BOOLEAN online) {
    PNLD_AVRCP_FILTER_DEVICE_CONTEXT context;
    context = NldAvrcpFilterGetDeviceContext(device);
    if (!context->TraceReady || context->TraceLock == NULL) {
        context->Online = online;
        return;
    }
    WdfSpinLockAcquire(context->TraceLock);
    context->Online = online;
    WdfSpinLockRelease(context->TraceLock);
}

static void NldAvrcpFilterRecord(
    _In_ WDFDEVICE device,
    _In_ NLD_AVRCP_FILTER_EVENT_TYPE type,
    _In_ ULONGLONG request_id,
    _In_ ULONG flags,
    _In_ ULONG control_code,
    _In_ ULONG input_size,
    _In_ ULONG output_size,
    _In_ LONG status,
    _In_ ULONGLONG information,
    _In_reads_bytes_opt_(raw_size) const void* raw_prefix,
    _In_ ULONG raw_size) {
    PNLD_AVRCP_FILTER_DEVICE_CONTEXT context;
    context = NldAvrcpFilterGetDeviceContext(device);
    if (!context->TraceReady || context->TraceLock == NULL) return;
    WdfSpinLockAcquire(context->TraceLock);
    NldAvrcpFilterTracePush(&context->TraceQueue,
                            type,
                            request_id,
                            flags,
                            control_code,
                            input_size,
                            output_size,
                            status,
                            information,
                            raw_prefix,
                            raw_size,
                            NldAvrcpFilterTimestamp100ns());
    WdfSpinLockRelease(context->TraceLock);
}

static ULONGLONG NldAvrcpFilterAllocateRequestId(
    _In_ WDFDEVICE device) {
    PNLD_AVRCP_FILTER_DEVICE_CONTEXT context;
    ULONGLONG request_id;
    context = NldAvrcpFilterGetDeviceContext(device);
    if (!context->TraceReady || context->TraceLock == NULL) return 0ull;
    WdfSpinLockAcquire(context->TraceLock);
    request_id = NldAvrcpFilterTraceAllocateRequestId(
        &context->TraceQueue);
    WdfSpinLockRelease(context->TraceLock);
    return request_id;
}

static void NldAvrcpFilterRecordLifecycle(
    _In_ WDFDEVICE device,
    _In_ NLD_AVRCP_FILTER_LIFECYCLE lifecycle) {
    NldAvrcpFilterRecord(device,
                         NldAvrcpFilterEventLifecycle,
                         0ull,
                         0u,
                         (ULONG)lifecycle,
                         0u,
                         0u,
                         STATUS_SUCCESS,
                         0ull,
                         NULL,
                         0u);
}

static NTSTATUS NldAvrcpFilterCreateControlDevice(
    _In_ WDFDEVICE filter_device) {
    DECLARE_CONST_UNICODE_STRING(
        security_descriptor,
        L"D:P(A;;GA;;;SY)(A;;GRGW;;;BA)(A;;GRGW;;;IU)");
    DECLARE_CONST_UNICODE_STRING(
        device_name,
        L"\\Device\\NativeLdacAvrcpIoFilter");
    DECLARE_CONST_UNICODE_STRING(
        symbolic_link,
        L"\\DosDevices\\NativeLdacAvrcpIoFilter");
    PWDFDEVICE_INIT control_init;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_QUEUE_CONFIG queue_config;
    WDFDEVICE control_device;
    WDFQUEUE control_queue;
    PNLD_AVRCP_FILTER_CONTROL_CONTEXT control_context;
    PNLD_AVRCP_FILTER_DEVICE_CONTEXT filter_context;
    NTSTATUS status;

    control_init = WdfControlDeviceInitAllocate(
        WdfDeviceGetDriver(filter_device),
        &security_descriptor);
    if (control_init == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    WdfDeviceInitSetDeviceType(control_init, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetCharacteristics(
        control_init,
        FILE_DEVICE_SECURE_OPEN,
        FALSE);
    WdfDeviceInitSetExclusive(control_init, FALSE);
    WdfDeviceInitSetIoType(control_init, WdfDeviceIoBuffered);
    status = WdfDeviceInitAssignName(control_init, &device_name);
    if (!NT_SUCCESS(status)) {
        WdfDeviceInitFree(control_init);
        return status;
    }
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes,
        NLD_AVRCP_FILTER_CONTROL_CONTEXT);
    control_device = NULL;
    status = WdfDeviceCreate(
        &control_init,
        &attributes,
        &control_device);
    if (!NT_SUCCESS(status)) return status;
    control_context = NldAvrcpFilterGetControlContext(control_device);
    control_context->FilterDevice = filter_device;
    status = WdfDeviceCreateSymbolicLink(control_device, &symbolic_link);
    if (!NT_SUCCESS(status)) {
        control_context->FilterDevice = NULL;
        WdfObjectDelete(control_device);
        return status;
    }
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queue_config,
        WdfIoQueueDispatchParallel);
    queue_config.EvtIoDeviceControl =
        NldAvrcpFilterControlEvtIoDeviceControl;
    control_queue = NULL;
    status = WdfIoQueueCreate(control_device,
                              &queue_config,
                              WDF_NO_OBJECT_ATTRIBUTES,
                              &control_queue);
    if (!NT_SUCCESS(status)) {
        control_context->FilterDevice = NULL;
        WdfObjectDelete(control_device);
        return status;
    }
    filter_context = NldAvrcpFilterGetDeviceContext(filter_device);
    filter_context->ControlDevice = control_device;
    filter_context->ControlQueue = control_queue;
    WdfControlFinishInitializing(control_device);
    return STATUS_SUCCESS;
}

static void NldAvrcpFilterDestroyControlDevice(
    _In_ WDFDEVICE filter_device) {
    PNLD_AVRCP_FILTER_DEVICE_CONTEXT filter_context;
    PNLD_AVRCP_FILTER_CONTROL_CONTEXT control_context;
    WDFDEVICE control_device;
    WDFQUEUE control_queue;

    filter_context = NldAvrcpFilterGetDeviceContext(filter_device);
    control_device = filter_context->ControlDevice;
    control_queue = filter_context->ControlQueue;
    if (control_device == NULL) return;
    if (control_queue != NULL) {
        WdfIoQueueStopSynchronously(control_queue);
    }
    control_context = NldAvrcpFilterGetControlContext(control_device);
    control_context->FilterDevice = NULL;
    filter_context->ControlQueue = NULL;
    filter_context->ControlDevice = NULL;
    WdfObjectDelete(control_device);
}

static void NldAvrcpFilterForwardUntracked(
    _In_ WDFDEVICE device,
    _In_ WDFREQUEST request) {
    WDF_REQUEST_SEND_OPTIONS options;
    NTSTATUS status;

    WdfRequestFormatRequestUsingCurrentType(request);
    WDF_REQUEST_SEND_OPTIONS_INIT(
        &options,
        WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
    if (WdfRequestSend(request,
                       WdfDeviceGetIoTarget(device),
                       &options)) {
        return;
    }
    status = WdfRequestGetStatus(request);
    WdfRequestComplete(request, status);
}

static void NldAvrcpFilterForward(
    _In_ WDFQUEUE queue,
    _In_ WDFREQUEST request,
    _In_ size_t output_buffer_length,
    _In_ size_t input_buffer_length,
    _In_ ULONG control_code,
    _In_ ULONG direction_flag) {
    WDFDEVICE device;
    WDF_OBJECT_ATTRIBUTES attributes;
    PNLD_AVRCP_FILTER_REQUEST_CONTEXT request_context;
    PVOID input_buffer;
    size_t input_buffer_size;
    ULONG input_size;
    ULONG output_size;
    ULONG raw_size;
    ULONGLONG request_id;
    NTSTATUS status;

    device = WdfIoQueueGetDevice(queue);
    input_size = NldAvrcpFilterSaturateSize(input_buffer_length);
    output_size = NldAvrcpFilterSaturateSize(output_buffer_length);
    request_id = NldAvrcpFilterAllocateRequestId(device);
    input_buffer = NULL;
    input_buffer_size = 0u;
    raw_size = 0u;
    if (input_buffer_length != 0u &&
        (control_code & 3u) != METHOD_NEITHER) {
        status = WdfRequestRetrieveInputBuffer(
            request,
            1u,
            &input_buffer,
            &input_buffer_size);
        if (NT_SUCCESS(status)) {
            raw_size = NldAvrcpFilterSaturateSize(input_buffer_size);
            if (raw_size > NLD_AVRCP_FILTER_RAW_PREFIX_CAPACITY) {
                raw_size = NLD_AVRCP_FILTER_RAW_PREFIX_CAPACITY;
            }
        } else {
            NldAvrcpFilterRecord(device,
                                 NldAvrcpFilterEventCaptureFailure,
                                 request_id,
                                 direction_flag,
                                 control_code,
                                 input_size,
                                 output_size,
                                 status,
                                 0ull,
                                 NULL,
                                 0u);
        }
    }
    NldAvrcpFilterRecord(
        device,
        NldAvrcpFilterEventRequest,
        request_id,
        direction_flag |
            (raw_size != 0u ? NLD_AVRCP_FILTER_EVENT_INPUT_PREFIX : 0u),
        control_code,
        input_size,
        output_size,
        STATUS_PENDING,
        0ull,
        input_buffer,
        raw_size);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes,
        NLD_AVRCP_FILTER_REQUEST_CONTEXT);
    request_context = NULL;
    status = WdfObjectAllocateContext(
        request,
        &attributes,
        (PVOID*)&request_context);
    if (!NT_SUCCESS(status)) {
        NldAvrcpFilterRecord(
            device,
            NldAvrcpFilterEventCaptureFailure,
            request_id,
            direction_flag | NLD_AVRCP_FILTER_EVENT_FORWARD_UNTRACKED,
            control_code,
            input_size,
            output_size,
            status,
            0ull,
            NULL,
            0u);
        NldAvrcpFilterForwardUntracked(device, request);
        return;
    }
    request_context->RequestId = request_id;
    request_context->ControlCode = control_code;
    request_context->Flags = direction_flag;
    request_context->InputSize = input_size;
    request_context->OutputSize = output_size;
    WdfRequestFormatRequestUsingCurrentType(request);
    WdfRequestSetCompletionRoutine(
        request,
        NldAvrcpFilterRequestCompletion,
        device);
    if (WdfRequestSend(request,
                       WdfDeviceGetIoTarget(device),
                       WDF_NO_SEND_OPTIONS)) {
        return;
    }
    status = WdfRequestGetStatus(request);
    NldAvrcpFilterRecord(device,
                         NldAvrcpFilterEventCompletion,
                         request_id,
                         direction_flag,
                         control_code,
                         input_size,
                         output_size,
                         status,
                         0ull,
                         NULL,
                         0u);
    WdfRequestComplete(request, status);
}

NTSTATUS NldAvrcpFilterEvtDeviceAdd(
    _In_ WDFDRIVER driver,
    _Inout_ PWDFDEVICE_INIT device_init) {
    WDF_PNPPOWER_EVENT_CALLBACKS pnp_callbacks;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES spin_lock_attributes;
    WDF_IO_QUEUE_CONFIG queue_config;
    WDFDEVICE device;
    PNLD_AVRCP_FILTER_DEVICE_CONTEXT context;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(driver);

    WdfFdoInitSetFilter(device_init);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp_callbacks);
    pnp_callbacks.EvtDeviceSelfManagedIoInit =
        NldAvrcpFilterEvtSelfManagedIoInit;
    pnp_callbacks.EvtDeviceSelfManagedIoSuspend =
        NldAvrcpFilterEvtSelfManagedIoSuspend;
    pnp_callbacks.EvtDeviceSelfManagedIoRestart =
        NldAvrcpFilterEvtSelfManagedIoRestart;
    pnp_callbacks.EvtDeviceSelfManagedIoCleanup =
        NldAvrcpFilterEvtSelfManagedIoCleanup;
    WdfDeviceInitSetPnpPowerEventCallbacks(device_init, &pnp_callbacks);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes,
        NLD_AVRCP_FILTER_DEVICE_CONTEXT);
    device = NULL;
    status = WdfDeviceCreate(&device_init, &attributes, &device);
    if (!NT_SUCCESS(status)) return status;
    context = NldAvrcpFilterGetDeviceContext(device);
    context->Device = device;
    NldAvrcpFilterTraceInitialize(&context->TraceQueue);
    WDF_OBJECT_ATTRIBUTES_INIT(&spin_lock_attributes);
    spin_lock_attributes.ParentObject = device;
    status = WdfSpinLockCreate(&spin_lock_attributes,
                               &context->TraceLock);
    if (!NT_SUCCESS(status)) {
        context->TraceLock = NULL;
        context->TraceReady = FALSE;
    } else {
        context->TraceReady = TRUE;
    }
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queue_config,
        WdfIoQueueDispatchParallel);
    queue_config.PowerManaged = WdfFalse;
    queue_config.EvtIoDeviceControl =
        NldAvrcpFilterEvtIoDeviceControl;
    queue_config.EvtIoInternalDeviceControl =
        NldAvrcpFilterEvtIoInternalDeviceControl;
    status = WdfIoQueueCreate(device,
                              &queue_config,
                              WDF_NO_OBJECT_ATTRIBUTES,
                              WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        NldAvrcpFilterRecord(device,
                             NldAvrcpFilterEventCaptureFailure,
                             0ull,
                             0u,
                             0u,
                             0u,
                             0u,
                             status,
                             0ull,
                             NULL,
                             0u);
        return STATUS_SUCCESS;
    }
    if (!context->TraceReady) {
        return STATUS_SUCCESS;
    }
    status = NldAvrcpFilterCreateControlDevice(device);
    if (!NT_SUCCESS(status)) {
        NldAvrcpFilterRecord(device,
                             NldAvrcpFilterEventCaptureFailure,
                             0ull,
                             0u,
                             0u,
                             0u,
                             0u,
                             status,
                             0ull,
                             NULL,
                             0u);
    }
    return STATUS_SUCCESS;
}

NTSTATUS NldAvrcpFilterEvtSelfManagedIoInit(
    _In_ WDFDEVICE device) {
    NldAvrcpFilterSetOnline(device, TRUE);
    NldAvrcpFilterRecordLifecycle(
        device,
        NldAvrcpFilterLifecycleStarted);
    return STATUS_SUCCESS;
}

NTSTATUS NldAvrcpFilterEvtSelfManagedIoSuspend(
    _In_ WDFDEVICE device) {
    NldAvrcpFilterSetOnline(device, FALSE);
    NldAvrcpFilterRecordLifecycle(
        device,
        NldAvrcpFilterLifecycleSuspended);
    return STATUS_SUCCESS;
}

NTSTATUS NldAvrcpFilterEvtSelfManagedIoRestart(
    _In_ WDFDEVICE device) {
    NldAvrcpFilterSetOnline(device, TRUE);
    NldAvrcpFilterRecordLifecycle(
        device,
        NldAvrcpFilterLifecycleRestarted);
    return STATUS_SUCCESS;
}

void NldAvrcpFilterEvtSelfManagedIoCleanup(
    _In_ WDFDEVICE device) {
    NldAvrcpFilterSetOnline(device, FALSE);
    NldAvrcpFilterRecordLifecycle(
        device,
        NldAvrcpFilterLifecycleCleanup);
    NldAvrcpFilterDestroyControlDevice(device);
}

void NldAvrcpFilterEvtIoDeviceControl(
    _In_ WDFQUEUE queue,
    _In_ WDFREQUEST request,
    _In_ size_t output_buffer_length,
    _In_ size_t input_buffer_length,
    _In_ ULONG io_control_code) {
    NldAvrcpFilterForward(queue,
                          request,
                          output_buffer_length,
                          input_buffer_length,
                          io_control_code,
                          NLD_AVRCP_FILTER_EVENT_DEVICE_CONTROL);
}

void NldAvrcpFilterEvtIoInternalDeviceControl(
    _In_ WDFQUEUE queue,
    _In_ WDFREQUEST request,
    _In_ size_t output_buffer_length,
    _In_ size_t input_buffer_length,
    _In_ ULONG io_control_code) {
    NldAvrcpFilterForward(queue,
                          request,
                          output_buffer_length,
                          input_buffer_length,
                          io_control_code,
                          NLD_AVRCP_FILTER_EVENT_INTERNAL_CONTROL);
}

void NldAvrcpFilterRequestCompletion(
    _In_ WDFREQUEST request,
    _In_ WDFIOTARGET target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS params,
    _In_ WDFCONTEXT completion_context) {
    WDFDEVICE device;
    PNLD_AVRCP_FILTER_REQUEST_CONTEXT request_context;
    PVOID output_buffer;
    size_t output_buffer_size;
    ULONG raw_size;
    ULONG flags;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(target);

    device = (WDFDEVICE)completion_context;
    request_context = NldAvrcpFilterGetRequestContext(request);
    output_buffer = NULL;
    output_buffer_size = 0u;
    raw_size = 0u;
    flags = request_context->Flags;
    if (params->IoStatus.Information != 0u &&
        request_context->OutputSize != 0u &&
        (request_context->ControlCode & 3u) != METHOD_NEITHER) {
        status = WdfRequestRetrieveOutputBuffer(
            request,
            1u,
            &output_buffer,
            &output_buffer_size);
        if (NT_SUCCESS(status)) {
            ULONGLONG bounded_information;
            bounded_information = (ULONGLONG)params->IoStatus.Information;
            if (bounded_information > output_buffer_size) {
                bounded_information = output_buffer_size;
            }
            raw_size = bounded_information >
                    NLD_AVRCP_FILTER_RAW_PREFIX_CAPACITY
                ? NLD_AVRCP_FILTER_RAW_PREFIX_CAPACITY
                : (ULONG)bounded_information;
            if (raw_size != 0u) {
                flags |= NLD_AVRCP_FILTER_EVENT_OUTPUT_PREFIX;
            }
        } else {
            NldAvrcpFilterRecord(
                device,
                NldAvrcpFilterEventCaptureFailure,
                request_context->RequestId,
                request_context->Flags,
                request_context->ControlCode,
                request_context->InputSize,
                request_context->OutputSize,
                status,
                (ULONGLONG)params->IoStatus.Information,
                NULL,
                0u);
        }
    }
    NldAvrcpFilterRecord(
        device,
        NldAvrcpFilterEventCompletion,
        request_context->RequestId,
        flags,
        request_context->ControlCode,
        request_context->InputSize,
        request_context->OutputSize,
        params->IoStatus.Status,
        (ULONGLONG)params->IoStatus.Information,
        output_buffer,
        raw_size);
    WdfRequestCompleteWithInformation(
        request,
        params->IoStatus.Status,
        params->IoStatus.Information);
}

void NldAvrcpFilterControlEvtIoDeviceControl(
    _In_ WDFQUEUE queue,
    _In_ WDFREQUEST request,
    _In_ size_t output_buffer_length,
    _In_ size_t input_buffer_length,
    _In_ ULONG io_control_code) {
    WDFDEVICE control_device;
    PNLD_AVRCP_FILTER_CONTROL_CONTEXT control_context;
    PNLD_AVRCP_FILTER_DEVICE_CONTEXT filter_context;
    PVOID output_buffer;
    size_t output_size;
    size_t information;
    NTSTATUS status;
    ULONG runtime_flags;
    UNREFERENCED_PARAMETER(output_buffer_length);
    UNREFERENCED_PARAMETER(input_buffer_length);

    control_device = WdfIoQueueGetDevice(queue);
    control_context = NldAvrcpFilterGetControlContext(control_device);
    if (control_context->FilterDevice == NULL) {
        WdfRequestComplete(request, STATUS_DEVICE_NOT_READY);
        return;
    }
    filter_context = NldAvrcpFilterGetDeviceContext(
        control_context->FilterDevice);
    output_buffer = NULL;
    output_size = 0u;
    information = 0u;
    status = STATUS_INVALID_DEVICE_REQUEST;
    switch (io_control_code) {
        case IOCTL_NLD_AVRCP_FILTER_GET_VERSION: {
            PNLD_AVRCP_FILTER_ABI_VERSION version;
            status = WdfRequestRetrieveOutputBuffer(
                request,
                sizeof(*version),
                (PVOID*)&version,
                &output_size);
            if (NT_SUCCESS(status)) {
                RtlZeroMemory(version, sizeof(*version));
                version->Size = sizeof(*version);
                version->Major = NLD_AVRCP_FILTER_ABI_MAJOR;
                version->Minor = NLD_AVRCP_FILTER_ABI_MINOR;
                version->Flags = NLD_AVRCP_FILTER_VERSION_FLAGS;
                information = sizeof(*version);
            }
            break;
        }
        case IOCTL_NLD_AVRCP_FILTER_GET_STATUS: {
            PNLD_AVRCP_FILTER_STATUS filter_status;
            status = WdfRequestRetrieveOutputBuffer(
                request,
                sizeof(*filter_status),
                (PVOID*)&filter_status,
                &output_size);
            if (NT_SUCCESS(status)) {
                runtime_flags = NLD_AVRCP_FILTER_STATUS_CONTROL_READY;
                WdfSpinLockAcquire(filter_context->TraceLock);
                if (filter_context->Online) {
                    runtime_flags |= NLD_AVRCP_FILTER_STATUS_ONLINE;
                }
                NldAvrcpFilterTraceGetStatus(
                    &filter_context->TraceQueue,
                    runtime_flags,
                    filter_status);
                WdfSpinLockRelease(filter_context->TraceLock);
                information = sizeof(*filter_status);
            }
            break;
        }
        case IOCTL_NLD_AVRCP_FILTER_DEQUEUE_EVENT: {
            PNLD_AVRCP_FILTER_EVENT event;
            status = WdfRequestRetrieveOutputBuffer(
                request,
                sizeof(*event),
                (PVOID*)&event,
                &output_size);
            if (NT_SUCCESS(status)) {
                WdfSpinLockAcquire(filter_context->TraceLock);
                if (!NldAvrcpFilterTracePop(
                        &filter_context->TraceQueue,
                        event)) {
                    status = STATUS_NO_MORE_ENTRIES;
                }
                WdfSpinLockRelease(filter_context->TraceLock);
                if (NT_SUCCESS(status)) {
                    information = sizeof(*event);
                }
            }
            break;
        }
        case IOCTL_NLD_AVRCP_FILTER_SET_ABSOLUTE_VOLUME: {
            PNLD_AVRCP_FILTER_SET_VOLUME_REQUEST volume_request;
            status = WdfRequestRetrieveInputBuffer(
                request,
                sizeof(*volume_request),
                (PVOID*)&volume_request,
                NULL);
            if (NT_SUCCESS(status) &&
                (volume_request->Size != sizeof(*volume_request) ||
                 volume_request->Version !=
                     NLD_AVRCP_FILTER_WRITE_ABI_VERSION ||
                 volume_request->Volume > 127u ||
                 volume_request->Reserved != 0u)) {
                status = STATUS_INVALID_PARAMETER;
            }
            if (NT_SUCCESS(status)) {
                status = NldAvrcpFilterSendAbsoluteVolume(
                    control_context->FilterDevice,
                    (UCHAR)volume_request->Volume);
            }
            break;
        }
        default:
            break;
    }
    WdfRequestCompleteWithInformation(request, status, information);
}
