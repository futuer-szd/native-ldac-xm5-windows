#ifndef _NATIVE_LDAC_PRESENCE_OWNER_CONTRACT_H_
#define _NATIVE_LDAC_PRESENCE_OWNER_CONTRACT_H_

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVE_LDAC_PRESENCE_OWNER_LEASE_TIMEOUT_100NS 150000000ull

typedef enum _NATIVE_LDAC_PRESENCE_OWNER_RESULT
{
    NativeLdacPresenceOwnerAccepted = 0,
    NativeLdacPresenceOwnerReleased,
    NativeLdacPresenceOwnerBusy,
    NativeLdacPresenceOwnerInvalid
} NATIVE_LDAC_PRESENCE_OWNER_RESULT;

typedef struct _NATIVE_LDAC_PRESENCE_OWNER
{
    unsigned long long OwnerId;
    unsigned long long PresenceGeneration;
    unsigned long long LastUpdateTime100ns;
} NATIVE_LDAC_PRESENCE_OWNER;

void NativeLdacPresenceOwnerInitialize(
    NATIVE_LDAC_PRESENCE_OWNER* owner);

NATIVE_LDAC_PRESENCE_OWNER_RESULT NativeLdacPresenceOwnerSetState(
    NATIVE_LDAC_PRESENCE_OWNER* owner,
    unsigned long long client_id,
    unsigned long long presence_generation,
    unsigned int requested_state,
    unsigned int observed_state,
    unsigned long long now_100ns);

#ifdef __cplusplus
}
#endif

#endif // _NATIVE_LDAC_PRESENCE_OWNER_CONTRACT_H_
