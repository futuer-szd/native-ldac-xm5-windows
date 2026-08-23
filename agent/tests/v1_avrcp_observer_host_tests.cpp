#include "../v1_avrcp_observer_host.h"

#include <cstdio>
#include <deque>
#include <vector>

namespace {

void Check(bool condition, const char* message, int* failures) {
    if (!condition) {
        std::fprintf(stderr, "v1_avrcp_observer_host_tests: %s\n", message);
        ++*failures;
    }
}

class FakeIo final : public native_ldac::agent::V1AvrcpObserverIo,
                     public native_ldac::agent::V1AvrcpBluetoothWriter {
public:
    bool OpenReadOnly(DWORD* error) override {
        ++open_calls;
        if (!open_success) {
            if (error != nullptr) *error = open_error;
            return false;
        }
        opened = true;
        if (error != nullptr) *error = ERROR_SUCCESS;
        return true;
    }

    bool OpenReadWrite(DWORD* error) override {
        ++open_rw_calls;
        if (!open_success) {
            if (error != nullptr) *error = open_error;
            return false;
        }
        opened = true;
        if (error != nullptr) *error = ERROR_SUCCESS;
        return true;
    }

    void Close() override { opened = false; }

    bool GetVersion(NLD_AVRCP_OBSERVER_ABI_VERSION* output,
                    DWORD* error) override {
        if (!opened || output == nullptr) {
            if (error != nullptr) *error = ERROR_INVALID_HANDLE;
            return false;
        }
        *output = version;
        if (error != nullptr) *error = ERROR_SUCCESS;
        return true;
    }

    bool GetStatus(NLD_AVRCP_OBSERVER_STATUS* output,
                   DWORD* error) override {
        if (!opened || output == nullptr) {
            if (error != nullptr) *error = ERROR_INVALID_HANDLE;
            return false;
        }
        *output = status;
        if (error != nullptr) *error = ERROR_SUCCESS;
        return true;
    }

    bool BeginObservation(DWORD* error) override {
        ++begin_calls;
        if (!begin_success) {
            if (error != nullptr) *error = begin_error;
            return false;
        }
        status.Flags &= ~NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUIRED;
        status.Flags |= NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUESTED;
        if (error != nullptr) *error = ERROR_SUCCESS;
        return true;
    }

    bool SendCommand(ULONG pdu,
                     ULONG response,
                     const UCHAR* parameters,
                     ULONG parameter_size,
                     DWORD* error) override {
        ++send_calls;
        sent_pdu = pdu;
        sent_response = response;
        sent_parameter_size = parameter_size;
        sent_parameters.clear();
        if (parameters != nullptr && parameter_size > 0u) {
            for (ULONG index = 0u; index < parameter_size; ++index) {
                sent_parameters.push_back(parameters[index]);
            }
        }
        if (!send_success) {
            if (error != nullptr) *error = send_error;
            return false;
        }
        if (error != nullptr) *error = ERROR_SUCCESS;
        return true;
    }

    bool WriteAvrcp(ULONG pdu,
                    ULONG response,
                    const UCHAR* parameters,
                    ULONG parameter_size) override {
        DWORD error = ERROR_SUCCESS;
        return SendCommand(pdu, response, parameters, parameter_size, &error);
    }

    native_ldac::agent::V1AvrcpBluetoothWriter* AsWriter() override {
        return this;
    }

    bool Dequeue(NLD_AVRCP_OBSERVER_EVENT* output,
                 DWORD* error) override {
        if (!opened || output == nullptr) {
            if (error != nullptr) *error = ERROR_INVALID_HANDLE;
            return false;
        }
        if (events.empty()) {
            if (error != nullptr) *error = ERROR_NO_MORE_ITEMS;
            return false;
        }
        *output = events.front();
        events.pop_front();
        if (error != nullptr) *error = ERROR_SUCCESS;
        return true;
    }

    bool open_success = true;
    DWORD open_error = ERROR_FILE_NOT_FOUND;
    bool begin_success = true;
    DWORD begin_error = ERROR_BUSY;
    bool send_success = true;
    DWORD send_error = ERROR_BUSY;
    bool opened = false;
    unsigned int open_calls = 0u;
    unsigned int open_rw_calls = 0u;
    unsigned int begin_calls = 0u;
    unsigned int send_calls = 0u;
    ULONG sent_pdu = 0u;
    ULONG sent_response = 0u;
    ULONG sent_parameter_size = 0u;
    std::vector<UCHAR> sent_parameters;
    NLD_AVRCP_OBSERVER_ABI_VERSION version{
        static_cast<ULONG>(sizeof(NLD_AVRCP_OBSERVER_ABI_VERSION)),
        NLD_AVRCP_OBSERVER_ABI_MAJOR,
        NLD_AVRCP_OBSERVER_ABI_MINOR,
        0u};
    NLD_AVRCP_OBSERVER_STATUS status{
        static_cast<ULONG>(sizeof(NLD_AVRCP_OBSERVER_STATUS)),
        NLD_AVRCP_OBSERVER_STATUS_OUTBOUND_OPEN |
            NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUIRED,
        7u};
    std::deque<NLD_AVRCP_OBSERVER_EVENT> events;
};

class RecordingSink final : public native_ldac::agent::V1AvrcpActionSink {
public:
    bool Handle(const native_ldac::agent::V1AvrcpActionSet& actions) override {
        ++handled;
        action_sets.push_back(actions);
        return true;
    }

    bool QueryWindowsVolume(
        native_ldac::agent::AvrcpWindowsVolume* volume) override {
        if (volume == nullptr) return false;
        ++queries;
        *volume = {40u, false};
        return true;
    }

    bool WindowsVolumeNotificationsActive() const override {
        return notifications_active;
    }

    bool ConsumeWindowsVolumeChange(
        native_ldac::agent::AvrcpWindowsVolume* volume) override {
        if (volume == nullptr || !notification_pending) return false;
        *volume = pending_volume;
        notification_pending = false;
        return true;
    }

    void RetryPendingWrites() override { ++write_retries; }

    unsigned int handled = 0u;
    unsigned int queries = 0u;
    unsigned int write_retries = 0u;
    bool notifications_active = false;
    bool notification_pending = false;
    native_ldac::agent::AvrcpWindowsVolume pending_volume{};
    std::vector<native_ldac::agent::V1AvrcpActionSet> action_sets;
};

NLD_AVRCP_OBSERVER_EVENT MakeEvent(ULONG type,
                                    ULONGLONG generation,
                                    ULONG value = 0u,
                                    ULONG flags = 0u) {
    NLD_AVRCP_OBSERVER_EVENT event{};
    event.Size = sizeof(event);
    event.Type = type;
    event.AclGeneration = generation;
    event.Value0 = value;
    event.Flags = flags;
    return event;
}

void TestMediaScopedActivation(int* failures) {
    FakeIo io;
    RecordingSink sink;
    native_ldac::agent::V1AvrcpObserverHost host(&io);
    native_ldac::agent::V1AvrcpReplayOptions options;
    options.volume_sync = true;
    options.media_routing = false;
    options.headset_preferred = true;
    options.acl_generation = 100u;

    DWORD error = ERROR_SUCCESS;
    Check(host.Poll(&error), "idle poll should be harmless", failures);
    Check(io.open_calls == 0u && io.begin_calls == 0u,
          "idle host must not open or activate observer", failures);

    const auto activation = host.BeginMediaSession(options, &sink, &error);
    Check(activation == native_ldac::agent::V1AvrcpObserverActivationResult::Active,
          "post-media activation should succeed", failures);
    Check(io.open_calls == 1u && io.begin_calls == 1u,
          "media start must issue exactly one observer activation", failures);
    Check(host.physical_acl_generation() == 100u,
          "host must use the physical ACL generation supplied by daily host",
          failures);

    io.events.push_back(MakeEvent(NldAvrcpObserverEventAclConnected, 7u));
    io.events.push_back(MakeEvent(
        NldAvrcpObserverEventVolumeCapability, 7u, 1u));
    io.events.push_back(MakeEvent(
        NldAvrcpObserverEventAbsoluteVolume,
        7u,
        64u,
        NLD_AVRCP_EVENT_FLAG_CHANGED));
    io.status.Flags =
        NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUESTED |
        NLD_AVRCP_OBSERVER_STATUS_CHANNEL_OPEN |
        NLD_AVRCP_OBSERVER_STATUS_VOLUME_SUPPORTED |
        NLD_AVRCP_OBSERVER_STATUS_OBSERVING;
    Check(host.Poll(&error), "observer event drain should succeed", failures);
    Check(host.observer_generation() == 7u,
          "host should retain PDO generation", failures);
    bool saw_windows_volume = false;
    for (const auto& actions : sink.action_sets) {
        saw_windows_volume = saw_windows_volume ||
            native_ldac::agent::V1AvrcpHasAction(
                actions,
                native_ldac::agent::V1AvrcpActionSetWindowsVolume);
    }
    Check(sink.handled >= 1u && saw_windows_volume,
          "headset absolute volume should dispatch through current lease",
          failures);
    Check(host.headset_initial_sync_complete(),
          "first XM5 volume must complete initial headset authority",
          failures);
    Check(host.control_channel_ready() && host.single_gain_ready(),
          "single gain must wait for control readiness and initial XM5 volume",
          failures);
    Check(sink.write_retries == 1u,
          "control readiness must trigger one immediate pending-write retry",
          failures);
    Check(host.Poll(&error) && sink.write_retries == 1u,
          "idle observer polls must not retry or hammer the writer", failures);
    io.events.push_back(MakeEvent(
        NldAvrcpObserverEventWriteResponse, 7u, 0x50u));
    Check(host.Poll(&error) && sink.write_retries == 2u,
          "write response must immediately release the latest pending write",
          failures);
    io.status.Flags |= NLD_AVRCP_OBSERVER_STATUS_OPEN_PENDING;
    Check(host.Poll(&error) && !host.control_channel_ready() &&
              !host.single_gain_ready(),
          "pending/disconnected control status must revoke single gain",
          failures);
    io.status.Flags &= ~NLD_AVRCP_OBSERVER_STATUS_OPEN_PENDING;

    host.EndMediaSession();
    Check(!host.media_session_active(),
          "media stop must revoke the active observer lease", failures);
    const auto second_activation =
        host.BeginMediaSession(options, &sink, &error);
    Check(second_activation ==
              native_ldac::agent::V1AvrcpObserverActivationResult::Active &&
              io.begin_calls == 1u,
          "same PDO generation must reuse observer without a second OPEN",
          failures);

    const std::size_t actions_before_same_generation = sink.action_sets.size();
    io.events.push_back(MakeEvent(
        NldAvrcpObserverEventAbsoluteVolume,
        7u,
        72u,
        NLD_AVRCP_EVENT_FLAG_CHANGED));
    Check(host.Poll(&error),
          "same-generation XM5 volume should remain routable", failures);
    bool repeated_initial_sync = false;
    for (std::size_t index = actions_before_same_generation;
         index < sink.action_sets.size();
         ++index) {
        repeated_initial_sync = repeated_initial_sync ||
            native_ldac::agent::V1AvrcpHasAction(
                sink.action_sets[index],
                native_ldac::agent::V1AvrcpActionSetWindowsVolume);
    }
    Check(repeated_initial_sync && host.headset_initial_sync_complete(),
          "later XM5 changes must sync without resetting initial authority",
          failures);

    host.ReleaseTransport();
    Check(!host.observer_open() &&
              host.physical_acl_generation() == 100u &&
              host.headset_initial_sync_complete(),
          "transport release must preserve physical-generation authority",
          failures);
    io.status.Flags = NLD_AVRCP_OBSERVER_STATUS_OUTBOUND_OPEN |
        NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUIRED;
    const auto reopened = host.BeginMediaSession(options, &sink, &error);
    Check(reopened ==
              native_ldac::agent::V1AvrcpObserverActivationResult::Active &&
              io.open_calls == 2u && io.begin_calls == 2u,
          "owner reacquisition must reopen and reactivate the observer",
          failures);
    Check(host.headset_initial_sync_complete(),
          "same physical ACL must not regain first-volume authority",
          failures);

    host.ReleaseTransport();
    options.acl_generation = 101u;
    io.status.Flags = NLD_AVRCP_OBSERVER_STATUS_OUTBOUND_OPEN |
        NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUIRED;
    const auto next_generation =
        host.BeginMediaSession(options, &sink, &error);
    Check(next_generation ==
              native_ldac::agent::V1AvrcpObserverActivationResult::Active &&
              host.physical_acl_generation() == 101u &&
              !host.headset_initial_sync_complete(),
          "new physical ACL must reset initial XM5 authority", failures);
    const std::size_t actions_before_new_generation = sink.action_sets.size();
    io.events.push_back(MakeEvent(
        NldAvrcpObserverEventAbsoluteVolume,
        7u,
        24u,
        NLD_AVRCP_EVENT_FLAG_CHANGED));
    Check(host.Poll(&error),
          "new physical generation event should be accepted", failures);
    bool saw_new_generation_sync = false;
    for (std::size_t index = actions_before_new_generation;
         index < sink.action_sets.size();
         ++index) {
        saw_new_generation_sync = saw_new_generation_sync ||
            native_ldac::agent::V1AvrcpHasAction(
                sink.action_sets[index],
                native_ldac::agent::V1AvrcpActionSetWindowsVolume);
    }
    Check(saw_new_generation_sync && host.headset_initial_sync_complete(),
          "driver PDO generation must not suppress new physical-ACL sync",
          failures);

    host.Close();
    Check(host.physical_acl_generation() == 0u &&
              !host.headset_initial_sync_complete(),
          "physical disconnect close must end headset authority", failures);
}

void TestUnavailableAndIncompatible(int* failures) {
    FakeIo missing;
    missing.open_success = false;
    missing.open_error = ERROR_FILE_NOT_FOUND;
    native_ldac::agent::V1AvrcpObserverHost missing_host(&missing);
    native_ldac::agent::V1AvrcpReplayOptions options;
    DWORD error = ERROR_SUCCESS;
    Check(missing_host.BeginMediaSession(options, nullptr, &error) ==
              native_ldac::agent::V1AvrcpObserverActivationResult::Unavailable,
          "missing PDO interface should degrade without activation", failures);
    Check(missing.begin_calls == 0u,
          "missing PDO must not consume a BEGIN_OBSERVATION attempt", failures);

    FakeIo pending;
    pending.status.Flags &= ~NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUIRED;
    native_ldac::agent::V1AvrcpObserverHost pending_host(&pending);
    Check(pending_host.BeginMediaSession(options, nullptr, &error) ==
              native_ldac::agent::V1AvrcpObserverActivationResult::Pending,
          "profile setup without activation readiness must stay pending",
          failures);
    Check(pending.begin_calls == 0u && !pending_host.media_session_active(),
          "not-ready observer must not consume or retain a media lease",
          failures);

    FakeIo incompatible;
    incompatible.version.Minor = NLD_AVRCP_OBSERVER_ABI_MINOR - 1u;
    native_ldac::agent::V1AvrcpObserverHost incompatible_host(&incompatible);
    Check(incompatible_host.BeginMediaSession(options, nullptr, &error) ==
              native_ldac::agent::V1AvrcpObserverActivationResult::Incompatible,
          "wrong observer ABI must be rejected", failures);
    Check(incompatible.begin_calls == 0u,
          "wrong observer ABI must not activate the channel", failures);
}

void TestEventDrivenWindowsVolume(int* failures) {
    FakeIo io;
    RecordingSink sink;
    sink.notifications_active = true;
    native_ldac::agent::V1AvrcpObserverHost host(&io, true);
    native_ldac::agent::V1AvrcpReplayOptions options;
    options.acl_generation = 200u;
    options.volume_sync = true;
    options.media_routing = false;
    options.headset_preferred = false;
    DWORD error = ERROR_SUCCESS;
    Check(host.BeginMediaSession(options, &sink, &error) ==
              native_ldac::agent::V1AvrcpObserverActivationResult::Active,
          "notification-mode activation should succeed", failures);
    io.events.push_back(MakeEvent(
        NldAvrcpObserverEventVolumeCapability, 7u, 1u));
    Check(host.Poll(&error),
          "notification-mode capability drain should succeed", failures);
    const unsigned int initial_queries = sink.queries;
    sink.pending_volume = {65u, false};
    sink.notification_pending = true;
    Check(host.Poll(&error),
          "endpoint notification should be consumed", failures);
    Check(host.stats().windows_volume_notifications == 1u,
          "endpoint notification was not counted", failures);
    Check(host.stats().windows_volume_polls == 0u &&
              sink.queries == initial_queries,
          "active notifications must suppress getter polling", failures);
    bool saw_projected_xm5_volume = false;
    for (const auto& actions : sink.action_sets) {
        saw_projected_xm5_volume = saw_projected_xm5_volume ||
            (native_ldac::agent::V1AvrcpHasAction(
                 actions,
                 native_ldac::agent::V1AvrcpActionSendXm5Volume) &&
             actions.xm5_absolute_volume == 83u);
    }
    Check(saw_projected_xm5_volume,
          "Windows notification did not produce projected XM5 volume",
          failures);
}

void TestMediaSessionEligibilityRouting(int* failures) {
    FakeIo io;
    RecordingSink sink;
    native_ldac::agent::V1AvrcpObserverHost host(&io);
    native_ldac::agent::V1AvrcpReplayOptions options;
    options.acl_generation = 300u;
    options.volume_sync = false;
    options.media_routing = true;
    options.media_session = {
        300u,
        native_ldac::agent::V1MediaSessionPlayback::Paused,
        true,
        false,
        true,
        true};
    DWORD error = ERROR_SUCCESS;
    Check(host.BeginMediaSession(options, &sink, &error) ==
              native_ldac::agent::V1AvrcpObserverActivationResult::Active,
          "media-eligibility activation should succeed", failures);
    io.events.push_back(MakeEvent(
        NldAvrcpObserverEventPassThrough, 7u, 0x44u));
    Check(host.Poll(&error), "paused play event should drain", failures);
    bool saw_play = false;
    for (const auto& actions : sink.action_sets) {
        saw_play = saw_play || native_ldac::agent::V1AvrcpHasAction(
            actions, native_ldac::agent::V1AvrcpActionMediaPlay);
    }
    Check(saw_play, "paused eligible session did not route play", failures);

    io.events.push_back(MakeEvent(
        NldAvrcpObserverEventPassThrough,
        7u,
        0x44u,
        NLD_AVRCP_EVENT_FLAG_RELEASED));
    (void)host.Poll(&error);
    host.SetMediaSessionSnapshot(
        {300u,
         native_ldac::agent::V1MediaSessionPlayback::Absent,
         false,
         false,
         false,
         false});
    const std::size_t actions_before_absent = sink.action_sets.size();
    io.events.push_back(MakeEvent(
        NldAvrcpObserverEventPassThrough, 7u, 0x44u));
    Check(host.Poll(&error), "absent-session event should drain", failures);
    bool absent_play = false;
    for (std::size_t index = actions_before_absent;
         index < sink.action_sets.size();
         ++index) {
        absent_play = absent_play || native_ldac::agent::V1AvrcpHasAction(
            sink.action_sets[index],
            native_ldac::agent::V1AvrcpActionMediaPlay);
    }
    Check(!absent_play,
          "absent Windows media session routed a play gesture", failures);
}

void TestWriteMode(int* failures) {
    FakeIo io;
    RecordingSink sink;
    native_ldac::agent::V1AvrcpObserverHost host(&io, true);
    native_ldac::agent::V1AvrcpReplayOptions options;
    options.volume_sync = true;
    options.media_routing = false;
    options.headset_preferred = false;
    DWORD error = ERROR_SUCCESS;
    const auto activation = host.BeginMediaSession(options, &sink, &error);
    Check(activation ==
              native_ldac::agent::V1AvrcpObserverActivationResult::Active,
          "write-mode activation should succeed", failures);
    Check(io.open_rw_calls == 1u && io.open_calls == 0u,
          "write mode must open read-write, not read-only", failures);
    auto* writer = host.writer();
    Check(writer != nullptr, "write-mode writer bridge missing", failures);
    if (writer != nullptr) {
        const UCHAR volume = 47u;
        Check(writer->WriteAvrcp(0x50u, 0u, &volume, 1u),
              "SEND_COMMAND write failed", failures);
        Check(io.send_calls == 1u && io.sent_pdu == 0x50u &&
                  io.sent_response == 0u && io.sent_parameter_size == 1u &&
                  io.sent_parameters.size() == 1u &&
                  io.sent_parameters[0] == 47u,
              "SEND_COMMAND payload mismatch", failures);
    }
    host.EndMediaSession();

    FakeIo read_io;
    native_ldac::agent::V1AvrcpObserverHost read_host(&read_io);
    Check(read_host.writer() == nullptr,
          "read-only host must not expose a writer bridge", failures);
}

void TestWindowsSinkNotificationSnapshot(int* failures) {
    native_ldac::agent::V1AvrcpWindowsSink sink(false);
    AUDIO_VOLUME_NOTIFICATION_DATA notification{};
    notification.bMuted = TRUE;
    notification.fMasterVolume = 0.42f;
    Check(SUCCEEDED(sink.OnNotify(&notification)),
          "Core Audio callback snapshot should be accepted", failures);
    Check(WaitForSingleObject(sink.volume_change_event(), 0u) ==
              WAIT_OBJECT_0,
          "Core Audio callback did not signal the volume wake event",
          failures);
    native_ldac::agent::AvrcpWindowsVolume observed{};
    Check(sink.ConsumeWindowsVolumeChange(&observed) &&
              observed.percent == 42u && observed.muted,
          "Core Audio callback snapshot value mismatch", failures);
    Check(!sink.ConsumeWindowsVolumeChange(&observed),
          "Core Audio callback snapshot must be consumed once", failures);
    Check(WaitForSingleObject(sink.volume_change_event(), 0u) ==
              WAIT_TIMEOUT,
          "consuming the Core Audio snapshot did not reset its wake event",
          failures);

    void* callback = nullptr;
    Check(SUCCEEDED(sink.QueryInterface(
              __uuidof(IAudioEndpointVolumeCallback), &callback)) &&
              callback != nullptr,
          "Core Audio callback QueryInterface failed", failures);
    if (callback != nullptr) {
        static_cast<IAudioEndpointVolumeCallback*>(callback)->Release();
    }
    void* unsupported = reinterpret_cast<void*>(1u);
    Check(sink.QueryInterface(__uuidof(IMMDevice), &unsupported) ==
              E_NOINTERFACE && unsupported == nullptr,
          "unsupported callback interface was not rejected", failures);
}

void TestWindowsSinkPlaybackStatusWrite(int* failures) {
    FakeIo io;
    native_ldac::agent::V1AvrcpWindowsSink sink(true, &io);
    native_ldac::agent::V1AvrcpActionSet actions;
    actions.actions = native_ldac::agent::V1AvrcpActionNotifyPlaybackStatus;
    actions.playback_changed = true;
    actions.playback_after =
        native_ldac::agent::V1AvrcpPlaybackState::Paused;
    Check(sink.Handle(actions),
          "playback status-only action should be accepted", failures);
    Check(io.send_calls == 1u && io.sent_pdu == 0x31u &&
              io.sent_response == 0x0Du && io.sent_parameter_size == 2u &&
              io.sent_parameters.size() == 2u &&
              io.sent_parameters[0] == 0x01u &&
              io.sent_parameters[1] == 0x02u,
          "paused playback status was not written as AVRCP notification",
          failures);
}

void TestWindowsSinkRetriesPendingPlaybackStatus(int* failures) {
    FakeIo io;
    io.send_success = false;
    native_ldac::agent::V1AvrcpWindowsSink sink(true, &io);
    native_ldac::agent::V1AvrcpActionSet actions;
    actions.actions = native_ldac::agent::V1AvrcpActionNotifyPlaybackStatus;
    actions.playback_changed = true;
    actions.playback_after =
        native_ldac::agent::V1AvrcpPlaybackState::Playing;
    Check(sink.Handle(actions),
          "pending playback status action should remain non-fatal",
          failures);
    Check(io.send_calls == 1u,
          "pending playback status should attempt the initial write",
          failures);
    io.send_success = true;
    sink.RetryPendingWrites();
    Check(io.send_calls == 2u && io.sent_pdu == 0x31u &&
              io.sent_response == 0x0Du && io.sent_parameter_size == 2u &&
              io.sent_parameters.size() == 2u &&
              io.sent_parameters[1] == 0x01u,
          "pending playback status was not retried after writer readiness",
          failures);
}

}  // namespace

int main() {
    int failures = 0;
    TestMediaScopedActivation(&failures);
    TestUnavailableAndIncompatible(&failures);
    TestEventDrivenWindowsVolume(&failures);
    TestMediaSessionEligibilityRouting(&failures);
    TestWriteMode(&failures);
    TestWindowsSinkNotificationSnapshot(&failures);
    TestWindowsSinkPlaybackStatusWrite(&failures);
    TestWindowsSinkRetriesPendingPlaybackStatus(&failures);
    if (failures != 0) return 1;
    std::puts("V1 AVRCP observer host tests passed.");
    return 0;
}
