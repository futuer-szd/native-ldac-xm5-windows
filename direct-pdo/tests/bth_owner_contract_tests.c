// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_bth_owner_contract.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", \
                __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void test_success_and_single_release(void) {
    NLD_BTH_INTERFACE_OWNER owner;
    unsigned long generation;

    NldBthOwnerInitialize(&owner);
    CHECK(NldBthOwnerOnPnpStart(&owner) == NldBthOwnerActionQuery);
    generation = owner.Generation;
    CHECK(NldBthOwnerOnQueryComplete(&owner,
                                     generation,
                                     1,
                                     1) == NldBthOwnerActionNone);
    CHECK(owner.State == NldBthOwnerReady);
    CHECK(owner.ReferenceHeld);
    CHECK(NldBthOwnerOnPnpStop(&owner) ==
          NldBthOwnerActionDereference);
    CHECK(NldBthOwnerOnPnpStop(&owner) == NldBthOwnerActionNone);
    CHECK(NldBthOwnerIsConsistent(&owner));
}

static void test_stop_while_querying_releases_late_interface(void) {
    NLD_BTH_INTERFACE_OWNER owner;
    unsigned long generation;

    NldBthOwnerInitialize(&owner);
    CHECK(NldBthOwnerOnPnpStart(&owner) == NldBthOwnerActionQuery);
    generation = owner.Generation;
    CHECK(NldBthOwnerOnPnpStop(&owner) == NldBthOwnerActionNone);
    CHECK(NldBthOwnerOnQueryComplete(&owner,
                                     generation,
                                     1,
                                     1) ==
          NldBthOwnerActionDereference);
    CHECK(owner.State == NldBthOwnerOffline);
    CHECK(!owner.ReferenceHeld);
    CHECK(NldBthOwnerIsConsistent(&owner));
}

static void test_failed_query_and_retry(void) {
    NLD_BTH_INTERFACE_OWNER owner;
    unsigned long first_generation;

    NldBthOwnerInitialize(&owner);
    (void)NldBthOwnerOnPnpStart(&owner);
    first_generation = owner.Generation;
    CHECK(NldBthOwnerOnQueryComplete(&owner,
                                     first_generation,
                                     0,
                                     0) == NldBthOwnerActionNone);
    CHECK(owner.State == NldBthOwnerFaulted);
    CHECK(NldBthOwnerRetry(&owner) == NldBthOwnerActionQuery);
    CHECK(owner.Generation == first_generation + 1ul);
    CHECK(NldBthOwnerOnQueryComplete(&owner,
                                     owner.Generation,
                                     1,
                                     1) == NldBthOwnerActionNone);
    CHECK(owner.State == NldBthOwnerReady);
    CHECK(NldBthOwnerOnPnpStop(&owner) ==
          NldBthOwnerActionDereference);
}

static void test_failed_validation_releases_provider_reference(void) {
    NLD_BTH_INTERFACE_OWNER owner;

    NldBthOwnerInitialize(&owner);
    (void)NldBthOwnerOnPnpStart(&owner);
    CHECK(NldBthOwnerOnQueryComplete(&owner,
                                     owner.Generation,
                                     0,
                                     1) ==
          NldBthOwnerActionDereference);
    CHECK(owner.State == NldBthOwnerFaulted);
    CHECK(!owner.ReferenceHeld);
}

static void test_stale_completion_does_not_replace_new_query(void) {
    NLD_BTH_INTERFACE_OWNER owner;
    unsigned long stale_generation;

    NldBthOwnerInitialize(&owner);
    (void)NldBthOwnerOnPnpStart(&owner);
    stale_generation = owner.Generation;
    (void)NldBthOwnerOnPnpStop(&owner);
    CHECK(NldBthOwnerOnPnpStart(&owner) == NldBthOwnerActionQuery);
    CHECK(owner.Generation != stale_generation);
    CHECK(NldBthOwnerOnQueryComplete(&owner,
                                     stale_generation,
                                     1,
                                     1) ==
          NldBthOwnerActionDereference);
    CHECK(owner.State == NldBthOwnerQuerying);
    CHECK(owner.QueryPending);
    CHECK(NldBthOwnerOnQueryComplete(&owner,
                                     owner.Generation,
                                     1,
                                     1) == NldBthOwnerActionNone);
    CHECK(owner.State == NldBthOwnerReady);
    CHECK(NldBthOwnerIsConsistent(&owner));
    CHECK(NldBthOwnerOnPnpStop(&owner) ==
          NldBthOwnerActionDereference);
}

int main(void) {
    test_success_and_single_release();
    test_stop_while_querying_releases_late_interface();
    test_failed_query_and_retry();
    test_failed_validation_releases_provider_reference();
    test_stale_completion_does_not_replace_new_query();
    puts("Bth interface owner contract tests passed");
    return 0;
}
