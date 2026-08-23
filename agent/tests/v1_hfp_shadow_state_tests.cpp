// SPDX-License-Identifier: Apache-2.0
#include "../v1_hfp_shadow_state.h"

#include <cstdio>

namespace {
using namespace native_ldac::agent;

int failures = 0;

void Expect(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "v1_hfp_shadow_state_tests: %s\n", message);
}

V1HfpShadowInput Input(std::uint64_t generation,
                       std::uint64_t now,
                       std::uint64_t sequence,
                       bool capture_active,
                       bool ldac_active,
                       bool render_demand = true) {
    V1HfpShadowInput input;
    input.acl_generation = generation;
    input.now_ms = now;
    input.monitor_ready = true;
    input.physical_connected = true;
    input.ldac_path_active = ldac_active;
    input.render_demand = render_demand;
    input.capture.sequence = sequence;
    input.capture.endpoint_matched = true;
    input.capture.capture_active = capture_active;
    input.capture.active_session_count = capture_active ? 1u : 0u;
    return input;
}

void TestCompleteShadowSwitchSequence() {
    V1HfpShadowState state;
    auto decision = state.Step(Input(1u, 0u, 1u, false, true));
    Expect(decision.snapshot_changed && !decision.capture_active,
           "initial idle snapshot was not published");

    decision = state.Step(Input(1u, 10u, 2u, true, true));
    Expect(decision.capture_active &&
               HasV1HfpSwitchAction(
                   decision.switch_decision, V1HfpActionStopLdac),
           "matched active capture did not request LDAC stop");

    decision = state.Step(Input(1u, 20u, 2u, true, false));
    Expect(!decision.snapshot_changed &&
               HasV1HfpSwitchAction(
                   decision.switch_decision, V1HfpActionEnterHfpOutput),
           "transport release did not enter HFP shadow output");

    decision = state.Step(Input(1u, 100u, 3u, false, false));
    Expect(decision.switch_decision.phase ==
               V1HfpSwitchPhase::ReleaseDelay,
           "capture stop did not enter the release delay");
    decision = state.Step(Input(1u, 2100u, 3u, false, false));
    Expect(HasV1HfpSwitchAction(
               decision.switch_decision, V1HfpActionExitHfpOutput) &&
               HasV1HfpSwitchAction(
                   decision.switch_decision, V1HfpActionResumeLdac),
           "stable HFP release did not request demanded LDAC resume");
}

void TestIdentityAndMonitorFailureFailClosed() {
    V1HfpShadowState state;
    auto untrusted = Input(2u, 0u, 1u, true, true);
    untrusted.capture.endpoint_matched = false;
    auto decision = state.Step(untrusted);
    Expect(!decision.capture_active &&
               decision.switch_decision.actions == V1HfpActionNone,
           "untrusted endpoint triggered an HFP switch");

    auto unavailable = Input(2u, 10u, 2u, true, true);
    unavailable.monitor_ready = false;
    decision = state.Step(unavailable);
    Expect(!decision.monitor_ready && !decision.capture_active &&
               decision.switch_decision.actions == V1HfpActionNone,
           "unavailable monitor triggered an HFP switch");
}

void TestGenerationIsolationFlowsThroughCoordinator() {
    V1HfpShadowState state;
    (void)state.Step(Input(10u, 0u, 1u, true, false));
    auto decision = state.Step(Input(9u, 10u, 2u, true, false));
    Expect(decision.switch_decision.stale &&
               decision.switch_decision.actions == V1HfpActionNone,
           "coordinator accepted an older ACL generation");
    decision = state.Step(Input(11u, 20u, 3u, true, false));
    Expect(!decision.switch_decision.stale &&
               HasV1HfpSwitchAction(
                   decision.switch_decision, V1HfpActionEnterHfpOutput),
           "coordinator did not accept a new ACL generation");
}

void TestEnginePreparationCountsAsLdacPath() {
    V1HfpShadowState state;
    auto decision = state.Step(Input(30u, 0u, 1u, false, true));
    Expect(decision.switch_decision.phase == V1HfpSwitchPhase::LdacAvailable,
           "baseline did not establish a connected generation");
    auto capture = Input(30u, 10u, 2u, true, true);
    decision = state.Step(capture);
    Expect(HasV1HfpSwitchAction(
               decision.switch_decision, V1HfpActionStopLdac),
           "engine preparation was not treated as an active LDAC path");
}

}  // namespace

int main() {
    TestCompleteShadowSwitchSequence();
    TestIdentityAndMonitorFailureFailClosed();
    TestGenerationIsolationFlowsThroughCoordinator();
    TestEnginePreparationCountsAsLdacPath();
    if (failures == 0) {
        std::puts("V1 HFP shadow state tests passed.");
    }
    return failures == 0 ? 0 : 1;
}
