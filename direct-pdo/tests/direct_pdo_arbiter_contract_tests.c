// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_direct_pdo_arbiter_contract.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", \
                __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static unsigned long acquire(
    NLD_DIRECT_PDO_ARBITER_OWNER* owner,
    NLD_DIRECT_PDO_ARBITER_CLIENT client) {
    unsigned long generation = 0ul;

    CHECK(NldDirectPdoArbiterTryAcquire(owner, client, &generation) ==
          NldDirectPdoArbiterAcquireGranted);
    CHECK(generation != 0ul);
    CHECK(NldDirectPdoArbiterIsConsistent(owner));
    return generation;
}

static void test_diagnostic_requires_no_render_demand(void) {
    NLD_DIRECT_PDO_ARBITER_OWNER owner;
    unsigned long generation = 99ul;

    NldDirectPdoArbiterInitialize(&owner);
    CHECK(NldDirectPdoArbiterOnPnpStart(&owner));
    CHECK(NldDirectPdoArbiterSetRenderDemand(&owner, 1) ==
          NldDirectPdoArbiterDemandAccepted);
    CHECK(NldDirectPdoArbiterTryAcquire(
        &owner, NldDirectPdoArbiterClientDiagnostic, &generation) ==
          NldDirectPdoArbiterAcquireRejected);
    CHECK(generation == 0ul);
    CHECK(NldDirectPdoArbiterSetRenderDemand(&owner, 0) ==
          NldDirectPdoArbiterDemandAccepted);
    generation = acquire(&owner, NldDirectPdoArbiterClientDiagnostic);
    CHECK(NldDirectPdoArbiterRelease(
        &owner, NldDirectPdoArbiterClientDiagnostic, generation));
}

static void test_only_one_client_owns_signaling(void) {
    NLD_DIRECT_PDO_ARBITER_OWNER owner;
    unsigned long generation;
    unsigned long duplicate_generation = 0ul;

    NldDirectPdoArbiterInitialize(&owner);
    CHECK(NldDirectPdoArbiterOnPnpStart(&owner));
    generation = acquire(&owner, NldDirectPdoArbiterClientRender);
    CHECK(NldDirectPdoArbiterTryAcquire(
        &owner, NldDirectPdoArbiterClientRender,
        &duplicate_generation) ==
          NldDirectPdoArbiterAcquireAlreadyOwned);
    CHECK(duplicate_generation == generation);
    CHECK(NldDirectPdoArbiterTryAcquire(
        &owner, NldDirectPdoArbiterClientDiagnostic,
        &duplicate_generation) == NldDirectPdoArbiterAcquireRejected);
    CHECK(!NldDirectPdoArbiterRelease(
        &owner, NldDirectPdoArbiterClientRender, generation + 1ul));
    CHECK(NldDirectPdoArbiterRelease(
        &owner, NldDirectPdoArbiterClientRender, generation));
}

static void test_render_demand_preempts_future_diagnostic_work(void) {
    NLD_DIRECT_PDO_ARBITER_OWNER owner;
    unsigned long diagnostic_generation;
    unsigned long render_generation = 0ul;

    NldDirectPdoArbiterInitialize(&owner);
    CHECK(NldDirectPdoArbiterOnPnpStart(&owner));
    diagnostic_generation = acquire(
        &owner, NldDirectPdoArbiterClientDiagnostic);
    CHECK(NldDirectPdoArbiterSetRenderDemand(&owner, 1) ==
          NldDirectPdoArbiterDemandPreemptDiagnostic);
    CHECK(NldDirectPdoArbiterTryAcquire(
        &owner, NldDirectPdoArbiterClientRender, &render_generation) ==
          NldDirectPdoArbiterAcquireRejected);
    CHECK(NldDirectPdoArbiterRelease(
        &owner, NldDirectPdoArbiterClientDiagnostic,
        diagnostic_generation));
    render_generation = acquire(
        &owner, NldDirectPdoArbiterClientRender);
    CHECK(NldDirectPdoArbiterRelease(
        &owner, NldDirectPdoArbiterClientRender, render_generation));
}

static void test_pnp_stop_waits_for_current_owner(void) {
    NLD_DIRECT_PDO_ARBITER_OWNER owner;
    unsigned long generation;

    NldDirectPdoArbiterInitialize(&owner);
    CHECK(NldDirectPdoArbiterOnPnpStart(&owner));
    generation = acquire(&owner, NldDirectPdoArbiterClientRender);
    CHECK(NldDirectPdoArbiterOnPnpStop(&owner));
    CHECK(owner.State == NldDirectPdoArbiterStopping);
    CHECK(!owner.PnpStarted);
    CHECK(NldDirectPdoArbiterTryAcquire(
        &owner, NldDirectPdoArbiterClientDiagnostic, 0) ==
          NldDirectPdoArbiterAcquireRejected);
    CHECK(NldDirectPdoArbiterRelease(
        &owner, NldDirectPdoArbiterClientRender, generation));
    CHECK(owner.State == NldDirectPdoArbiterOffline);
    CHECK(NldDirectPdoArbiterIsConsistent(&owner));
}

static void test_restart_rejects_stale_release(void) {
    NLD_DIRECT_PDO_ARBITER_OWNER owner;
    unsigned long old_generation;
    unsigned long generation;

    NldDirectPdoArbiterInitialize(&owner);
    CHECK(NldDirectPdoArbiterOnPnpStart(&owner));
    old_generation = acquire(&owner,
                             NldDirectPdoArbiterClientDiagnostic);
    NldDirectPdoArbiterForceOffline(&owner);
    CHECK(NldDirectPdoArbiterOnPnpStart(&owner));
    generation = acquire(&owner, NldDirectPdoArbiterClientRender);
    CHECK(generation != old_generation);
    CHECK(!NldDirectPdoArbiterRelease(
        &owner, NldDirectPdoArbiterClientDiagnostic, old_generation));
    CHECK(owner.Client == NldDirectPdoArbiterClientRender);
}

static void test_idle_stop_finishes_immediately(void) {
    NLD_DIRECT_PDO_ARBITER_OWNER owner;

    NldDirectPdoArbiterInitialize(&owner);
    CHECK(NldDirectPdoArbiterOnPnpStart(&owner));
    CHECK(NldDirectPdoArbiterOnPnpStop(&owner));
    CHECK(owner.State == NldDirectPdoArbiterOffline);
    CHECK(NldDirectPdoArbiterIsConsistent(&owner));
}

int main(void) {
    test_diagnostic_requires_no_render_demand();
    test_only_one_client_owns_signaling();
    test_render_demand_preempts_future_diagnostic_work();
    test_pnp_stop_waits_for_current_owner();
    test_restart_rejects_stale_release();
    test_idle_stop_finishes_immediately();
    puts("Direct-PDO arbiter contract tests passed");
    return 0;
}
