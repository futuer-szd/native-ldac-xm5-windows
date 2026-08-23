// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_bth_indication_contract.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", \
                __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void test_teardown_without_bth_reference_frees(void) {
    NLD_BTH_INDICATION_OWNER owner;
    unsigned long generation;

    NldBthIndicationInitialize(&owner);
    generation = NldBthIndicationArm(&owner);
    CHECK(generation != 0ul);
    CHECK(NldBthIndicationBeginTeardown(&owner, generation) ==
          NldBthIndicationFreeContext);
    CHECK(NldBthIndicationIsConsistent(&owner));
}

static void test_bth_reference_defers_free(void) {
    NLD_BTH_INDICATION_OWNER owner;
    unsigned long generation;

    NldBthIndicationInitialize(&owner);
    generation = NldBthIndicationArm(&owner);
    CHECK(NldBthIndicationAddReference(&owner, generation) ==
          NldBthIndicationAccepted);
    CHECK(NldBthIndicationAddReference(&owner, generation) ==
          NldBthIndicationAccepted);
    CHECK(NldBthIndicationBeginTeardown(&owner, generation) ==
          NldBthIndicationReleaseOwner);
    CHECK(NldBthIndicationReleaseReference(&owner, generation) ==
          NldBthIndicationAccepted);
    CHECK(NldBthIndicationReleaseReference(&owner, generation) ==
          NldBthIndicationFreeContext);
    CHECK(NldBthIndicationIsConsistent(&owner));
}

static void test_remote_disconnect_disarms(void) {
    NLD_BTH_INDICATION_OWNER owner;
    unsigned long generation;

    NldBthIndicationInitialize(&owner);
    generation = NldBthIndicationArm(&owner);
    CHECK(NldBthIndicationRemoteDisconnect(&owner, generation) ==
          NldBthIndicationAccepted);
    CHECK(owner.RemoteDisconnected);
    CHECK(!owner.Armed);
    CHECK(NldBthIndicationAddReference(&owner, generation) ==
          NldBthIndicationIgnored);
    CHECK(NldBthIndicationBeginTeardown(&owner, generation) ==
          NldBthIndicationFreeContext);
}

static void test_stale_callbacks_cannot_mutate_new_generation(void) {
    NLD_BTH_INDICATION_OWNER owner;
    unsigned long old_generation;
    unsigned long generation;

    NldBthIndicationInitialize(&owner);
    old_generation = NldBthIndicationArm(&owner);
    CHECK(NldBthIndicationBeginTeardown(&owner, old_generation) ==
          NldBthIndicationFreeContext);
    generation = NldBthIndicationArm(&owner);
    CHECK(generation != old_generation);
    CHECK(NldBthIndicationRemoteDisconnect(&owner, old_generation) ==
          NldBthIndicationIgnored);
    CHECK(NldBthIndicationReleaseReference(&owner, old_generation) ==
          NldBthIndicationIgnored);
    CHECK(owner.Armed);
    CHECK(!owner.RemoteDisconnected);
}

static void test_duplicate_release_is_ignored(void) {
    NLD_BTH_INDICATION_OWNER owner;
    unsigned long generation;

    NldBthIndicationInitialize(&owner);
    generation = NldBthIndicationArm(&owner);
    CHECK(NldBthIndicationAddReference(&owner, generation) ==
          NldBthIndicationAccepted);
    CHECK(NldBthIndicationBeginTeardown(&owner, generation) ==
          NldBthIndicationReleaseOwner);
    CHECK(NldBthIndicationReleaseReference(&owner, generation) ==
          NldBthIndicationFreeContext);
    CHECK(NldBthIndicationReleaseReference(&owner, generation) ==
          NldBthIndicationIgnored);
}

int main(void) {
    test_teardown_without_bth_reference_frees();
    test_bth_reference_defers_free();
    test_remote_disconnect_disarms();
    test_stale_callbacks_cannot_mutate_new_generation();
    test_duplicate_release_is_ignored();
    puts("Bth indication contract tests passed");
    return 0;
}
