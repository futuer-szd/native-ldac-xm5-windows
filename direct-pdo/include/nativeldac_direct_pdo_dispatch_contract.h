// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_DIRECT_PDO_DISPATCH_CONTRACT_H
#define NATIVE_LDAC_DIRECT_PDO_DISPATCH_CONTRACT_H

#include "nativeldac_direct_pdo_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NLD_DIRECT_PDO_DISPATCH_COMMAND {
    NldDirectPdoDispatchNone = 0,
    NldDirectPdoDispatchQueueWorker = 1,
    NldDirectPdoDispatchCancelActive = 2
} NLD_DIRECT_PDO_DISPATCH_COMMAND;

typedef struct NLD_DIRECT_PDO_DISPATCH_OWNER {
    NLD_DIRECT_PDO_SESSION Session;
    NLD_DIRECT_PDO_ACTION ActiveAction;
    unsigned long ActiveGeneration;
    int WorkerOwned;
    int StopRequested;
} NLD_DIRECT_PDO_DISPATCH_OWNER;

void NldDirectPdoDispatchInitialize(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner);

NLD_DIRECT_PDO_DISPATCH_COMMAND NldDirectPdoDispatchOnPnpStart(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner);

NLD_DIRECT_PDO_DISPATCH_COMMAND NldDirectPdoDispatchOnPnpStop(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner);

NLD_DIRECT_PDO_DISPATCH_COMMAND NldDirectPdoDispatchSetKsIntent(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner,
    NLD_DIRECT_PDO_KS_INTENT intent);

NLD_DIRECT_PDO_DISPATCH_COMMAND NldDirectPdoDispatchOnTransportLost(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner);

NLD_DIRECT_PDO_DISPATCH_COMMAND NldDirectPdoDispatchRetry(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner);

NLD_DIRECT_PDO_ACTION NldDirectPdoDispatchTakeAction(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner,
    unsigned long* generation);

int NldDirectPdoDispatchCompleteAction(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner,
    unsigned long generation,
    NLD_DIRECT_PDO_ACTION action,
    int succeeded);

int NldDirectPdoDispatchIsConsistent(
    const NLD_DIRECT_PDO_DISPATCH_OWNER* owner);

#ifdef __cplusplus
}
#endif

#endif
