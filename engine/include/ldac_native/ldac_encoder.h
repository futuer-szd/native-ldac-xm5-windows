// SPDX-License-Identifier: Apache-2.0
#ifndef LDAC_NATIVE_ENCODER_H
#define LDAC_NATIVE_ENCODER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LDAC_ENCODER_PCM_FRAMES_PER_CALL 128u
#define LDAC_ENCODER_STEREO_CHANNELS 2u
#define LDAC_ENCODER_MAX_OUTPUT_BYTES 1024u
#define LDAC_ENCODER_MIN_PAYLOAD_MTU 679u

typedef enum ldac_encoder_quality {
    LDAC_ENCODER_QUALITY_HQ = 0,
    LDAC_ENCODER_QUALITY_SQ = 1,
    LDAC_ENCODER_QUALITY_MQ = 2
} ldac_encoder_quality;

typedef enum ldac_encoder_channel_mode {
    LDAC_ENCODER_CHANNEL_STEREO = 1,
    LDAC_ENCODER_CHANNEL_DUAL = 2,
    LDAC_ENCODER_CHANNEL_MONO = 4
} ldac_encoder_channel_mode;

typedef enum ldac_encoder_status {
    LDAC_ENCODER_OK = 0,
    LDAC_ENCODER_INVALID_ARGUMENT = -1,
    LDAC_ENCODER_NO_MEMORY = -2,
    LDAC_ENCODER_INITIALIZATION_FAILED = -3,
    LDAC_ENCODER_ENCODING_FAILED = -4,
    LDAC_ENCODER_NO_SPACE = -5,
    LDAC_ENCODER_INVALID_OUTPUT = -6,
    LDAC_ENCODER_CONFIGURATION_FAILED = -7
} ldac_encoder_status;

typedef struct ldac_encoder ldac_encoder;

ldac_encoder_status ldac_encoder_create(ldac_encoder **out,
                                        size_t encoded_payload_mtu,
                                        ldac_encoder_quality quality,
                                        unsigned sample_rate_hz);

ldac_encoder_status ldac_encoder_create_with_channel_mode(
    ldac_encoder **out,
    size_t encoded_payload_mtu,
    ldac_encoder_quality quality,
    unsigned sample_rate_hz,
    ldac_encoder_channel_mode channel_mode);

void ldac_encoder_destroy(ldac_encoder *encoder);

ldac_encoder_status ldac_encoder_set_quality(
    ldac_encoder *encoder,
    ldac_encoder_quality quality);

ldac_encoder_status ldac_encoder_encode_f32(
    ldac_encoder *encoder,
    const float *interleaved_stereo_pcm,
    size_t pcm_frames,
    uint8_t *encoded,
    size_t encoded_capacity,
    size_t *encoded_size,
    uint8_t *transport_frame_count);

unsigned ldac_encoder_sample_rate_hz(const ldac_encoder *encoder);
unsigned ldac_encoder_samples_per_transport_frame(
    const ldac_encoder *encoder);
unsigned ldac_encoder_nominal_bitrate_kbps(const ldac_encoder *encoder);
ldac_encoder_quality ldac_encoder_quality_mode(const ldac_encoder *encoder);
ldac_encoder_channel_mode ldac_encoder_channel_mode_value(
    const ldac_encoder *encoder);
int ldac_encoder_last_error(const ldac_encoder *encoder);

#ifdef __cplusplus
}
#endif

#endif
