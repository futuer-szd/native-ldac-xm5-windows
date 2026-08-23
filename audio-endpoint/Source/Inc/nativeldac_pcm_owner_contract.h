#ifndef _NATIVE_LDAC_PCM_OWNER_CONTRACT_H_
#define _NATIVE_LDAC_PCM_OWNER_CONTRACT_H_

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVE_LDAC_PCM_OWNER_LEASE_TIMEOUT_100NS 300000000ull

#define NATIVE_LDAC_PCM_OWNER_SOURCE_LINK     0x00000001u
#define NATIVE_LDAC_PCM_OWNER_SOURCE_CONSUMER 0x00000002u

typedef enum _NATIVE_LDAC_PCM_OWNER_RESULT
{
    NativeLdacPcmOwnerAccepted = 0,
    NativeLdacPcmOwnerReleased,
    NativeLdacPcmOwnerBusy,
    NativeLdacPcmOwnerNotReady,
    NativeLdacPcmOwnerInvalid
} NATIVE_LDAC_PCM_OWNER_RESULT;

typedef struct _NATIVE_LDAC_PCM_OWNER
{
    unsigned long long OwnerId;
    unsigned long long SessionId;
    unsigned long long LastUpdateTime100ns;
    unsigned int Sources;
    int ReadActive;
} NATIVE_LDAC_PCM_OWNER;

void NativeLdacPcmOwnerInitialize(
    NATIVE_LDAC_PCM_OWNER* owner);

NATIVE_LDAC_PCM_OWNER_RESULT NativeLdacPcmOwnerSetLinkState(
    NATIVE_LDAC_PCM_OWNER* owner,
    unsigned long long client_id,
    unsigned long long session_id,
    unsigned int requested_state,
    unsigned int observed_state,
    unsigned long long now_100ns);

NATIVE_LDAC_PCM_OWNER_RESULT NativeLdacPcmOwnerAcquireConsumer(
    NATIVE_LDAC_PCM_OWNER* owner,
    unsigned long long client_id,
    unsigned long long generation,
    unsigned long long now_100ns);

NATIVE_LDAC_PCM_OWNER_RESULT NativeLdacPcmOwnerReleaseConsumer(
    NATIVE_LDAC_PCM_OWNER* owner,
    unsigned long long client_id,
    unsigned long long generation);

NATIVE_LDAC_PCM_OWNER_RESULT NativeLdacPcmOwnerBeginRead(
    NATIVE_LDAC_PCM_OWNER* owner,
    unsigned long long client_id,
    unsigned int observed_state,
    unsigned long long now_100ns);

NATIVE_LDAC_PCM_OWNER_RESULT NativeLdacPcmOwnerEndRead(
    NATIVE_LDAC_PCM_OWNER* owner,
    unsigned long long client_id);

#ifdef __cplusplus
}
#endif

#endif // _NATIVE_LDAC_PCM_OWNER_CONTRACT_H_
