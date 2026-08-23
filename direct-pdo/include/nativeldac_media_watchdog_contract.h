// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_MEDIA_WATCHDOG_CONTRACT_H
#define NATIVE_LDAC_MEDIA_WATCHDOG_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NLD_MEDIA_WATCHDOG_RESULT {
    NldMediaWatchdogIdle = 0,
    NldMediaWatchdogWaiting = 1,
    NldMediaWatchdogExpired = 2
} NLD_MEDIA_WATCHDOG_RESULT;

typedef struct NLD_MEDIA_WATCHDOG_OWNER {
    unsigned long Generation;
    unsigned long long LastWriteTime100ns;
    int Armed;
} NLD_MEDIA_WATCHDOG_OWNER;

void NldMediaWatchdogInitialize(
    NLD_MEDIA_WATCHDOG_OWNER* owner);

int NldMediaWatchdogArm(
    NLD_MEDIA_WATCHDOG_OWNER* owner,
    unsigned long generation,
    unsigned long long now100ns);

int NldMediaWatchdogRecordWrite(
    NLD_MEDIA_WATCHDOG_OWNER* owner,
    unsigned long generation,
    unsigned long long now100ns);

void NldMediaWatchdogStop(
    NLD_MEDIA_WATCHDOG_OWNER* owner);

NLD_MEDIA_WATCHDOG_RESULT NldMediaWatchdogCheck(
    NLD_MEDIA_WATCHDOG_OWNER* owner,
    unsigned long generation,
    unsigned long long now100ns,
    unsigned long long timeout100ns,
    unsigned long long* remaining100ns);

#ifdef __cplusplus
}
#endif

#endif
