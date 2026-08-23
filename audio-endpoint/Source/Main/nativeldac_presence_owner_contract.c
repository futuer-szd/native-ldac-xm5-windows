#include "nativeldac_presence_owner_contract.h"

#define NATIVE_LDAC_PRESENCE_ABSENT 0u
#define NATIVE_LDAC_PRESENCE_PRESENT 1u

static void NativeLdacPresenceOwnerClear(
    NATIVE_LDAC_PRESENCE_OWNER* owner)
{
    owner->OwnerId = 0u;
    owner->PresenceGeneration = 0u;
    owner->LastUpdateTime100ns = 0u;
}

static int NativeLdacPresenceOwnerIsExpired(
    const NATIVE_LDAC_PRESENCE_OWNER* owner,
    unsigned long long now_100ns)
{
    unsigned long long age;

    if (owner->OwnerId == 0u) return 0;
    age = now_100ns >= owner->LastUpdateTime100ns
        ? now_100ns - owner->LastUpdateTime100ns
        : 0u;
    return age >= NATIVE_LDAC_PRESENCE_OWNER_LEASE_TIMEOUT_100NS;
}

void NativeLdacPresenceOwnerInitialize(
    NATIVE_LDAC_PRESENCE_OWNER* owner)
{
    if (owner == 0) return;
    NativeLdacPresenceOwnerClear(owner);
}

NATIVE_LDAC_PRESENCE_OWNER_RESULT NativeLdacPresenceOwnerSetState(
    NATIVE_LDAC_PRESENCE_OWNER* owner,
    unsigned long long client_id,
    unsigned long long presence_generation,
    unsigned int requested_state,
    unsigned int observed_state,
    unsigned long long now_100ns)
{
    if (owner == 0 || client_id == 0u || presence_generation == 0u ||
        requested_state > NATIVE_LDAC_PRESENCE_PRESENT ||
        observed_state > NATIVE_LDAC_PRESENCE_PRESENT ||
        now_100ns == 0u) {
        return NativeLdacPresenceOwnerInvalid;
    }
    if (NativeLdacPresenceOwnerIsExpired(
            owner,
            now_100ns)) {
        NativeLdacPresenceOwnerClear(owner);
    }

    if (requested_state == NATIVE_LDAC_PRESENCE_PRESENT) {
        if (owner->OwnerId == 0u) {
            owner->OwnerId = client_id;
            owner->PresenceGeneration = presence_generation;
        } else if (owner->OwnerId != client_id ||
                   owner->PresenceGeneration != presence_generation) {
            return NativeLdacPresenceOwnerBusy;
        }
        owner->LastUpdateTime100ns = now_100ns;
        return NativeLdacPresenceOwnerAccepted;
    }

    if (owner->OwnerId == 0u) {
        return NativeLdacPresenceOwnerReleased;
    }
    if (owner->OwnerId != client_id ||
        owner->PresenceGeneration != presence_generation) {
        return NativeLdacPresenceOwnerBusy;
    }
    NativeLdacPresenceOwnerClear(owner);
    return NativeLdacPresenceOwnerReleased;
}
