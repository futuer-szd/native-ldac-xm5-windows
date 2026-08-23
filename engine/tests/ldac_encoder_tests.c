// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ldac_native/ldac_encoder.h"
#include "ldac_native/rtp_ldac.h"

static int failures = 0;

#define CHECK(condition) do {                                                \
    if (!(condition)) {                                                      \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n",                    \
                __FILE__, __LINE__, #condition);                             \
        failures++;                                                          \
    }                                                                        \
} while (0)

static void test_quality(ldac_encoder_quality quality,
                         unsigned expectedKbps) {
    enum { SAMPLE_RATE = 96000, ENCODE_CALLS = 750 };
    float silence[LDAC_ENCODER_PCM_FRAMES_PER_CALL *
                  LDAC_ENCODER_STEREO_CHANNELS];
    uint8_t encoded[LDAC_ENCODER_MAX_OUTPUT_BYTES];
    uint8_t packet[1100];
    ldac_encoder *encoder = NULL;
    uint64_t encodedBytes = 0u;
    uint32_t timestamp = 0u;
    uint16_t sequence = 0u;
    size_t maxEncodedSize = 0u;
    unsigned packetCount = 0u;
    int call;

    memset(silence, 0, sizeof(silence));
    CHECK(ldac_encoder_create(&encoder,
                              1008u,
                              quality,
                              SAMPLE_RATE) == LDAC_ENCODER_OK);
    if (encoder == NULL) return;
    CHECK(ldac_encoder_sample_rate_hz(encoder) == SAMPLE_RATE);
    CHECK(ldac_encoder_samples_per_transport_frame(encoder) == 256u);
    CHECK(ldac_encoder_nominal_bitrate_kbps(encoder) == expectedKbps);

    for (call = 0; call < ENCODE_CALLS; ++call) {
        size_t encodedSize = 0u;
        size_t packetSize = 0u;
        uint8_t frameCount = 0u;
        ldac_encoder_status status = ldac_encoder_encode_f32(
            encoder,
            silence,
            LDAC_ENCODER_PCM_FRAMES_PER_CALL,
            encoded,
            sizeof(encoded),
            &encodedSize,
            &frameCount);
        CHECK(status == LDAC_ENCODER_OK);
        if (status != LDAC_ENCODER_OK || encodedSize == 0u) continue;
        CHECK(frameCount > 0u && frameCount <= LDAC_RTP_MAX_FRAME_COUNT);
        CHECK(ldac_rtp_build_unfragmented(packet,
                                          sizeof(packet),
                                          1021u,
                                          sequence,
                                          timestamp,
                                          0x4C444143u,
                                          frameCount,
                                          encoded,
                                          encodedSize,
                                          &packetSize) == LDAC_RTP_OK);
        CHECK(packetSize <= 1021u);
        encodedBytes += encodedSize;
        if (encodedSize > maxEncodedSize) maxEncodedSize = encodedSize;
        timestamp += frameCount *
                     ldac_encoder_samples_per_transport_frame(encoder);
        sequence++;
        packetCount++;
    }

    {
        unsigned measuredKbps = (unsigned)((encodedBytes * 8u) / 1000u);
        unsigned lower = (expectedKbps * 94u) / 100u;
        unsigned upper = (expectedKbps * 106u) / 100u;
        printf("96 kHz quality %d: %u packets, max %zu bytes, %u kbps\n",
               (int)quality,
               packetCount,
               maxEncodedSize,
               measuredKbps);
        CHECK(measuredKbps >= lower && measuredKbps <= upper);
        CHECK(packetCount > 0u);
        CHECK(timestamp > 0u);
    }
    ldac_encoder_destroy(encoder);
}

static uint64_t encode_byte_window(ldac_encoder *encoder,
                                   unsigned encodeCalls) {
    float silence[LDAC_ENCODER_PCM_FRAMES_PER_CALL *
                  LDAC_ENCODER_STEREO_CHANNELS];
    uint8_t encoded[LDAC_ENCODER_MAX_OUTPUT_BYTES];
    uint64_t encodedBytes = 0u;
    unsigned call;

    memset(silence, 0, sizeof(silence));
    for (call = 0u; call < encodeCalls; ++call) {
        size_t encodedSize = 0u;
        uint8_t frameCount = 0u;
        ldac_encoder_status status = ldac_encoder_encode_f32(
            encoder,
            silence,
            LDAC_ENCODER_PCM_FRAMES_PER_CALL,
            encoded,
            sizeof(encoded),
            &encodedSize,
            &frameCount);
        CHECK(status == LDAC_ENCODER_OK);
        if (status == LDAC_ENCODER_OK) encodedBytes += encodedSize;
    }
    return encodedBytes;
}

static void test_dynamic_quality(void) {
    enum { SAMPLE_RATE = 96000, ENCODE_CALLS = 750 };
    ldac_encoder *encoder = NULL;
    uint64_t hqBytes;
    uint64_t sqBytes;
    uint64_t mqBytes;

    CHECK(ldac_encoder_create(&encoder,
                              1008u,
                              LDAC_ENCODER_QUALITY_HQ,
                              SAMPLE_RATE) == LDAC_ENCODER_OK);
    if (encoder == NULL) return;
    hqBytes = encode_byte_window(encoder, ENCODE_CALLS);
    CHECK(ldac_encoder_set_quality(encoder,
                                   LDAC_ENCODER_QUALITY_SQ) ==
          LDAC_ENCODER_OK);
    CHECK(ldac_encoder_quality_mode(encoder) == LDAC_ENCODER_QUALITY_SQ);
    CHECK(ldac_encoder_nominal_bitrate_kbps(encoder) == 660u);
    sqBytes = encode_byte_window(encoder, ENCODE_CALLS);
    CHECK(ldac_encoder_set_quality(encoder,
                                   LDAC_ENCODER_QUALITY_MQ) ==
          LDAC_ENCODER_OK);
    CHECK(ldac_encoder_quality_mode(encoder) == LDAC_ENCODER_QUALITY_MQ);
    CHECK(ldac_encoder_nominal_bitrate_kbps(encoder) == 330u);
    mqBytes = encode_byte_window(encoder, ENCODE_CALLS);
    CHECK(hqBytes > sqBytes);
    CHECK(sqBytes > mqBytes);
    CHECK(ldac_encoder_set_quality(
              encoder,
              (ldac_encoder_quality)99) == LDAC_ENCODER_INVALID_ARGUMENT);
    ldac_encoder_destroy(encoder);
}

static void test_channel_modes(void) {
    static const ldac_encoder_channel_mode modes[] = {
        LDAC_ENCODER_CHANNEL_STEREO,
        LDAC_ENCODER_CHANNEL_DUAL,
        LDAC_ENCODER_CHANNEL_MONO
    };
    size_t index;

    for (index = 0u; index < sizeof(modes) / sizeof(modes[0]); ++index) {
        ldac_encoder *encoder = NULL;
        CHECK(ldac_encoder_create_with_channel_mode(
                  &encoder,
                  1008u,
                  LDAC_ENCODER_QUALITY_MQ,
                  48000u,
                  modes[index]) == LDAC_ENCODER_OK);
        if (encoder == NULL) continue;
        CHECK(ldac_encoder_channel_mode_value(encoder) == modes[index]);
        CHECK(ldac_encoder_nominal_bitrate_kbps(encoder) > 0u);
        CHECK(encode_byte_window(encoder, 100u) > 0u);
        ldac_encoder_destroy(encoder);
    }
    {
        ldac_encoder *encoder = NULL;
        CHECK(ldac_encoder_create_with_channel_mode(
                  &encoder,
                  1008u,
                  LDAC_ENCODER_QUALITY_MQ,
                  48000u,
                  (ldac_encoder_channel_mode)8) ==
              LDAC_ENCODER_INVALID_ARGUMENT);
        CHECK(encoder == NULL);
    }
}

int main(void) {
    ldac_encoder *encoder = NULL;
    CHECK(ldac_encoder_create(&encoder,
                              LDAC_ENCODER_MIN_PAYLOAD_MTU - 1u,
                              LDAC_ENCODER_QUALITY_MQ,
                              96000u) == LDAC_ENCODER_INVALID_ARGUMENT);
    CHECK(encoder == NULL);
    test_quality(LDAC_ENCODER_QUALITY_MQ, 330u);
    test_quality(LDAC_ENCODER_QUALITY_SQ, 660u);
    test_quality(LDAC_ENCODER_QUALITY_HQ, 990u);
    test_dynamic_quality();
    test_channel_modes();
    if (failures != 0) {
        fprintf(stderr, "%d encoder test(s) failed\n", failures);
        return 1;
    }
    puts("All LDAC encoder tests passed.");
    return 0;
}
