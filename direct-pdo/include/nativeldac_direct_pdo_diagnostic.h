// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_DIRECT_PDO_DIAGNOSTIC_H
#define NATIVE_LDAC_DIRECT_PDO_DIAGNOSTIC_H

#include <ntddk.h>

#include "nativeldac_bth_signaling.h"
#include "nativeldac_direct_pdo_arbiter.h"
#include "nativeldac_direct_pdo_diagnostic_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NLD_DIRECT_PDO_DIAGNOSTIC_RESPONSE_PREFIX 32u

typedef struct _NLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT {
    KSPIN_LOCK Lock;
    KEVENT IdleEvent;
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER Owner;
    PNLD_BTH_SIGNALING_CONTEXT Signaling;
    PNLD_DIRECT_PDO_ARBITER_CONTEXT Arbiter;
    PDEVICE_OBJECT ReferenceDeviceObject;
    PIO_WORKITEM WorkItem;
    unsigned char* ResponseBuffer;
    unsigned char ResponsePrefix[
        NLD_DIRECT_PDO_DIAGNOSTIC_RESPONSE_PREFIX];
    ULONG ResponseLength;
    ULONG PayloadOffset;
    ULONG ResultGeneration;
    ULONG ArbiterGeneration;
    NLD_DIRECT_PDO_DIAGNOSTIC_ACTION LastAction;
    NTSTATUS LastStatus;
    NTSTATUS DiscoverStatus;
    NTSTATUS CloseStatus;
    BOOLEAN ResponseTruncated;
    BOOLEAN Started;
} NLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT,
  *PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT;

typedef struct _NLD_DIRECT_PDO_DIAGNOSTIC_SNAPSHOT {
    NLD_DIRECT_PDO_DIAGNOSTIC_STATE State;
    NLD_DIRECT_PDO_DIAGNOSTIC_ACTION PendingAction;
    NLD_DIRECT_PDO_DIAGNOSTIC_ACTION ActiveAction;
    NLD_DIRECT_PDO_DIAGNOSTIC_ACTION LastAction;
    ULONG Generation;
    ULONG ResultGeneration;
    ULONG ArbiterGeneration;
    ULONG ResponseLength;
    ULONG PayloadOffset;
    ULONG ResponsePrefixLength;
    NTSTATUS LastStatus;
    NTSTATUS DiscoverStatus;
    NTSTATUS CloseStatus;
    unsigned char ResponsePrefix[
        NLD_DIRECT_PDO_DIAGNOSTIC_RESPONSE_PREFIX];
    BOOLEAN WorkerOwned;
    BOOLEAN StopRequested;
    BOOLEAN CancelRequested;
    BOOLEAN ResponseTruncated;
    BOOLEAN Started;
} NLD_DIRECT_PDO_DIAGNOSTIC_SNAPSHOT,
  *PNLD_DIRECT_PDO_DIAGNOSTIC_SNAPSHOT;

void NldDirectPdoDiagnosticRuntimeInitialize(
    _Out_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT context);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldDirectPdoDiagnosticRuntimeStart(
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT context,
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT signaling,
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT arbiter,
    _In_ PDEVICE_OBJECT reference_device_object);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS NldDirectPdoDiagnosticRuntimeRequestDiscover(
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT context);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldDirectPdoDiagnosticRuntimePreempt(
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT context,
    _In_ ULONG timeout_ms);

_IRQL_requires_max_(PASSIVE_LEVEL)
void NldDirectPdoDiagnosticRuntimeStop(
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT context,
    _In_ ULONG timeout_ms);

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldDirectPdoDiagnosticRuntimeGetSnapshot(
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT context,
    _Out_ PNLD_DIRECT_PDO_DIAGNOSTIC_SNAPSHOT snapshot);

#ifdef __cplusplus
}
#endif

#endif
