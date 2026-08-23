#include "../v1_lifecycle.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>

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
using native_ldac::agent::V1LifecycleAction;
using native_ldac::agent::V1LifecycleEvent;
using native_ldac::agent::V1LifecycleState;
using native_ldac::agent::V1MediaSession;
using native_ldac::agent::V1PhysicalPresence;
using native_ldac::agent::V1RenderDemand;

struct ResourceLedger {
    bool endpoint_present = false;
    bool engine_active = false;
    bool transport_authorized = false;
    std::uint64_t engine_generation = 0u;
};

struct StressCounters {
    std::uint64_t events = 0u;
    std::uint64_t opens = 0u;
    std::uint64_t disconnects = 0u;
    std::uint64_t expiries = 0u;
    std::uint64_t convergences = 0u;
};

int Fail(const char* message,
         std::uint64_t seed = 0u,
         std::uint64_t step = 0u) {
    std::fprintf(stderr,
                 "%s (seed 0x%016llx, step %llu)\n",
                 message,
                 static_cast<unsigned long long>(seed),
                 static_cast<unsigned long long>(step));
    return 1;
}

bool Has(std::uint32_t actions, V1LifecycleAction action) {
    return HasV1LifecycleAction(actions, action);
}

std::uint64_t NextRandom(std::uint64_t* state) {
    // xorshift64*: deterministic, fast, and independent of CRT rand().
    std::uint64_t value = *state;
    value ^= value >> 12u;
    value ^= value << 25u;
    value ^= value >> 27u;
    *state = value;
    return value * 0x2545f4914f6cdd1dull;
}

void ApplyExternalEvent(ResourceLedger* resources,
                        V1LifecycleEvent event) {
    if (event == V1LifecycleEvent::EngineExited) {
        resources->engine_active = false;
        resources->engine_generation = 0u;
    }
    if (event == V1LifecycleEvent::TransportOpenSuppressed ||
        event == V1LifecycleEvent::MediaStopped ||
        event == V1LifecycleEvent::TransportRetryableFailure ||
        event == V1LifecycleEvent::MediaFailed) {
        resources->transport_authorized = false;
    }
    if (event == V1LifecycleEvent::MediaStopped) {
        resources->engine_active = false;
        resources->engine_generation = 0u;
    }
}

bool ApplyActions(ResourceLedger* resources,
                  const V1LifecycleState& state,
                  std::uint32_t actions) {
    if (Has(actions, V1ActionPublishEndpointPresent)) {
        resources->endpoint_present = true;
    }
    if (Has(actions, V1ActionPublishEndpointAbsent)) {
        resources->endpoint_present = false;
    }
    if (Has(actions, V1ActionStartEngine)) {
        resources->engine_active = true;
        resources->engine_generation = state.acl_generation;
    }
    if (Has(actions, V1ActionStopEngine)) {
        resources->engine_active = false;
        resources->engine_generation = 0u;
    }
    if (Has(actions, V1ActionOpenTransport)) {
        if (!resources->engine_active ||
            resources->engine_generation != state.acl_generation) {
            return false;
        }
        resources->transport_authorized = true;
    }
    if (Has(actions, V1ActionCancelTransport) ||
        Has(actions, V1ActionGracefulStopTransport)) {
        resources->transport_authorized = false;
    }
    return true;
}

bool SettleSynchronousStop(V1LifecycleState* state,
                           ResourceLedger* resources,
                           std::uint32_t actions) {
    if (!Has(actions, V1ActionStopEngine)) {
        return true;
    }
    // ExecuteEngineActions waits for the contained child. A graceful stop
    // feeds MediaStopped back into the reducer; a local-only stop feeds
    // EngineExited back. Cancel paths have already reset the reducer state.
    if (Has(actions, V1ActionGracefulStopTransport)) {
        const std::uint32_t followup =
            ReduceV1Lifecycle(state, V1LifecycleEvent::MediaStopped);
        ApplyExternalEvent(resources, V1LifecycleEvent::MediaStopped);
        return followup == 0u;
    }
    if (!Has(actions, V1ActionCancelTransport)) {
        const std::uint32_t followup =
            ReduceV1Lifecycle(state, V1LifecycleEvent::EngineExited);
        ApplyExternalEvent(resources, V1LifecycleEvent::EngineExited);
        return followup == 0u;
    }
    return state->engine_lease == V1EngineLease::Absent;
}

bool ValidateActions(const V1LifecycleState& before,
                     const V1LifecycleState& after,
                     V1LifecycleEvent event,
                     std::uint32_t actions,
                     std::uint32_t previous_attempts) {
    if (Has(actions, V1ActionCancelTransport) &&
        Has(actions, V1ActionGracefulStopTransport)) {
        return false;
    }
    if (Has(actions, V1ActionPublishEndpointPresent) &&
        Has(actions, V1ActionPublishEndpointAbsent)) {
        return false;
    }
    if (after.open_attempts_for_generation > after.maximum_open_attempts) {
        return false;
    }
    if (Has(actions, V1ActionOpenTransport)) {
        if (after.physical_presence != V1PhysicalPresence::Present ||
            after.engine_lease != V1EngineLease::Ready ||
            after.media_session != V1MediaSession::Opening ||
            after.open_attempts_for_generation != previous_attempts + 1u ||
            after.open_attempts_for_generation >
                after.maximum_open_attempts) {
            return false;
        }
    } else if (event == V1LifecycleEvent::RenderStarted &&
               before.allow_multiple_media_sessions &&
               before.render_demand == V1RenderDemand::Idle &&
               before.physical_presence == V1PhysicalPresence::Present &&
               before.engine_lease == V1EngineLease::Absent &&
               before.media_session == V1MediaSession::Stopped &&
               previous_attempts != 0u) {
        if (!Has(actions, V1ActionStartEngine) ||
            after.open_attempts_for_generation != 0u) {
            return false;
        }
    } else if (event != V1LifecycleEvent::AclConnected &&
               event != V1LifecycleEvent::AclDisconnected &&
               event != V1LifecycleEvent::WatcherLeaseExpired &&
               after.open_attempts_for_generation != previous_attempts) {
        return false;
    }
    if (Has(actions, V1ActionStartEngine) &&
        (after.physical_presence != V1PhysicalPresence::Present ||
         after.render_demand != V1RenderDemand::Running ||
         after.engine_lease != V1EngineLease::Starting)) {
        return false;
    }
    if (Has(actions, V1ActionScheduleTransportRetry) &&
        (!Has(actions, V1ActionFailMute) ||
         !Has(actions, V1ActionCancelTransport) ||
         !Has(actions, V1ActionStopEngine) ||
         after.physical_presence != V1PhysicalPresence::Present ||
         after.render_demand != V1RenderDemand::Running ||
         after.engine_lease != V1EngineLease::Absent ||
         after.media_session != V1MediaSession::Stopped ||
         after.open_attempts_for_generation == 0u ||
         after.open_attempts_for_generation >=
             after.maximum_open_attempts)) {
        return false;
    }
    if (event == V1LifecycleEvent::AclConnected &&
        before.physical_presence == V1PhysicalPresence::Absent) {
        const std::uint64_t expected_generation =
            before.acl_generation ==
                    std::numeric_limits<std::uint64_t>::max()
                ? 1u
                : before.acl_generation + 1u;
        if (!Has(actions, V1ActionPublishEndpointPresent) ||
            after.acl_generation != expected_generation ||
            after.open_attempts_for_generation != 0u ||
            after.completed_media_sessions_for_generation != 0u) {
            return false;
        }
    } else if (event != V1LifecycleEvent::AclConnected &&
               after.acl_generation != before.acl_generation) {
        return false;
    }
    if (event == V1LifecycleEvent::AclDisconnected ||
        event == V1LifecycleEvent::WatcherLeaseExpired) {
        if (!Has(actions, V1ActionFailMute) ||
            !Has(actions, V1ActionPublishEndpointAbsent) ||
            Has(actions, V1ActionGracefulStopTransport) ||
            after.physical_presence != V1PhysicalPresence::Absent ||
            after.render_demand != V1RenderDemand::Idle ||
            after.engine_lease != V1EngineLease::Absent ||
            after.media_session != V1MediaSession::Stopped ||
            after.open_attempts_for_generation != 0u ||
            after.completed_media_sessions_for_generation != 0u) {
            return false;
        }
    }
    if (!before.allow_multiple_media_sessions &&
        event != V1LifecycleEvent::AclConnected &&
        event != V1LifecycleEvent::AclDisconnected &&
        event != V1LifecycleEvent::WatcherLeaseExpired &&
        after.completed_media_sessions_for_generation !=
            before.completed_media_sessions_for_generation) {
        return false;
    }
    return true;
}

int VerifyRetryBudget(std::uint32_t maximum_attempts) {
    V1LifecycleState state{};
    state.maximum_open_attempts = maximum_attempts;
    (void)ReduceV1Lifecycle(&state, V1LifecycleEvent::AclConnected);
    (void)ReduceV1Lifecycle(&state, V1LifecycleEvent::RenderStarted);

    for (std::uint32_t attempt = 1u; attempt <= maximum_attempts; ++attempt) {
        std::uint32_t actions =
            ReduceV1Lifecycle(&state, V1LifecycleEvent::EngineReady);
        if (!Has(actions, V1ActionOpenTransport) ||
            state.open_attempts_for_generation != attempt) {
            return Fail("Bounded retry did not authorize the expected OPEN.");
        }
        actions = ReduceV1Lifecycle(
            &state, V1LifecycleEvent::TransportRetryableFailure);
        const bool should_retry = attempt < maximum_attempts;
        if (Has(actions, V1ActionScheduleTransportRetry) != should_retry ||
            (should_retry && state.media_session != V1MediaSession::Stopped) ||
            (!should_retry && state.media_session != V1MediaSession::Faulted)) {
            return Fail("Bounded retry exhaustion policy changed.");
        }
        actions = ReduceV1Lifecycle(
            &state, V1LifecycleEvent::TransportRetryDue);
        if (should_retry != Has(actions, V1ActionStartEngine)) {
            return Fail("Retry deadline escaped its bounded attempt policy.");
        }
    }

    const std::uint32_t actions =
        ReduceV1Lifecycle(&state, V1LifecycleEvent::EngineReady);
    if (Has(actions, V1ActionOpenTransport) ||
        state.open_attempts_for_generation != maximum_attempts) {
        return Fail("An exhausted generation authorized another OPEN.");
    }
    return 0;
}

int VerifyGenerationIsolation() {
    V1LifecycleState state{};
    ResourceLedger resources{};

    auto reduce = [&](V1LifecycleEvent event) {
        ApplyExternalEvent(&resources, event);
        const std::uint32_t actions = ReduceV1Lifecycle(&state, event);
        return ApplyActions(&resources, state, actions) &&
                       SettleSynchronousStop(&state, &resources, actions)
                   ? actions
                   : 0xffffffffu;
    };

    (void)reduce(V1LifecycleEvent::AclConnected);
    (void)reduce(V1LifecycleEvent::RenderStarted);
    std::uint32_t actions = reduce(V1LifecycleEvent::EngineReady);
    if (!Has(actions, V1ActionOpenTransport) || state.acl_generation != 1u) {
        return Fail("Generation one did not reach its bounded OPEN.");
    }
    actions = reduce(V1LifecycleEvent::AclDisconnected);
    if (!Has(actions, V1ActionCancelTransport) ||
        resources.engine_active || resources.transport_authorized) {
        return Fail("Disconnect did not revoke generation-one resources.");
    }

    // Delayed callbacks from the dead generation must be harmless while no
    // new generation owns a worker.
    const std::array<V1LifecycleEvent, 5> stale_events = {
        V1LifecycleEvent::EngineReady,
        V1LifecycleEvent::MediaStarted,
        V1LifecycleEvent::TransportRetryDue,
        V1LifecycleEvent::MediaStopped,
        V1LifecycleEvent::EngineExited,
    };
    for (const V1LifecycleEvent event : stale_events) {
        actions = reduce(event);
        if (Has(actions, V1ActionOpenTransport) ||
            resources.transport_authorized) {
            return Fail("A stale generation callback authorized transport.");
        }
    }

    (void)reduce(V1LifecycleEvent::AclConnected);
    if (state.acl_generation != 2u) {
        return Fail("Fresh ACL did not create a fresh generation.");
    }
    (void)reduce(V1LifecycleEvent::RenderStarted);
    actions = reduce(V1LifecycleEvent::EngineReady);
    if (!Has(actions, V1ActionOpenTransport) ||
        resources.engine_generation != 2u) {
        return Fail("Generation two did not own its OPEN authorization.");
    }
    (void)reduce(V1LifecycleEvent::WatcherLeaseExpired);
    if (resources.endpoint_present || resources.engine_active ||
        resources.transport_authorized) {
        return Fail("Watcher expiry retained cross-generation resources.");
    }

    state.acl_generation = std::numeric_limits<std::uint64_t>::max();
    actions = reduce(V1LifecycleEvent::AclConnected);
    if (!Has(actions, V1ActionPublishEndpointPresent) ||
        state.acl_generation != 1u) {
        return Fail("ACL generation wrap did not avoid generation zero.");
    }
    return 0;
}

int RunStressSequence(std::uint64_t seed,
                      std::uint64_t steps,
                      StressCounters* totals) {
    constexpr std::array<V1LifecycleEvent, 13> events = {
        V1LifecycleEvent::AclConnected,
        V1LifecycleEvent::AclDisconnected,
        V1LifecycleEvent::RenderStarted,
        V1LifecycleEvent::RenderStopped,
        V1LifecycleEvent::EngineReady,
        V1LifecycleEvent::EngineExited,
        V1LifecycleEvent::TransportOpenSuppressed,
        V1LifecycleEvent::MediaStarted,
        V1LifecycleEvent::MediaStopped,
        V1LifecycleEvent::TransportRetryableFailure,
        V1LifecycleEvent::TransportRetryDue,
        V1LifecycleEvent::MediaFailed,
        V1LifecycleEvent::WatcherLeaseExpired,
    };

    std::uint64_t random = seed;
    V1LifecycleState state{};
    state.maximum_open_attempts =
        (NextRandom(&random) & 1u) == 0u
            ? native_ldac::agent::kV1MaximumTransportOpenAttempts
            : native_ldac::agent::kV1MaximumPcmTransportOpenAttempts;
    state.tolerate_pretransport_render_gaps =
        (NextRandom(&random) & 1u) != 0u;
    state.allow_multiple_media_sessions =
        (NextRandom(&random) & 1u) != 0u;
    ResourceLedger resources{};
    std::uint64_t current_generation = 0u;
    std::uint32_t opens_in_generation = 0u;

    for (std::uint64_t step = 0u; step < steps; ++step) {
        V1LifecycleEvent event =
            events[static_cast<std::size_t>(
                NextRandom(&random) % events.size())];
        // Periodic forced teardown proves convergence independently of the
        // random distribution and makes every failing prefix bounded.
        if ((step + 1u) % 127u == 0u) {
            event = (NextRandom(&random) & 1u) == 0u
                        ? V1LifecycleEvent::AclDisconnected
                        : V1LifecycleEvent::WatcherLeaseExpired;
        }

        const V1LifecycleState before = state;
        const std::uint32_t previous_attempts =
            state.open_attempts_for_generation;
        ApplyExternalEvent(&resources, event);
        const std::uint32_t actions = ReduceV1Lifecycle(&state, event);
        ++totals->events;

        if (!ValidateActions(before,
                             state,
                             event,
                             actions,
                             previous_attempts)) {
            return Fail("Lifecycle action/state invariant failed.", seed, step);
        }
        if (!ApplyActions(&resources, state, actions)) {
            return Fail("OPEN crossed its engine ACL generation.", seed, step);
        }
        if (!SettleSynchronousStop(&state, &resources, actions)) {
            return Fail("Synchronous engine stop did not settle cleanly.",
                        seed,
                        step);
        }
        if (resources.endpoint_present !=
            (state.physical_presence == V1PhysicalPresence::Present)) {
            return Fail("Endpoint resource ledger diverged.", seed, step);
        }

        if (event == V1LifecycleEvent::AclConnected &&
            before.physical_presence == V1PhysicalPresence::Absent) {
            current_generation = state.acl_generation;
            opens_in_generation = 0u;
        }
        if (event == V1LifecycleEvent::RenderStarted &&
            Has(actions, V1ActionStartEngine) &&
            before.allow_multiple_media_sessions &&
            before.open_attempts_for_generation != 0u &&
            state.open_attempts_for_generation == 0u) {
            opens_in_generation = 0u;
        }
        if (Has(actions, V1ActionOpenTransport)) {
            if (state.acl_generation == 0u ||
                state.acl_generation != current_generation) {
                return Fail("OPEN was not bound to the current ACL generation.",
                            seed,
                            step);
            }
            ++opens_in_generation;
            ++totals->opens;
            if (opens_in_generation !=
                    state.open_attempts_for_generation ||
                opens_in_generation > state.maximum_open_attempts) {
                return Fail("OPEN attempt accounting escaped its bound.",
                            seed,
                            step);
            }
        }

        if (event == V1LifecycleEvent::AclDisconnected ||
            event == V1LifecycleEvent::WatcherLeaseExpired) {
            if (event == V1LifecycleEvent::AclDisconnected) {
                ++totals->disconnects;
            } else {
                ++totals->expiries;
            }
            if (resources.endpoint_present || resources.engine_active ||
                resources.transport_authorized) {
                return Fail("Teardown retained an external resource.", seed, step);
            }
            ++totals->convergences;
        }
    }

    ApplyExternalEvent(&resources, V1LifecycleEvent::AclDisconnected);
    const V1LifecycleState before = state;
    const std::uint32_t attempts = state.open_attempts_for_generation;
    const std::uint32_t actions =
        ReduceV1Lifecycle(&state, V1LifecycleEvent::AclDisconnected);
    if (!ValidateActions(before,
                         state,
                         V1LifecycleEvent::AclDisconnected,
                         actions,
                         attempts) ||
        !ApplyActions(&resources, state, actions) ||
        !SettleSynchronousStop(&state, &resources, actions) ||
        resources.endpoint_present || resources.engine_active ||
        resources.transport_authorized ||
        state.physical_presence != V1PhysicalPresence::Absent ||
        state.engine_lease != V1EngineLease::Absent ||
        state.media_session != V1MediaSession::Stopped) {
        return Fail("Final forced teardown did not converge.", seed, steps);
    }
    ++totals->convergences;
    return 0;
}

}  // namespace

int main() {
    if (VerifyRetryBudget(
            native_ldac::agent::kV1MaximumTransportOpenAttempts) != 0 ||
        VerifyRetryBudget(
            native_ldac::agent::kV1MaximumPcmTransportOpenAttempts) != 0 ||
        VerifyGenerationIsolation() != 0) {
        return 1;
    }

    constexpr std::array<std::uint64_t, 16> seeds = {
        0x0000000000000001ull,
        0x9e3779b97f4a7c15ull,
        0xd1b54a32d192ed03ull,
        0x94d049bb133111ebull,
        0x853c49e6748fea9bull,
        0xda3e39cb94b95bdbull,
        0x243f6a8885a308d3ull,
        0x13198a2e03707344ull,
        0xa4093822299f31d0ull,
        0x082efa98ec4e6c89ull,
        0x452821e638d01377ull,
        0xbe5466cf34e90c6cull,
        0xc0ac29b7c97c50ddull,
        0x3f84d5b5b5470917ull,
        0x9216d5d98979fb1bull,
        0xd1310ba698dfb5acull,
    };
    constexpr std::uint64_t kStepsPerSeed = 16384u;
    StressCounters totals{};
    for (const std::uint64_t seed : seeds) {
        if (RunStressSequence(seed, kStepsPerSeed, &totals) != 0) {
            return 1;
        }
    }

    std::printf(
        "V1 lifecycle stress tests passed: %llu events, %llu OPENs, "
        "%llu ACL disconnects, %llu watcher expiries, %llu convergences.\n",
        static_cast<unsigned long long>(totals.events),
        static_cast<unsigned long long>(totals.opens),
        static_cast<unsigned long long>(totals.disconnects),
        static_cast<unsigned long long>(totals.expiries),
        static_cast<unsigned long long>(totals.convergences));
    return 0;
}
