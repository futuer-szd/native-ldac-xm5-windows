#include "../v1_render_demand_tracker.h"

#include <cstdio>

namespace {

using native_ldac::agent::ObserveV1RenderDemand;
using native_ldac::agent::ResetV1RenderDemandTracker;
using native_ldac::agent::V1RenderDemandTracker;
using native_ldac::agent::V1RenderDemandTransition;

int Fail(const char* message) {
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

}  // namespace

int main() {
    V1RenderDemandTracker tracker;
    if (ObserveV1RenderDemand(&tracker, false) !=
        V1RenderDemandTransition::None) {
        return Fail("Idle baseline created a transition.");
    }
    if (ObserveV1RenderDemand(&tracker, true) !=
            V1RenderDemandTransition::None ||
        ObserveV1RenderDemand(&tracker, false) !=
            V1RenderDemandTransition::None) {
        return Fail("One active sample was not rejected as a transient.");
    }
    if (ObserveV1RenderDemand(&tracker, true) !=
            V1RenderDemandTransition::None ||
        ObserveV1RenderDemand(&tracker, true) !=
            V1RenderDemandTransition::Started ||
        !tracker.stable_running) {
        return Fail("Two active samples did not confirm render start.");
    }
    if (ObserveV1RenderDemand(&tracker, true) !=
        V1RenderDemandTransition::None) {
        return Fail("Stable running state repeated its start transition.");
    }
    if (ObserveV1RenderDemand(&tracker, false) !=
            V1RenderDemandTransition::None ||
        ObserveV1RenderDemand(&tracker, false) !=
            V1RenderDemandTransition::Stopped ||
        tracker.stable_running) {
        return Fail("Two idle samples did not confirm render stop.");
    }
    ResetV1RenderDemandTracker(&tracker);
    if (tracker.stable_running || tracker.candidate_samples != 0u) {
        return Fail("Tracker reset did not restore idle baseline.");
    }
    if (ObserveV1RenderDemand(nullptr, true) !=
        V1RenderDemandTransition::None) {
        return Fail("Null tracker was not safely ignored.");
    }
    std::printf("V1 render-demand tracker tests passed.\n");
    return 0;
}
