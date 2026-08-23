// SPDX-License-Identifier: Apache-2.0
#include "../v1_linked_stereo_block_limiter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expression); \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

bool Near(float left, float right, float tolerance = 0.000001f) {
    return std::fabs(left - right) <= tolerance;
}

void TestBlockPreattackAndLatePeak() {
    constexpr std::size_t frames =
        native_ldac::agent::kV1LinkedStereoLimiterMaximumFrames;
    std::vector<float> samples(frames * 2u, 0.1f);
    samples[samples.size() - 2u] = 1.0f;
    samples[samples.size() - 1u] = -0.5f;
    native_ldac::agent::V1LinkedStereoBlockLimiterState state{};
    native_ldac::agent::V1LinkedStereoBlockLimiterTelemetry telemetry{};
    CHECK(native_ldac::agent::ProcessV1LinkedStereoBlock(
        samples.data(), frames, 0.25f, 48000u, 50.0f,
        &state, &telemetry));
    CHECK(Near(samples[0], 0.025f));
    CHECK(Near(samples[1], 0.025f));
    CHECK(Near(samples[samples.size() - 2u], 0.25f));
    CHECK(Near(samples[samples.size() - 1u], -0.125f));
    CHECK(Near(telemetry.block_peak, 1.0f));
    CHECK(Near(telemetry.output_peak, 0.25f));
    CHECK(Near(telemetry.minimum_gain, 0.25f));
    CHECK(Near(telemetry.last_gain, 0.25f));
    CHECK(telemetry.attack_count == 1u);
    CHECK(Near(telemetry.maximum_gain_step, 0.75f));
    CHECK(telemetry.reduced_frame_count == frames);
    CHECK(telemetry.reduced_sample_count == frames * 2u);
    CHECK(telemetry.pre_over_ceiling_frame_count == 1u);
    CHECK(telemetry.pre_over_ceiling_sample_count == 2u);
    CHECK(telemetry.fallback_clamp_count == 0u);
    CHECK(state.gain > 0.25f);
    CHECK(Near(state.release_step, 0.75f / 2400.0f));
}

void TestFirstPeakAndLinkedStereoRatios() {
    float samples[] = {
        1.0f, -0.5f,
        -1.0f, 0.25f,
        1.0f, -0.25f,
    };
    native_ldac::agent::V1LinkedStereoBlockLimiterState state{};
    native_ldac::agent::V1LinkedStereoBlockLimiterTelemetry telemetry{};
    CHECK(native_ldac::agent::ProcessV1LinkedStereoBlock(
        samples, 3u, 0.25f, 48000u, 50.0f, &state, &telemetry));
    CHECK(Near(samples[0], 0.25f));
    CHECK(Near(samples[1], -0.125f));
    CHECK(Near(samples[2], -0.25f));
    CHECK(Near(samples[3], 0.0625f));
    CHECK(Near(samples[0] / samples[1], -2.0f));
    CHECK(Near(samples[2] / samples[3], -4.0f));
    CHECK(telemetry.attack_count == 1u);
    CHECK(telemetry.fallback_clamp_count == 0u);
}

void TestExactCeilingAndEpsilon() {
    native_ldac::agent::V1LinkedStereoBlockLimiterState exact_state{};
    native_ldac::agent::V1LinkedStereoBlockLimiterTelemetry telemetry{};
    float exact[] = {0.25f, -0.25f};
    CHECK(native_ldac::agent::ProcessV1LinkedStereoBlock(
        exact, 1u, 0.25f, 48000u, 50.0f,
        &exact_state, &telemetry));
    CHECK(exact[0] == 0.25f && exact[1] == -0.25f);
    CHECK(telemetry.attack_count == 0u);
    CHECK(telemetry.pre_over_ceiling_sample_count == 0u);
    CHECK(telemetry.reduced_sample_count == 0u);

    native_ldac::agent::V1LinkedStereoBlockLimiterState epsilon_state{};
    const float epsilon = std::nextafter(
        0.25f, std::numeric_limits<float>::infinity());
    float over[] = {epsilon, -epsilon};
    CHECK(native_ldac::agent::ProcessV1LinkedStereoBlock(
        over, 1u, 0.25f, 48000u, 50.0f,
        &epsilon_state, &telemetry));
    CHECK(std::fabs(over[0]) <= 0.25f);
    CHECK(std::fabs(over[1]) <= 0.25f);
    CHECK(telemetry.attack_count == 1u);
    CHECK(telemetry.pre_over_ceiling_frame_count == 1u);
    CHECK(telemetry.pre_over_ceiling_sample_count == 2u);
    CHECK(telemetry.fallback_clamp_count == 0u);
}

void TestSustainedHotAlternatingAndAntiphase() {
    constexpr std::size_t frames =
        native_ldac::agent::kV1LinkedStereoLimiterMaximumFrames;
    std::vector<float> samples(frames * 2u);
    for (std::size_t frame = 0u; frame < frames; ++frame) {
        samples[frame * 2u] = (frame & 1u) == 0u ? 1.0f : -1.0f;
        samples[frame * 2u + 1u] = -samples[frame * 2u];
    }
    native_ldac::agent::V1LinkedStereoBlockLimiterState state{};
    native_ldac::agent::V1LinkedStereoBlockLimiterTelemetry telemetry{};
    CHECK(native_ldac::agent::ProcessV1LinkedStereoBlock(
        samples.data(), frames, 0.25f, 96000u, 50.0f,
        &state, &telemetry));
    for (std::size_t frame = 0u; frame < frames; ++frame) {
        CHECK(Near(samples[frame * 2u], -samples[frame * 2u + 1u]));
        CHECK(Near(std::fabs(samples[frame * 2u]), 0.25f));
    }
    CHECK(Near(telemetry.minimum_gain, 0.25f));
    CHECK(Near(telemetry.last_gain, 0.25f));
    CHECK(telemetry.reduced_frame_count == frames);
    CHECK(telemetry.reduced_sample_count == frames * 2u);
    CHECK(telemetry.fallback_clamp_count == 0u);
}

void ProcessSilentFrames(
    std::size_t frames,
    unsigned sample_rate_hz,
    float release_ms,
    native_ldac::agent::V1LinkedStereoBlockLimiterState* state) {
    while (frames != 0u) {
        const std::size_t block = std::min(
            frames,
            native_ldac::agent::kV1LinkedStereoLimiterMaximumFrames);
        std::vector<float> silence(block * 2u, 0.0f);
        native_ldac::agent::V1LinkedStereoBlockLimiterTelemetry telemetry{};
        CHECK(native_ldac::agent::ProcessV1LinkedStereoBlock(
            silence.data(), block, 0.25f, sample_rate_hz, release_ms,
            state, &telemetry));
        CHECK(telemetry.fallback_clamp_count == 0u);
        frames -= block;
    }
}

void TestFiftyMillisecondReleaseAtCommonRates() {
    constexpr unsigned rates[] = {44100u, 48000u, 88200u, 96000u};
    for (const unsigned rate : rates) {
        native_ldac::agent::V1LinkedStereoBlockLimiterState state{};
        native_ldac::agent::V1LinkedStereoBlockLimiterTelemetry telemetry{};
        float attack[] = {1.0f, -1.0f};
        CHECK(native_ldac::agent::ProcessV1LinkedStereoBlock(
            attack, 1u, 0.25f, rate, 50.0f, &state, &telemetry));
        const std::size_t release_frames =
            static_cast<std::size_t>(rate / 20u);
        CHECK(state.gain > 0.25f && state.gain < 1.0f);
        ProcessSilentFrames(
            release_frames - 1u, rate, 50.0f, &state);
        CHECK(Near(state.gain, 1.0f, 0.0002f));
        CHECK(state.release_step == 0.0f);
    }
}

void TestBlockBoundaryReattackResetsRelease() {
    native_ldac::agent::V1LinkedStereoBlockLimiterState state{};
    native_ldac::agent::V1LinkedStereoBlockLimiterTelemetry telemetry{};
    float first_attack[] = {1.0f, 0.5f};
    CHECK(native_ldac::agent::ProcessV1LinkedStereoBlock(
        first_attack, 1u, 0.25f, 48000u, 50.0f,
        &state, &telemetry));
    const float first_step = state.release_step;
    ProcessSilentFrames(512u, 48000u, 50.0f, &state);
    const float released_gain = state.gain;
    CHECK(released_gain > 0.25f);

    float hotter[] = {2.0f, -0.5f, 0.1f, -0.1f};
    CHECK(native_ldac::agent::ProcessV1LinkedStereoBlock(
        hotter, 2u, 0.25f, 48000u, 50.0f,
        &state, &telemetry));
    CHECK(Near(telemetry.minimum_gain, 0.125f));
    CHECK(telemetry.attack_count == 1u);
    CHECK(telemetry.maximum_gain_step >= released_gain - 0.125f);
    CHECK(state.release_step > first_step);
    CHECK(Near(state.release_step, 0.875f / 2400.0f));
    CHECK(telemetry.fallback_clamp_count == 0u);
}

void TestSanitizationTelemetry() {
    float samples[] = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        0.5f,
        0.0f,
        0.1f,
    };
    native_ldac::agent::V1LinkedStereoBlockLimiterState state{};
    native_ldac::agent::V1LinkedStereoBlockLimiterTelemetry telemetry{};
    CHECK(native_ldac::agent::ProcessV1LinkedStereoBlock(
        samples, 3u, 0.25f, 48000u, 50.0f,
        &state, &telemetry));
    CHECK(telemetry.sanitized_sample_count == 3u);
    CHECK(telemetry.pre_over_ceiling_frame_count == 1u);
    CHECK(telemetry.pre_over_ceiling_sample_count == 1u);
    CHECK(Near(telemetry.block_peak, 0.5f));
    CHECK(telemetry.fallback_clamp_count == 0u);
    for (const float sample : samples) {
        CHECK(std::isfinite(sample));
        CHECK(std::fabs(sample) <= 0.25f);
    }
}

void TestInvalidPathsAreNonMutating() {
    native_ldac::agent::V1LinkedStereoBlockLimiterState state{};
    native_ldac::agent::V1LinkedStereoBlockLimiterTelemetry telemetry{};
    telemetry.block_peak = 0.625f;
    float samples[] = {0.75f, -0.75f};
    const auto unchanged = [&]() {
        CHECK(samples[0] == 0.75f && samples[1] == -0.75f);
        CHECK(state.gain == 1.0f && state.release_step == 0.0f);
        CHECK(telemetry.block_peak == 0.625f);
    };
    CHECK(!native_ldac::agent::ProcessV1LinkedStereoBlock(
        samples, 1u,
        native_ldac::agent::kV1LinkedStereoLimiterHardMaximumCeiling +
            0.00001f,
        48000u, 50.0f, &state, &telemetry));
    unchanged();
    CHECK(!native_ldac::agent::ProcessV1LinkedStereoBlock(
        samples, 1u, 0.0f, 48000u, 50.0f, &state, &telemetry));
    unchanged();
    CHECK(!native_ldac::agent::ProcessV1LinkedStereoBlock(
        samples,
        1u,
        std::numeric_limits<float>::quiet_NaN(),
        48000u,
        50.0f,
        &state,
        &telemetry));
    unchanged();
    CHECK(!native_ldac::agent::ProcessV1LinkedStereoBlock(
        samples, 1u, 0.25f, 0u, 50.0f, &state, &telemetry));
    unchanged();
    CHECK(!native_ldac::agent::ProcessV1LinkedStereoBlock(
        samples, 1u, 0.25f, 48000u, 0.0f, &state, &telemetry));
    unchanged();
    CHECK(!native_ldac::agent::ProcessV1LinkedStereoBlock(
        samples,
        1u,
        0.25f,
        48000u,
        std::numeric_limits<float>::infinity(),
        &state,
        &telemetry));
    unchanged();
    CHECK(!native_ldac::agent::ProcessV1LinkedStereoBlock(
        nullptr, 1u, 0.25f, 48000u, 50.0f, &state, &telemetry));
    unchanged();

    std::vector<float> oversized(
        (native_ldac::agent::kV1LinkedStereoLimiterMaximumFrames + 1u) *
            2u,
        0.75f);
    const std::vector<float> oversized_before = oversized;
    CHECK(!native_ldac::agent::ProcessV1LinkedStereoBlock(
        oversized.data(),
        native_ldac::agent::kV1LinkedStereoLimiterMaximumFrames + 1u,
        0.25f, 48000u, 50.0f, &state, &telemetry));
    CHECK(oversized == oversized_before);
    unchanged();

    state.gain = 0.5f;
    state.release_step = 0.0f;
    CHECK(!native_ldac::agent::ProcessV1LinkedStereoBlock(
        samples, 1u, 0.25f, 48000u, 50.0f, &state, &telemetry));
    CHECK(samples[0] == 0.75f && samples[1] == -0.75f);
    CHECK(state.gain == 0.5f && state.release_step == 0.0f);
    CHECK(telemetry.block_peak == 0.625f);
}

void TestMinusOneDbfsSamplePeakCeiling() {
    native_ldac::agent::V1LinkedStereoBlockLimiterState state{};
    native_ldac::agent::V1LinkedStereoBlockLimiterTelemetry telemetry{};
    float samples[] = {1.0f, -0.5f, 0.35f, -0.35f};
    constexpr float ceiling = 0.89125094f;
    CHECK(native_ldac::agent::ProcessV1LinkedStereoBlock(
        samples, 2u, ceiling, 48000u, 50.0f, &state, &telemetry));
    CHECK(std::fabs(samples[0]) <= ceiling);
    CHECK(std::fabs(samples[1]) <= ceiling);
    CHECK(telemetry.minimum_gain < 1.0f);
    CHECK(telemetry.fallback_clamp_count == 0u);
}

void TestUnitySampleBoundaryPreservesFullScale() {
    native_ldac::agent::V1LinkedStereoBlockLimiterState state{};
    native_ldac::agent::V1LinkedStereoBlockLimiterTelemetry telemetry{};
    float samples[] = {1.0f, -1.0f, 0.35f, -0.35f};
    float ceiling =
        native_ldac::agent::kV1LinkedStereoLimiterHardMaximumCeiling;
    CHECK(ceiling == 1.0f);
    CHECK(native_ldac::agent::ProcessV1LinkedStereoBlock(
        samples, 2u, ceiling, 48000u, 50.0f, &state, &telemetry));
    CHECK(samples[0] == 1.0f && samples[1] == -1.0f);
    CHECK(telemetry.minimum_gain == 1.0f);
    CHECK(telemetry.attack_count == 0u);
    CHECK(telemetry.reduced_frame_count == 0u);
    CHECK(telemetry.fallback_clamp_count == 0u);
}

std::uint32_t NextRandom(std::uint32_t* state) {
    std::uint32_t value = *state;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    *state = value;
    return value;
}

void TestDeterministicRandomProperties() {
    std::uint32_t random = 0x6d2b79f5u;
    native_ldac::agent::V1LinkedStereoBlockLimiterState state{};
    for (std::size_t block_index = 0u; block_index < 2000u; ++block_index) {
        const std::size_t frames =
            1u + NextRandom(&random) %
                native_ldac::agent::kV1LinkedStereoLimiterMaximumFrames;
        std::vector<float> samples(frames * 2u);
        std::uint64_t expected_sanitized = 0u;
        float expected_peak = 0.0f;
        for (float& sample : samples) {
            const std::uint32_t bits = NextRandom(&random);
            if (bits % 257u == 0u) {
                sample = std::numeric_limits<float>::quiet_NaN();
                ++expected_sanitized;
            } else if (bits % 263u == 0u) {
                sample = std::numeric_limits<float>::infinity();
                ++expected_sanitized;
            } else {
                sample =
                    (static_cast<float>(bits & 0xffffu) / 16383.75f) - 2.0f;
                expected_peak = std::max(expected_peak, std::fabs(sample));
            }
        }
        const float ceiling = 0.05f +
            static_cast<float>(NextRandom(&random) % 201u) / 1000.0f;
        const float release_ms =
            1.0f + static_cast<float>(NextRandom(&random) % 200u);
        native_ldac::agent::V1LinkedStereoBlockLimiterTelemetry telemetry{};
        CHECK(native_ldac::agent::ProcessV1LinkedStereoBlock(
            samples.data(), frames, ceiling, 48000u, release_ms,
            &state, &telemetry));
        CHECK(Near(telemetry.block_peak, expected_peak, 0.00001f));
        CHECK(telemetry.sanitized_sample_count == expected_sanitized);
        CHECK(telemetry.fallback_clamp_count == 0u);
        CHECK(telemetry.reduced_frame_count <= frames);
        CHECK(telemetry.reduced_sample_count <= frames * 2u);
        CHECK(telemetry.minimum_gain >= 0.0f &&
              telemetry.minimum_gain <= 1.0f);
        CHECK(telemetry.last_gain >= telemetry.minimum_gain &&
              telemetry.last_gain <= 1.0f);
        CHECK(state.gain >= 0.0f && state.gain <= 1.0f);
        for (const float sample : samples) {
            CHECK(std::isfinite(sample));
            CHECK(std::fabs(sample) <= ceiling);
        }
    }
}

}  // namespace

int main() {
    TestBlockPreattackAndLatePeak();
    TestFirstPeakAndLinkedStereoRatios();
    TestExactCeilingAndEpsilon();
    TestSustainedHotAlternatingAndAntiphase();
    TestFiftyMillisecondReleaseAtCommonRates();
    TestBlockBoundaryReattackResetsRelease();
    TestSanitizationTelemetry();
    TestInvalidPathsAreNonMutating();
    TestMinusOneDbfsSamplePeakCeiling();
    TestUnitySampleBoundaryPreservesFullScale();
    TestDeterministicRandomProperties();
    if (failures != 0) {
        std::fprintf(stderr,
                     "V1 linked-stereo block limiter tests failed: %d.\n",
                     failures);
        return 1;
    }
    std::printf("V1 linked-stereo block limiter tests passed.\n");
    return 0;
}
