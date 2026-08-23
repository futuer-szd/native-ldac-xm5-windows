#pragma once

#include <cstdint>

namespace native_ldac::agent {

enum class V1PhysicalPresence {
    Absent,
    Present,
};

enum class V1RenderDemand {
    Idle,
    Running,
};

enum class V1EngineLease {
    Absent,
    Starting,
    Ready,
};

enum class V1MediaSession {
    Stopped,
    Opening,
    Streaming,
    Stopping,
    Faulted,
};

enum class V1LifecycleEvent {
    AclConnected,
    AclDisconnected,
    RenderStarted,
    RenderStopped,
    HfpSuspendLdac,
    HfpResumeLdac,
    EngineReady,
    EngineExited,
    TransportOpenSuppressed,
    MediaStarted,
    MediaStopped,
    TransportRetryableFailure,
    TransportRetryDue,
    MediaFailed,
    WatcherLeaseExpired,
};

enum V1LifecycleAction : std::uint32_t {
    V1ActionNone = 0u,
    V1ActionPublishEndpointPresent = 1u << 0u,
    V1ActionPublishEndpointAbsent = 1u << 1u,
    V1ActionStartEngine = 1u << 2u,
    V1ActionStopEngine = 1u << 3u,
    V1ActionOpenTransport = 1u << 4u,
    V1ActionGracefulStopTransport = 1u << 5u,
    V1ActionCancelTransport = 1u << 6u,
    V1ActionFailMute = 1u << 7u,
    V1ActionScheduleTransportRetry = 1u << 8u,
};

constexpr std::uint32_t kV1MaximumTransportOpenAttempts = 3u;
constexpr std::uint32_t kV1MaximumPcmTransportOpenAttempts = 4u;

struct V1LifecycleState {
    V1PhysicalPresence physical_presence = V1PhysicalPresence::Absent;
    V1RenderDemand render_demand = V1RenderDemand::Idle;
    V1EngineLease engine_lease = V1EngineLease::Absent;
    V1MediaSession media_session = V1MediaSession::Stopped;
    std::uint64_t acl_generation = 0u;
    std::uint32_t open_attempts_for_generation = 0u;
    std::uint32_t maximum_open_attempts =
        kV1MaximumTransportOpenAttempts;
    bool tolerate_pretransport_render_gaps = false;
    bool allow_multiple_media_sessions = false;
    bool hfp_suspended = false;
    std::uint32_t completed_media_sessions_for_generation = 0u;
};

std::uint32_t ReduceV1Lifecycle(V1LifecycleState* state,
                                V1LifecycleEvent event);

bool HasV1LifecycleAction(std::uint32_t actions,
                          V1LifecycleAction action);

std::uint32_t GetV1TransportRetryDelayMs(
    std::uint32_t completed_open_attempts);

std::uint32_t GetV1PcmTransportRetryDelayMs(
    std::uint32_t completed_open_attempts);

}  // namespace native_ldac::agent
