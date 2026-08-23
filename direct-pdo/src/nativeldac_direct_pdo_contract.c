// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_direct_pdo_contract.h"

static NLD_DIRECT_PDO_ACTION NldDirectPdoPlan(
    NLD_DIRECT_PDO_SESSION* session) {
    NLD_DIRECT_PDO_ACTION action = NldDirectPdoActionNone;

    if (session == 0 || session->PendingAction != NldDirectPdoActionNone ||
        session->TransportState == NldDirectPdoTransportFaulted) {
        return NldDirectPdoActionNone;
    }

    if (!session->PnpStarted) {
        if (session->TransportState != NldDirectPdoTransportOffline) {
            action = NldDirectPdoActionCancelAndClose;
        }
    } else if (session->KsIntent == NldDirectPdoKsRunning) {
        if (session->TransportState == NldDirectPdoTransportClosed) {
            action = NldDirectPdoActionOpen;
        } else if (session->TransportState == NldDirectPdoTransportOpen) {
            action = NldDirectPdoActionStart;
        }
    } else if (session->KsIntent == NldDirectPdoKsAcquired) {
        if (session->TransportState == NldDirectPdoTransportClosed) {
            action = NldDirectPdoActionOpen;
        } else if (session->TransportState ==
                   NldDirectPdoTransportStreaming) {
            action = NldDirectPdoActionSuspend;
        }
    } else {
        if (session->TransportState == NldDirectPdoTransportStreaming) {
            action = NldDirectPdoActionSuspend;
        } else if (session->TransportState == NldDirectPdoTransportOpen) {
            action = NldDirectPdoActionClose;
        }
    }

    session->PendingAction = action;
    return action;
}

void NldDirectPdoInitialize(NLD_DIRECT_PDO_SESSION* session) {
    if (session == 0) return;
    session->KsIntent = NldDirectPdoKsStopped;
    session->TransportState = NldDirectPdoTransportOffline;
    session->PendingAction = NldDirectPdoActionNone;
    session->Generation = 0ul;
    session->PnpStarted = 0;
    session->RecoveryRequired = 0;
}

NLD_DIRECT_PDO_ACTION NldDirectPdoOnPnpStart(
    NLD_DIRECT_PDO_SESSION* session) {
    if (session == 0 || session->PnpStarted) {
        return NldDirectPdoActionNone;
    }
    session->PnpStarted = 1;
    session->KsIntent = NldDirectPdoKsStopped;
    session->TransportState = NldDirectPdoTransportClosed;
    session->PendingAction = NldDirectPdoActionNone;
    session->RecoveryRequired = 0;
    ++session->Generation;
    return NldDirectPdoPlan(session);
}

NLD_DIRECT_PDO_ACTION NldDirectPdoOnPnpStop(
    NLD_DIRECT_PDO_SESSION* session) {
    if (session == 0) return NldDirectPdoActionNone;
    session->PnpStarted = 0;
    session->KsIntent = NldDirectPdoKsStopped;
    session->RecoveryRequired = 0;
    if (session->TransportState == NldDirectPdoTransportOffline) {
        session->PendingAction = NldDirectPdoActionNone;
        return NldDirectPdoActionNone;
    }
    if (session->TransportState == NldDirectPdoTransportClosed &&
        session->PendingAction == NldDirectPdoActionNone) {
        session->TransportState = NldDirectPdoTransportOffline;
        return NldDirectPdoActionNone;
    }
    session->PendingAction = NldDirectPdoActionCancelAndClose;
    return NldDirectPdoActionCancelAndClose;
}

NLD_DIRECT_PDO_ACTION NldDirectPdoSetKsIntent(
    NLD_DIRECT_PDO_SESSION* session,
    NLD_DIRECT_PDO_KS_INTENT intent) {
    if (session == 0 || intent < NldDirectPdoKsStopped ||
        intent > NldDirectPdoKsRunning) {
        return NldDirectPdoActionNone;
    }
    session->KsIntent = intent;
    return NldDirectPdoPlan(session);
}

NLD_DIRECT_PDO_ACTION NldDirectPdoOnTransportLost(
    NLD_DIRECT_PDO_SESSION* session) {
    if (session == 0 || !session->PnpStarted ||
        session->TransportState == NldDirectPdoTransportOffline) {
        return NldDirectPdoActionNone;
    }
    if (session->RecoveryRequired) {
        return session->PendingAction;
    }

    session->RecoveryRequired = 1;
    ++session->Generation;
    if (session->Generation == 0ul) ++session->Generation;
    if ((session->TransportState == NldDirectPdoTransportClosed ||
         session->TransportState == NldDirectPdoTransportFaulted) &&
        session->PendingAction == NldDirectPdoActionNone) {
        session->TransportState = NldDirectPdoTransportFaulted;
        session->PendingAction = NldDirectPdoActionNone;
        return NldDirectPdoActionNone;
    }
    session->PendingAction = NldDirectPdoActionCancelAndClose;
    return NldDirectPdoActionCancelAndClose;
}

NLD_DIRECT_PDO_ACTION NldDirectPdoCompleteAction(
    NLD_DIRECT_PDO_SESSION* session,
    NLD_DIRECT_PDO_ACTION action,
    int succeeded) {
    if (session == 0 || action == NldDirectPdoActionNone ||
        action != session->PendingAction) {
        return NldDirectPdoActionNone;
    }

    session->PendingAction = NldDirectPdoActionNone;
    if (action == NldDirectPdoActionCancelAndClose &&
        session->RecoveryRequired) {
        session->TransportState = NldDirectPdoTransportFaulted;
        return NldDirectPdoActionNone;
    }
    if (!succeeded) {
        if (action == NldDirectPdoActionCancelAndClose &&
            !session->PnpStarted) {
            session->TransportState = NldDirectPdoTransportOffline;
        } else {
            session->TransportState = NldDirectPdoTransportFaulted;
        }
        return NldDirectPdoActionNone;
    }

    switch (action) {
        case NldDirectPdoActionOpen:
            session->TransportState = NldDirectPdoTransportOpen;
            break;
        case NldDirectPdoActionStart:
            session->TransportState = NldDirectPdoTransportStreaming;
            break;
        case NldDirectPdoActionSuspend:
            session->TransportState = NldDirectPdoTransportOpen;
            break;
        case NldDirectPdoActionClose:
            session->TransportState = NldDirectPdoTransportClosed;
            break;
        case NldDirectPdoActionCancelAndClose:
            session->TransportState = session->PnpStarted
                ? NldDirectPdoTransportClosed
                : NldDirectPdoTransportOffline;
            break;
        default:
            session->TransportState = NldDirectPdoTransportFaulted;
            return NldDirectPdoActionNone;
    }
    return NldDirectPdoPlan(session);
}

NLD_DIRECT_PDO_ACTION NldDirectPdoRetry(
    NLD_DIRECT_PDO_SESSION* session) {
    if (session == 0 || !session->PnpStarted ||
        session->PendingAction != NldDirectPdoActionNone ||
        session->TransportState != NldDirectPdoTransportFaulted ||
        session->KsIntent != NldDirectPdoKsStopped) {
        return NldDirectPdoActionNone;
    }
    session->TransportState = NldDirectPdoTransportClosed;
    session->RecoveryRequired = 0;
    ++session->Generation;
    return NldDirectPdoPlan(session);
}

int NldDirectPdoIsConsistent(const NLD_DIRECT_PDO_SESSION* session) {
    if (session == 0) return 0;
    if (session->KsIntent < NldDirectPdoKsStopped ||
        session->KsIntent > NldDirectPdoKsRunning) {
        return 0;
    }
    if (session->TransportState < NldDirectPdoTransportOffline ||
        session->TransportState > NldDirectPdoTransportFaulted) {
        return 0;
    }
    if (session->PendingAction < NldDirectPdoActionNone ||
        session->PendingAction > NldDirectPdoActionCancelAndClose) {
        return 0;
    }
    if (!session->PnpStarted &&
        session->TransportState != NldDirectPdoTransportOffline &&
        session->PendingAction != NldDirectPdoActionCancelAndClose) {
        return 0;
    }
    if (session->TransportState == NldDirectPdoTransportOffline &&
        session->PnpStarted) {
        return 0;
    }
    if (session->RecoveryRequired &&
        session->TransportState != NldDirectPdoTransportFaulted &&
        session->PendingAction != NldDirectPdoActionCancelAndClose) {
        return 0;
    }
    return 1;
}

int NldDirectPdoShouldPublishEndpoint(
    int dispatcher_started,
    int stop_requested,
    NLD_DIRECT_PDO_TRANSPORT_STATE transport_state,
    int fault_is_remote_disconnect) {
    return dispatcher_started && !stop_requested &&
           transport_state != NldDirectPdoTransportOffline &&
           (transport_state != NldDirectPdoTransportFaulted ||
            !fault_is_remote_disconnect);
}
