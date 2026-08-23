// SPDX-License-Identifier: Apache-2.0
#include "../v1_media_session_eligibility.h"

#include <cstdio>

namespace {
using namespace native_ldac::agent;
int failures = 0;

void Expect(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "v1_media_session_eligibility_tests: %s\n", message);
}

void TestAbsentAndStoppedFailClosed() {
    const V1MediaSessionSnapshot absent{1u,
        V1MediaSessionPlayback::Absent, true, true, true, true};
    const auto absent_result = EvaluateV1MediaSessionEligibility(absent);
    Expect(!absent_result.session_present &&
               !absent_result.play_eligible &&
               !absent_result.pause_eligible &&
               !absent_result.next_eligible &&
               !absent_result.previous_eligible,
           "absent session was eligible");
    const V1MediaSessionSnapshot stopped{1u,
        V1MediaSessionPlayback::Stopped, true, true, true, true};
    const auto stopped_result = EvaluateV1MediaSessionEligibility(stopped);
    Expect(!stopped_result.session_present,
           "stopped session was eligible");
}

void TestPlayingAndPausedCapabilities() {
    const V1MediaSessionSnapshot playing{2u,
        V1MediaSessionPlayback::Playing, false, true, true, false};
    const auto playing_result = EvaluateV1MediaSessionEligibility(playing);
    Expect(playing_result.session_present &&
               !playing_result.play_eligible &&
               playing_result.pause_eligible &&
               playing_result.next_eligible &&
               !playing_result.previous_eligible,
           "playing session capabilities were not projected");

    const V1MediaSessionSnapshot paused{2u,
        V1MediaSessionPlayback::Paused, true, false, false, true};
    const auto paused_result = EvaluateV1MediaSessionEligibility(paused);
    Expect(paused_result.session_present &&
               paused_result.play_eligible &&
               !paused_result.pause_eligible &&
               !paused_result.next_eligible &&
               paused_result.previous_eligible,
           "paused session capabilities were not projected");

    const V1MediaSessionSnapshot no_pause{2u,
        V1MediaSessionPlayback::Playing, false, false, true, true};
    const auto no_pause_result = EvaluateV1MediaSessionEligibility(no_pause);
    Expect(no_pause_result.session_present &&
               !no_pause_result.play_eligible &&
               !no_pause_result.pause_eligible &&
               no_pause_result.next_eligible &&
               no_pause_result.previous_eligible,
           "disabled pause control was treated as usable");
}

void TestGenerationAndZeroGuards() {
    const V1MediaSessionSnapshot zero{0u,
        V1MediaSessionPlayback::Playing, true, true, true, true};
    const auto result = EvaluateV1MediaSessionEligibility(zero);
    Expect(!result.session_present && result.acl_generation == 0u,
           "zero generation was not fail-closed");
}

void TestChangingPlaybackStability() {
    Expect(NormalizeV1MediaSessionPlayback(
               V1MediaSessionObservedPlayback::Changing,
               V1MediaSessionPlayback::Playing,
               true) == V1MediaSessionPlayback::Playing,
           "Changing tore down a playing session");
    Expect(NormalizeV1MediaSessionPlayback(
               V1MediaSessionObservedPlayback::Changing,
               V1MediaSessionPlayback::Paused,
               true) == V1MediaSessionPlayback::Paused,
           "Changing tore down a paused session");
    Expect(NormalizeV1MediaSessionPlayback(
               V1MediaSessionObservedPlayback::Changing,
               V1MediaSessionPlayback::Playing,
               false) == V1MediaSessionPlayback::Stopped,
           "Changing from a new session was not fail-closed");
    Expect(NormalizeV1MediaSessionPlayback(
               V1MediaSessionObservedPlayback::Closed,
               V1MediaSessionPlayback::Playing,
               true) == V1MediaSessionPlayback::Stopped,
           "Closed session was not stopped");
}
}

int main() {
    TestAbsentAndStoppedFailClosed();
    TestPlayingAndPausedCapabilities();
    TestGenerationAndZeroGuards();
    TestChangingPlaybackStability();
    if (failures == 0) {
        std::puts("V1 media session eligibility tests passed.");
    }
    return failures == 0 ? 0 : 1;
}
