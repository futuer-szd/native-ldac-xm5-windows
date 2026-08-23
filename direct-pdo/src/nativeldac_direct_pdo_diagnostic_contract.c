// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_direct_pdo_diagnostic_contract.h"

static unsigned long NldDirectPdoDiagnosticNextGeneration(
    unsigned long generation) {
    ++generation;
    return generation == 0ul ? 1ul : generation;
}

static NLD_DIRECT_PDO_DIAGNOSTIC_COMMAND
NldDirectPdoDiagnosticOwnWorker(
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner) {
    if (owner == 0 || owner->PendingAction ==
                          NldDirectPdoDiagnosticActionNone ||
        owner->WorkerOwned) {
        return NldDirectPdoDiagnosticCommandNone;
    }
    owner->WorkerOwned = 1;
    return NldDirectPdoDiagnosticCommandQueueWorker;
}

void NldDirectPdoDiagnosticInitialize(
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner) {
    if (owner == 0) return;
    owner->State = NldDirectPdoDiagnosticOffline;
    owner->PendingAction = NldDirectPdoDiagnosticActionNone;
    owner->ActiveAction = NldDirectPdoDiagnosticActionNone;
    owner->Generation = 0ul;
    owner->ActiveGeneration = 0ul;
    owner->PnpStarted = 0;
    owner->WorkerOwned = 0;
    owner->StopRequested = 0;
    owner->CancelRequested = 0;
    owner->DiscoverSucceeded = 0;
    owner->LastCloseSucceeded = 0;
}

NLD_DIRECT_PDO_DIAGNOSTIC_COMMAND NldDirectPdoDiagnosticOnPnpStart(
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner) {
    if (owner == 0 || owner->PnpStarted ||
        owner->State != NldDirectPdoDiagnosticOffline ||
        owner->WorkerOwned ||
        owner->ActiveAction != NldDirectPdoDiagnosticActionNone) {
        return NldDirectPdoDiagnosticCommandNone;
    }
    owner->State = NldDirectPdoDiagnosticIdle;
    owner->Generation = NldDirectPdoDiagnosticNextGeneration(
        owner->Generation);
    owner->PnpStarted = 1;
    owner->StopRequested = 0;
    owner->CancelRequested = 0;
    owner->DiscoverSucceeded = 0;
    owner->LastCloseSucceeded = 0;
    return NldDirectPdoDiagnosticCommandNone;
}

NLD_DIRECT_PDO_DIAGNOSTIC_COMMAND NldDirectPdoDiagnosticOnPnpStop(
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner) {
    if (owner == 0 || owner->State == NldDirectPdoDiagnosticOffline) {
        return NldDirectPdoDiagnosticCommandNone;
    }
    if (owner->StopRequested) {
        return NldDirectPdoDiagnosticCommandNone;
    }
    owner->PnpStarted = 0;
    owner->StopRequested = 1;
    owner->CancelRequested = 0;
    owner->State = NldDirectPdoDiagnosticStopping;
    owner->PendingAction = NldDirectPdoDiagnosticActionCancelAndClose;
    if (owner->ActiveAction != NldDirectPdoDiagnosticActionNone) {
        return NldDirectPdoDiagnosticCommandCancelActive;
    }
    return NldDirectPdoDiagnosticOwnWorker(owner);
}

NLD_DIRECT_PDO_DIAGNOSTIC_COMMAND NldDirectPdoDiagnosticRequestDiscover(
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner) {
    if (owner == 0 || !owner->PnpStarted || owner->StopRequested ||
        owner->CancelRequested ||
        owner->WorkerOwned ||
        owner->ActiveAction != NldDirectPdoDiagnosticActionNone ||
        (owner->State != NldDirectPdoDiagnosticIdle &&
         owner->State != NldDirectPdoDiagnosticComplete &&
         owner->State != NldDirectPdoDiagnosticFaulted)) {
        return NldDirectPdoDiagnosticCommandNone;
    }
    owner->Generation = NldDirectPdoDiagnosticNextGeneration(
        owner->Generation);
    owner->State = NldDirectPdoDiagnosticOpening;
    owner->PendingAction = NldDirectPdoDiagnosticActionOpen;
    owner->DiscoverSucceeded = 0;
    owner->LastCloseSucceeded = 0;
    return NldDirectPdoDiagnosticOwnWorker(owner);
}

NLD_DIRECT_PDO_DIAGNOSTIC_COMMAND NldDirectPdoDiagnosticRequestCancel(
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner) {
    if (owner == 0 || !owner->PnpStarted || owner->StopRequested ||
        owner->CancelRequested || !owner->WorkerOwned) {
        return NldDirectPdoDiagnosticCommandNone;
    }
    owner->CancelRequested = 1;
    owner->State = NldDirectPdoDiagnosticClosing;
    owner->PendingAction =
        NldDirectPdoDiagnosticActionCancelAndClose;
    return owner->ActiveAction != NldDirectPdoDiagnosticActionNone
        ? NldDirectPdoDiagnosticCommandCancelActive
        : NldDirectPdoDiagnosticCommandNone;
}

NLD_DIRECT_PDO_DIAGNOSTIC_ACTION NldDirectPdoDiagnosticTakeAction(
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner,
    unsigned long* generation) {
    NLD_DIRECT_PDO_DIAGNOSTIC_ACTION action;

    if (generation != 0) *generation = 0ul;
    if (owner == 0 || !owner->WorkerOwned ||
        owner->ActiveAction != NldDirectPdoDiagnosticActionNone) {
        return NldDirectPdoDiagnosticActionNone;
    }
    action = owner->PendingAction;
    if (action == NldDirectPdoDiagnosticActionNone) {
        owner->WorkerOwned = 0;
        return NldDirectPdoDiagnosticActionNone;
    }
    owner->PendingAction = NldDirectPdoDiagnosticActionNone;
    owner->ActiveAction = action;
    owner->ActiveGeneration = owner->Generation;
    if (generation != 0) *generation = owner->ActiveGeneration;
    return action;
}

int NldDirectPdoDiagnosticCompleteAction(
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner,
    unsigned long generation,
    NLD_DIRECT_PDO_DIAGNOSTIC_ACTION action,
    int succeeded) {
    if (owner == 0 || action == NldDirectPdoDiagnosticActionNone ||
        action != owner->ActiveAction ||
        generation != owner->ActiveGeneration) {
        return 0;
    }
    owner->ActiveAction = NldDirectPdoDiagnosticActionNone;
    owner->ActiveGeneration = 0ul;
    if (owner->StopRequested &&
        action != NldDirectPdoDiagnosticActionCancelAndClose) {
        owner->State = NldDirectPdoDiagnosticStopping;
        owner->PendingAction =
            NldDirectPdoDiagnosticActionCancelAndClose;
        return 1;
    }
    if (owner->CancelRequested &&
        action != NldDirectPdoDiagnosticActionCancelAndClose) {
        owner->State = NldDirectPdoDiagnosticClosing;
        owner->PendingAction =
            NldDirectPdoDiagnosticActionCancelAndClose;
        return 1;
    }

    switch (action) {
        case NldDirectPdoDiagnosticActionOpen:
            if (succeeded) {
                owner->State = NldDirectPdoDiagnosticDiscovering;
                owner->PendingAction =
                    NldDirectPdoDiagnosticActionDiscover;
            } else {
                owner->State = NldDirectPdoDiagnosticFaulted;
            }
            break;
        case NldDirectPdoDiagnosticActionDiscover:
            owner->DiscoverSucceeded = succeeded != 0;
            owner->State = NldDirectPdoDiagnosticClosing;
            owner->PendingAction = NldDirectPdoDiagnosticActionClose;
            break;
        case NldDirectPdoDiagnosticActionClose:
            owner->LastCloseSucceeded = succeeded != 0;
            owner->State = succeeded && owner->DiscoverSucceeded
                ? NldDirectPdoDiagnosticComplete
                : NldDirectPdoDiagnosticFaulted;
            break;
        case NldDirectPdoDiagnosticActionCancelAndClose:
            owner->LastCloseSucceeded = succeeded != 0;
            if (owner->CancelRequested) {
                owner->State = succeeded
                    ? NldDirectPdoDiagnosticIdle
                    : NldDirectPdoDiagnosticFaulted;
                owner->CancelRequested = 0;
                owner->PendingAction =
                    NldDirectPdoDiagnosticActionNone;
                break;
            }
            owner->State = NldDirectPdoDiagnosticOffline;
            owner->PendingAction = NldDirectPdoDiagnosticActionNone;
            owner->PnpStarted = 0;
            owner->StopRequested = 0;
            break;
        default:
            return 0;
    }
    return 1;
}

int NldDirectPdoDiagnosticIsConsistent(
    const NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner) {
    if (owner == 0 ||
        owner->State < NldDirectPdoDiagnosticOffline ||
        owner->State > NldDirectPdoDiagnosticStopping ||
        owner->PendingAction < NldDirectPdoDiagnosticActionNone ||
        owner->PendingAction > NldDirectPdoDiagnosticActionCancelAndClose ||
        owner->ActiveAction < NldDirectPdoDiagnosticActionNone ||
        owner->ActiveAction > NldDirectPdoDiagnosticActionCancelAndClose) {
        return 0;
    }
    if ((owner->ActiveAction == NldDirectPdoDiagnosticActionNone) !=
        (owner->ActiveGeneration == 0ul)) {
        return 0;
    }
    if (owner->ActiveAction != NldDirectPdoDiagnosticActionNone &&
        !owner->WorkerOwned) {
        return 0;
    }
    if (owner->PendingAction != NldDirectPdoDiagnosticActionNone &&
        !owner->WorkerOwned) {
        return 0;
    }
    if (!owner->PnpStarted &&
        owner->State != NldDirectPdoDiagnosticOffline &&
        owner->State != NldDirectPdoDiagnosticStopping) {
        return 0;
    }
    if (owner->StopRequested !=
        (owner->State == NldDirectPdoDiagnosticStopping)) {
        return 0;
    }
    if (owner->CancelRequested &&
        (!owner->PnpStarted || owner->StopRequested ||
         !owner->WorkerOwned ||
         owner->State != NldDirectPdoDiagnosticClosing)) {
        return 0;
    }
    return 1;
}
