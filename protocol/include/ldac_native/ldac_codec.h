// SPDX-License-Identifier: Apache-2.0
#ifndef LDAC_NATIVE_LDAC_CODEC_H
#define LDAC_NATIVE_LDAC_CODEC_H

#ifdef NATIVE_LDAC_KERNEL_MODE
#include <ntddk.h>
#ifndef NATIVE_LDAC_UINT8_DEFINED
typedef unsigned char uint8_t;
#define NATIVE_LDAC_UINT8_DEFINED 1
#endif
typedef unsigned short uint16_t;
typedef unsigned long uint32_t;
#else
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LDAC_VENDOR_ID 0x0000012Du
#define LDAC_CODEC_ID 0x00AAu
#define LDAC_CODEC_INFO_SIZE 8u

#define LDAC_SF_44100 0x20u
#define LDAC_SF_48000 0x10u
#define LDAC_SF_88200 0x08u
#define LDAC_SF_96000 0x04u
#define LDAC_SF_ALL (LDAC_SF_44100 | LDAC_SF_48000 | LDAC_SF_88200 | LDAC_SF_96000)

#define LDAC_CM_STEREO 0x01u
#define LDAC_CM_DUAL 0x02u
#define LDAC_CM_MONO 0x04u
#define LDAC_CM_ALL (LDAC_CM_STEREO | LDAC_CM_DUAL | LDAC_CM_MONO)

#define AVDTP_SERVICE_MEDIA_TRANSPORT 0x01u
#define AVDTP_SERVICE_MEDIA_CODEC 0x07u
#define AVDTP_MEDIA_TYPE_AUDIO 0x00u
#define AVDTP_CODEC_VENDOR 0xFFu

typedef struct ldac_capabilities {
    uint8_t sample_rates;
    uint8_t channel_modes;
} ldac_capabilities;

typedef struct ldac_configuration {
    uint8_t sample_rate;
    uint8_t channel_mode;
} ldac_configuration;

typedef enum ldac_codec_status {
    LDAC_CODEC_OK = 0,
    LDAC_CODEC_INVALID_ARGUMENT = -1,
    LDAC_CODEC_TRUNCATED = -2,
    LDAC_CODEC_NOT_FOUND = -3,
    LDAC_CODEC_UNSUPPORTED = -4,
    LDAC_CODEC_NO_SPACE = -5
} ldac_codec_status;

void ldac_build_codec_info(uint8_t out[LDAC_CODEC_INFO_SIZE],
                           uint8_t sample_rates,
                           uint8_t channel_modes);

ldac_codec_status ldac_parse_codec_info(const uint8_t *info,
                                        size_t info_size,
                                        ldac_capabilities *out);

ldac_codec_status ldac_find_in_service_capabilities(const uint8_t *capabilities,
                                                     size_t capabilities_size,
                                                     ldac_capabilities *out);

ldac_codec_status ldac_choose_configuration(ldac_capabilities local,
                                            ldac_capabilities remote,
                                            unsigned preferred_sample_rate_hz,
                                            ldac_configuration *out);

size_t ldac_build_set_configuration_payload(uint8_t *out,
                                            size_t out_size,
                                            uint8_t remote_seid,
                                            uint8_t local_seid,
                                            ldac_configuration configuration);

unsigned ldac_sample_rate_to_hz(uint8_t sample_rate_bit);
unsigned ldac_samples_per_frame(uint8_t sample_rate_bit);

#ifdef __cplusplus
}
#endif

#endif
