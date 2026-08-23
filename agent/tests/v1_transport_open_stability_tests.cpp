#include "v1_transport_open_stability.h"

#include <cstdio>

namespace {

using native_ldac::agent::ArmV1TransportOpenStability;
using native_ldac::agent::ObserveV1TransportOpenStability;
using native_ldac::agent::V1TransportOpenStabilityGate;
using native_ldac::agent::V1TransportOpenStabilityObservation;

bool Expect(V1TransportOpenStabilityObservation actual,
            V1TransportOpenStabilityObservation expected,
            const char* label) {
    if (actual == expected) {
        return true;
    }
    std::fprintf(stderr, "Unexpected stability result: %s\n", label);
    return false;
}

}  // namespace

int main() {
    V1TransportOpenStabilityGate short_run;
    ArmV1TransportOpenStability(&short_run, 1000u, 1u, true, 10u, 100u);
    if (!Expect(ObserveV1TransportOpenStability(
                    &short_run, 1u, true, 10u, 1099u),
                V1TransportOpenStabilityObservation::None,
                "short RUN stays pending") ||
        !Expect(ObserveV1TransportOpenStability(
                    &short_run, 1u, false, 10u, 1100u),
                V1TransportOpenStabilityObservation::Reset,
                "short RUN STOP resets") ||
        !short_run.pending) {
        return 1;
    }

    V1TransportOpenStabilityGate resumed;
    ArmV1TransportOpenStability(&resumed, 1000u, 2u, true, 20u, 0u);
    if (!Expect(ObserveV1TransportOpenStability(
                    &resumed, 2u, false, 20u, 750u),
                V1TransportOpenStabilityObservation::Reset,
                "STOP resets timer") ||
        !Expect(ObserveV1TransportOpenStability(
                    &resumed, 2u, true, 21u, 900u),
                V1TransportOpenStabilityObservation::None,
                "new epoch starts timer") ||
        !Expect(ObserveV1TransportOpenStability(
                    &resumed, 2u, true, 21u, 1899u),
                V1TransportOpenStabilityObservation::None,
                "new epoch remains bounded") ||
        !Expect(ObserveV1TransportOpenStability(
                    &resumed, 2u, true, 21u, 1900u),
                V1TransportOpenStabilityObservation::Ready,
                "stable resumed epoch authorizes")) {
        return 2;
    }

    V1TransportOpenStabilityGate stable;
    ArmV1TransportOpenStability(&stable, 1000u, 3u, true, 30u, 500u);
    if (!Expect(ObserveV1TransportOpenStability(
                    &stable, 3u, true, 30u, 1500u),
                V1TransportOpenStabilityObservation::Ready,
                "stable RUN authorizes") ||
        !Expect(ObserveV1TransportOpenStability(
                    &stable, 3u, true, 30u, 2500u),
                V1TransportOpenStabilityObservation::None,
                "authorization occurs exactly once")) {
        return 3;
    }

    V1TransportOpenStabilityGate changed_epoch;
    ArmV1TransportOpenStability(
        &changed_epoch, 1000u, 4u, true, 40u, 1000u);
    if (!Expect(ObserveV1TransportOpenStability(
                    &changed_epoch, 4u, true, 41u, 1500u),
                V1TransportOpenStabilityObservation::Reset,
                "epoch change resets timer") ||
        !Expect(ObserveV1TransportOpenStability(
                    &changed_epoch, 4u, true, 41u, 2499u),
                V1TransportOpenStabilityObservation::None,
                "changed epoch waits full interval") ||
        !Expect(ObserveV1TransportOpenStability(
                    &changed_epoch, 4u, true, 41u, 2500u),
                V1TransportOpenStabilityObservation::Ready,
                "changed epoch eventually authorizes")) {
        return 4;
    }

    V1TransportOpenStabilityGate stale_generation;
    ArmV1TransportOpenStability(
        &stale_generation, 1000u, 5u, true, 50u, 2000u);
    if (!Expect(ObserveV1TransportOpenStability(
                    &stale_generation, 6u, true, 50u, 3000u),
                V1TransportOpenStabilityObservation::Cancelled,
                "ACL generation change cancels") ||
        stale_generation.pending) {
        return 5;
    }

    std::puts("V1 transport OPEN stability tests passed.");
    return 0;
}
