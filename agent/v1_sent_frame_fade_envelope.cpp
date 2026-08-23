// SPDX-License-Identifier: Apache-2.0
#include "v1_sent_frame_fade_envelope.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace native_ldac::agent {
namespace {

std::uint64_t NextRevision(std::uint64_t revision) {
    return revision == std::numeric_limits<std::uint64_t>::max()
               ? 1u
               : revision + 1u;
}

bool BuffersOverlap(const float* source,
                    const float* destination,
                    std::size_t sample_count) {
    const std::uintptr_t source_begin =
        reinterpret_cast<std::uintptr_t>(source);
    const std::uintptr_t destination_begin =
        reinterpret_cast<std::uintptr_t>(destination);
    const std::size_t byte_count = sample_count * sizeof(float);
    const std::uintptr_t source_end = source_begin + byte_count;
    const std::uintptr_t destination_end = destination_begin + byte_count;
    if (source_end < source_begin || destination_end < destination_begin) {
        return true;
    }
    return source_begin < destination_end && destination_begin < source_end;
}

}  // namespace

bool BeginV1SentFrameFadeSession(V1SentFrameFadeState* state,
                                 std::uint64_t session_generation) {
    if (state == nullptr || session_generation == 0u) {
        return false;
    }
    state->session_generation = session_generation;
    state->committed_sent_frames = 0u;
    state->revision = NextRevision(state->revision);
    state->active = true;
    return true;
}

void EndV1SentFrameFadeSession(V1SentFrameFadeState* state) {
    if (state != nullptr) {
        state->revision = NextRevision(state->revision);
        state->active = false;
    }
}

bool PrepareV1SentFrameFadeBlock(
    const float* source_interleaved_stereo,
    float* destination_interleaved_stereo,
    std::size_t frame_count,
    unsigned sample_rate_hz,
    float fade_duration_ms,
    const V1SentFrameFadeState* state,
    V1SentFrameFadeBlock* block,
    V1SentFrameFadeTelemetry* telemetry) {
    if (source_interleaved_stereo == nullptr ||
        destination_interleaved_stereo == nullptr || state == nullptr ||
        block == nullptr || telemetry == nullptr || !state->active ||
        state->session_generation == 0u || frame_count == 0u ||
        frame_count > kV1SentFrameFadeMaximumFrames ||
        sample_rate_hz == 0u || !std::isfinite(fade_duration_ms) ||
        fade_duration_ms <= 0.0f ||
        fade_duration_ms > kV1SentFrameFadeMaximumDurationMs ||
        state->committed_sent_frames >
            std::numeric_limits<std::uint64_t>::max() - frame_count ||
        BuffersOverlap(source_interleaved_stereo,
                       destination_interleaved_stereo,
                       frame_count * 2u)) {
        return false;
    }
    const double duration_frames_value = std::ceil(
        static_cast<double>(sample_rate_hz) *
        static_cast<double>(fade_duration_ms) / 1000.0);
    if (!std::isfinite(duration_frames_value) ||
        duration_frames_value < 1.0 ||
        duration_frames_value >
            static_cast<double>(
                std::numeric_limits<std::uint64_t>::max())) {
        return false;
    }
    const std::uint64_t duration_frames =
        static_cast<std::uint64_t>(duration_frames_value);

    V1SentFrameFadeBlock next_block{};
    next_block.session_generation = state->session_generation;
    next_block.starting_sent_frame = state->committed_sent_frames;
    next_block.revision = state->revision;
    next_block.frame_count = static_cast<std::uint32_t>(frame_count);
    V1SentFrameFadeTelemetry next{};
    next.starting_sent_frame = state->committed_sent_frames;
    next.fade_duration_frames = duration_frames;

    for (std::size_t frame = 0u; frame < frame_count; ++frame) {
        const std::uint64_t absolute_frame =
            state->committed_sent_frames + frame;
        const float gain = absolute_frame >= duration_frames
                               ? 1.0f
                               : static_cast<float>(
                                     static_cast<double>(absolute_frame + 1u) /
                                     static_cast<double>(duration_frames));
        next.minimum_gain = std::min(next.minimum_gain, gain);
        next.last_gain = gain;
        if (gain < 1.0f) {
            ++next.frames_below_unity;
        }
        for (std::size_t channel = 0u; channel < 2u; ++channel) {
            const std::size_t index = frame * 2u + channel;
            const float source = source_interleaved_stereo[index];
            const float sanitized = std::isfinite(source) ? source : 0.0f;
            if (!std::isfinite(source)) {
                ++next.sanitized_sample_count;
            }
            destination_interleaved_stereo[index] = sanitized * gain;
        }
    }
    *block = next_block;
    *telemetry = next;
    return true;
}

bool CommitV1SentFrameFadeBlock(
    V1SentFrameFadeState* state,
    const V1SentFrameFadeBlock& block,
    std::size_t actual_sent_frames) {
    if (state == nullptr || !state->active ||
        block.session_generation != state->session_generation ||
        block.revision != state->revision ||
        block.starting_sent_frame != state->committed_sent_frames ||
        block.frame_count == 0u ||
        block.frame_count > kV1SentFrameFadeMaximumFrames ||
        actual_sent_frames != block.frame_count ||
        state->committed_sent_frames >
            std::numeric_limits<std::uint64_t>::max() -
                actual_sent_frames) {
        return false;
    }
    state->committed_sent_frames += actual_sent_frames;
    return true;
}

}  // namespace native_ldac::agent
