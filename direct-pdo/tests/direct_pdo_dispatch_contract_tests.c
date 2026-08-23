// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_direct_pdo_dispatch_contract.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", \
                __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static unsigned long take(
    NLD_DIRECT_PDO_DISPATCH_OWNER* owner,
    NLD_DIRECT_PDO_ACTION expected) {
    unsigned long generation = 0ul;

    CHECK(NldDirectPdoDispatchTakeAction(owner, &generation) == expected);
    CHECK(generation != 0ul);
    return generation;
}

static void drain_idle(NLD_DIRECT_PDO_DISPATCH_OWNER* owner) {
    unsigned long generation = 99ul;

    CHECK(NldDirectPdoDispatchTakeAction(owner, &generation) ==
          NldDirectPdoActionNone);
    CHECK(generation == 0ul);
    CHECK(!owner->WorkerOwned);
    CHECK(NldDirectPdoDispatchIsConsistent(owner));
}

static void test_acquire_queues_one_open(void) {
    NLD_DIRECT_PDO_DISPATCH_OWNER owner;
    unsigned long generation;

    NldDirectPdoDispatchInitialize(&owner);
    CHECK(NldDirectPdoDispatchOnPnpStart(&owner) ==
          NldDirectPdoDispatchNone);
    CHECK(NldDirectPdoDispatchSetKsIntent(&owner,
                                          NldDirectPdoKsAcquired) ==
          NldDirectPdoDispatchQueueWorker);
    CHECK(NldDirectPdoDispatchSetKsIntent(&owner,
                                          NldDirectPdoKsAcquired) ==
          NldDirectPdoDispatchNone);
    generation = take(&owner, NldDirectPdoActionOpen);
    CHECK(NldDirectPdoDispatchCompleteAction(&owner,
                                             generation,
                                             NldDirectPdoActionOpen,
                                             1));
    drain_idle(&owner);
    CHECK(owner.Session.TransportState == NldDirectPdoTransportOpen);
}

static void test_run_continues_in_same_worker(void) {
    NLD_DIRECT_PDO_DISPATCH_OWNER owner;
    unsigned long generation;

    NldDirectPdoDispatchInitialize(&owner);
    (void)NldDirectPdoDispatchOnPnpStart(&owner);
    CHECK(NldDirectPdoDispatchSetKsIntent(&owner,
                                          NldDirectPdoKsRunning) ==
          NldDirectPdoDispatchQueueWorker);
    generation = take(&owner, NldDirectPdoActionOpen);
    CHECK(NldDirectPdoDispatchCompleteAction(&owner,
                                             generation,
                                             NldDirectPdoActionOpen,
                                             1));
    generation = take(&owner, NldDirectPdoActionStart);
    CHECK(NldDirectPdoDispatchCompleteAction(&owner,
                                             generation,
                                             NldDirectPdoActionStart,
                                             1));
    drain_idle(&owner);
    CHECK(owner.Session.TransportState == NldDirectPdoTransportStreaming);
}

static void test_stop_intent_during_open_plans_close(void) {
    NLD_DIRECT_PDO_DISPATCH_OWNER owner;
    unsigned long generation;

    NldDirectPdoDispatchInitialize(&owner);
    (void)NldDirectPdoDispatchOnPnpStart(&owner);
    (void)NldDirectPdoDispatchSetKsIntent(&owner,
                                          NldDirectPdoKsAcquired);
    generation = take(&owner, NldDirectPdoActionOpen);
    CHECK(NldDirectPdoDispatchSetKsIntent(&owner,
                                          NldDirectPdoKsStopped) ==
          NldDirectPdoDispatchNone);
    CHECK(NldDirectPdoDispatchCompleteAction(&owner,
                                             generation,
                                             NldDirectPdoActionOpen,
                                             1));
    generation = take(&owner, NldDirectPdoActionClose);
    CHECK(NldDirectPdoDispatchCompleteAction(&owner,
                                             generation,
                                             NldDirectPdoActionClose,
                                             1));
    drain_idle(&owner);
    CHECK(owner.Session.TransportState == NldDirectPdoTransportClosed);
}

static void test_pnp_stop_cancels_active_then_closes(void) {
    NLD_DIRECT_PDO_DISPATCH_OWNER owner;
    unsigned long generation;

    NldDirectPdoDispatchInitialize(&owner);
    (void)NldDirectPdoDispatchOnPnpStart(&owner);
    (void)NldDirectPdoDispatchSetKsIntent(&owner,
                                          NldDirectPdoKsRunning);
    generation = take(&owner, NldDirectPdoActionOpen);
    CHECK(NldDirectPdoDispatchOnPnpStop(&owner) ==
          NldDirectPdoDispatchCancelActive);
    CHECK(NldDirectPdoDispatchCompleteAction(&owner,
                                             generation,
                                             NldDirectPdoActionOpen,
                                             1));
    generation = take(&owner, NldDirectPdoActionCancelAndClose);
    CHECK(NldDirectPdoDispatchCompleteAction(
        &owner,
        generation,
        NldDirectPdoActionCancelAndClose,
        1));
    drain_idle(&owner);
    CHECK(owner.Session.TransportState == NldDirectPdoTransportOffline);
}

static void test_stale_completion_is_rejected(void) {
    NLD_DIRECT_PDO_DISPATCH_OWNER owner;
    unsigned long generation;

    NldDirectPdoDispatchInitialize(&owner);
    (void)NldDirectPdoDispatchOnPnpStart(&owner);
    (void)NldDirectPdoDispatchSetKsIntent(&owner,
                                          NldDirectPdoKsAcquired);
    generation = take(&owner, NldDirectPdoActionOpen);
    CHECK(!NldDirectPdoDispatchCompleteAction(&owner,
                                              generation + 1ul,
                                              NldDirectPdoActionOpen,
                                              1));
    CHECK(owner.ActiveAction == NldDirectPdoActionOpen);
    CHECK(NldDirectPdoDispatchCompleteAction(&owner,
                                             generation,
                                             NldDirectPdoActionOpen,
                                             1));
    drain_idle(&owner);
}

static void test_fault_requires_explicit_retry(void) {
    NLD_DIRECT_PDO_DISPATCH_OWNER owner;
    unsigned long generation;
    unsigned long original_generation;

    NldDirectPdoDispatchInitialize(&owner);
    (void)NldDirectPdoDispatchOnPnpStart(&owner);
    (void)NldDirectPdoDispatchSetKsIntent(&owner,
                                          NldDirectPdoKsRunning);
    generation = take(&owner, NldDirectPdoActionOpen);
    original_generation = generation;
    CHECK(NldDirectPdoDispatchCompleteAction(&owner,
                                             generation,
                                             NldDirectPdoActionOpen,
                                             0));
    drain_idle(&owner);
    CHECK(owner.Session.TransportState == NldDirectPdoTransportFaulted);
    CHECK(NldDirectPdoDispatchRetry(&owner) ==
          NldDirectPdoDispatchNone);
    CHECK(owner.Session.TransportState == NldDirectPdoTransportFaulted);
    CHECK(NldDirectPdoDispatchSetKsIntent(&owner,
                                          NldDirectPdoKsStopped) ==
          NldDirectPdoDispatchNone);
    CHECK(NldDirectPdoDispatchRetry(&owner) ==
          NldDirectPdoDispatchNone);
    CHECK(owner.Session.Generation != original_generation);
    CHECK(owner.Session.TransportState == NldDirectPdoTransportClosed);
}

static void test_transport_loss_queues_cleanup_without_auto_reopen(void) {
    NLD_DIRECT_PDO_DISPATCH_OWNER owner;
    unsigned long generation;

    NldDirectPdoDispatchInitialize(&owner);
    (void)NldDirectPdoDispatchOnPnpStart(&owner);
    (void)NldDirectPdoDispatchSetKsIntent(&owner,
                                          NldDirectPdoKsRunning);
    generation = take(&owner, NldDirectPdoActionOpen);
    (void)NldDirectPdoDispatchCompleteAction(&owner,
                                             generation,
                                             NldDirectPdoActionOpen,
                                             1);
    generation = take(&owner, NldDirectPdoActionStart);
    (void)NldDirectPdoDispatchCompleteAction(&owner,
                                             generation,
                                             NldDirectPdoActionStart,
                                             1);
    drain_idle(&owner);

    CHECK(NldDirectPdoDispatchOnTransportLost(&owner) ==
          NldDirectPdoDispatchQueueWorker);
    generation = take(&owner, NldDirectPdoActionCancelAndClose);
    CHECK(NldDirectPdoDispatchCompleteAction(
        &owner,
        generation,
        NldDirectPdoActionCancelAndClose,
        1));
    drain_idle(&owner);
    CHECK(owner.Session.TransportState == NldDirectPdoTransportFaulted);
    CHECK(owner.Session.RecoveryRequired);
}

static void test_transport_loss_during_active_action_preserves_cleanup(void) {
    NLD_DIRECT_PDO_DISPATCH_OWNER owner;
    unsigned long generation;

    NldDirectPdoDispatchInitialize(&owner);
    (void)NldDirectPdoDispatchOnPnpStart(&owner);
    (void)NldDirectPdoDispatchSetKsIntent(&owner,
                                          NldDirectPdoKsRunning);
    generation = take(&owner, NldDirectPdoActionOpen);
    CHECK(NldDirectPdoDispatchOnTransportLost(&owner) ==
          NldDirectPdoDispatchCancelActive);
    CHECK(NldDirectPdoDispatchCompleteAction(&owner,
                                             generation,
                                             NldDirectPdoActionOpen,
                                             0));
    generation = take(&owner, NldDirectPdoActionCancelAndClose);
    CHECK(NldDirectPdoDispatchCompleteAction(
        &owner,
        generation,
        NldDirectPdoActionCancelAndClose,
        1));
    drain_idle(&owner);
    CHECK(owner.Session.TransportState == NldDirectPdoTransportFaulted);
}

int main(void) {
    test_acquire_queues_one_open();
    test_run_continues_in_same_worker();
    test_stop_intent_during_open_plans_close();
    test_pnp_stop_cancels_active_then_closes();
    test_stale_completion_is_rejected();
    test_fault_requires_explicit_retry();
    test_transport_loss_queues_cleanup_without_auto_reopen();
    test_transport_loss_during_active_action_preserves_cleanup();
    puts("Direct-PDO dispatcher contract tests passed");
    return 0;
}
