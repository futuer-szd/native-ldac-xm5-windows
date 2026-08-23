// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_direct_pdo_preemption_contract.h"

#include "nativeldac_direct_pdo_arbiter_contract.h"
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
    NLD_DIRECT_PDO_PREEMPTION_OWNER* owner,
    NLD_DIRECT_PDO_PREEMPTION_ACTION expected) {
    unsigned long generation = 0ul;

    CHECK(NldDirectPdoPreemptionTakeAction(owner, &generation) ==
          expected);
    CHECK(generation != 0ul);
    return generation;
}

static void start(NLD_DIRECT_PDO_PREEMPTION_OWNER* owner) {
    NldDirectPdoPreemptionInitialize(owner);
    CHECK(NldDirectPdoPreemptionOnPnpStart(owner));
    CHECK(NldDirectPdoPreemptionIsConsistent(owner));
}

static void test_cancel_then_retry_render(void) {
    NLD_DIRECT_PDO_PREEMPTION_OWNER owner;
    unsigned long generation;

    start(&owner);
    CHECK(NldDirectPdoPreemptionRequestRender(&owner));
    generation = take(
        &owner,
        NldDirectPdoPreemptionActionCancelDiagnostic);
    CHECK(NldDirectPdoPreemptionCompleteAction(
        &owner,
        generation,
        NldDirectPdoPreemptionActionCancelDiagnostic,
        1));
    CHECK(owner.State == NldDirectPdoPreemptionRetryingRender);
    generation = take(&owner,
                      NldDirectPdoPreemptionActionRetryRender);
    CHECK(NldDirectPdoPreemptionCompleteAction(
        &owner,
        generation,
        NldDirectPdoPreemptionActionRetryRender,
        1));
    CHECK(owner.State == NldDirectPdoPreemptionComplete);
    CHECK(NldDirectPdoPreemptionIsConsistent(&owner));
}

static void test_cancel_timeout_faults_without_retry(void) {
    NLD_DIRECT_PDO_PREEMPTION_OWNER owner;
    unsigned long generation;
    unsigned long next_generation;

    start(&owner);
    CHECK(NldDirectPdoPreemptionRequestRender(&owner));
    generation = take(
        &owner,
        NldDirectPdoPreemptionActionCancelDiagnostic);
    CHECK(NldDirectPdoPreemptionCompleteAction(
        &owner,
        generation,
        NldDirectPdoPreemptionActionCancelDiagnostic,
        0));
    CHECK(owner.State == NldDirectPdoPreemptionFaulted);
    CHECK(NldDirectPdoPreemptionTakeAction(&owner, 0) ==
          NldDirectPdoPreemptionActionNone);
    CHECK(NldDirectPdoPreemptionRequestRender(&owner));
    next_generation = take(
        &owner,
        NldDirectPdoPreemptionActionCancelDiagnostic);
    CHECK(next_generation != generation);
}

static void test_retry_failure_is_faulted(void) {
    NLD_DIRECT_PDO_PREEMPTION_OWNER owner;
    unsigned long generation;

    start(&owner);
    CHECK(NldDirectPdoPreemptionRequestRender(&owner));
    generation = take(
        &owner,
        NldDirectPdoPreemptionActionCancelDiagnostic);
    CHECK(NldDirectPdoPreemptionCompleteAction(
        &owner,
        generation,
        NldDirectPdoPreemptionActionCancelDiagnostic,
        1));
    generation = take(&owner,
                      NldDirectPdoPreemptionActionRetryRender);
    CHECK(NldDirectPdoPreemptionCompleteAction(
        &owner,
        generation,
        NldDirectPdoPreemptionActionRetryRender,
        0));
    CHECK(owner.State == NldDirectPdoPreemptionFaulted);
}

static void test_stale_completion_is_rejected(void) {
    NLD_DIRECT_PDO_PREEMPTION_OWNER owner;
    unsigned long generation;

    start(&owner);
    CHECK(NldDirectPdoPreemptionRequestRender(&owner));
    generation = take(
        &owner,
        NldDirectPdoPreemptionActionCancelDiagnostic);
    CHECK(!NldDirectPdoPreemptionCompleteAction(
        &owner,
        generation + 1ul,
        NldDirectPdoPreemptionActionCancelDiagnostic,
        1));
    CHECK(owner.ActiveAction ==
          NldDirectPdoPreemptionActionCancelDiagnostic);
}

static void test_pnp_stop_cancels_pending_or_waits_active(void) {
    NLD_DIRECT_PDO_PREEMPTION_OWNER owner;
    unsigned long generation;

    start(&owner);
    CHECK(NldDirectPdoPreemptionRequestRender(&owner));
    CHECK(NldDirectPdoPreemptionOnPnpStop(&owner));
    CHECK(owner.State == NldDirectPdoPreemptionOffline);
    CHECK(NldDirectPdoPreemptionIsConsistent(&owner));

    CHECK(NldDirectPdoPreemptionOnPnpStart(&owner));
    CHECK(NldDirectPdoPreemptionRequestRender(&owner));
    generation = take(
        &owner,
        NldDirectPdoPreemptionActionCancelDiagnostic);
    CHECK(NldDirectPdoPreemptionOnPnpStop(&owner));
    CHECK(owner.State == NldDirectPdoPreemptionStopping);
    CHECK(NldDirectPdoPreemptionCompleteAction(
        &owner,
        generation,
        NldDirectPdoPreemptionActionCancelDiagnostic,
        1));
    CHECK(owner.State == NldDirectPdoPreemptionOffline);
    CHECK(NldDirectPdoPreemptionIsConsistent(&owner));
}

static void test_full_contract_handoff_preserves_render_priority(void) {
    NLD_DIRECT_PDO_ARBITER_OWNER arbiter;
    NLD_DIRECT_PDO_DIAGNOSTIC_OWNER diagnostic;
    NLD_DIRECT_PDO_PREEMPTION_OWNER preemption;
    unsigned long diagnostic_generation = 0ul;
    unsigned long diagnostic_action_generation = 0ul;
    unsigned long preemption_generation = 0ul;
    unsigned long render_generation = 0ul;

    NldDirectPdoArbiterInitialize(&arbiter);
    CHECK(NldDirectPdoArbiterOnPnpStart(&arbiter));
    NldDirectPdoDiagnosticInitialize(&diagnostic);
    (void)NldDirectPdoDiagnosticOnPnpStart(&diagnostic);
    start(&preemption);
    CHECK(NldDirectPdoArbiterTryAcquire(
        &arbiter,
        NldDirectPdoArbiterClientDiagnostic,
        &diagnostic_generation) == NldDirectPdoArbiterAcquireGranted);
    CHECK(NldDirectPdoDiagnosticRequestDiscover(&diagnostic) ==
          NldDirectPdoDiagnosticCommandQueueWorker);
    CHECK(NldDirectPdoDiagnosticTakeAction(
        &diagnostic,
        &diagnostic_action_generation) ==
          NldDirectPdoDiagnosticActionOpen);
    CHECK(NldDirectPdoArbiterSetRenderDemand(&arbiter, 1) ==
          NldDirectPdoArbiterDemandPreemptDiagnostic);
    CHECK(NldDirectPdoPreemptionRequestRender(&preemption));
    CHECK(NldDirectPdoPreemptionTakeAction(
        &preemption,
        &preemption_generation) ==
          NldDirectPdoPreemptionActionCancelDiagnostic);
    CHECK(NldDirectPdoDiagnosticRequestCancel(&diagnostic) ==
          NldDirectPdoDiagnosticCommandCancelActive);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &diagnostic,
        diagnostic_action_generation,
        NldDirectPdoDiagnosticActionOpen,
        0));
    CHECK(NldDirectPdoDiagnosticTakeAction(
        &diagnostic,
        &diagnostic_action_generation) ==
          NldDirectPdoDiagnosticActionCancelAndClose);
    CHECK(NldDirectPdoDiagnosticCompleteAction(
        &diagnostic,
        diagnostic_action_generation,
        NldDirectPdoDiagnosticActionCancelAndClose,
        1));
    CHECK(NldDirectPdoDiagnosticTakeAction(&diagnostic, 0) ==
          NldDirectPdoDiagnosticActionNone);
    CHECK(NldDirectPdoArbiterRelease(
        &arbiter,
        NldDirectPdoArbiterClientDiagnostic,
        diagnostic_generation));
    CHECK(NldDirectPdoPreemptionCompleteAction(
        &preemption,
        preemption_generation,
        NldDirectPdoPreemptionActionCancelDiagnostic,
        1));
    CHECK(NldDirectPdoPreemptionTakeAction(
        &preemption,
        &preemption_generation) ==
          NldDirectPdoPreemptionActionRetryRender);
    CHECK(NldDirectPdoArbiterTryAcquire(
        &arbiter,
        NldDirectPdoArbiterClientRender,
        &render_generation) == NldDirectPdoArbiterAcquireGranted);
    CHECK(NldDirectPdoPreemptionCompleteAction(
        &preemption,
        preemption_generation,
        NldDirectPdoPreemptionActionRetryRender,
        1));
    CHECK(preemption.State == NldDirectPdoPreemptionComplete);
    CHECK(arbiter.Client == NldDirectPdoArbiterClientRender);
    CHECK(NldDirectPdoArbiterTryAcquire(
        &arbiter,
        NldDirectPdoArbiterClientDiagnostic,
        &diagnostic_generation) ==
          NldDirectPdoArbiterAcquireRejected);
    CHECK(NldDirectPdoArbiterRelease(
        &arbiter,
        NldDirectPdoArbiterClientRender,
        render_generation));
}

int main(void) {
    test_cancel_then_retry_render();
    test_cancel_timeout_faults_without_retry();
    test_retry_failure_is_faulted();
    test_stale_completion_is_rejected();
    test_pnp_stop_cancels_pending_or_waits_active();
    test_full_contract_handoff_preserves_render_priority();
    puts("Direct-PDO preemption contract tests passed");
    return 0;
}
