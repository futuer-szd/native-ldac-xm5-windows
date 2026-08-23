// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_direct_pdo_dispatch_contract.h"

static NLD_DIRECT_PDO_DISPATCH_COMMAND NldDirectPdoDispatchOwnWorker(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner,
    NLD_DIRECT_PDO_ACTION action) {
    if (owner == 0 || action == NldDirectPdoActionNone ||
        owner->WorkerOwned) {
        return NldDirectPdoDispatchNone;
    }
    owner->WorkerOwned = 1;
    return NldDirectPdoDispatchQueueWorker;
}

void NldDirectPdoDispatchInitialize(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner) {
    if (owner == 0) return;
    NldDirectPdoInitialize(&owner->Session);
    owner->ActiveAction = NldDirectPdoActionNone;
    owner->ActiveGeneration = 0ul;
    owner->WorkerOwned = 0;
    owner->StopRequested = 0;
}

NLD_DIRECT_PDO_DISPATCH_COMMAND NldDirectPdoDispatchOnPnpStart(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner) {
    NLD_DIRECT_PDO_ACTION action;

    if (owner == 0 || owner->WorkerOwned ||
        owner->ActiveAction != NldDirectPdoActionNone) {
        return NldDirectPdoDispatchNone;
    }
    owner->StopRequested = 0;
    action = NldDirectPdoOnPnpStart(&owner->Session);
    return NldDirectPdoDispatchOwnWorker(owner, action);
}

NLD_DIRECT_PDO_DISPATCH_COMMAND NldDirectPdoDispatchOnPnpStop(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner) {
    NLD_DIRECT_PDO_ACTION action;

    if (owner == 0) return NldDirectPdoDispatchNone;
    action = NldDirectPdoOnPnpStop(&owner->Session);
    owner->StopRequested = 1;
    if (owner->ActiveAction != NldDirectPdoActionNone) {
        return NldDirectPdoDispatchCancelActive;
    }
    return NldDirectPdoDispatchOwnWorker(owner, action);
}

NLD_DIRECT_PDO_DISPATCH_COMMAND NldDirectPdoDispatchSetKsIntent(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner,
    NLD_DIRECT_PDO_KS_INTENT intent) {
    NLD_DIRECT_PDO_ACTION action;

    if (owner == 0 || owner->StopRequested) {
        return NldDirectPdoDispatchNone;
    }
    action = NldDirectPdoSetKsIntent(&owner->Session, intent);
    return NldDirectPdoDispatchOwnWorker(owner, action);
}

NLD_DIRECT_PDO_DISPATCH_COMMAND NldDirectPdoDispatchOnTransportLost(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner) {
    NLD_DIRECT_PDO_ACTION action;

    if (owner == 0 || owner->StopRequested) {
        return NldDirectPdoDispatchNone;
    }
    action = NldDirectPdoOnTransportLost(&owner->Session);
    if (owner->ActiveAction != NldDirectPdoActionNone) {
        return action == NldDirectPdoActionCancelAndClose
            ? NldDirectPdoDispatchCancelActive
            : NldDirectPdoDispatchNone;
    }
    return NldDirectPdoDispatchOwnWorker(owner, action);
}

NLD_DIRECT_PDO_DISPATCH_COMMAND NldDirectPdoDispatchRetry(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner) {
    NLD_DIRECT_PDO_ACTION action;

    if (owner == 0 || owner->StopRequested || owner->WorkerOwned ||
        owner->ActiveAction != NldDirectPdoActionNone) {
        return NldDirectPdoDispatchNone;
    }
    action = NldDirectPdoRetry(&owner->Session);
    return NldDirectPdoDispatchOwnWorker(owner, action);
}

NLD_DIRECT_PDO_ACTION NldDirectPdoDispatchTakeAction(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner,
    unsigned long* generation) {
    NLD_DIRECT_PDO_ACTION action;

    if (generation != 0) *generation = 0ul;
    if (owner == 0 || !owner->WorkerOwned ||
        owner->ActiveAction != NldDirectPdoActionNone) {
        return NldDirectPdoActionNone;
    }

    action = owner->Session.PendingAction;
    if (action == NldDirectPdoActionNone) {
        owner->WorkerOwned = 0;
        return NldDirectPdoActionNone;
    }
    owner->ActiveAction = action;
    owner->ActiveGeneration = owner->Session.Generation;
    if (generation != 0) *generation = owner->ActiveGeneration;
    return action;
}

int NldDirectPdoDispatchCompleteAction(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner,
    unsigned long generation,
    NLD_DIRECT_PDO_ACTION action,
    int succeeded) {
    if (owner == 0 || action == NldDirectPdoActionNone ||
        action != owner->ActiveAction ||
        generation != owner->ActiveGeneration) {
        return 0;
    }

    owner->ActiveAction = NldDirectPdoActionNone;
    owner->ActiveGeneration = 0ul;
    if ((owner->StopRequested || owner->Session.RecoveryRequired) &&
        owner->Session.PendingAction == NldDirectPdoActionCancelAndClose &&
        action != NldDirectPdoActionCancelAndClose) {
        return 1;
    }
    (void)NldDirectPdoCompleteAction(&owner->Session,
                                     action,
                                     succeeded);
    return 1;
}

int NldDirectPdoDispatchIsConsistent(
    const NLD_DIRECT_PDO_DISPATCH_OWNER* owner) {
    if (owner == 0 || !NldDirectPdoIsConsistent(&owner->Session)) {
        return 0;
    }
    if (owner->ActiveAction < NldDirectPdoActionNone ||
        owner->ActiveAction > NldDirectPdoActionCancelAndClose) {
        return 0;
    }
    if ((owner->ActiveAction == NldDirectPdoActionNone) !=
        (owner->ActiveGeneration == 0ul)) {
        return 0;
    }
    if (owner->ActiveAction != NldDirectPdoActionNone &&
        !owner->WorkerOwned) {
        return 0;
    }
    if (!owner->WorkerOwned &&
        owner->Session.PendingAction != NldDirectPdoActionNone) {
        return 0;
    }
    if (owner->StopRequested && owner->Session.PnpStarted) {
        return 0;
    }
    return 1;
}
