// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_bth_indication_contract.h"

static NLD_BTH_INDICATION_RESULT NldBthIndicationMaybeFree(
    const NLD_BTH_INDICATION_OWNER* owner) {
    return owner != 0 && !owner->OwnerReferenceHeld &&
                   owner->BthReferenceCount == 0ul
        ? NldBthIndicationFreeContext
        : NldBthIndicationAccepted;
}

void NldBthIndicationInitialize(
    NLD_BTH_INDICATION_OWNER* owner) {
    if (owner == 0) return;
    owner->Generation = 0ul;
    owner->BthReferenceCount = 0ul;
    owner->OwnerReferenceHeld = 0;
    owner->Armed = 0;
    owner->TeardownRequested = 0;
    owner->RemoteDisconnected = 0;
}

unsigned long NldBthIndicationArm(
    NLD_BTH_INDICATION_OWNER* owner) {
    if (owner == 0 || owner->OwnerReferenceHeld || owner->Armed ||
        owner->BthReferenceCount != 0ul) {
        return 0ul;
    }
    ++owner->Generation;
    if (owner->Generation == 0ul) ++owner->Generation;
    owner->OwnerReferenceHeld = 1;
    owner->Armed = 1;
    owner->TeardownRequested = 0;
    owner->RemoteDisconnected = 0;
    return owner->Generation;
}

NLD_BTH_INDICATION_RESULT NldBthIndicationAddReference(
    NLD_BTH_INDICATION_OWNER* owner,
    unsigned long generation) {
    if (owner == 0 || generation != owner->Generation ||
        !owner->Armed || owner->TeardownRequested ||
        !owner->OwnerReferenceHeld) {
        return NldBthIndicationIgnored;
    }
    ++owner->BthReferenceCount;
    return NldBthIndicationAccepted;
}

NLD_BTH_INDICATION_RESULT NldBthIndicationRemoteDisconnect(
    NLD_BTH_INDICATION_OWNER* owner,
    unsigned long generation) {
    if (owner == 0 || generation != owner->Generation ||
        !owner->Armed || owner->TeardownRequested) {
        return NldBthIndicationIgnored;
    }
    owner->RemoteDisconnected = 1;
    owner->Armed = 0;
    return NldBthIndicationAccepted;
}

NLD_BTH_INDICATION_RESULT NldBthIndicationBeginTeardown(
    NLD_BTH_INDICATION_OWNER* owner,
    unsigned long generation) {
    if (owner == 0 || generation != owner->Generation ||
        !owner->OwnerReferenceHeld) {
        return NldBthIndicationIgnored;
    }
    owner->Armed = 0;
    owner->TeardownRequested = 1;
    owner->OwnerReferenceHeld = 0;
    if (owner->BthReferenceCount == 0ul) {
        return NldBthIndicationFreeContext;
    }
    return NldBthIndicationReleaseOwner;
}

NLD_BTH_INDICATION_RESULT NldBthIndicationReleaseReference(
    NLD_BTH_INDICATION_OWNER* owner,
    unsigned long generation) {
    if (owner == 0 || generation != owner->Generation ||
        owner->BthReferenceCount == 0ul) {
        return NldBthIndicationIgnored;
    }
    --owner->BthReferenceCount;
    return NldBthIndicationMaybeFree(owner);
}

int NldBthIndicationIsConsistent(
    const NLD_BTH_INDICATION_OWNER* owner) {
    if (owner == 0) return 0;
    if (owner->Armed && (!owner->OwnerReferenceHeld ||
                         owner->TeardownRequested ||
                         owner->RemoteDisconnected)) {
        return 0;
    }
    if (owner->TeardownRequested && owner->OwnerReferenceHeld) return 0;
    if (owner->Generation == 0ul &&
        (owner->OwnerReferenceHeld || owner->Armed ||
         owner->BthReferenceCount != 0ul)) {
        return 0;
    }
    return 1;
}
