// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace native_ldac::agent {

constexpr std::uint64_t kV1HfpReleaseStabilityMs = 2000u;

enum class V1HfpSwitchPhase : std::uint8_t {
    Disconnected = 0u,
    LdacAvailable,
    WaitingForLdacStop,
    HfpActive,
    ReleaseDelay,
};

struct V1HfpSwitchInput {
    std::uint64_t acl_generation = 0u;
    std::uint64_t now_ms = 0u;
    bool physical_connected = false;
    bool capture_active = false;
    // True while any LDAC path resource is alive, including an engine that is
    // Starting/Ready before AVDTP reaches Opening/Streaming.
    bool ldac_path_active = false;
    bool render_demand = false;
};

enum V1HfpSwitchAction : std::uint32_t {
    V1HfpActionNone = 0u,
    V1HfpActionStopLdac = 1u << 0u,
    V1HfpActionEnterHfpOutput = 1u << 1u,
    V1HfpActionExitHfpOutput = 1u << 2u,
    V1HfpActionResumeLdac = 1u << 3u,
};

struct V1HfpSwitchDecision {
    std::uint32_t actions = V1HfpActionNone;
    V1HfpSwitchPhase phase = V1HfpSwitchPhase::Disconnected;
    std::uint64_t acl_generation = 0u;
    std::uint64_t release_deadline_ms = 0u;
    bool stale = false;
};

class V1HfpSwitchState {
public:
    V1HfpSwitchDecision Step(const V1HfpSwitchInput& input);

    V1HfpSwitchPhase phase() const { return phase_; }
    std::uint64_t acl_generation() const { return acl_generation_; }
    std::uint64_t release_deadline_ms() const {
        return release_deadline_ms_;
    }
    bool hfp_output_active() const { return hfp_output_active_; }

private:
    V1HfpSwitchDecision Snapshot(std::uint32_t actions = V1HfpActionNone,
                                 bool stale = false) const;
    void ResetDisconnected();

    V1HfpSwitchPhase phase_ = V1HfpSwitchPhase::Disconnected;
    std::uint64_t acl_generation_ = 0u;
    std::uint64_t release_deadline_ms_ = 0u;
    bool hfp_output_active_ = false;
};

bool HasV1HfpSwitchAction(const V1HfpSwitchDecision& decision,
                          V1HfpSwitchAction action);

}  // namespace native_ldac::agent
