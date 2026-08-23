// SPDX-License-Identifier: Apache-2.0
#include "../avrcp_passthrough_state.h"

#include <array>
#include <cstdio>
#include <limits>

namespace {

using namespace native_ldac::agent;

int failures = 0;

void Expect(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "avrcp_passthrough_state_tests: %s\n", message);
}

AvrcpPassThroughEvent Parse(std::uint8_t operation,
                            bool released = false) {
    const std::array<std::uint8_t, 5u> frame = {
        0x00u, 0x48u, 0x7Cu,
        static_cast<std::uint8_t>(operation | (released ? 0x80u : 0u)),
        0x00u,
    };
    AvrcpPassThroughEvent event{};
    Expect(ParseAvrcpPassThroughCommand(
               frame.data(), frame.size(), &event),
           "known frame did not parse");
    return event;
}

void TestFrameParsing() {
    struct Case {
        std::uint8_t operation_id;
        AvrcpPassThroughOperation operation;
    };
    const Case cases[] = {
        {0x41u, AvrcpPassThroughOperation::VolumeUp},
        {0x42u, AvrcpPassThroughOperation::VolumeDown},
        {0x43u, AvrcpPassThroughOperation::Mute},
        {0x44u, AvrcpPassThroughOperation::Play},
        {0x45u, AvrcpPassThroughOperation::Stop},
        {0x46u, AvrcpPassThroughOperation::Pause},
        {0x4Bu, AvrcpPassThroughOperation::Forward},
        {0x4Cu, AvrcpPassThroughOperation::Backward},
    };
    for (const auto& value : cases) {
        const auto pressed = Parse(value.operation_id);
        Expect(pressed.operation == value.operation &&
                   pressed.key_state ==
                       AvrcpPassThroughKeyState::Pressed &&
                   pressed.operation_id == value.operation_id,
               "known press mapping changed");
        const auto released = Parse(value.operation_id, true);
        Expect(released.operation == value.operation &&
                   released.key_state ==
                       AvrcpPassThroughKeyState::Released,
               "known release mapping changed");
    }

    auto unknown = Parse(0x7Eu);
    Expect(unknown.operation == AvrcpPassThroughOperation::Unknown,
           "unknown operation did not remain observable and inert");

    AvrcpPassThroughEvent event{};
    const std::array<std::array<std::uint8_t, 5u>, 4u> invalid = {{
        {{0x09u, 0x48u, 0x7Cu, 0x44u, 0x00u}},
        {{0x00u, 0x00u, 0x7Cu, 0x44u, 0x00u}},
        {{0x00u, 0x48u, 0x30u, 0x44u, 0x00u}},
        {{0x00u, 0x48u, 0x7Cu, 0x44u, 0x01u}},
    }};
    for (const auto& frame : invalid) {
        Expect(!ParseAvrcpPassThroughCommand(
                   frame.data(), frame.size(), &event),
               "response, wrong target, or malformed length was accepted");
    }
    Expect(!ParseAvrcpPassThroughCommand(nullptr, 0u, &event) &&
               !ParseAvrcpPassThroughCommand(
                   invalid[0].data(), invalid[0].size(), nullptr),
           "null frame or event was accepted");
}

void TestFailClosedGateAndRouting() {
    AvrcpPassThroughGateState state{};
    auto play = Parse(0x44u);
    auto decision = ObserveAvrcpPassThroughEvent(&state, 1u, play);
    Expect(!decision.event_observed && decision.actions == 0u,
           "event escaped without an ACL generation");

    decision = BeginAvrcpPassThroughAclGeneration(&state, 1u);
    Expect(state.acl_generation_current &&
               state.mode == AvrcpPassThroughGateMode::ObserveOnly &&
               state.owner_lease == 0u && decision.actions == 0u,
           "fresh generation was not observe-only");
    decision = ObserveAvrcpPassThroughEvent(&state, 1u, play);
    Expect(decision.event_observed && decision.actions == 0u &&
               state.held_operation_valid,
           "observe-only press was not consumed safely");
    decision = ObserveAvrcpPassThroughEvent(
        &state, 1u, Parse(0x44u, true));
    Expect(decision.matching_release && !state.held_operation_valid,
           "matching observe-only release did not clear held state");

    (void)SetAvrcpPassThroughGateMode(
        &state, 1u,
        AvrcpPassThroughGateMode::RouteToCurrentMediaSession);
    decision = ObserveAvrcpPassThroughEvent(&state, 1u, play);
    Expect(decision.actions == 0u,
           "route mode without owner lease emitted an action");
    (void)ObserveAvrcpPassThroughEvent(
        &state, 1u, Parse(0x44u, true));
    (void)AcquireAvrcpPassThroughOwnerLease(&state, 1u, 77u);

    decision = ObserveAvrcpPassThroughEvent(&state, 1u, play);
    Expect(HasAvrcpPassThroughAction(
               decision, AvrcpPassThroughActionPlay) &&
               IsAvrcpPassThroughDecisionCurrent(state, decision),
           "authorized play press was not routed");
    const auto authorized = decision;
    decision = ObserveAvrcpPassThroughEvent(&state, 1u, play);
    Expect(decision.actions == 0u && decision.duplicate_press,
           "duplicate press repeated a media action");
    decision = ObserveAvrcpPassThroughEvent(
        &state, 1u, Parse(0x44u, true));
    Expect(decision.actions == 0u && decision.matching_release,
           "release emitted a media action");

    decision = ObserveAvrcpPassThroughEvent(
        &state, 1u, Parse(0x4Bu));
    Expect(HasAvrcpPassThroughAction(
               decision, AvrcpPassThroughActionNextTrack),
           "forward was not mapped to next track");
    (void)ObserveAvrcpPassThroughEvent(
        &state, 1u, Parse(0x4Bu, true));
    decision = ObserveAvrcpPassThroughEvent(
        &state, 1u, Parse(0x4Cu));
    Expect(HasAvrcpPassThroughAction(
               decision, AvrcpPassThroughActionPreviousTrack),
           "backward was not mapped to previous track");
    (void)ObserveAvrcpPassThroughEvent(
        &state, 1u, Parse(0x4Cu, true));

    decision = RevokeAvrcpPassThroughOwnerLease(&state, 1u, 77u);
    Expect(state.owner_lease == 0u &&
               !IsAvrcpPassThroughDecisionCurrent(state, authorized),
           "lease revocation did not invalidate queued action");
    decision = ObserveAvrcpPassThroughEvent(
        &state, 1u, Parse(0x46u));
    Expect(decision.actions == 0u,
           "pause escaped after lease revocation");
}

void TestAllActionsAndGenerationGuards() {
    struct Case {
        std::uint8_t operation_id;
        AvrcpPassThroughAction action;
    };
    const Case cases[] = {
        {0x41u, AvrcpPassThroughActionStepVolumeUp},
        {0x42u, AvrcpPassThroughActionStepVolumeDown},
        {0x43u, AvrcpPassThroughActionToggleMute},
        {0x44u, AvrcpPassThroughActionPlay},
        {0x45u, AvrcpPassThroughActionStop},
        {0x46u, AvrcpPassThroughActionPause},
        {0x4Bu, AvrcpPassThroughActionNextTrack},
        {0x4Cu, AvrcpPassThroughActionPreviousTrack},
    };
    AvrcpPassThroughGateState state{};
    (void)BeginAvrcpPassThroughAclGeneration(&state, 9u);
    (void)AcquireAvrcpPassThroughOwnerLease(&state, 9u, 900u);
    (void)SetAvrcpPassThroughGateMode(
        &state, 9u,
        AvrcpPassThroughGateMode::RouteToCurrentMediaSession);
    for (const auto& value : cases) {
        auto decision = ObserveAvrcpPassThroughEvent(
            &state, 9u, Parse(value.operation_id));
        Expect(HasAvrcpPassThroughAction(decision, value.action),
               "known operation did not produce its action");
        (void)ObserveAvrcpPassThroughEvent(
            &state, 9u, Parse(value.operation_id, true));
    }
    const std::uint64_t sequence = state.accepted_event_sequence;
    auto decision = ObserveAvrcpPassThroughEvent(
        &state, 8u, Parse(0x44u));
    Expect(decision.actions == 0u &&
               state.accepted_event_sequence == sequence,
           "stale generation emitted or consumed an action");

    decision = EndAvrcpPassThroughAclGeneration(&state, 9u);
    Expect(!state.acl_generation_current && state.owner_lease == 0u &&
               state.mode == AvrcpPassThroughGateMode::ObserveOnly,
           "generation end did not fail-close");
    decision = ObserveAvrcpPassThroughEvent(
        &state, 9u, Parse(0x44u));
    Expect(decision.actions == 0u && !decision.event_observed,
           "ended generation consumed a stale event");
    decision = BeginAvrcpPassThroughAclGeneration(&state, 9u);
    Expect(!state.acl_generation_current && decision.actions == 0u,
           "ended generation was reopened");

    state.authorization_epoch = std::numeric_limits<std::uint64_t>::max();
    (void)BeginAvrcpPassThroughAclGeneration(&state, 10u);
    Expect(state.authorization_epoch == 1u,
           "authorization epoch did not wrap safely");
}

}  // namespace

int main() {
    TestFrameParsing();
    TestFailClosedGateAndRouting();
    TestAllActionsAndGenerationGuards();
    if (failures == 0) {
        std::puts("AVRCP PASS THROUGH offline parser/reducer tests passed.");
    }
    return failures == 0 ? 0 : 1;
}
