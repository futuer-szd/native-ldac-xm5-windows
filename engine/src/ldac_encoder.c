// SPDX-License-Identifier: Apache-2.0
#include "ldac_native/ldac_encoder.h"

#include <limits.h>
#include <stdlib.h>

#include <ldacBT.h>

struct ldac_encoder {
    HANDLE_LDAC_BT handle;
    size_t encoded_payload_mtu;
    ldac_encoder_quality quality;
    ldac_encoder_channel_mode channel_mode;
    unsigned sample_rate_hz;
    int last_error;
};

static int supported_sample_rate(unsigned sample_rate_hz) {
    return sample_rate_hz == 44100u || sample_rate_hz == 48000u ||
           sample_rate_hz == 88200u || sample_rate_hz == 96000u;
}

static int supported_quality(ldac_encoder_quality quality) {
    return quality == LDAC_ENCODER_QUALITY_HQ ||
           quality == LDAC_ENCODER_QUALITY_SQ ||
           quality == LDAC_ENCODER_QUALITY_MQ;
}

static int supported_channel_mode(ldac_encoder_channel_mode channel_mode) {
    return channel_mode == LDAC_ENCODER_CHANNEL_STEREO ||
           channel_mode == LDAC_ENCODER_CHANNEL_DUAL ||
           channel_mode == LDAC_ENCODER_CHANNEL_MONO;
}

ldac_encoder_status ldac_encoder_create(ldac_encoder **out,
                                        size_t encoded_payload_mtu,
                                        ldac_encoder_quality quality,
                                        unsigned sample_rate_hz) {
    return ldac_encoder_create_with_channel_mode(
        out,
        encoded_payload_mtu,
        quality,
        sample_rate_hz,
        LDAC_ENCODER_CHANNEL_STEREO);
}

ldac_encoder_status ldac_encoder_create_with_channel_mode(
    ldac_encoder **out,
    size_t encoded_payload_mtu,
    ldac_encoder_quality quality,
    unsigned sample_rate_hz,
    ldac_encoder_channel_mode channel_mode) {
    ldac_encoder *encoder;
    int result;

    if (out == NULL) return LDAC_ENCODER_INVALID_ARGUMENT;
    *out = NULL;
    if (encoded_payload_mtu < LDAC_ENCODER_MIN_PAYLOAD_MTU ||
        encoded_payload_mtu > INT_MAX ||
        !supported_quality(quality) ||
        !supported_sample_rate(sample_rate_hz) ||
        !supported_channel_mode(channel_mode)) {
        return LDAC_ENCODER_INVALID_ARGUMENT;
    }

    encoder = (ldac_encoder *)calloc(1u, sizeof(*encoder));
    if (encoder == NULL) return LDAC_ENCODER_NO_MEMORY;
    encoder->handle = ldacBT_get_handle();
    if (encoder->handle == NULL) {
        free(encoder);
        return LDAC_ENCODER_NO_MEMORY;
    }
    result = ldacBT_init_handle_encode(encoder->handle,
                                       (int)encoded_payload_mtu,
                                       (int)quality,
                                       (int)channel_mode,
                                       LDACBT_SMPL_FMT_F32,
                                       (int)sample_rate_hz);
    if (result != 0) {
        encoder->last_error = ldacBT_get_error_code(encoder->handle);
        ldacBT_free_handle(encoder->handle);
        free(encoder);
        return LDAC_ENCODER_INITIALIZATION_FAILED;
    }
    encoder->encoded_payload_mtu = encoded_payload_mtu;
    encoder->quality = quality;
    encoder->channel_mode = channel_mode;
    encoder->sample_rate_hz = sample_rate_hz;
    *out = encoder;
    return LDAC_ENCODER_OK;
}

void ldac_encoder_destroy(ldac_encoder *encoder) {
    if (encoder == NULL) return;
    if (encoder->handle != NULL) ldacBT_free_handle(encoder->handle);
    free(encoder);
}

ldac_encoder_status ldac_encoder_set_quality(
    ldac_encoder *encoder,
    ldac_encoder_quality quality) {
    if (encoder == NULL || encoder->handle == NULL ||
        !supported_quality(quality)) {
        return LDAC_ENCODER_INVALID_ARGUMENT;
    }
    if (ldacBT_set_eqmid(encoder->handle, (int)quality) != 0) {
        encoder->last_error = ldacBT_get_error_code(encoder->handle);
        return LDAC_ENCODER_CONFIGURATION_FAILED;
    }
    encoder->quality = quality;
    return LDAC_ENCODER_OK;
}

ldac_encoder_status ldac_encoder_encode_f32(
    ldac_encoder *encoder,
    const float *interleaved_stereo_pcm,
    size_t pcm_frames,
    uint8_t *encoded,
    size_t encoded_capacity,
    size_t *encoded_size,
    uint8_t *transport_frame_count) {
    int pcmUsed = 0;
    int streamSize = 0;
    int frameCount = 0;
    int result;
    const float *pcm = interleaved_stereo_pcm;
    float mono[LDAC_ENCODER_PCM_FRAMES_PER_CALL];
    size_t index;
    int expected_pcm_bytes;

    if (encoded_size != NULL) *encoded_size = 0u;
    if (transport_frame_count != NULL) *transport_frame_count = 0u;
    if (encoder == NULL || encoder->handle == NULL ||
        interleaved_stereo_pcm == NULL ||
        pcm_frames != LDAC_ENCODER_PCM_FRAMES_PER_CALL ||
        encoded == NULL || encoded_size == NULL ||
        transport_frame_count == NULL) {
        return LDAC_ENCODER_INVALID_ARGUMENT;
    }
    if (encoded_capacity < LDAC_ENCODER_MAX_OUTPUT_BYTES) {
        return LDAC_ENCODER_NO_SPACE;
    }

    if (encoder->channel_mode == LDAC_ENCODER_CHANNEL_MONO) {
        for (index = 0u; index < LDAC_ENCODER_PCM_FRAMES_PER_CALL; ++index) {
            mono[index] =
                (interleaved_stereo_pcm[index * 2u] +
                 interleaved_stereo_pcm[index * 2u + 1u]) * 0.5f;
        }
        pcm = mono;
    }
    expected_pcm_bytes = (int)(LDAC_ENCODER_PCM_FRAMES_PER_CALL *
        (encoder->channel_mode == LDAC_ENCODER_CHANNEL_MONO ? 1u : 2u) *
        sizeof(float));
    result = ldacBT_encode(encoder->handle,
                           (void *)pcm,
                           &pcmUsed,
                           encoded,
                           &streamSize,
                           &frameCount);
    if (result != 0) {
        encoder->last_error = ldacBT_get_error_code(encoder->handle);
        return LDAC_ENCODER_ENCODING_FAILED;
    }
    if (pcmUsed != expected_pcm_bytes ||
        streamSize < 0 || (size_t)streamSize > encoded_capacity ||
        (size_t)streamSize > encoder->encoded_payload_mtu ||
        frameCount < 0 || frameCount > 31 ||
        ((streamSize == 0) != (frameCount == 0))) {
        return LDAC_ENCODER_INVALID_OUTPUT;
    }
    *encoded_size = (size_t)streamSize;
    *transport_frame_count = (uint8_t)frameCount;
    return LDAC_ENCODER_OK;
}

unsigned ldac_encoder_sample_rate_hz(const ldac_encoder *encoder) {
    return encoder != NULL ? encoder->sample_rate_hz : 0u;
}

unsigned ldac_encoder_samples_per_transport_frame(
    const ldac_encoder *encoder) {
    if (encoder == NULL) return 0u;
    return encoder->sample_rate_hz >= 88200u ? 256u : 128u;
}

unsigned ldac_encoder_nominal_bitrate_kbps(const ldac_encoder *encoder) {
    int x441_family;
    unsigned bitrate;
    if (encoder == NULL) return 0u;
    x441_family = encoder->sample_rate_hz == 44100u ||
                  encoder->sample_rate_hz == 88200u;
    switch (encoder->quality) {
        case LDAC_ENCODER_QUALITY_HQ: bitrate = x441_family ? 909u : 990u; break;
        case LDAC_ENCODER_QUALITY_SQ: bitrate = x441_family ? 606u : 660u; break;
        case LDAC_ENCODER_QUALITY_MQ: bitrate = x441_family ? 303u : 330u; break;
        default: return 0u;
    }
    return encoder->channel_mode == LDAC_ENCODER_CHANNEL_MONO
        ? bitrate / 2u
        : bitrate;
}

ldac_encoder_quality ldac_encoder_quality_mode(const ldac_encoder *encoder) {
    return encoder != NULL ? encoder->quality : LDAC_ENCODER_QUALITY_MQ;
}

ldac_encoder_channel_mode ldac_encoder_channel_mode_value(
    const ldac_encoder *encoder) {
    return encoder != NULL
        ? encoder->channel_mode
        : LDAC_ENCODER_CHANNEL_STEREO;
}

int ldac_encoder_last_error(const ldac_encoder *encoder) {
    return encoder != NULL ? encoder->last_error : 0;
}
