// SPDX-License-Identifier: Apache-2.0
#include "../avrcp_absolute_volume_observe_json.h"

#include <array>
#include <cstdio>
#include <cstring>
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

AvrcpVolumeObserveSnapshot ExampleSnapshot() {
    AvrcpVolumeObserveSnapshot value;
    value.replayed_event_count = 7u;
    value.last_sequence = 7u;
    value.generations_started = 1u;
    value.current_generation = 9u;
    value.generation_current = true;
    value.support = AvrcpAbsoluteVolumeSupport::Supported;
    value.capability_event_count = 1u;
    value.windows_volume_observed = true;
    value.windows_volume = {55u, false};
    value.windows_callback_count = 1u;
    value.xm5_volume_observed = true;
    value.xm5_absolute_volume = 71u;
    value.xm5_event = AvrcpXm5VolumeEvent::RemoteNotification;
    value.xm5_command_response_count = 1u;
    value.xm5_remote_notification_count = 1u;
    value.owner_lease = 42u;
    value.lease_acquire_count = 1u;
    value.requested_mode =
        AvrcpAbsoluteVolumeGateMode::Synchronize;
    value.mode_change_count = 1u;
    return value;
}

AvrcpVolumeObserveSnapshot MaximumValidSnapshot() {
    AvrcpVolumeObserveSnapshot value;
    value.replayed_event_count = kAvrcpVolumeObserveLogCapacity;
    value.last_sequence = kAvrcpVolumeObserveLogCapacity;
    value.generations_started = 1u;
    value.current_generation =
        std::numeric_limits<std::uint64_t>::max();
    value.generation_current = true;
    value.support = AvrcpAbsoluteVolumeSupport::Unsupported;
    value.capability_event_count = 1u;
    value.windows_volume_observed = true;
    value.windows_volume = {100u, true};
    value.windows_callback_count = 247u;
    value.xm5_volume_observed = true;
    value.xm5_absolute_volume = 127u;
    value.xm5_event = AvrcpXm5VolumeEvent::CommandResponse;
    value.xm5_command_response_count = 1u;
    value.xm5_remote_notification_count = 1u;
    value.owner_lease = std::numeric_limits<std::uint64_t>::max();
    value.lease_acquire_count = 4u;
    value.requested_mode =
        AvrcpAbsoluteVolumeGateMode::Synchronize;
    value.mode_change_count = 1u;
    return value;
}

void TestStableFieldOrderAndEnumStrings() {
    const auto snapshot = ExampleSnapshot();
    std::array<char,
               kAvrcpVolumeObserveSnapshotMaximumJsonBytes> output{};
    std::size_t written = 0u;
    CHECK(SerializeAvrcpVolumeObserveSnapshotJson(
              snapshot, output.data(), output.size(), &written) ==
          AvrcpVolumeObserveJsonStatus::Succeeded);
    constexpr char expected[] =
        "{\"schema_version\":1,\"replayed_event_count\":7,"
        "\"last_sequence\":7,\"generations_started\":1,"
        "\"generations_ended\":0,\"current_generation\":9,"
        "\"generation_current\":true,\"support\":\"supported\","
        "\"capability_event_count\":1,"
        "\"windows_volume_observed\":true,"
        "\"windows_volume_percent\":55,"
        "\"windows_volume_muted\":false,"
        "\"windows_callback_count\":1,"
        "\"xm5_volume_observed\":true,"
        "\"xm5_absolute_volume\":71,"
        "\"xm5_event\":\"remote_notification\","
        "\"xm5_command_response_count\":1,"
        "\"xm5_remote_notification_count\":1,"
        "\"owner_lease\":42,\"lease_acquire_count\":1,"
        "\"lease_revoke_count\":0,"
        "\"requested_mode\":\"synchronize\","
        "\"mode_change_count\":1,"
        "\"enforced_replay_mode\":\"observe_only\","
        "\"emitted_action_count\":0}";
    CHECK(written == sizeof(expected) - 1u);
    CHECK(std::strcmp(output.data(), expected) == 0);
    CHECK(output[written] == '\0');

    std::array<char,
               kAvrcpVolumeObserveSnapshotMaximumJsonBytes> second{};
    std::size_t second_written = 0u;
    CHECK(SerializeAvrcpVolumeObserveSnapshotJson(
              snapshot, second.data(), second.size(), &second_written) ==
          AvrcpVolumeObserveJsonStatus::Succeeded);
    CHECK(second_written == written);
    CHECK(std::memcmp(output.data(), second.data(), written + 1u) == 0);

    auto maximum = snapshot;
    maximum.owner_lease = std::numeric_limits<std::uint64_t>::max();
    CHECK(SerializeAvrcpVolumeObserveSnapshotJson(
              maximum, second.data(), second.size(), &second_written) ==
          AvrcpVolumeObserveJsonStatus::Succeeded);
    CHECK(std::strstr(
              second.data(),
              "\"owner_lease\":18446744073709551615") != nullptr);
}

void TestExactCapacityAndNoPartialOutput() {
    const auto snapshot = ExampleSnapshot();
    std::array<char,
               kAvrcpVolumeObserveSnapshotMaximumJsonBytes> reference{};
    std::size_t required = 0u;
    CHECK(SerializeAvrcpVolumeObserveSnapshotJson(
              snapshot,
              reference.data(),
              reference.size(),
              &required) == AvrcpVolumeObserveJsonStatus::Succeeded);

    std::array<char,
               kAvrcpVolumeObserveSnapshotMaximumJsonBytes> exact{};
    exact.fill('X');
    std::size_t exact_written = 0u;
    CHECK(SerializeAvrcpVolumeObserveSnapshotJson(
              snapshot, exact.data(), required + 1u, &exact_written) ==
          AvrcpVolumeObserveJsonStatus::Succeeded);
    CHECK(exact_written == required);
    CHECK(exact[required] == '\0');
    CHECK(exact[required + 1u] == 'X');

    std::array<char,
               kAvrcpVolumeObserveSnapshotMaximumJsonBytes> small{};
    small.fill('Q');
    std::size_t unchanged = 12345u;
    CHECK(SerializeAvrcpVolumeObserveSnapshotJson(
              snapshot, small.data(), required, &unchanged) ==
          AvrcpVolumeObserveJsonStatus::BufferTooSmall);
    CHECK(unchanged == 12345u);
    for (char value : small) CHECK(value == 'Q');

    CHECK(SerializeAvrcpVolumeObserveSnapshotJson(
              snapshot, nullptr, 0u, &unchanged) ==
          AvrcpVolumeObserveJsonStatus::InvalidArgument);
    CHECK(unchanged == 12345u);
    CHECK(SerializeAvrcpVolumeObserveSnapshotJson(
              snapshot, small.data(), small.size(), nullptr) ==
          AvrcpVolumeObserveJsonStatus::InvalidArgument);

    const auto maximum = MaximumValidSnapshot();
    std::array<char,
               kAvrcpVolumeObserveSnapshotMaximumJsonBytes> maximum_json{};
    std::size_t maximum_written = 0u;
    CHECK(SerializeAvrcpVolumeObserveSnapshotJson(
              maximum,
              maximum_json.data(),
              maximum_json.size(),
              &maximum_written) ==
          AvrcpVolumeObserveJsonStatus::Succeeded);
    CHECK(maximum_written + 1u <
          kAvrcpVolumeObserveSnapshotMaximumJsonBytes);
    CHECK(std::strstr(
              maximum_json.data(),
              "\"current_generation\":18446744073709551615") !=
          nullptr);
    CHECK(std::strstr(maximum_json.data(),
                      "\"support\":\"unsupported\"") != nullptr);
    CHECK(std::strstr(maximum_json.data(),
                      "\"xm5_absolute_volume\":127") != nullptr);

    std::array<char,
               kAvrcpVolumeObserveSnapshotMaximumJsonBytes> repeated{};
    std::size_t repeated_written = 0u;
    CHECK(SerializeAvrcpVolumeObserveSnapshotJson(
              maximum,
              repeated.data(),
              repeated.size(),
              &repeated_written) ==
          AvrcpVolumeObserveJsonStatus::Succeeded);
    CHECK(repeated_written == maximum_written);
    CHECK(std::memcmp(maximum_json.data(),
                      repeated.data(),
                      maximum_written + 1u) == 0);
}

void ExpectRejectedWithoutOutput(
    const AvrcpVolumeObserveSnapshot& snapshot,
    AvrcpVolumeObserveJsonStatus expected) {
    std::array<char, 1024u> output{};
    output.fill('Z');
    std::size_t written = 9876u;
    CHECK(SerializeAvrcpVolumeObserveSnapshotJson(
              snapshot, output.data(), output.size(), &written) ==
          expected);
    CHECK(written == 9876u);
    for (char value : output) CHECK(value == 'Z');
}

void TestInvalidEnumsAndSnapshotBoundsFailClosed() {
    auto value = ExampleSnapshot();
    value.support = static_cast<AvrcpAbsoluteVolumeSupport>(99u);
    ExpectRejectedWithoutOutput(
        value, AvrcpVolumeObserveJsonStatus::InvalidEnum);
    value = ExampleSnapshot();
    value.xm5_event = static_cast<AvrcpXm5VolumeEvent>(99u);
    ExpectRejectedWithoutOutput(
        value, AvrcpVolumeObserveJsonStatus::InvalidEnum);
    value = ExampleSnapshot();
    value.requested_mode =
        static_cast<AvrcpAbsoluteVolumeGateMode>(99u);
    ExpectRejectedWithoutOutput(
        value, AvrcpVolumeObserveJsonStatus::InvalidEnum);
    value = ExampleSnapshot();
    value.enforced_replay_mode =
        static_cast<AvrcpAbsoluteVolumeGateMode>(99u);
    ExpectRejectedWithoutOutput(
        value, AvrcpVolumeObserveJsonStatus::InvalidEnum);

    value = ExampleSnapshot();
    value.windows_volume.percent = 101u;
    ExpectRejectedWithoutOutput(
        value, AvrcpVolumeObserveJsonStatus::OutOfRange);
    value = ExampleSnapshot();
    value.xm5_absolute_volume = 128u;
    ExpectRejectedWithoutOutput(
        value, AvrcpVolumeObserveJsonStatus::OutOfRange);
    value = ExampleSnapshot();
    value.replayed_event_count =
        kAvrcpVolumeObserveLogCapacity + 1u;
    ExpectRejectedWithoutOutput(
        value, AvrcpVolumeObserveJsonStatus::OutOfRange);
    value = ExampleSnapshot();
    value.schema_version = 2u;
    ExpectRejectedWithoutOutput(
        value, AvrcpVolumeObserveJsonStatus::OutOfRange);
}

void TestInconsistentOrWritableSnapshotsFailClosed() {
    auto value = ExampleSnapshot();
    value.last_sequence = 6u;
    ExpectRejectedWithoutOutput(
        value, AvrcpVolumeObserveJsonStatus::InvalidSnapshot);
    value = ExampleSnapshot();
    ++value.windows_callback_count;
    ExpectRejectedWithoutOutput(
        value, AvrcpVolumeObserveJsonStatus::InvalidSnapshot);
    value = ExampleSnapshot();
    value.generations_ended = 2u;
    ExpectRejectedWithoutOutput(
        value, AvrcpVolumeObserveJsonStatus::InvalidSnapshot);
    value = ExampleSnapshot();
    value.generation_current = false;
    value.owner_lease = 0u;
    value.requested_mode =
        AvrcpAbsoluteVolumeGateMode::ObserveOnly;
    ExpectRejectedWithoutOutput(
        value, AvrcpVolumeObserveJsonStatus::InvalidSnapshot);
    value = ExampleSnapshot();
    value.enforced_replay_mode =
        AvrcpAbsoluteVolumeGateMode::Synchronize;
    ExpectRejectedWithoutOutput(
        value, AvrcpVolumeObserveJsonStatus::InvalidSnapshot);
    value = ExampleSnapshot();
    value.emitted_action_count = 1u;
    ExpectRejectedWithoutOutput(
        value, AvrcpVolumeObserveJsonStatus::InvalidSnapshot);
    value = ExampleSnapshot();
    value.capability_event_count = 0u;
    ++value.mode_change_count;
    ExpectRejectedWithoutOutput(
        value, AvrcpVolumeObserveJsonStatus::InvalidSnapshot);
    value = ExampleSnapshot();
    value.lease_revoke_count = 2u;
    value.replayed_event_count = 9u;
    value.last_sequence = 9u;
    ExpectRejectedWithoutOutput(
        value, AvrcpVolumeObserveJsonStatus::InvalidSnapshot);
    value = ExampleSnapshot();
    value.lease_revoke_count = 1u;
    ++value.replayed_event_count;
    ++value.last_sequence;
    ExpectRejectedWithoutOutput(
        value, AvrcpVolumeObserveJsonStatus::InvalidSnapshot);

    AvrcpVolumeObserveSnapshot empty;
    std::array<char, 1024u> output{};
    std::size_t written = 0u;
    CHECK(SerializeAvrcpVolumeObserveSnapshotJson(
              empty, output.data(), output.size(), &written) ==
          AvrcpVolumeObserveJsonStatus::Succeeded);
    CHECK(output[written] == '\0');
    CHECK(std::strstr(output.data(), "\"support\":\"unknown\"") !=
          nullptr);
    CHECK(std::strstr(output.data(),
                      "\"xm5_event\":\"command_response\"") !=
          nullptr);
}

}  // namespace

int main() {
    TestStableFieldOrderAndEnumStrings();
    TestExactCapacityAndNoPartialOutput();
    TestInvalidEnumsAndSnapshotBoundsFailClosed();
    TestInconsistentOrWritableSnapshotsFailClosed();
    if (failures != 0) {
        std::fprintf(stderr,
                     "AVRCP ObserveOnly JSON tests failed: %d.\n",
                     failures);
        return 1;
    }
    std::printf("AVRCP ObserveOnly JSON tests passed.\n");
    return 0;
}
