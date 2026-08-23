// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_direct_pdo_preemption_contract.h"

static unsigned long NldDirectPdoPreemptionNextGeneration(
    unsigned long generation) {
    ++generation;
    return generation == 0ul ? 1ul : generation;
}

void NldDirectPdoPreemptionInitialize(
    NLD_DIRECT_PDO_PREEMPTION_OWNER* owner) {
    if (owner == 0) return;
    owner->State = NldDirectPdoPreemptionOffline;
    owner->PendingAction = NldDirectPdoPreemptionActionNone;
    owner->ActiveAction = NldDirectPdoPreemptionActionNone;
    owner->Generation = 0ul;
    owner->ActiveGeneration = 0ul;
    owner->PnpStarted = 0;
    owner->StopRequested = 0;
}

int NldDirectPdoPreemptionOnPnpStart(
    NLD_DIRECT_PDO_PREEMPTION_OWNER* owner) {
    if (owner == 0 || owner->PnpStarted || owner->StopRequested ||
        owner->State != NldDirectPdoPreemptionOffline ||
        owner->PendingAction != NldDirectPdoPreemptionActionNone ||
        owner->ActiveAction != NldDirectPdoPreemptionActionNone) {
        return 0;
    }
    owner->Generation = NldDirectPdoPreemptionNextGeneration(
        owner->Generation);
    owner->State = NldDirectPdoPreemptionIdle;
    owner->PnpStarted = 1;
    return 1;
}

int NldDirectPdoPreemptionOnPnpStop(
    NLD_DIRECT_PDO_PREEMPTION_OWNER* owner) {
    if (owner == 0 || !owner->PnpStarted || owner->StopRequested) {
        return 0;
    }
    owner->PnpStarted = 0;
    owner->PendingAction = NldDirectPdoPreemptionActionNone;
    if (owner->ActiveAction == NldDirectPdoPreemptionActionNone) {
        owner->State = NldDirectPdoPreemptionOffline;
        return 1;
    }
    owner->State = NldDirectPdoPreemptionStopping;
    owner->StopRequested = 1;
    return 1;
}

int NldDirectPdoPreemptionRequestRender(
    NLD_DIRECT_PDO_PREEMPTION_OWNER* owner) {
    if (owner == 0 || !owner->PnpStarted || owner->StopRequested ||
        owner->PendingAction != NldDirectPdoPreemptionActionNone ||
        owner->ActiveAction != NldDirectPdoPreemptionActionNone ||
        (owner->State != NldDirectPdoPreemptionIdle &&
         owner->State != NldDirectPdoPreemptionComplete &&
         owner->State != NldDirectPdoPreemptionFaulted)) {
        return 0;
    }
    owner->Generation = NldDirectPdoPreemptionNextGeneration(
        owner->Generation);
    owner->State = NldDirectPdoPreemptionCancelingDiagnostic;
    owner->PendingAction =
        NldDirectPdoPreemptionActionCancelDiagnostic;
    return 1;
}

NLD_DIRECT_PDO_PREEMPTION_ACTION NldDirectPdoPreemptionTakeAction(
    NLD_DIRECT_PDO_PREEMPTION_OWNER* owner,
    unsigned long* generation) {
    NLD_DIRECT_PDO_PREEMPTION_ACTION action;

    if (generation != 0) *generation = 0ul;
    if (owner == 0 || owner->StopRequested ||
        owner->ActiveAction != NldDirectPdoPreemptionActionNone) {
        return NldDirectPdoPreemptionActionNone;
    }
    action = owner->PendingAction;
    if (action == NldDirectPdoPreemptionActionNone) {
        return NldDirectPdoPreemptionActionNone;
    }
    owner->PendingAction = NldDirectPdoPreemptionActionNone;
    owner->ActiveAction = action;
    owner->ActiveGeneration = owner->Generation;
    if (generation != 0) *generation = owner->ActiveGeneration;
    return action;
}

int NldDirectPdoPreemptionCompleteAction(
    NLD_DIRECT_PDO_PREEMPTION_OWNER* owner,
    unsigned long generation,
    NLD_DIRECT_PDO_PREEMPTION_ACTION action,
    int succeeded) {
    if (owner == 0 ||
        action == NldDirectPdoPreemptionActionNone ||
        action != owner->ActiveAction || generation == 0ul ||
        generation != owner->ActiveGeneration) {
        return 0;
    }
    owner->ActiveAction = NldDirectPdoPreemptionActionNone;
    owner->ActiveGeneration = 0ul;
    if (owner->StopRequested) {
        owner->State = NldDirectPdoPreemptionOffline;
        owner->PendingAction = NldDirectPdoPreemptionActionNone;
        owner->StopRequested = 0;
        return 1;
    }

    switch (action) {
        case NldDirectPdoPreemptionActionCancelDiagnostic:
            if (succeeded) {
                owner->State = NldDirectPdoPreemptionRetryingRender;
                owner->PendingAction =
                    NldDirectPdoPreemptionActionRetryRender;
            } else {
                owner->State = NldDirectPdoPreemptionFaulted;
            }
            break;
        case NldDirectPdoPreemptionActionRetryRender:
            owner->State = succeeded
                ? NldDirectPdoPreemptionComplete
                : NldDirectPdoPreemptionFaulted;
            break;
        default:
            return 0;
    }
    return 1;
}

int NldDirectPdoPreemptionIsConsistent(
    const NLD_DIRECT_PDO_PREEMPTION_OWNER* owner) {
    if (owner == 0 ||
        owner->State < NldDirectPdoPreemptionOffline ||
        owner->State > NldDirectPdoPreemptionStopping ||
        owner->PendingAction < NldDirectPdoPreemptionActionNone ||
        owner->PendingAction > NldDirectPdoPreemptionActionRetryRender ||
        owner->ActiveAction < NldDirectPdoPreemptionActionNone ||
        owner->ActiveAction > NldDirectPdoPreemptionActionRetryRender) {
        return 0;
    }
    if ((owner->ActiveAction == NldDirectPdoPreemptionActionNone) !=
        (owner->ActiveGeneration == 0ul)) {
        return 0;
    }
    if (owner->PendingAction != NldDirectPdoPreemptionActionNone &&
        owner->ActiveAction != NldDirectPdoPreemptionActionNone) {
        return 0;
    }
    if (owner->State == NldDirectPdoPreemptionOffline &&
        (owner->PnpStarted || owner->StopRequested ||
         owner->PendingAction != NldDirectPdoPreemptionActionNone ||
         owner->ActiveAction != NldDirectPdoPreemptionActionNone)) {
        return 0;
    }
    if (owner->State == NldDirectPdoPreemptionStopping &&
        (owner->PnpStarted || !owner->StopRequested ||
         owner->ActiveAction == NldDirectPdoPreemptionActionNone)) {
        return 0;
    }
    if (owner->StopRequested !=
        (owner->State == NldDirectPdoPreemptionStopping)) {
        return 0;
    }
    if ((owner->State == NldDirectPdoPreemptionIdle ||
         owner->State == NldDirectPdoPreemptionComplete ||
         owner->State == NldDirectPdoPreemptionFaulted) &&
        (owner->PendingAction != NldDirectPdoPreemptionActionNone ||
         owner->ActiveAction != NldDirectPdoPreemptionActionNone)) {
        return 0;
    }
    if (owner->State ==
            NldDirectPdoPreemptionCancelingDiagnostic &&
        owner->PendingAction !=
            NldDirectPdoPreemptionActionCancelDiagnostic &&
        owner->ActiveAction !=
            NldDirectPdoPreemptionActionCancelDiagnostic) {
        return 0;
    }
    if (owner->State == NldDirectPdoPreemptionRetryingRender &&
        owner->PendingAction !=
            NldDirectPdoPreemptionActionRetryRender &&
        owner->ActiveAction !=
            NldDirectPdoPreemptionActionRetryRender) {
        return 0;
    }
    return 1;
}
