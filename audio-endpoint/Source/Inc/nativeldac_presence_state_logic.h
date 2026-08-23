#ifndef _NATIVE_LDAC_PRESENCE_STATE_LOGIC_H_
#define _NATIVE_LDAC_PRESENCE_STATE_LOGIC_H_

#include "nativeldac_pcm_abi.h"

#define NATIVE_LDAC_PRESENCE_LEASE_TIMEOUT_100NS 150000000ull

static __inline int NativeLdacPresenceStateIsFreshPresent(
    const NATIVE_LDAC_PRESENCE_STATE* State,
    ULONGLONG NowInterruptTime100ns)
{
    ULONGLONG age;

    if (State == NULL ||
        State->State != NativeLdacPresencePresent ||
        State->PresenceGeneration == 0 ||
        State->UpdatedInterruptTime100ns == 0)
    {
        return 0;
    }
    age = NowInterruptTime100ns >= State->UpdatedInterruptTime100ns
        ? NowInterruptTime100ns - State->UpdatedInterruptTime100ns
        : 0;
    return age < NATIVE_LDAC_PRESENCE_LEASE_TIMEOUT_100NS;
}

static __inline ULONGLONG NativeLdacPresenceStateRemainingTime100ns(
    const NATIVE_LDAC_PRESENCE_STATE* State,
    ULONGLONG NowInterruptTime100ns)
{
    ULONGLONG age;

    if (State == NULL ||
        State->State != NativeLdacPresencePresent ||
        State->PresenceGeneration == 0 ||
        State->UpdatedInterruptTime100ns == 0)
    {
        return 0;
    }
    age = NowInterruptTime100ns >= State->UpdatedInterruptTime100ns
        ? NowInterruptTime100ns - State->UpdatedInterruptTime100ns
        : 0;
    return age < NATIVE_LDAC_PRESENCE_LEASE_TIMEOUT_100NS
        ? NATIVE_LDAC_PRESENCE_LEASE_TIMEOUT_100NS - age
        : 0;
}

static __inline int NativeLdacExpirePresenceState(
    NATIVE_LDAC_PRESENCE_STATE* State,
    ULONGLONG NowInterruptTime100ns)
{
    if (State == NULL ||
        State->State != NativeLdacPresencePresent ||
        NativeLdacPresenceStateRemainingTime100ns(
            State,
            NowInterruptTime100ns) != 0)
    {
        return 0;
    }
    State->State = NativeLdacPresenceAbsent;
    State->UpdateSequence++;
    State->UpdatedInterruptTime100ns = NowInterruptTime100ns;
    return 1;
}

#endif // _NATIVE_LDAC_PRESENCE_STATE_LOGIC_H_
