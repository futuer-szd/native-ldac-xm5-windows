// SPDX-License-Identifier: Apache-2.0
#include "../v1_sent_frame_fade_envelope.h"

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

void TestFadeTracksOnlyCommittedSentFrames() {
    native_ldac::agent::V1SentFrameFadeState state{};
    CHECK(native_ldac::agent::BeginV1SentFrameFadeSession(&state, 7u));
    std::vector<float> source(128u * 2u, 1.0f);
    std::vector<float> first(source.size(), 0.0f);
    std::vector<float> repeated(source.size(), 0.0f);
    native_ldac::agent::V1SentFrameFadeBlock first_block{};
    native_ldac::agent::V1SentFrameFadeBlock repeated_block{};
    native_ldac::agent::V1SentFrameFadeTelemetry first_telemetry{};
    native_ldac::agent::V1SentFrameFadeTelemetry repeated_telemetry{};
    CHECK(native_ldac::agent::PrepareV1SentFrameFadeBlock(
        source.data(), first.data(), 128u, 48000u, 100.0f,
        &state, &first_block, &first_telemetry));
    CHECK(state.committed_sent_frames == 0u);
    CHECK(native_ldac::agent::PrepareV1SentFrameFadeBlock(
        source.data(), repeated.data(), 128u, 48000u, 100.0f,
        &state, &repeated_block, &repeated_telemetry));
    CHECK(first == repeated);
    CHECK(first_block.starting_sent_frame == 0u);
    CHECK(repeated_block.starting_sent_frame == 0u);
    CHECK(Near(first.front(), 1.0f / 4800.0f));
    CHECK(!native_ldac::agent::CommitV1SentFrameFadeBlock(
        &state, first_block, 64u));
    CHECK(state.committed_sent_frames == 0u);
    CHECK(native_ldac::agent::CommitV1SentFrameFadeBlock(
        &state, first_block, 128u));
    CHECK(state.committed_sent_frames == 128u);
    CHECK(!native_ldac::agent::CommitV1SentFrameFadeBlock(
        &state, repeated_block, 128u));
    CHECK(state.committed_sent_frames == 128u);
}

void TestHundredMillisecondFadeAtCommonRates() {
    constexpr unsigned rates[] = {44100u, 48000u, 88200u, 96000u};
    for (const unsigned rate : rates) {
        native_ldac::agent::V1SentFrameFadeState state{};
        CHECK(native_ldac::agent::BeginV1SentFrameFadeSession(
            &state, static_cast<std::uint64_t>(rate)));
        const std::uint64_t duration = rate / 10u;
        while (state.committed_sent_frames < duration) {
            const std::size_t frames = static_cast<std::size_t>(std::min<
                std::uint64_t>(
                native_ldac::agent::kV1SentFrameFadeMaximumFrames,
                duration - state.committed_sent_frames));
            std::vector<float> source(frames * 2u, 1.0f);
            std::vector<float> destination(frames * 2u, 0.0f);
            native_ldac::agent::V1SentFrameFadeBlock block{};
            native_ldac::agent::V1SentFrameFadeTelemetry telemetry{};
            CHECK(native_ldac::agent::PrepareV1SentFrameFadeBlock(
                source.data(), destination.data(), frames, rate, 100.0f,
                &state, &block, &telemetry));
            CHECK(telemetry.starting_sent_frame ==
                  state.committed_sent_frames);
            CHECK(telemetry.fade_duration_frames == duration);
            CHECK(native_ldac::agent::CommitV1SentFrameFadeBlock(
                &state, block, frames));
        }
        CHECK(state.committed_sent_frames == duration);
        float source[] = {0.75f, -0.75f};
        float destination[] = {0.0f, 0.0f};
        native_ldac::agent::V1SentFrameFadeBlock block{};
        native_ldac::agent::V1SentFrameFadeTelemetry telemetry{};
        CHECK(native_ldac::agent::PrepareV1SentFrameFadeBlock(
            source, destination, 1u, rate, 100.0f,
            &state, &block, &telemetry));
        CHECK(telemetry.minimum_gain == 1.0f);
        CHECK(telemetry.last_gain == 1.0f);
        CHECK(telemetry.frames_below_unity == 0u);
        CHECK(destination[0] == source[0]);
        CHECK(destination[1] == source[1]);
    }
}

void TestGenerationAndStopInvalidatePendingBlocks() {
    native_ldac::agent::V1SentFrameFadeState state{};
    CHECK(native_ldac::agent::BeginV1SentFrameFadeSession(&state, 1u));
    float source[] = {1.0f, -1.0f};
    float destination[] = {0.0f, 0.0f};
    native_ldac::agent::V1SentFrameFadeBlock old_block{};
    native_ldac::agent::V1SentFrameFadeTelemetry telemetry{};
    CHECK(native_ldac::agent::PrepareV1SentFrameFadeBlock(
        source, destination, 1u, 48000u, 100.0f,
        &state, &old_block, &telemetry));
    native_ldac::agent::EndV1SentFrameFadeSession(&state);
    CHECK(!native_ldac::agent::CommitV1SentFrameFadeBlock(
        &state, old_block, 1u));
    CHECK(state.committed_sent_frames == 0u);

    CHECK(native_ldac::agent::BeginV1SentFrameFadeSession(&state, 2u));
    CHECK(!native_ldac::agent::CommitV1SentFrameFadeBlock(
        &state, old_block, 1u));
    native_ldac::agent::V1SentFrameFadeBlock new_block{};
    CHECK(native_ldac::agent::PrepareV1SentFrameFadeBlock(
        source, destination, 1u, 48000u, 100.0f,
        &state, &new_block, &telemetry));
    CHECK(new_block.starting_sent_frame == 0u);
    CHECK(Near(destination[0], 1.0f / 4800.0f));
    CHECK(native_ldac::agent::CommitV1SentFrameFadeBlock(
        &state, new_block, 1u));
}

void TestBlockBoundaryAndSanitization() {
    native_ldac::agent::V1SentFrameFadeState state{};
    CHECK(native_ldac::agent::BeginV1SentFrameFadeSession(&state, 3u));
    float source[] = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        1.0f,
        -1.0f,
    };
    float destination[] = {9.0f, 9.0f, 9.0f, 9.0f};
    native_ldac::agent::V1SentFrameFadeBlock block{};
    native_ldac::agent::V1SentFrameFadeTelemetry telemetry{};
    CHECK(native_ldac::agent::PrepareV1SentFrameFadeBlock(
        source, destination, 2u, 48000u, 100.0f,
        &state, &block, &telemetry));
    CHECK(destination[0] == 0.0f && destination[1] == 0.0f);
    CHECK(Near(destination[2], 2.0f / 4800.0f));
    CHECK(Near(destination[3], -2.0f / 4800.0f));
    CHECK(telemetry.sanitized_sample_count == 2u);
    CHECK(native_ldac::agent::CommitV1SentFrameFadeBlock(
        &state, block, 2u));

    float next_source[] = {1.0f, -1.0f};
    float next_destination[] = {0.0f, 0.0f};
    CHECK(native_ldac::agent::PrepareV1SentFrameFadeBlock(
        next_source, next_destination, 1u, 48000u, 100.0f,
        &state, &block, &telemetry));
    CHECK(block.starting_sent_frame == 2u);
    CHECK(Near(next_destination[0], 3.0f / 4800.0f));
}

void TestInvalidPathsAreNonMutating() {
    native_ldac::agent::V1SentFrameFadeState state{};
    CHECK(native_ldac::agent::BeginV1SentFrameFadeSession(&state, 9u));
    float source[] = {1.0f, -1.0f};
    float destination[] = {7.0f, 7.0f};
    native_ldac::agent::V1SentFrameFadeBlock block{};
    block.frame_count = 77u;
    native_ldac::agent::V1SentFrameFadeTelemetry telemetry{};
    telemetry.minimum_gain = 0.75f;
    CHECK(!native_ldac::agent::PrepareV1SentFrameFadeBlock(
        source, source, 1u, 48000u, 100.0f,
        &state, &block, &telemetry));
    CHECK(!native_ldac::agent::PrepareV1SentFrameFadeBlock(
        source, destination, 129u, 48000u, 100.0f,
        &state, &block, &telemetry));
    CHECK(!native_ldac::agent::PrepareV1SentFrameFadeBlock(
        source, destination, 1u, 0u, 100.0f,
        &state, &block, &telemetry));
    CHECK(!native_ldac::agent::PrepareV1SentFrameFadeBlock(
        source, destination, 1u, 48000u, 0.0f,
        &state, &block, &telemetry));
    CHECK(destination[0] == 7.0f && destination[1] == 7.0f);
    CHECK(state.committed_sent_frames == 0u);
    CHECK(block.frame_count == 77u);
    CHECK(telemetry.minimum_gain == 0.75f);
    CHECK(!native_ldac::agent::BeginV1SentFrameFadeSession(nullptr, 1u));
    CHECK(!native_ldac::agent::BeginV1SentFrameFadeSession(&state, 0u));
}

}  // namespace

int main() {
    TestFadeTracksOnlyCommittedSentFrames();
    TestHundredMillisecondFadeAtCommonRates();
    TestGenerationAndStopInvalidatePendingBlocks();
    TestBlockBoundaryAndSanitization();
    TestInvalidPathsAreNonMutating();
    if (failures != 0) {
        std::fprintf(stderr,
                     "V1 sent-frame fade envelope tests failed: %d.\n",
                     failures);
        return 1;
    }
    std::printf("V1 sent-frame fade envelope tests passed.\n");
    return 0;
}
