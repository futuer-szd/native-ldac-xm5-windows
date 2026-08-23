// SPDX-License-Identifier: Apache-2.0
#include "../avrcp_absolute_volume_observe_log.h"

#include <cstdio>
#include <limits>

namespace {

using namespace native_ldac::agent;

int failures = 0;

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expression); \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

AvrcpVolumeObserveEvent Event(AvrcpVolumeObserveEventKind kind,
                              std::uint64_t generation) {
    AvrcpVolumeObserveEvent event;
    event.kind = kind;
    event.generation = generation;
    return event;
}

void Append(AvrcpVolumeObserveLog* log,
            const AvrcpVolumeObserveEvent& event) {
    CHECK(RecordAvrcpVolumeObserveEvent(log, event) ==
          AvrcpVolumeObserveStatus::Accepted);
}

bool SameSnapshot(const AvrcpVolumeObserveSnapshot& left,
                  const AvrcpVolumeObserveSnapshot& right) {
    return left.schema_version == right.schema_version &&
           left.replayed_event_count == right.replayed_event_count &&
           left.last_sequence == right.last_sequence &&
           left.generations_started == right.generations_started &&
           left.generations_ended == right.generations_ended &&
           left.current_generation == right.current_generation &&
           left.generation_current == right.generation_current &&
           left.support == right.support &&
           left.capability_event_count == right.capability_event_count &&
           left.windows_volume_observed ==
               right.windows_volume_observed &&
           left.windows_volume.percent == right.windows_volume.percent &&
           left.windows_volume.muted == right.windows_volume.muted &&
           left.windows_callback_count == right.windows_callback_count &&
           left.xm5_volume_observed == right.xm5_volume_observed &&
           left.xm5_absolute_volume == right.xm5_absolute_volume &&
           left.xm5_event == right.xm5_event &&
           left.xm5_command_response_count ==
               right.xm5_command_response_count &&
           left.xm5_remote_notification_count ==
               right.xm5_remote_notification_count &&
           left.owner_lease == right.owner_lease &&
           left.lease_acquire_count == right.lease_acquire_count &&
           left.lease_revoke_count == right.lease_revoke_count &&
           left.requested_mode == right.requested_mode &&
           left.mode_change_count == right.mode_change_count &&
           left.enforced_replay_mode == right.enforced_replay_mode &&
           left.emitted_action_count == right.emitted_action_count;
}

void TestObserveOnlyRecordAndReplay() {
    AvrcpVolumeObserveLog log;
    Append(&log, Event(
        AvrcpVolumeObserveEventKind::GenerationStarted, 1u));

    auto event = Event(AvrcpVolumeObserveEventKind::Capability, 1u);
    event.support = AvrcpAbsoluteVolumeSupport::Supported;
    Append(&log, event);
    event = Event(
        AvrcpVolumeObserveEventKind::WindowsVolumeCallback, 1u);
    event.windows_volume = {55u, false};
    Append(&log, event);
    event = Event(
        AvrcpVolumeObserveEventKind::OwnerLeaseAcquired, 1u);
    event.owner_lease = 42u;
    Append(&log, event);
    event = Event(AvrcpVolumeObserveEventKind::ModeChanged, 1u);
    event.mode = AvrcpAbsoluteVolumeGateMode::Synchronize;
    Append(&log, event);
    event = Event(AvrcpVolumeObserveEventKind::Xm5Volume, 1u);
    event.xm5_absolute_volume = 70u;
    event.xm5_event = AvrcpXm5VolumeEvent::CommandResponse;
    Append(&log, event);
    event.xm5_absolute_volume = 71u;
    event.xm5_event = AvrcpXm5VolumeEvent::RemoteNotification;
    Append(&log, event);

    AvrcpVolumeObserveSnapshot snapshot;
    CHECK(ReplayAvrcpVolumeObserveLog(log, &snapshot) ==
          AvrcpVolumeObserveStatus::Accepted);
    CHECK(snapshot.schema_version == 1u);
    CHECK(snapshot.replayed_event_count == 7u);
    CHECK(snapshot.last_sequence == 7u);
    CHECK(snapshot.generations_started == 1u);
    CHECK(snapshot.generations_ended == 0u);
    CHECK(snapshot.current_generation == 1u);
    CHECK(snapshot.generation_current);
    CHECK(snapshot.support == AvrcpAbsoluteVolumeSupport::Supported);
    CHECK(snapshot.capability_event_count == 1u);
    CHECK(snapshot.windows_volume_observed);
    CHECK(snapshot.windows_volume.percent == 55u);
    CHECK(!snapshot.windows_volume.muted);
    CHECK(snapshot.windows_callback_count == 1u);
    CHECK(snapshot.xm5_volume_observed);
    CHECK(snapshot.xm5_absolute_volume == 71u);
    CHECK(snapshot.xm5_event ==
          AvrcpXm5VolumeEvent::RemoteNotification);
    CHECK(snapshot.xm5_command_response_count == 1u);
    CHECK(snapshot.xm5_remote_notification_count == 1u);
    CHECK(snapshot.owner_lease == 42u);
    CHECK(snapshot.lease_acquire_count == 1u);
    CHECK(snapshot.lease_revoke_count == 0u);
    CHECK(snapshot.requested_mode ==
          AvrcpAbsoluteVolumeGateMode::Synchronize);
    CHECK(snapshot.mode_change_count == 1u);
    CHECK(snapshot.enforced_replay_mode ==
          AvrcpAbsoluteVolumeGateMode::ObserveOnly);
    CHECK(snapshot.emitted_action_count == 0u);

    event = Event(
        AvrcpVolumeObserveEventKind::OwnerLeaseRevoked, 1u);
    event.owner_lease = 42u;
    Append(&log, event);
    Append(&log, Event(
        AvrcpVolumeObserveEventKind::GenerationEnded, 1u));
    CHECK(ReplayAvrcpVolumeObserveLog(log, &snapshot) ==
          AvrcpVolumeObserveStatus::Accepted);
    CHECK(snapshot.replayed_event_count == 9u);
    CHECK(snapshot.generations_ended == 1u);
    CHECK(!snapshot.generation_current);
    CHECK(snapshot.owner_lease == 0u);
    CHECK(snapshot.lease_revoke_count == 1u);
    CHECK(snapshot.requested_mode ==
          AvrcpAbsoluteVolumeGateMode::ObserveOnly);
    CHECK(snapshot.emitted_action_count == 0u);
}

void TestRecorderRejectsInvalidInputWithoutMutation() {
    AvrcpVolumeObserveLog log;
    auto start = Event(
        AvrcpVolumeObserveEventKind::GenerationStarted, 5u);
    start.sequence = 1u;
    CHECK(RecordAvrcpVolumeObserveEvent(&log, start) ==
          AvrcpVolumeObserveStatus::InvalidArgument);
    start.sequence = 0u;
    start.generation = 0u;
    CHECK(RecordAvrcpVolumeObserveEvent(&log, start) ==
          AvrcpVolumeObserveStatus::InvalidArgument);
    start.generation = 5u;
    Append(&log, start);
    const std::size_t initial_count = log.event_count;

    auto event = Event(
        static_cast<AvrcpVolumeObserveEventKind>(999u), 5u);
    CHECK(RecordAvrcpVolumeObserveEvent(&log, event) ==
          AvrcpVolumeObserveStatus::InvalidEnum);
    event = Event(AvrcpVolumeObserveEventKind::Capability, 5u);
    event.support = static_cast<AvrcpAbsoluteVolumeSupport>(999u);
    CHECK(RecordAvrcpVolumeObserveEvent(&log, event) ==
          AvrcpVolumeObserveStatus::InvalidEnum);
    event = Event(
        AvrcpVolumeObserveEventKind::WindowsVolumeCallback, 5u);
    event.windows_volume.percent = 101u;
    CHECK(RecordAvrcpVolumeObserveEvent(&log, event) ==
          AvrcpVolumeObserveStatus::OutOfRange);
    event = Event(AvrcpVolumeObserveEventKind::Xm5Volume, 5u);
    event.xm5_absolute_volume = 128u;
    CHECK(RecordAvrcpVolumeObserveEvent(&log, event) ==
          AvrcpVolumeObserveStatus::OutOfRange);
    event.xm5_absolute_volume = 100u;
    event.xm5_event = static_cast<AvrcpXm5VolumeEvent>(999u);
    CHECK(RecordAvrcpVolumeObserveEvent(&log, event) ==
          AvrcpVolumeObserveStatus::InvalidEnum);
    event = Event(AvrcpVolumeObserveEventKind::ModeChanged, 5u);
    event.mode = static_cast<AvrcpAbsoluteVolumeGateMode>(999u);
    CHECK(RecordAvrcpVolumeObserveEvent(&log, event) ==
          AvrcpVolumeObserveStatus::InvalidEnum);
    event = Event(
        AvrcpVolumeObserveEventKind::OwnerLeaseAcquired, 5u);
    event.owner_lease = 0u;
    CHECK(RecordAvrcpVolumeObserveEvent(&log, event) ==
          AvrcpVolumeObserveStatus::OutOfRange);
    event.owner_lease = 11u;
    Append(&log, event);
    auto conflicting = event;
    conflicting.owner_lease = 12u;
    CHECK(RecordAvrcpVolumeObserveEvent(&log, conflicting) ==
          AvrcpVolumeObserveStatus::InvalidTransition);
    event.kind = AvrcpVolumeObserveEventKind::OwnerLeaseRevoked;
    event.owner_lease = 12u;
    CHECK(RecordAvrcpVolumeObserveEvent(&log, event) ==
          AvrcpVolumeObserveStatus::InvalidTransition);
    event = Event(AvrcpVolumeObserveEventKind::Capability, 4u);
    CHECK(RecordAvrcpVolumeObserveEvent(&log, event) ==
          AvrcpVolumeObserveStatus::StaleGeneration);
    CHECK(log.event_count == initial_count + 1u);
    CHECK(log.observed_owner_lease == 11u);

    auto hidden_invalid = Event(
        AvrcpVolumeObserveEventKind::Capability, 5u);
    hidden_invalid.mode =
        static_cast<AvrcpAbsoluteVolumeGateMode>(999u);
    CHECK(RecordAvrcpVolumeObserveEvent(&log, hidden_invalid) ==
          AvrcpVolumeObserveStatus::InvalidEnum);
    hidden_invalid = Event(
        AvrcpVolumeObserveEventKind::Capability, 5u);
    hidden_invalid.xm5_absolute_volume = 128u;
    CHECK(RecordAvrcpVolumeObserveEvent(&log, hidden_invalid) ==
          AvrcpVolumeObserveStatus::OutOfRange);
    hidden_invalid = Event(
        AvrcpVolumeObserveEventKind::Capability, 5u);
    hidden_invalid.support =
        static_cast<AvrcpAbsoluteVolumeSupport>(999u);
    hidden_invalid.xm5_event =
        static_cast<AvrcpXm5VolumeEvent>(999u);
    CHECK(RecordAvrcpVolumeObserveEvent(&log, hidden_invalid) ==
          AvrcpVolumeObserveStatus::InvalidEnum);

    auto bad_metadata = log;
    bad_metadata.last_generation = 6u;
    event = Event(AvrcpVolumeObserveEventKind::Capability, 5u);
    CHECK(RecordAvrcpVolumeObserveEvent(&bad_metadata, event) ==
          AvrcpVolumeObserveStatus::InvalidTransition);
    CHECK(bad_metadata.event_count == log.event_count);
}

void TestReplayRejectsTamperingWithoutPublishingPartialSnapshot() {
    AvrcpVolumeObserveLog log;
    Append(&log, Event(
        AvrcpVolumeObserveEventKind::GenerationStarted, 9u));
    auto event = Event(
        AvrcpVolumeObserveEventKind::WindowsVolumeCallback, 9u);
    event.windows_volume = {25u, false};
    Append(&log, event);

    AvrcpVolumeObserveSnapshot sentinel;
    sentinel.schema_version = 77u;
    sentinel.replayed_event_count = 88u;
    sentinel.last_sequence = 99u;
    sentinel.owner_lease = 123u;
    sentinel.requested_mode =
        AvrcpAbsoluteVolumeGateMode::Synchronize;
    const auto original_sentinel = sentinel;
    auto tampered = log;
    tampered.events[1].sequence = 99u;
    CHECK(ReplayAvrcpVolumeObserveLog(tampered, &sentinel) ==
          AvrcpVolumeObserveStatus::InvalidArgument);
    CHECK(SameSnapshot(sentinel, original_sentinel));

    tampered = log;
    tampered.events[1].generation = 8u;
    CHECK(ReplayAvrcpVolumeObserveLog(tampered, &sentinel) ==
          AvrcpVolumeObserveStatus::StaleGeneration);
    tampered = log;
    tampered.events[1].windows_volume.percent = 101u;
    CHECK(ReplayAvrcpVolumeObserveLog(tampered, &sentinel) ==
          AvrcpVolumeObserveStatus::OutOfRange);
    tampered = log;
    tampered.events[1].kind =
        static_cast<AvrcpVolumeObserveEventKind>(999u);
    CHECK(ReplayAvrcpVolumeObserveLog(tampered, &sentinel) ==
          AvrcpVolumeObserveStatus::InvalidEnum);
    CHECK(SameSnapshot(sentinel, original_sentinel));

    tampered = log;
    tampered.last_generation = 10u;
    CHECK(ReplayAvrcpVolumeObserveLog(tampered, &sentinel) ==
          AvrcpVolumeObserveStatus::InvalidTransition);
    CHECK(SameSnapshot(sentinel, original_sentinel));
    tampered = log;
    tampered.generation_current = false;
    CHECK(ReplayAvrcpVolumeObserveLog(tampered, &sentinel) ==
          AvrcpVolumeObserveStatus::InvalidTransition);
    CHECK(SameSnapshot(sentinel, original_sentinel));

    tampered = log;
    tampered.event_count = kAvrcpVolumeObserveLogCapacity + 1u;
    CHECK(ReplayAvrcpVolumeObserveLog(tampered, &sentinel) ==
          AvrcpVolumeObserveStatus::InvalidArgument);
    CHECK(SameSnapshot(sentinel, original_sentinel));
    CHECK(ReplayAvrcpVolumeObserveLog(log, nullptr) ==
          AvrcpVolumeObserveStatus::InvalidArgument);
}

void TestGenerationWrapAndCapacity() {
    AvrcpVolumeObserveLog log;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    Append(&log, Event(
        AvrcpVolumeObserveEventKind::GenerationStarted, maximum));
    Append(&log, Event(
        AvrcpVolumeObserveEventKind::GenerationEnded, maximum));
    Append(&log, Event(
        AvrcpVolumeObserveEventKind::GenerationStarted, 1u));
    auto stale = Event(AvrcpVolumeObserveEventKind::Capability, maximum);
    CHECK(RecordAvrcpVolumeObserveEvent(&log, stale) ==
          AvrcpVolumeObserveStatus::StaleGeneration);

    AvrcpVolumeObserveSnapshot snapshot;
    CHECK(ReplayAvrcpVolumeObserveLog(log, &snapshot) ==
          AvrcpVolumeObserveStatus::Accepted);
    CHECK(snapshot.generations_started == 2u);
    CHECK(snapshot.generations_ended == 1u);
    CHECK(snapshot.current_generation == 1u);
    CHECK(snapshot.generation_current);
    CHECK(snapshot.emitted_action_count == 0u);

    AvrcpVolumeObserveLog serial_boundary;
    Append(&serial_boundary, Event(
        AvrcpVolumeObserveEventKind::GenerationStarted, 1u));
    Append(&serial_boundary, Event(
        AvrcpVolumeObserveEventKind::GenerationEnded, 1u));
    const std::uint64_t half_range = std::uint64_t{1u} << 63u;
    auto ambiguous = Event(
        AvrcpVolumeObserveEventKind::GenerationStarted,
        1u + half_range);
    CHECK(RecordAvrcpVolumeObserveEvent(&serial_boundary, ambiguous) ==
          AvrcpVolumeObserveStatus::StaleGeneration);
    auto newest = Event(
        AvrcpVolumeObserveEventKind::GenerationStarted,
        half_range);
    CHECK(RecordAvrcpVolumeObserveEvent(&serial_boundary, newest) ==
          AvrcpVolumeObserveStatus::Accepted);

    AvrcpVolumeObserveLog full;
    Append(&full, Event(
        AvrcpVolumeObserveEventKind::GenerationStarted, 1u));
    auto callback = Event(
        AvrcpVolumeObserveEventKind::WindowsVolumeCallback, 1u);
    callback.windows_volume = {50u, false};
    while (full.event_count < kAvrcpVolumeObserveLogCapacity) {
        Append(&full, callback);
    }
    CHECK(full.event_count == kAvrcpVolumeObserveLogCapacity);
    const auto before = full.events[full.event_count - 1u];
    CHECK(RecordAvrcpVolumeObserveEvent(&full, callback) ==
          AvrcpVolumeObserveStatus::CapacityExceeded);
    CHECK(full.event_count == kAvrcpVolumeObserveLogCapacity);
    CHECK(full.events[full.event_count - 1u].sequence == before.sequence);
    CHECK(ReplayAvrcpVolumeObserveLog(full, &snapshot) ==
          AvrcpVolumeObserveStatus::Accepted);
    CHECK(snapshot.replayed_event_count ==
          kAvrcpVolumeObserveLogCapacity);
    CHECK(snapshot.emitted_action_count == 0u);
}

}  // namespace

int main() {
    TestObserveOnlyRecordAndReplay();
    TestRecorderRejectsInvalidInputWithoutMutation();
    TestReplayRejectsTamperingWithoutPublishingPartialSnapshot();
    TestGenerationWrapAndCapacity();
    if (failures != 0) {
        std::fprintf(stderr,
                     "AVRCP ObserveOnly log tests failed: %d.\n",
                     failures);
        return 1;
    }
    std::printf("AVRCP ObserveOnly log tests passed.\n");
    return 0;
}
