// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_direct_pdo_arbiter_contract.h"

static unsigned long NldDirectPdoArbiterNextGeneration(
    unsigned long generation) {
    ++generation;
    return generation == 0ul ? 1ul : generation;
}

void NldDirectPdoArbiterInitialize(
    NLD_DIRECT_PDO_ARBITER_OWNER* owner) {
    if (owner == 0) return;
    owner->State = NldDirectPdoArbiterOffline;
    owner->Client = NldDirectPdoArbiterClientNone;
    owner->Generation = 0ul;
    owner->ActiveGeneration = 0ul;
    owner->PnpStarted = 0;
    owner->StopRequested = 0;
    owner->RenderDemand = 0;
}

int NldDirectPdoArbiterOnPnpStart(
    NLD_DIRECT_PDO_ARBITER_OWNER* owner) {
    if (owner == 0 || owner->PnpStarted || owner->StopRequested ||
        owner->State != NldDirectPdoArbiterOffline ||
        owner->Client != NldDirectPdoArbiterClientNone ||
        owner->ActiveGeneration != 0ul) {
        return 0;
    }
    owner->Generation = NldDirectPdoArbiterNextGeneration(
        owner->Generation);
    owner->State = NldDirectPdoArbiterIdle;
    owner->PnpStarted = 1;
    owner->RenderDemand = 0;
    return 1;
}

int NldDirectPdoArbiterOnPnpStop(
    NLD_DIRECT_PDO_ARBITER_OWNER* owner) {
    if (owner == 0 || !owner->PnpStarted || owner->StopRequested) {
        return 0;
    }
    owner->PnpStarted = 0;
    owner->StopRequested = 1;
    owner->RenderDemand = 0;
    if (owner->Client == NldDirectPdoArbiterClientNone) {
        owner->State = NldDirectPdoArbiterOffline;
        owner->StopRequested = 0;
        return 1;
    }
    owner->State = NldDirectPdoArbiterStopping;
    return 1;
}

NLD_DIRECT_PDO_ARBITER_DEMAND_RESULT
NldDirectPdoArbiterSetRenderDemand(
    NLD_DIRECT_PDO_ARBITER_OWNER* owner,
    int active) {
    if (owner == 0 || !owner->PnpStarted || owner->StopRequested) {
        return NldDirectPdoArbiterDemandRejected;
    }
    owner->RenderDemand = active != 0;
    if (owner->RenderDemand &&
        owner->Client == NldDirectPdoArbiterClientDiagnostic) {
        return NldDirectPdoArbiterDemandPreemptDiagnostic;
    }
    return NldDirectPdoArbiterDemandAccepted;
}

NLD_DIRECT_PDO_ARBITER_ACQUIRE_RESULT NldDirectPdoArbiterTryAcquire(
    NLD_DIRECT_PDO_ARBITER_OWNER* owner,
    NLD_DIRECT_PDO_ARBITER_CLIENT client,
    unsigned long* generation) {
    if (generation != 0) *generation = 0ul;
    if (owner == 0 || !owner->PnpStarted || owner->StopRequested ||
        (client != NldDirectPdoArbiterClientRender &&
         client != NldDirectPdoArbiterClientDiagnostic)) {
        return NldDirectPdoArbiterAcquireRejected;
    }
    if (client == NldDirectPdoArbiterClientDiagnostic &&
        owner->RenderDemand) {
        return NldDirectPdoArbiterAcquireRejected;
    }
    if (owner->Client == client) {
        if (generation != 0) *generation = owner->ActiveGeneration;
        return NldDirectPdoArbiterAcquireAlreadyOwned;
    }
    if (owner->Client != NldDirectPdoArbiterClientNone ||
        owner->State != NldDirectPdoArbiterIdle) {
        return NldDirectPdoArbiterAcquireRejected;
    }

    owner->Generation = NldDirectPdoArbiterNextGeneration(
        owner->Generation);
    owner->ActiveGeneration = owner->Generation;
    owner->Client = client;
    owner->State = client == NldDirectPdoArbiterClientRender
        ? NldDirectPdoArbiterRenderOwned
        : NldDirectPdoArbiterDiagnosticOwned;
    if (generation != 0) *generation = owner->ActiveGeneration;
    return NldDirectPdoArbiterAcquireGranted;
}

int NldDirectPdoArbiterRelease(
    NLD_DIRECT_PDO_ARBITER_OWNER* owner,
    NLD_DIRECT_PDO_ARBITER_CLIENT client,
    unsigned long generation) {
    if (owner == 0 || client == NldDirectPdoArbiterClientNone ||
        owner->Client != client || generation == 0ul ||
        owner->ActiveGeneration != generation) {
        return 0;
    }
    owner->Client = NldDirectPdoArbiterClientNone;
    owner->ActiveGeneration = 0ul;
    if (owner->StopRequested) {
        owner->State = NldDirectPdoArbiterOffline;
        owner->StopRequested = 0;
    } else {
        owner->State = NldDirectPdoArbiterIdle;
    }
    return 1;
}

void NldDirectPdoArbiterForceOffline(
    NLD_DIRECT_PDO_ARBITER_OWNER* owner) {
    if (owner == 0) return;
    owner->State = NldDirectPdoArbiterOffline;
    owner->Client = NldDirectPdoArbiterClientNone;
    owner->ActiveGeneration = 0ul;
    owner->PnpStarted = 0;
    owner->StopRequested = 0;
    owner->RenderDemand = 0;
}

int NldDirectPdoArbiterIsConsistent(
    const NLD_DIRECT_PDO_ARBITER_OWNER* owner) {
    if (owner == 0 ||
        owner->State < NldDirectPdoArbiterOffline ||
        owner->State > NldDirectPdoArbiterStopping ||
        owner->Client < NldDirectPdoArbiterClientNone ||
        owner->Client > NldDirectPdoArbiterClientDiagnostic) {
        return 0;
    }
    if ((owner->Client == NldDirectPdoArbiterClientNone) !=
        (owner->ActiveGeneration == 0ul)) {
        return 0;
    }
    if (owner->State == NldDirectPdoArbiterRenderOwned &&
        owner->Client != NldDirectPdoArbiterClientRender) {
        return 0;
    }
    if (owner->State == NldDirectPdoArbiterDiagnosticOwned &&
        owner->Client != NldDirectPdoArbiterClientDiagnostic) {
        return 0;
    }
    if (owner->State == NldDirectPdoArbiterIdle &&
        (!owner->PnpStarted || owner->StopRequested ||
         owner->Client != NldDirectPdoArbiterClientNone)) {
        return 0;
    }
    if (owner->State == NldDirectPdoArbiterOffline &&
        (owner->PnpStarted || owner->StopRequested ||
         owner->Client != NldDirectPdoArbiterClientNone)) {
        return 0;
    }
    if (owner->State == NldDirectPdoArbiterStopping &&
        (owner->PnpStarted || !owner->StopRequested ||
         owner->Client == NldDirectPdoArbiterClientNone)) {
        return 0;
    }
    if (owner->StopRequested !=
        (owner->State == NldDirectPdoArbiterStopping)) {
        return 0;
    }
    return 1;
}
