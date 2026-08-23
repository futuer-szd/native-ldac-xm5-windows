// SPDX-License-Identifier: Apache-2.0
#include "ldac_native/ldac_codec.h"

#include <string.h>

static uint8_t choose_sample_rate(uint8_t rates, unsigned preferred_hz) {
    uint8_t preferred = 0;
    switch (preferred_hz) {
        case 44100u: preferred = LDAC_SF_44100; break;
        case 48000u: preferred = LDAC_SF_48000; break;
        case 88200u: preferred = LDAC_SF_88200; break;
        case 96000u: preferred = LDAC_SF_96000; break;
        default: break;
    }
    if (preferred != 0u && (rates & preferred) != 0u) {
        return preferred;
    }
    if ((rates & LDAC_SF_96000) != 0u) return LDAC_SF_96000;
    if ((rates & LDAC_SF_88200) != 0u) return LDAC_SF_88200;
    if ((rates & LDAC_SF_48000) != 0u) return LDAC_SF_48000;
    if ((rates & LDAC_SF_44100) != 0u) return LDAC_SF_44100;
    return 0u;
}

static uint8_t choose_channel_mode(uint8_t modes) {
    if ((modes & LDAC_CM_STEREO) != 0u) return LDAC_CM_STEREO;
    if ((modes & LDAC_CM_DUAL) != 0u) return LDAC_CM_DUAL;
    if ((modes & LDAC_CM_MONO) != 0u) return LDAC_CM_MONO;
    return 0u;
}

void ldac_build_codec_info(uint8_t out[LDAC_CODEC_INFO_SIZE],
                           uint8_t sample_rates,
                           uint8_t channel_modes) {
    out[0] = 0x2Du;
    out[1] = 0x01u;
    out[2] = 0x00u;
    out[3] = 0x00u;
    out[4] = 0xAAu;
    out[5] = 0x00u;
    out[6] = sample_rates;
    out[7] = channel_modes;
}

ldac_codec_status ldac_parse_codec_info(const uint8_t *info,
                                        size_t info_size,
                                        ldac_capabilities *out) {
    uint32_t vendor_id;
    uint16_t codec_id;
    if (info == NULL || out == NULL) return LDAC_CODEC_INVALID_ARGUMENT;
    if (info_size < LDAC_CODEC_INFO_SIZE) return LDAC_CODEC_TRUNCATED;

    vendor_id = (uint32_t)info[0] |
                ((uint32_t)info[1] << 8u) |
                ((uint32_t)info[2] << 16u) |
                ((uint32_t)info[3] << 24u);
    codec_id = (uint16_t)((uint16_t)info[4] | ((uint16_t)info[5] << 8u));
    if (vendor_id != LDAC_VENDOR_ID || codec_id != LDAC_CODEC_ID) {
        return LDAC_CODEC_NOT_FOUND;
    }

    out->sample_rates = (uint8_t)(info[6] & LDAC_SF_ALL);
    out->channel_modes = (uint8_t)(info[7] & LDAC_CM_ALL);
    if (out->sample_rates == 0u || out->channel_modes == 0u) {
        return LDAC_CODEC_UNSUPPORTED;
    }
    return LDAC_CODEC_OK;
}

ldac_codec_status ldac_find_in_service_capabilities(const uint8_t *capabilities,
                                                     size_t capabilities_size,
                                                     ldac_capabilities *out) {
    size_t offset = 0u;
    if (capabilities == NULL || out == NULL) return LDAC_CODEC_INVALID_ARGUMENT;

    while (offset < capabilities_size) {
        uint8_t category;
        size_t item_size;
        const uint8_t *item;
        if (capabilities_size - offset < 2u) return LDAC_CODEC_TRUNCATED;
        category = capabilities[offset];
        item_size = capabilities[offset + 1u];
        offset += 2u;
        if (item_size > capabilities_size - offset) return LDAC_CODEC_TRUNCATED;
        item = capabilities + offset;

        if (category == AVDTP_SERVICE_MEDIA_CODEC && item_size >= 2u) {
            uint8_t media_type = (uint8_t)(item[0] >> 4u);
            uint8_t codec_type = item[1];
            if (media_type == AVDTP_MEDIA_TYPE_AUDIO &&
                codec_type == AVDTP_CODEC_VENDOR &&
                item_size >= 2u + LDAC_CODEC_INFO_SIZE) {
                ldac_codec_status status =
                    ldac_parse_codec_info(item + 2u, item_size - 2u, out);
                if (status == LDAC_CODEC_OK || status == LDAC_CODEC_UNSUPPORTED) {
                    return status;
                }
            }
        }
        offset += item_size;
    }
    return LDAC_CODEC_NOT_FOUND;
}

ldac_codec_status ldac_choose_configuration(ldac_capabilities local,
                                            ldac_capabilities remote,
                                            unsigned preferred_sample_rate_hz,
                                            ldac_configuration *out) {
    uint8_t rates;
    uint8_t modes;
    if (out == NULL) return LDAC_CODEC_INVALID_ARGUMENT;
    rates = (uint8_t)(local.sample_rates & remote.sample_rates & LDAC_SF_ALL);
    modes = (uint8_t)(local.channel_modes & remote.channel_modes & LDAC_CM_ALL);
    out->sample_rate = choose_sample_rate(rates, preferred_sample_rate_hz);
    out->channel_mode = choose_channel_mode(modes);
    if (out->sample_rate == 0u || out->channel_mode == 0u) {
        memset(out, 0, sizeof(*out));
        return LDAC_CODEC_UNSUPPORTED;
    }
    return LDAC_CODEC_OK;
}

size_t ldac_build_set_configuration_payload(uint8_t *out,
                                            size_t out_size,
                                            uint8_t remote_seid,
                                            uint8_t local_seid,
                                            ldac_configuration configuration) {
    uint8_t codec_info[LDAC_CODEC_INFO_SIZE];
    const size_t required = 16u;
    if (out == NULL || out_size < required ||
        remote_seid == 0u || remote_seid > 0x3Fu ||
        local_seid == 0u || local_seid > 0x3Fu ||
        configuration.sample_rate == 0u ||
        (configuration.sample_rate & LDAC_SF_ALL) != configuration.sample_rate ||
        (configuration.sample_rate & (uint8_t)(configuration.sample_rate - 1u)) != 0u ||
        configuration.channel_mode == 0u ||
        (configuration.channel_mode & LDAC_CM_ALL) != configuration.channel_mode ||
        (configuration.channel_mode & (uint8_t)(configuration.channel_mode - 1u)) != 0u) {
        return 0u;
    }

    ldac_build_codec_info(codec_info,
                          configuration.sample_rate,
                          configuration.channel_mode);
    out[0] = (uint8_t)(remote_seid << 2u);
    out[1] = (uint8_t)(local_seid << 2u);
    out[2] = AVDTP_SERVICE_MEDIA_TRANSPORT;
    out[3] = 0u;
    out[4] = AVDTP_SERVICE_MEDIA_CODEC;
    out[5] = (uint8_t)(2u + LDAC_CODEC_INFO_SIZE);
    out[6] = (uint8_t)(AVDTP_MEDIA_TYPE_AUDIO << 4u);
    out[7] = AVDTP_CODEC_VENDOR;
    memcpy(out + 8u, codec_info, sizeof(codec_info));
    return required;
}

unsigned ldac_sample_rate_to_hz(uint8_t sample_rate_bit) {
    switch (sample_rate_bit) {
        case LDAC_SF_44100: return 44100u;
        case LDAC_SF_48000: return 48000u;
        case LDAC_SF_88200: return 88200u;
        case LDAC_SF_96000: return 96000u;
        default: return 0u;
    }
}

unsigned ldac_samples_per_frame(uint8_t sample_rate_bit) {
    switch (sample_rate_bit) {
        case LDAC_SF_44100:
        case LDAC_SF_48000:
            return 128u;
        case LDAC_SF_88200:
        case LDAC_SF_96000:
            return 256u;
        default:
            return 0u;
    }
}
