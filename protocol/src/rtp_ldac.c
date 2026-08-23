// SPDX-License-Identifier: Apache-2.0
#include "ldac_native/rtp_ldac.h"

#include <string.h>

static void write_u16_be(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)(value >> 8u);
    out[1] = (uint8_t)value;
}

static void write_u32_be(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value >> 24u);
    out[1] = (uint8_t)(value >> 16u);
    out[2] = (uint8_t)(value >> 8u);
    out[3] = (uint8_t)value;
}

ldac_rtp_status ldac_rtp_build_unfragmented(uint8_t *out,
                                            size_t out_size,
                                            size_t l2cap_mtu,
                                            uint16_t sequence_number,
                                            uint32_t timestamp,
                                            uint32_t ssrc,
                                            uint8_t frame_count,
                                            const uint8_t *encoded_frames,
                                            size_t encoded_size,
                                            size_t *packet_size) {
    size_t required;
    if (packet_size != NULL) *packet_size = 0u;
    if (out == NULL || packet_size == NULL || encoded_frames == NULL ||
        encoded_size == 0u || frame_count == 0u ||
        frame_count > LDAC_RTP_MAX_FRAME_COUNT) {
        return LDAC_RTP_INVALID_ARGUMENT;
    }
    if (encoded_size > (size_t)-1 - LDAC_RTP_OVERHEAD) {
        return LDAC_RTP_INVALID_ARGUMENT;
    }
    required = LDAC_RTP_OVERHEAD + encoded_size;
    if (required > l2cap_mtu) return LDAC_RTP_MTU_EXCEEDED;
    if (required > out_size) return LDAC_RTP_NO_SPACE;

    out[0] = 0x80u;  /* RTP v2, no padding/extension/CSRC. */
    out[1] = LDAC_RTP_DEFAULT_PAYLOAD_TYPE;
    write_u16_be(out + 2u, sequence_number);
    write_u32_be(out + 4u, timestamp);
    write_u32_be(out + 8u, ssrc);
    out[12] = frame_count;  /* F/S/L are all zero for an unfragmented packet. */
    memcpy(out + LDAC_RTP_OVERHEAD, encoded_frames, encoded_size);
    *packet_size = required;
    return LDAC_RTP_OK;
}
