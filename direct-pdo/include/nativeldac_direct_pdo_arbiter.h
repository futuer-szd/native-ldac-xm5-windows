// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_DIRECT_PDO_ARBITER_H
#define NATIVE_LDAC_DIRECT_PDO_ARBITER_H

#include <ntddk.h>

#include "nativeldac_direct_pdo_arbiter_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _NLD_DIRECT_PDO_ARBITER_CONTEXT {
    KSPIN_LOCK Lock;
    NLD_DIRECT_PDO_ARBITER_OWNER Owner;
    BOOLEAN Started;
} NLD_DIRECT_PDO_ARBITER_CONTEXT,
  *PNLD_DIRECT_PDO_ARBITER_CONTEXT;

typedef struct _NLD_DIRECT_PDO_ARBITER_SNAPSHOT {
    NLD_DIRECT_PDO_ARBITER_STATE State;
    NLD_DIRECT_PDO_ARBITER_CLIENT Client;
    ULONG Generation;
    ULONG ActiveGeneration;
    BOOLEAN PnpStarted;
    BOOLEAN StopRequested;
    BOOLEAN RenderDemand;
    BOOLEAN Started;
} NLD_DIRECT_PDO_ARBITER_SNAPSHOT,
  *PNLD_DIRECT_PDO_ARBITER_SNAPSHOT;

void NldDirectPdoArbiterRuntimeInitialize(
    _Out_ PNLD_DIRECT_PDO_ARBITER_CONTEXT context);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS NldDirectPdoArbiterRuntimeStart(
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT context);

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldDirectPdoArbiterRuntimeStop(
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT context);

_IRQL_requires_max_(DISPATCH_LEVEL)
NLD_DIRECT_PDO_ARBITER_DEMAND_RESULT
NldDirectPdoArbiterRuntimeSetRenderDemand(
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT context,
    _In_ BOOLEAN active);

_IRQL_requires_max_(DISPATCH_LEVEL)
NLD_DIRECT_PDO_ARBITER_ACQUIRE_RESULT
NldDirectPdoArbiterRuntimeTryAcquire(
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT context,
    _In_ NLD_DIRECT_PDO_ARBITER_CLIENT client,
    _Out_ ULONG* generation);

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN NldDirectPdoArbiterRuntimeRelease(
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT context,
    _In_ NLD_DIRECT_PDO_ARBITER_CLIENT client,
    _In_ ULONG generation);

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldDirectPdoArbiterRuntimeGetSnapshot(
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT context,
    _Out_ PNLD_DIRECT_PDO_ARBITER_SNAPSHOT snapshot);

#ifdef __cplusplus
}
#endif

#endif
