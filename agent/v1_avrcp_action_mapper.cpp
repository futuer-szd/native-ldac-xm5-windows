// SPDX-License-Identifier: Apache-2.0
#include "v1_avrcp_action_mapper.h"

namespace native_ldac::agent {
namespace {

bool IsCurrentGeneration(const V1AvrcpControlMapperState* state,
                         std::uint64_t generation) {
    return state != nullptr && generation != 0u &&
           state->acl_generation_current &&
           state->acl_generation == generation;
}

V1AvrcpActionSet Snapshot(const V1AvrcpControlMapperState& state) {
    V1AvrcpActionSet actions;
    actions.acl_generation = state.acl_generation;
    actions.authorized_current =
        state.acl_generation_current && state.owner_lease != 0u;
    return actions;
}

void MergePassThroughActions(
    const V1MediaSessionEligibility& eligibility,
    V1AvrcpActionSet* actions,
                             const AvrcpPassThroughDecision& decision) {
    if (actions == nullptr) return;
    actions->event_observed =
        actions->event_observed || decision.event_observed;
    if (HasAvrcpPassThroughAction(
            decision, AvrcpPassThroughActionStepVolumeUp)) {
        actions->actions |= V1AvrcpActionStepVolumeUp;
    }
    if (HasAvrcpPassThroughAction(
            decision, AvrcpPassThroughActionStepVolumeDown)) {
        actions->actions |= V1AvrcpActionStepVolumeDown;
    }
    if (HasAvrcpPassThroughAction(
            decision, AvrcpPassThroughActionToggleMute)) {
        actions->actions |= V1AvrcpActionToggleMute;
    }
    const bool play_requested =
        HasAvrcpPassThroughAction(decision, AvrcpPassThroughActionPlay);
    const bool pause_requested =
        HasAvrcpPassThroughAction(decision, AvrcpPassThroughActionPause);
    if (eligibility.pause_eligible && play_requested) {
        // Some XM5 firmware revisions have emitted PLAY for the physical
        // play/pause gesture while the remote session was already playing.
        // Treat that exact, routed event as a toggle-to-pause fallback. The
        // normal PAUSE opcode below remains the preferred standard path.
        actions->actions |= V1AvrcpActionMediaPlayPause;
        actions->playback_after = V1AvrcpPlaybackState::Paused;
        actions->playback_changed = true;
    } else if (eligibility.play_eligible && play_requested) {
        actions->actions |= V1AvrcpActionMediaPlay;
        actions->playback_after = V1AvrcpPlaybackState::Playing;
        actions->playback_changed = true;
    }
    if (eligibility.play_eligible && pause_requested) {
        // The inverse mismatch has also been observed on XM5: while the PC
        // session is paused, the physical play/pause gesture may arrive as
        // PAUSE. Use the PC eligibility as the authority and toggle to play.
        actions->actions |= V1AvrcpActionMediaPlayPause;
        actions->playback_after = V1AvrcpPlaybackState::Playing;
        actions->playback_changed = true;
    } else if (eligibility.pause_eligible && pause_requested) {
        actions->actions |= V1AvrcpActionMediaPause;
        actions->playback_after = V1AvrcpPlaybackState::Paused;
        actions->playback_changed = true;
    }
    if (eligibility.session_present &&
        HasAvrcpPassThroughAction(decision, AvrcpPassThroughActionStop)) {
        actions->actions |= V1AvrcpActionMediaStop;
        actions->playback_after = V1AvrcpPlaybackState::Stopped;
        actions->playback_changed = true;
    }
    if (eligibility.next_eligible && HasAvrcpPassThroughAction(
            decision, AvrcpPassThroughActionNextTrack)) {
        actions->actions |= V1AvrcpActionMediaNextTrack;
    }
    if (eligibility.previous_eligible && HasAvrcpPassThroughAction(
            decision, AvrcpPassThroughActionPreviousTrack)) {
        actions->actions |= V1AvrcpActionMediaPreviousTrack;
    }
}

void UpdatePlaybackState(V1AvrcpControlMapperState* state,
                         const V1AvrcpActionSet& actions) {
    if (state == nullptr) return;
    if (actions.playback_changed) {
        state->playback = actions.playback_after;
        if (V1AvrcpHasAction(actions,
                             V1AvrcpActionNotifyPlaybackStatus)) {
            state->last_playback_status = actions.playback_after;
            state->playback_status_valid = true;
        }
        return;
    }
    if (V1AvrcpHasAction(actions, V1AvrcpActionMediaPlay)) {
        state->playback = V1AvrcpPlaybackState::Playing;
    } else if (V1AvrcpHasAction(actions, V1AvrcpActionMediaPause)) {
        state->playback = V1AvrcpPlaybackState::Paused;
    } else if (V1AvrcpHasAction(actions, V1AvrcpActionMediaStop)) {
        state->playback = V1AvrcpPlaybackState::Stopped;
    }
}

void FilterHeadsetPreferredPush(V1AvrcpControlMapperState* state,
                               V1AvrcpActionSet* actions) {
    if (state == nullptr || actions == nullptr ||
        !state->headset_preferred || state->xm5_volume_seen) {
        return;
    }
    actions->actions &= ~V1AvrcpActionSendXm5Volume;
}

void MergeVolumeActions(V1AvrcpActionSet* actions,
                        const AvrcpAbsoluteVolumeGateDecision& decision) {
    if (actions == nullptr) return;
    if (HasAvrcpAbsoluteVolumeGateAction(
            decision, AvrcpVolumeActionSetWindowsEndpoint)) {
        actions->actions |= V1AvrcpActionSetWindowsVolume;
        actions->windows_volume = decision.volume.windows_volume;
    }
    if (HasAvrcpAbsoluteVolumeGateAction(
            decision, AvrcpVolumeActionSendXm5AbsoluteVolume)) {
        actions->actions |= V1AvrcpActionSendXm5Volume;
        actions->xm5_absolute_volume = decision.volume.xm5_absolute_volume;
    }
}

}  // namespace

V1AvrcpActionSet V1AvrcpBeginAclGeneration(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation) {
    V1AvrcpActionSet actions;
    if (state == nullptr) return actions;
    (void)BeginAvrcpPassThroughAclGeneration(&state->pass_through,
                                             acl_generation);
    (void)BeginAvrcpAbsoluteVolumeAclGeneration(&state->absolute_volume,
                                                acl_generation);
    if (state->pass_through.acl_generation != acl_generation ||
        !state->pass_through.acl_generation_current ||
        state->absolute_volume.acl_generation != acl_generation ||
        !state->absolute_volume.acl_generation_current) {
        return actions;
    }
    state->acl_generation = acl_generation;
    state->acl_generation_current = true;
    state->owner_lease = 0u;
    state->volume_sync_enabled = false;
    state->media_routing_enabled = false;
    state->media_eligibility = {};
    state->xm5_volume_seen = false;
    state->playback = V1AvrcpPlaybackState::Stopped;
    state->pc_playback = V1AvrcpPlaybackState::Stopped;
    state->pc_playback_valid = false;
    state->playback_status_sync_required = false;
    state->last_playback_status = V1AvrcpPlaybackState::Stopped;
    state->playback_status_valid = false;
    return Snapshot(*state);
}

V1AvrcpActionSet V1AvrcpEndAclGeneration(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation) {
    V1AvrcpActionSet actions;
    if (!IsCurrentGeneration(state, acl_generation)) return actions;
    (void)EndAvrcpPassThroughAclGeneration(&state->pass_through,
                                           acl_generation);
    (void)EndAvrcpAbsoluteVolumeAclGeneration(&state->absolute_volume,
                                              acl_generation);
    state->acl_generation_current = false;
    state->owner_lease = 0u;
    state->volume_sync_enabled = false;
    state->media_routing_enabled = false;
    state->media_eligibility = {};
    state->xm5_volume_seen = false;
    state->playback = V1AvrcpPlaybackState::Stopped;
    state->pc_playback = V1AvrcpPlaybackState::Stopped;
    state->pc_playback_valid = false;
    state->playback_status_sync_required = false;
    state->last_playback_status = V1AvrcpPlaybackState::Stopped;
    state->playback_status_valid = false;
    return Snapshot(*state);
}

V1AvrcpActionSet V1AvrcpSetControlMode(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation,
    bool volume_sync,
    bool media_routing) {
    V1AvrcpActionSet actions;
    if (!IsCurrentGeneration(state, acl_generation)) return actions;
    const bool enabling_media_routing =
        !state->media_routing_enabled && media_routing;
    (void)SetAvrcpPassThroughGateMode(
        &state->pass_through,
        acl_generation,
        media_routing ? AvrcpPassThroughGateMode::RouteToCurrentMediaSession
                      : AvrcpPassThroughGateMode::ObserveOnly);
    (void)SetAvrcpAbsoluteVolumeGateMode(
        &state->absolute_volume,
        acl_generation,
        volume_sync ? AvrcpAbsoluteVolumeGateMode::Synchronize
                    : AvrcpAbsoluteVolumeGateMode::ObserveOnly);
    state->volume_sync_enabled = volume_sync;
    state->media_routing_enabled = media_routing;
    if (enabling_media_routing) {
        state->playback_status_sync_required = true;
    }
    return Snapshot(*state);
}

V1AvrcpActionSet V1AvrcpAcquireOwnerLease(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation,
    std::uint64_t owner_lease) {
    V1AvrcpActionSet actions;
    if (!IsCurrentGeneration(state, acl_generation) || owner_lease == 0u) {
        return actions;
    }
    (void)AcquireAvrcpPassThroughOwnerLease(&state->pass_through,
                                            acl_generation,
                                            owner_lease);
    (void)AcquireAvrcpAbsoluteVolumeOwnerLease(&state->absolute_volume,
                                               acl_generation,
                                               owner_lease);
    if (state->pass_through.owner_lease != owner_lease ||
        state->absolute_volume.owner_lease != owner_lease) {
        return actions;
    }
    state->owner_lease = owner_lease;
    // A newly acquired media lease represents a new control connection from
    // the sink's perspective. The next PC snapshot must be sent even when
    // its playback value is unchanged.
    state->playback_status_sync_required = true;
    return Snapshot(*state);
}

V1AvrcpActionSet V1AvrcpSetMediaSessionSnapshot(
    V1AvrcpControlMapperState* state,
    const V1MediaSessionSnapshot& snapshot) {
    V1AvrcpActionSet actions;
    if (state == nullptr ||
        !IsCurrentGeneration(state, snapshot.acl_generation)) {
        return actions;
    }
    const V1AvrcpPlaybackState next_playback =
        snapshot.playback == V1MediaSessionPlayback::Playing
        ? V1AvrcpPlaybackState::Playing
        : snapshot.playback == V1MediaSessionPlayback::Paused
            ? V1AvrcpPlaybackState::Paused
            : V1AvrcpPlaybackState::Stopped;
    const bool pc_playback_changed =
        !state->pc_playback_valid || state->pc_playback != next_playback;
    state->media_eligibility =
        EvaluateV1MediaSessionEligibility(snapshot);
    state->pc_playback = next_playback;
    state->pc_playback_valid = true;
    state->playback = next_playback;

    actions = Snapshot(*state);
    const bool authorized_media_route = state->media_routing_enabled &&
        state->owner_lease != 0u;
    const bool status_needs_sync = authorized_media_route &&
        (state->playback_status_sync_required || pc_playback_changed ||
         !state->playback_status_valid ||
         state->last_playback_status != next_playback);
    if (status_needs_sync) {
        actions.actions |= V1AvrcpActionNotifyPlaybackStatus;
        actions.playback_after = next_playback;
        actions.playback_changed = true;
        state->last_playback_status = next_playback;
        state->playback_status_valid = true;
        state->playback_status_sync_required = false;
    }
    return actions;
}

V1AvrcpActionSet V1AvrcpRevokeOwnerLease(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation,
    std::uint64_t owner_lease) {
    V1AvrcpActionSet actions;
    if (!IsCurrentGeneration(state, acl_generation) || owner_lease == 0u ||
        state->owner_lease != owner_lease) {
        return actions;
    }
    (void)RevokeAvrcpPassThroughOwnerLease(&state->pass_through,
                                           acl_generation,
                                           owner_lease);
    (void)RevokeAvrcpAbsoluteVolumeOwnerLease(&state->absolute_volume,
                                              acl_generation,
                                              owner_lease);
    state->owner_lease = 0u;
    return Snapshot(*state);
}

V1AvrcpActionSet V1AvrcpObserveVolumeCapability(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation,
    bool supported) {
    V1AvrcpActionSet actions;
    if (!IsCurrentGeneration(state, acl_generation)) return actions;
    const AvrcpAbsoluteVolumeGateDecision decision =
        ObserveAvrcpAbsoluteVolumeCapability(
            &state->absolute_volume,
            acl_generation,
            supported ? AvrcpAbsoluteVolumeSupport::Supported
                      : AvrcpAbsoluteVolumeSupport::Unsupported);
    actions = Snapshot(*state);
    MergeVolumeActions(&actions, decision);
    FilterHeadsetPreferredPush(state, &actions);
    return actions;
}

V1AvrcpActionSet V1AvrcpObserveWindowsVolume(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation,
    const AvrcpWindowsVolume& observed) {
    V1AvrcpActionSet actions;
    if (!IsCurrentGeneration(state, acl_generation)) return actions;
    const AvrcpAbsoluteVolumeGateDecision decision =
        ObserveWindowsEndpointVolumeThroughGate(&state->absolute_volume,
                                                acl_generation,
                                                observed);
    actions = Snapshot(*state);
    MergeVolumeActions(&actions, decision);
    FilterHeadsetPreferredPush(state, &actions);
    return actions;
}

V1AvrcpActionSet V1AvrcpObserveXm5AbsoluteVolume(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation,
    std::uint8_t absolute_volume,
    AvrcpXm5VolumeEvent event) {
    V1AvrcpActionSet actions;
    if (!IsCurrentGeneration(state, acl_generation)) return actions;
    const AvrcpAbsoluteVolumeGateDecision decision =
        ObserveXm5AbsoluteVolumeThroughGate(&state->absolute_volume,
                                            acl_generation,
                                            absolute_volume,
                                            event);
    actions = Snapshot(*state);
    MergeVolumeActions(&actions, decision);
    if (state->headset_preferred && !state->xm5_volume_seen) {
        state->xm5_volume_seen = true;
        FilterHeadsetPreferredPush(state, &actions);
        // The headset value is now the shared gain; tell the reducer that
        // this is the value we last sent so the endpoint readback does not
        // immediately push the PC projection back to the headset.
        state->absolute_volume.reducer.last_xm5_volume_sent =
            static_cast<std::uint8_t>(absolute_volume & 0x7Fu);
        state->absolute_volume.reducer.outbound_echo_pending = false;
        if (state->owner_lease != 0u &&
            state->absolute_volume.mode ==
                AvrcpAbsoluteVolumeGateMode::Synchronize &&
            !V1AvrcpHasAction(actions, V1AvrcpActionSetWindowsVolume)) {
            const std::uint8_t percent =
                Xm5AbsoluteVolumeToWindowsPercent(absolute_volume);
            const bool muted = absolute_volume == 0u;
            const bool matches_last =
                state->absolute_volume.windows_volume_observed &&
                state->absolute_volume.last_windows_volume.percent ==
                    percent &&
                state->absolute_volume.last_windows_volume.muted == muted;
            if (!matches_last) {
                actions.actions |= V1AvrcpActionSetWindowsVolume;
                actions.windows_volume.percent = percent;
                actions.windows_volume.muted = muted;
            }
        }
    }
    return actions;
}

V1AvrcpActionSet V1AvrcpObservePassThrough(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation,
    std::uint8_t operation_id,
    bool released) {
    V1AvrcpActionSet actions;
    if (!IsCurrentGeneration(state, acl_generation)) return actions;
    const AvrcpPassThroughEvent event =
        MakeAvrcpPassThroughEvent(operation_id, released);
    const AvrcpPassThroughDecision decision =
        ObserveAvrcpPassThroughEvent(&state->pass_through,
                                     acl_generation,
                                     event);
    actions = Snapshot(*state);
    MergePassThroughActions(state->media_eligibility, &actions, decision);
    UpdatePlaybackState(state, actions);
    return actions;
}

bool V1AvrcpHasAction(const V1AvrcpActionSet& actions,
                      V1AvrcpAction action) {
    return (actions.actions & static_cast<std::uint32_t>(action)) != 0u;
}

const char* V1AvrcpPlaybackStateName(V1AvrcpPlaybackState state) {
    switch (state) {
        case V1AvrcpPlaybackState::Stopped:
            return "stopped";
        case V1AvrcpPlaybackState::Playing:
            return "playing";
        case V1AvrcpPlaybackState::Paused:
            return "paused";
        default:
            return "unknown";
    }
}

}  // namespace native_ldac::agent
