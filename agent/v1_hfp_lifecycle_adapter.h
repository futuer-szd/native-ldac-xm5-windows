// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "v1_hfp_switch_state.h"
#include "v1_lifecycle.h"

namespace native_ldac::agent {

enum class V1HfpLifecycleCommand : std::uint8_t {
    None = 0u,
    SuspendLdac,
    ResumeLdac,
};

struct V1HfpLifecyclePlan {
    V1HfpLifecycleCommand proposed_command = V1HfpLifecycleCommand::None;
    V1HfpLifecycleCommand command = V1HfpLifecycleCommand::None;
    bool shadow_only = true;
    bool stale = false;
    bool invalid = false;
    bool enter_hfp_output_requested = false;
    bool exit_hfp_output_requested = false;
};

// Maps generation-safe HFP switch decisions to the existing lifecycle. The
// transport execution path is opt-in; HFP output requests stay explicit until
// the Native endpoint bridge has its own independently verified executor.
V1HfpLifecyclePlan PlanV1HfpLifecycle(
    const V1HfpSwitchDecision& decision,
    std::uint64_t current_acl_generation,
    bool transport_switch_enabled);

V1LifecycleEvent V1HfpLifecycleEventForCommand(
    V1HfpLifecycleCommand command);

}  // namespace native_ldac::agent
