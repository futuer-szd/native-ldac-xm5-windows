// SPDX-License-Identifier: Apache-2.0
#include "v1_hfp_switch_state.h"

namespace native_ldac::agent {
namespace {

bool IsNewerGeneration(std::uint64_t candidate, std::uint64_t current) {
    if (candidate == 0u) return false;
    if (current == 0u) return true;
    constexpr std::uint64_t half_range = std::uint64_t{1u} << 63u;
    const std::uint64_t distance = candidate - current;
    return distance != 0u && distance < half_range;
}

}  // namespace

V1HfpSwitchDecision V1HfpSwitchState::Snapshot(std::uint32_t actions,
                                                bool stale) const {
    V1HfpSwitchDecision decision;
    decision.actions = actions;
    decision.phase = phase_;
    decision.acl_generation = acl_generation_;
    decision.release_deadline_ms = release_deadline_ms_;
    decision.stale = stale;
    return decision;
}

void V1HfpSwitchState::ResetDisconnected() {
    phase_ = V1HfpSwitchPhase::Disconnected;
    release_deadline_ms_ = 0u;
    hfp_output_active_ = false;
}

V1HfpSwitchDecision V1HfpSwitchState::Step(
    const V1HfpSwitchInput& input) {
    if (input.acl_generation == 0u) {
        return Snapshot(V1HfpActionNone, true);
    }
    if (acl_generation_ != 0u &&
        input.acl_generation != acl_generation_ &&
        !IsNewerGeneration(input.acl_generation, acl_generation_)) {
        return Snapshot(V1HfpActionNone, true);
    }

    std::uint32_t actions = V1HfpActionNone;
    if (input.acl_generation != acl_generation_) {
        if (hfp_output_active_) actions |= V1HfpActionExitHfpOutput;
        acl_generation_ = input.acl_generation;
        ResetDisconnected();
        if (input.physical_connected) {
            phase_ = V1HfpSwitchPhase::LdacAvailable;
        }
    }

    if (!input.physical_connected) {
        if (hfp_output_active_) actions |= V1HfpActionExitHfpOutput;
        ResetDisconnected();
        return Snapshot(actions);
    }
    if (phase_ == V1HfpSwitchPhase::Disconnected) {
        phase_ = V1HfpSwitchPhase::LdacAvailable;
    }

    if (input.capture_active) {
        release_deadline_ms_ = 0u;
        if (input.ldac_path_active) {
            if (phase_ != V1HfpSwitchPhase::WaitingForLdacStop) {
                actions |= V1HfpActionStopLdac;
            }
            phase_ = V1HfpSwitchPhase::WaitingForLdacStop;
            return Snapshot(actions);
        }
        if (!hfp_output_active_) {
            actions |= V1HfpActionEnterHfpOutput;
            hfp_output_active_ = true;
        }
        phase_ = V1HfpSwitchPhase::HfpActive;
        return Snapshot(actions);
    }

    if (phase_ == V1HfpSwitchPhase::WaitingForLdacStop) {
        if (input.ldac_path_active) return Snapshot(actions);
        phase_ = V1HfpSwitchPhase::ReleaseDelay;
        release_deadline_ms_ = input.now_ms + kV1HfpReleaseStabilityMs;
        return Snapshot(actions);
    }
    if (phase_ == V1HfpSwitchPhase::HfpActive) {
        phase_ = V1HfpSwitchPhase::ReleaseDelay;
        release_deadline_ms_ = input.now_ms + kV1HfpReleaseStabilityMs;
        return Snapshot(actions);
    }
    if (phase_ == V1HfpSwitchPhase::ReleaseDelay &&
        input.now_ms >= release_deadline_ms_) {
        if (hfp_output_active_) {
            actions |= V1HfpActionExitHfpOutput;
            hfp_output_active_ = false;
        }
        if (input.render_demand && !input.ldac_path_active) {
            actions |= V1HfpActionResumeLdac;
        }
        phase_ = V1HfpSwitchPhase::LdacAvailable;
        release_deadline_ms_ = 0u;
    }
    return Snapshot(actions);
}

bool HasV1HfpSwitchAction(const V1HfpSwitchDecision& decision,
                          V1HfpSwitchAction action) {
    return (decision.actions & static_cast<std::uint32_t>(action)) != 0u;
}

}  // namespace native_ldac::agent
