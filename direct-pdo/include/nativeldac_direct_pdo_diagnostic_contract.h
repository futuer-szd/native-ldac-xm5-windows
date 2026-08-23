// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_DIRECT_PDO_DIAGNOSTIC_CONTRACT_H
#define NATIVE_LDAC_DIRECT_PDO_DIAGNOSTIC_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NLD_DIRECT_PDO_DIAGNOSTIC_STATE {
    NldDirectPdoDiagnosticOffline = 0,
    NldDirectPdoDiagnosticIdle = 1,
    NldDirectPdoDiagnosticOpening = 2,
    NldDirectPdoDiagnosticDiscovering = 3,
    NldDirectPdoDiagnosticClosing = 4,
    NldDirectPdoDiagnosticComplete = 5,
    NldDirectPdoDiagnosticFaulted = 6,
    NldDirectPdoDiagnosticStopping = 7
} NLD_DIRECT_PDO_DIAGNOSTIC_STATE;

typedef enum NLD_DIRECT_PDO_DIAGNOSTIC_ACTION {
    NldDirectPdoDiagnosticActionNone = 0,
    NldDirectPdoDiagnosticActionOpen = 1,
    NldDirectPdoDiagnosticActionDiscover = 2,
    NldDirectPdoDiagnosticActionClose = 3,
    NldDirectPdoDiagnosticActionCancelAndClose = 4
} NLD_DIRECT_PDO_DIAGNOSTIC_ACTION;

typedef enum NLD_DIRECT_PDO_DIAGNOSTIC_COMMAND {
    NldDirectPdoDiagnosticCommandNone = 0,
    NldDirectPdoDiagnosticCommandQueueWorker = 1,
    NldDirectPdoDiagnosticCommandCancelActive = 2
} NLD_DIRECT_PDO_DIAGNOSTIC_COMMAND;

typedef struct NLD_DIRECT_PDO_DIAGNOSTIC_OWNER {
    NLD_DIRECT_PDO_DIAGNOSTIC_STATE State;
    NLD_DIRECT_PDO_DIAGNOSTIC_ACTION PendingAction;
    NLD_DIRECT_PDO_DIAGNOSTIC_ACTION ActiveAction;
    unsigned long Generation;
    unsigned long ActiveGeneration;
    int PnpStarted;
    int WorkerOwned;
    int StopRequested;
    int CancelRequested;
    int DiscoverSucceeded;
    int LastCloseSucceeded;
} NLD_DIRECT_PDO_DIAGNOSTIC_OWNER;

void NldDirectPdoDiagnosticInitialize(
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner);

NLD_DIRECT_PDO_DIAGNOSTIC_COMMAND NldDirectPdoDiagnosticOnPnpStart(
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner);

NLD_DIRECT_PDO_DIAGNOSTIC_COMMAND NldDirectPdoDiagnosticOnPnpStop(
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner);

NLD_DIRECT_PDO_DIAGNOSTIC_COMMAND NldDirectPdoDiagnosticRequestDiscover(
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner);

NLD_DIRECT_PDO_DIAGNOSTIC_COMMAND NldDirectPdoDiagnosticRequestCancel(
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner);

NLD_DIRECT_PDO_DIAGNOSTIC_ACTION NldDirectPdoDiagnosticTakeAction(
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner,
    unsigned long* generation);

int NldDirectPdoDiagnosticCompleteAction(
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner,
    unsigned long generation,
    NLD_DIRECT_PDO_DIAGNOSTIC_ACTION action,
    int succeeded);

int NldDirectPdoDiagnosticIsConsistent(
    const NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner);

#ifdef __cplusplus
}
#endif

#endif
