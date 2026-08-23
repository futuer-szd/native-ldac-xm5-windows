// SPDX-License-Identifier: Apache-2.0
#include "v1_linked_stereo_block_limiter.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace native_ldac::agent {
namespace {

bool IsUnitScalar(float value) {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

float SafeGainForPeak(float peak, float ceiling) {
    if (peak <= ceiling) {
        return 1.0f;
    }
    float gain = ceiling / peak;
    while (peak * gain > ceiling) {
        gain = std::nextafter(gain, 0.0f);
    }
    return gain;
}

float Sanitized(float sample) {
    return std::isfinite(sample) ? sample : 0.0f;
}

bool ComputeReleaseStep(float attacked_gain,
                        unsigned sample_rate_hz,
                        float release_ms,
                        float* release_step) {
    const double release_frames =
        static_cast<double>(sample_rate_hz) *
        static_cast<double>(release_ms) / 1000.0;
    const double step =
        (1.0 - static_cast<double>(attacked_gain)) / release_frames;
    const float stored = static_cast<float>(step);
    if (!std::isfinite(stored) || stored <= 0.0f || stored > 1.0f) {
        return false;
    }
    *release_step = stored;
    return true;
}

}  // namespace

void ResetV1LinkedStereoBlockLimiter(
    V1LinkedStereoBlockLimiterState* state) {
    if (state != nullptr) {
        *state = {};
    }
}

bool ProcessV1LinkedStereoBlock(
    float* interleaved_stereo,
    std::size_t frame_count,
    float ceiling,
    unsigned sample_rate_hz,
    float release_ms,
    V1LinkedStereoBlockLimiterState* state,
    V1LinkedStereoBlockLimiterTelemetry* telemetry) {
    if (state == nullptr || telemetry == nullptr ||
        (frame_count != 0u && interleaved_stereo == nullptr) ||
        frame_count > kV1LinkedStereoLimiterMaximumFrames ||
        !std::isfinite(ceiling) || ceiling <= 0.0f ||
        ceiling > kV1LinkedStereoLimiterHardMaximumCeiling ||
        sample_rate_hz == 0u || !std::isfinite(release_ms) ||
        release_ms <= 0.0f || !IsUnitScalar(state->gain) ||
        !IsUnitScalar(state->release_step) ||
        (state->gain < 1.0f && state->release_step <= 0.0f) ||
        (state->gain == 1.0f && state->release_step != 0.0f)) {
        return false;
    }

    std::array<float, kV1LinkedStereoLimiterMaximumFrames> suffix_peak{};
    V1LinkedStereoBlockLimiterTelemetry next{};
    for (std::size_t frame = 0u; frame < frame_count; ++frame) {
        const std::size_t left_index = frame * 2u;
        const std::size_t right_index = left_index + 1u;
        const float left = Sanitized(interleaved_stereo[left_index]);
        const float right = Sanitized(interleaved_stereo[right_index]);
        if (!std::isfinite(interleaved_stereo[left_index])) {
            ++next.sanitized_sample_count;
        }
        if (!std::isfinite(interleaved_stereo[right_index])) {
            ++next.sanitized_sample_count;
        }
        const float left_peak = std::fabs(left);
        const float right_peak = std::fabs(right);
        suffix_peak[frame] = std::max(left_peak, right_peak);
        next.block_peak = std::max(next.block_peak, suffix_peak[frame]);
        bool frame_over = false;
        if (left_peak > ceiling) {
            ++next.pre_over_ceiling_sample_count;
            frame_over = true;
        }
        if (right_peak > ceiling) {
            ++next.pre_over_ceiling_sample_count;
            frame_over = true;
        }
        if (frame_over) {
            ++next.pre_over_ceiling_frame_count;
        }
    }
    float future_peak = 0.0f;
    for (std::size_t frame = frame_count; frame != 0u; --frame) {
        const std::size_t index = frame - 1u;
        future_peak = std::max(future_peak, suffix_peak[index]);
        suffix_peak[index] = future_peak;
    }

    float gain = state->gain;
    float release_step = state->release_step;
    const float block_limit = SafeGainForPeak(next.block_peak, ceiling);
    if (block_limit < gain) {
        next.maximum_gain_step = gain - block_limit;
        gain = block_limit;
        if (!ComputeReleaseStep(
                gain, sample_rate_hz, release_ms, &release_step)) {
            return false;
        }
        ++next.attack_count;
    }
    next.minimum_gain = frame_count == 0u ? gain : 1.0f;
    next.last_gain = gain;

    for (std::size_t frame = 0u; frame < frame_count; ++frame) {
        const std::size_t left_index = frame * 2u;
        const std::size_t right_index = left_index + 1u;
        const float frame_peak = std::max(
            std::fabs(Sanitized(interleaved_stereo[left_index])),
            std::fabs(Sanitized(interleaved_stereo[right_index])));
        const float frame_limit = SafeGainForPeak(frame_peak, ceiling);
        if (frame_limit < gain) {
            next.maximum_gain_step = std::max(
                next.maximum_gain_step, gain - frame_limit);
            gain = frame_limit;
            if (!ComputeReleaseStep(
                    gain, sample_rate_hz, release_ms, &release_step)) {
                return false;
            }
            ++next.attack_count;
        }

        next.minimum_gain = std::min(next.minimum_gain, gain);
        next.last_gain = gain;
        bool frame_reduced = false;
        const std::size_t indices[] = {left_index, right_index};
        for (const std::size_t index : indices) {
            const float input = Sanitized(interleaved_stereo[index]);
            const float scaled = input * gain;
            const float output = std::clamp(scaled, -ceiling, ceiling);
            if (output != scaled) {
                ++next.fallback_clamp_count;
            }
            interleaved_stereo[index] = output;
            next.output_peak = std::max(next.output_peak, std::fabs(output));
            if (input != 0.0f && std::fabs(output) < std::fabs(input)) {
                ++next.reduced_sample_count;
                frame_reduced = true;
            }
        }
        if (frame_reduced) {
            ++next.reduced_frame_count;
        }

        float next_gain = gain;
        if (gain < 1.0f) {
            const float next_suffix_peak =
                frame + 1u < frame_count ? suffix_peak[frame + 1u] : 0.0f;
            const float suffix_limit =
                SafeGainForPeak(next_suffix_peak, ceiling);
            float released = gain + release_step;
            if (released >= 1.0f - release_step) {
                released = 1.0f;
            }
            next_gain = std::min(
                suffix_limit, std::min(1.0f, released));
            next_gain = std::max(gain, next_gain);
        }
        next.maximum_gain_step = std::max(
            next.maximum_gain_step, std::fabs(next_gain - gain));
        gain = next_gain;
        if (gain == 1.0f) {
            release_step = 0.0f;
        }
    }

    state->gain = gain;
    state->release_step = release_step;
    *telemetry = next;
    return true;
}

}  // namespace native_ldac::agent
