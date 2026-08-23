// SPDX-License-Identifier: Apache-2.0
#include <initguid.h>
#include "device.h"
#include <wdmsec.h>

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, NldAvrcpObserverEvtDeviceAdd)
#pragma alloc_text(PAGE, NldAvrcpObserverEvtSelfManagedIoInit)
#pragma alloc_text(PAGE, NldAvrcpObserverEvtSelfManagedIoSuspend)
#pragma alloc_text(PAGE, NldAvrcpObserverEvtSelfManagedIoRestart)
#pragma alloc_text(PAGE, NldAvrcpObserverEvtSelfManagedIoCleanup)
#endif

static ULONGLONG NldAvrcpObserverNextGeneration(ULONGLONG generation) {
    return generation == ~0ull ? 1ull : generation + 1ull;
}

static ULONGLONG NldAvrcpObserverTimestamp100ns(void) {
    return KeQueryInterruptTime();
}

static void NldAvrcpObserverSetRuntimeFlags(
    _In_ PNLD_AVRCP_OBSERVER_DEVICE_CONTEXT context,
    _In_ ULONG set_flags,
    _In_ ULONG clear_flags) {
    WdfSpinLockAcquire(context->StateLock);
    context->RuntimeFlags &= ~clear_flags;
    context->RuntimeFlags |= set_flags;
    WdfSpinLockRelease(context->StateLock);
}

static void NldAvrcpObserverSetProtocolStatus(
    _In_ PNLD_AVRCP_OBSERVER_DEVICE_CONTEXT context,
    _In_ LONG status) {
    WdfSpinLockAcquire(context->StateLock);
    context->LastProtocolStatus = status;
    WdfSpinLockRelease(context->StateLock);
}

static void NldAvrcpObserverCompleteWrite(
    _In_ PNLD_AVRCP_OBSERVER_DEVICE_CONTEXT context) {
    WdfSpinLockAcquire(context->StateLock);
    context->WriteTransactionActive = FALSE;
    WdfSpinLockRelease(context->StateLock);
}

static void NldAvrcpObserverQueueProtocolEvent(
    _In_ PNLD_AVRCP_OBSERVER_DEVICE_CONTEXT context,
    _In_ const avrcp_observer_event* event) {
    NLD_AVRCP_OBSERVER_EVENT_TYPE type = NldAvrcpObserverEventNone;
    ULONG flags = 0u;
    ULONG value = 0u;
    ULONG raw_prefix_size = 0u;
    ULONG raw_high_words[15] = {0u};
    LONG protocol_status = 0;

    if (event == NULL || event->kind == AVRCP_OBSERVER_EVENT_NONE) return;
    switch (event->kind) {
        case AVRCP_OBSERVER_EVENT_VOLUME_CAPABILITY:
            type = NldAvrcpObserverEventVolumeCapability;
            if (event->volume_supported) {
                flags |= NLD_AVRCP_EVENT_FLAG_SUPPORTED;
            }
            value = event->volume_supported;
            break;
        case AVRCP_OBSERVER_EVENT_VOLUME_CHANGED:
            type = NldAvrcpObserverEventAbsoluteVolume;
            flags |= NLD_AVRCP_EVENT_FLAG_RESPONSE;
            if (event->response_code == AVRCP_RESPONSE_INTERIM) {
                flags |= NLD_AVRCP_EVENT_FLAG_INTERIM;
            } else if (event->response_code == AVRCP_RESPONSE_CHANGED) {
                flags |= NLD_AVRCP_EVENT_FLAG_CHANGED;
            }
            value = event->absolute_volume;
            break;
        case AVRCP_OBSERVER_EVENT_PASS_THROUGH:
            type = NldAvrcpObserverEventPassThrough;
            if (event->released) flags |= NLD_AVRCP_EVENT_FLAG_RELEASED;
            value = event->operation_id;
            break;
        case AVRCP_OBSERVER_EVENT_WRITE_RESPONSE:
            type = NldAvrcpObserverEventWriteResponse;
            value = event->pdu_id;
            protocol_status = event->response_code;
            if (event->parameter_size != 0u &&
                event->parameter_size <= 8u) {
                unsigned raw_index;
                for (raw_index = 0u; raw_index < 4u &&
                     raw_index < event->parameter_size; ++raw_index) {
                    raw_high_words[0] |=
                        (ULONG)event->parameter_bytes[raw_index] <<
                        (raw_index * 8u);
                }
                for (raw_index = 4u; raw_index < 8u &&
                     raw_index < event->parameter_size; ++raw_index) {
                    raw_high_words[1] |=
                        (ULONG)event->parameter_bytes[raw_index] <<
                        ((raw_index - 4u) * 8u);
                }
            }
            break;
        case AVRCP_OBSERVER_EVENT_VENDOR_COMMAND:
            type = NldAvrcpObserverEventVendorCommand;
            value = event->pdu_id;
            if (event->parameter_size != 0u &&
                event->parameter_size <= 8u) {
                unsigned raw_index;
                for (raw_index = 0u; raw_index < 4u &&
                     raw_index < event->parameter_size; ++raw_index) {
                    raw_high_words[0] |=
                        (ULONG)event->parameter_bytes[raw_index] <<
                        (raw_index * 8u);
                }
                for (raw_index = 4u; raw_index < 8u &&
                     raw_index < event->parameter_size; ++raw_index) {
                    raw_high_words[1] |=
                        (ULONG)event->parameter_bytes[raw_index] <<
                        ((raw_index - 4u) * 8u);
                }
            }
            break;
        case AVRCP_OBSERVER_EVENT_PROTOCOL_ERROR:
            type = NldAvrcpObserverEventProtocolError;
            protocol_status = event->error_code;
            if (event->raw_prefix_size != 0u &&
                event->raw_prefix_size <= 64u) {
                unsigned word_index;
                unsigned raw_index;
                raw_prefix_size = event->raw_prefix_size;
                flags = NLD_AVRCP_EVENT_FLAG_RAW_PREFIX |
                    (raw_prefix_size << NLD_AVRCP_EVENT_RAW_LENGTH_SHIFT) |
                    (((ULONG)event->raw_total_size & 0xFFu) <<
                        NLD_AVRCP_EVENT_PACKET_SIZE_SHIFT) |
                    ((ULONG)event->error_stage <<
                        NLD_AVRCP_EVENT_PARSE_STAGE_SHIFT);
                for (raw_index = 0u; raw_index < 4u; ++raw_index) {
                    if (raw_index < raw_prefix_size) {
                        value |= (ULONG)event->raw_prefix[raw_index] <<
                            (raw_index * 8u);
                    }
                }
                for (word_index = 0u; word_index < 15u; ++word_index) {
                    unsigned byte_base = 4u + word_index * 4u;
                    for (raw_index = 0u; raw_index < 4u; ++raw_index) {
                        unsigned byte_index = byte_base + raw_index;
                        if (byte_index < raw_prefix_size) {
                            raw_high_words[word_index] |=
                                (ULONG)event->raw_prefix[byte_index] <<
                                (raw_index * 8u);
                        }
                    }
                }
            }
            break;
        case AVRCP_OBSERVER_EVENT_NONE:
        default:
            return;
    }

    WdfSpinLockAcquire(context->StateLock);
    (void)NldAvrcpEventQueuePush(&context->EventQueue,
                                 context->AclGeneration,
                                 type,
                                 flags,
                                 value,
                                 protocol_status,
                                 raw_prefix_size,
                                 raw_high_words,
                                 NldAvrcpObserverTimestamp100ns());
    WdfSpinLockRelease(context->StateLock);
}

static void NldAvrcpObserverEndGeneration(
    _In_ PNLD_AVRCP_OBSERVER_DEVICE_CONTEXT context) {
    WdfSpinLockAcquire(context->StateLock);
    (void)NldAvrcpEventQueueEndGeneration(
        &context->EventQueue,
        context->AclGeneration,
        NldAvrcpObserverTimestamp100ns());
    WdfSpinLockRelease(context->StateLock);
}

static void NldAvrcpObserverRemoteDisconnect(
    _In_opt_ PVOID callback_context,
    _In_ ULONG channel_generation) {
    PNLD_AVRCP_OBSERVER_DEVICE_CONTEXT context =
        (PNLD_AVRCP_OBSERVER_DEVICE_CONTEXT)callback_context;

    UNREFERENCED_PARAMETER(channel_generation);
    if (context != NULL) {
        KeSetEvent(&context->StopEvent, IO_NO_INCREMENT, FALSE);
    }
}

static NTSTATUS NldAvrcpObserverStart(
    _In_ PNLD_AVRCP_OBSERVER_DEVICE_CONTEXT context) {
    KeClearEvent(&context->StopEvent);
    context->AclGeneration = NldAvrcpObserverNextGeneration(
        context->AclGeneration);
    avrcp_observer_init(&context->Observer);
    (void)InterlockedExchange(&context->WorkerRunning, 0);
    WdfSpinLockAcquire(context->StateLock);
    context->RuntimeFlags =
        NLD_AVRCP_OBSERVER_STATUS_OUTBOUND_OPEN |
        NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUIRED;
    context->LastProtocolStatus = 0;
    context->ActivationRequested = FALSE;
    context->PendingWriteValid = FALSE;
    context->WriteTransactionActive = FALSE;
    (void)NldAvrcpEventQueueBeginGeneration(
        &context->EventQueue,
        context->AclGeneration,
        NldAvrcpObserverTimestamp100ns());
    WdfSpinLockRelease(context->StateLock);

    return STATUS_SUCCESS;
}

static void NldAvrcpObserverStop(
    _In_ PNLD_AVRCP_OBSERVER_DEVICE_CONTEXT context) {
    KeSetEvent(&context->StopEvent, IO_NO_INCREMENT, FALSE);
    WdfWorkItemFlush(context->ObserverWorkItem);
    NldAvrcpObserverCompleteWrite(context);
    NldBthSignalingSetDisconnectCallback(&context->Channel, NULL, NULL);
    NldBthSignalingStop(&context->Channel,
                        NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);
    NldBthProfileStop(&context->Profile);
    NldAvrcpObserverSetRuntimeFlags(context, 0u, ~0u);
    NldAvrcpObserverEndGeneration(context);
}

VOID NldAvrcpObserverEvtFileCleanup(_In_ WDFFILEOBJECT file_object) {
    PNLD_AVRCP_OBSERVER_DEVICE_CONTEXT context =
        NldAvrcpObserverGetDeviceContext(
            WdfFileObjectGetDevice(file_object));

    KeSetEvent(&context->StopEvent, IO_NO_INCREMENT, FALSE);
    WdfWorkItemFlush(context->ObserverWorkItem);
    NldAvrcpObserverCompleteWrite(context);
    NldBthSignalingSetDisconnectCallback(&context->Channel, NULL, NULL);
    NldBthSignalingStop(&context->Channel,
                        NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);
    NldBthProfileStop(&context->Profile);
    NldAvrcpObserverSetRuntimeFlags(context, 0u, ~0u);
    NldAvrcpObserverEndGeneration(context);
    (void)NldAvrcpObserverStart(context);
}

NTSTATUS NldAvrcpObserverEvtDeviceAdd(
    _In_ WDFDRIVER driver,
    _Inout_ PWDFDEVICE_INIT device_init) {
    WDFDEVICE device;
    WDF_OBJECT_ATTRIBUTES device_attributes;
    WDF_OBJECT_ATTRIBUTES lock_attributes;
    WDF_OBJECT_ATTRIBUTES queue_attributes;
    WDF_OBJECT_ATTRIBUTES work_item_attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnp_callbacks;
    WDF_IO_QUEUE_CONFIG queue_config;
    WDF_FILEOBJECT_CONFIG file_config;
    WDF_WORKITEM_CONFIG work_item_config;
    PNLD_AVRCP_OBSERVER_DEVICE_CONTEXT context;
    NTSTATUS status;
    DECLARE_CONST_UNICODE_STRING(sddl,
        L"D:P(A;;GR;;;SY)(A;;GR;;;BA)");

    UNREFERENCED_PARAMETER(driver);
    PAGED_CODE();

    WdfDeviceInitSetCharacteristics(device_init,
                                    FILE_AUTOGENERATED_DEVICE_NAME,
                                    TRUE);
    status = WdfDeviceInitAssignSDDLString(device_init, &sddl);
    if (!NT_SUCCESS(status)) return status;
    WdfDeviceInitSetDeviceType(device_init,
                               FILE_DEVICE_NLD_AVRCP_OBSERVER);
    WdfDeviceInitSetIoType(device_init, WdfDeviceIoBuffered);
    WdfDeviceInitSetExclusive(device_init, TRUE);
    WDF_FILEOBJECT_CONFIG_INIT(
        &file_config,
        WDF_NO_EVENT_CALLBACK,
        WDF_NO_EVENT_CALLBACK,
        NldAvrcpObserverEvtFileCleanup);
    WdfDeviceInitSetFileObjectConfig(
        device_init, &file_config, WDF_NO_OBJECT_ATTRIBUTES);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp_callbacks);
    pnp_callbacks.EvtDeviceSelfManagedIoInit =
        NldAvrcpObserverEvtSelfManagedIoInit;
    pnp_callbacks.EvtDeviceSelfManagedIoSuspend =
        NldAvrcpObserverEvtSelfManagedIoSuspend;
    pnp_callbacks.EvtDeviceSelfManagedIoRestart =
        NldAvrcpObserverEvtSelfManagedIoRestart;
    pnp_callbacks.EvtDeviceSelfManagedIoCleanup =
        NldAvrcpObserverEvtSelfManagedIoCleanup;
    WdfDeviceInitSetPnpPowerEventCallbacks(device_init, &pnp_callbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &device_attributes,
        NLD_AVRCP_OBSERVER_DEVICE_CONTEXT);
    device_attributes.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfDeviceCreate(&device_init, &device_attributes, &device);
    if (!NT_SUCCESS(status)) return status;

    context = NldAvrcpObserverGetDeviceContext(device);
    RtlZeroMemory(context, sizeof(*context));
    context->Device = device;
    context->LastProtocolStatus = STATUS_NOT_SUPPORTED;
    KeInitializeEvent(&context->StopEvent, NotificationEvent, TRUE);
    NldBthProfileInitialize(&context->Profile);
    NldBthSignalingInitialize(&context->Channel);
    NldAvrcpEventQueueInitialize(&context->EventQueue);
    avrcp_observer_init(&context->Observer);

    WDF_OBJECT_ATTRIBUTES_INIT(&lock_attributes);
    lock_attributes.ParentObject = device;
    status = WdfSpinLockCreate(&lock_attributes, &context->StateLock);
    if (!NT_SUCCESS(status)) return status;

    WDF_WORKITEM_CONFIG_INIT(&work_item_config, NldAvrcpObserverWorker);
    WDF_OBJECT_ATTRIBUTES_INIT(&work_item_attributes);
    work_item_attributes.ParentObject = device;
    status = WdfWorkItemCreate(&work_item_config,
                               &work_item_attributes,
                               &context->ObserverWorkItem);
    if (!NT_SUCCESS(status)) return status;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queue_config,
        WdfIoQueueDispatchSequential);
    queue_config.EvtIoDeviceControl =
        NldAvrcpObserverEvtIoDeviceControl;
    WDF_OBJECT_ATTRIBUTES_INIT(&queue_attributes);
    queue_attributes.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfIoQueueCreate(device,
                              &queue_config,
                              &queue_attributes,
                              WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) return status;

    return WdfDeviceCreateDeviceInterface(
        device,
        &GUID_DEVINTERFACE_NATIVE_LDAC_AVRCP_OBSERVER,
        NULL);
}

NTSTATUS NldAvrcpObserverEvtSelfManagedIoInit(_In_ WDFDEVICE device) {
    PAGED_CODE();
    return NldAvrcpObserverStart(
        NldAvrcpObserverGetDeviceContext(device));
}

NTSTATUS NldAvrcpObserverEvtSelfManagedIoSuspend(_In_ WDFDEVICE device) {
    PAGED_CODE();
    NldAvrcpObserverStop(NldAvrcpObserverGetDeviceContext(device));
    return STATUS_SUCCESS;
}

NTSTATUS NldAvrcpObserverEvtSelfManagedIoRestart(_In_ WDFDEVICE device) {
    PAGED_CODE();
    return NldAvrcpObserverStart(
        NldAvrcpObserverGetDeviceContext(device));
}

VOID NldAvrcpObserverEvtSelfManagedIoCleanup(_In_ WDFDEVICE device) {
    PAGED_CODE();
    NldAvrcpObserverStop(NldAvrcpObserverGetDeviceContext(device));
}

VOID NldAvrcpObserverWorker(_In_ WDFWORKITEM work_item) {
    WDFDEVICE device = (WDFDEVICE)WdfWorkItemGetParentObject(work_item);
    PNLD_AVRCP_OBSERVER_DEVICE_CONTEXT context =
        NldAvrcpObserverGetDeviceContext(device);
    avrcp_observer_result result;
    unsigned char packet[L2CAP_DEFAULT_MTU];
    ULONG bytes_read;
    NTSTATUS status;
    NLD_BTH_SIGNALING_SNAPSHOT signaling_snapshot;
    BOOLEAN profile_started = FALSE;
    BOOLEAN channel_started = FALSE;

    status = NldBthProfileStart(
        WdfDeviceWdmGetAttachedDevice(context->Device),
        &context->Profile);
    if (!NT_SUCCESS(status)) goto OpenFailed;
    profile_started = TRUE;
    NldAvrcpObserverSetRuntimeFlags(
        context,
        NLD_AVRCP_OBSERVER_STATUS_PROFILE_READY |
            NLD_AVRCP_OBSERVER_STATUS_REMOTE_READY |
            NLD_AVRCP_OBSERVER_STATUS_LOCAL_READY,
        0u);

    status = NldBthChannelStart(
        &context->Channel,
        &context->Profile,
        WdfDeviceWdmGetDeviceObject(context->Device),
        NLD_AVRCP_CONTROL_PSM);
    if (!NT_SUCCESS(status)) goto OpenFailed;
    channel_started = TRUE;
    NldBthSignalingSetDisconnectCallback(
        &context->Channel,
        NldAvrcpObserverRemoteDisconnect,
        context);

    status = NldBthSignalingOpen(&context->Channel, L2CAP_DEFAULT_MTU);
    if (status == STATUS_PENDING) {
        NldAvrcpObserverSetRuntimeFlags(
            context,
            NLD_AVRCP_OBSERVER_STATUS_OPEN_PENDING,
            0u);
        status = NldBthSignalingWaitForRequestDrain(
            &context->Channel,
            NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);
    }
    NldAvrcpObserverSetRuntimeFlags(
        context, 0u, NLD_AVRCP_OBSERVER_STATUS_OPEN_PENDING);
    NldBthSignalingGetSnapshot(&context->Channel,
                               &signaling_snapshot);
    if (NT_SUCCESS(status) &&
        signaling_snapshot.State != NldBthSignalingChannelOpen) {
        status = NT_SUCCESS(signaling_snapshot.LastOpenStatus)
            ? STATUS_DEVICE_NOT_READY
            : signaling_snapshot.LastOpenStatus;
    }
    if (!NT_SUCCESS(status)) {
OpenFailed:
        NldAvrcpObserverSetProtocolStatus(context, status);
        WdfSpinLockAcquire(context->StateLock);
        (void)NldAvrcpEventQueuePush(
            &context->EventQueue,
            context->AclGeneration,
            NldAvrcpObserverEventProtocolError,
            0u,
            0u,
            status,
            0u,
            NULL,
            NldAvrcpObserverTimestamp100ns());
        WdfSpinLockRelease(context->StateLock);
        if (channel_started) {
            NldBthSignalingSetDisconnectCallback(
                &context->Channel, NULL, NULL);
            NldBthSignalingStop(
                &context->Channel,
                NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);
        }
        if (profile_started) NldBthProfileStop(&context->Profile);
        NldAvrcpObserverSetRuntimeFlags(
            context,
            0u,
            NLD_AVRCP_OBSERVER_STATUS_PROFILE_READY |
                NLD_AVRCP_OBSERVER_STATUS_REMOTE_READY |
                NLD_AVRCP_OBSERVER_STATUS_LOCAL_READY |
                NLD_AVRCP_OBSERVER_STATUS_OPEN_PENDING);
        NldAvrcpObserverEndGeneration(context);
        (void)InterlockedExchange(&context->WorkerRunning, 0);
        return;
    }
    NldAvrcpObserverSetRuntimeFlags(
        context,
        NLD_AVRCP_OBSERVER_STATUS_CHANNEL_OPEN |
            NLD_AVRCP_OBSERVER_STATUS_CHANNEL_HELD,
        0u);

    result = avrcp_observer_begin(&context->Observer);
    if (result.packet_size == 0u) {
        status = STATUS_DEVICE_PROTOCOL_ERROR;
        NldAvrcpObserverQueueProtocolEvent(context, &result.event);
        goto Exit;
    }
    status = NldBthSignalingWrite(&context->Channel,
                                  result.packet,
                                  (ULONG)result.packet_size,
                                  NLD_AVRCP_WRITE_TIMEOUT_MS);
    if (!NT_SUCCESS(status)) goto Exit;

    for (;;) {
        if (KeReadStateEvent(&context->StopEvent) != 0) {
            status = STATUS_SUCCESS;
            break;
        }
        if (context->PendingWriteValid) {
            NLD_AVRCP_OBSERVER_WRITE_REQUEST write_request;
            avrcp_observer_result write_result;
            WdfSpinLockAcquire(context->StateLock);
            write_request = context->PendingWrite;
            context->PendingWriteValid = FALSE;
            WdfSpinLockRelease(context->StateLock);
            write_result = avrcp_observer_submit_write(
                &context->Observer,
                (uint8_t)write_request.PduId,
                (uint8_t)write_request.Response,
                write_request.Parameters,
                write_request.ParameterSize);
            if (write_result.packet_size != 0u) {
                status = NldBthSignalingWrite(
                    &context->Channel,
                    write_result.packet,
                    (ULONG)write_result.packet_size,
                    NLD_AVRCP_WRITE_TIMEOUT_MS);
                if (!NT_SUCCESS(status)) {
                    NldAvrcpObserverCompleteWrite(context);
                    break;
                }
                // Response packets do not create a protocol write
                // transaction. Commands remain active until the matching
                // remote response is observed below.
                if (write_request.Response != 0u) {
                    NldAvrcpObserverCompleteWrite(context);
                }
            } else {
                // The protocol rejected the request (for example because a
                // transaction is already active); do not leave the IOCTL
                // serialization gate permanently closed.
                NldAvrcpObserverCompleteWrite(context);
            }
        }
        bytes_read = 0u;
        status = NldBthSignalingRead(&context->Channel,
                                     packet,
                                     sizeof(packet),
                                     NLD_AVRCP_READ_TIMEOUT_MS,
                                     &bytes_read);
        if (status == STATUS_IO_TIMEOUT) continue;
        if (!NT_SUCCESS(status)) break;

        result = avrcp_observer_handle_packet(
            &context->Observer, packet, bytes_read);
        NldAvrcpObserverQueueProtocolEvent(context, &result.event);
        if (result.event.kind == AVRCP_OBSERVER_EVENT_WRITE_RESPONSE) {
            NldAvrcpObserverCompleteWrite(context);
        }
        if (result.event.kind == AVRCP_OBSERVER_EVENT_VOLUME_CAPABILITY) {
            NldAvrcpObserverSetRuntimeFlags(
                context,
                result.event.volume_supported
                    ? NLD_AVRCP_OBSERVER_STATUS_VOLUME_SUPPORTED
                    : 0u,
                result.event.volume_supported
                    ? 0u
                    : NLD_AVRCP_OBSERVER_STATUS_VOLUME_SUPPORTED);
        } else if (result.event.kind ==
                   AVRCP_OBSERVER_EVENT_VOLUME_CHANGED) {
            NldAvrcpObserverSetRuntimeFlags(
                context,
                NLD_AVRCP_OBSERVER_STATUS_OBSERVING,
                0u);
        }
        if (result.event.kind == AVRCP_OBSERVER_EVENT_PROTOCOL_ERROR) {
            status = STATUS_DEVICE_PROTOCOL_ERROR;
            break;
        }
        if (result.packet_size != 0u) {
            status = NldBthSignalingWrite(
                &context->Channel,
                result.packet,
                (ULONG)result.packet_size,
                NLD_AVRCP_WRITE_TIMEOUT_MS);
            if (!NT_SUCCESS(status)) break;
        }
    }

Exit:
    NldBthSignalingGetSnapshot(&context->Channel,
                               &signaling_snapshot);
    NldAvrcpObserverCompleteWrite(context);
    NldAvrcpObserverSetProtocolStatus(context, status);
    (void)NldBthSignalingClose(&context->Channel,
                               NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);
    NldAvrcpObserverSetRuntimeFlags(
        context,
        0u,
        NLD_AVRCP_OBSERVER_STATUS_CHANNEL_OPEN |
            NLD_AVRCP_OBSERVER_STATUS_CHANNEL_HELD |
            NLD_AVRCP_OBSERVER_STATUS_OPEN_PENDING |
            NLD_AVRCP_OBSERVER_STATUS_OBSERVING);
    NldAvrcpObserverEndGeneration(context);
    (void)InterlockedExchange(&context->WorkerRunning, 0);
    if (signaling_snapshot.RemoteDisconnected) {
        NldBthSignalingSetDisconnectCallback(
            &context->Channel, NULL, NULL);
        NldBthSignalingStop(
            &context->Channel,
            NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);
        NldBthProfileStop(&context->Profile);
        (void)NldAvrcpObserverStart(context);
    }
}

VOID NldAvrcpObserverEvtIoDeviceControl(
    _In_ WDFQUEUE queue,
    _In_ WDFREQUEST request,
    _In_ size_t output_buffer_length,
    _In_ size_t input_buffer_length,
    _In_ ULONG io_control_code) {
    WDFDEVICE device = WdfIoQueueGetDevice(queue);
    PNLD_AVRCP_OBSERVER_DEVICE_CONTEXT context =
        NldAvrcpObserverGetDeviceContext(device);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    size_t information = 0u;
    BOOLEAN start_observation = FALSE;

    UNREFERENCED_PARAMETER(input_buffer_length);
    if (io_control_code == IOCTL_NLD_AVRCP_OBSERVER_GET_VERSION) {
        PNLD_AVRCP_OBSERVER_ABI_VERSION version;
        if (output_buffer_length < sizeof(*version)) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            status = WdfRequestRetrieveOutputBuffer(
                request,
                sizeof(*version),
                (PVOID*)&version,
                NULL);
            if (NT_SUCCESS(status)) {
                RtlZeroMemory(version, sizeof(*version));
                version->Size = sizeof(*version);
                version->Major = NLD_AVRCP_OBSERVER_ABI_MAJOR;
                version->Minor = NLD_AVRCP_OBSERVER_ABI_MINOR;
                information = sizeof(*version);
            }
        }
    } else if (io_control_code == IOCTL_NLD_AVRCP_OBSERVER_GET_STATUS) {
        PNLD_AVRCP_OBSERVER_STATUS observer_status;
        NLD_BTH_SIGNALING_SNAPSHOT signaling_snapshot;
        if (output_buffer_length < sizeof(*observer_status)) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            status = WdfRequestRetrieveOutputBuffer(
                request,
                sizeof(*observer_status),
                (PVOID*)&observer_status,
                NULL);
            if (NT_SUCCESS(status)) {
                WdfSpinLockAcquire(context->StateLock);
                NldAvrcpEventQueueGetStatus(&context->EventQueue,
                                            observer_status);
                NldBthSignalingGetSnapshot(&context->Channel,
                                           &signaling_snapshot);
                observer_status->Flags |= context->RuntimeFlags;
                observer_status->LastProtocolStatus =
                    context->LastProtocolStatus;
                observer_status->LastOpenStatus =
                    signaling_snapshot.LastOpenStatus;
                observer_status->LastCloseStatus =
                    signaling_snapshot.LastCloseStatus;
                if (signaling_snapshot.OpenPending) {
                    observer_status->Flags |=
                        NLD_AVRCP_OBSERVER_STATUS_OPEN_PENDING;
                }
                if (signaling_snapshot.ChannelHeld) {
                    observer_status->Flags |=
                        NLD_AVRCP_OBSERVER_STATUS_CHANNEL_HELD;
                }
                if (signaling_snapshot.RemoteDisconnected) {
                    observer_status->Flags |=
                        NLD_AVRCP_OBSERVER_STATUS_REMOTE_DISCONNECTED;
                }
                WdfSpinLockRelease(context->StateLock);
                information = sizeof(*observer_status);
            }
        }
    } else if (io_control_code ==
               IOCTL_NLD_AVRCP_OBSERVER_BEGIN_OBSERVATION) {
        WdfSpinLockAcquire(context->StateLock);
        if (context->ActivationRequested ||
            InterlockedCompareExchange(&context->WorkerRunning, 0, 0) !=
                0) {
            status = STATUS_REQUEST_NOT_ACCEPTED;
        } else if ((context->RuntimeFlags &
                        (NLD_AVRCP_OBSERVER_STATUS_OUTBOUND_OPEN |
                         NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUIRED)) !=
                   (NLD_AVRCP_OBSERVER_STATUS_OUTBOUND_OPEN |
                    NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUIRED)) {
            status = STATUS_DEVICE_NOT_READY;
        } else {
            context->ActivationRequested = TRUE;
            (void)InterlockedExchange(&context->WorkerRunning, 1);
            context->RuntimeFlags &=
                ~NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUIRED;
            context->RuntimeFlags |=
                NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUESTED;
            status = STATUS_SUCCESS;
            start_observation = TRUE;
        }
        WdfSpinLockRelease(context->StateLock);
        if (start_observation) {
            WdfWorkItemEnqueue(context->ObserverWorkItem);
        }
    } else if (io_control_code ==
               IOCTL_NLD_AVRCP_OBSERVER_SEND_COMMAND) {
        PNLD_AVRCP_OBSERVER_WRITE_REQUEST write_request;
        if (input_buffer_length < sizeof(*write_request)) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            status = WdfRequestRetrieveInputBuffer(
                request,
                sizeof(*write_request),
                (PVOID*)&write_request,
                NULL);
            if (NT_SUCCESS(status)) {
                if (write_request->Size != sizeof(*write_request) ||
                    write_request->ParameterSize > 8u ||
                    write_request->PduId == 0u ||
                    write_request->Response > 0x0Fu) {
                    status = STATUS_INVALID_PARAMETER;
                } else {
                    WdfSpinLockAcquire(context->StateLock);
                    if (context->PendingWriteValid ||
                        context->WriteTransactionActive ||
                        !context->WorkerRunning ||
                        (context->RuntimeFlags &
                            NLD_AVRCP_OBSERVER_STATUS_CHANNEL_OPEN) == 0u) {
                        WdfSpinLockRelease(context->StateLock);
                        status = STATUS_DEVICE_BUSY;
                    } else {
                        context->PendingWrite = *write_request;
                        context->PendingWriteValid = TRUE;
                        context->WriteTransactionActive = TRUE;
                        WdfSpinLockRelease(context->StateLock);
                        status = STATUS_SUCCESS;
                        information = sizeof(*write_request);
                    }
                }
            }
        }
    } else if (io_control_code ==
               IOCTL_NLD_AVRCP_OBSERVER_DEQUEUE_EVENT) {
        PNLD_AVRCP_OBSERVER_EVENT event;
        if (output_buffer_length < sizeof(*event)) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            status = WdfRequestRetrieveOutputBuffer(
                request,
                sizeof(*event),
                (PVOID*)&event,
                NULL);
            if (NT_SUCCESS(status)) {
                WdfSpinLockAcquire(context->StateLock);
                if (NldAvrcpEventQueuePop(&context->EventQueue, event)) {
                    information = sizeof(*event);
                } else {
                    status = STATUS_NO_MORE_ENTRIES;
                }
                WdfSpinLockRelease(context->StateLock);
            }
        }
    }

    WdfRequestCompleteWithInformation(request, status, information);
}
