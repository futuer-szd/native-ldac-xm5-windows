// SPDX-License-Identifier: Apache-2.0
#ifndef LDAC_NATIVE_PCM_VOLUME_LOGIC_H
#define LDAC_NATIVE_PCM_VOLUME_LOGIC_H

#include <algorithm>
#include <cmath>
#include <cstddef>

struct native_pcm_volume_state {
    bool available = false;
    bool muted = true;
    float scalar = 0.0f;
    float decibels = -96.0f;
    float gain = 0.0f;
};

inline void native_pcm_volume_fail_muted(native_pcm_volume_state *state) {
    if (state == nullptr) return;
    state->available = false;
    state->muted = true;
    state->scalar = 0.0f;
    state->decibels = -96.0f;
    state->gain = 0.0f;
}

inline void native_pcm_volume_set_endpoint(native_pcm_volume_state *state,
                                           bool muted,
                                           float scalar,
                                           float decibels) {
    if (state == nullptr) return;
    state->available = true;
    state->muted = muted;
    state->scalar = std::clamp(scalar, 0.0f, 1.0f);
    state->decibels = decibels;
    state->gain = muted
        ? 0.0f
        : static_cast<float>(std::pow(10.0, decibels / 20.0));
    state->gain = std::clamp(state->gain, 0.0f, 1.0f);
}

inline void native_pcm_volume_apply(float *samples,
                                    std::size_t sample_count,
                                    const native_pcm_volume_state& state) {
    if (samples == nullptr) return;
    if (state.gain == 1.0f) return;
    for (std::size_t index = 0u; index < sample_count; ++index) {
        samples[index] *= state.gain;
    }
}

#endif
