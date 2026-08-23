// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

namespace native_ldac::agent {

constexpr std::size_t kV1LinkedStereoLimiterMaximumFrames = 128u;
constexpr float kV1LinkedStereoLimiterHardMaximumCeiling = 1.0f;

struct V1LinkedStereoBlockLimiterState {
    float gain = 1.0f;
    // Linear per-frame recovery increment fixed by the most recent attack.
    float release_step = 0.0f;
};

struct V1LinkedStereoBlockLimiterTelemetry {
    float block_peak = 0.0f;
    float output_peak = 0.0f;
    float minimum_gain = 1.0f;
    float last_gain = 1.0f;
    float maximum_gain_step = 0.0f;
    std::uint32_t attack_count = 0u;
    std::uint64_t reduced_frame_count = 0u;
    std::uint64_t reduced_sample_count = 0u;
    std::uint64_t fallback_clamp_count = 0u;
    std::uint64_t sanitized_sample_count = 0u;
    std::uint64_t pre_over_ceiling_frame_count = 0u;
    std::uint64_t pre_over_ceiling_sample_count = 0u;
};

void ResetV1LinkedStereoBlockLimiter(
    V1LinkedStereoBlockLimiterState* state);

bool ProcessV1LinkedStereoBlock(
    float* interleaved_stereo,
    std::size_t frame_count,
    float ceiling,
    unsigned sample_rate_hz,
    // Unobstructed recovery time from the most recent attacked gain to unity.
    float release_ms,
    V1LinkedStereoBlockLimiterState* state,
    V1LinkedStereoBlockLimiterTelemetry* telemetry);

}  // namespace native_ldac::agent
