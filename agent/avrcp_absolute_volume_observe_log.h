// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "avrcp_absolute_volume_state.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace native_ldac::agent {

constexpr std::size_t kAvrcpVolumeObserveLogCapacity = 256u;
constexpr std::uint32_t kAvrcpVolumeObserveSnapshotSchema = 1u;

enum class AvrcpVolumeObserveEventKind : std::uint32_t {
    GenerationStarted = 0u,
    GenerationEnded,
    Capability,
    WindowsVolumeCallback,
    Xm5Volume,
    OwnerLeaseAcquired,
    OwnerLeaseRevoked,
    ModeChanged,
};

enum class AvrcpVolumeObserveStatus : std::uint32_t {
    Accepted = 0u,
    InvalidArgument,
    InvalidEnum,
    OutOfRange,
    StaleGeneration,
    InvalidTransition,
    CapacityExceeded,
    ActionViolation,
};

// Unused payload fields remain at their defaults. Sequence zero is required
// when recording; the bounded recorder assigns a deterministic sequence.
struct AvrcpVolumeObserveEvent {
    std::uint64_t sequence = 0u;
    std::uint64_t generation = 0u;
    AvrcpVolumeObserveEventKind kind =
        AvrcpVolumeObserveEventKind::GenerationStarted;
    AvrcpAbsoluteVolumeSupport support =
        AvrcpAbsoluteVolumeSupport::Unknown;
    AvrcpWindowsVolume windows_volume{};
    std::uint32_t xm5_absolute_volume = 0u;
    AvrcpXm5VolumeEvent xm5_event =
        AvrcpXm5VolumeEvent::CommandResponse;
    std::uint64_t owner_lease = 0u;
    AvrcpAbsoluteVolumeGateMode mode =
        AvrcpAbsoluteVolumeGateMode::ObserveOnly;
};

struct AvrcpVolumeObserveLog {
    std::array<AvrcpVolumeObserveEvent,
               kAvrcpVolumeObserveLogCapacity> events{};
    std::size_t event_count = 0u;
    std::uint64_t last_generation = 0u;
    bool generation_current = false;
    std::uint64_t observed_owner_lease = 0u;
};

// Plain, stable fields are intentionally suitable for deterministic JSON
// serialization by a separate diagnostics layer.
struct AvrcpVolumeObserveSnapshot {
    std::uint32_t schema_version =
        kAvrcpVolumeObserveSnapshotSchema;
    std::uint64_t replayed_event_count = 0u;
    std::uint64_t last_sequence = 0u;
    std::uint64_t generations_started = 0u;
    std::uint64_t generations_ended = 0u;
    std::uint64_t current_generation = 0u;
    bool generation_current = false;

    AvrcpAbsoluteVolumeSupport support =
        AvrcpAbsoluteVolumeSupport::Unknown;
    std::uint64_t capability_event_count = 0u;
    bool windows_volume_observed = false;
    AvrcpWindowsVolume windows_volume{};
    std::uint64_t windows_callback_count = 0u;
    bool xm5_volume_observed = false;
    std::uint8_t xm5_absolute_volume = 0u;
    AvrcpXm5VolumeEvent xm5_event =
        AvrcpXm5VolumeEvent::CommandResponse;
    std::uint64_t xm5_command_response_count = 0u;
    std::uint64_t xm5_remote_notification_count = 0u;

    std::uint64_t owner_lease = 0u;
    std::uint64_t lease_acquire_count = 0u;
    std::uint64_t lease_revoke_count = 0u;
    AvrcpAbsoluteVolumeGateMode requested_mode =
        AvrcpAbsoluteVolumeGateMode::ObserveOnly;
    std::uint64_t mode_change_count = 0u;

    AvrcpAbsoluteVolumeGateMode enforced_replay_mode =
        AvrcpAbsoluteVolumeGateMode::ObserveOnly;
    std::uint64_t emitted_action_count = 0u;
};

AvrcpVolumeObserveStatus RecordAvrcpVolumeObserveEvent(
    AvrcpVolumeObserveLog* log,
    const AvrcpVolumeObserveEvent& event);

// Replay never enters Synchronize mode, including when a recorded ModeChanged
// event requested it. Any nonzero gate decision fails the replay closed.
AvrcpVolumeObserveStatus ReplayAvrcpVolumeObserveLog(
    const AvrcpVolumeObserveLog& log,
    AvrcpVolumeObserveSnapshot* snapshot);

}  // namespace native_ldac::agent
