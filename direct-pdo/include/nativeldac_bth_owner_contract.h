// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_BTH_OWNER_CONTRACT_H
#define NATIVE_LDAC_BTH_OWNER_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NLD_BTH_OWNER_STATE {
    NldBthOwnerOffline = 0,
    NldBthOwnerQuerying = 1,
    NldBthOwnerReady = 2,
    NldBthOwnerFaulted = 3
} NLD_BTH_OWNER_STATE;

typedef enum NLD_BTH_OWNER_ACTION {
    NldBthOwnerActionNone = 0,
    NldBthOwnerActionQuery = 1,
    NldBthOwnerActionDereference = 2
} NLD_BTH_OWNER_ACTION;

typedef struct NLD_BTH_INTERFACE_OWNER {
    NLD_BTH_OWNER_STATE State;
    unsigned long Generation;
    int PnpStarted;
    int QueryPending;
    int ReferenceHeld;
} NLD_BTH_INTERFACE_OWNER;

void NldBthOwnerInitialize(NLD_BTH_INTERFACE_OWNER* owner);

NLD_BTH_OWNER_ACTION NldBthOwnerOnPnpStart(
    NLD_BTH_INTERFACE_OWNER* owner);

NLD_BTH_OWNER_ACTION NldBthOwnerOnQueryComplete(
    NLD_BTH_INTERFACE_OWNER* owner,
    unsigned long generation,
    int succeeded,
    int interface_referenced);

NLD_BTH_OWNER_ACTION NldBthOwnerOnPnpStop(
    NLD_BTH_INTERFACE_OWNER* owner);

NLD_BTH_OWNER_ACTION NldBthOwnerRetry(
    NLD_BTH_INTERFACE_OWNER* owner);

int NldBthOwnerIsConsistent(const NLD_BTH_INTERFACE_OWNER* owner);

#ifdef __cplusplus
}
#endif

#endif
