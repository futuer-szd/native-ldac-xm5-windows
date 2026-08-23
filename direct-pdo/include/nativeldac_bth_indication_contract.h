// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_BTH_INDICATION_CONTRACT_H
#define NATIVE_LDAC_BTH_INDICATION_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NLD_BTH_INDICATION_RESULT {
    NldBthIndicationIgnored = 0,
    NldBthIndicationAccepted = 1,
    NldBthIndicationReleaseOwner = 2,
    NldBthIndicationFreeContext = 3
} NLD_BTH_INDICATION_RESULT;

typedef struct NLD_BTH_INDICATION_OWNER {
    unsigned long Generation;
    unsigned long BthReferenceCount;
    int OwnerReferenceHeld;
    int Armed;
    int TeardownRequested;
    int RemoteDisconnected;
} NLD_BTH_INDICATION_OWNER;

void NldBthIndicationInitialize(
    NLD_BTH_INDICATION_OWNER* owner);

unsigned long NldBthIndicationArm(
    NLD_BTH_INDICATION_OWNER* owner);

NLD_BTH_INDICATION_RESULT NldBthIndicationAddReference(
    NLD_BTH_INDICATION_OWNER* owner,
    unsigned long generation);

NLD_BTH_INDICATION_RESULT NldBthIndicationRemoteDisconnect(
    NLD_BTH_INDICATION_OWNER* owner,
    unsigned long generation);

NLD_BTH_INDICATION_RESULT NldBthIndicationBeginTeardown(
    NLD_BTH_INDICATION_OWNER* owner,
    unsigned long generation);

NLD_BTH_INDICATION_RESULT NldBthIndicationReleaseReference(
    NLD_BTH_INDICATION_OWNER* owner,
    unsigned long generation);

int NldBthIndicationIsConsistent(
    const NLD_BTH_INDICATION_OWNER* owner);

#ifdef __cplusplus
}
#endif

#endif
