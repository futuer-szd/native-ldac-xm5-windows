// SPDX-License-Identifier: Apache-2.0
#include "v1_hfp_shadow_state.h"

namespace native_ldac::agent {

V1HfpShadowDecision V1HfpShadowState::Step(
    const V1HfpShadowInput& input) {
    V1HfpShadowDecision result;
    result.snapshot_changed =
        input.capture.sequence != capture_sequence_;
    capture_sequence_ = input.capture.sequence;
    result.capture_sequence = capture_sequence_;
    result.monitor_ready = input.monitor_ready;
    result.endpoint_matched = input.monitor_ready &&
        input.capture.endpoint_matched;
    result.capture_active = result.endpoint_matched &&
        input.capture.capture_active;

    V1HfpSwitchInput switch_input;
    switch_input.acl_generation = input.acl_generation;
    switch_input.now_ms = input.now_ms;
    switch_input.physical_connected = input.physical_connected;
    switch_input.capture_active = result.capture_active;
    switch_input.ldac_path_active = input.ldac_path_active;
    switch_input.render_demand = input.render_demand;
    result.switch_decision = switch_state_.Step(switch_input);
    return result;
}

}  // namespace native_ldac::agent
