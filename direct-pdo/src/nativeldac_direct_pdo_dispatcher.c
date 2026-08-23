// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_direct_pdo_dispatcher.h"

#define NLD_DIRECT_PDO_OPEN_MAX_ATTEMPTS 5u
#define NLD_DIRECT_PDO_OPEN_RETRY_DELAY_MS 750u
#define NLD_DIRECT_PDO_OPEN_SETTLE_MS 5000u

static IO_WORKITEM_ROUTINE NldDirectPdoDispatcherWorker;

static void NldDirectPdoDispatcherTransportDisconnected(
    _In_opt_ PVOID callback_context,
    _In_ ULONG channel_generation);

static NTSTATUS NldDirectPdoDispatcherExecute(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ NLD_DIRECT_PDO_ACTION action);

static BOOLEAN NldDirectPdoDispatcherIsTransientOpenStatus(
    _In_ NTSTATUS status);

static NTSTATUS NldDirectPdoDispatcherOpenSignaling(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context);

static NTSTATUS NldDirectPdoDispatcherAcquireRender(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context);

static NTSTATUS NldDirectPdoDispatcherPreemptDiagnostic(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context);

static void NldDirectPdoDispatcherReleaseRenderIfClosed(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context);

static NTSTATUS NldDirectPdoDispatcherRunAvdtp(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ avdtp_action initial_action,
    _In_ avdtp_action_kind expected_terminal);

static void NldDirectPdoDispatcherSetProtocolDiagnostic(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ NLD_DIRECT_PDO_PROTOCOL_PHASE phase,
    _In_ ULONG signal_id,
    _In_ BOOLEAN command_completed);

static NTSTATUS NldDirectPdoDispatcherCloseSession(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ BOOLEAN graceful);

static void NldDirectPdoDispatcherTransportDisconnected(
    _In_opt_ PVOID callback_context,
    _In_ ULONG channel_generation) {
    PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context =
        (PNLD_DIRECT_PDO_DISPATCHER_CONTEXT)callback_context;
    NLD_DIRECT_PDO_DISPATCH_COMMAND command;
    PIO_WORKITEM work_item = NULL;
    KIRQL old_irql;

    UNREFERENCED_PARAMETER(channel_generation);
    if (context == NULL) return;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    if (!context->Started || context->WorkItem == NULL ||
        context->Owner.StopRequested) {
        KeReleaseSpinLock(&context->Lock, old_irql);
        return;
    }
    command = NldDirectPdoDispatchOnTransportLost(&context->Owner);
    NldMediaWatchdogStop(&context->MediaWatchdog);
    context->MediaWatchdogTimerScheduled = FALSE;
    context->LastMediaWriteStatus = STATUS_CONNECTION_DISCONNECTED;
    if (context->FailureReason == NldDirectPdoFailureNone) {
        context->FailureReason =
            NldDirectPdoFailureRemoteDisconnect;
    }
    if (command == NldDirectPdoDispatchQueueWorker) {
        KeClearEvent(&context->IdleEvent);
        work_item = context->WorkItem;
    }
    KeReleaseSpinLock(&context->Lock, old_irql);

    if (work_item != NULL) {
        IoQueueWorkItem(work_item,
                        NldDirectPdoDispatcherWorker,
                        DelayedWorkQueue,
                        context);
    }
}

static NTSTATUS NldDirectPdoDispatcherOpenMedia(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context) {
    NLD_BTH_SIGNALING_SNAPSHOT snapshot;
    NTSTATUS status;

    status = NldBthSignalingOpen(context->Media, 1021u);
    if (status == STATUS_PENDING) {
        status = NldBthSignalingWaitForRequestDrain(
            context->Media,
            NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);
    }
    if (!NT_SUCCESS(status)) return status;
    NldBthSignalingGetSnapshot(context->Media, &snapshot);
    if (snapshot.State != NldBthSignalingChannelOpen ||
        snapshot.OutgoingMtu == 0u) {
        return NT_SUCCESS(snapshot.LastOpenStatus)
            ? STATUS_DEVICE_NOT_READY
            : snapshot.LastOpenStatus;
    }
    context->MediaMtu = snapshot.OutgoingMtu;
    return STATUS_SUCCESS;
}

static void NldDirectPdoDispatcherSetProtocolDiagnostic(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ NLD_DIRECT_PDO_PROTOCOL_PHASE phase,
    _In_ ULONG signal_id,
    _In_ BOOLEAN command_completed) {
    KIRQL old_irql;

    KeAcquireSpinLock(&context->Lock, &old_irql);
    context->LastProtocolPhase = phase;
    if (signal_id != 0u) context->LastProtocolSignalId = signal_id & 0x3fu;
    if (command_completed && context->ProtocolCommandsCompleted < 0x0fu) {
        context->ProtocolCommandsCompleted++;
    }
    KeReleaseSpinLock(&context->Lock, old_irql);
}

static NTSTATUS NldDirectPdoDispatcherRunAvdtp(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ avdtp_action initial_action,
    _In_ avdtp_action_kind expected_terminal) {
    avdtp_action action = initial_action;
    unsigned char* response;
    ULONG response_length;
    ULONG signal_id;
    NTSTATUS status = STATUS_SUCCESS;

    response = (unsigned char*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        NLD_BTH_MAX_SIGNALING_MTU,
        'AcdL');
    if (response == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    for (;;) {
        if (action.kind == expected_terminal) {
            NldDirectPdoDispatcherSetProtocolDiagnostic(
                context,
                NldDirectPdoProtocolPhaseComplete,
                0u,
                FALSE);
            break;
        }
        if (action.kind == AVDTP_ACTION_ERROR) {
            status = STATUS_DEVICE_PROTOCOL_ERROR;
            break;
        }
        if (action.kind == AVDTP_ACTION_OPEN_MEDIA_CHANNEL) {
            NldDirectPdoDispatcherSetProtocolDiagnostic(
                context,
                NldDirectPdoProtocolPhaseOpenMedia,
                0u,
                FALSE);
            status = NldDirectPdoDispatcherOpenMedia(context);
            if (!NT_SUCCESS(status)) break;
            action = avdtp_source_media_channel_opened(
                &context->AvdtpSource);
            continue;
        }
        if (action.kind != AVDTP_ACTION_SEND_SIGNALING ||
            action.packet_size < 2u ||
            action.packet_size > (size_t)NLD_BTH_MAX_SIGNALING_MTU) {
            status = STATUS_INVALID_DEVICE_STATE;
            break;
        }
        signal_id = (ULONG)(action.packet[1] & 0x3fu);
        NldDirectPdoDispatcherSetProtocolDiagnostic(
            context,
            NldDirectPdoProtocolPhaseWrite,
            signal_id,
            FALSE);
        status = NldBthSignalingWrite(
            context->Signaling,
            action.packet,
            (ULONG)action.packet_size,
            NLD_BTH_DISCOVER_TIMEOUT_MS);
        if (!NT_SUCCESS(status)) break;
        NldDirectPdoDispatcherSetProtocolDiagnostic(
            context,
            NldDirectPdoProtocolPhaseRead,
            signal_id,
            FALSE);
        response_length = 0u;
        status = NldBthSignalingRead(
            context->Signaling,
            response,
            NLD_BTH_MAX_SIGNALING_MTU,
            NLD_BTH_DISCOVER_TIMEOUT_MS,
            &response_length);
        if (!NT_SUCCESS(status)) break;
        NldDirectPdoDispatcherSetProtocolDiagnostic(
            context,
            NldDirectPdoProtocolPhaseHandle,
            signal_id,
            FALSE);
        action = avdtp_source_handle_signaling(
            &context->AvdtpSource,
            response,
            response_length);
        NldDirectPdoDispatcherSetProtocolDiagnostic(
            context,
            NldDirectPdoProtocolPhaseHandle,
            signal_id,
            TRUE);
    }

    ExFreePoolWithTag(response, 'AcdL');
    return status;
}

static NTSTATUS NldDirectPdoDispatcherCloseSession(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ BOOLEAN graceful) {
    NTSTATUS protocol_status = STATUS_SUCCESS;
    NTSTATUS media_status;
    NTSTATUS signaling_status;
    avdtp_action action;

    if (graceful && context->AvdtpInitialized) {
        if (context->AvdtpSource.state == AVDTP_SOURCE_STREAMING) {
            action = avdtp_source_suspend(&context->AvdtpSource);
            protocol_status = NldDirectPdoDispatcherRunAvdtp(
                context,
                action,
                AVDTP_ACTION_STREAM_SUSPENDED);
        }
        if (NT_SUCCESS(protocol_status) &&
            context->AvdtpSource.state == AVDTP_SOURCE_OPEN) {
            action = avdtp_source_close(&context->AvdtpSource);
            protocol_status = NldDirectPdoDispatcherRunAvdtp(
                context,
                action,
                AVDTP_ACTION_SESSION_CLOSED);
        }
    }
    media_status = NldBthSignalingClose(
        context->Media,
        NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);
    signaling_status = NldBthSignalingClose(
        context->Signaling,
        NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);
    context->AvdtpInitialized = FALSE;
    context->MediaMtu = 0u;
    RtlZeroMemory(&context->AvdtpSource,
                  sizeof(context->AvdtpSource));
    if (!NT_SUCCESS(protocol_status)) return protocol_status;
    if (!NT_SUCCESS(media_status)) return media_status;
    return signaling_status;
}

static NTSTATUS NldDirectPdoDispatcherAcquireRender(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context) {
    NLD_DIRECT_PDO_ARBITER_ACQUIRE_RESULT result;
    NLD_DIRECT_PDO_ARBITER_SNAPSHOT snapshot;
    ULONG generation = 0u;

    result = NldDirectPdoArbiterRuntimeTryAcquire(
        context->Arbiter,
        NldDirectPdoArbiterClientRender,
        &generation);
    if (result == NldDirectPdoArbiterAcquireRejected) {
        NldDirectPdoArbiterRuntimeGetSnapshot(context->Arbiter,
                                               &snapshot);
        if (snapshot.RenderDemand &&
            snapshot.Client ==
                NldDirectPdoArbiterClientDiagnostic) {
            return NldDirectPdoDispatcherPreemptDiagnostic(context);
        }
        return STATUS_DEVICE_BUSY;
    }
    context->ArbiterGeneration = generation;
    return STATUS_SUCCESS;
}

static NTSTATUS NldDirectPdoDispatcherPreemptDiagnostic(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context) {
    NLD_DIRECT_PDO_PREEMPTION_ACTION action;
    NLD_DIRECT_PDO_ARBITER_ACQUIRE_RESULT acquire_result;
    ULONG arbiter_generation = 0u;
    unsigned long generation;
    KIRQL old_irql;
    NTSTATUS status = STATUS_DEVICE_BUSY;

    if (context->Diagnostic == NULL) return STATUS_DEVICE_NOT_READY;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    if (!NldDirectPdoPreemptionRequestRender(
            &context->Preemption)) {
        KeReleaseSpinLock(&context->Lock, old_irql);
        return STATUS_DEVICE_BUSY;
    }
    action = NldDirectPdoPreemptionTakeAction(
        &context->Preemption,
        &generation);
    KeReleaseSpinLock(&context->Lock, old_irql);

    while (action != NldDirectPdoPreemptionActionNone) {
        if (action ==
            NldDirectPdoPreemptionActionCancelDiagnostic) {
            status = NldDirectPdoDiagnosticRuntimePreempt(
                context->Diagnostic,
                NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);
        } else if (action ==
                   NldDirectPdoPreemptionActionRetryRender) {
            acquire_result = NldDirectPdoArbiterRuntimeTryAcquire(
                context->Arbiter,
                NldDirectPdoArbiterClientRender,
                &arbiter_generation);
            status = acquire_result ==
                    NldDirectPdoArbiterAcquireRejected
                ? STATUS_DEVICE_BUSY
                : STATUS_SUCCESS;
        } else {
            status = STATUS_INVALID_DEVICE_STATE;
        }

        KeAcquireSpinLock(&context->Lock, &old_irql);
        context->LastPreemptionStatus = status;
        if (NT_SUCCESS(status) &&
            action == NldDirectPdoPreemptionActionRetryRender) {
            context->ArbiterGeneration = arbiter_generation;
        }
        (void)NldDirectPdoPreemptionCompleteAction(
            &context->Preemption,
            generation,
            action,
            NT_SUCCESS(status));
        action = NldDirectPdoPreemptionTakeAction(
            &context->Preemption,
            &generation);
        KeReleaseSpinLock(&context->Lock, old_irql);
    }
    return status;
}

static void NldDirectPdoDispatcherReleaseRenderIfClosed(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context) {
    NLD_BTH_SIGNALING_SNAPSHOT signaling_snapshot;

    if (context->Arbiter == NULL ||
        context->ArbiterGeneration == 0u) {
        return;
    }
    NldBthSignalingGetSnapshot(context->Signaling,
                               &signaling_snapshot);
    if (signaling_snapshot.State != NldBthSignalingClosed) return;
    if (NldDirectPdoArbiterRuntimeRelease(
            context->Arbiter,
            NldDirectPdoArbiterClientRender,
            context->ArbiterGeneration)) {
        context->ArbiterGeneration = 0u;
    }
}

static BOOLEAN NldDirectPdoDispatcherIsTransientOpenStatus(
    _In_ NTSTATUS status) {
    return status == STATUS_REQUEST_NOT_ACCEPTED ||
           status == STATUS_DEVICE_BUSY ||
           status == STATUS_DEVICE_NOT_READY ||
           status == STATUS_DEVICE_NOT_CONNECTED ||
           status == STATUS_TIMEOUT ||
           status == STATUS_IO_TIMEOUT ||
           status == STATUS_CONNECTION_DISCONNECTED;
}

static NTSTATUS NldDirectPdoDispatcherOpenSignaling(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context) {
    NLD_BTH_SIGNALING_SNAPSHOT signaling_snapshot;
    LARGE_INTEGER delay;
    LARGE_INTEGER settle_delay;
    NTSTATUS status = STATUS_DEVICE_NOT_READY;
    ULONG attempt;
    KIRQL old_irql;
    ULONGLONG now_100ns;
    ULONGLONG settle_100ns;
    ULONGLONG elapsed_100ns;

    delay.QuadPart = -((LONGLONG)NLD_DIRECT_PDO_OPEN_RETRY_DELAY_MS *
                       10ll * 1000ll);
    now_100ns = KeQueryInterruptTime();
    settle_100ns = (ULONGLONG)NLD_DIRECT_PDO_OPEN_SETTLE_MS *
                   10ull * 1000ull;
    elapsed_100ns = now_100ns >= context->StartedInterruptTime100ns
        ? now_100ns - context->StartedInterruptTime100ns
        : 0ull;
    if (elapsed_100ns < settle_100ns) {
        settle_delay.QuadPart = -(LONGLONG)(settle_100ns - elapsed_100ns);
        status = KeWaitForSingleObject(&context->OpenRetryEvent,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       &settle_delay);
        if (status == STATUS_SUCCESS) return STATUS_CANCELLED;
        if (status != STATUS_TIMEOUT) return status;
    }
    for (attempt = 1u;
         attempt <= NLD_DIRECT_PDO_OPEN_MAX_ATTEMPTS;
         ++attempt) {
        KeAcquireSpinLock(&context->Lock, &old_irql);
        context->LastOpenAttempts = attempt;
        KeReleaseSpinLock(&context->Lock, old_irql);
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
        if (NT_SUCCESS(status)) return status;

        (void)NldBthSignalingClose(
            context->Signaling,
            NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);
        if (attempt == NLD_DIRECT_PDO_OPEN_MAX_ATTEMPTS ||
            !NldDirectPdoDispatcherIsTransientOpenStatus(status)) {
            return status;
        }
        status = KeWaitForSingleObject(&context->OpenRetryEvent,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       &delay);
        if (status == STATUS_SUCCESS) return STATUS_CANCELLED;
        if (status != STATUS_TIMEOUT) return status;
    }
    return status;
}

static NTSTATUS NldDirectPdoDispatcherExecute(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ NLD_DIRECT_PDO_ACTION action) {
    ldac_capabilities capabilities;
    ULONG preferred_sample_rate_hz;
    KIRQL old_irql;
    NTSTATUS status;

    switch (action) {
        case NldDirectPdoActionOpen:
            KeAcquireSpinLock(&context->Lock, &old_irql);
            context->LastProtocolPhase = NldDirectPdoProtocolPhaseNone;
            context->LastProtocolSignalId = 0u;
            context->ProtocolCommandsCompleted = 0u;
            KeReleaseSpinLock(&context->Lock, old_irql);
            status = NldDirectPdoDispatcherOpenSignaling(context);
            if (!NT_SUCCESS(status)) return status;
            capabilities.sample_rates = LDAC_SF_ALL;
            capabilities.channel_modes = LDAC_CM_ALL;
            KeAcquireSpinLock(&context->Lock, &old_irql);
            preferred_sample_rate_hz = context->PreferredSampleRateHz;
            KeReleaseSpinLock(&context->Lock, old_irql);
            avdtp_source_init(&context->AvdtpSource,
                              capabilities,
                              1u,
                              preferred_sample_rate_hz);
            context->AvdtpInitialized = TRUE;
            status = NldDirectPdoDispatcherRunAvdtp(
                context,
                avdtp_source_begin(&context->AvdtpSource),
                AVDTP_ACTION_SESSION_OPEN);
            if (!NT_SUCCESS(status)) {
                (void)NldDirectPdoDispatcherCloseSession(context, FALSE);
            }
            return status;

        case NldDirectPdoActionClose:
            return NldDirectPdoDispatcherCloseSession(context, TRUE);

        case NldDirectPdoActionCancelAndClose:
            return NldDirectPdoDispatcherCloseSession(context, FALSE);

        case NldDirectPdoActionStart:
            if (!context->AvdtpInitialized) {
                return STATUS_DEVICE_NOT_READY;
            }
            return NldDirectPdoDispatcherRunAvdtp(
                context,
                avdtp_source_start(&context->AvdtpSource),
                AVDTP_ACTION_STREAM_READY);

        case NldDirectPdoActionSuspend:
            if (!context->AvdtpInitialized) {
                return STATUS_DEVICE_NOT_READY;
            }
            return NldDirectPdoDispatcherRunAvdtp(
                context,
                avdtp_source_suspend(&context->AvdtpSource),
                AVDTP_ACTION_STREAM_SUSPENDED);

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

static VOID NldDirectPdoDispatcherWorker(
    _In_ PDEVICE_OBJECT device_object,
    _In_opt_ PVOID work_item_context) {
    PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context =
        (PNLD_DIRECT_PDO_DISPATCHER_CONTEXT)work_item_context;
    NLD_DIRECT_PDO_ACTION action;
    ULONG generation;
    KIRQL old_irql;
    NTSTATUS status;
    NLD_DIRECT_PDO_STATE_CALLBACK state_callback;
    PVOID state_callback_context;

    UNREFERENCED_PARAMETER(device_object);
    if (context == NULL) return;

    for (;;) {
        KeAcquireSpinLock(&context->Lock, &old_irql);
        action = NldDirectPdoDispatchTakeAction(&context->Owner,
                                                &generation);
        if (action == NldDirectPdoActionNone) {
            KeSetEvent(&context->IdleEvent, IO_NO_INCREMENT, FALSE);
            KeReleaseSpinLock(&context->Lock, old_irql);
            return;
        }
        KeReleaseSpinLock(&context->Lock, old_irql);

        if (action == NldDirectPdoActionOpen ||
            action == NldDirectPdoActionStart ||
            action == NldDirectPdoActionSuspend) {
            status = NldDirectPdoDispatcherAcquireRender(context);
        } else {
            status = STATUS_SUCCESS;
        }
        if (NT_SUCCESS(status)) {
            status = NldDirectPdoDispatcherExecute(context, action);
        }
        NldDirectPdoDispatcherReleaseRenderIfClosed(context);

        KeAcquireSpinLock(&context->Lock, &old_irql);
        state_callback = NULL;
        state_callback_context = NULL;
        context->LastAction = action;
        context->LastStatus = status;
        if (!NT_SUCCESS(status) &&
            context->FailureReason == NldDirectPdoFailureNone) {
            context->FailureReason = NldDirectPdoFailureBackend;
        }
        if (NldDirectPdoDispatchCompleteAction(&context->Owner,
                                               generation,
                                               action,
                                               NT_SUCCESS(status))) {
            if (action == NldDirectPdoActionStart &&
                NT_SUCCESS(status) &&
                context->Owner.Session.TransportState ==
                    NldDirectPdoTransportStreaming) {
                (void)NldMediaWatchdogArm(
                    &context->MediaWatchdog,
                    context->Owner.Session.Generation,
                    KeQueryInterruptTime());
                context->MediaWatchdogTimerScheduled = FALSE;
            } else if (action == NldDirectPdoActionSuspend ||
                       action == NldDirectPdoActionClose ||
                       action == NldDirectPdoActionCancelAndClose ||
                       !NT_SUCCESS(status)) {
                NldMediaWatchdogStop(&context->MediaWatchdog);
                context->MediaWatchdogTimerScheduled = FALSE;
            }
            state_callback = context->StateCallback;
            state_callback_context = context->StateCallbackContext;
        }
        KeReleaseSpinLock(&context->Lock, old_irql);
        if (state_callback != NULL) {
            state_callback(state_callback_context);
        }
    }
}

void NldDirectPdoDispatcherInitialize(
    _Out_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context) {
    if (context == NULL) return;
    RtlZeroMemory(context, sizeof(*context));
    KeInitializeSpinLock(&context->Lock);
    KeInitializeEvent(&context->IdleEvent, NotificationEvent, TRUE);
    KeInitializeEvent(&context->OpenRetryEvent,
                      SynchronizationEvent,
                      FALSE);
    NldDirectPdoDispatchInitialize(&context->Owner);
    NldDirectPdoPreemptionInitialize(&context->Preemption);
    NldMediaWatchdogInitialize(&context->MediaWatchdog);
    context->LastAction = NldDirectPdoActionNone;
    context->LastStatus = STATUS_NOT_SUPPORTED;
    context->LastOpenAttempts = 0u;
    context->LastProtocolPhase = NldDirectPdoProtocolPhaseNone;
    context->LastProtocolSignalId = 0u;
    context->ProtocolCommandsCompleted = 0u;
    context->StartedInterruptTime100ns = 0ull;
    context->LastMediaWriteStatus = STATUS_NOT_SUPPORTED;
    context->FailureReason = NldDirectPdoFailureNone;
    NldMediaWatchdogStop(&context->MediaWatchdog);
    context->MediaWatchdogTimerScheduled = FALSE;
    context->PreferredSampleRateHz = 48000u;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldDirectPdoDispatcherStart(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT signaling,
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT media,
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT arbiter,
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT diagnostic,
    _In_ PDEVICE_OBJECT reference_device_object) {
    NLD_DIRECT_PDO_ARBITER_SNAPSHOT arbiter_snapshot;
    NLD_BTH_SIGNALING_SNAPSHOT signaling_snapshot;
    PIO_WORKITEM work_item;
    KIRQL old_irql;

    if (context == NULL || signaling == NULL || media == NULL ||
        arbiter == NULL ||
        diagnostic == NULL ||
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
    NldBthSignalingGetSnapshot(media, &signaling_snapshot);
    if (signaling_snapshot.State != NldBthSignalingClosed) {
        return STATUS_DEVICE_NOT_READY;
    }

    work_item = IoAllocateWorkItem(reference_device_object);
    if (work_item == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    ObReferenceObject(reference_device_object);

    KeAcquireSpinLock(&context->Lock, &old_irql);
    if (context->Started || context->WorkItem != NULL ||
        context->ReferenceDeviceObject != NULL) {
        KeReleaseSpinLock(&context->Lock, old_irql);
        ObDereferenceObject(reference_device_object);
        IoFreeWorkItem(work_item);
        return STATUS_INVALID_DEVICE_STATE;
    }
    context->Signaling = signaling;
    context->Media = media;
    context->Arbiter = arbiter;
    context->Diagnostic = diagnostic;
    context->ReferenceDeviceObject = reference_device_object;
    context->WorkItem = work_item;
    context->Started = TRUE;
    context->LastAction = NldDirectPdoActionNone;
    context->LastStatus = STATUS_NOT_SUPPORTED;
    context->LastOpenAttempts = 0u;
    context->StartedInterruptTime100ns = KeQueryInterruptTime();
    context->LastPreemptionStatus = STATUS_NOT_SUPPORTED;
    context->ArbiterGeneration = 0u;
    context->MediaMtu = 0u;
    context->MediaPacketsAccepted = 0u;
    context->MediaBytesAccepted = 0u;
    context->LastMediaWriteStatus = STATUS_NOT_SUPPORTED;
    context->FailureReason = NldDirectPdoFailureNone;
    context->AvdtpInitialized = FALSE;
    KeClearEvent(&context->OpenRetryEvent);
    KeSetEvent(&context->IdleEvent, IO_NO_INCREMENT, FALSE);
    (void)NldDirectPdoDispatchOnPnpStart(&context->Owner);
    (void)NldDirectPdoPreemptionOnPnpStart(
        &context->Preemption);
    KeReleaseSpinLock(&context->Lock, old_irql);
    NldBthSignalingSetDisconnectCallback(
        signaling,
        NldDirectPdoDispatcherTransportDisconnected,
        context);
    NldBthSignalingSetDisconnectCallback(
        media,
        NldDirectPdoDispatcherTransportDisconnected,
        context);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS NldDirectPdoDispatcherSetFormat(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ ULONG sample_rate_hz) {
    KIRQL old_irql;

    if (context == NULL ||
        (sample_rate_hz != 44100u && sample_rate_hz != 48000u &&
         sample_rate_hz != 88200u && sample_rate_hz != 96000u)) {
        return STATUS_INVALID_PARAMETER;
    }
    KeAcquireSpinLock(&context->Lock, &old_irql);
    if (!context->Started || context->Owner.StopRequested) {
        KeReleaseSpinLock(&context->Lock, old_irql);
        return STATUS_DEVICE_NOT_READY;
    }
    context->PreferredSampleRateHz = sample_rate_hz;
    KeReleaseSpinLock(&context->Lock, old_irql);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldDirectPdoDispatcherSetStateCallback(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_opt_ NLD_DIRECT_PDO_STATE_CALLBACK callback,
    _In_opt_ PVOID callback_context) {
    KIRQL old_irql;

    if (context == NULL) return;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    context->StateCallback = callback;
    context->StateCallbackContext = callback_context;
    KeReleaseSpinLock(&context->Lock, old_irql);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS NldDirectPdoDispatcherRequestRecovery(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ const NLD_DIRECT_PDO_RECOVERY_REQUEST_V1* request,
    _In_ ULONG request_size) {
    NLD_DIRECT_PDO_RECOVERY_VALIDATION validation;
    NLD_DIRECT_PDO_DISPATCH_COMMAND command;
    NLD_DIRECT_PDO_STATE_CALLBACK state_callback = NULL;
    PVOID state_callback_context = NULL;
    PIO_WORKITEM work_item = NULL;
    ULONG previous_generation;
    KIRQL old_irql;
    NTSTATUS status = STATUS_SUCCESS;

    if (context == NULL || request == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    KeAcquireSpinLock(&context->Lock, &old_irql);
    validation = NldDirectPdoValidateRecoveryRequest(
        request,
        request_size,
        context->Owner.Session.Generation,
        context->FailureReason);
    if (validation != NldDirectPdoRecoveryValidationOk) {
        status = validation == NldDirectPdoRecoveryValidationGeneration ||
                         validation == NldDirectPdoRecoveryValidationReason
                     ? STATUS_RETRY
                     : STATUS_INVALID_PARAMETER;
    } else if (!context->Started || context->Owner.StopRequested ||
               context->WorkItem == NULL) {
        status = STATUS_DEVICE_NOT_READY;
    } else if (context->Owner.Session.TransportState !=
                   NldDirectPdoTransportFaulted ||
               context->Owner.Session.KsIntent !=
                   NldDirectPdoKsStopped ||
               context->Owner.WorkerOwned ||
               context->Owner.ActiveAction != NldDirectPdoActionNone ||
               context->Owner.Session.PendingAction !=
                   NldDirectPdoActionNone ||
               context->ArbiterGeneration != 0u) {
        status = STATUS_DEVICE_BUSY;
    } else {
        previous_generation = context->Owner.Session.Generation;
        command = NldDirectPdoDispatchRetry(&context->Owner);
        if (context->Owner.Session.Generation == previous_generation ||
            context->Owner.Session.TransportState !=
                NldDirectPdoTransportClosed) {
            status = STATUS_INVALID_DEVICE_STATE;
        } else {
            context->FailureReason = NldDirectPdoFailureNone;
            context->LastStatus = STATUS_SUCCESS;
            context->LastMediaWriteStatus = STATUS_NOT_SUPPORTED;
            state_callback = context->StateCallback;
            state_callback_context = context->StateCallbackContext;
            if (command == NldDirectPdoDispatchQueueWorker) {
                KeClearEvent(&context->IdleEvent);
                work_item = context->WorkItem;
            }
        }
    }
    KeReleaseSpinLock(&context->Lock, old_irql);

    if (work_item != NULL) {
        IoQueueWorkItem(work_item,
                        NldDirectPdoDispatcherWorker,
                        DelayedWorkQueue,
                        context);
    }
    if (NT_SUCCESS(status) && state_callback != NULL) {
        state_callback(state_callback_context);
    }
    return status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS NldDirectPdoDispatcherSetIntent(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ NLD_DIRECT_PDO_KS_INTENT intent) {
    NLD_DIRECT_PDO_DISPATCH_COMMAND command;
    NLD_DIRECT_PDO_ARBITER_DEMAND_RESULT demand_result;
    PIO_WORKITEM work_item = NULL;
    KIRQL old_irql;

    if (context == NULL || intent < NldDirectPdoKsStopped ||
        intent > NldDirectPdoKsRunning) {
        return STATUS_INVALID_PARAMETER;
    }
    KeAcquireSpinLock(&context->Lock, &old_irql);
    if (!context->Started || context->WorkItem == NULL ||
        context->Owner.StopRequested) {
        KeReleaseSpinLock(&context->Lock, old_irql);
        return STATUS_DEVICE_NOT_READY;
    }
    demand_result = NldDirectPdoArbiterRuntimeSetRenderDemand(
        context->Arbiter,
        intent != NldDirectPdoKsStopped);
    if (demand_result == NldDirectPdoArbiterDemandRejected) {
        KeReleaseSpinLock(&context->Lock, old_irql);
        return STATUS_DEVICE_NOT_READY;
    }
    if (intent != NldDirectPdoKsRunning) {
        NldMediaWatchdogStop(&context->MediaWatchdog);
        context->MediaWatchdogTimerScheduled = FALSE;
    }
    command = NldDirectPdoDispatchSetKsIntent(&context->Owner, intent);
    if (command == NldDirectPdoDispatchQueueWorker) {
        KeClearEvent(&context->IdleEvent);
        work_item = context->WorkItem;
    }
    KeReleaseSpinLock(&context->Lock, old_irql);

    if (work_item != NULL) {
        IoQueueWorkItem(work_item,
                        NldDirectPdoDispatcherWorker,
                        DelayedWorkQueue,
                        context);
    }
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
void NldDirectPdoDispatcherStop(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ ULONG timeout_ms) {
    NLD_DIRECT_PDO_DISPATCH_COMMAND command;
    PNLD_BTH_SIGNALING_CONTEXT signaling;
    PNLD_BTH_SIGNALING_CONTEXT media;
    PDEVICE_OBJECT reference_device_object;
    PIO_WORKITEM work_item;
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
    media = context->Media;
    work_item = context->WorkItem;
    (void)NldDirectPdoArbiterRuntimeSetRenderDemand(
        context->Arbiter,
        FALSE);
    (void)NldDirectPdoPreemptionOnPnpStop(
        &context->Preemption);
    command = NldDirectPdoDispatchOnPnpStop(&context->Owner);
    KeSetEvent(&context->OpenRetryEvent, IO_NO_INCREMENT, FALSE);
    cancel_active = command == NldDirectPdoDispatchCancelActive;
    queue_worker = command == NldDirectPdoDispatchQueueWorker;
    if (queue_worker) KeClearEvent(&context->IdleEvent);
    KeReleaseSpinLock(&context->Lock, old_irql);

    if (signaling != NULL) {
        NldBthSignalingSetDisconnectCallback(signaling, NULL, NULL);
    }
    if (media != NULL) {
        NldBthSignalingSetDisconnectCallback(media, NULL, NULL);
    }

    if (queue_worker) {
        IoQueueWorkItem(work_item,
                        NldDirectPdoDispatcherWorker,
                        DelayedWorkQueue,
                        context);
    }
    if (cancel_active && signaling != NULL) {
        (void)NldBthSignalingClose(signaling, timeout_ms);
    }
    if (cancel_active && media != NULL) {
        (void)NldBthSignalingClose(media, timeout_ms);
    }
    (void)KeWaitForSingleObject(&context->IdleEvent,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    if (signaling != NULL) {
        NldBthSignalingStop(signaling, timeout_ms);
    }
    if (media != NULL) {
        NldBthSignalingStop(media, timeout_ms);
    }
    if (context->Arbiter != NULL &&
        context->ArbiterGeneration != 0u) {
        (void)NldDirectPdoArbiterRuntimeRelease(
            context->Arbiter,
            NldDirectPdoArbiterClientRender,
            context->ArbiterGeneration);
        context->ArbiterGeneration = 0u;
    }

    KeAcquireSpinLock(&context->Lock, &old_irql);
    reference_device_object = context->ReferenceDeviceObject;
    work_item = context->WorkItem;
    context->Signaling = NULL;
    context->Media = NULL;
    context->Arbiter = NULL;
    context->Diagnostic = NULL;
    context->ReferenceDeviceObject = NULL;
    context->WorkItem = NULL;
    context->Started = FALSE;
    context->AvdtpInitialized = FALSE;
    context->MediaMtu = 0u;
    context->FailureReason = NldDirectPdoFailureNone;
    NldMediaWatchdogStop(&context->MediaWatchdog);
    context->MediaWatchdogTimerScheduled = FALSE;
    context->StateCallback = NULL;
    context->StateCallbackContext = NULL;
    KeReleaseSpinLock(&context->Lock, old_irql);

    if (work_item != NULL) IoFreeWorkItem(work_item);
    if (reference_device_object != NULL) {
        ObDereferenceObject(reference_device_object);
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldDirectPdoDispatcherGetSnapshot(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _Out_ PNLD_DIRECT_PDO_DISPATCHER_SNAPSHOT snapshot) {
    KIRQL old_irql;

    if (context == NULL || snapshot == NULL) return;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    snapshot->KsIntent = context->Owner.Session.KsIntent;
    snapshot->TransportState = context->Owner.Session.TransportState;
    snapshot->PendingAction = context->Owner.Session.PendingAction;
    snapshot->ActiveAction = context->Owner.ActiveAction;
    snapshot->LastAction = context->LastAction;
    snapshot->Generation = context->Owner.Session.Generation;
    snapshot->ArbiterGeneration = context->ArbiterGeneration;
    snapshot->LastStatus = context->LastStatus;
    snapshot->FailureReason = context->FailureReason;
    snapshot->PreemptionState = context->Preemption.State;
    snapshot->PreemptionPendingAction =
        context->Preemption.PendingAction;
    snapshot->PreemptionActiveAction =
        context->Preemption.ActiveAction;
    snapshot->PreemptionGeneration =
        (ULONG)context->Preemption.Generation;
    snapshot->LastPreemptionStatus =
        context->LastPreemptionStatus;
    snapshot->WorkerOwned = context->Owner.WorkerOwned != 0;
    snapshot->StopRequested = context->Owner.StopRequested != 0;
    snapshot->Started = context->Started;
    KeReleaseSpinLock(&context->Lock, old_irql);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldDirectPdoDispatcherGetMediaStatus(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _Out_ NLD_DIRECT_PDO_MEDIA_STATUS_V1* status) {
    PNLD_BTH_SIGNALING_CONTEXT media;
    NLD_BTH_SIGNALING_SNAPSHOT media_snapshot;
    NLD_BTH_SIGNALING_SNAPSHOT signaling_snapshot;
    PNLD_BTH_SIGNALING_CONTEXT signaling;
    NLD_DIRECT_PDO_TRANSPORT_STATE transport_state;
    NLD_DIRECT_PDO_KS_INTENT ks_intent;
    BOOLEAN started;
    BOOLEAN stopping;
    KIRQL old_irql;

    if (context == NULL || status == NULL) return;
    RtlZeroMemory(status, sizeof(*status));
    status->Size = sizeof(*status);
    status->Version = NLD_DIRECT_PDO_MEDIA_ABI_VERSION;

    KeAcquireSpinLock(&context->Lock, &old_irql);
    media = context->Media;
    signaling = context->Signaling;
    started = context->Started;
    stopping = context->Owner.StopRequested != 0;
    transport_state = context->Owner.Session.TransportState;
    ks_intent = context->Owner.Session.KsIntent;
    status->LastStatus = context->LastMediaWriteStatus;
    status->LastBackendAction = context->LastAction;
    status->LastBackendStatus = context->LastStatus;
    if (context->Owner.ActiveAction != NldDirectPdoActionNone) {
        status->Flags |= NLD_DIRECT_PDO_MEDIA_STATUS_BACKEND_ACTIVE;
    }
    status->Flags |=
        (context->LastOpenAttempts <<
         NLD_DIRECT_PDO_MEDIA_STATUS_OPEN_ATTEMPTS_SHIFT) &
        NLD_DIRECT_PDO_MEDIA_STATUS_OPEN_ATTEMPTS_MASK;
    status->Flags |=
        ((ULONG)context->LastProtocolPhase <<
         NLD_DIRECT_PDO_MEDIA_STATUS_PROTOCOL_PHASE_SHIFT) &
        NLD_DIRECT_PDO_MEDIA_STATUS_PROTOCOL_PHASE_MASK;
    status->Flags |=
        (context->ProtocolCommandsCompleted <<
         NLD_DIRECT_PDO_MEDIA_STATUS_COMMANDS_COMPLETED_SHIFT) &
        NLD_DIRECT_PDO_MEDIA_STATUS_COMMANDS_COMPLETED_MASK;
    status->Flags |=
        (context->LastProtocolSignalId <<
         NLD_DIRECT_PDO_MEDIA_STATUS_SIGNAL_ID_SHIFT) &
        NLD_DIRECT_PDO_MEDIA_STATUS_SIGNAL_ID_MASK;
    status->SessionGeneration = context->Owner.Session.Generation;
    status->FailureReason = context->FailureReason;
    status->PacketsAccepted = context->MediaPacketsAccepted;
    status->BytesAccepted = context->MediaBytesAccepted;
    KeReleaseSpinLock(&context->Lock, old_irql);

    if (signaling != NULL) {
        NldBthSignalingGetSnapshot(signaling, &signaling_snapshot);
        status->LastSignalingOpenStatus = signaling_snapshot.LastOpenStatus;
    }

    if (!started || media == NULL) {
        status->State = NldDirectPdoMediaOffline;
        return;
    }
    status->Flags |= NLD_DIRECT_PDO_MEDIA_STATUS_PNP_STARTED;
    if (ks_intent == NldDirectPdoKsRunning) {
        status->Flags |= NLD_DIRECT_PDO_MEDIA_STATUS_WAVERT_RUN;
    }
    if (stopping) {
        status->State = NldDirectPdoMediaStopping;
        status->Flags |= NLD_DIRECT_PDO_MEDIA_STATUS_STOPPING;
        return;
    }

    NldBthSignalingGetSnapshot(media, &media_snapshot);
    status->MediaGeneration = media_snapshot.Generation;
    status->OutgoingMtu = media_snapshot.OutgoingMtu;
    if (media_snapshot.State == NldBthSignalingChannelOpen) {
        status->Flags |= NLD_DIRECT_PDO_MEDIA_STATUS_CHANNEL_OPEN;
    }
    if (transport_state == NldDirectPdoTransportFaulted) {
        status->State = NldDirectPdoMediaFaulted;
    } else if (transport_state == NldDirectPdoTransportStreaming &&
               ks_intent == NldDirectPdoKsRunning &&
               media_snapshot.State == NldBthSignalingChannelOpen) {
        status->State = NldDirectPdoMediaStreaming;
        status->Flags |= NLD_DIRECT_PDO_MEDIA_STATUS_STREAMING;
    } else if (media_snapshot.State == NldBthSignalingChannelOpen) {
        status->State = NldDirectPdoMediaOpen;
    } else {
        status->State = NldDirectPdoMediaIdle;
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldDirectPdoDispatcherWriteMedia(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ ULONG media_generation,
    _In_reads_bytes_(packet_length) const void* packet,
    _In_ ULONG packet_length) {
    NLD_DIRECT_PDO_MEDIA_STATUS_V1 media_status;
    NLD_DIRECT_PDO_MEDIA_PACKET_V1 packet_header;
    PNLD_BTH_SIGNALING_CONTEXT media;
    NTSTATUS status;
    KIRQL old_irql;

    PAGED_CODE();
    if (context == NULL || packet == NULL || packet_length == 0u ||
        packet_length > NLD_DIRECT_PDO_MEDIA_MAX_PACKET_SIZE) {
        return STATUS_INVALID_PARAMETER;
    }
    NldDirectPdoDispatcherGetMediaStatus(context, &media_status);
    RtlZeroMemory(&packet_header, sizeof(packet_header));
    packet_header.Size = NLD_DIRECT_PDO_MEDIA_PACKET_HEADER_SIZE +
                         packet_length;
    packet_header.Version = NLD_DIRECT_PDO_MEDIA_ABI_VERSION;
    packet_header.MediaGeneration = media_generation;
    packet_header.PayloadLength = packet_length;
    if (NldDirectPdoMediaValidatePacket(
            &packet_header,
            packet_header.Size,
            &media_status) != NldDirectPdoMediaValidationOk) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    KeAcquireSpinLock(&context->Lock, &old_irql);
    media = context->Media;
    KeReleaseSpinLock(&context->Lock, old_irql);
    if (media == NULL) return STATUS_DEVICE_NOT_READY;
    status = NldBthSignalingWriteGeneration(
        media,
        media_generation,
        packet,
        packet_length,
        NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);

    KeAcquireSpinLock(&context->Lock, &old_irql);
    context->LastMediaWriteStatus = status;
    if (NT_SUCCESS(status)) {
        context->MediaPacketsAccepted++;
        context->MediaBytesAccepted += packet_length;
        (void)NldMediaWatchdogRecordWrite(
            &context->MediaWatchdog,
            context->Owner.Session.Generation,
            KeQueryInterruptTime());
    }
    KeReleaseSpinLock(&context->Lock, old_irql);
    return status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
ULONG NldDirectPdoDispatcherScheduleMediaWatchdog(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ ULONG timeout_ms) {
    ULONG delay_ms = 0u;
    KIRQL old_irql;

    if (context == NULL || timeout_ms == 0u) return 0u;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    if (context->Started && !context->Owner.StopRequested &&
        context->Owner.Session.KsIntent == NldDirectPdoKsRunning &&
        context->Owner.Session.TransportState ==
            NldDirectPdoTransportStreaming &&
        context->MediaWatchdog.Armed &&
        !context->MediaWatchdogTimerScheduled) {
        context->MediaWatchdogTimerScheduled = TRUE;
        delay_ms = timeout_ms;
    }
    KeReleaseSpinLock(&context->Lock, old_irql);
    return delay_ms;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
ULONG NldDirectPdoDispatcherCheckMediaWatchdog(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ ULONG timeout_ms) {
    NLD_MEDIA_WATCHDOG_RESULT watchdog_result;
    NLD_DIRECT_PDO_DISPATCH_COMMAND command = NldDirectPdoDispatchNone;
    unsigned long long remaining100ns = 0ull;
    unsigned long long timeout100ns;
    PIO_WORKITEM work_item = NULL;
    ULONG delay_ms = 0u;
    KIRQL old_irql;

    if (context == NULL || timeout_ms == 0u) return 0u;
    timeout100ns = (unsigned long long)timeout_ms * 10000ull;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    context->MediaWatchdogTimerScheduled = FALSE;
    watchdog_result = NldMediaWatchdogCheck(
        &context->MediaWatchdog,
        context->Owner.Session.Generation,
        KeQueryInterruptTime(),
        timeout100ns,
        &remaining100ns);
    if (watchdog_result == NldMediaWatchdogWaiting) {
        delay_ms = (ULONG)((remaining100ns + 9999ull) / 10000ull);
        if (delay_ms == 0u) delay_ms = 1u;
        context->MediaWatchdogTimerScheduled = TRUE;
    } else if (watchdog_result == NldMediaWatchdogExpired &&
               context->Started && !context->Owner.StopRequested) {
        command = NldDirectPdoDispatchOnTransportLost(&context->Owner);
        context->LastMediaWriteStatus = STATUS_IO_TIMEOUT;
        if (context->FailureReason == NldDirectPdoFailureNone) {
            context->FailureReason = NldDirectPdoFailureMediaTimeout;
        }
        if (command == NldDirectPdoDispatchQueueWorker) {
            KeClearEvent(&context->IdleEvent);
            work_item = context->WorkItem;
        }
    }
    KeReleaseSpinLock(&context->Lock, old_irql);

    if (work_item != NULL) {
        IoQueueWorkItem(work_item,
                        NldDirectPdoDispatcherWorker,
                        DelayedWorkQueue,
                        context);
    }
    return delay_ms;
}
