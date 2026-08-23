// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_bth_address_contract.h"

void NldBthAddressInitialize(NLD_BTH_ADDRESS_DISCOVERY* discovery) {
    if (discovery == 0) return;
    discovery->State = NldBthAddressOffline;
    discovery->Generation = 0ul;
    discovery->PnpStarted = 0;
}

NLD_BTH_ADDRESS_ACTION NldBthAddressOnPnpStart(
    NLD_BTH_ADDRESS_DISCOVERY* discovery) {
    if (discovery == 0 || discovery->PnpStarted) {
        return NldBthAddressActionNone;
    }

    discovery->PnpStarted = 1;
    discovery->State = NldBthAddressRemotePending;
    ++discovery->Generation;
    return NldBthAddressActionQueryRemote;
}

NLD_BTH_ADDRESS_ACTION NldBthAddressOnRemoteComplete(
    NLD_BTH_ADDRESS_DISCOVERY* discovery,
    unsigned long generation,
    int succeeded) {
    if (discovery == 0 || generation != discovery->Generation ||
        !discovery->PnpStarted ||
        discovery->State != NldBthAddressRemotePending) {
        return NldBthAddressActionNone;
    }

    if (!succeeded) {
        discovery->State = NldBthAddressFaulted;
        return NldBthAddressActionNone;
    }

    discovery->State = NldBthAddressLocalPending;
    return NldBthAddressActionQueryLocal;
}

NLD_BTH_ADDRESS_ACTION NldBthAddressOnLocalComplete(
    NLD_BTH_ADDRESS_DISCOVERY* discovery,
    unsigned long generation,
    int succeeded) {
    if (discovery == 0 || generation != discovery->Generation ||
        !discovery->PnpStarted ||
        discovery->State != NldBthAddressLocalPending) {
        return NldBthAddressActionNone;
    }

    discovery->State = succeeded
        ? NldBthAddressReady
        : NldBthAddressFaulted;
    return NldBthAddressActionNone;
}

void NldBthAddressOnPnpStop(NLD_BTH_ADDRESS_DISCOVERY* discovery) {
    if (discovery == 0) return;
    if (discovery->PnpStarted || discovery->State != NldBthAddressOffline) {
        ++discovery->Generation;
    }
    discovery->PnpStarted = 0;
    discovery->State = NldBthAddressOffline;
}

int NldBthAddressIsConsistent(
    const NLD_BTH_ADDRESS_DISCOVERY* discovery) {
    if (discovery == 0) return 0;
    if (discovery->State < NldBthAddressOffline ||
        discovery->State > NldBthAddressFaulted) {
        return 0;
    }
    if (!discovery->PnpStarted &&
        discovery->State != NldBthAddressOffline) {
        return 0;
    }
    if (discovery->PnpStarted &&
        discovery->State == NldBthAddressOffline) {
        return 0;
    }
    return 1;
}
