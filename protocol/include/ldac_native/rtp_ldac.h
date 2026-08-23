// SPDX-License-Identifier: Apache-2.0
#ifndef LDAC_NATIVE_RTP_LDAC_H
#define LDAC_NATIVE_RTP_LDAC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTP_FIXED_HEADER_SIZE 12u
#define LDAC_MEDIA_HEADER_SIZE 1u
#define LDAC_RTP_OVERHEAD (RTP_FIXED_HEADER_SIZE + LDAC_MEDIA_HEADER_SIZE)
#define LDAC_RTP_DEFAULT_PAYLOAD_TYPE 96u
#define LDAC_RTP_MAX_FRAME_COUNT 31u

typedef enum ldac_rtp_status {
    LDAC_RTP_OK = 0,
    LDAC_RTP_INVALID_ARGUMENT = -1,
    LDAC_RTP_NO_SPACE = -2,
    LDAC_RTP_MTU_EXCEEDED = -3
} ldac_rtp_status;

ldac_rtp_status ldac_rtp_build_unfragmented(uint8_t *out,
                                            size_t out_size,
                                            size_t l2cap_mtu,
                                            uint16_t sequence_number,
                                            uint32_t timestamp,
                                            uint32_t ssrc,
                                            uint8_t frame_count,
                                            const uint8_t *encoded_frames,
                                            size_t encoded_size,
                                            size_t *packet_size);

#ifdef __cplusplus
}
#endif

#endif
