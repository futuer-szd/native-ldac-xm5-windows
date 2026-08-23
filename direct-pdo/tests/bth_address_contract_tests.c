// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_bth_address_contract.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", \
                __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void test_remote_then_local_success(void) {
    NLD_BTH_ADDRESS_DISCOVERY discovery;
    unsigned long generation;

    NldBthAddressInitialize(&discovery);
    CHECK(NldBthAddressOnPnpStart(&discovery) ==
          NldBthAddressActionQueryRemote);
    generation = discovery.Generation;
    CHECK(NldBthAddressOnRemoteComplete(&discovery,
                                        generation,
                                        1) ==
          NldBthAddressActionQueryLocal);
    CHECK(NldBthAddressOnLocalComplete(&discovery,
                                       generation,
                                       1) ==
          NldBthAddressActionNone);
    CHECK(discovery.State == NldBthAddressReady);
    CHECK(NldBthAddressIsConsistent(&discovery));
    NldBthAddressOnPnpStop(&discovery);
    CHECK(discovery.State == NldBthAddressOffline);
}

static void test_remote_failure_fails_closed(void) {
    NLD_BTH_ADDRESS_DISCOVERY discovery;

    NldBthAddressInitialize(&discovery);
    (void)NldBthAddressOnPnpStart(&discovery);
    CHECK(NldBthAddressOnRemoteComplete(&discovery,
                                        discovery.Generation,
                                        0) ==
          NldBthAddressActionNone);
    CHECK(discovery.State == NldBthAddressFaulted);
    CHECK(NldBthAddressOnLocalComplete(&discovery,
                                       discovery.Generation,
                                       1) ==
          NldBthAddressActionNone);
    CHECK(discovery.State == NldBthAddressFaulted);
}

static void test_local_failure_fails_closed(void) {
    NLD_BTH_ADDRESS_DISCOVERY discovery;

    NldBthAddressInitialize(&discovery);
    (void)NldBthAddressOnPnpStart(&discovery);
    CHECK(NldBthAddressOnRemoteComplete(&discovery,
                                        discovery.Generation,
                                        1) ==
          NldBthAddressActionQueryLocal);
    (void)NldBthAddressOnLocalComplete(&discovery,
                                       discovery.Generation,
                                       0);
    CHECK(discovery.State == NldBthAddressFaulted);
    CHECK(NldBthAddressIsConsistent(&discovery));
}

static void test_stop_rejects_stale_remote_completion(void) {
    NLD_BTH_ADDRESS_DISCOVERY discovery;
    unsigned long stale_generation;

    NldBthAddressInitialize(&discovery);
    (void)NldBthAddressOnPnpStart(&discovery);
    stale_generation = discovery.Generation;
    NldBthAddressOnPnpStop(&discovery);
    CHECK(NldBthAddressOnRemoteComplete(&discovery,
                                        stale_generation,
                                        1) ==
          NldBthAddressActionNone);
    CHECK(discovery.State == NldBthAddressOffline);
    CHECK(NldBthAddressIsConsistent(&discovery));
}

static void test_restart_rejects_previous_generation(void) {
    NLD_BTH_ADDRESS_DISCOVERY discovery;
    unsigned long stale_generation;

    NldBthAddressInitialize(&discovery);
    (void)NldBthAddressOnPnpStart(&discovery);
    stale_generation = discovery.Generation;
    NldBthAddressOnPnpStop(&discovery);
    CHECK(NldBthAddressOnPnpStart(&discovery) ==
          NldBthAddressActionQueryRemote);
    CHECK(discovery.Generation != stale_generation);
    CHECK(NldBthAddressOnRemoteComplete(&discovery,
                                        stale_generation,
                                        1) ==
          NldBthAddressActionNone);
    CHECK(discovery.State == NldBthAddressRemotePending);
    CHECK(NldBthAddressOnRemoteComplete(&discovery,
                                        discovery.Generation,
                                        1) ==
          NldBthAddressActionQueryLocal);
}

int main(void) {
    test_remote_then_local_success();
    test_remote_failure_fails_closed();
    test_local_failure_fails_closed();
    test_stop_rejects_stale_remote_completion();
    test_restart_rejects_previous_generation();
    puts("Bth address discovery contract tests passed");
    return 0;
}
