// SPDX-License-Identifier: Apache-2.0
#include "../v1_avrcp_bootstrap_play.h"

#include <cstdio>

namespace {

using namespace native_ldac::agent;

int failures = 0;

void Check(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "v1_avrcp_bootstrap_play_tests: %s\n", message);
}

V1MediaSessionSnapshot Paused(std::uint64_t generation) {
    return {generation, V1MediaSessionPlayback::Paused,
            true, false, true, true};
}

void MicrosoftWinsWithoutFallback() {
    V1AvrcpBootstrapPlayState state{};
    ResetV1AvrcpBootstrapPlay(&state, 1u);
    Check(ObserveV1AvrcpBootstrapPlayGesture(
              &state, Paused(1u), true, 1000u) ==
              V1AvrcpBootstrapPlayDecision::Scheduled,
          "paused PLAY was not scheduled");
    Check(ReconcileV1AvrcpBootstrapPlay(
              &state,
              {1u, V1MediaSessionPlayback::Playing,
               false, true, true, true},
              1050u) == V1AvrcpBootstrapPlayDecision::MicrosoftHandled,
          "Microsoft playback transition was not preferred");
    Check(!state.pending && state.microsoft_handled_count == 1u &&
              state.fallback_injected_count == 0u,
          "Microsoft handling left a duplicate fallback");
}

void MissingMicrosoftGetsOneDelayedFallback() {
    V1AvrcpBootstrapPlayState state{};
    ResetV1AvrcpBootstrapPlay(&state, 2u);
    Check(ObserveV1AvrcpBootstrapPlayGesture(
              &state, Paused(2u), true, 2000u) ==
              V1AvrcpBootstrapPlayDecision::Scheduled,
          "bootstrap fallback was not scheduled");
    Check(ObserveV1AvrcpBootstrapPlayGesture(
              &state, Paused(2u), true, 2050u) ==
              V1AvrcpBootstrapPlayDecision::None,
          "duplicate press extended the pending fallback");
    Check(ReconcileV1AvrcpBootstrapPlay(
              &state, Paused(2u), 2199u) ==
              V1AvrcpBootstrapPlayDecision::None,
          "fallback fired before the arbitration deadline");
    Check(ReconcileV1AvrcpBootstrapPlay(
              &state, Paused(2u), 2200u) ==
              V1AvrcpBootstrapPlayDecision::InjectPlay,
          "fallback did not fire at the deadline");
    Check(ReconcileV1AvrcpBootstrapPlay(
              &state, Paused(2u), 2400u) ==
              V1AvrcpBootstrapPlayDecision::None,
          "fallback fired more than once");
}

void ConnectionStatesRemainFailClosed() {
    V1AvrcpBootstrapPlayState state{};
    ResetV1AvrcpBootstrapPlay(&state, 3u);
    Check(ObserveV1AvrcpBootstrapPlayGesture(
              &state,
              {3u, V1MediaSessionPlayback::Playing,
               false, true, true, true},
              true, 0u) == V1AvrcpBootstrapPlayDecision::None,
          "Playing connection scheduled a duplicate PLAY");
    Check(ObserveV1AvrcpBootstrapPlayGesture(
              &state,
              {3u, V1MediaSessionPlayback::Stopped,
               false, false, false, false},
              true, 0u) == V1AvrcpBootstrapPlayDecision::None,
          "Stopped connection scheduled PLAY");
    Check(ObserveV1AvrcpBootstrapPlayGesture(
              &state,
              {3u, V1MediaSessionPlayback::Absent,
               false, false, false, false},
              true, 0u) == V1AvrcpBootstrapPlayDecision::None,
          "Absent connection scheduled PLAY");
    Check(ObserveV1AvrcpBootstrapPlayGesture(
              &state, Paused(3u), false, 0u) ==
              V1AvrcpBootstrapPlayDecision::None,
          "non-PLAY gesture scheduled bootstrap");

    Check(ObserveV1AvrcpBootstrapPlayGesture(
              &state, Paused(3u), true, 100u) ==
              V1AvrcpBootstrapPlayDecision::Scheduled,
          "paused connection could not schedule PLAY");
    Check(ReconcileV1AvrcpBootstrapPlay(
              &state,
              {3u, V1MediaSessionPlayback::Stopped,
               false, false, false, false},
              150u) == V1AvrcpBootstrapPlayDecision::Cancelled,
          "Stopped transition retained a pending PLAY");
}

void ReconnectDropsOldPendingState() {
    V1AvrcpBootstrapPlayState state{};
    ResetV1AvrcpBootstrapPlay(&state, 4u);
    (void)ObserveV1AvrcpBootstrapPlayGesture(
        &state, Paused(4u), true, 100u);
    ResetV1AvrcpBootstrapPlay(&state, 5u);
    Check(!state.pending && state.acl_generation == 5u,
          "new ACL generation retained old PLAY");
    Check(ObserveV1AvrcpBootstrapPlayGesture(
              &state, Paused(5u), true, 200u) ==
              V1AvrcpBootstrapPlayDecision::Scheduled,
          "new ACL generation could not schedule PLAY");
}

}  // namespace

int main() {
    MicrosoftWinsWithoutFallback();
    MissingMicrosoftGetsOneDelayedFallback();
    ConnectionStatesRemainFailClosed();
    ReconnectDropsOldPendingState();
    return failures == 0 ? 0 : 1;
}
