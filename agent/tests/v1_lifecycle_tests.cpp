#include "../v1_lifecycle.h"
#include "../xm5_acl_event.h"

#include <bthdef.h>
#include <dbt.h>

#include <cstddef>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using native_ldac::agent::HasV1LifecycleAction;
using native_ldac::agent::ReduceV1Lifecycle;
using native_ldac::agent::V1ActionCancelTransport;
using native_ldac::agent::V1ActionFailMute;
using native_ldac::agent::V1ActionGracefulStopTransport;
using native_ldac::agent::V1ActionOpenTransport;
using native_ldac::agent::V1ActionPublishEndpointAbsent;
using native_ldac::agent::V1ActionPublishEndpointPresent;
using native_ldac::agent::V1ActionScheduleTransportRetry;
using native_ldac::agent::V1ActionStartEngine;
using native_ldac::agent::V1ActionStopEngine;
using native_ldac::agent::V1EngineLease;
using native_ldac::agent::V1LifecycleEvent;
using native_ldac::agent::V1LifecycleState;
using native_ldac::agent::V1MediaSession;
using native_ldac::agent::V1PhysicalPresence;
using native_ldac::agent::V1RenderDemand;

int Fail(const char* message) {
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

bool Has(std::uint32_t actions,
         native_ldac::agent::V1LifecycleAction action) {
    return HasV1LifecycleAction(actions, action);
}

native_ldac::agent::Xm5AclEvent ParseAclEvent(
    BTH_ADDR address,
    UCHAR connection_type,
    bool connected) {
    const std::size_t size =
        offsetof(DEV_BROADCAST_HANDLE, dbch_data) +
        sizeof(BTH_HCI_EVENT_INFO);
    std::vector<unsigned char> storage(size, 0u);
    auto* broadcast = reinterpret_cast<DEV_BROADCAST_HANDLE*>(
        storage.data());
    broadcast->dbch_size = static_cast<DWORD>(size);
    broadcast->dbch_devicetype = DBT_DEVTYP_HANDLE;
    broadcast->dbch_eventguid = GUID_BLUETOOTH_HCI_EVENT;
    BTH_HCI_EVENT_INFO event{};
    event.bthAddress = address;
    event.connectionType = connection_type;
    event.connected = connected ? 1u : 0u;
    std::memcpy(broadcast->dbch_data, &event, sizeof(event));
    return native_ldac::agent::ParseXm5AclDeviceChange(
        0x001122334455ull,
        DBT_CUSTOMEVENT,
        reinterpret_cast<LPARAM>(broadcast));
}

}  // namespace

int main() {
    if (native_ldac::agent::GetV1TransportRetryDelayMs(0u) != 0u ||
        native_ldac::agent::GetV1TransportRetryDelayMs(1u) != 15000u ||
        native_ldac::agent::GetV1TransportRetryDelayMs(2u) != 30000u ||
        native_ldac::agent::GetV1TransportRetryDelayMs(3u) != 45000u ||
        native_ldac::agent::GetV1TransportRetryDelayMs(4u) != 0u) {
        return Fail("Bounded transport retry delays changed.");
    }
    if (native_ldac::agent::GetV1PcmTransportRetryDelayMs(0u) != 0u ||
        native_ldac::agent::GetV1PcmTransportRetryDelayMs(1u) != 1000u ||
        native_ldac::agent::GetV1PcmTransportRetryDelayMs(2u) != 2000u ||
        native_ldac::agent::GetV1PcmTransportRetryDelayMs(3u) != 4000u ||
        native_ldac::agent::GetV1PcmTransportRetryDelayMs(4u) != 0u) {
        return Fail("PCM signaling acquisition delays changed.");
    }
    if (ParseAclEvent(0x001122334455ull,
                      HCI_CONNECTION_TYPE_ACL,
                      true) !=
            native_ldac::agent::Xm5AclEvent::Connected ||
        ParseAclEvent(0x001122334455ull,
                      HCI_CONNECTION_TYPE_ACL,
                      false) !=
            native_ldac::agent::Xm5AclEvent::Disconnected ||
        ParseAclEvent(0x001122334454ull,
                      HCI_CONNECTION_TYPE_ACL,
                      true) != native_ldac::agent::Xm5AclEvent::None ||
        ParseAclEvent(0x001122334455ull,
                      HCI_CONNECTION_TYPE_SCO,
                      true) != native_ldac::agent::Xm5AclEvent::None ||
        native_ldac::agent::ParseXm5AclDeviceChange(0x001122334455ull,
                                                    0u, 0) !=
            native_ldac::agent::Xm5AclEvent::None) {
        return Fail("XM5 ACL device-change filtering is incorrect.");
    }

    V1LifecycleState state;

    std::uint32_t actions = ReduceV1Lifecycle(
        &state, V1LifecycleEvent::RenderStarted);
    if (actions != 0u ||
        state.render_demand != V1RenderDemand::Running ||
        state.engine_lease != V1EngineLease::Absent) {
        return Fail("Render demand started an engine without physical presence.");
    }

    actions = ReduceV1Lifecycle(&state, V1LifecycleEvent::AclConnected);
    if (!Has(actions, V1ActionPublishEndpointPresent) ||
        Has(actions, V1ActionStartEngine) ||
        Has(actions, V1ActionOpenTransport) ||
        state.acl_generation != 1u ||
        state.render_demand != V1RenderDemand::Idle) {
        return Fail("ACL connect did more than publish endpoint presence.");
    }

    actions = ReduceV1Lifecycle(&state, V1LifecycleEvent::AclConnected);
    if (actions != 0u || state.acl_generation != 1u) {
        return Fail("Duplicate ACL connect created a fresh generation.");
    }

    V1LifecycleState hfp_preparing{};
    hfp_preparing.allow_multiple_media_sessions = true;
    (void)ReduceV1Lifecycle(
        &hfp_preparing, V1LifecycleEvent::AclConnected);
    actions = ReduceV1Lifecycle(
        &hfp_preparing, V1LifecycleEvent::RenderStarted);
    if (!Has(actions, V1ActionStartEngine)) {
        return Fail("HFP fixture did not stage its engine.");
    }
    actions = ReduceV1Lifecycle(
        &hfp_preparing, V1LifecycleEvent::HfpSuspendLdac);
    if (!Has(actions, V1ActionStopEngine) ||
        Has(actions, V1ActionGracefulStopTransport) ||
        !hfp_preparing.hfp_suspended ||
        hfp_preparing.render_demand != V1RenderDemand::Running) {
        return Fail("HFP suspend did not stop preparation and preserve demand.");
    }
    (void)ReduceV1Lifecycle(
        &hfp_preparing, V1LifecycleEvent::EngineExited);
    actions = ReduceV1Lifecycle(
        &hfp_preparing, V1LifecycleEvent::HfpResumeLdac);
    if (!Has(actions, V1ActionStartEngine) ||
        hfp_preparing.hfp_suspended ||
        hfp_preparing.engine_lease != V1EngineLease::Starting) {
        return Fail("HFP resume did not restart preserved render demand.");
    }

    V1LifecycleState hfp_streaming{};
    hfp_streaming.allow_multiple_media_sessions = true;
    (void)ReduceV1Lifecycle(
        &hfp_streaming, V1LifecycleEvent::AclConnected);
    (void)ReduceV1Lifecycle(
        &hfp_streaming, V1LifecycleEvent::RenderStarted);
    (void)ReduceV1Lifecycle(
        &hfp_streaming, V1LifecycleEvent::EngineReady);
    (void)ReduceV1Lifecycle(
        &hfp_streaming, V1LifecycleEvent::MediaStarted);
    actions = ReduceV1Lifecycle(
        &hfp_streaming, V1LifecycleEvent::HfpSuspendLdac);
    if (!Has(actions, V1ActionGracefulStopTransport) ||
        !Has(actions, V1ActionStopEngine) ||
        hfp_streaming.media_session != V1MediaSession::Stopping ||
        hfp_streaming.render_demand != V1RenderDemand::Running) {
        return Fail("HFP suspend did not gracefully stop streaming LDAC.");
    }
    (void)ReduceV1Lifecycle(
        &hfp_streaming, V1LifecycleEvent::MediaStopped);
    actions = ReduceV1Lifecycle(
        &hfp_streaming, V1LifecycleEvent::HfpResumeLdac);
    if (!Has(actions, V1ActionStartEngine) ||
        hfp_streaming.open_attempts_for_generation != 0u) {
        return Fail("HFP resume did not create a fresh same-generation session.");
    }

    actions = ReduceV1Lifecycle(&state, V1LifecycleEvent::RenderStarted);
    if (!Has(actions, V1ActionStartEngine) ||
        Has(actions, V1ActionOpenTransport) ||
        state.engine_lease != V1EngineLease::Starting) {
        return Fail("Render start did not stage the engine safely.");
    }

    actions = ReduceV1Lifecycle(&state, V1LifecycleEvent::EngineReady);
    if (!Has(actions, V1ActionOpenTransport) ||
        state.engine_lease != V1EngineLease::Ready ||
        state.media_session != V1MediaSession::Opening ||
        state.open_attempts_for_generation != 1u) {
        return Fail("Ready engine did not issue exactly one transport open.");
    }

    V1LifecycleState suppressed_state = state;
    actions = ReduceV1Lifecycle(
        &suppressed_state,
        V1LifecycleEvent::TransportOpenSuppressed);
    if (actions != 0u ||
        suppressed_state.media_session != V1MediaSession::Stopped ||
        suppressed_state.engine_lease != V1EngineLease::Ready ||
        suppressed_state.open_attempts_for_generation != 1u) {
        return Fail("Suppressed transport open did not preserve the ready engine lease.");
    }
    actions = ReduceV1Lifecycle(
        &suppressed_state, V1LifecycleEvent::RenderStopped);
    if (!Has(actions, V1ActionStopEngine) ||
        Has(actions, V1ActionGracefulStopTransport) ||
        Has(actions, V1ActionCancelTransport)) {
        return Fail("Suppressed transport open later attempted a transport stop.");
    }
    actions = ReduceV1Lifecycle(
        &suppressed_state, V1LifecycleEvent::EngineExited);
    if (actions != 0u ||
        suppressed_state.engine_lease != V1EngineLease::Absent) {
        return Fail("Suppressed engine exit did not remain transport-free.");
    }

    V1LifecycleState pcm_gap_state{};
    pcm_gap_state.maximum_open_attempts =
        native_ldac::agent::kV1MaximumPcmTransportOpenAttempts;
    pcm_gap_state.tolerate_pretransport_render_gaps = true;
    (void)ReduceV1Lifecycle(
        &pcm_gap_state, V1LifecycleEvent::AclConnected);
    actions = ReduceV1Lifecycle(
        &pcm_gap_state, V1LifecycleEvent::RenderStarted);
    if (!Has(actions, V1ActionStartEngine) ||
        pcm_gap_state.engine_lease != V1EngineLease::Starting) {
        return Fail("PCM render start did not stage its bounded worker.");
    }
    actions = ReduceV1Lifecycle(
        &pcm_gap_state, V1LifecycleEvent::RenderStopped);
    if (actions != 0u ||
        pcm_gap_state.render_demand != V1RenderDemand::Idle ||
        pcm_gap_state.engine_lease != V1EngineLease::Starting ||
        pcm_gap_state.open_attempts_for_generation != 0u) {
        return Fail("A pre-ready PCM render gap stopped its bounded worker.");
    }
    actions = ReduceV1Lifecycle(
        &pcm_gap_state, V1LifecycleEvent::RenderStarted);
    if (actions != 0u ||
        pcm_gap_state.engine_lease != V1EngineLease::Starting) {
        return Fail("A pre-ready PCM resume started a duplicate worker.");
    }
    (void)ReduceV1Lifecycle(
        &pcm_gap_state, V1LifecycleEvent::EngineReady);
    actions = ReduceV1Lifecycle(
        &pcm_gap_state, V1LifecycleEvent::RenderStopped);
    if (actions != 0u ||
        pcm_gap_state.render_demand != V1RenderDemand::Idle ||
        pcm_gap_state.engine_lease != V1EngineLease::Ready ||
        pcm_gap_state.media_session != V1MediaSession::Opening ||
        pcm_gap_state.open_attempts_for_generation != 1u) {
        return Fail("A pre-START PCM render gap stopped its bounded local wait.");
    }
    actions = ReduceV1Lifecycle(
        &pcm_gap_state, V1LifecycleEvent::RenderStarted);
    if (actions != 0u ||
        pcm_gap_state.engine_lease != V1EngineLease::Ready ||
        pcm_gap_state.media_session != V1MediaSession::Opening) {
        return Fail("A resumed PCM render started a duplicate worker.");
    }
    (void)ReduceV1Lifecycle(
        &pcm_gap_state, V1LifecycleEvent::MediaStarted);
    actions = ReduceV1Lifecycle(
        &pcm_gap_state, V1LifecycleEvent::RenderStopped);
    if (!Has(actions, V1ActionGracefulStopTransport) ||
        !Has(actions, V1ActionStopEngine) ||
        pcm_gap_state.media_session != V1MediaSession::Stopping) {
        return Fail("A post-START PCM render stop was not graceful.");
    }

    actions = ReduceV1Lifecycle(&state, V1LifecycleEvent::EngineReady);
    if (Has(actions, V1ActionOpenTransport)) {
        return Fail("One ACL generation issued a second transport open.");
    }

    V1LifecycleState retry_state = state;
    actions = ReduceV1Lifecycle(
        &retry_state, V1LifecycleEvent::TransportRetryableFailure);
    if (!Has(actions, V1ActionFailMute) ||
        !Has(actions, V1ActionCancelTransport) ||
        !Has(actions, V1ActionStopEngine) ||
        !Has(actions, V1ActionScheduleTransportRetry) ||
        retry_state.media_session != V1MediaSession::Stopped ||
        retry_state.engine_lease != V1EngineLease::Absent ||
        retry_state.open_attempts_for_generation != 1u) {
        return Fail("A retryable OPEN failure was not safely staged.");
    }
    actions = ReduceV1Lifecycle(
        &retry_state, V1LifecycleEvent::TransportRetryDue);
    if (!Has(actions, V1ActionStartEngine) ||
        retry_state.engine_lease != V1EngineLease::Starting) {
        return Fail("A valid retry deadline did not stage a fresh worker.");
    }
    actions = ReduceV1Lifecycle(&retry_state,
                                V1LifecycleEvent::EngineReady);
    if (!Has(actions, V1ActionOpenTransport) ||
        retry_state.open_attempts_for_generation != 2u) {
        return Fail("The second bounded OPEN was not counted exactly.");
    }
    (void)ReduceV1Lifecycle(
        &retry_state, V1LifecycleEvent::TransportRetryableFailure);
    (void)ReduceV1Lifecycle(&retry_state,
                            V1LifecycleEvent::TransportRetryDue);
    (void)ReduceV1Lifecycle(&retry_state,
                            V1LifecycleEvent::EngineReady);
    actions = ReduceV1Lifecycle(
        &retry_state, V1LifecycleEvent::TransportRetryableFailure);
    if (Has(actions, V1ActionScheduleTransportRetry) ||
        retry_state.media_session != V1MediaSession::Faulted ||
        retry_state.open_attempts_for_generation !=
            native_ldac::agent::kV1MaximumTransportOpenAttempts) {
        return Fail("The third failed OPEN did not exhaust the retry budget.");
    }

    V1LifecycleState pcm_retry_state{};
    pcm_retry_state.maximum_open_attempts =
        native_ldac::agent::kV1MaximumPcmTransportOpenAttempts;
    (void)ReduceV1Lifecycle(&pcm_retry_state,
                            V1LifecycleEvent::AclConnected);
    (void)ReduceV1Lifecycle(&pcm_retry_state,
                            V1LifecycleEvent::RenderStarted);
    (void)ReduceV1Lifecycle(&pcm_retry_state,
                            V1LifecycleEvent::EngineReady);
    for (std::uint32_t completed = 1u; completed < 4u; ++completed) {
        actions = ReduceV1Lifecycle(
            &pcm_retry_state,
            V1LifecycleEvent::TransportRetryableFailure);
        if (!Has(actions, V1ActionScheduleTransportRetry)) {
            return Fail("The PCM retry profile ended before attempt four.");
        }
        (void)ReduceV1Lifecycle(
            &pcm_retry_state, V1LifecycleEvent::TransportRetryDue);
        (void)ReduceV1Lifecycle(
            &pcm_retry_state, V1LifecycleEvent::EngineReady);
    }
    actions = ReduceV1Lifecycle(
        &pcm_retry_state, V1LifecycleEvent::TransportRetryableFailure);
    if (Has(actions, V1ActionScheduleTransportRetry) ||
        pcm_retry_state.open_attempts_for_generation != 4u ||
        pcm_retry_state.media_session != V1MediaSession::Faulted) {
        return Fail("The fourth PCM OPEN did not exhaust its bounded budget.");
    }

    V1LifecycleState stopped_retry_state = state;
    (void)ReduceV1Lifecycle(
        &stopped_retry_state,
        V1LifecycleEvent::TransportRetryableFailure);
    (void)ReduceV1Lifecycle(&stopped_retry_state,
                            V1LifecycleEvent::RenderStopped);
    actions = ReduceV1Lifecycle(
        &stopped_retry_state, V1LifecycleEvent::TransportRetryDue);
    if (Has(actions, V1ActionStartEngine)) {
        return Fail("Render STOP did not cancel a pending transport retry.");
    }

    (void)ReduceV1Lifecycle(&state, V1LifecycleEvent::MediaStarted);
    if (state.media_session != V1MediaSession::Streaming) {
        return Fail("Media start did not enter streaming.");
    }

    actions = ReduceV1Lifecycle(&state, V1LifecycleEvent::RenderStopped);
    if (!Has(actions, V1ActionGracefulStopTransport) ||
        !Has(actions, V1ActionStopEngine) ||
        Has(actions, V1ActionCancelTransport) ||
        state.media_session != V1MediaSession::Stopping) {
        return Fail("Online render stop was not graceful.");
    }

    (void)ReduceV1Lifecycle(&state, V1LifecycleEvent::MediaStopped);
    actions = ReduceV1Lifecycle(&state, V1LifecycleEvent::RenderStarted);
    if (actions != 0u) {
        return Fail("A stopped render retried within the same ACL generation.");
    }

    actions = ReduceV1Lifecycle(&state, V1LifecycleEvent::AclDisconnected);
    if (!Has(actions, V1ActionFailMute) ||
        !Has(actions, V1ActionPublishEndpointAbsent) ||
        state.physical_presence != V1PhysicalPresence::Absent ||
        state.render_demand != V1RenderDemand::Idle ||
        state.media_session != V1MediaSession::Stopped) {
        return Fail("ACL disconnect did not fail closed.");
    }

    (void)ReduceV1Lifecycle(&state, V1LifecycleEvent::AclConnected);
    (void)ReduceV1Lifecycle(&state, V1LifecycleEvent::RenderStarted);
    (void)ReduceV1Lifecycle(&state, V1LifecycleEvent::EngineReady);
    (void)ReduceV1Lifecycle(&state, V1LifecycleEvent::MediaStarted);
    actions = ReduceV1Lifecycle(&state, V1LifecycleEvent::AclDisconnected);
    if (!Has(actions, V1ActionFailMute) ||
        !Has(actions, V1ActionCancelTransport) ||
        !Has(actions, V1ActionStopEngine) ||
        !Has(actions, V1ActionPublishEndpointAbsent) ||
        Has(actions, V1ActionGracefulStopTransport)) {
        return Fail("Remote disconnect attempted a graceful Bluetooth exchange.");
    }

    (void)ReduceV1Lifecycle(&state, V1LifecycleEvent::AclConnected);
    actions = ReduceV1Lifecycle(&state,
                                V1LifecycleEvent::WatcherLeaseExpired);
    if (!Has(actions, V1ActionFailMute) ||
        !Has(actions, V1ActionPublishEndpointAbsent) ||
        state.physical_presence != V1PhysicalPresence::Absent) {
        return Fail("Watcher lease expiry did not unplug the endpoint.");
    }

    (void)ReduceV1Lifecycle(&state, V1LifecycleEvent::AclConnected);
    (void)ReduceV1Lifecycle(&state, V1LifecycleEvent::RenderStarted);
    (void)ReduceV1Lifecycle(&state, V1LifecycleEvent::EngineReady);
    actions = ReduceV1Lifecycle(&state, V1LifecycleEvent::MediaFailed);
    if (!Has(actions, V1ActionFailMute) ||
        !Has(actions, V1ActionCancelTransport) ||
        !Has(actions, V1ActionStopEngine) ||
        state.media_session != V1MediaSession::Faulted) {
        return Fail("Media failure did not converge to fail-muted fault.");
    }
    actions = ReduceV1Lifecycle(&state, V1LifecycleEvent::RenderStarted);
    if (Has(actions, V1ActionStartEngine) ||
        Has(actions, V1ActionOpenTransport)) {
        return Fail("A fault retried before a fresh ACL generation.");
    }

    V1LifecycleState daily_state{};
    daily_state.allow_multiple_media_sessions = true;
    (void)ReduceV1Lifecycle(
        &daily_state, V1LifecycleEvent::AclConnected);
    actions = ReduceV1Lifecycle(
        &daily_state, V1LifecycleEvent::RenderStarted);
    if (!Has(actions, V1ActionStartEngine)) {
        return Fail("Daily session one did not start its engine.");
    }
    (void)ReduceV1Lifecycle(
        &daily_state, V1LifecycleEvent::EngineReady);
    (void)ReduceV1Lifecycle(
        &daily_state, V1LifecycleEvent::MediaStarted);
    actions = ReduceV1Lifecycle(
        &daily_state, V1LifecycleEvent::RenderStopped);
    if (!Has(actions, V1ActionGracefulStopTransport) ||
        !Has(actions, V1ActionStopEngine)) {
        return Fail("Daily session one did not stop gracefully.");
    }
    (void)ReduceV1Lifecycle(
        &daily_state, V1LifecycleEvent::MediaStopped);
    if (daily_state.completed_media_sessions_for_generation != 1u ||
        daily_state.open_attempts_for_generation != 1u) {
        return Fail("Daily session completion accounting changed.");
    }
    actions = ReduceV1Lifecycle(
        &daily_state, V1LifecycleEvent::MediaStopped);
    if (actions != 0u ||
        daily_state.completed_media_sessions_for_generation != 1u) {
        return Fail("Duplicate daily media stop was not idempotent.");
    }
    actions = ReduceV1Lifecycle(
        &daily_state, V1LifecycleEvent::RenderStarted);
    if (!Has(actions, V1ActionStartEngine) ||
        daily_state.open_attempts_for_generation != 0u) {
        return Fail("Daily session two did not receive a fresh attempt budget.");
    }
    (void)ReduceV1Lifecycle(
        &daily_state, V1LifecycleEvent::EngineReady);
    if (daily_state.open_attempts_for_generation != 1u) {
        return Fail("Daily session two OPEN accounting changed.");
    }
    actions = ReduceV1Lifecycle(
        &daily_state, V1LifecycleEvent::MediaFailed);
    if (!Has(actions, V1ActionFailMute) ||
        daily_state.media_session != V1MediaSession::Faulted) {
        return Fail("Daily media failure did not remain fail closed.");
    }
    (void)ReduceV1Lifecycle(
        &daily_state, V1LifecycleEvent::RenderStopped);
    actions = ReduceV1Lifecycle(
        &daily_state, V1LifecycleEvent::RenderStarted);
    if (Has(actions, V1ActionStartEngine) ||
        daily_state.media_session != V1MediaSession::Faulted) {
        return Fail("Daily fault retried without a fresh physical ACL.");
    }
    (void)ReduceV1Lifecycle(
        &daily_state, V1LifecycleEvent::AclDisconnected);
    (void)ReduceV1Lifecycle(
        &daily_state, V1LifecycleEvent::AclConnected);
    if (daily_state.completed_media_sessions_for_generation != 0u ||
        daily_state.media_session != V1MediaSession::Stopped) {
        return Fail("Fresh ACL did not reset daily session state.");
    }

    std::printf("V1 lifecycle tests passed.\n");
    return 0;
}
