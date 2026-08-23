// SPDX-License-Identifier: Apache-2.0
#include <cmath>
#include <cstdio>

#include "native_pcm_volume_logic.h"

static int expect_true(bool condition, const char *message) {
    if (condition) return 0;
    std::fprintf(stderr, "native_pcm_volume_logic_tests: %s\n", message);
    return 1;
}

int main() {
    int failed = 0;
    native_pcm_volume_state state;
    float samples[] = {1.0f, -0.5f, 0.25f};

    failed += expect_true(!state.available && state.muted &&
                              state.scalar == 0.0f && state.gain == 0.0f,
                          "default state must fail muted");
    native_pcm_volume_apply(samples, 3u, state);
    failed += expect_true(samples[0] == 0.0f && samples[1] == 0.0f &&
                              samples[2] == 0.0f,
                          "unavailable volume must zero PCM samples");

    native_pcm_volume_set_endpoint(&state, false, 0.5f, -6.0f);
    failed += expect_true(state.available && !state.muted &&
                              std::fabs(state.gain - 0.5011872f) < 0.0001f,
                          "endpoint decibels must produce bounded gain");
    float attenuated[] = {1.0f, -1.0f};
    native_pcm_volume_apply(attenuated, 2u, state);
    failed += expect_true(
        std::fabs(attenuated[0] - state.gain) < 0.0001f &&
            std::fabs(attenuated[1] + state.gain) < 0.0001f,
        "available endpoint gain must attenuate PCM");

    native_pcm_volume_set_endpoint(&state, true, 1.0f, 0.0f);
    failed += expect_true(state.available && state.muted &&
                              state.gain == 0.0f,
                          "Windows mute must force zero gain");
    native_pcm_volume_fail_muted(&state);
    failed += expect_true(!state.available && state.muted &&
                              state.gain == 0.0f,
                          "interface failure must restore fail-muted state");

    if (failed == 0) {
        std::puts("Native PCM volume safety tests passed.");
    }
    return failed == 0 ? 0 : 1;
}
