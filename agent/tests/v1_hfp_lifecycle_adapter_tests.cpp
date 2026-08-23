// SPDX-License-Identifier: Apache-2.0
#include "../v1_hfp_lifecycle_adapter.h"

#include <cstdio>

namespace {
using namespace native_ldac::agent;

int failures = 0;

void Expect(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "v1_hfp_lifecycle_adapter_tests: %s\n", message);
}

V1HfpSwitchDecision Decision(std::uint64_t generation,
                             std::uint32_t actions) {
    V1HfpSwitchDecision decision;
    decision.acl_generation = generation;
    decision.actions = actions;
    return decision;
}

void TestDefaultIsShadowOnly() {
    auto plan = PlanV1HfpLifecycle(
        Decision(1u, V1HfpActionStopLdac), 1u, false);
    Expect(plan.shadow_only &&
               plan.proposed_command ==
                   V1HfpLifecycleCommand::SuspendLdac &&
               plan.command == V1HfpLifecycleCommand::None &&
               !plan.invalid && !plan.stale,
           "disabled transport switch executed an HFP command");
}

void TestOptInMapsStopAndResume() {
    auto plan = PlanV1HfpLifecycle(
        Decision(2u, V1HfpActionStopLdac), 2u, true);
    Expect(!plan.shadow_only &&
               plan.proposed_command ==
                   V1HfpLifecycleCommand::SuspendLdac &&
               plan.command == V1HfpLifecycleCommand::SuspendLdac &&
               V1HfpLifecycleEventForCommand(plan.command) ==
                   V1LifecycleEvent::HfpSuspendLdac,
           "opt-in stop did not map to HFP suspend");
    plan = PlanV1HfpLifecycle(
        Decision(2u, V1HfpActionResumeLdac), 2u, true);
    Expect(plan.command == V1HfpLifecycleCommand::ResumeLdac &&
               V1HfpLifecycleEventForCommand(plan.command) ==
                   V1LifecycleEvent::HfpResumeLdac,
           "opt-in resume did not map to HFP resume");
}

void TestGenerationAndConflictFailClosed() {
    auto plan = PlanV1HfpLifecycle(
        Decision(3u, V1HfpActionStopLdac), 4u, true);
    Expect(plan.stale && plan.command == V1HfpLifecycleCommand::None,
           "old generation executed an HFP lifecycle command");
    plan = PlanV1HfpLifecycle(
        Decision(4u, V1HfpActionStopLdac | V1HfpActionResumeLdac),
        4u,
        true);
    Expect(plan.invalid && plan.command == V1HfpLifecycleCommand::None,
           "conflicting stop/resume command was accepted");
}

void TestOutputRequestsRemainSeparate() {
    auto plan = PlanV1HfpLifecycle(
        Decision(5u, V1HfpActionEnterHfpOutput), 5u, true);
    Expect(plan.enter_hfp_output_requested &&
               plan.command == V1HfpLifecycleCommand::None,
           "HFP output request was incorrectly mapped to transport lifecycle");
    plan = PlanV1HfpLifecycle(
        Decision(5u, V1HfpActionExitHfpOutput), 5u, true);
    Expect(plan.exit_hfp_output_requested &&
               plan.command == V1HfpLifecycleCommand::None,
           "HFP output release was incorrectly mapped to transport lifecycle");
}

}  // namespace

int main() {
    TestDefaultIsShadowOnly();
    TestOptInMapsStopAndResume();
    TestGenerationAndConflictFailClosed();
    TestOutputRequestsRemainSeparate();
    if (failures == 0) {
        std::puts("V1 HFP lifecycle adapter tests passed.");
    }
    return failures == 0 ? 0 : 1;
}
