// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_media_watchdog_contract.h"

void NldMediaWatchdogInitialize(
    NLD_MEDIA_WATCHDOG_OWNER* owner) {
    if (owner == 0) return;
    owner->Generation = 0ul;
    owner->LastWriteTime100ns = 0ull;
    owner->Armed = 0;
}

int NldMediaWatchdogArm(
    NLD_MEDIA_WATCHDOG_OWNER* owner,
    unsigned long generation,
    unsigned long long now100ns) {
    if (owner == 0 || generation == 0ul || now100ns == 0ull) return 0;
    owner->Generation = generation;
    owner->LastWriteTime100ns = now100ns;
    owner->Armed = 1;
    return 1;
}

int NldMediaWatchdogRecordWrite(
    NLD_MEDIA_WATCHDOG_OWNER* owner,
    unsigned long generation,
    unsigned long long now100ns) {
    if (owner == 0 || !owner->Armed || generation == 0ul ||
        generation != owner->Generation || now100ns == 0ull ||
        now100ns < owner->LastWriteTime100ns) {
        return 0;
    }
    owner->LastWriteTime100ns = now100ns;
    return 1;
}

void NldMediaWatchdogStop(
    NLD_MEDIA_WATCHDOG_OWNER* owner) {
    if (owner == 0) return;
    owner->Generation = 0ul;
    owner->LastWriteTime100ns = 0ull;
    owner->Armed = 0;
}

NLD_MEDIA_WATCHDOG_RESULT NldMediaWatchdogCheck(
    NLD_MEDIA_WATCHDOG_OWNER* owner,
    unsigned long generation,
    unsigned long long now100ns,
    unsigned long long timeout100ns,
    unsigned long long* remaining100ns) {
    unsigned long long elapsed;

    if (remaining100ns != 0) *remaining100ns = 0ull;
    if (owner == 0 || !owner->Armed || generation == 0ul ||
        generation != owner->Generation) {
        return NldMediaWatchdogIdle;
    }
    if (now100ns == 0ull || timeout100ns == 0ull ||
        now100ns < owner->LastWriteTime100ns) {
        NldMediaWatchdogStop(owner);
        return NldMediaWatchdogExpired;
    }
    elapsed = now100ns - owner->LastWriteTime100ns;
    if (elapsed >= timeout100ns) {
        NldMediaWatchdogStop(owner);
        return NldMediaWatchdogExpired;
    }
    if (remaining100ns != 0) {
        *remaining100ns = timeout100ns - elapsed;
    }
    return NldMediaWatchdogWaiting;
}
