// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_DIRECT_PDO_PREEMPTION_CONTRACT_H
#define NATIVE_LDAC_DIRECT_PDO_PREEMPTION_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NLD_DIRECT_PDO_PREEMPTION_STATE {
    NldDirectPdoPreemptionOffline = 0,
    NldDirectPdoPreemptionIdle = 1,
    NldDirectPdoPreemptionCancelingDiagnostic = 2,
    NldDirectPdoPreemptionRetryingRender = 3,
    NldDirectPdoPreemptionComplete = 4,
    NldDirectPdoPreemptionFaulted = 5,
    NldDirectPdoPreemptionStopping = 6
} NLD_DIRECT_PDO_PREEMPTION_STATE;

typedef enum NLD_DIRECT_PDO_PREEMPTION_ACTION {
    NldDirectPdoPreemptionActionNone = 0,
    NldDirectPdoPreemptionActionCancelDiagnostic = 1,
    NldDirectPdoPreemptionActionRetryRender = 2
} NLD_DIRECT_PDO_PREEMPTION_ACTION;

typedef struct NLD_DIRECT_PDO_PREEMPTION_OWNER {
    NLD_DIRECT_PDO_PREEMPTION_STATE State;
    NLD_DIRECT_PDO_PREEMPTION_ACTION PendingAction;
    NLD_DIRECT_PDO_PREEMPTION_ACTION ActiveAction;
    unsigned long Generation;
    unsigned long ActiveGeneration;
    int PnpStarted;
    int StopRequested;
} NLD_DIRECT_PDO_PREEMPTION_OWNER;

void NldDirectPdoPreemptionInitialize(
    NLD_DIRECT_PDO_PREEMPTION_OWNER* owner);

int NldDirectPdoPreemptionOnPnpStart(
    NLD_DIRECT_PDO_PREEMPTION_OWNER* owner);

int NldDirectPdoPreemptionOnPnpStop(
    NLD_DIRECT_PDO_PREEMPTION_OWNER* owner);

int NldDirectPdoPreemptionRequestRender(
    NLD_DIRECT_PDO_PREEMPTION_OWNER* owner);

NLD_DIRECT_PDO_PREEMPTION_ACTION NldDirectPdoPreemptionTakeAction(
    NLD_DIRECT_PDO_PREEMPTION_OWNER* owner,
    unsigned long* generation);

int NldDirectPdoPreemptionCompleteAction(
    NLD_DIRECT_PDO_PREEMPTION_OWNER* owner,
    unsigned long generation,
    NLD_DIRECT_PDO_PREEMPTION_ACTION action,
    int succeeded);

int NldDirectPdoPreemptionIsConsistent(
    const NLD_DIRECT_PDO_PREEMPTION_OWNER* owner);

#ifdef __cplusplus
}
#endif

#endif
