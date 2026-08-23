// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "avrcp_absolute_volume_state.h"
#include "avrcp_passthrough_state.h"
#include "v1_media_session_eligibility.h"

namespace native_ldac::agent {

enum class V1AvrcpPlaybackState : std::uint8_t {
    Stopped = 0u,
    Playing,
    Paused,
};

enum V1AvrcpAction : std::uint32_t {
    V1AvrcpActionNone = 0u,
    V1AvrcpActionSetWindowsVolume = 1u << 0u,
    V1AvrcpActionStepVolumeUp = 1u << 1u,
    V1AvrcpActionStepVolumeDown = 1u << 2u,
    V1AvrcpActionToggleMute = 1u << 3u,
    V1AvrcpActionMediaPlay = 1u << 4u,
    V1AvrcpActionMediaPause = 1u << 5u,
    V1AvrcpActionMediaPlayPause = 1u << 6u,
    V1AvrcpActionMediaStop = 1u << 7u,
    V1AvrcpActionMediaNextTrack = 1u << 8u,
    V1AvrcpActionMediaPreviousTrack = 1u << 9u,
    V1AvrcpActionSendXm5Volume = 1u << 10u,
    // Sends AVRCP PlaybackStatusChanged without injecting a Windows media key.
    V1AvrcpActionNotifyPlaybackStatus = 1u << 11u,
};

struct V1AvrcpActionSet {
    std::uint32_t actions = V1AvrcpActionNone;
    std::uint64_t acl_generation = 0u;
    bool authorized_current = false;
    bool event_observed = false;
    AvrcpWindowsVolume windows_volume{};
    std::uint8_t xm5_absolute_volume = 0u;
    // Set when the executor should write playback status to the headset
    // (AVRCP PlaybackStatusChanged). This can accompany a media-key action or
    // be a status-only PC-state synchronization action.
    V1AvrcpPlaybackState playback_after = V1AvrcpPlaybackState::Stopped;
    bool playback_changed = false;
};

struct V1AvrcpControlMapperState {
    std::uint64_t acl_generation = 0u;
    bool acl_generation_current = false;
    std::uint64_t owner_lease = 0u;
    bool volume_sync_enabled = false;
    bool media_routing_enabled = false;
    V1MediaSessionEligibility media_eligibility{};
    // Headset-preferred initial sync: until the first XM5 absolute volume is
    // observed, the mapper suppresses pushing the PC volume to the headset
    // and adopts the headset value as the shared gain instead.
    bool headset_preferred = false;
    bool xm5_volume_seen = false;
    // The GSMTC snapshot is the PC-side playback truth. `playback` remains the
    // mapper's effective state for existing diagnostics and is refreshed from
    // this value whenever a snapshot arrives.
    V1AvrcpPlaybackState playback = V1AvrcpPlaybackState::Stopped;
    V1AvrcpPlaybackState pc_playback = V1AvrcpPlaybackState::Stopped;
    bool pc_playback_valid = false;
    bool playback_status_sync_required = false;
    V1AvrcpPlaybackState last_playback_status =
        V1AvrcpPlaybackState::Stopped;
    bool playback_status_valid = false;
    AvrcpPassThroughGateState pass_through{};
    AvrcpAbsoluteVolumeGateState absolute_volume{};
};

V1AvrcpActionSet V1AvrcpBeginAclGeneration(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation);

V1AvrcpActionSet V1AvrcpEndAclGeneration(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation);

V1AvrcpActionSet V1AvrcpSetControlMode(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation,
    bool volume_sync,
    bool media_routing);

V1AvrcpActionSet V1AvrcpAcquireOwnerLease(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation,
    std::uint64_t owner_lease);

V1AvrcpActionSet V1AvrcpSetMediaSessionSnapshot(
    V1AvrcpControlMapperState* state,
    const V1MediaSessionSnapshot& snapshot);

V1AvrcpActionSet V1AvrcpRevokeOwnerLease(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation,
    std::uint64_t owner_lease);

V1AvrcpActionSet V1AvrcpObserveVolumeCapability(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation,
    bool supported);

V1AvrcpActionSet V1AvrcpObserveWindowsVolume(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation,
    const AvrcpWindowsVolume& observed);

V1AvrcpActionSet V1AvrcpObserveXm5AbsoluteVolume(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation,
    std::uint8_t absolute_volume,
    AvrcpXm5VolumeEvent event);

V1AvrcpActionSet V1AvrcpObservePassThrough(
    V1AvrcpControlMapperState* state,
    std::uint64_t acl_generation,
    std::uint8_t operation_id,
    bool released);

bool V1AvrcpHasAction(const V1AvrcpActionSet& actions,
                      V1AvrcpAction action);

const char* V1AvrcpPlaybackStateName(V1AvrcpPlaybackState state);

}  // namespace native_ldac::agent
