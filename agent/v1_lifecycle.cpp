#include "v1_lifecycle.h"

#include <limits>

namespace native_ldac::agent {
namespace {

std::uint32_t DisconnectActions(const V1LifecycleState& state) {
    std::uint32_t actions =
        V1ActionFailMute | V1ActionPublishEndpointAbsent;
    if (state.media_session == V1MediaSession::Opening ||
        state.media_session == V1MediaSession::Streaming ||
        state.media_session == V1MediaSession::Stopping) {
        actions |= V1ActionCancelTransport;
    }
    if (state.engine_lease != V1EngineLease::Absent) {
        actions |= V1ActionStopEngine;
    }
    return actions;
}

void ResetDisconnectedState(V1LifecycleState* state) {
    state->physical_presence = V1PhysicalPresence::Absent;
    state->render_demand = V1RenderDemand::Idle;
    state->engine_lease = V1EngineLease::Absent;
    state->media_session = V1MediaSession::Stopped;
    state->open_attempts_for_generation = 0u;
    state->completed_media_sessions_for_generation = 0u;
    state->hfp_suspended = false;
}

}  // namespace

bool HasV1LifecycleAction(std::uint32_t actions,
                          V1LifecycleAction action) {
    return (actions & static_cast<std::uint32_t>(action)) != 0u;
}

std::uint32_t GetV1TransportRetryDelayMs(
    std::uint32_t completed_open_attempts) {
    if (completed_open_attempts == 1u) {
        return 15000u;
    }
    if (completed_open_attempts == 2u) {
        return 30000u;
    }
    if (completed_open_attempts == 3u) {
        return 45000u;
    }
    return 0u;
}

std::uint32_t GetV1PcmTransportRetryDelayMs(
    std::uint32_t completed_open_attempts) {
    if (completed_open_attempts == 1u) {
        return 1000u;
    }
    if (completed_open_attempts == 2u) {
        return 2000u;
    }
    if (completed_open_attempts == 3u) {
        return 4000u;
    }
    return 0u;
}

std::uint32_t ReduceV1Lifecycle(V1LifecycleState* state,
                                V1LifecycleEvent event) {
    if (state == nullptr) {
        return V1ActionNone;
    }

    switch (event) {
        case V1LifecycleEvent::AclConnected:
            if (state->physical_presence == V1PhysicalPresence::Present) {
                return V1ActionNone;
            }
            state->physical_presence = V1PhysicalPresence::Present;
            state->render_demand = V1RenderDemand::Idle;
            state->engine_lease = V1EngineLease::Absent;
            state->media_session = V1MediaSession::Stopped;
            state->open_attempts_for_generation = 0u;
            state->completed_media_sessions_for_generation = 0u;
            state->hfp_suspended = false;
            if (state->acl_generation ==
                std::numeric_limits<std::uint64_t>::max()) {
                state->acl_generation = 1u;
            } else {
                ++state->acl_generation;
            }
            return V1ActionPublishEndpointPresent;

        case V1LifecycleEvent::AclDisconnected:
        case V1LifecycleEvent::WatcherLeaseExpired: {
            const std::uint32_t actions = DisconnectActions(*state);
            ResetDisconnectedState(state);
            return actions;
        }

        case V1LifecycleEvent::RenderStarted: {
            const bool fresh_render_edge =
                state->render_demand == V1RenderDemand::Idle;
            state->render_demand = V1RenderDemand::Running;
            if (state->hfp_suspended) {
                return V1ActionNone;
            }
            if (fresh_render_edge &&
                state->allow_multiple_media_sessions &&
                state->physical_presence == V1PhysicalPresence::Present &&
                state->engine_lease == V1EngineLease::Absent &&
                state->media_session == V1MediaSession::Stopped &&
                state->open_attempts_for_generation != 0u) {
                state->open_attempts_for_generation = 0u;
            }
            if (state->physical_presence == V1PhysicalPresence::Present &&
                state->engine_lease == V1EngineLease::Absent &&
                state->open_attempts_for_generation == 0u) {
                state->engine_lease = V1EngineLease::Starting;
                return V1ActionStartEngine;
            }
            return V1ActionNone;
        }

        case V1LifecycleEvent::RenderStopped: {
            state->render_demand = V1RenderDemand::Idle;
            std::uint32_t actions = V1ActionNone;
            if (state->tolerate_pretransport_render_gaps &&
                state->media_session != V1MediaSession::Streaming &&
                state->media_session != V1MediaSession::Stopping &&
                state->media_session != V1MediaSession::Faulted &&
                state->engine_lease != V1EngineLease::Absent) {
                return V1ActionNone;
            }
            if (state->media_session == V1MediaSession::Opening ||
                state->media_session == V1MediaSession::Streaming) {
                actions |= state->physical_presence ==
                                   V1PhysicalPresence::Present
                               ? V1ActionGracefulStopTransport
                               : V1ActionCancelTransport;
                state->media_session = V1MediaSession::Stopping;
            } else if (state->media_session == V1MediaSession::Faulted &&
                       !state->allow_multiple_media_sessions) {
                state->media_session = V1MediaSession::Stopped;
            }
            if (state->engine_lease != V1EngineLease::Absent) {
                actions |= V1ActionStopEngine;
            }
            return actions;
        }

        case V1LifecycleEvent::HfpSuspendLdac: {
            state->hfp_suspended = true;
            std::uint32_t actions = V1ActionNone;
            if (state->media_session == V1MediaSession::Opening ||
                state->media_session == V1MediaSession::Streaming ||
                state->media_session == V1MediaSession::Stopping) {
                actions |= state->physical_presence ==
                                   V1PhysicalPresence::Present
                               ? V1ActionGracefulStopTransport
                               : V1ActionCancelTransport;
                if (state->media_session != V1MediaSession::Stopping) {
                    state->media_session = V1MediaSession::Stopping;
                }
            }
            if (state->engine_lease != V1EngineLease::Absent) {
                actions |= V1ActionStopEngine;
            }
            return actions;
        }

        case V1LifecycleEvent::HfpResumeLdac:
            if (!state->hfp_suspended) return V1ActionNone;
            state->hfp_suspended = false;
            if (state->physical_presence == V1PhysicalPresence::Present &&
                state->render_demand == V1RenderDemand::Running &&
                state->engine_lease == V1EngineLease::Absent &&
                state->media_session == V1MediaSession::Stopped) {
                state->open_attempts_for_generation = 0u;
                state->engine_lease = V1EngineLease::Starting;
                return V1ActionStartEngine;
            }
            return V1ActionNone;

        case V1LifecycleEvent::EngineReady:
            if (state->engine_lease == V1EngineLease::Ready &&
                state->open_attempts_for_generation != 0u) {
                return V1ActionNone;
            }
            if (state->physical_presence != V1PhysicalPresence::Present ||
                state->hfp_suspended ||
                (state->render_demand != V1RenderDemand::Running &&
                 !state->tolerate_pretransport_render_gaps) ||
                state->engine_lease != V1EngineLease::Starting ||
                 state->open_attempts_for_generation >=
                     state->maximum_open_attempts) {
                state->engine_lease = V1EngineLease::Absent;
                return V1ActionStopEngine;
            }
            state->engine_lease = V1EngineLease::Ready;
            state->media_session = V1MediaSession::Opening;
            ++state->open_attempts_for_generation;
            return V1ActionOpenTransport;

        case V1LifecycleEvent::EngineExited: {
            std::uint32_t actions = V1ActionNone;
            if (state->media_session == V1MediaSession::Opening ||
                state->media_session == V1MediaSession::Streaming ||
                state->media_session == V1MediaSession::Stopping) {
                actions |= V1ActionFailMute | V1ActionCancelTransport;
                state->media_session = V1MediaSession::Faulted;
            }
            state->engine_lease = V1EngineLease::Absent;
            return actions;
        }

        case V1LifecycleEvent::TransportOpenSuppressed:
            if (state->media_session == V1MediaSession::Opening) {
                state->media_session = V1MediaSession::Stopped;
            }
            return V1ActionNone;

        case V1LifecycleEvent::MediaStarted:
            if (state->physical_presence == V1PhysicalPresence::Present &&
                !state->hfp_suspended &&
                state->render_demand == V1RenderDemand::Running &&
                state->engine_lease == V1EngineLease::Ready &&
                state->media_session == V1MediaSession::Opening) {
                state->media_session = V1MediaSession::Streaming;
            }
            return V1ActionNone;

        case V1LifecycleEvent::MediaStopped:
            if (state->allow_multiple_media_sessions &&
                (state->media_session == V1MediaSession::Streaming ||
                 state->media_session == V1MediaSession::Stopping)) {
                if (state->completed_media_sessions_for_generation !=
                    std::numeric_limits<std::uint32_t>::max()) {
                    ++state->completed_media_sessions_for_generation;
                }
            }
            state->media_session = V1MediaSession::Stopped;
            state->engine_lease = V1EngineLease::Absent;
            return V1ActionNone;

        case V1LifecycleEvent::TransportRetryableFailure: {
            std::uint32_t actions =
                V1ActionFailMute | V1ActionCancelTransport |
                V1ActionStopEngine;
            state->engine_lease = V1EngineLease::Absent;
            if (state->physical_presence == V1PhysicalPresence::Present &&
                state->render_demand == V1RenderDemand::Running &&
                state->media_session == V1MediaSession::Opening &&
                 state->open_attempts_for_generation <
                     state->maximum_open_attempts) {
                state->media_session = V1MediaSession::Stopped;
                return actions | V1ActionScheduleTransportRetry;
            }
            state->media_session = V1MediaSession::Faulted;
            return actions;
        }

        case V1LifecycleEvent::TransportRetryDue:
            if (state->physical_presence == V1PhysicalPresence::Present &&
                state->render_demand == V1RenderDemand::Running &&
                state->engine_lease == V1EngineLease::Absent &&
                state->media_session == V1MediaSession::Stopped &&
                state->open_attempts_for_generation != 0u &&
                 state->open_attempts_for_generation <
                     state->maximum_open_attempts) {
                state->engine_lease = V1EngineLease::Starting;
                return V1ActionStartEngine;
            }
            return V1ActionNone;

        case V1LifecycleEvent::MediaFailed: {
            std::uint32_t actions = V1ActionFailMute;
            if (state->media_session != V1MediaSession::Stopped) {
                actions |= V1ActionCancelTransport;
            }
            if (state->engine_lease != V1EngineLease::Absent) {
                actions |= V1ActionStopEngine;
            }
            state->engine_lease = V1EngineLease::Absent;
            state->media_session = V1MediaSession::Faulted;
            return actions;
        }
    }
    return V1ActionNone;
}

}  // namespace native_ldac::agent
