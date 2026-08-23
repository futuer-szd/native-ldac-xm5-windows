// SPDX-License-Identifier: Apache-2.0
#include "avrcp_absolute_volume_state.h"

#include <algorithm>
#include <limits>

namespace native_ldac::agent {
namespace {

AvrcpWindowsVolume ClampWindowsVolume(AvrcpWindowsVolume volume) {
    volume.percent = static_cast<std::uint8_t>(
        std::min<std::uint32_t>(volume.percent, 100u));
    return volume;
}

bool IsCurrentSession(const AvrcpAbsoluteVolumeState& state,
                      std::uint64_t generation) {
    return generation != 0u && state.session_active &&
           generation == state.generation;
}

bool IsNewerGeneration(std::uint64_t candidate,
                       std::uint64_t current) {
    if (candidate == 0u) return false;
    if (current == 0u) return true;
    constexpr std::uint64_t kHalfGenerationRange =
        (std::uint64_t{1u} << 63u);
    const std::uint64_t distance = candidate - current;
    return distance != 0u && distance < kHalfGenerationRange;
}

AvrcpAbsoluteVolumeDecision SendAuthoritativeWindowsVolume(
    AvrcpAbsoluteVolumeState* state) {
    AvrcpAbsoluteVolumeDecision decision;
    if (state == nullptr || !state->windows_volume_known ||
        state->support != AvrcpAbsoluteVolumeSupport::Supported) {
        return decision;
    }

    const std::uint8_t projected =
        ProjectWindowsVolumeToXm5(state->windows_volume);
    decision.actions = AvrcpVolumeActionSendXm5AbsoluteVolume;
    decision.xm5_absolute_volume = projected;
    state->first_session_sync_complete = true;
    state->outbound_echo_pending = true;
    state->last_xm5_volume_sent = projected;
    return decision;
}

}  // namespace

std::uint8_t WindowsPercentToXm5AbsoluteVolume(
    std::uint32_t windows_percent) {
    const std::uint32_t bounded = std::min(windows_percent, 100u);
    return static_cast<std::uint8_t>((bounded * 127u + 50u) / 100u);
}

std::uint8_t Xm5AbsoluteVolumeToWindowsPercent(
    std::uint32_t xm5_absolute_volume) {
    const std::uint32_t bounded = std::min(xm5_absolute_volume, 127u);
    return static_cast<std::uint8_t>((bounded * 100u + 63u) / 127u);
}

std::uint8_t ProjectWindowsVolumeToXm5(
    const AvrcpWindowsVolume& volume) {
    return volume.muted || volume.percent == 0u
               ? 0u
               : WindowsPercentToXm5AbsoluteVolume(volume.percent);
}

bool HasAvrcpAbsoluteVolumeAction(
    const AvrcpAbsoluteVolumeDecision& decision,
    AvrcpAbsoluteVolumeAction action) {
    return (decision.actions & static_cast<std::uint32_t>(action)) != 0u;
}

AvrcpAbsoluteVolumeDecision BeginAvrcpAbsoluteVolumeSession(
    AvrcpAbsoluteVolumeState* state,
    std::uint64_t generation,
    AvrcpAbsoluteVolumeSupport support,
    const AvrcpWindowsVolume& observed_windows_volume) {
    AvrcpAbsoluteVolumeDecision decision;
    if (state == nullptr ||
        !IsNewerGeneration(generation, state->generation)) {
        return decision;
    }

    *state = AvrcpAbsoluteVolumeState{};
    state->generation = generation;
    state->session_active = true;
    state->support = support;
    state->windows_volume_known = true;
    state->windows_volume = ClampWindowsVolume(observed_windows_volume);
    return SendAuthoritativeWindowsVolume(state);
}

AvrcpAbsoluteVolumeDecision SetAvrcpAbsoluteVolumeSupport(
    AvrcpAbsoluteVolumeState* state,
    std::uint64_t generation,
    AvrcpAbsoluteVolumeSupport support) {
    AvrcpAbsoluteVolumeDecision decision;
    if (state == nullptr || !IsCurrentSession(*state, generation)) {
        return decision;
    }

    state->support = support;
    if (support != AvrcpAbsoluteVolumeSupport::Supported) {
        state->first_session_sync_complete = false;
        state->outbound_echo_pending = false;
        state->windows_update_pending = false;
        return decision;
    }
    if (!state->first_session_sync_complete) {
        return SendAuthoritativeWindowsVolume(state);
    }
    return decision;
}

AvrcpAbsoluteVolumeDecision ObserveWindowsEndpointVolume(
    AvrcpAbsoluteVolumeState* state,
    std::uint64_t generation,
    const AvrcpWindowsVolume& observed_windows_volume) {
    AvrcpAbsoluteVolumeDecision decision;
    if (state == nullptr || !IsCurrentSession(*state, generation)) {
        return decision;
    }

    state->windows_volume_known = true;
    state->windows_volume = ClampWindowsVolume(observed_windows_volume);
    const std::uint8_t projected =
        ProjectWindowsVolumeToXm5(state->windows_volume);

    if (state->windows_update_pending) {
        const bool is_expected_callback =
            projected == state->pending_xm5_volume;
        state->windows_update_pending = false;
        if (is_expected_callback) {
            state->last_xm5_volume_sent = projected;
            state->outbound_echo_pending = false;
            return decision;
        }
        // The endpoint did not accept the remote-requested value. Windows is
        // authoritative, so reassert it even if the same AVRCP value was sent
        // earlier in this generation.
        return SendAuthoritativeWindowsVolume(state);
    }

    if (state->support != AvrcpAbsoluteVolumeSupport::Supported) {
        return decision;
    }
    if (state->first_session_sync_complete &&
        state->last_xm5_volume_sent == projected) {
        return decision;
    }
    return SendAuthoritativeWindowsVolume(state);
}

AvrcpAbsoluteVolumeDecision ObserveXm5AbsoluteVolume(
    AvrcpAbsoluteVolumeState* state,
    std::uint64_t generation,
    std::uint32_t xm5_absolute_volume,
    AvrcpXm5VolumeEvent event) {
    AvrcpAbsoluteVolumeDecision decision;
    if (state == nullptr || !IsCurrentSession(*state, generation) ||
        state->support != AvrcpAbsoluteVolumeSupport::Supported ||
        xm5_absolute_volume > 127u) {
        return decision;
    }

    const std::uint8_t value =
        static_cast<std::uint8_t>(xm5_absolute_volume);
    if (event == AvrcpXm5VolumeEvent::CommandResponse) {
        if (state->outbound_echo_pending &&
            value == state->last_xm5_volume_sent) {
            state->outbound_echo_pending = false;
        }
        return decision;
    }
    if (state->inbound_volume_seen &&
        value == state->last_xm5_volume_received) {
        return decision;
    }

    state->inbound_volume_seen = true;
    state->last_xm5_volume_received = value;
    state->outbound_echo_pending = false;

    if (state->windows_volume_known &&
        ProjectWindowsVolumeToXm5(state->windows_volume) == value) {
        return decision;
    }

    decision.actions = AvrcpVolumeActionSetWindowsEndpoint;
    decision.windows_volume.percent =
        Xm5AbsoluteVolumeToWindowsPercent(value);
    decision.windows_volume.muted = value == 0u;
    state->windows_update_pending = true;
    state->pending_xm5_volume = value;
    return decision;
}

void EndAvrcpAbsoluteVolumeSession(AvrcpAbsoluteVolumeState* state,
                                   std::uint64_t generation) {
    if (state == nullptr || !IsCurrentSession(*state, generation)) {
        return;
    }
    state->session_active = false;
    state->support = AvrcpAbsoluteVolumeSupport::Unknown;
    state->outbound_echo_pending = false;
    state->windows_update_pending = false;
    state->inbound_volume_seen = false;
}

namespace {

bool IsCurrentAclGeneration(const AvrcpAbsoluteVolumeGateState& state,
                            std::uint64_t acl_generation) {
    return acl_generation != 0u && state.acl_generation_current &&
           state.acl_generation == acl_generation;
}

bool GateCanSynchronize(const AvrcpAbsoluteVolumeGateState& state) {
    return state.acl_generation_current && state.acl_generation != 0u &&
           state.owner_lease != 0u &&
           state.mode == AvrcpAbsoluteVolumeGateMode::Synchronize &&
           state.observed_support ==
               AvrcpAbsoluteVolumeSupport::Supported &&
           state.windows_volume_observed;
}

void ResetGateReducer(AvrcpAbsoluteVolumeGateState* state) {
    if (state == nullptr) return;
    state->reducer = AvrcpAbsoluteVolumeState{};
}

void InvalidateGateAuthorization(AvrcpAbsoluteVolumeGateState* state) {
    if (state == nullptr) return;
    if (state->authorization_epoch ==
        std::numeric_limits<std::uint64_t>::max()) {
        state->authorization_epoch = 1u;
    } else {
        ++state->authorization_epoch;
    }
    ResetGateReducer(state);
}

AvrcpAbsoluteVolumeGateDecision AuthorizeGateDecision(
    const AvrcpAbsoluteVolumeGateState& state,
    const AvrcpAbsoluteVolumeDecision& volume) {
    AvrcpAbsoluteVolumeGateDecision decision;
    if (!GateCanSynchronize(state) || volume.actions == 0u) {
        return decision;
    }
    decision.volume = volume;
    decision.acl_generation = state.acl_generation;
    decision.owner_lease = state.owner_lease;
    decision.authorization_epoch = state.authorization_epoch;
    return decision;
}

AvrcpAbsoluteVolumeGateDecision ActivateGateIfAuthorized(
    AvrcpAbsoluteVolumeGateState* state) {
    AvrcpAbsoluteVolumeGateDecision decision;
    if (state == nullptr || !GateCanSynchronize(*state) ||
        state->reducer.session_active) {
        return decision;
    }
    ResetGateReducer(state);
    return AuthorizeGateDecision(
        *state,
        BeginAvrcpAbsoluteVolumeSession(
            &state->reducer,
            state->acl_generation,
            AvrcpAbsoluteVolumeSupport::Supported,
            state->last_windows_volume));
}

}  // namespace

AvrcpAbsoluteVolumeGateDecision BeginAvrcpAbsoluteVolumeAclGeneration(
    AvrcpAbsoluteVolumeGateState* state,
    std::uint64_t acl_generation) {
    AvrcpAbsoluteVolumeGateDecision decision;
    if (state == nullptr ||
        !IsNewerGeneration(acl_generation, state->acl_generation)) {
        return decision;
    }
    const std::uint64_t previous_authorization_epoch =
        state->authorization_epoch;
    *state = AvrcpAbsoluteVolumeGateState{};
    state->authorization_epoch = previous_authorization_epoch;
    InvalidateGateAuthorization(state);
    state->acl_generation = acl_generation;
    state->acl_generation_current = true;
    return decision;
}

AvrcpAbsoluteVolumeGateDecision SetAvrcpAbsoluteVolumeGateMode(
    AvrcpAbsoluteVolumeGateState* state,
    std::uint64_t acl_generation,
    AvrcpAbsoluteVolumeGateMode mode) {
    AvrcpAbsoluteVolumeGateDecision decision;
    if (state == nullptr ||
        !IsCurrentAclGeneration(*state, acl_generation)) {
        return decision;
    }
    state->mode = mode;
    if (mode != AvrcpAbsoluteVolumeGateMode::Synchronize) {
        InvalidateGateAuthorization(state);
        return decision;
    }
    return ActivateGateIfAuthorized(state);
}

AvrcpAbsoluteVolumeGateDecision AcquireAvrcpAbsoluteVolumeOwnerLease(
    AvrcpAbsoluteVolumeGateState* state,
    std::uint64_t acl_generation,
    std::uint64_t owner_lease) {
    AvrcpAbsoluteVolumeGateDecision decision;
    if (state == nullptr || owner_lease == 0u ||
        !IsCurrentAclGeneration(*state, acl_generation) ||
        (state->owner_lease != 0u &&
         state->owner_lease != owner_lease)) {
        return decision;
    }
    state->owner_lease = owner_lease;
    return ActivateGateIfAuthorized(state);
}

AvrcpAbsoluteVolumeGateDecision RevokeAvrcpAbsoluteVolumeOwnerLease(
    AvrcpAbsoluteVolumeGateState* state,
    std::uint64_t acl_generation,
    std::uint64_t owner_lease) {
    AvrcpAbsoluteVolumeGateDecision decision;
    if (state == nullptr || owner_lease == 0u ||
        !IsCurrentAclGeneration(*state, acl_generation) ||
        state->owner_lease != owner_lease) {
        return decision;
    }
    state->owner_lease = 0u;
    InvalidateGateAuthorization(state);
    return decision;
}

AvrcpAbsoluteVolumeGateDecision ObserveAvrcpAbsoluteVolumeCapability(
    AvrcpAbsoluteVolumeGateState* state,
    std::uint64_t acl_generation,
    AvrcpAbsoluteVolumeSupport support) {
    AvrcpAbsoluteVolumeGateDecision decision;
    if (state == nullptr ||
        !IsCurrentAclGeneration(*state, acl_generation)) {
        return decision;
    }
    state->observed_support = support;
    if (support != AvrcpAbsoluteVolumeSupport::Supported) {
        InvalidateGateAuthorization(state);
        return decision;
    }
    return ActivateGateIfAuthorized(state);
}

AvrcpAbsoluteVolumeGateDecision ObserveWindowsEndpointVolumeThroughGate(
    AvrcpAbsoluteVolumeGateState* state,
    std::uint64_t acl_generation,
    const AvrcpWindowsVolume& observed_windows_volume) {
    AvrcpAbsoluteVolumeGateDecision decision;
    if (state == nullptr ||
        !IsCurrentAclGeneration(*state, acl_generation)) {
        return decision;
    }
    state->windows_volume_observed = true;
    state->last_windows_volume = ClampWindowsVolume(observed_windows_volume);
    if (!GateCanSynchronize(*state)) {
        return decision;
    }
    if (!state->reducer.session_active) {
        return ActivateGateIfAuthorized(state);
    }
    return AuthorizeGateDecision(
        *state,
        ObserveWindowsEndpointVolume(&state->reducer,
                                     acl_generation,
                                     state->last_windows_volume));
}

AvrcpAbsoluteVolumeGateDecision ObserveXm5AbsoluteVolumeThroughGate(
    AvrcpAbsoluteVolumeGateState* state,
    std::uint64_t acl_generation,
    std::uint32_t xm5_absolute_volume,
    AvrcpXm5VolumeEvent event) {
    AvrcpAbsoluteVolumeGateDecision decision;
    if (state == nullptr || xm5_absolute_volume > 127u ||
        !IsCurrentAclGeneration(*state, acl_generation)) {
        return decision;
    }
    state->xm5_volume_observed = true;
    state->last_xm5_volume =
        static_cast<std::uint8_t>(xm5_absolute_volume);
    state->last_xm5_event = event;
    if (!GateCanSynchronize(*state) ||
        !state->reducer.session_active) {
        return decision;
    }
    return AuthorizeGateDecision(
        *state,
        ObserveXm5AbsoluteVolume(&state->reducer,
                                 acl_generation,
                                 xm5_absolute_volume,
                                 event));
}

AvrcpAbsoluteVolumeGateDecision EndAvrcpAbsoluteVolumeAclGeneration(
    AvrcpAbsoluteVolumeGateState* state,
    std::uint64_t acl_generation) {
    AvrcpAbsoluteVolumeGateDecision decision;
    if (state == nullptr ||
        !IsCurrentAclGeneration(*state, acl_generation)) {
        return decision;
    }
    state->acl_generation_current = false;
    state->owner_lease = 0u;
    state->mode = AvrcpAbsoluteVolumeGateMode::ObserveOnly;
    state->observed_support = AvrcpAbsoluteVolumeSupport::Unknown;
    InvalidateGateAuthorization(state);
    return decision;
}

bool IsAvrcpAbsoluteVolumeGateDecisionCurrent(
    const AvrcpAbsoluteVolumeGateState& state,
    const AvrcpAbsoluteVolumeGateDecision& decision) {
    return decision.volume.actions != 0u && GateCanSynchronize(state) &&
           state.reducer.session_active &&
           decision.acl_generation == state.acl_generation &&
           decision.owner_lease == state.owner_lease &&
           decision.authorization_epoch == state.authorization_epoch;
}

bool HasAvrcpAbsoluteVolumeGateAction(
    const AvrcpAbsoluteVolumeGateDecision& decision,
    AvrcpAbsoluteVolumeAction action) {
    return HasAvrcpAbsoluteVolumeAction(decision.volume, action);
}

}  // namespace native_ldac::agent
