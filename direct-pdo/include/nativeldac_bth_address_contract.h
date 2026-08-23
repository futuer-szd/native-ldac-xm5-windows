// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_BTH_ADDRESS_CONTRACT_H
#define NATIVE_LDAC_BTH_ADDRESS_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NLD_BTH_ADDRESS_STATE {
    NldBthAddressOffline = 0,
    NldBthAddressRemotePending = 1,
    NldBthAddressLocalPending = 2,
    NldBthAddressReady = 3,
    NldBthAddressFaulted = 4
} NLD_BTH_ADDRESS_STATE;

typedef enum NLD_BTH_ADDRESS_ACTION {
    NldBthAddressActionNone = 0,
    NldBthAddressActionQueryRemote = 1,
    NldBthAddressActionQueryLocal = 2
} NLD_BTH_ADDRESS_ACTION;

typedef struct NLD_BTH_ADDRESS_DISCOVERY {
    NLD_BTH_ADDRESS_STATE State;
    unsigned long Generation;
    int PnpStarted;
} NLD_BTH_ADDRESS_DISCOVERY;

void NldBthAddressInitialize(NLD_BTH_ADDRESS_DISCOVERY* discovery);

NLD_BTH_ADDRESS_ACTION NldBthAddressOnPnpStart(
    NLD_BTH_ADDRESS_DISCOVERY* discovery);

NLD_BTH_ADDRESS_ACTION NldBthAddressOnRemoteComplete(
    NLD_BTH_ADDRESS_DISCOVERY* discovery,
    unsigned long generation,
    int succeeded);

NLD_BTH_ADDRESS_ACTION NldBthAddressOnLocalComplete(
    NLD_BTH_ADDRESS_DISCOVERY* discovery,
    unsigned long generation,
    int succeeded);

void NldBthAddressOnPnpStop(NLD_BTH_ADDRESS_DISCOVERY* discovery);

int NldBthAddressIsConsistent(
    const NLD_BTH_ADDRESS_DISCOVERY* discovery);

#ifdef __cplusplus
}
#endif

#endif
