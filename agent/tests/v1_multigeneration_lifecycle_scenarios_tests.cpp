// SPDX-License-Identifier: Apache-2.0
#include "../v1_lifecycle.h"

#include <array>
#include <cstdint>
#include <cstdio>

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

int failures = 0;

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expression); \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

bool Has(std::uint32_t actions, V1LifecycleAction action) {
    return HasV1LifecycleAction(actions, action);
}

struct ResourceLedger {
    bool endpoint_present = false;
    bool engine_active = false;
    bool transport_authorized = false;
    bool media_streaming = false;
    std::uint64_t engine_generation = 0u;
    std::uint64_t transport_generation = 0u;

    bool Empty() const {
        return !endpoint_present && !engine_active &&
            !transport_authorized && !media_streaming &&
            engine_generation == 0u && transport_generation == 0u;
    }
};

struct ScenarioSnapshot {
    V1LifecycleState lifecycle{};
    ResourceLedger resources{};
    std::uint64_t active_generation = 0u;
};

bool SameLifecycle(const V1LifecycleState& left,
                   const V1LifecycleState& right) {
    return left.physical_presence == right.physical_presence &&
        left.render_demand == right.render_demand &&
        left.engine_lease == right.engine_lease &&
        left.media_session == right.media_session &&
        left.acl_generation == right.acl_generation &&
        left.open_attempts_for_generation ==
            right.open_attempts_for_generation &&
        left.maximum_open_attempts == right.maximum_open_attempts &&
        left.tolerate_pretransport_render_gaps ==
            right.tolerate_pretransport_render_gaps &&
        left.allow_multiple_media_sessions ==
            right.allow_multiple_media_sessions &&
        left.completed_media_sessions_for_generation ==
            right.completed_media_sessions_for_generation;
}

bool SameResources(const ResourceLedger& left,
                   const ResourceLedger& right) {
    return left.endpoint_present == right.endpoint_present &&
        left.engine_active == right.engine_active &&
        left.transport_authorized == right.transport_authorized &&
        left.media_streaming == right.media_streaming &&
        left.engine_generation == right.engine_generation &&
        left.transport_generation == right.transport_generation;
}

class LifecycleHarness {
public:
    std::uint32_t Connect() {
        const std::uint32_t actions = ReduceAndApply(
            V1LifecycleEvent::AclConnected);
        if (Has(actions, V1ActionPublishEndpointPresent)) {
            active_generation_ = lifecycle_.acl_generation;
        }
        return actions;
    }

    std::uint32_t Disconnect() {
        const std::uint32_t actions = ReduceAndApply(
            V1LifecycleEvent::AclDisconnected);
        active_generation_ = 0u;
        return actions;
    }

    std::uint32_t Render(V1LifecycleEvent event) {
        CHECK(event == V1LifecycleEvent::RenderStarted ||
              event == V1LifecycleEvent::RenderStopped);
        return ReduceAndApply(event);
    }

    std::uint32_t Worker(std::uint64_t generation,
                         V1LifecycleEvent event) {
        if (active_generation_ == 0u || generation != active_generation_) {
            ++stale_events_dropped_;
            return 0u;
        }
        return ReduceAndApply(event);
    }

    ScenarioSnapshot Snapshot() const {
        return {lifecycle_, resources_, active_generation_};
    }

    bool SameAs(const ScenarioSnapshot& snapshot) const {
        return SameLifecycle(lifecycle_, snapshot.lifecycle) &&
            SameResources(resources_, snapshot.resources) &&
            active_generation_ == snapshot.active_generation;
    }

    const V1LifecycleState& lifecycle() const { return lifecycle_; }
    const ResourceLedger& resources() const { return resources_; }
    std::uint64_t active_generation() const { return active_generation_; }
    std::uint64_t stale_events_dropped() const {
        return stale_events_dropped_;
    }
    std::uint64_t open_count() const { return open_count_; }

    void EnableMultipleMediaSessions() {
        lifecycle_.allow_multiple_media_sessions = true;
    }

private:
    void ApplyExternalEvent(V1LifecycleEvent event) {
        if (event == V1LifecycleEvent::EngineExited) {
            resources_.engine_active = false;
            resources_.engine_generation = 0u;
        }
        if (event == V1LifecycleEvent::MediaStopped) {
            resources_.transport_authorized = false;
            resources_.media_streaming = false;
            resources_.transport_generation = 0u;
            resources_.engine_active = false;
            resources_.engine_generation = 0u;
        }
        if (event == V1LifecycleEvent::TransportRetryableFailure ||
            event == V1LifecycleEvent::MediaFailed) {
            resources_.transport_authorized = false;
            resources_.media_streaming = false;
            resources_.transport_generation = 0u;
        }
    }

    void ApplyActions(std::uint32_t actions) {
        if (Has(actions, V1ActionPublishEndpointPresent)) {
            resources_.endpoint_present = true;
        }
        if (Has(actions, V1ActionPublishEndpointAbsent)) {
            resources_.endpoint_present = false;
        }
        if (Has(actions, V1ActionStartEngine)) {
            resources_.engine_active = true;
            resources_.engine_generation = lifecycle_.acl_generation;
        }
        if (Has(actions, V1ActionStopEngine)) {
            resources_.engine_active = false;
            resources_.engine_generation = 0u;
        }
        if (Has(actions, V1ActionOpenTransport)) {
            CHECK(resources_.engine_active);
            CHECK(resources_.engine_generation == lifecycle_.acl_generation);
            resources_.transport_authorized = true;
            resources_.transport_generation = lifecycle_.acl_generation;
            ++open_count_;
        }
        if (Has(actions, V1ActionGracefulStopTransport) ||
            Has(actions, V1ActionCancelTransport)) {
            resources_.transport_authorized = false;
            resources_.media_streaming = false;
            resources_.transport_generation = 0u;
        }
    }

    std::uint32_t ReduceAndApply(V1LifecycleEvent event) {
        ApplyExternalEvent(event);
        const std::uint32_t actions = ReduceV1Lifecycle(&lifecycle_, event);
        ApplyActions(actions);
        if (event == V1LifecycleEvent::MediaStarted &&
            lifecycle_.media_session == V1MediaSession::Streaming) {
            resources_.media_streaming = true;
        }
        return actions;
    }

    V1LifecycleState lifecycle_{};
    ResourceLedger resources_{};
    std::uint64_t active_generation_ = 0u;
    std::uint64_t stale_events_dropped_ = 0u;
    std::uint64_t open_count_ = 0u;
};

void CheckDisconnected(const LifecycleHarness& harness) {
    CHECK(harness.active_generation() == 0u);
    CHECK(harness.lifecycle().physical_presence == V1PhysicalPresence::Absent);
    CHECK(harness.lifecycle().render_demand == V1RenderDemand::Idle);
    CHECK(harness.lifecycle().engine_lease == V1EngineLease::Absent);
    CHECK(harness.lifecycle().media_session == V1MediaSession::Stopped);
    CHECK(harness.lifecycle().open_attempts_for_generation == 0u);
    CHECK(harness.resources().Empty());
}

void StartMedia(LifecycleHarness* harness, std::uint64_t generation) {
    std::uint32_t actions = harness->Render(V1LifecycleEvent::RenderStarted);
    CHECK(Has(actions, V1ActionStartEngine));
    actions = harness->Worker(generation, V1LifecycleEvent::EngineReady);
    CHECK(Has(actions, V1ActionOpenTransport));
    actions = harness->Worker(generation, V1LifecycleEvent::MediaStarted);
    CHECK(actions == 0u);
    CHECK(harness->lifecycle().media_session == V1MediaSession::Streaming);
    CHECK(harness->resources().media_streaming);
    CHECK(harness->resources().transport_generation == generation);
}

void GracefulStopAndDisconnect(LifecycleHarness* harness,
                               std::uint64_t generation) {
    std::uint32_t actions = harness->Render(
        V1LifecycleEvent::RenderStopped);
    CHECK(Has(actions, V1ActionGracefulStopTransport));
    CHECK(Has(actions, V1ActionStopEngine));
    CHECK(!Has(actions, V1ActionCancelTransport));
    CHECK(harness->lifecycle().media_session == V1MediaSession::Stopping);
    actions = harness->Worker(generation, V1LifecycleEvent::MediaStopped);
    CHECK(actions == 0u);
    CHECK(harness->lifecycle().media_session == V1MediaSession::Stopped);
    actions = harness->Disconnect();
    CHECK(Has(actions, V1ActionFailMute));
    CHECK(Has(actions, V1ActionPublishEndpointAbsent));
    CHECK(!Has(actions, V1ActionCancelTransport));
    CheckDisconnected(*harness);
}

void TestTwoDailyGracefulGenerations() {
    LifecycleHarness harness;
    std::uint32_t actions = harness.Connect();
    CHECK(Has(actions, V1ActionPublishEndpointPresent));
    CHECK(harness.active_generation() == 1u);
    StartMedia(&harness, 1u);
    GracefulStopAndDisconnect(&harness, 1u);

    actions = harness.Connect();
    CHECK(Has(actions, V1ActionPublishEndpointPresent));
    CHECK(harness.active_generation() == 2u);
    const ScenarioSnapshot connected = harness.Snapshot();
    constexpr std::array<V1LifecycleEvent, 4> stale = {
        V1LifecycleEvent::TransportRetryDue,
        V1LifecycleEvent::MediaStarted,
        V1LifecycleEvent::MediaStopped,
        V1LifecycleEvent::EngineExited,
    };
    for (const V1LifecycleEvent event : stale) {
        CHECK(harness.Worker(1u, event) == 0u);
        CHECK(harness.SameAs(connected));
    }
    StartMedia(&harness, 2u);
    GracefulStopAndDisconnect(&harness, 2u);
    CHECK(harness.open_count() == 2u);
    CHECK(harness.stale_events_dropped() == stale.size());
}

void TestOldRetryAndCallbacksCannotReachNewMedia() {
    LifecycleHarness harness;
    (void)harness.Connect();
    (void)harness.Render(V1LifecycleEvent::RenderStarted);
    (void)harness.Worker(1u, V1LifecycleEvent::EngineReady);
    std::uint32_t actions = harness.Worker(
        1u, V1LifecycleEvent::TransportRetryableFailure);
    CHECK(Has(actions, V1ActionScheduleTransportRetry));
    CHECK(Has(actions, V1ActionCancelTransport));
    CHECK(Has(actions, V1ActionStopEngine));
    actions = harness.Disconnect();
    CHECK(Has(actions, V1ActionPublishEndpointAbsent));
    CheckDisconnected(harness);

    (void)harness.Connect();
    CHECK(harness.active_generation() == 2u);
    CHECK(harness.Worker(
        1u, V1LifecycleEvent::TransportRetryDue) == 0u);
    StartMedia(&harness, 2u);
    const ScenarioSnapshot streaming = harness.Snapshot();
    constexpr std::array<V1LifecycleEvent, 4> stale = {
        V1LifecycleEvent::MediaStarted,
        V1LifecycleEvent::MediaStopped,
        V1LifecycleEvent::EngineExited,
        V1LifecycleEvent::TransportRetryDue,
    };
    for (const V1LifecycleEvent event : stale) {
        CHECK(harness.Worker(1u, event) == 0u);
        CHECK(harness.SameAs(streaming));
    }
    GracefulStopAndDisconnect(&harness, 2u);
}

void TestDisconnectDuringPlaybackCancelsLocally() {
    LifecycleHarness harness;
    (void)harness.Connect();
    StartMedia(&harness, 1u);
    const std::uint32_t actions = harness.Disconnect();
    CHECK(Has(actions, V1ActionFailMute));
    CHECK(Has(actions, V1ActionCancelTransport));
    CHECK(Has(actions, V1ActionStopEngine));
    CHECK(Has(actions, V1ActionPublishEndpointAbsent));
    CHECK(!Has(actions, V1ActionGracefulStopTransport));
    CheckDisconnected(harness);

    const ScenarioSnapshot disconnected = harness.Snapshot();
    constexpr std::array<V1LifecycleEvent, 4> stale = {
        V1LifecycleEvent::MediaStarted,
        V1LifecycleEvent::MediaStopped,
        V1LifecycleEvent::EngineExited,
        V1LifecycleEvent::TransportRetryDue,
    };
    for (const V1LifecycleEvent event : stale) {
        CHECK(harness.Worker(1u, event) == 0u);
        CHECK(harness.SameAs(disconnected));
    }

    (void)harness.Connect();
    CHECK(harness.active_generation() == 2u);
    StartMedia(&harness, 2u);
    const ScenarioSnapshot generation_two = harness.Snapshot();
    for (const V1LifecycleEvent event : stale) {
        CHECK(harness.Worker(1u, event) == 0u);
        CHECK(harness.SameAs(generation_two));
    }
    GracefulStopAndDisconnect(&harness, 2u);
}

void TestNormalStopAndLateCallbacksAreIdempotent() {
    LifecycleHarness harness;
    (void)harness.Connect();
    StartMedia(&harness, 1u);

    std::uint32_t actions = harness.Render(
        V1LifecycleEvent::RenderStopped);
    CHECK(Has(actions, V1ActionGracefulStopTransport));
    CHECK(Has(actions, V1ActionStopEngine));
    CHECK(!Has(actions, V1ActionCancelTransport));
    CHECK(!Has(actions, V1ActionOpenTransport));

    // The real host performs Stop synchronously. If a duplicate render-stop
    // was already queued before MediaStopped is reduced, it may repeat the
    // idempotent local StopEngine request but must not repeat transport stop.
    actions = harness.Render(V1LifecycleEvent::RenderStopped);
    CHECK(Has(actions, V1ActionStopEngine));
    CHECK(!Has(actions, V1ActionGracefulStopTransport));
    CHECK(!Has(actions, V1ActionCancelTransport));
    CHECK(!Has(actions, V1ActionOpenTransport));

    actions = harness.Worker(1u, V1LifecycleEvent::MediaStopped);
    CHECK(actions == 0u);
    const ScenarioSnapshot stopped = harness.Snapshot();
    constexpr std::array<V1LifecycleEvent, 5> late_current = {
        V1LifecycleEvent::MediaStopped,
        V1LifecycleEvent::MediaStarted,
        V1LifecycleEvent::EngineExited,
        V1LifecycleEvent::TransportRetryDue,
        V1LifecycleEvent::EngineReady,
    };
    for (const V1LifecycleEvent event : late_current) {
        actions = harness.Worker(1u, event);
        CHECK(!Has(actions, V1ActionOpenTransport));
        CHECK(!Has(actions, V1ActionGracefulStopTransport));
        CHECK(!Has(actions, V1ActionCancelTransport));
        CHECK(harness.SameAs(stopped));
    }
    actions = harness.Render(V1LifecycleEvent::RenderStopped);
    CHECK(actions == 0u);
    CHECK(harness.SameAs(stopped));

    actions = harness.Disconnect();
    CHECK(Has(actions, V1ActionFailMute));
    CHECK(Has(actions, V1ActionPublishEndpointAbsent));
    CheckDisconnected(harness);
    const ScenarioSnapshot disconnected = harness.Snapshot();
    actions = harness.Disconnect();
    CHECK(Has(actions, V1ActionFailMute));
    CHECK(Has(actions, V1ActionPublishEndpointAbsent));
    CHECK(!Has(actions, V1ActionStopEngine));
    CHECK(!Has(actions, V1ActionCancelTransport));
    CHECK(!Has(actions, V1ActionGracefulStopTransport));
    CHECK(harness.SameAs(disconnected));
}

void TestPlaybackDisconnectLateEventsCannotAffectTakeover() {
    LifecycleHarness harness;
    (void)harness.Connect();
    StartMedia(&harness, 1u);
    std::uint32_t actions = harness.Disconnect();
    CHECK(Has(actions, V1ActionCancelTransport));
    CHECK(Has(actions, V1ActionStopEngine));
    CHECK(!Has(actions, V1ActionGracefulStopTransport));
    CheckDisconnected(harness);

    const ScenarioSnapshot disconnected = harness.Snapshot();
    constexpr std::array<V1LifecycleEvent, 6> old_callbacks = {
        V1LifecycleEvent::EngineReady,
        V1LifecycleEvent::MediaStarted,
        V1LifecycleEvent::MediaStopped,
        V1LifecycleEvent::EngineExited,
        V1LifecycleEvent::TransportRetryDue,
        V1LifecycleEvent::TransportRetryableFailure,
    };
    for (const V1LifecycleEvent event : old_callbacks) {
        CHECK(harness.Worker(1u, event) == 0u);
        CHECK(harness.SameAs(disconnected));
    }

    (void)harness.Connect();
    CHECK(harness.active_generation() == 2u);
    const ScenarioSnapshot generation_two_idle = harness.Snapshot();
    for (const V1LifecycleEvent event : old_callbacks) {
        CHECK(harness.Worker(1u, event) == 0u);
        CHECK(harness.SameAs(generation_two_idle));
    }
    StartMedia(&harness, 2u);
    const ScenarioSnapshot generation_two_streaming = harness.Snapshot();
    for (const V1LifecycleEvent event : old_callbacks) {
        CHECK(harness.Worker(1u, event) == 0u);
        CHECK(harness.SameAs(generation_two_streaming));
    }

    // Duplicate callbacks from the current worker are also idempotent while
    // streaming; none may authorize a second OPEN.
    actions = harness.Worker(2u, V1LifecycleEvent::EngineReady);
    CHECK(actions == 0u);
    actions = harness.Worker(2u, V1LifecycleEvent::MediaStarted);
    CHECK(actions == 0u);
    actions = harness.Worker(2u, V1LifecycleEvent::TransportRetryDue);
    CHECK(actions == 0u);
    CHECK(harness.SameAs(generation_two_streaming));
    CHECK(harness.open_count() == 2u);
    GracefulStopAndDisconnect(&harness, 2u);
}

void TestManyDailyGenerationsConverge() {
    LifecycleHarness harness;
    constexpr std::uint64_t kGenerations = 64u;
    for (std::uint64_t generation = 1u;
         generation <= kGenerations;
         ++generation) {
        (void)harness.Connect();
        CHECK(harness.active_generation() == generation);
        if (generation > 1u) {
            const ScenarioSnapshot before_stale = harness.Snapshot();
            CHECK(harness.Worker(
                generation - 1u,
                V1LifecycleEvent::TransportRetryDue) == 0u);
            CHECK(harness.Worker(
                generation - 1u,
                V1LifecycleEvent::MediaStopped) == 0u);
            CHECK(harness.SameAs(before_stale));
        }
        StartMedia(&harness, generation);
        if ((generation & 1u) == 0u) {
            GracefulStopAndDisconnect(&harness, generation);
        } else {
            const std::uint32_t actions = harness.Disconnect();
            CHECK(Has(actions, V1ActionCancelTransport));
            CHECK(!Has(actions, V1ActionGracefulStopTransport));
            CheckDisconnected(harness);
        }
    }
    CHECK(harness.lifecycle().acl_generation == kGenerations);
    CHECK(harness.open_count() == kGenerations);
    CheckDisconnected(harness);
}

void TestMultipleSessionsWithinOneAclGeneration() {
    LifecycleHarness harness;
    harness.EnableMultipleMediaSessions();
    (void)harness.Connect();
    StartMedia(&harness, 1u);
    std::uint32_t actions = harness.Render(
        V1LifecycleEvent::RenderStopped);
    CHECK(Has(actions, V1ActionGracefulStopTransport));
    CHECK(Has(actions, V1ActionStopEngine));
    CHECK(harness.Worker(1u, V1LifecycleEvent::MediaStopped) == 0u);
    CHECK(harness.lifecycle().completed_media_sessions_for_generation == 1u);

    actions = harness.Render(V1LifecycleEvent::RenderStarted);
    CHECK(Has(actions, V1ActionStartEngine));
    CHECK(harness.lifecycle().open_attempts_for_generation == 0u);
    actions = harness.Worker(1u, V1LifecycleEvent::EngineReady);
    CHECK(Has(actions, V1ActionOpenTransport));
    CHECK(harness.Worker(1u, V1LifecycleEvent::EngineReady) == 0u);
    CHECK(harness.Worker(1u, V1LifecycleEvent::MediaStarted) == 0u);
    CHECK(harness.open_count() == 2u);

    actions = harness.Worker(1u, V1LifecycleEvent::MediaFailed);
    CHECK(Has(actions, V1ActionFailMute));
    CHECK(harness.lifecycle().media_session == V1MediaSession::Faulted);
    (void)harness.Render(V1LifecycleEvent::RenderStopped);
    actions = harness.Render(V1LifecycleEvent::RenderStarted);
    CHECK(!Has(actions, V1ActionStartEngine));
    CHECK(harness.lifecycle().media_session == V1MediaSession::Faulted);
    (void)harness.Disconnect();
    (void)harness.Connect();
    CHECK(harness.lifecycle().completed_media_sessions_for_generation == 0u);
    CHECK(harness.lifecycle().media_session == V1MediaSession::Stopped);
}

}  // namespace

int main() {
    TestTwoDailyGracefulGenerations();
    TestOldRetryAndCallbacksCannotReachNewMedia();
    TestDisconnectDuringPlaybackCancelsLocally();
    TestNormalStopAndLateCallbacksAreIdempotent();
    TestPlaybackDisconnectLateEventsCannotAffectTakeover();
    TestManyDailyGenerationsConverge();
    TestMultipleSessionsWithinOneAclGeneration();
    if (failures != 0) {
        std::fprintf(stderr,
                     "V1 multi-generation lifecycle scenarios failed: %d.\n",
                     failures);
        return 1;
    }
    std::printf(
        "V1 multi-generation lifecycle scenarios passed: graceful, "
        "local-cancel, stale-event isolation, and 64-generation convergence.\n");
    return 0;
}
