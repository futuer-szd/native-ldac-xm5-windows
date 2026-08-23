// SPDX-License-Identifier: Apache-2.0
#include "../avrcp_absolute_volume_state.h"

#include <cstdio>
#include <limits>

namespace {

using native_ldac::agent::AvrcpAbsoluteVolumeState;
using native_ldac::agent::AvrcpAbsoluteVolumeSupport;
using native_ldac::agent::AvrcpAbsoluteVolumeGateMode;
using native_ldac::agent::AvrcpAbsoluteVolumeGateState;
using native_ldac::agent::AvrcpVolumeActionSendXm5AbsoluteVolume;
using native_ldac::agent::AvrcpVolumeActionSetWindowsEndpoint;
using native_ldac::agent::AvrcpWindowsVolume;
using native_ldac::agent::AvrcpXm5VolumeEvent;
using native_ldac::agent::BeginAvrcpAbsoluteVolumeSession;
using native_ldac::agent::BeginAvrcpAbsoluteVolumeAclGeneration;
using native_ldac::agent::AcquireAvrcpAbsoluteVolumeOwnerLease;
using native_ldac::agent::EndAvrcpAbsoluteVolumeSession;
using native_ldac::agent::EndAvrcpAbsoluteVolumeAclGeneration;
using native_ldac::agent::HasAvrcpAbsoluteVolumeAction;
using native_ldac::agent::HasAvrcpAbsoluteVolumeGateAction;
using native_ldac::agent::IsAvrcpAbsoluteVolumeGateDecisionCurrent;
using native_ldac::agent::ObserveAvrcpAbsoluteVolumeCapability;
using native_ldac::agent::ObserveWindowsEndpointVolume;
using native_ldac::agent::ObserveWindowsEndpointVolumeThroughGate;
using native_ldac::agent::ObserveXm5AbsoluteVolume;
using native_ldac::agent::ObserveXm5AbsoluteVolumeThroughGate;
using native_ldac::agent::RevokeAvrcpAbsoluteVolumeOwnerLease;
using native_ldac::agent::SetAvrcpAbsoluteVolumeSupport;
using native_ldac::agent::SetAvrcpAbsoluteVolumeGateMode;

int failures = 0;

void Expect(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "avrcp_absolute_volume_state_tests: %s\n", message);
}

bool SendsXm5(const native_ldac::agent::AvrcpAbsoluteVolumeDecision& value) {
    return HasAvrcpAbsoluteVolumeAction(
        value, AvrcpVolumeActionSendXm5AbsoluteVolume);
}

bool SetsWindows(
    const native_ldac::agent::AvrcpAbsoluteVolumeDecision& value) {
    return HasAvrcpAbsoluteVolumeAction(
        value, AvrcpVolumeActionSetWindowsEndpoint);
}

void TestMappingsAndMuteProjection() {
    using native_ldac::agent::ProjectWindowsVolumeToXm5;
    using native_ldac::agent::WindowsPercentToXm5AbsoluteVolume;
    using native_ldac::agent::Xm5AbsoluteVolumeToWindowsPercent;

    Expect(WindowsPercentToXm5AbsoluteVolume(0u) == 0u &&
               WindowsPercentToXm5AbsoluteVolume(1u) == 1u &&
               WindowsPercentToXm5AbsoluteVolume(50u) == 64u &&
               WindowsPercentToXm5AbsoluteVolume(100u) == 127u &&
               WindowsPercentToXm5AbsoluteVolume(999u) == 127u,
           "Windows-to-XM5 mapping or clamping changed");
    Expect(Xm5AbsoluteVolumeToWindowsPercent(0u) == 0u &&
               Xm5AbsoluteVolumeToWindowsPercent(1u) == 1u &&
               Xm5AbsoluteVolumeToWindowsPercent(63u) == 50u &&
               Xm5AbsoluteVolumeToWindowsPercent(64u) == 50u &&
               Xm5AbsoluteVolumeToWindowsPercent(127u) == 100u &&
               Xm5AbsoluteVolumeToWindowsPercent(999u) == 100u,
           "XM5-to-Windows mapping or clamping changed");
    Expect(ProjectWindowsVolumeToXm5({80u, true}) == 0u &&
               ProjectWindowsVolumeToXm5({0u, false}) == 0u &&
               ProjectWindowsVolumeToXm5({80u, false}) == 102u,
           "mute and zero-percent projection changed");

    std::uint8_t previous_xm5 = 0u;
    for (std::uint32_t percent = 0u; percent <= 100u; ++percent) {
        const std::uint8_t xm5 =
            WindowsPercentToXm5AbsoluteVolume(percent);
        const std::uint8_t round_trip =
            Xm5AbsoluteVolumeToWindowsPercent(xm5);
        Expect(xm5 >= previous_xm5,
               "Windows-to-XM5 mapping is not monotonic");
        const int difference = static_cast<int>(round_trip) -
                               static_cast<int>(percent);
        Expect(difference >= -1 && difference <= 1,
               "Windows/XM5 round trip exceeds one percent");
        previous_xm5 = xm5;
    }

    std::uint8_t previous_windows = 0u;
    for (std::uint32_t xm5 = 0u; xm5 <= 127u; ++xm5) {
        const std::uint8_t percent =
            Xm5AbsoluteVolumeToWindowsPercent(xm5);
        Expect(percent >= previous_windows,
               "XM5-to-Windows mapping is not monotonic");
        previous_windows = percent;
    }
}

void TestWindowsAuthorityAndLoopSuppression() {
    AvrcpAbsoluteVolumeState state;
    auto decision = BeginAvrcpAbsoluteVolumeSession(
        &state, 1u, AvrcpAbsoluteVolumeSupport::Supported, {50u, false});
    Expect(SendsXm5(decision) && !SetsWindows(decision) &&
               decision.xm5_absolute_volume == 64u &&
               state.first_session_sync_complete,
           "first supported session did not synchronize from Windows");

    decision = BeginAvrcpAbsoluteVolumeSession(
        &state, 1u, AvrcpAbsoluteVolumeSupport::Supported, {90u, false});
    Expect(decision.actions == 0u && state.windows_volume.percent == 50u,
           "duplicate session replaced authoritative Windows state");

    decision = ObserveXm5AbsoluteVolume(
        &state, 1u, 64u, AvrcpXm5VolumeEvent::CommandResponse);
    Expect(decision.actions == 0u && !state.outbound_echo_pending,
           "outbound absolute-volume echo was not suppressed");

    decision = ObserveWindowsEndpointVolume(&state, 1u, {60u, false});
    Expect(SendsXm5(decision) && decision.xm5_absolute_volume == 76u &&
               state.windows_volume.percent == 60u,
           "Windows update was not propagated to XM5");
    decision = ObserveXm5AbsoluteVolume(
        &state, 1u, 64u, AvrcpXm5VolumeEvent::CommandResponse);
    Expect(decision.actions == 0u && state.windows_volume.percent == 60u &&
               state.outbound_echo_pending,
           "stale command response changed authority or acknowledged a newer send");
    decision = ObserveWindowsEndpointVolume(&state, 1u, {60u, false});
    Expect(decision.actions == 0u,
           "duplicate Windows update produced another XM5 command");

    decision = ObserveXm5AbsoluteVolume(
        &state, 1u, 80u, AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(SetsWindows(decision) && !SendsXm5(decision) &&
               decision.windows_volume.percent == 63u &&
               !decision.windows_volume.muted &&
               state.windows_volume.percent == 60u,
           "XM5 update did not request Windows update without assuming authority");
    decision = ObserveXm5AbsoluteVolume(
        &state, 1u, 80u, AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(decision.actions == 0u,
           "duplicate XM5 update repeated a Windows endpoint request");

    decision = ObserveWindowsEndpointVolume(&state, 1u, {63u, false});
    Expect(decision.actions == 0u && state.windows_volume.percent == 63u &&
               !state.windows_update_pending &&
               state.last_xm5_volume_sent == 80u,
           "matching Windows callback was not accepted and loop-suppressed");
    decision = ObserveWindowsEndpointVolume(&state, 1u, {63u, false});
    Expect(decision.actions == 0u,
           "duplicate matching Windows callback caused a delayed echo loop");

    decision = ObserveXm5AbsoluteVolume(
        &state, 1u, 90u, AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(SetsWindows(decision),
           "new XM5 value did not replace the previous value");
    decision = ObserveWindowsEndpointVolume(&state, 1u, {75u, false});
    Expect(SendsXm5(decision) && decision.xm5_absolute_volume == 95u,
           "non-matching Windows callback did not reassert Windows authority");
}

void TestMuteAndZero() {
    AvrcpAbsoluteVolumeState state;
    auto decision = BeginAvrcpAbsoluteVolumeSession(
        &state, 7u, AvrcpAbsoluteVolumeSupport::Supported, {80u, true});
    Expect(SendsXm5(decision) && decision.xm5_absolute_volume == 0u,
           "Windows mute did not synchronize as XM5 zero");
    decision = ObserveXm5AbsoluteVolume(
        &state, 7u, 0u, AvrcpXm5VolumeEvent::CommandResponse);
    Expect(decision.actions == 0u,
           "zero echo after muted first sync was not suppressed");

    decision = ObserveXm5AbsoluteVolume(
        &state, 7u, 20u, AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(SetsWindows(decision) &&
               decision.windows_volume.percent == 16u &&
               !decision.windows_volume.muted,
           "raising XM5 from zero did not request Windows unmute");
    (void)ObserveWindowsEndpointVolume(&state, 7u, {16u, false});
    decision = ObserveXm5AbsoluteVolume(
        &state, 7u, 0u, AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(SetsWindows(decision) && decision.windows_volume.percent == 0u &&
               decision.windows_volume.muted,
           "XM5 zero did not request a safely muted Windows endpoint");

    decision = ObserveWindowsEndpointVolume(&state, 7u, {0u, false});
    Expect(decision.actions == 0u,
           "zero-percent callback was not equivalent to muted AVRCP zero");
    decision = ObserveWindowsEndpointVolume(&state, 7u, {0u, false});
    Expect(decision.actions == 0u,
           "duplicate zero-percent callback caused a delayed echo loop");

    decision = ObserveXm5AbsoluteVolume(
        &state, 7u, 10u, AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(SetsWindows(decision),
           "XM5 raise after zero did not request a Windows update");
    decision = ObserveWindowsEndpointVolume(&state, 7u, {80u, true});
    Expect(SendsXm5(decision) && decision.xm5_absolute_volume == 0u,
           "non-matching muted callback did not reassert Windows mute");
}

void TestCapabilityFallbackAndLateDiscovery() {
    AvrcpAbsoluteVolumeState state;
    auto decision = BeginAvrcpAbsoluteVolumeSession(
        &state, 10u, AvrcpAbsoluteVolumeSupport::Unsupported, {40u, false});
    Expect(decision.actions == 0u && !state.first_session_sync_complete,
           "unsupported session attempted first synchronization");
    decision = ObserveWindowsEndpointVolume(&state, 10u, {45u, false});
    Expect(decision.actions == 0u && state.windows_volume.percent == 45u,
           "unsupported session did not remain local-only");
    decision = ObserveXm5AbsoluteVolume(
        &state, 10u, 100u, AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(decision.actions == 0u && state.windows_volume.percent == 45u,
           "unsupported session accepted remote volume input");

    decision = SetAvrcpAbsoluteVolumeSupport(
        &state, 10u, AvrcpAbsoluteVolumeSupport::Supported);
    Expect(SendsXm5(decision) && decision.xm5_absolute_volume == 57u,
           "late Absolute Volume discovery did not sync current Windows state");
    decision = SetAvrcpAbsoluteVolumeSupport(
        &state, 10u, AvrcpAbsoluteVolumeSupport::Supported);
    Expect(decision.actions == 0u,
           "duplicate capability discovery repeated first sync");
    decision = SetAvrcpAbsoluteVolumeSupport(
        &state, 10u, AvrcpAbsoluteVolumeSupport::Unsupported);
    Expect(decision.actions == 0u,
           "capability loss emitted a transport or endpoint action");
    (void)ObserveWindowsEndpointVolume(&state, 10u, {46u, false});
    decision = SetAvrcpAbsoluteVolumeSupport(
        &state, 10u, AvrcpAbsoluteVolumeSupport::Supported);
    Expect(SendsXm5(decision) && decision.xm5_absolute_volume == 58u,
           "capability restoration did not resync current Windows state");

    AvrcpAbsoluteVolumeState unknown;
    decision = BeginAvrcpAbsoluteVolumeSession(
        &unknown, 11u, AvrcpAbsoluteVolumeSupport::Unknown, {33u, false});
    Expect(decision.actions == 0u,
           "unknown capability emitted an AVRCP action");
    decision = SetAvrcpAbsoluteVolumeSupport(
        &unknown, 11u, AvrcpAbsoluteVolumeSupport::Unsupported);
    Expect(decision.actions == 0u,
           "unsupported capability fallback emitted an action");
}

void TestGenerationAndRangeGuards() {
    AvrcpAbsoluteVolumeState state;
    (void)BeginAvrcpAbsoluteVolumeSession(
        &state, 20u, AvrcpAbsoluteVolumeSupport::Supported, {25u, false});
    auto decision = ObserveWindowsEndpointVolume(&state, 19u, {90u, false});
    Expect(decision.actions == 0u && state.windows_volume.percent == 25u,
           "stale Windows generation changed authoritative state");
    decision = ObserveXm5AbsoluteVolume(
        &state, 19u, 100u, AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(decision.actions == 0u,
           "stale XM5 generation produced an endpoint action");
    decision = ObserveXm5AbsoluteVolume(
        &state, 20u, 128u, AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(decision.actions == 0u,
           "out-of-range XM5 value was not rejected");
    SetAvrcpAbsoluteVolumeSupport(
        &state, 19u, AvrcpAbsoluteVolumeSupport::Unsupported);
    Expect(state.support == AvrcpAbsoluteVolumeSupport::Supported,
           "stale capability generation changed the current session");

    EndAvrcpAbsoluteVolumeSession(&state, 19u);
    Expect(state.session_active,
           "stale end event stopped the current session");
    EndAvrcpAbsoluteVolumeSession(&state, 20u);
    Expect(!state.session_active &&
               state.support == AvrcpAbsoluteVolumeSupport::Unknown,
           "current end event did not clear session-only state");
    decision = ObserveXm5AbsoluteVolume(
        &state, 20u, 40u, AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(decision.actions == 0u,
           "ended session accepted a remote update");

    decision = BeginAvrcpAbsoluteVolumeSession(
        &state, 20u, AvrcpAbsoluteVolumeSupport::Supported, {90u, false});
    Expect(decision.actions == 0u,
           "duplicate ended generation was reopened");
    decision = BeginAvrcpAbsoluteVolumeSession(
        &state, 21u, AvrcpAbsoluteVolumeSupport::Supported, {100u, false});
    Expect(SendsXm5(decision) && decision.xm5_absolute_volume == 127u &&
               state.generation == 21u,
           "fresh generation did not start from its Windows snapshot");

    AvrcpAbsoluteVolumeState wrapping;
    const std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max();
    decision = BeginAvrcpAbsoluteVolumeSession(
        &wrapping, maximum, AvrcpAbsoluteVolumeSupport::Supported,
        {30u, false});
    Expect(SendsXm5(decision) && wrapping.generation == maximum,
           "initial maximum generation was rejected");
    decision = BeginAvrcpAbsoluteVolumeSession(
        &wrapping, 1u, AvrcpAbsoluteVolumeSupport::Supported, {31u, false});
    Expect(SendsXm5(decision) && wrapping.generation == 1u &&
               wrapping.windows_volume.percent == 31u,
           "maximum generation did not wrap to generation one");
    decision = ObserveWindowsEndpointVolume(
        &wrapping, maximum, {99u, false});
    Expect(decision.actions == 0u && wrapping.windows_volume.percent == 31u,
           "pre-wrap generation was not stale after wrap");
}

void TestFailClosedObservationGate() {
    AvrcpAbsoluteVolumeGateState gate;
    Expect(gate.mode == AvrcpAbsoluteVolumeGateMode::ObserveOnly &&
               !gate.acl_generation_current && gate.owner_lease == 0u,
           "default gate was not fail-closed");

    auto decision = ObserveAvrcpAbsoluteVolumeCapability(
        &gate, 1u, AvrcpAbsoluteVolumeSupport::Supported);
    Expect(decision.volume.actions == 0u,
           "capability without an ACL generation escaped the gate");
    decision = BeginAvrcpAbsoluteVolumeAclGeneration(&gate, 1u);
    Expect(decision.volume.actions == 0u &&
               gate.acl_generation_current &&
               gate.mode == AvrcpAbsoluteVolumeGateMode::ObserveOnly,
           "ACL begin did not remain observe-only");

    decision = ObserveAvrcpAbsoluteVolumeCapability(
        &gate, 1u, AvrcpAbsoluteVolumeSupport::Supported);
    Expect(decision.volume.actions == 0u &&
               gate.observed_support ==
                   AvrcpAbsoluteVolumeSupport::Supported,
           "observe-only capability was not consumed safely");
    decision = ObserveWindowsEndpointVolumeThroughGate(
        &gate, 1u, {40u, false});
    Expect(decision.volume.actions == 0u &&
               gate.windows_volume_observed &&
               gate.last_windows_volume.percent == 40u,
           "observe-only Windows callback was not consumed safely");
    decision = ObserveXm5AbsoluteVolumeThroughGate(
        &gate,
        1u,
        50u,
        AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(decision.volume.actions == 0u && gate.xm5_volume_observed &&
               gate.last_xm5_volume == 50u,
           "observe-only remote notification was not consumed safely");
    decision = ObserveXm5AbsoluteVolumeThroughGate(
        &gate, 1u, 51u, AvrcpXm5VolumeEvent::CommandResponse);
    Expect(decision.volume.actions == 0u &&
               gate.last_xm5_event ==
                   AvrcpXm5VolumeEvent::CommandResponse,
           "observe-only command response was not consumed safely");

    decision = SetAvrcpAbsoluteVolumeGateMode(
        &gate, 1u, AvrcpAbsoluteVolumeGateMode::Synchronize);
    Expect(decision.volume.actions == 0u,
           "Synchronize mode without an owner lease emitted an action");
    decision = AcquireAvrcpAbsoluteVolumeOwnerLease(&gate, 1u, 99u);
    Expect(HasAvrcpAbsoluteVolumeGateAction(
               decision, AvrcpVolumeActionSendXm5AbsoluteVolume) &&
               decision.volume.xm5_absolute_volume == 51u &&
               decision.acl_generation == 1u &&
               decision.owner_lease == 99u &&
               decision.authorization_epoch != 0u &&
               IsAvrcpAbsoluteVolumeGateDecisionCurrent(gate, decision),
           "four-condition authorization did not release first sync");
    auto duplicate_lease = AcquireAvrcpAbsoluteVolumeOwnerLease(
        &gate, 1u, 99u);
    Expect(duplicate_lease.volume.actions == 0u,
           "duplicate owner lease acquisition repeated first sync");
    auto conflicting_lease = AcquireAvrcpAbsoluteVolumeOwnerLease(
        &gate, 1u, 100u);
    Expect(conflicting_lease.volume.actions == 0u &&
               gate.owner_lease == 99u,
           "conflicting owner lease replaced the current owner");

    auto first_sync = decision;
    decision = ObserveXm5AbsoluteVolumeThroughGate(
        &gate,
        1u,
        70u,
        AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(HasAvrcpAbsoluteVolumeGateAction(
               decision, AvrcpVolumeActionSetWindowsEndpoint) &&
               decision.volume.windows_volume.percent == 55u &&
               IsAvrcpAbsoluteVolumeGateDecisionCurrent(gate, decision),
           "authorized remote notification did not request Windows update");
    decision = ObserveWindowsEndpointVolumeThroughGate(
        &gate, 1u, {55u, false});
    Expect(decision.volume.actions == 0u,
           "matching Windows callback escaped remote-to-Windows loop suppression");
    decision = ObserveWindowsEndpointVolumeThroughGate(
        &gate, 1u, {60u, false});
    Expect(HasAvrcpAbsoluteVolumeGateAction(
               decision, AvrcpVolumeActionSendXm5AbsoluteVolume),
           "authorized Windows callback did not synchronize XM5");

    decision = RevokeAvrcpAbsoluteVolumeOwnerLease(&gate, 1u, 99u);
    Expect(decision.volume.actions == 0u && gate.owner_lease == 0u &&
               !gate.reducer.session_active &&
               !IsAvrcpAbsoluteVolumeGateDecisionCurrent(gate, first_sync),
           "owner lease revocation did not invalidate queued actions immediately");
    decision = ObserveWindowsEndpointVolumeThroughGate(
        &gate, 1u, {70u, false});
    Expect(decision.volume.actions == 0u,
           "Windows callback escaped after owner lease revocation");
    decision = ObserveXm5AbsoluteVolumeThroughGate(
        &gate,
        1u,
        80u,
        AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(decision.volume.actions == 0u,
           "remote notification escaped after owner lease revocation");

    decision = AcquireAvrcpAbsoluteVolumeOwnerLease(&gate, 1u, 100u);
    Expect(HasAvrcpAbsoluteVolumeGateAction(
               decision, AvrcpVolumeActionSendXm5AbsoluteVolume) &&
               decision.owner_lease == 100u &&
               decision.volume.xm5_absolute_volume == 89u,
           "fresh owner lease did not resync latest observed Windows state");
    Expect(!IsAvrcpAbsoluteVolumeGateDecisionCurrent(gate, first_sync),
           "revoked decision became current after owner lease reacquisition");
}

void TestGateRevocationAndStaleEvents() {
    AvrcpAbsoluteVolumeGateState gate;
    (void)BeginAvrcpAbsoluteVolumeAclGeneration(&gate, 7u);
    (void)ObserveWindowsEndpointVolumeThroughGate(
        &gate, 7u, {30u, false});
    (void)ObserveAvrcpAbsoluteVolumeCapability(
        &gate, 7u, AvrcpAbsoluteVolumeSupport::Supported);
    (void)AcquireAvrcpAbsoluteVolumeOwnerLease(&gate, 7u, 700u);
    auto decision = SetAvrcpAbsoluteVolumeGateMode(
        &gate, 7u, AvrcpAbsoluteVolumeGateMode::Synchronize);
    Expect(HasAvrcpAbsoluteVolumeGateAction(
               decision, AvrcpVolumeActionSendXm5AbsoluteVolume),
           "ordered authorization did not activate the gate");
    auto authorized = decision;

    decision = ObserveAvrcpAbsoluteVolumeCapability(
        &gate, 6u, AvrcpAbsoluteVolumeSupport::Unsupported);
    Expect(decision.volume.actions == 0u &&
               gate.observed_support ==
                   AvrcpAbsoluteVolumeSupport::Supported,
           "stale capability event changed current authorization");
    decision = RevokeAvrcpAbsoluteVolumeOwnerLease(&gate, 6u, 700u);
    Expect(decision.volume.actions == 0u && gate.owner_lease == 700u,
           "stale generation revoked the current owner lease");
    decision = RevokeAvrcpAbsoluteVolumeOwnerLease(&gate, 7u, 701u);
    Expect(decision.volume.actions == 0u && gate.owner_lease == 700u,
           "non-owner lease revoked the current owner");

    decision = ObserveAvrcpAbsoluteVolumeCapability(
        &gate, 7u, AvrcpAbsoluteVolumeSupport::Unsupported);
    Expect(decision.volume.actions == 0u &&
               !gate.reducer.session_active &&
               !IsAvrcpAbsoluteVolumeGateDecisionCurrent(gate, authorized),
           "capability loss did not invalidate queued actions immediately");
    decision = ObserveWindowsEndpointVolumeThroughGate(
        &gate, 7u, {35u, false});
    Expect(decision.volume.actions == 0u,
           "unsupported capability allowed a Windows action");
    decision = ObserveXm5AbsoluteVolumeThroughGate(
        &gate,
        7u,
        60u,
        AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(decision.volume.actions == 0u,
           "unsupported capability allowed a remote action");

    decision = ObserveAvrcpAbsoluteVolumeCapability(
        &gate, 7u, AvrcpAbsoluteVolumeSupport::Supported);
    Expect(HasAvrcpAbsoluteVolumeGateAction(
               decision, AvrcpVolumeActionSendXm5AbsoluteVolume),
           "capability restoration did not resync through an authorized gate");
    Expect(!IsAvrcpAbsoluteVolumeGateDecisionCurrent(gate, authorized),
           "invalidated decision became current after capability restoration");
    authorized = decision;
    decision = SetAvrcpAbsoluteVolumeGateMode(
        &gate, 7u, AvrcpAbsoluteVolumeGateMode::ObserveOnly);
    Expect(decision.volume.actions == 0u &&
               !gate.reducer.session_active &&
               !IsAvrcpAbsoluteVolumeGateDecisionCurrent(gate, authorized),
           "ObserveOnly transition did not invalidate queued actions");
    decision = ObserveWindowsEndpointVolumeThroughGate(
        &gate, 7u, {36u, false});
    Expect(decision.volume.actions == 0u,
           "ObserveOnly transition still permitted Windows synchronization");
    decision = SetAvrcpAbsoluteVolumeGateMode(
        &gate, 7u, AvrcpAbsoluteVolumeGateMode::Synchronize);
    Expect(HasAvrcpAbsoluteVolumeGateAction(
               decision, AvrcpVolumeActionSendXm5AbsoluteVolume) &&
               !IsAvrcpAbsoluteVolumeGateDecisionCurrent(gate, authorized),
           "old decision became current after Synchronize mode was re-entered");

    decision = EndAvrcpAbsoluteVolumeAclGeneration(&gate, 7u);
    Expect(decision.volume.actions == 0u &&
               !gate.acl_generation_current && gate.owner_lease == 0u &&
               gate.mode == AvrcpAbsoluteVolumeGateMode::ObserveOnly,
           "ACL end did not return the gate to fail-closed state");
    decision = ObserveWindowsEndpointVolumeThroughGate(
        &gate, 7u, {99u, false});
    Expect(decision.volume.actions == 0u &&
               gate.last_windows_volume.percent == 36u,
           "ended ACL generation consumed a stale Windows callback");

    decision = BeginAvrcpAbsoluteVolumeAclGeneration(&gate, 8u);
    Expect(decision.volume.actions == 0u && gate.owner_lease == 0u &&
               gate.mode == AvrcpAbsoluteVolumeGateMode::ObserveOnly &&
               gate.observed_support ==
                   AvrcpAbsoluteVolumeSupport::Unknown,
           "fresh ACL generation inherited write authorization");

    AvrcpAbsoluteVolumeGateState wrapping;
    wrapping.authorization_epoch =
        std::numeric_limits<std::uint64_t>::max();
    const std::uint64_t maximum_generation =
        std::numeric_limits<std::uint64_t>::max();
    (void)BeginAvrcpAbsoluteVolumeAclGeneration(
        &wrapping, maximum_generation);
    Expect(wrapping.acl_generation == maximum_generation &&
               wrapping.authorization_epoch == 1u,
           "gate generation/authorization epoch maximum was not accepted safely");
    (void)ObserveWindowsEndpointVolumeThroughGate(
        &wrapping, maximum_generation, {50u, false});
    (void)ObserveAvrcpAbsoluteVolumeCapability(
        &wrapping,
        maximum_generation,
        AvrcpAbsoluteVolumeSupport::Supported);
    (void)AcquireAvrcpAbsoluteVolumeOwnerLease(
        &wrapping, maximum_generation, 5u);
    auto pre_wrap = SetAvrcpAbsoluteVolumeGateMode(
        &wrapping,
        maximum_generation,
        AvrcpAbsoluteVolumeGateMode::Synchronize);
    Expect(IsAvrcpAbsoluteVolumeGateDecisionCurrent(wrapping, pre_wrap),
           "pre-wrap gate decision was not current");
    decision = BeginAvrcpAbsoluteVolumeAclGeneration(&wrapping, 1u);
    Expect(decision.volume.actions == 0u &&
               wrapping.acl_generation == 1u &&
               wrapping.authorization_epoch == 2u &&
               wrapping.mode == AvrcpAbsoluteVolumeGateMode::ObserveOnly &&
               !IsAvrcpAbsoluteVolumeGateDecisionCurrent(wrapping, pre_wrap),
           "ACL generation wrap did not fail-close and invalidate old actions");
}

}  // namespace

int main() {
    TestMappingsAndMuteProjection();
    TestWindowsAuthorityAndLoopSuppression();
    TestMuteAndZero();
    TestCapabilityFallbackAndLateDiscovery();
    TestGenerationAndRangeGuards();
    TestFailClosedObservationGate();
    TestGateRevocationAndStaleEvents();
    if (failures == 0) {
        std::puts("AVRCP Absolute Volume offline reducer tests passed.");
    }
    return failures == 0 ? 0 : 1;
}
