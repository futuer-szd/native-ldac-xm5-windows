// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_media_watchdog_contract.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", \
                __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void test_active_writes_extend_deadline(void) {
    NLD_MEDIA_WATCHDOG_OWNER owner;
    unsigned long long remaining = 0ull;

    NldMediaWatchdogInitialize(&owner);
    CHECK(NldMediaWatchdogArm(&owner, 4ul, 100ull));
    CHECK(NldMediaWatchdogRecordWrite(&owner, 4ul, 150ull));
    CHECK(NldMediaWatchdogCheck(&owner, 4ul, 200ull, 100ull,
                                &remaining) ==
          NldMediaWatchdogWaiting);
    CHECK(remaining == 50ull);
    CHECK(NldMediaWatchdogRecordWrite(&owner, 4ul, 225ull));
    CHECK(NldMediaWatchdogCheck(&owner, 4ul, 300ull, 100ull,
                                &remaining) ==
          NldMediaWatchdogWaiting);
    CHECK(remaining == 25ull);
}

static void test_timeout_disarms_and_is_idempotent(void) {
    NLD_MEDIA_WATCHDOG_OWNER owner;

    NldMediaWatchdogInitialize(&owner);
    CHECK(NldMediaWatchdogArm(&owner, 8ul, 1000ull));
    CHECK(NldMediaWatchdogCheck(&owner, 8ul, 1100ull, 100ull, 0) ==
          NldMediaWatchdogExpired);
    CHECK(!owner.Armed);
    CHECK(NldMediaWatchdogCheck(&owner, 8ul, 1200ull, 100ull, 0) ==
          NldMediaWatchdogIdle);
}

static void test_stale_generation_cannot_refresh_new_session(void) {
    NLD_MEDIA_WATCHDOG_OWNER owner;

    NldMediaWatchdogInitialize(&owner);
    CHECK(NldMediaWatchdogArm(&owner, 3ul, 100ull));
    CHECK(NldMediaWatchdogArm(&owner, 4ul, 200ull));
    CHECK(!NldMediaWatchdogRecordWrite(&owner, 3ul, 250ull));
    CHECK(owner.Generation == 4ul);
    CHECK(owner.LastWriteTime100ns == 200ull);
}

static void test_stop_is_recoverable(void) {
    NLD_MEDIA_WATCHDOG_OWNER owner;

    NldMediaWatchdogInitialize(&owner);
    CHECK(NldMediaWatchdogArm(&owner, 1ul, 100ull));
    NldMediaWatchdogStop(&owner);
    CHECK(!owner.Armed);
    CHECK(NldMediaWatchdogArm(&owner, 2ul, 200ull));
    CHECK(owner.Armed);
    CHECK(owner.Generation == 2ul);
}

static void test_clock_regression_fails_closed(void) {
    NLD_MEDIA_WATCHDOG_OWNER owner;

    NldMediaWatchdogInitialize(&owner);
    CHECK(NldMediaWatchdogArm(&owner, 5ul, 500ull));
    CHECK(NldMediaWatchdogCheck(&owner, 5ul, 499ull, 100ull, 0) ==
          NldMediaWatchdogExpired);
    CHECK(!owner.Armed);
}

int main(void) {
    test_active_writes_extend_deadline();
    test_timeout_disarms_and_is_idempotent();
    test_stale_generation_cannot_refresh_new_session();
    test_stop_is_recoverable();
    test_clock_regression_fails_closed();
    puts("Media watchdog contract tests passed");
    return 0;
}
