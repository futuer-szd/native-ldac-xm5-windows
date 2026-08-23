// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_direct_pdo_arbiter.h"

void NldDirectPdoArbiterRuntimeInitialize(
    _Out_ PNLD_DIRECT_PDO_ARBITER_CONTEXT context) {
    if (context == NULL) return;
    RtlZeroMemory(context, sizeof(*context));
    KeInitializeSpinLock(&context->Lock);
    NldDirectPdoArbiterInitialize(&context->Owner);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS NldDirectPdoArbiterRuntimeStart(
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT context) {
    KIRQL old_irql;
    int started;

    if (context == NULL) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    if (context->Started) {
        KeReleaseSpinLock(&context->Lock, old_irql);
        return STATUS_INVALID_DEVICE_STATE;
    }
    started = NldDirectPdoArbiterOnPnpStart(&context->Owner);
    context->Started = started != 0;
    KeReleaseSpinLock(&context->Lock, old_irql);
    return started ? STATUS_SUCCESS : STATUS_INVALID_DEVICE_STATE;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldDirectPdoArbiterRuntimeStop(
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT context) {
    KIRQL old_irql;

    if (context == NULL) return;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    if (context->Started) {
        (void)NldDirectPdoArbiterOnPnpStop(&context->Owner);
        NldDirectPdoArbiterForceOffline(&context->Owner);
        context->Started = FALSE;
    }
    KeReleaseSpinLock(&context->Lock, old_irql);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NLD_DIRECT_PDO_ARBITER_DEMAND_RESULT
NldDirectPdoArbiterRuntimeSetRenderDemand(
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT context,
    _In_ BOOLEAN active) {
    NLD_DIRECT_PDO_ARBITER_DEMAND_RESULT result;
    KIRQL old_irql;

    if (context == NULL) return NldDirectPdoArbiterDemandRejected;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    result = context->Started
        ? NldDirectPdoArbiterSetRenderDemand(&context->Owner,
                                             active != FALSE)
        : NldDirectPdoArbiterDemandRejected;
    KeReleaseSpinLock(&context->Lock, old_irql);
    return result;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NLD_DIRECT_PDO_ARBITER_ACQUIRE_RESULT
NldDirectPdoArbiterRuntimeTryAcquire(
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT context,
    _In_ NLD_DIRECT_PDO_ARBITER_CLIENT client,
    _Out_ ULONG* generation) {
    NLD_DIRECT_PDO_ARBITER_ACQUIRE_RESULT result;
    unsigned long contract_generation = 0ul;
    KIRQL old_irql;

    if (generation != NULL) *generation = 0u;
    if (context == NULL || generation == NULL) {
        return NldDirectPdoArbiterAcquireRejected;
    }
    KeAcquireSpinLock(&context->Lock, &old_irql);
    result = context->Started
        ? NldDirectPdoArbiterTryAcquire(&context->Owner,
                                        client,
                                        &contract_generation)
        : NldDirectPdoArbiterAcquireRejected;
    *generation = (ULONG)contract_generation;
    KeReleaseSpinLock(&context->Lock, old_irql);
    return result;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN NldDirectPdoArbiterRuntimeRelease(
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT context,
    _In_ NLD_DIRECT_PDO_ARBITER_CLIENT client,
    _In_ ULONG generation) {
    KIRQL old_irql;
    int released;

    if (context == NULL) return FALSE;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    released = context->Started
        ? NldDirectPdoArbiterRelease(&context->Owner,
                                     client,
                                     (unsigned long)generation)
        : 0;
    KeReleaseSpinLock(&context->Lock, old_irql);
    return released != 0;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldDirectPdoArbiterRuntimeGetSnapshot(
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT context,
    _Out_ PNLD_DIRECT_PDO_ARBITER_SNAPSHOT snapshot) {
    KIRQL old_irql;

    if (context == NULL || snapshot == NULL) return;
    KeAcquireSpinLock(&context->Lock, &old_irql);
    snapshot->State = context->Owner.State;
    snapshot->Client = context->Owner.Client;
    snapshot->Generation = (ULONG)context->Owner.Generation;
    snapshot->ActiveGeneration =
        (ULONG)context->Owner.ActiveGeneration;
    snapshot->PnpStarted = context->Owner.PnpStarted != 0;
    snapshot->StopRequested = context->Owner.StopRequested != 0;
    snapshot->RenderDemand = context->Owner.RenderDemand != 0;
    snapshot->Started = context->Started;
    KeReleaseSpinLock(&context->Lock, old_irql);
}
