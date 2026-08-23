// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_bth_owner_contract.h"

void NldBthOwnerInitialize(NLD_BTH_INTERFACE_OWNER* owner) {
    if (owner == 0) return;
    owner->State = NldBthOwnerOffline;
    owner->Generation = 0ul;
    owner->PnpStarted = 0;
    owner->QueryPending = 0;
    owner->ReferenceHeld = 0;
}

NLD_BTH_OWNER_ACTION NldBthOwnerOnPnpStart(
    NLD_BTH_INTERFACE_OWNER* owner) {
    if (owner == 0 || owner->PnpStarted) {
        return NldBthOwnerActionNone;
    }

    owner->PnpStarted = 1;
    owner->State = NldBthOwnerQuerying;
    owner->QueryPending = 1;
    owner->ReferenceHeld = 0;
    ++owner->Generation;
    return NldBthOwnerActionQuery;
}

NLD_BTH_OWNER_ACTION NldBthOwnerOnQueryComplete(
    NLD_BTH_INTERFACE_OWNER* owner,
    unsigned long generation,
    int succeeded,
    int interface_referenced) {
    if (owner == 0) {
        return interface_referenced
            ? NldBthOwnerActionDereference
            : NldBthOwnerActionNone;
    }

    if (generation != owner->Generation || !owner->QueryPending) {
        return interface_referenced
            ? NldBthOwnerActionDereference
            : NldBthOwnerActionNone;
    }

    owner->QueryPending = 0;
    if (!owner->PnpStarted) {
        owner->State = NldBthOwnerOffline;
        owner->ReferenceHeld = 0;
        return interface_referenced
            ? NldBthOwnerActionDereference
            : NldBthOwnerActionNone;
    }

    if (succeeded && interface_referenced) {
        owner->State = NldBthOwnerReady;
        owner->ReferenceHeld = 1;
        return NldBthOwnerActionNone;
    }

    owner->State = NldBthOwnerFaulted;
    owner->ReferenceHeld = 0;
    return interface_referenced
        ? NldBthOwnerActionDereference
        : NldBthOwnerActionNone;
}

NLD_BTH_OWNER_ACTION NldBthOwnerOnPnpStop(
    NLD_BTH_INTERFACE_OWNER* owner) {
    int release_reference;

    if (owner == 0) return NldBthOwnerActionNone;
    release_reference = owner->ReferenceHeld;
    owner->PnpStarted = 0;
    owner->State = NldBthOwnerOffline;
    owner->ReferenceHeld = 0;
    return release_reference
        ? NldBthOwnerActionDereference
        : NldBthOwnerActionNone;
}

NLD_BTH_OWNER_ACTION NldBthOwnerRetry(
    NLD_BTH_INTERFACE_OWNER* owner) {
    if (owner == 0 || !owner->PnpStarted || owner->QueryPending ||
        owner->ReferenceHeld || owner->State != NldBthOwnerFaulted) {
        return NldBthOwnerActionNone;
    }

    owner->State = NldBthOwnerQuerying;
    owner->QueryPending = 1;
    ++owner->Generation;
    return NldBthOwnerActionQuery;
}

int NldBthOwnerIsConsistent(const NLD_BTH_INTERFACE_OWNER* owner) {
    if (owner == 0) return 0;
    if (owner->State < NldBthOwnerOffline ||
        owner->State > NldBthOwnerFaulted) {
        return 0;
    }
    if (owner->QueryPending && owner->State != NldBthOwnerQuerying &&
        owner->PnpStarted) {
        return 0;
    }
    if (owner->ReferenceHeld != (owner->State == NldBthOwnerReady)) {
        return 0;
    }
    if (owner->ReferenceHeld && !owner->PnpStarted) return 0;
    if (owner->State == NldBthOwnerReady && owner->QueryPending) return 0;
    if (owner->State == NldBthOwnerFaulted && !owner->PnpStarted) return 0;
    return 1;
}
