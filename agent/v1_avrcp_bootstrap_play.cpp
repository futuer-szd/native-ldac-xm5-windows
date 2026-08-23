// SPDX-License-Identifier: Apache-2.0
#include "v1_avrcp_bootstrap_play.h"

namespace native_ldac::agent {

void ResetV1AvrcpBootstrapPlay(
    V1AvrcpBootstrapPlayState* state,
    std::uint64_t acl_generation) {
    if (state == nullptr) return;
    state->pending = false;
    state->acl_generation = acl_generation;
    state->deadline_ms = 0u;
}

V1AvrcpBootstrapPlayDecision ObserveV1AvrcpBootstrapPlayGesture(
    V1AvrcpBootstrapPlayState* state,
    const V1MediaSessionSnapshot& media,
    bool play_like_press,
    std::uint64_t now_ms) {
    if (state == nullptr || !play_like_press ||
        media.acl_generation == 0u ||
        media.acl_generation != state->acl_generation ||
        media.playback != V1MediaSessionPlayback::Paused ||
        !media.play_enabled) {
        return V1AvrcpBootstrapPlayDecision::None;
    }
    if (state->pending) {
        return V1AvrcpBootstrapPlayDecision::None;
    }
    state->pending = true;
    state->deadline_ms = now_ms + kV1AvrcpBootstrapPlayDelayMs;
    ++state->scheduled_count;
    return V1AvrcpBootstrapPlayDecision::Scheduled;
}

V1AvrcpBootstrapPlayDecision ReconcileV1AvrcpBootstrapPlay(
    V1AvrcpBootstrapPlayState* state,
    const V1MediaSessionSnapshot& media,
    std::uint64_t now_ms) {
    if (state == nullptr || !state->pending) {
        return V1AvrcpBootstrapPlayDecision::None;
    }
    if (media.acl_generation != state->acl_generation) {
        state->pending = false;
        state->deadline_ms = 0u;
        ++state->cancelled_count;
        return V1AvrcpBootstrapPlayDecision::Cancelled;
    }
    if (media.playback == V1MediaSessionPlayback::Playing) {
        state->pending = false;
        state->deadline_ms = 0u;
        ++state->microsoft_handled_count;
        return V1AvrcpBootstrapPlayDecision::MicrosoftHandled;
    }
    if (media.playback != V1MediaSessionPlayback::Paused ||
        !media.play_enabled) {
        state->pending = false;
        state->deadline_ms = 0u;
        ++state->cancelled_count;
        return V1AvrcpBootstrapPlayDecision::Cancelled;
    }
    if (now_ms < state->deadline_ms) {
        return V1AvrcpBootstrapPlayDecision::None;
    }
    state->pending = false;
    state->deadline_ms = 0u;
    ++state->fallback_injected_count;
    return V1AvrcpBootstrapPlayDecision::InjectPlay;
}

}  // namespace native_ldac::agent
