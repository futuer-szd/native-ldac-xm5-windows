#include "nativeldac_pcm_owner_contract.h"

#define NATIVE_LDAC_LINK_DISCONNECTED 0u
#define NATIVE_LDAC_LINK_CONNECTING 1u
#define NATIVE_LDAC_LINK_CONNECTED 2u
#define NATIVE_LDAC_LINK_STOPPING 3u

static void NativeLdacPcmOwnerClear(NATIVE_LDAC_PCM_OWNER* owner)
{
    owner->OwnerId = 0u;
    owner->SessionId = 0u;
    owner->LastUpdateTime100ns = 0u;
    owner->Sources = 0u;
    owner->ReadActive = 0;
}

static int NativeLdacPcmOwnerIsExpired(
    const NATIVE_LDAC_PCM_OWNER* owner,
    unsigned long long now_100ns)
{
    unsigned long long age;

    if (owner->OwnerId == 0u || owner->ReadActive) return 0;
    age = now_100ns >= owner->LastUpdateTime100ns
        ? now_100ns - owner->LastUpdateTime100ns
        : 0u;
    return age >= NATIVE_LDAC_PCM_OWNER_LEASE_TIMEOUT_100NS;
}

void NativeLdacPcmOwnerInitialize(NATIVE_LDAC_PCM_OWNER* owner)
{
    if (owner == 0) return;
    NativeLdacPcmOwnerClear(owner);
}

NATIVE_LDAC_PCM_OWNER_RESULT NativeLdacPcmOwnerSetLinkState(
    NATIVE_LDAC_PCM_OWNER* owner,
    unsigned long long client_id,
    unsigned long long session_id,
    unsigned int requested_state,
    unsigned int observed_state,
    unsigned long long now_100ns)
{
    if (owner == 0 || client_id == 0u || session_id == 0u ||
        requested_state > NATIVE_LDAC_LINK_STOPPING ||
        observed_state > NATIVE_LDAC_LINK_STOPPING || now_100ns == 0u) {
        return NativeLdacPcmOwnerInvalid;
    }
    if (NativeLdacPcmOwnerIsExpired(owner, now_100ns)) {
        NativeLdacPcmOwnerClear(owner);
    }

    if (observed_state == NATIVE_LDAC_LINK_DISCONNECTED &&
        (owner->Sources & NATIVE_LDAC_PCM_OWNER_SOURCE_LINK) != 0u &&
        !owner->ReadActive) {
        owner->Sources &= ~NATIVE_LDAC_PCM_OWNER_SOURCE_LINK;
        if (owner->Sources == 0u) {
            NativeLdacPcmOwnerClear(owner);
        }
    }

    if (requested_state == NATIVE_LDAC_LINK_CONNECTING) {
        if (owner->OwnerId == 0u) {
            owner->OwnerId = client_id;
            owner->SessionId = session_id;
        } else if (owner->OwnerId != client_id ||
                   owner->SessionId != session_id) {
            return NativeLdacPcmOwnerBusy;
        }
        owner->Sources |= NATIVE_LDAC_PCM_OWNER_SOURCE_LINK;
        owner->LastUpdateTime100ns = now_100ns;
        return NativeLdacPcmOwnerAccepted;
    }

    if (owner->OwnerId != client_id || owner->SessionId != session_id ||
        (owner->Sources & NATIVE_LDAC_PCM_OWNER_SOURCE_LINK) == 0u) {
        return NativeLdacPcmOwnerBusy;
    }
    if (requested_state == NATIVE_LDAC_LINK_DISCONNECTED) {
        if (owner->ReadActive &&
            owner->Sources == NATIVE_LDAC_PCM_OWNER_SOURCE_LINK) {
            return NativeLdacPcmOwnerBusy;
        }
        owner->Sources &= ~NATIVE_LDAC_PCM_OWNER_SOURCE_LINK;
        if (owner->Sources == 0u) {
            NativeLdacPcmOwnerClear(owner);
        }
        return NativeLdacPcmOwnerReleased;
    }
    owner->LastUpdateTime100ns = now_100ns;
    return NativeLdacPcmOwnerAccepted;
}

NATIVE_LDAC_PCM_OWNER_RESULT NativeLdacPcmOwnerAcquireConsumer(
    NATIVE_LDAC_PCM_OWNER* owner,
    unsigned long long client_id,
    unsigned long long generation,
    unsigned long long now_100ns)
{
    if (owner == 0 || client_id == 0u || generation == 0u || now_100ns == 0u) {
        return NativeLdacPcmOwnerInvalid;
    }
    if (NativeLdacPcmOwnerIsExpired(owner, now_100ns)) {
        NativeLdacPcmOwnerClear(owner);
    }
    if (owner->OwnerId == 0u) {
        owner->OwnerId = client_id;
        owner->SessionId = generation;
    } else if (owner->OwnerId != client_id ||
               owner->SessionId != generation) {
        return NativeLdacPcmOwnerBusy;
    }
    owner->Sources |= NATIVE_LDAC_PCM_OWNER_SOURCE_CONSUMER;
    owner->LastUpdateTime100ns = now_100ns;
    return NativeLdacPcmOwnerAccepted;
}

NATIVE_LDAC_PCM_OWNER_RESULT NativeLdacPcmOwnerReleaseConsumer(
    NATIVE_LDAC_PCM_OWNER* owner,
    unsigned long long client_id,
    unsigned long long generation)
{
    if (owner == 0 || client_id == 0u || generation == 0u) {
        return NativeLdacPcmOwnerInvalid;
    }
    if (owner->OwnerId == 0u) {
        return NativeLdacPcmOwnerReleased;
    }
    if (owner->OwnerId != client_id || owner->SessionId != generation ||
        (owner->Sources & NATIVE_LDAC_PCM_OWNER_SOURCE_CONSUMER) == 0u) {
        return NativeLdacPcmOwnerBusy;
    }
    if (owner->ReadActive &&
        owner->Sources == NATIVE_LDAC_PCM_OWNER_SOURCE_CONSUMER) {
        return NativeLdacPcmOwnerBusy;
    }
    owner->Sources &= ~NATIVE_LDAC_PCM_OWNER_SOURCE_CONSUMER;
    if (owner->Sources == 0u) {
        NativeLdacPcmOwnerClear(owner);
    }
    return NativeLdacPcmOwnerReleased;
}

NATIVE_LDAC_PCM_OWNER_RESULT NativeLdacPcmOwnerBeginRead(
    NATIVE_LDAC_PCM_OWNER* owner,
    unsigned long long client_id,
    unsigned int observed_state,
    unsigned long long now_100ns)
{
    if (owner == 0 || client_id == 0u ||
        observed_state > NATIVE_LDAC_LINK_STOPPING || now_100ns == 0u) {
        return NativeLdacPcmOwnerInvalid;
    }
    if (NativeLdacPcmOwnerIsExpired(owner, now_100ns)) {
        NativeLdacPcmOwnerClear(owner);
    }
    if (owner->OwnerId == 0u || owner->Sources == 0u) {
        return NativeLdacPcmOwnerNotReady;
    }
    if ((owner->Sources & NATIVE_LDAC_PCM_OWNER_SOURCE_CONSUMER) == 0u &&
        observed_state != NATIVE_LDAC_LINK_CONNECTED) {
        return NativeLdacPcmOwnerNotReady;
    }
    if (owner->OwnerId != client_id || owner->ReadActive) {
        return NativeLdacPcmOwnerBusy;
    }
    owner->ReadActive = 1;
    owner->LastUpdateTime100ns = now_100ns;
    return NativeLdacPcmOwnerAccepted;
}

NATIVE_LDAC_PCM_OWNER_RESULT NativeLdacPcmOwnerEndRead(
    NATIVE_LDAC_PCM_OWNER* owner,
    unsigned long long client_id)
{
    if (owner == 0 || client_id == 0u) {
        return NativeLdacPcmOwnerInvalid;
    }
    if (owner->OwnerId != client_id || !owner->ReadActive) {
        return NativeLdacPcmOwnerBusy;
    }
    owner->ReadActive = 0;
    return NativeLdacPcmOwnerAccepted;
}
