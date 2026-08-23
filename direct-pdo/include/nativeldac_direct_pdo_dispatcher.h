// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_DIRECT_PDO_DISPATCHER_H
#define NATIVE_LDAC_DIRECT_PDO_DISPATCHER_H

#include <ntddk.h>

#include "nativeldac_bth_signaling.h"
#include "nativeldac_direct_pdo_arbiter.h"
#include "nativeldac_direct_pdo_diagnostic.h"
#include "nativeldac_direct_pdo_dispatch_contract.h"
#include "nativeldac_direct_pdo_preemption_contract.h"
#include "nativeldac_direct_pdo_media_abi.h"
#include "nativeldac_media_watchdog_contract.h"
#include "ldac_native/avdtp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*NLD_DIRECT_PDO_STATE_CALLBACK)(
    _In_opt_ PVOID callback_context);

typedef struct _NLD_DIRECT_PDO_DISPATCHER_CONTEXT {
    KSPIN_LOCK Lock;
    KEVENT IdleEvent;
    KEVENT OpenRetryEvent;
    NLD_DIRECT_PDO_DISPATCH_OWNER Owner;
    NLD_DIRECT_PDO_PREEMPTION_OWNER Preemption;
    PNLD_BTH_SIGNALING_CONTEXT Signaling;
    PNLD_BTH_SIGNALING_CONTEXT Media;
    PNLD_DIRECT_PDO_ARBITER_CONTEXT Arbiter;
    PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT Diagnostic;
    PDEVICE_OBJECT ReferenceDeviceObject;
    PIO_WORKITEM WorkItem;
    ULONG ArbiterGeneration;
    NLD_DIRECT_PDO_ACTION LastAction;
    NTSTATUS LastStatus;
    ULONG LastOpenAttempts;
    NLD_DIRECT_PDO_PROTOCOL_PHASE LastProtocolPhase;
    ULONG LastProtocolSignalId;
    ULONG ProtocolCommandsCompleted;
    ULONGLONG StartedInterruptTime100ns;
    NTSTATUS LastPreemptionStatus;
    avdtp_source AvdtpSource;
    USHORT MediaMtu;
    ULONG PreferredSampleRateHz;
    ULONGLONG MediaPacketsAccepted;
    ULONGLONG MediaBytesAccepted;
    NTSTATUS LastMediaWriteStatus;
    NLD_DIRECT_PDO_FAILURE_REASON FailureReason;
    NLD_MEDIA_WATCHDOG_OWNER MediaWatchdog;
    BOOLEAN MediaWatchdogTimerScheduled;
    NLD_DIRECT_PDO_STATE_CALLBACK StateCallback;
    PVOID StateCallbackContext;
    BOOLEAN AvdtpInitialized;
    BOOLEAN Started;
} NLD_DIRECT_PDO_DISPATCHER_CONTEXT,
  *PNLD_DIRECT_PDO_DISPATCHER_CONTEXT;

typedef struct _NLD_DIRECT_PDO_DISPATCHER_SNAPSHOT {
    NLD_DIRECT_PDO_KS_INTENT KsIntent;
    NLD_DIRECT_PDO_TRANSPORT_STATE TransportState;
    NLD_DIRECT_PDO_ACTION PendingAction;
    NLD_DIRECT_PDO_ACTION ActiveAction;
    NLD_DIRECT_PDO_ACTION LastAction;
    ULONG Generation;
    ULONG ArbiterGeneration;
    NTSTATUS LastStatus;
    NLD_DIRECT_PDO_FAILURE_REASON FailureReason;
    NLD_DIRECT_PDO_PREEMPTION_STATE PreemptionState;
    NLD_DIRECT_PDO_PREEMPTION_ACTION PreemptionPendingAction;
    NLD_DIRECT_PDO_PREEMPTION_ACTION PreemptionActiveAction;
    ULONG PreemptionGeneration;
    NTSTATUS LastPreemptionStatus;
    BOOLEAN WorkerOwned;
    BOOLEAN StopRequested;
    BOOLEAN Started;
} NLD_DIRECT_PDO_DISPATCHER_SNAPSHOT,
  *PNLD_DIRECT_PDO_DISPATCHER_SNAPSHOT;

void NldDirectPdoDispatcherInitialize(
    _Out_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldDirectPdoDispatcherStart(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT signaling,
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT media,
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT arbiter,
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT diagnostic,
    _In_ PDEVICE_OBJECT reference_device_object);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS NldDirectPdoDispatcherSetIntent(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ NLD_DIRECT_PDO_KS_INTENT intent);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS NldDirectPdoDispatcherSetFormat(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ ULONG sample_rate_hz);

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldDirectPdoDispatcherSetStateCallback(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_opt_ NLD_DIRECT_PDO_STATE_CALLBACK callback,
    _In_opt_ PVOID callback_context);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS NldDirectPdoDispatcherRequestRecovery(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ const NLD_DIRECT_PDO_RECOVERY_REQUEST_V1* request,
    _In_ ULONG request_size);

_IRQL_requires_max_(PASSIVE_LEVEL)
void NldDirectPdoDispatcherStop(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ ULONG timeout_ms);

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldDirectPdoDispatcherGetSnapshot(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _Out_ PNLD_DIRECT_PDO_DISPATCHER_SNAPSHOT snapshot);

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldDirectPdoDispatcherGetMediaStatus(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _Out_ NLD_DIRECT_PDO_MEDIA_STATUS_V1* status);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldDirectPdoDispatcherWriteMedia(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ ULONG media_generation,
    _In_reads_bytes_(packet_length) const void* packet,
    _In_ ULONG packet_length);

_IRQL_requires_max_(DISPATCH_LEVEL)
ULONG NldDirectPdoDispatcherScheduleMediaWatchdog(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ ULONG timeout_ms);

_IRQL_requires_max_(DISPATCH_LEVEL)
ULONG NldDirectPdoDispatcherCheckMediaWatchdog(
    _Inout_ PNLD_DIRECT_PDO_DISPATCHER_CONTEXT context,
    _In_ ULONG timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
