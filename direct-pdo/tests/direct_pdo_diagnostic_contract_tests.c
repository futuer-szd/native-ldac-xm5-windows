// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_direct_pdo_diagnostic_contract.h"

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
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner,
    NLD_DIRECT_PDO_DIAGNOSTIC_ACTION expected) {
    unsigned long generation = 0ul;

    CHECK(NldDirectPdoDiagnosticTakeAction(owner, &generation) == expected);
    CHECK(generation != 0ul);
    return generation;
}

static void drain(NLD_DIRECT_PDO_DIAGNOSTIC_OWNER* owner) {
    unsigned long generation = 99ul;

    CHECK(NldDirectPdoDiagnosticTakeAction(owner, &generation) ==
          NldDirectPdoDiagnosticActionNone);
    CHECK(generation == 0ul);
    CHECK(!owner->WorkerOwned);
    CHECK(NldDirectPdoDiagnosticIsConsistent(owner));
}

static void test_no_automatic_request_on_pnp_start(void) {
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER owner;

    NldDirectPdoDiagnosticInitialize(&owner);
    CHECK(NldDirectPdoDiagnosticOnPnpStart(&owner) ==
          NldDirectPdoDiagnosticCommandNone);
    CHECK(owner.State == NldDirectPdoDiagnosticIdle);
    CHECK(!owner.WorkerOwned);
    CHECK(NldDirectPdoDiagnosticIsConsistent(&owner));
}

static void test_success_is_open_discover_close(void) {
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER owner;
    unsigned long generation;
    unsigned long completed_generation;

    NldDirectPdoDiagnosticInitialize(&owner);
    (void)NldDirectPdoDiagnosticOnPnpStart(&owner);
    CHECK(NldDirectPdoDiagnosticRequestDiscover(&owner) ==
          NldDirectPdoDiagnosticCommandQueueWorker);
    CHECK(NldDirectPdoDiagnosticRequestDiscover(&owner) ==
          NldDirectPdoDiagnosticCommandNone);
    generation = take(&owner, NldDirectPdoDiagnosticActionOpen);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner, generation, NldDirectPdoDiagnosticActionOpen, 1));
    generation = take(&owner, NldDirectPdoDiagnosticActionDiscover);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner, generation, NldDirectPdoDiagnosticActionDiscover, 1));
    generation = take(&owner, NldDirectPdoDiagnosticActionClose);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner, generation, NldDirectPdoDiagnosticActionClose, 1));
    drain(&owner);
    CHECK(owner.State == NldDirectPdoDiagnosticComplete);
    completed_generation = owner.Generation;
    CHECK(NldDirectPdoDiagnosticRequestDiscover(&owner) ==
          NldDirectPdoDiagnosticCommandQueueWorker);
    generation = take(&owner, NldDirectPdoDiagnosticActionOpen);
    CHECK(generation != completed_generation);
}

static void test_discover_failure_still_closes(void) {
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER owner;
    unsigned long generation;

    NldDirectPdoDiagnosticInitialize(&owner);
    (void)NldDirectPdoDiagnosticOnPnpStart(&owner);
    (void)NldDirectPdoDiagnosticRequestDiscover(&owner);
    generation = take(&owner, NldDirectPdoDiagnosticActionOpen);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner, generation, NldDirectPdoDiagnosticActionOpen, 1));
    generation = take(&owner, NldDirectPdoDiagnosticActionDiscover);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner, generation, NldDirectPdoDiagnosticActionDiscover, 0));
    generation = take(&owner, NldDirectPdoDiagnosticActionClose);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner, generation, NldDirectPdoDiagnosticActionClose, 1));
    drain(&owner);
    CHECK(owner.State == NldDirectPdoDiagnosticFaulted);
    CHECK(owner.LastCloseSucceeded);
}

static void test_open_failure_faults_without_discover(void) {
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER owner;
    unsigned long generation;

    NldDirectPdoDiagnosticInitialize(&owner);
    (void)NldDirectPdoDiagnosticOnPnpStart(&owner);
    (void)NldDirectPdoDiagnosticRequestDiscover(&owner);
    generation = take(&owner, NldDirectPdoDiagnosticActionOpen);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner, generation, NldDirectPdoDiagnosticActionOpen, 0));
    drain(&owner);
    CHECK(owner.State == NldDirectPdoDiagnosticFaulted);
}

static void test_close_failure_faults(void) {
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER owner;
    unsigned long generation;

    NldDirectPdoDiagnosticInitialize(&owner);
    (void)NldDirectPdoDiagnosticOnPnpStart(&owner);
    (void)NldDirectPdoDiagnosticRequestDiscover(&owner);
    generation = take(&owner, NldDirectPdoDiagnosticActionOpen);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner, generation, NldDirectPdoDiagnosticActionOpen, 1));
    generation = take(&owner, NldDirectPdoDiagnosticActionDiscover);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner, generation, NldDirectPdoDiagnosticActionDiscover, 1));
    generation = take(&owner, NldDirectPdoDiagnosticActionClose);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner, generation, NldDirectPdoDiagnosticActionClose, 0));
    drain(&owner);
    CHECK(owner.State == NldDirectPdoDiagnosticFaulted);
    CHECK(!owner.LastCloseSucceeded);
}

static void test_preempt_replaces_queued_open_and_returns_idle(void) {
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER owner;
    unsigned long generation;

    NldDirectPdoDiagnosticInitialize(&owner);
    (void)NldDirectPdoDiagnosticOnPnpStart(&owner);
    (void)NldDirectPdoDiagnosticRequestDiscover(&owner);
    CHECK(NldDirectPdoDiagnosticRequestCancel(&owner) ==
          NldDirectPdoDiagnosticCommandNone);
    CHECK(owner.CancelRequested);
    generation = take(
        &owner,
        NldDirectPdoDiagnosticActionCancelAndClose);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner,
        generation,
        NldDirectPdoDiagnosticActionCancelAndClose,
        1));
    drain(&owner);
    CHECK(owner.State == NldDirectPdoDiagnosticIdle);
    CHECK(owner.PnpStarted);
    CHECK(!owner.CancelRequested);
    CHECK(NldDirectPdoDiagnosticRequestDiscover(&owner) ==
          NldDirectPdoDiagnosticCommandQueueWorker);
}

static void test_preempt_cancels_active_and_closes(void) {
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER owner;
    unsigned long generation;

    NldDirectPdoDiagnosticInitialize(&owner);
    (void)NldDirectPdoDiagnosticOnPnpStart(&owner);
    (void)NldDirectPdoDiagnosticRequestDiscover(&owner);
    generation = take(&owner, NldDirectPdoDiagnosticActionOpen);
    CHECK(NldDirectPdoDiagnosticRequestCancel(&owner) ==
          NldDirectPdoDiagnosticCommandCancelActive);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner,
        generation,
        NldDirectPdoDiagnosticActionOpen,
        1));
    generation = take(
        &owner,
        NldDirectPdoDiagnosticActionCancelAndClose);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner,
        generation,
        NldDirectPdoDiagnosticActionCancelAndClose,
        1));
    drain(&owner);
    CHECK(owner.State == NldDirectPdoDiagnosticIdle);
}

static void test_preempt_close_failure_stays_online_but_faulted(void) {
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER owner;
    unsigned long generation;

    NldDirectPdoDiagnosticInitialize(&owner);
    (void)NldDirectPdoDiagnosticOnPnpStart(&owner);
    (void)NldDirectPdoDiagnosticRequestDiscover(&owner);
    CHECK(NldDirectPdoDiagnosticRequestCancel(&owner) ==
          NldDirectPdoDiagnosticCommandNone);
    generation = take(
        &owner,
        NldDirectPdoDiagnosticActionCancelAndClose);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner,
        generation,
        NldDirectPdoDiagnosticActionCancelAndClose,
        0));
    drain(&owner);
    CHECK(owner.State == NldDirectPdoDiagnosticFaulted);
    CHECK(owner.PnpStarted);
    CHECK(!owner.CancelRequested);
}

static void test_pnp_stop_supersedes_preempt(void) {
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER owner;
    unsigned long generation;

    NldDirectPdoDiagnosticInitialize(&owner);
    (void)NldDirectPdoDiagnosticOnPnpStart(&owner);
    (void)NldDirectPdoDiagnosticRequestDiscover(&owner);
    generation = take(&owner, NldDirectPdoDiagnosticActionOpen);
    CHECK(NldDirectPdoDiagnosticRequestCancel(&owner) ==
          NldDirectPdoDiagnosticCommandCancelActive);
    CHECK(NldDirectPdoDiagnosticOnPnpStop(&owner) ==
          NldDirectPdoDiagnosticCommandCancelActive);
    CHECK(!owner.CancelRequested);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner,
        generation,
        NldDirectPdoDiagnosticActionOpen,
        0));
    generation = take(
        &owner,
        NldDirectPdoDiagnosticActionCancelAndClose);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner,
        generation,
        NldDirectPdoDiagnosticActionCancelAndClose,
        1));
    drain(&owner);
    CHECK(owner.State == NldDirectPdoDiagnosticOffline);
}

static void test_pnp_stop_cancels_active_and_closes(void) {
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER owner;
    unsigned long generation;

    NldDirectPdoDiagnosticInitialize(&owner);
    (void)NldDirectPdoDiagnosticOnPnpStart(&owner);
    (void)NldDirectPdoDiagnosticRequestDiscover(&owner);
    generation = take(&owner, NldDirectPdoDiagnosticActionOpen);
    CHECK(NldDirectPdoDiagnosticOnPnpStop(&owner) ==
          NldDirectPdoDiagnosticCommandCancelActive);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner, generation, NldDirectPdoDiagnosticActionOpen, 1));
    generation = take(&owner,
                      NldDirectPdoDiagnosticActionCancelAndClose);
    CHECK(NldDirectPdoDiagnosticOnPnpStop(&owner) ==
          NldDirectPdoDiagnosticCommandNone);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner, generation,
        NldDirectPdoDiagnosticActionCancelAndClose, 1));
    drain(&owner);
    CHECK(owner.State == NldDirectPdoDiagnosticOffline);
}

static void test_pnp_stop_replaces_queued_open(void) {
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER owner;
    unsigned long generation;

    NldDirectPdoDiagnosticInitialize(&owner);
    (void)NldDirectPdoDiagnosticOnPnpStart(&owner);
    (void)NldDirectPdoDiagnosticRequestDiscover(&owner);
    CHECK(NldDirectPdoDiagnosticOnPnpStop(&owner) ==
          NldDirectPdoDiagnosticCommandNone);
    generation = take(&owner,
                      NldDirectPdoDiagnosticActionCancelAndClose);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner, generation,
        NldDirectPdoDiagnosticActionCancelAndClose, 1));
    drain(&owner);
}

static void test_pnp_stop_while_idle_queues_only_close(void) {
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER owner;
    unsigned long generation;

    NldDirectPdoDiagnosticInitialize(&owner);
    (void)NldDirectPdoDiagnosticOnPnpStart(&owner);
    CHECK(NldDirectPdoDiagnosticOnPnpStop(&owner) ==
          NldDirectPdoDiagnosticCommandQueueWorker);
    generation = take(&owner,
                      NldDirectPdoDiagnosticActionCancelAndClose);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner, generation,
        NldDirectPdoDiagnosticActionCancelAndClose, 1));
    drain(&owner);
}

static void test_stale_completion_cannot_finish_new_request(void) {
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER owner;
    unsigned long old_generation;
    unsigned long generation;

    NldDirectPdoDiagnosticInitialize(&owner);
    (void)NldDirectPdoDiagnosticOnPnpStart(&owner);
    (void)NldDirectPdoDiagnosticRequestDiscover(&owner);
    old_generation = take(&owner, NldDirectPdoDiagnosticActionOpen);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &owner, old_generation, NldDirectPdoDiagnosticActionOpen, 0));
    drain(&owner);
    CHECK(NldDirectPdoDiagnosticRequestDiscover(&owner) ==
          NldDirectPdoDiagnosticCommandQueueWorker);
    generation = take(&owner, NldDirectPdoDiagnosticActionOpen);
    CHECK(generation != old_generation);
    CHECK(!NldDirectPdoDiagnosticCompleteAction(
        &owner, old_generation, NldDirectPdoDiagnosticActionOpen, 1));
    CHECK(owner.ActiveAction == NldDirectPdoDiagnosticActionOpen);
}

int main(void) {
    test_no_automatic_request_on_pnp_start();
    test_success_is_open_discover_close();
    test_discover_failure_still_closes();
    test_open_failure_faults_without_discover();
    test_close_failure_faults();
    test_preempt_replaces_queued_open_and_returns_idle();
    test_preempt_cancels_active_and_closes();
    test_preempt_close_failure_stays_online_but_faulted();
    test_pnp_stop_supersedes_preempt();
    test_pnp_stop_cancels_active_and_closes();
    test_pnp_stop_replaces_queued_open();
    test_pnp_stop_while_idle_queues_only_close();
    test_stale_completion_cannot_finish_new_request();
    puts("Direct-PDO diagnostic contract tests passed");
    return 0;
}
