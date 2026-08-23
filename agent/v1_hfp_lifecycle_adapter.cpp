// SPDX-License-Identifier: Apache-2.0
#include "v1_hfp_lifecycle_adapter.h"

namespace native_ldac::agent {

V1HfpLifecyclePlan PlanV1HfpLifecycle(
    const V1HfpSwitchDecision& decision,
    std::uint64_t current_acl_generation,
    bool transport_switch_enabled) {
    V1HfpLifecyclePlan plan;
    plan.enter_hfp_output_requested = HasV1HfpSwitchAction(
        decision, V1HfpActionEnterHfpOutput);
    plan.exit_hfp_output_requested = HasV1HfpSwitchAction(
        decision, V1HfpActionExitHfpOutput);
    if (decision.stale || decision.acl_generation == 0u ||
        decision.acl_generation != current_acl_generation) {
        plan.stale = true;
        return plan;
    }
    const bool stop = HasV1HfpSwitchAction(
        decision, V1HfpActionStopLdac);
    const bool resume = HasV1HfpSwitchAction(
        decision, V1HfpActionResumeLdac);
    if (stop && resume) {
        plan.invalid = true;
        return plan;
    }
    if (stop) {
        plan.proposed_command = V1HfpLifecycleCommand::SuspendLdac;
    } else if (resume) {
        plan.proposed_command = V1HfpLifecycleCommand::ResumeLdac;
    }
    if (!transport_switch_enabled) return plan;
    plan.shadow_only = false;
    plan.command = plan.proposed_command;
    return plan;
}

V1LifecycleEvent V1HfpLifecycleEventForCommand(
    V1HfpLifecycleCommand command) {
    switch (command) {
        case V1HfpLifecycleCommand::SuspendLdac:
            return V1LifecycleEvent::HfpSuspendLdac;
        case V1HfpLifecycleCommand::ResumeLdac:
            return V1LifecycleEvent::HfpResumeLdac;
        case V1HfpLifecycleCommand::None:
        default:
            return V1LifecycleEvent::HfpResumeLdac;
    }
}

}  // namespace native_ldac::agent
