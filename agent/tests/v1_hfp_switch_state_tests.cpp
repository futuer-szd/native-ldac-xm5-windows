// SPDX-License-Identifier: Apache-2.0
#include "../v1_hfp_switch_state.h"

#include <cstdio>
#include <limits>

namespace {
using namespace native_ldac::agent;

int failures = 0;

void Expect(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "v1_hfp_switch_state_tests: %s\n", message);
}

V1HfpSwitchInput Input(std::uint64_t generation,
                       std::uint64_t now,
                       bool capture,
                       bool ldac,
                       bool render = true) {
    return {generation, now, true, capture, ldac, render};
}

void TestActiveLdacStopsBeforeHfp() {
    V1HfpSwitchState state;
    (void)state.Step(Input(1u, 0u, false, true));
    auto decision = state.Step(Input(1u, 10u, true, true));
    Expect(HasV1HfpSwitchAction(decision, V1HfpActionStopLdac) &&
               !HasV1HfpSwitchAction(decision, V1HfpActionEnterHfpOutput) &&
               decision.phase == V1HfpSwitchPhase::WaitingForLdacStop,
           "capture must stop LDAC before entering HFP");
    decision = state.Step(Input(1u, 20u, true, true));
    Expect(decision.actions == V1HfpActionNone,
           "LDAC stop request must not repeat");
    decision = state.Step(Input(1u, 30u, true, false));
    Expect(HasV1HfpSwitchAction(decision, V1HfpActionEnterHfpOutput) &&
               decision.phase == V1HfpSwitchPhase::HfpActive,
           "HFP must enter after LDAC releases the transport");
}

void TestReleaseDelayAndConditionalResume() {
    V1HfpSwitchState state;
    (void)state.Step(Input(2u, 0u, true, false));
    auto decision = state.Step(Input(2u, 100u, false, false));
    Expect(decision.phase == V1HfpSwitchPhase::ReleaseDelay &&
               decision.release_deadline_ms == 2100u &&
               decision.actions == V1HfpActionNone,
           "capture stop must begin a two-second stability delay");
    decision = state.Step(Input(2u, 2099u, false, false));
    Expect(decision.actions == V1HfpActionNone,
           "HFP released before the stability deadline");
    decision = state.Step(Input(2u, 2100u, false, false));
    Expect(HasV1HfpSwitchAction(decision, V1HfpActionExitHfpOutput) &&
               HasV1HfpSwitchAction(decision, V1HfpActionResumeLdac) &&
               decision.phase == V1HfpSwitchPhase::LdacAvailable,
           "stable capture stop must exit HFP and resume demanded LDAC");

    V1HfpSwitchState idle;
    (void)idle.Step(Input(3u, 0u, true, false, false));
    (void)idle.Step(Input(3u, 10u, false, false, false));
    decision = idle.Step(Input(3u, 2010u, false, false, false));
    Expect(HasV1HfpSwitchAction(decision, V1HfpActionExitHfpOutput) &&
               !HasV1HfpSwitchAction(decision, V1HfpActionResumeLdac),
           "idle render must not resume LDAC after HFP");
}

void TestCaptureFlapCancelsRelease() {
    V1HfpSwitchState state;
    (void)state.Step(Input(4u, 0u, true, false));
    (void)state.Step(Input(4u, 100u, false, false));
    auto decision = state.Step(Input(4u, 1000u, true, false));
    Expect(decision.phase == V1HfpSwitchPhase::HfpActive &&
               decision.actions == V1HfpActionNone &&
               decision.release_deadline_ms == 0u,
           "capture flap must cancel release without cycling HFP");
}

void TestGenerationIsolationAndDisconnect() {
    V1HfpSwitchState state;
    (void)state.Step(Input(10u, 0u, true, false));
    auto disconnected = Input(10u, 5u, false, false);
    disconnected.physical_connected = false;
    auto decision = state.Step(disconnected);
    Expect(HasV1HfpSwitchAction(decision, V1HfpActionExitHfpOutput) &&
               decision.phase == V1HfpSwitchPhase::Disconnected,
           "ACL disconnect must release HFP immediately");
    decision = state.Step(Input(9u, 10u, true, false));
    Expect(decision.stale && decision.actions == V1HfpActionNone,
           "older ACL generation was not rejected");
    decision = state.Step(Input(11u, 20u, true, false));
    Expect(!decision.stale &&
               HasV1HfpSwitchAction(decision, V1HfpActionEnterHfpOutput) &&
               decision.acl_generation == 11u,
           "new ACL generation did not start a fresh HFP session");

    V1HfpSwitchState wrapped;
    (void)wrapped.Step(Input(std::numeric_limits<std::uint64_t>::max(),
                            0u, false, false));
    decision = wrapped.Step(Input(1u, 1u, true, false));
    Expect(!decision.stale && decision.acl_generation == 1u,
           "wrapped ACL generation was not accepted");
}

void TestCaptureEndsBeforeLdacStops() {
    V1HfpSwitchState state;
    (void)state.Step(Input(20u, 0u, false, true));
    (void)state.Step(Input(20u, 10u, true, true));
    auto decision = state.Step(Input(20u, 20u, false, true));
    Expect(decision.phase == V1HfpSwitchPhase::WaitingForLdacStop &&
               decision.actions == V1HfpActionNone,
           "short capture must still wait for bounded LDAC stop");
    decision = state.Step(Input(20u, 30u, false, false));
    Expect(decision.phase == V1HfpSwitchPhase::ReleaseDelay &&
               decision.release_deadline_ms == 2030u,
           "post-stop quiet period was not armed");
    decision = state.Step(Input(20u, 2030u, false, false));
    Expect(!HasV1HfpSwitchAction(decision, V1HfpActionExitHfpOutput) &&
               HasV1HfpSwitchAction(decision, V1HfpActionResumeLdac),
           "never-entered HFP must not emit an exit action");
}

}  // namespace

int main() {
    TestActiveLdacStopsBeforeHfp();
    TestReleaseDelayAndConditionalResume();
    TestCaptureFlapCancelsRelease();
    TestGenerationIsolationAndDisconnect();
    TestCaptureEndsBeforeLdacStops();
    if (failures == 0) {
        std::puts("V1 HFP switch state tests passed.");
    }
    return failures == 0 ? 0 : 1;
}
