// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "v1_hfp_capture_monitor.h"
#include "v1_hfp_switch_state.h"

namespace native_ldac::agent {

struct V1HfpShadowInput {
    V1HfpCaptureSnapshot capture;
    std::uint64_t acl_generation = 0u;
    std::uint64_t now_ms = 0u;
    bool monitor_ready = false;
    bool physical_connected = false;
    bool ldac_path_active = false;
    bool render_demand = false;
};

struct V1HfpShadowDecision {
    V1HfpSwitchDecision switch_decision;
    bool snapshot_changed = false;
    bool monitor_ready = false;
    bool endpoint_matched = false;
    bool capture_active = false;
    std::uint64_t capture_sequence = 0u;
};

// Pure coordinator between the read-only Windows monitor and the transport-
// free HFP switch reducer. Monitor failures and endpoint identity uncertainty
// are converted to capture inactive before any switch decision is evaluated.
class V1HfpShadowState {
public:
    V1HfpShadowDecision Step(const V1HfpShadowInput& input);

private:
    V1HfpSwitchState switch_state_;
    std::uint64_t capture_sequence_ = 0u;
};

}  // namespace native_ldac::agent
