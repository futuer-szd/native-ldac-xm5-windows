#ifndef _NATIVE_LDAC_LINK_STATE_LOGIC_H_
#define _NATIVE_LDAC_LINK_STATE_LOGIC_H_

#include "nativeldac_pcm_abi.h"

#define NATIVE_LDAC_LINK_HEARTBEAT_TIMEOUT_100NS 50000000ull

static __inline int NativeLdacLinkStateIsFreshConnected(
    const NATIVE_LDAC_LINK_STATE* State,
    ULONGLONG NowInterruptTime100ns)
{
    ULONGLONG age;

    if (State == NULL ||
        State->State != NativeLdacLinkStateConnected ||
        State->UpdatedInterruptTime100ns == 0)
    {
        return 0;
    }
    age = NowInterruptTime100ns >= State->UpdatedInterruptTime100ns
        ? NowInterruptTime100ns - State->UpdatedInterruptTime100ns
        : 0;
    return age < NATIVE_LDAC_LINK_HEARTBEAT_TIMEOUT_100NS;
}

static __inline ULONGLONG NativeLdacLinkStateRemainingFreshTime100ns(
    const NATIVE_LDAC_LINK_STATE* State,
    ULONGLONG NowInterruptTime100ns)
{
    ULONGLONG age;

    if (State == NULL ||
        State->State != NativeLdacLinkStateConnected ||
        State->UpdatedInterruptTime100ns == 0)
    {
        return 0;
    }
    age = NowInterruptTime100ns >= State->UpdatedInterruptTime100ns
        ? NowInterruptTime100ns - State->UpdatedInterruptTime100ns
        : 0;
    return age < NATIVE_LDAC_LINK_HEARTBEAT_TIMEOUT_100NS
        ? NATIVE_LDAC_LINK_HEARTBEAT_TIMEOUT_100NS - age
        : 0;
}

static __inline int NativeLdacExpireConnectedLinkState(
    NATIVE_LDAC_LINK_STATE* State,
    ULONGLONG NowInterruptTime100ns)
{
    if (State == NULL ||
        State->State != NativeLdacLinkStateConnected ||
        NativeLdacLinkStateRemainingFreshTime100ns(
            State,
            NowInterruptTime100ns) != 0)
    {
        return 0;
    }
    State->State = NativeLdacLinkStateDisconnected;
    State->UpdateSequence++;
    State->UpdatedInterruptTime100ns = NowInterruptTime100ns;
    return 1;
}

#endif // _NATIVE_LDAC_LINK_STATE_LOGIC_H_
