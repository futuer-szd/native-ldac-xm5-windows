// SPDX-License-Identifier: Apache-2.0
#include <cstdio>

#include "ldac_native/native_pcm_source.h"

static int fail(const char *message) {
    std::fprintf(stderr, "native_pcm_source_tests: %s\n", message);
    return 1;
}

int main() {
    native_pcm_source *source = nullptr;
    native_pcm_source_status status = native_pcm_source_create(&source);
    if (status == NATIVE_PCM_SOURCE_NOT_FOUND) {
        std::puts("Native LDAC PCM endpoint is not installed; skipping.");
        return 77;
    }
    if (status != NATIVE_PCM_SOURCE_OK || source == nullptr) {
        return fail("could not open the installed PCM endpoint");
    }

    int result = 0;
    native_pcm_source *specific_source = nullptr;
    if (native_pcm_source_create_for_interface(
            native_pcm_source_interface_path(source),
            &specific_source) != NATIVE_PCM_SOURCE_OK ||
        specific_source == nullptr) {
        result = fail("could not reopen the selected PCM interface");
    }
    native_pcm_source_destroy(specific_source);
    if (native_pcm_source_sample_rate_hz(source) != 48000u ||
        native_pcm_source_channels(source) != 2u ||
        native_pcm_source_bits_per_sample(source) != 16u) {
        result = fail("unexpected endpoint PCM format");
    }

    native_pcm_source_snapshot snapshot{};
    if (result == 0 &&
        native_pcm_source_get_snapshot(source, &snapshot) !=
            NATIVE_PCM_SOURCE_OK) {
        result = fail("could not read the endpoint snapshot");
    }
    native_pcm_link_snapshot link_snapshot{};
    status = native_pcm_source_get_link_state(source, &link_snapshot);
    if (result == 0 && status != NATIVE_PCM_SOURCE_OK &&
        status != NATIVE_PCM_SOURCE_UNSUPPORTED_PROPERTY) {
        std::fprintf(stderr,
                     "native_pcm_source_tests: link-state status %d, "
                     "Win32 %lu\n",
                     static_cast<int>(status),
                     native_pcm_source_last_error(source));
        result = fail("link-state query returned an unexpected error");
    }
    if (result == 0 && status == NATIVE_PCM_SOURCE_OK &&
        (link_snapshot.state < NATIVE_PCM_LINK_DISCONNECTED ||
         link_snapshot.state > NATIVE_PCM_LINK_STOPPING)) {
        result = fail("link-state query returned an invalid state");
    }
    if (result == 0 && !snapshot.volume_control_available &&
        (status != NATIVE_PCM_SOURCE_OK ||
         link_snapshot.state == NATIVE_PCM_LINK_CONNECTED)) {
        result = fail("Windows endpoint master volume is unavailable while "
                      "the endpoint should be online");
    }

    float pcm[128u * 2u]{};
    size_t frames_read = 0u;
    status = native_pcm_source_read_f32_stereo(source,
                                               pcm,
                                               128u,
                                               0u,
                                               &frames_read);
    if (result == 0 && status != NATIVE_PCM_SOURCE_OK &&
        status != NATIVE_PCM_SOURCE_TIMEOUT) {
        result = fail("PCM read returned an unexpected error");
    }
    if (result == 0 && status == NATIVE_PCM_SOURCE_OK &&
        frames_read != 128u) {
        result = fail("successful PCM read returned a partial block");
    }
    if (result == 0 && status == NATIVE_PCM_SOURCE_TIMEOUT &&
        frames_read != 0u) {
        result = fail("timed-out PCM read reported consumed frames");
    }

    if (result == 0 && snapshot.volume_control_available) {
        std::printf("PCM endpoint OK: %u Hz, %u channel(s), %u-bit, "
                    "epoch %llu, buffer %u/%u bytes, volume %.0f%%%s.\n",
                    snapshot.sample_rate_hz,
                    snapshot.channels,
                    snapshot.bits_per_sample,
                    static_cast<unsigned long long>(snapshot.stream_epoch),
                    snapshot.available_bytes,
                    snapshot.capacity_bytes,
                    snapshot.volume_scalar * 100.0f,
                    snapshot.muted ? " (muted)" : "");
    } else if (result == 0) {
        std::printf("PCM endpoint OK while offline: %u Hz, %u channel(s), "
                    "%u-bit, epoch %llu, buffer %u/%u bytes; Windows "
                    "volume control is intentionally unavailable.\n",
                    snapshot.sample_rate_hz,
                    snapshot.channels,
                    snapshot.bits_per_sample,
                    static_cast<unsigned long long>(snapshot.stream_epoch),
                    snapshot.available_bytes,
                    snapshot.capacity_bytes);
    }
    native_pcm_source_destroy(source);
    return result;
}
