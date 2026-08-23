// SPDX-License-Identifier: Apache-2.0
#include "../v1_avrcp_action_mapper.h"
#include "../xm5_media_control_event.h"

#include <cstdint>
#include <cstdio>

namespace {

using namespace native_ldac::agent;

int failures = 0;

void Expect(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "v1_avrcp_action_mapper_tests: %s\n", message);
}

void TestPassThroughEventConstruction() {
    const auto press = MakeAvrcpPassThroughEvent(0x44u, false);
    Expect(press.operation == AvrcpPassThroughOperation::Play &&
               press.key_state == AvrcpPassThroughKeyState::Pressed,
           "press construction failed");
    const auto release = MakeAvrcpPassThroughEvent(0x44u, true);
    Expect(release.operation == AvrcpPassThroughOperation::Play &&
               release.key_state == AvrcpPassThroughKeyState::Released,
           "release construction failed");
    const auto unknown = MakeAvrcpPassThroughEvent(0x7Eu, false);
    Expect(unknown.operation == AvrcpPassThroughOperation::Unknown,
           "unknown operation should decode to Unknown");
}

void TestAuthorizedRoutingAgainstObservedSequence() {
    V1AvrcpControlMapperState state{};
    V1AvrcpActionSet actions;

    actions = V1AvrcpBeginAclGeneration(&state, 1u);
    Expect(actions.acl_generation == 1u && state.acl_generation_current &&
               !actions.authorized_current,
           "begin generation did not reset the mapper");

    actions = V1AvrcpSetControlMode(&state, 1u, true, true);
    Expect(state.volume_sync_enabled && state.media_routing_enabled,
           "control mode was not stored");

    actions = V1AvrcpAcquireOwnerLease(&state, 1u, 42u);
    Expect(state.owner_lease == 42u && actions.authorized_current,
           "owner lease was not acquired");
    state.headset_preferred = true;
    (void)V1AvrcpSetMediaSessionSnapshot(
        &state,
        {1u, V1MediaSessionPlayback::Paused, true, false, true, true});

    actions = V1AvrcpObserveVolumeCapability(&state, 1u, true);
    Expect(actions.actions == V1AvrcpActionNone,
           "capability alone must not emit actions");

    AvrcpWindowsVolume windows{};
    windows.percent = 50u;
    windows.muted = false;
    actions = V1AvrcpObserveWindowsVolume(&state, 1u, windows);
    Expect(actions.actions == V1AvrcpActionNone,
           "headset-preferred initial sync must not push the PC volume");

    actions = V1AvrcpObserveXm5AbsoluteVolume(
        &state, 1u, 0x15u, AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionSetWindowsVolume) &&
               actions.windows_volume.percent == 17u &&
               !actions.windows_volume.muted,
           "remote XM5 volume 0x15 did not map to Windows 17%%");

    actions = V1AvrcpObserveXm5AbsoluteVolume(
        &state, 1u, 0x15u, AvrcpXm5VolumeEvent::CommandResponse);
    Expect(actions.actions == V1AvrcpActionNone,
           "interim echo must not produce an action");

    AvrcpWindowsVolume feedback{};
    feedback.percent = 17u;
    feedback.muted = false;
    actions = V1AvrcpObserveWindowsVolume(&state, 1u, feedback);
    Expect(!V1AvrcpHasAction(actions, V1AvrcpActionSetWindowsVolume),
           "endpoint feedback must not loop into another volume write");

    actions = V1AvrcpObserveXm5AbsoluteVolume(
        &state, 1u, 0x19u, AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionSetWindowsVolume) &&
               actions.windows_volume.percent == 20u,
           "remote XM5 volume 0x19 did not map to Windows 20%%");

    actions = V1AvrcpObserveXm5AbsoluteVolume(
        &state, 1u, 0x1Eu, AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionSetWindowsVolume) &&
               actions.windows_volume.percent == 24u,
           "remote XM5 volume 0x1E did not map to Windows 24%%");

    actions = V1AvrcpObservePassThrough(&state, 1u, 0x44u, false);
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionMediaPlay) &&
               state.playback == V1AvrcpPlaybackState::Playing,
           "PLAY press did not route to media play");
    actions = V1AvrcpObservePassThrough(&state, 1u, 0x44u, true);
    Expect(actions.actions == V1AvrcpActionNone,
           "PLAY release must not repeat the action");
    (void)V1AvrcpSetMediaSessionSnapshot(
        &state,
        {1u, V1MediaSessionPlayback::Playing, false, true, true, true});

    actions = V1AvrcpObservePassThrough(&state, 1u, 0x4Bu, false);
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionMediaNextTrack),
           "FORWARD press did not route to next track");
    actions = V1AvrcpObservePassThrough(&state, 1u, 0x4Bu, true);
    Expect(actions.actions == V1AvrcpActionNone,
           "FORWARD release must not repeat the action");

    actions = V1AvrcpObservePassThrough(&state, 1u, 0x4Cu, false);
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionMediaPreviousTrack),
           "BACKWARD press did not route to previous track");
    (void)V1AvrcpObservePassThrough(&state, 1u, 0x4Cu, true);

    actions = V1AvrcpObservePassThrough(&state, 1u, 0x46u, false);
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionMediaPause) &&
               state.playback == V1AvrcpPlaybackState::Paused,
           "PAUSE press did not route to media pause");
    (void)V1AvrcpObservePassThrough(&state, 1u, 0x46u, true);

    actions = V1AvrcpObservePassThrough(&state, 1u, 0x41u, false);
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionStepVolumeUp),
           "volume-up pass-through did not route");
    (void)V1AvrcpObservePassThrough(&state, 1u, 0x41u, true);

    actions = V1AvrcpObservePassThrough(&state, 1u, 0x42u, false);
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionStepVolumeDown),
           "volume-down pass-through did not route");
    (void)V1AvrcpObservePassThrough(&state, 1u, 0x42u, true);

    actions = V1AvrcpRevokeOwnerLease(&state, 1u, 42u);
    Expect(state.owner_lease == 0u && !actions.authorized_current,
           "lease revoke did not fail-close");

    actions = V1AvrcpObservePassThrough(&state, 1u, 0x44u, false);
    Expect(actions.actions == V1AvrcpActionNone &&
               state.playback == V1AvrcpPlaybackState::Paused,
           "unauthorized press must not route or change playback");

    actions = V1AvrcpEndAclGeneration(&state, 1u);
    Expect(!state.acl_generation_current && state.owner_lease == 0u &&
               state.playback == V1AvrcpPlaybackState::Stopped,
           "generation end did not fail-close");
}


void TestPcPreferredInitialPush() {
    V1AvrcpControlMapperState state{};
    V1AvrcpActionSet actions;
    (void)V1AvrcpBeginAclGeneration(&state, 5u);
    (void)V1AvrcpSetControlMode(&state, 5u, true, false);
    (void)V1AvrcpAcquireOwnerLease(&state, 5u, 7u);
    state.headset_preferred = false;
    AvrcpWindowsVolume windows{50u, false};
    actions = V1AvrcpObserveWindowsVolume(&state, 5u, windows);
    Expect(actions.actions == V1AvrcpActionNone,
           "pc-preferred window observation must wait for capability");
    actions = V1AvrcpObserveVolumeCapability(&state, 5u, true);
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionSendXm5Volume) &&
               actions.xm5_absolute_volume == 64u,
           "pc-preferred capability activation must push the PC volume");
}

void TestPcPlaybackStatusSync() {
    V1AvrcpControlMapperState state{};
    (void)V1AvrcpBeginAclGeneration(&state, 40u);
    (void)V1AvrcpSetControlMode(&state, 40u, false, true);
    (void)V1AvrcpAcquireOwnerLease(&state, 40u, 9u);

    auto actions = V1AvrcpSetMediaSessionSnapshot(
        &state,
        {40u, V1MediaSessionPlayback::Playing, false, true, true, true});
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionNotifyPlaybackStatus) &&
               actions.playback_changed &&
               actions.playback_after == V1AvrcpPlaybackState::Playing &&
               state.pc_playback == V1AvrcpPlaybackState::Playing,
           "initial PC playback state did not produce a status notification");

    actions = V1AvrcpSetMediaSessionSnapshot(
        &state,
        {40u, V1MediaSessionPlayback::Playing, false, true, true, true});
    Expect(actions.actions == V1AvrcpActionNone,
           "unchanged PC playback state produced a duplicate notification");

    actions = V1AvrcpSetMediaSessionSnapshot(
        &state,
        {40u, V1MediaSessionPlayback::Paused, true, false, true, true});
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionNotifyPlaybackStatus) &&
               actions.playback_after == V1AvrcpPlaybackState::Paused &&
               state.pc_playback == V1AvrcpPlaybackState::Paused,
           "PC playback transition to paused was not announced");

    (void)V1AvrcpRevokeOwnerLease(&state, 40u, 9u);
    (void)V1AvrcpAcquireOwnerLease(&state, 40u, 10u);
    actions = V1AvrcpSetMediaSessionSnapshot(
        &state,
        {40u, V1MediaSessionPlayback::Paused, true, false, true, true});
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionNotifyPlaybackStatus),
           "new owner lease did not force a current playback notification");
}

void TestMediaActionWaitsForPcSnapshot() {
    V1AvrcpControlMapperState state{};
    (void)V1AvrcpBeginAclGeneration(&state, 41u);
    (void)V1AvrcpSetControlMode(&state, 41u, false, true);
    (void)V1AvrcpAcquireOwnerLease(&state, 41u, 11u);
    (void)V1AvrcpSetMediaSessionSnapshot(
        &state,
        {41u, V1MediaSessionPlayback::Paused, true, false, true, true});

    auto actions = V1AvrcpObservePassThrough(
        &state, 41u, 0x46u, false);
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionMediaPlayPause) &&
               !V1AvrcpHasAction(
                   actions, V1AvrcpActionNotifyPlaybackStatus),
           "media action must wait for the PC snapshot before notifying XM5");

    actions = V1AvrcpSetMediaSessionSnapshot(
        &state,
        {41u, V1MediaSessionPlayback::Paused, true, false, true, true});
    Expect(actions.actions == V1AvrcpActionNone,
           "unchanged PC snapshot emitted a duplicate status after media action");

    actions = V1AvrcpSetMediaSessionSnapshot(
        &state,
        {41u, V1MediaSessionPlayback::Playing, false, true, true, true});
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionNotifyPlaybackStatus) &&
               actions.playback_after == V1AvrcpPlaybackState::Playing,
           "actual PC transition did not emit the playback status notification");
}

void TestObserveOnlyIsInert() {
    V1AvrcpControlMapperState state{};
    V1AvrcpActionSet actions;

    (void)V1AvrcpBeginAclGeneration(&state, 2u);
    (void)V1AvrcpSetControlMode(&state, 2u, false, false);

    actions = V1AvrcpObservePassThrough(&state, 2u, 0x4Bu, false);
    Expect(actions.actions == V1AvrcpActionNone,
           "observe-only pass-through produced an action");

    actions = V1AvrcpObserveXm5AbsoluteVolume(
        &state, 2u, 0x15u, AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(actions.actions == V1AvrcpActionNone,
           "observe-only XM5 volume produced an action");
}

void TestHeadsetAuthorityResetsPerPhysicalGeneration() {
    V1AvrcpControlMapperState state{};
    (void)V1AvrcpBeginAclGeneration(&state, 10u);
    (void)V1AvrcpSetControlMode(&state, 10u, true, false);
    (void)V1AvrcpAcquireOwnerLease(&state, 10u, 1u);
    state.headset_preferred = true;
    (void)V1AvrcpObserveVolumeCapability(&state, 10u, true);
    (void)V1AvrcpObserveWindowsVolume(&state, 10u, {80u, false});
    const auto actions = V1AvrcpObserveXm5AbsoluteVolume(
        &state, 10u, 32u, AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(state.xm5_volume_seen &&
               V1AvrcpHasAction(
                   actions, V1AvrcpActionSetWindowsVolume),
           "first physical generation did not adopt XM5 volume");

    (void)V1AvrcpRevokeOwnerLease(&state, 10u, 1u);
    (void)V1AvrcpAcquireOwnerLease(&state, 10u, 2u);
    Expect(state.xm5_volume_seen,
           "same physical generation forgot its initial XM5 authority");

    (void)V1AvrcpEndAclGeneration(&state, 10u);
    (void)V1AvrcpBeginAclGeneration(&state, 11u);
    Expect(!state.xm5_volume_seen,
           "new physical generation inherited completed XM5 authority");
}

void TestMicrosoftPreservingVolumeOnlyMode() {
    V1AvrcpControlMapperState state{};
    (void)V1AvrcpBeginAclGeneration(&state, 6u);
    (void)V1AvrcpSetControlMode(&state, 6u, true, false);
    (void)V1AvrcpAcquireOwnerLease(&state, 6u, 8u);
    state.headset_preferred = true;
    (void)V1AvrcpSetMediaSessionSnapshot(
        &state,
        {6u, V1MediaSessionPlayback::Playing,
         false, true, true, true});

    auto actions = V1AvrcpObservePassThrough(
        &state, 6u, 0x46u, false);
    Expect(actions.actions == V1AvrcpActionNone,
           "volume-only mode injected Microsoft-owned PAUSE");
    (void)V1AvrcpObservePassThrough(&state, 6u, 0x46u, true);
    actions = V1AvrcpObservePassThrough(&state, 6u, 0x4Bu, false);
    Expect(actions.actions == V1AvrcpActionNone,
           "volume-only mode injected Microsoft-owned NEXT");

    actions = V1AvrcpObserveXm5AbsoluteVolume(
        &state, 6u, 64u, AvrcpXm5VolumeEvent::RemoteNotification);
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionSetWindowsVolume),
           "volume-only mode blocked XM5 absolute volume");
}

void TestMediaSessionEligibilityGate() {
    V1AvrcpControlMapperState state{};
    (void)V1AvrcpBeginAclGeneration(&state, 30u);
    (void)V1AvrcpSetControlMode(&state, 30u, false, true);
    (void)V1AvrcpAcquireOwnerLease(&state, 30u, 3u);

    auto actions = V1AvrcpObservePassThrough(&state, 30u, 0x44u, false);
    Expect(actions.actions == V1AvrcpActionNone,
           "media play escaped without a Windows media session");
    (void)V1AvrcpObservePassThrough(&state, 30u, 0x44u, true);

    actions = V1AvrcpObservePassThrough(&state, 30u, 0x41u, false);
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionStepVolumeUp),
           "media eligibility incorrectly blocked volume control");
    (void)V1AvrcpObservePassThrough(&state, 30u, 0x41u, true);

    (void)V1AvrcpSetMediaSessionSnapshot(
        &state,
        {30u, V1MediaSessionPlayback::Paused, true, false, false, true});
    actions = V1AvrcpObservePassThrough(&state, 30u, 0x44u, false);
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionMediaPlay),
           "paused eligible session did not accept play");
    (void)V1AvrcpObservePassThrough(&state, 30u, 0x44u, true);

    (void)V1AvrcpSetMediaSessionSnapshot(
        &state,
        {30u, V1MediaSessionPlayback::Playing, false, false, false, true});
    actions = V1AvrcpObservePassThrough(&state, 30u, 0x44u, false);
    Expect(actions.actions == V1AvrcpActionNone &&
               state.playback == V1AvrcpPlaybackState::Playing,
           "playing PLAY fallback escaped a disabled pause gate");
    (void)V1AvrcpObservePassThrough(&state, 30u, 0x44u, true);

    (void)V1AvrcpSetMediaSessionSnapshot(
        &state,
        {30u, V1MediaSessionPlayback::Playing, false, true, false, true});
    actions = V1AvrcpObservePassThrough(&state, 30u, 0x44u, false);
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionMediaPlayPause) &&
               !V1AvrcpHasAction(actions, V1AvrcpActionMediaPlay) &&
               !V1AvrcpHasAction(actions, V1AvrcpActionMediaPause) &&
               state.playback == V1AvrcpPlaybackState::Paused,
           "playing XM5 PLAY did not use the toggle-to-pause fallback");
    (void)V1AvrcpObservePassThrough(&state, 30u, 0x44u, true);

    (void)V1AvrcpSetMediaSessionSnapshot(
        &state,
        {30u, V1MediaSessionPlayback::Paused, true, false, false, true});
    actions = V1AvrcpObservePassThrough(&state, 30u, 0x46u, false);
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionMediaPlayPause) &&
               !V1AvrcpHasAction(actions, V1AvrcpActionMediaPlay) &&
               !V1AvrcpHasAction(actions, V1AvrcpActionMediaPause) &&
               state.playback == V1AvrcpPlaybackState::Playing &&
               state.pc_playback == V1AvrcpPlaybackState::Paused,
           "paused XM5 PAUSE did not use the toggle-to-play fallback");
    (void)V1AvrcpObservePassThrough(&state, 30u, 0x46u, true);

    actions = V1AvrcpObservePassThrough(&state, 30u, 0x4Bu, false);
    Expect(!V1AvrcpHasAction(actions, V1AvrcpActionMediaNextTrack),
           "disabled next capability emitted an action");
    (void)V1AvrcpObservePassThrough(&state, 30u, 0x4Bu, true);
    actions = V1AvrcpObservePassThrough(&state, 30u, 0x4Cu, false);
    Expect(V1AvrcpHasAction(actions, V1AvrcpActionMediaPreviousTrack),
           "enabled previous capability was blocked");

    const auto before = state.playback;
    (void)V1AvrcpSetMediaSessionSnapshot(
        &state,
        {29u, V1MediaSessionPlayback::Playing, true, true, true, true});
    Expect(state.playback == before,
           "stale media snapshot changed current generation state");
}

void TestVirtualKeyInverseMapping() {
    Expect(Xm5MediaControlVirtualKey(Xm5MediaControl::PlayPause) == 0xB3u,
           "play-pause VK mapping changed");
    Expect(Xm5MediaControlVirtualKey(Xm5MediaControl::NextTrack) == 0xB0u,
           "next-track VK mapping changed");
    Expect(Xm5MediaControlVirtualKey(Xm5MediaControl::PreviousTrack) == 0xB1u,
           "previous-track VK mapping changed");
    Expect(Xm5MediaControlVirtualKey(Xm5MediaControl::VolumeUp) == 0xAFu,
           "volume-up VK mapping changed");
    Expect(Xm5MediaControlVirtualKey(Xm5MediaControl::VolumeDown) == 0xAEu,
           "volume-down VK mapping changed");
    Expect(Xm5MediaControlVirtualKey(Xm5MediaControl::Mute) == 0xADu,
           "mute VK mapping changed");
    Expect(Xm5MediaControlVirtualKey(Xm5MediaControl::Unknown) == 0u,
           "unknown VK mapping changed");
}

}  // namespace

int main() {
    TestPassThroughEventConstruction();
    TestAuthorizedRoutingAgainstObservedSequence();
    TestPcPreferredInitialPush();
    TestPcPlaybackStatusSync();
    TestMediaActionWaitsForPcSnapshot();
    TestObserveOnlyIsInert();
    TestMicrosoftPreservingVolumeOnlyMode();
    TestHeadsetAuthorityResetsPerPhysicalGeneration();
    TestMediaSessionEligibilityGate();
    TestVirtualKeyInverseMapping();
    if (failures == 0) {
        std::puts("V1 AVRCP action mapper offline tests passed.");
    }
    return failures == 0 ? 0 : 1;
}
