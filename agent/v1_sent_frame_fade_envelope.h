// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

namespace native_ldac::agent {

constexpr std::size_t kV1SentFrameFadeMaximumFrames = 128u;
constexpr float kV1SentFrameFadeMaximumDurationMs = 1000.0f;

struct V1SentFrameFadeState {
    std::uint64_t session_generation = 0u;
    std::uint64_t committed_sent_frames = 0u;
    std::uint64_t revision = 0u;
    bool active = false;
};

struct V1SentFrameFadeBlock {
    std::uint64_t session_generation = 0u;
    std::uint64_t starting_sent_frame = 0u;
    std::uint64_t revision = 0u;
    std::uint32_t frame_count = 0u;
};

struct V1SentFrameFadeTelemetry {
    std::uint64_t starting_sent_frame = 0u;
    std::uint64_t fade_duration_frames = 0u;
    float minimum_gain = 1.0f;
    float last_gain = 1.0f;
    std::uint64_t frames_below_unity = 0u;
    std::uint64_t sanitized_sample_count = 0u;
};

bool BeginV1SentFrameFadeSession(V1SentFrameFadeState* state,
                                 std::uint64_t session_generation);

void EndV1SentFrameFadeSession(V1SentFrameFadeState* state);

bool PrepareV1SentFrameFadeBlock(
    const float* source_interleaved_stereo,
    float* destination_interleaved_stereo,
    std::size_t frame_count,
    unsigned sample_rate_hz,
    float fade_duration_ms,
    const V1SentFrameFadeState* state,
    V1SentFrameFadeBlock* block,
    V1SentFrameFadeTelemetry* telemetry);

bool CommitV1SentFrameFadeBlock(
    V1SentFrameFadeState* state,
    const V1SentFrameFadeBlock& block,
    std::size_t actual_sent_frames);

}  // namespace native_ldac::agent
