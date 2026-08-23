// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

namespace native_ldac::agent {

enum class AvrcpPassThroughOperation : std::uint8_t {
    Unknown = 0u,
    VolumeUp,
    VolumeDown,
    Mute,
    Play,
    Stop,
    Pause,
    Forward,
    Backward,
};

enum class AvrcpPassThroughKeyState : std::uint8_t {
    Pressed,
    Released,
};

struct AvrcpPassThroughEvent {
    AvrcpPassThroughOperation operation =
        AvrcpPassThroughOperation::Unknown;
    AvrcpPassThroughKeyState key_state =
        AvrcpPassThroughKeyState::Released;
    std::uint8_t operation_id = 0u;
    std::uint8_t operation_data_length = 0u;
};

// Parses one AV/C CONTROL command addressed to the panel subunit with the
// PASS THROUGH opcode. Responses, other subunits/opcodes, truncated operands,
// and trailing bytes are rejected so transport echoes cannot become actions.
bool ParseAvrcpPassThroughCommand(const std::uint8_t* frame,
                                  std::size_t frame_size,
                                  AvrcpPassThroughEvent* event);

// Builds an event from the decoded observer fields (AVRCP operation id and
// the released flag). Unknown operation ids decode to Unknown.
AvrcpPassThroughEvent MakeAvrcpPassThroughEvent(
    std::uint8_t operation_id,
    bool released);

enum AvrcpPassThroughAction : std::uint32_t {
    AvrcpPassThroughActionNone = 0u,
    AvrcpPassThroughActionStepVolumeUp = 1u << 0u,
    AvrcpPassThroughActionStepVolumeDown = 1u << 1u,
    AvrcpPassThroughActionToggleMute = 1u << 2u,
    AvrcpPassThroughActionPlay = 1u << 3u,
    AvrcpPassThroughActionStop = 1u << 4u,
    AvrcpPassThroughActionPause = 1u << 5u,
    AvrcpPassThroughActionNextTrack = 1u << 6u,
    AvrcpPassThroughActionPreviousTrack = 1u << 7u,
};

enum class AvrcpPassThroughGateMode : std::uint8_t {
    ObserveOnly,
    RouteToCurrentMediaSession,
};

struct AvrcpPassThroughDecision {
    std::uint32_t actions = AvrcpPassThroughActionNone;
    std::uint64_t acl_generation = 0u;
    std::uint64_t owner_lease = 0u;
    std::uint64_t authorization_epoch = 0u;
    std::uint64_t accepted_event_sequence = 0u;
    bool event_observed = false;
    bool duplicate_press = false;
    bool matching_release = false;
};

struct AvrcpPassThroughGateState {
    std::uint64_t acl_generation = 0u;
    bool acl_generation_current = false;
    std::uint64_t owner_lease = 0u;
    std::uint64_t authorization_epoch = 0u;
    std::uint64_t observed_event_count = 0u;
    std::uint64_t accepted_event_sequence = 0u;
    AvrcpPassThroughGateMode mode =
        AvrcpPassThroughGateMode::ObserveOnly;
    bool held_operation_valid = false;
    AvrcpPassThroughOperation held_operation =
        AvrcpPassThroughOperation::Unknown;
};

AvrcpPassThroughDecision BeginAvrcpPassThroughAclGeneration(
    AvrcpPassThroughGateState* state,
    std::uint64_t acl_generation);

AvrcpPassThroughDecision EndAvrcpPassThroughAclGeneration(
    AvrcpPassThroughGateState* state,
    std::uint64_t acl_generation);

AvrcpPassThroughDecision SetAvrcpPassThroughGateMode(
    AvrcpPassThroughGateState* state,
    std::uint64_t acl_generation,
    AvrcpPassThroughGateMode mode);

AvrcpPassThroughDecision AcquireAvrcpPassThroughOwnerLease(
    AvrcpPassThroughGateState* state,
    std::uint64_t acl_generation,
    std::uint64_t owner_lease);

AvrcpPassThroughDecision RevokeAvrcpPassThroughOwnerLease(
    AvrcpPassThroughGateState* state,
    std::uint64_t acl_generation,
    std::uint64_t owner_lease);

AvrcpPassThroughDecision ObserveAvrcpPassThroughEvent(
    AvrcpPassThroughGateState* state,
    std::uint64_t acl_generation,
    const AvrcpPassThroughEvent& event);

bool IsAvrcpPassThroughDecisionCurrent(
    const AvrcpPassThroughGateState& state,
    const AvrcpPassThroughDecision& decision);

bool HasAvrcpPassThroughAction(
    const AvrcpPassThroughDecision& decision,
    AvrcpPassThroughAction action);

}  // namespace native_ldac::agent
