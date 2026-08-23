// SPDX-License-Identifier: Apache-2.0
#include "avrcp_absolute_volume_observe_log.h"

namespace native_ldac::agent {
namespace {

struct ObserveCursor {
    std::uint64_t last_generation = 0u;
    bool generation_current = false;
    std::uint64_t owner_lease = 0u;
};

bool IsValidKind(AvrcpVolumeObserveEventKind value) {
    switch (value) {
        case AvrcpVolumeObserveEventKind::GenerationStarted:
        case AvrcpVolumeObserveEventKind::GenerationEnded:
        case AvrcpVolumeObserveEventKind::Capability:
        case AvrcpVolumeObserveEventKind::WindowsVolumeCallback:
        case AvrcpVolumeObserveEventKind::Xm5Volume:
        case AvrcpVolumeObserveEventKind::OwnerLeaseAcquired:
        case AvrcpVolumeObserveEventKind::OwnerLeaseRevoked:
        case AvrcpVolumeObserveEventKind::ModeChanged:
            return true;
    }
    return false;
}

bool IsValidSupport(AvrcpAbsoluteVolumeSupport value) {
    switch (value) {
        case AvrcpAbsoluteVolumeSupport::Unknown:
        case AvrcpAbsoluteVolumeSupport::Unsupported:
        case AvrcpAbsoluteVolumeSupport::Supported:
            return true;
    }
    return false;
}

bool IsValidXm5Event(AvrcpXm5VolumeEvent value) {
    switch (value) {
        case AvrcpXm5VolumeEvent::CommandResponse:
        case AvrcpXm5VolumeEvent::RemoteNotification:
            return true;
    }
    return false;
}

bool IsValidMode(AvrcpAbsoluteVolumeGateMode value) {
    switch (value) {
        case AvrcpAbsoluteVolumeGateMode::ObserveOnly:
        case AvrcpAbsoluteVolumeGateMode::Synchronize:
            return true;
    }
    return false;
}

bool IsNewerGeneration(std::uint64_t candidate,
                       std::uint64_t current) {
    if (candidate == 0u) return false;
    if (current == 0u) return true;
    constexpr std::uint64_t kHalfRange =
        std::uint64_t{1u} << 63u;
    const std::uint64_t distance = candidate - current;
    return distance != 0u && distance < kHalfRange;
}

AvrcpVolumeObserveStatus ValidateAndApplyEvent(
    const AvrcpVolumeObserveEvent& event,
    std::uint64_t expected_sequence,
    ObserveCursor* cursor) {
    if (cursor == nullptr || event.sequence != expected_sequence ||
        event.generation == 0u) {
        return AvrcpVolumeObserveStatus::InvalidArgument;
    }
    if (!IsValidKind(event.kind) || !IsValidSupport(event.support) ||
        !IsValidXm5Event(event.xm5_event) ||
        !IsValidMode(event.mode)) {
        return AvrcpVolumeObserveStatus::InvalidEnum;
    }
    if (event.windows_volume.percent > 100u ||
        event.xm5_absolute_volume > 127u) {
        return AvrcpVolumeObserveStatus::OutOfRange;
    }
    if (event.kind == AvrcpVolumeObserveEventKind::GenerationStarted) {
        if (!IsNewerGeneration(event.generation,
                               cursor->last_generation)) {
            return AvrcpVolumeObserveStatus::StaleGeneration;
        }
        cursor->last_generation = event.generation;
        cursor->generation_current = true;
        cursor->owner_lease = 0u;
        return AvrcpVolumeObserveStatus::Accepted;
    }
    if (!cursor->generation_current ||
        event.generation != cursor->last_generation) {
        return AvrcpVolumeObserveStatus::StaleGeneration;
    }

    switch (event.kind) {
        case AvrcpVolumeObserveEventKind::GenerationStarted:
            break;
        case AvrcpVolumeObserveEventKind::GenerationEnded:
            cursor->generation_current = false;
            cursor->owner_lease = 0u;
            return AvrcpVolumeObserveStatus::Accepted;
        case AvrcpVolumeObserveEventKind::Capability:
            return AvrcpVolumeObserveStatus::Accepted;
        case AvrcpVolumeObserveEventKind::WindowsVolumeCallback:
            return AvrcpVolumeObserveStatus::Accepted;
        case AvrcpVolumeObserveEventKind::Xm5Volume:
            return AvrcpVolumeObserveStatus::Accepted;
        case AvrcpVolumeObserveEventKind::OwnerLeaseAcquired:
            if (event.owner_lease == 0u) {
                return AvrcpVolumeObserveStatus::OutOfRange;
            }
            if (cursor->owner_lease != 0u &&
                cursor->owner_lease != event.owner_lease) {
                return AvrcpVolumeObserveStatus::InvalidTransition;
            }
            cursor->owner_lease = event.owner_lease;
            return AvrcpVolumeObserveStatus::Accepted;
        case AvrcpVolumeObserveEventKind::OwnerLeaseRevoked:
            if (event.owner_lease == 0u) {
                return AvrcpVolumeObserveStatus::OutOfRange;
            }
            if (cursor->owner_lease != event.owner_lease) {
                return AvrcpVolumeObserveStatus::InvalidTransition;
            }
            cursor->owner_lease = 0u;
            return AvrcpVolumeObserveStatus::Accepted;
        case AvrcpVolumeObserveEventKind::ModeChanged:
            return AvrcpVolumeObserveStatus::Accepted;
    }
    return AvrcpVolumeObserveStatus::InvalidEnum;
}

bool DecisionIsEmpty(const AvrcpAbsoluteVolumeGateDecision& decision) {
    return decision.volume.actions == 0u &&
           decision.acl_generation == 0u &&
           decision.owner_lease == 0u &&
           decision.authorization_epoch == 0u;
}

void ResetCurrentGenerationSnapshot(
    AvrcpVolumeObserveSnapshot* snapshot,
    std::uint64_t generation) {
    snapshot->current_generation = generation;
    snapshot->generation_current = true;
    snapshot->support = AvrcpAbsoluteVolumeSupport::Unknown;
    snapshot->windows_volume_observed = false;
    snapshot->windows_volume = {};
    snapshot->xm5_volume_observed = false;
    snapshot->xm5_absolute_volume = 0u;
    snapshot->xm5_event = AvrcpXm5VolumeEvent::CommandResponse;
    snapshot->owner_lease = 0u;
    snapshot->requested_mode =
        AvrcpAbsoluteVolumeGateMode::ObserveOnly;
}

}  // namespace

AvrcpVolumeObserveStatus RecordAvrcpVolumeObserveEvent(
    AvrcpVolumeObserveLog* log,
    const AvrcpVolumeObserveEvent& event) {
    if (log == nullptr || event.sequence != 0u ||
        log->event_count > kAvrcpVolumeObserveLogCapacity) {
        return AvrcpVolumeObserveStatus::InvalidArgument;
    }
    ObserveCursor cursor{};
    for (std::size_t index = 0u; index < log->event_count; ++index) {
        const auto existing_status = ValidateAndApplyEvent(
            log->events[index],
            static_cast<std::uint64_t>(index) + 1u,
            &cursor);
        if (existing_status != AvrcpVolumeObserveStatus::Accepted) {
            return existing_status;
        }
    }
    if (cursor.last_generation != log->last_generation ||
        cursor.generation_current != log->generation_current ||
        cursor.owner_lease != log->observed_owner_lease) {
        return AvrcpVolumeObserveStatus::InvalidTransition;
    }
    if (log->event_count == kAvrcpVolumeObserveLogCapacity) {
        return AvrcpVolumeObserveStatus::CapacityExceeded;
    }

    AvrcpVolumeObserveEvent candidate = event;
    candidate.sequence =
        static_cast<std::uint64_t>(log->event_count) + 1u;
    const auto status = ValidateAndApplyEvent(
        candidate, candidate.sequence, &cursor);
    if (status != AvrcpVolumeObserveStatus::Accepted) {
        return status;
    }

    log->events[log->event_count] = candidate;
    ++log->event_count;
    log->last_generation = cursor.last_generation;
    log->generation_current = cursor.generation_current;
    log->observed_owner_lease = cursor.owner_lease;
    return AvrcpVolumeObserveStatus::Accepted;
}

AvrcpVolumeObserveStatus ReplayAvrcpVolumeObserveLog(
    const AvrcpVolumeObserveLog& log,
    AvrcpVolumeObserveSnapshot* snapshot) {
    if (snapshot == nullptr ||
        log.event_count > kAvrcpVolumeObserveLogCapacity) {
        return AvrcpVolumeObserveStatus::InvalidArgument;
    }

    ObserveCursor cursor{};
    AvrcpAbsoluteVolumeGateState gate{};
    AvrcpVolumeObserveSnapshot next{};
    for (std::size_t index = 0u; index < log.event_count; ++index) {
        const auto& event = log.events[index];
        const auto validation = ValidateAndApplyEvent(
            event, static_cast<std::uint64_t>(index) + 1u, &cursor);
        if (validation != AvrcpVolumeObserveStatus::Accepted) {
            return validation;
        }

        AvrcpAbsoluteVolumeGateDecision decision{};
        switch (event.kind) {
            case AvrcpVolumeObserveEventKind::GenerationStarted:
                decision = BeginAvrcpAbsoluteVolumeAclGeneration(
                    &gate, event.generation);
                ++next.generations_started;
                ResetCurrentGenerationSnapshot(&next, event.generation);
                break;
            case AvrcpVolumeObserveEventKind::GenerationEnded:
                decision = EndAvrcpAbsoluteVolumeAclGeneration(
                    &gate, event.generation);
                ++next.generations_ended;
                next.generation_current = false;
                next.owner_lease = 0u;
                next.requested_mode =
                    AvrcpAbsoluteVolumeGateMode::ObserveOnly;
                break;
            case AvrcpVolumeObserveEventKind::Capability:
                decision = ObserveAvrcpAbsoluteVolumeCapability(
                    &gate, event.generation, event.support);
                next.support = event.support;
                ++next.capability_event_count;
                break;
            case AvrcpVolumeObserveEventKind::WindowsVolumeCallback:
                decision = ObserveWindowsEndpointVolumeThroughGate(
                    &gate, event.generation, event.windows_volume);
                next.windows_volume_observed = true;
                next.windows_volume = event.windows_volume;
                ++next.windows_callback_count;
                break;
            case AvrcpVolumeObserveEventKind::Xm5Volume:
                decision = ObserveXm5AbsoluteVolumeThroughGate(
                    &gate,
                    event.generation,
                    event.xm5_absolute_volume,
                    event.xm5_event);
                next.xm5_volume_observed = true;
                next.xm5_absolute_volume = static_cast<std::uint8_t>(
                    event.xm5_absolute_volume);
                next.xm5_event = event.xm5_event;
                if (event.xm5_event ==
                    AvrcpXm5VolumeEvent::CommandResponse) {
                    ++next.xm5_command_response_count;
                } else {
                    ++next.xm5_remote_notification_count;
                }
                break;
            case AvrcpVolumeObserveEventKind::OwnerLeaseAcquired:
                decision = AcquireAvrcpAbsoluteVolumeOwnerLease(
                    &gate, event.generation, event.owner_lease);
                next.owner_lease = event.owner_lease;
                ++next.lease_acquire_count;
                break;
            case AvrcpVolumeObserveEventKind::OwnerLeaseRevoked:
                decision = RevokeAvrcpAbsoluteVolumeOwnerLease(
                    &gate, event.generation, event.owner_lease);
                next.owner_lease = 0u;
                ++next.lease_revoke_count;
                break;
            case AvrcpVolumeObserveEventKind::ModeChanged:
                // Record the requested mode, but never authorize it during
                // diagnostics replay.
                decision = SetAvrcpAbsoluteVolumeGateMode(
                    &gate,
                    event.generation,
                    AvrcpAbsoluteVolumeGateMode::ObserveOnly);
                next.requested_mode = event.mode;
                ++next.mode_change_count;
                break;
        }
        if (!DecisionIsEmpty(decision)) {
            return AvrcpVolumeObserveStatus::ActionViolation;
        }
        ++next.replayed_event_count;
        next.last_sequence = event.sequence;
    }

    if (cursor.last_generation != log.last_generation ||
        cursor.generation_current != log.generation_current ||
        cursor.owner_lease != log.observed_owner_lease) {
        return AvrcpVolumeObserveStatus::InvalidTransition;
    }

    next.emitted_action_count = 0u;
    *snapshot = next;
    return AvrcpVolumeObserveStatus::Accepted;
}

}  // namespace native_ldac::agent
