// SPDX-License-Identifier: Apache-2.0
#include "avrcp_absolute_volume_observe_json.h"

#include <array>
#include <cstring>

namespace native_ldac::agent {
namespace {

const char* SupportName(AvrcpAbsoluteVolumeSupport value) {
    switch (value) {
        case AvrcpAbsoluteVolumeSupport::Unknown:
            return "unknown";
        case AvrcpAbsoluteVolumeSupport::Unsupported:
            return "unsupported";
        case AvrcpAbsoluteVolumeSupport::Supported:
            return "supported";
    }
    return nullptr;
}

const char* Xm5EventName(AvrcpXm5VolumeEvent value) {
    switch (value) {
        case AvrcpXm5VolumeEvent::CommandResponse:
            return "command_response";
        case AvrcpXm5VolumeEvent::RemoteNotification:
            return "remote_notification";
    }
    return nullptr;
}

const char* GateModeName(AvrcpAbsoluteVolumeGateMode value) {
    switch (value) {
        case AvrcpAbsoluteVolumeGateMode::ObserveOnly:
            return "observe_only";
        case AvrcpAbsoluteVolumeGateMode::Synchronize:
            return "synchronize";
    }
    return nullptr;
}

class FixedJsonWriter {
public:
    explicit FixedJsonWriter(
        std::array<char,
                   kAvrcpVolumeObserveSnapshotMaximumJsonBytes>* storage)
        : storage_(storage) {}

    void Character(char value) {
        if (!Reserve(1u)) return;
        (*storage_)[size_++] = value;
    }

    void Text(const char* value) {
        if (value == nullptr) {
            overflow_ = true;
            return;
        }
        const std::size_t length = std::strlen(value);
        if (!Reserve(length)) return;
        std::memcpy(storage_->data() + size_, value, length);
        size_ += length;
    }

    void Unsigned(std::uint64_t value) {
        char digits[20]{};
        std::size_t count = 0u;
        do {
            digits[count++] = static_cast<char>('0' + value % 10u);
            value /= 10u;
        } while (value != 0u);
        if (!Reserve(count)) return;
        while (count != 0u) {
            (*storage_)[size_++] = digits[--count];
        }
    }

    void Boolean(bool value) {
        Text(value ? "true" : "false");
    }

    void Key(const char* value, bool* first) {
        if (first == nullptr) {
            overflow_ = true;
            return;
        }
        if (!*first) Character(',');
        *first = false;
        Character('"');
        Text(value);
        Text("\":");
    }

    void String(const char* value) {
        Character('"');
        Text(value);
        Character('"');
    }

    bool overflow() const { return overflow_; }
    std::size_t size() const { return size_; }

private:
    bool Reserve(std::size_t count) {
        if (overflow_ || storage_ == nullptr ||
            count > storage_->size() - size_) {
            overflow_ = true;
            return false;
        }
        return true;
    }

    std::array<char,
               kAvrcpVolumeObserveSnapshotMaximumJsonBytes>* storage_ =
        nullptr;
    std::size_t size_ = 0u;
    bool overflow_ = false;
};

AvrcpVolumeObserveJsonStatus ValidateSnapshot(
    const AvrcpVolumeObserveSnapshot& value) {
    if (SupportName(value.support) == nullptr ||
        Xm5EventName(value.xm5_event) == nullptr ||
        GateModeName(value.requested_mode) == nullptr ||
        GateModeName(value.enforced_replay_mode) == nullptr) {
        return AvrcpVolumeObserveJsonStatus::InvalidEnum;
    }
    if (value.schema_version != kAvrcpVolumeObserveSnapshotSchema ||
        value.replayed_event_count > kAvrcpVolumeObserveLogCapacity ||
        value.generations_started > value.replayed_event_count ||
        value.generations_ended > value.replayed_event_count ||
        value.capability_event_count > value.replayed_event_count ||
        value.windows_callback_count > value.replayed_event_count ||
        value.xm5_command_response_count > value.replayed_event_count ||
        value.xm5_remote_notification_count >
            value.replayed_event_count ||
        value.lease_acquire_count > value.replayed_event_count ||
        value.lease_revoke_count > value.replayed_event_count ||
        value.mode_change_count > value.replayed_event_count ||
        value.windows_volume.percent > 100u ||
        value.xm5_absolute_volume > 127u) {
        return AvrcpVolumeObserveJsonStatus::OutOfRange;
    }
    const std::uint64_t categorized_events =
        value.generations_started + value.generations_ended +
        value.capability_event_count + value.windows_callback_count +
        value.xm5_command_response_count +
        value.xm5_remote_notification_count +
        value.lease_acquire_count + value.lease_revoke_count +
        value.mode_change_count;
    if (categorized_events != value.replayed_event_count ||
        value.last_sequence != value.replayed_event_count ||
        value.generations_ended > value.generations_started ||
        (value.replayed_event_count != 0u &&
         value.generations_started == 0u) ||
        (value.generations_started == 0u &&
         value.current_generation != 0u) ||
        (value.generations_started != 0u &&
         value.current_generation == 0u) ||
        (value.generation_current && value.current_generation == 0u) ||
        (value.generation_current &&
         value.generations_started <= value.generations_ended) ||
        (!value.generation_current &&
         value.generations_started != 0u &&
         value.generations_ended == 0u) ||
        (value.support != AvrcpAbsoluteVolumeSupport::Unknown &&
         value.capability_event_count == 0u) ||
        (value.windows_volume_observed &&
         value.windows_callback_count == 0u) ||
        (value.xm5_volume_observed &&
         value.xm5_command_response_count == 0u &&
         value.xm5_remote_notification_count == 0u) ||
        value.lease_revoke_count > value.lease_acquire_count ||
        (value.owner_lease != 0u &&
         value.lease_acquire_count <= value.lease_revoke_count) ||
        (value.requested_mode ==
             AvrcpAbsoluteVolumeGateMode::Synchronize &&
         value.mode_change_count == 0u) ||
        (!value.generation_current && value.owner_lease != 0u) ||
        (!value.generation_current &&
         value.requested_mode !=
             AvrcpAbsoluteVolumeGateMode::ObserveOnly) ||
        value.enforced_replay_mode !=
            AvrcpAbsoluteVolumeGateMode::ObserveOnly ||
        value.emitted_action_count != 0u) {
        return AvrcpVolumeObserveJsonStatus::InvalidSnapshot;
    }
    return AvrcpVolumeObserveJsonStatus::Succeeded;
}

}  // namespace

AvrcpVolumeObserveJsonStatus SerializeAvrcpVolumeObserveSnapshotJson(
    const AvrcpVolumeObserveSnapshot& snapshot,
    char* output,
    std::size_t output_capacity,
    std::size_t* bytes_written) {
    if (output == nullptr || bytes_written == nullptr ||
        output_capacity == 0u) {
        return AvrcpVolumeObserveJsonStatus::InvalidArgument;
    }
    const auto validation = ValidateSnapshot(snapshot);
    if (validation != AvrcpVolumeObserveJsonStatus::Succeeded) {
        return validation;
    }

    std::array<char,
               kAvrcpVolumeObserveSnapshotMaximumJsonBytes> staging{};
    FixedJsonWriter writer(&staging);
    bool first = true;
    writer.Character('{');
#define JSON_UNSIGNED(field)                     \
    writer.Key(#field, &first);                  \
    writer.Unsigned(snapshot.field)
#define JSON_BOOLEAN(field)                      \
    writer.Key(#field, &first);                  \
    writer.Boolean(snapshot.field)
    JSON_UNSIGNED(schema_version);
    JSON_UNSIGNED(replayed_event_count);
    JSON_UNSIGNED(last_sequence);
    JSON_UNSIGNED(generations_started);
    JSON_UNSIGNED(generations_ended);
    JSON_UNSIGNED(current_generation);
    JSON_BOOLEAN(generation_current);
    writer.Key("support", &first);
    writer.String(SupportName(snapshot.support));
    JSON_UNSIGNED(capability_event_count);
    JSON_BOOLEAN(windows_volume_observed);
    writer.Key("windows_volume_percent", &first);
    writer.Unsigned(snapshot.windows_volume.percent);
    writer.Key("windows_volume_muted", &first);
    writer.Boolean(snapshot.windows_volume.muted);
    JSON_UNSIGNED(windows_callback_count);
    JSON_BOOLEAN(xm5_volume_observed);
    JSON_UNSIGNED(xm5_absolute_volume);
    writer.Key("xm5_event", &first);
    writer.String(Xm5EventName(snapshot.xm5_event));
    JSON_UNSIGNED(xm5_command_response_count);
    JSON_UNSIGNED(xm5_remote_notification_count);
    JSON_UNSIGNED(owner_lease);
    JSON_UNSIGNED(lease_acquire_count);
    JSON_UNSIGNED(lease_revoke_count);
    writer.Key("requested_mode", &first);
    writer.String(GateModeName(snapshot.requested_mode));
    JSON_UNSIGNED(mode_change_count);
    writer.Key("enforced_replay_mode", &first);
    writer.String(GateModeName(snapshot.enforced_replay_mode));
    JSON_UNSIGNED(emitted_action_count);
#undef JSON_BOOLEAN
#undef JSON_UNSIGNED
    writer.Character('}');

    if (writer.overflow()) {
        return AvrcpVolumeObserveJsonStatus::InternalOverflow;
    }
    if (output_capacity <= writer.size()) {
        return AvrcpVolumeObserveJsonStatus::BufferTooSmall;
    }
    std::memcpy(output, staging.data(), writer.size());
    output[writer.size()] = '\0';
    *bytes_written = writer.size();
    return AvrcpVolumeObserveJsonStatus::Succeeded;
}

}  // namespace native_ldac::agent
