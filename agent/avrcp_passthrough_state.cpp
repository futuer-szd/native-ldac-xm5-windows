// SPDX-License-Identifier: Apache-2.0
#include "avrcp_passthrough_state.h"

#include <limits>

namespace native_ldac::agent {
namespace {

constexpr std::uint8_t kAvcControl = 0x00u;
constexpr std::uint8_t kPanelSubunitIdZero = 0x48u;
constexpr std::uint8_t kPassThroughOpcode = 0x7Cu;
constexpr std::uint8_t kReleasedMask = 0x80u;
constexpr std::uint8_t kOperationMask = 0x7Fu;

constexpr std::uint8_t kVolumeUp = 0x41u;
constexpr std::uint8_t kVolumeDown = 0x42u;
constexpr std::uint8_t kMute = 0x43u;
constexpr std::uint8_t kPlay = 0x44u;
constexpr std::uint8_t kStop = 0x45u;
constexpr std::uint8_t kPause = 0x46u;
constexpr std::uint8_t kForward = 0x4Bu;
constexpr std::uint8_t kBackward = 0x4Cu;

std::uint64_t NextNonzero(std::uint64_t value) {
    return value == std::numeric_limits<std::uint64_t>::max()
        ? 1u
        : value + 1u;
}

bool IsNewerGeneration(std::uint64_t candidate,
                       std::uint64_t current) {
    if (candidate == 0u) return false;
    if (current == 0u) return true;
    constexpr std::uint64_t kHalfGenerationRange =
        (std::uint64_t{1u} << 63u);
    const std::uint64_t distance = candidate - current;
    return distance != 0u && distance < kHalfGenerationRange;
}

AvrcpPassThroughOperation DecodeOperation(std::uint8_t operation_id) {
    switch (operation_id) {
        case kVolumeUp:
            return AvrcpPassThroughOperation::VolumeUp;
        case kVolumeDown:
            return AvrcpPassThroughOperation::VolumeDown;
        case kMute:
            return AvrcpPassThroughOperation::Mute;
        case kPlay:
            return AvrcpPassThroughOperation::Play;
        case kStop:
            return AvrcpPassThroughOperation::Stop;
        case kPause:
            return AvrcpPassThroughOperation::Pause;
        case kForward:
            return AvrcpPassThroughOperation::Forward;
        case kBackward:
            return AvrcpPassThroughOperation::Backward;
        default:
            return AvrcpPassThroughOperation::Unknown;
    }
}

std::uint32_t ActionForOperation(AvrcpPassThroughOperation operation) {
    switch (operation) {
        case AvrcpPassThroughOperation::VolumeUp:
            return AvrcpPassThroughActionStepVolumeUp;
        case AvrcpPassThroughOperation::VolumeDown:
            return AvrcpPassThroughActionStepVolumeDown;
        case AvrcpPassThroughOperation::Mute:
            return AvrcpPassThroughActionToggleMute;
        case AvrcpPassThroughOperation::Play:
            return AvrcpPassThroughActionPlay;
        case AvrcpPassThroughOperation::Stop:
            return AvrcpPassThroughActionStop;
        case AvrcpPassThroughOperation::Pause:
            return AvrcpPassThroughActionPause;
        case AvrcpPassThroughOperation::Forward:
            return AvrcpPassThroughActionNextTrack;
        case AvrcpPassThroughOperation::Backward:
            return AvrcpPassThroughActionPreviousTrack;
        default:
            return AvrcpPassThroughActionNone;
    }
}

bool IsCurrentGeneration(const AvrcpPassThroughGateState* state,
                         std::uint64_t generation) {
    return state != nullptr && generation != 0u &&
        state->acl_generation_current &&
        state->acl_generation == generation;
}

void ResetHeldOperation(AvrcpPassThroughGateState* state) {
    state->held_operation_valid = false;
    state->held_operation = AvrcpPassThroughOperation::Unknown;
}

AvrcpPassThroughDecision Snapshot(
    const AvrcpPassThroughGateState& state) {
    AvrcpPassThroughDecision decision{};
    decision.acl_generation = state.acl_generation;
    decision.owner_lease = state.owner_lease;
    decision.authorization_epoch = state.authorization_epoch;
    decision.accepted_event_sequence = state.accepted_event_sequence;
    return decision;
}

}  // namespace

bool ParseAvrcpPassThroughCommand(const std::uint8_t* frame,
                                  std::size_t frame_size,
                                  AvrcpPassThroughEvent* event) {
    if (frame == nullptr || event == nullptr || frame_size < 5u ||
        frame[0] != kAvcControl || frame[1] != kPanelSubunitIdZero ||
        frame[2] != kPassThroughOpcode ||
        frame_size != static_cast<std::size_t>(5u + frame[4])) {
        return false;
    }
    const std::uint8_t state_and_operation = frame[3];
    event->operation_id = state_and_operation & kOperationMask;
    event->operation = DecodeOperation(event->operation_id);
    event->key_state = (state_and_operation & kReleasedMask) == 0u
        ? AvrcpPassThroughKeyState::Pressed
        : AvrcpPassThroughKeyState::Released;
    event->operation_data_length = frame[4];
    return true;
}

AvrcpPassThroughEvent MakeAvrcpPassThroughEvent(
    std::uint8_t operation_id,
    bool released) {
    AvrcpPassThroughEvent event;
    event.operation_id = operation_id;
    event.operation = DecodeOperation(operation_id);
    event.key_state = released
        ? AvrcpPassThroughKeyState::Released
        : AvrcpPassThroughKeyState::Pressed;
    event.operation_data_length = 0u;
    return event;
}

AvrcpPassThroughDecision BeginAvrcpPassThroughAclGeneration(
    AvrcpPassThroughGateState* state,
    std::uint64_t acl_generation) {
    if (state == nullptr ||
        !IsNewerGeneration(acl_generation, state->acl_generation)) {
        return {};
    }
    state->acl_generation = acl_generation;
    state->acl_generation_current = true;
    state->owner_lease = 0u;
    state->authorization_epoch = NextNonzero(state->authorization_epoch);
    state->observed_event_count = 0u;
    state->accepted_event_sequence = 0u;
    state->mode = AvrcpPassThroughGateMode::ObserveOnly;
    ResetHeldOperation(state);
    return Snapshot(*state);
}

AvrcpPassThroughDecision EndAvrcpPassThroughAclGeneration(
    AvrcpPassThroughGateState* state,
    std::uint64_t acl_generation) {
    if (!IsCurrentGeneration(state, acl_generation)) {
        return {};
    }
    state->acl_generation_current = false;
    state->owner_lease = 0u;
    state->authorization_epoch = NextNonzero(state->authorization_epoch);
    state->mode = AvrcpPassThroughGateMode::ObserveOnly;
    ResetHeldOperation(state);
    return Snapshot(*state);
}

AvrcpPassThroughDecision SetAvrcpPassThroughGateMode(
    AvrcpPassThroughGateState* state,
    std::uint64_t acl_generation,
    AvrcpPassThroughGateMode mode) {
    if (!IsCurrentGeneration(state, acl_generation)) {
        return {};
    }
    if (state->mode != mode) {
        state->mode = mode;
        state->authorization_epoch = NextNonzero(state->authorization_epoch);
        ResetHeldOperation(state);
    }
    return Snapshot(*state);
}

AvrcpPassThroughDecision AcquireAvrcpPassThroughOwnerLease(
    AvrcpPassThroughGateState* state,
    std::uint64_t acl_generation,
    std::uint64_t owner_lease) {
    if (!IsCurrentGeneration(state, acl_generation) || owner_lease == 0u ||
        (state->owner_lease != 0u && state->owner_lease != owner_lease)) {
        return {};
    }
    if (state->owner_lease == 0u) {
        state->owner_lease = owner_lease;
        state->authorization_epoch = NextNonzero(state->authorization_epoch);
        ResetHeldOperation(state);
    }
    return Snapshot(*state);
}

AvrcpPassThroughDecision RevokeAvrcpPassThroughOwnerLease(
    AvrcpPassThroughGateState* state,
    std::uint64_t acl_generation,
    std::uint64_t owner_lease) {
    if (!IsCurrentGeneration(state, acl_generation) || owner_lease == 0u ||
        state->owner_lease != owner_lease) {
        return {};
    }
    state->owner_lease = 0u;
    state->authorization_epoch = NextNonzero(state->authorization_epoch);
    ResetHeldOperation(state);
    return Snapshot(*state);
}

AvrcpPassThroughDecision ObserveAvrcpPassThroughEvent(
    AvrcpPassThroughGateState* state,
    std::uint64_t acl_generation,
    const AvrcpPassThroughEvent& event) {
    if (!IsCurrentGeneration(state, acl_generation)) {
        return {};
    }
    ++state->observed_event_count;
    auto decision = Snapshot(*state);
    decision.event_observed = true;

    if (event.operation == AvrcpPassThroughOperation::Unknown) {
        return decision;
    }
    if (event.key_state == AvrcpPassThroughKeyState::Released) {
        if (state->held_operation_valid &&
            state->held_operation == event.operation) {
            ResetHeldOperation(state);
            decision.matching_release = true;
        }
        return decision;
    }
    if (state->held_operation_valid &&
        state->held_operation == event.operation) {
        decision.duplicate_press = true;
        return decision;
    }

    state->held_operation_valid = true;
    state->held_operation = event.operation;
    if (state->mode !=
            AvrcpPassThroughGateMode::RouteToCurrentMediaSession ||
        state->owner_lease == 0u) {
        return decision;
    }
    const std::uint32_t action = ActionForOperation(event.operation);
    if (action == AvrcpPassThroughActionNone) {
        return decision;
    }
    state->accepted_event_sequence =
        NextNonzero(state->accepted_event_sequence);
    decision = Snapshot(*state);
    decision.event_observed = true;
    decision.actions = action;
    return decision;
}

bool IsAvrcpPassThroughDecisionCurrent(
    const AvrcpPassThroughGateState& state,
    const AvrcpPassThroughDecision& decision) {
    return decision.actions != AvrcpPassThroughActionNone &&
        state.acl_generation_current &&
        state.mode ==
            AvrcpPassThroughGateMode::RouteToCurrentMediaSession &&
        state.owner_lease != 0u &&
        decision.acl_generation == state.acl_generation &&
        decision.owner_lease == state.owner_lease &&
        decision.authorization_epoch == state.authorization_epoch &&
        decision.accepted_event_sequence == state.accepted_event_sequence;
}

bool HasAvrcpPassThroughAction(
    const AvrcpPassThroughDecision& decision,
    AvrcpPassThroughAction action) {
    return (decision.actions & static_cast<std::uint32_t>(action)) != 0u;
}

}  // namespace native_ldac::agent
