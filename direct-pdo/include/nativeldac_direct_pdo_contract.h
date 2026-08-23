// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_DIRECT_PDO_CONTRACT_H
#define NATIVE_LDAC_DIRECT_PDO_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NLD_DIRECT_PDO_KS_INTENT {
    NldDirectPdoKsStopped = 0,
    NldDirectPdoKsAcquired = 1,
    NldDirectPdoKsRunning = 2
} NLD_DIRECT_PDO_KS_INTENT;

typedef enum NLD_DIRECT_PDO_TRANSPORT_STATE {
    NldDirectPdoTransportOffline = 0,
    NldDirectPdoTransportClosed = 1,
    NldDirectPdoTransportOpen = 2,
    NldDirectPdoTransportStreaming = 3,
    NldDirectPdoTransportFaulted = 4
} NLD_DIRECT_PDO_TRANSPORT_STATE;

typedef enum NLD_DIRECT_PDO_ACTION {
    NldDirectPdoActionNone = 0,
    NldDirectPdoActionOpen = 1,
    NldDirectPdoActionStart = 2,
    NldDirectPdoActionSuspend = 3,
    NldDirectPdoActionClose = 4,
    NldDirectPdoActionCancelAndClose = 5
} NLD_DIRECT_PDO_ACTION;

typedef struct NLD_DIRECT_PDO_SESSION {
    NLD_DIRECT_PDO_KS_INTENT KsIntent;
    NLD_DIRECT_PDO_TRANSPORT_STATE TransportState;
    NLD_DIRECT_PDO_ACTION PendingAction;
    unsigned long Generation;
    int PnpStarted;
    int RecoveryRequired;
} NLD_DIRECT_PDO_SESSION;

void NldDirectPdoInitialize(NLD_DIRECT_PDO_SESSION* session);

NLD_DIRECT_PDO_ACTION NldDirectPdoOnPnpStart(
    NLD_DIRECT_PDO_SESSION* session);

NLD_DIRECT_PDO_ACTION NldDirectPdoOnPnpStop(
    NLD_DIRECT_PDO_SESSION* session);

NLD_DIRECT_PDO_ACTION NldDirectPdoSetKsIntent(
    NLD_DIRECT_PDO_SESSION* session,
    NLD_DIRECT_PDO_KS_INTENT intent);

NLD_DIRECT_PDO_ACTION NldDirectPdoOnTransportLost(
    NLD_DIRECT_PDO_SESSION* session);

NLD_DIRECT_PDO_ACTION NldDirectPdoCompleteAction(
    NLD_DIRECT_PDO_SESSION* session,
    NLD_DIRECT_PDO_ACTION action,
    int succeeded);

NLD_DIRECT_PDO_ACTION NldDirectPdoRetry(
    NLD_DIRECT_PDO_SESSION* session);

int NldDirectPdoIsConsistent(const NLD_DIRECT_PDO_SESSION* session);

int NldDirectPdoShouldPublishEndpoint(
    int dispatcher_started,
    int stop_requested,
    NLD_DIRECT_PDO_TRANSPORT_STATE transport_state,
    int fault_is_remote_disconnect);

#ifdef __cplusplus
}
#endif

#endif
