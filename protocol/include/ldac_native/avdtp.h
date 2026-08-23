// SPDX-License-Identifier: Apache-2.0
#ifndef LDAC_NATIVE_AVDTP_H
#define LDAC_NATIVE_AVDTP_H

#ifdef NATIVE_LDAC_KERNEL_MODE
#include <ntddk.h>
#ifndef NATIVE_LDAC_UINT8_DEFINED
typedef unsigned char uint8_t;
#define NATIVE_LDAC_UINT8_DEFINED 1
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#include "ldac_native/ldac_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AVDTP_SIGNAL_DISCOVER 0x01u
#define AVDTP_SIGNAL_GET_CAPABILITIES 0x02u
#define AVDTP_SIGNAL_SET_CONFIGURATION 0x03u
#define AVDTP_SIGNAL_OPEN 0x06u
#define AVDTP_SIGNAL_START 0x07u
#define AVDTP_SIGNAL_CLOSE 0x08u
#define AVDTP_SIGNAL_SUSPEND 0x09u
#define AVDTP_SIGNAL_ABORT 0x0Au
#define AVDTP_SIGNAL_GET_ALL_CAPABILITIES 0x0Cu

#define AVDTP_MAX_SIGNALING_PACKET 64u
#define AVDTP_MAX_REMOTE_SEIDS 63u

typedef enum avdtp_packet_type {
    AVDTP_PACKET_SINGLE = 0,
    AVDTP_PACKET_START = 1,
    AVDTP_PACKET_CONTINUE = 2,
    AVDTP_PACKET_END = 3
} avdtp_packet_type;

typedef enum avdtp_message_type {
    AVDTP_MESSAGE_COMMAND = 0,
    AVDTP_MESSAGE_GENERAL_REJECT = 1,
    AVDTP_MESSAGE_ACCEPT = 2,
    AVDTP_MESSAGE_REJECT = 3
} avdtp_message_type;

typedef struct avdtp_header {
    uint8_t transaction_label;
    avdtp_packet_type packet_type;
    avdtp_message_type message_type;
    uint8_t packet_count;
    uint8_t signal_id;
    size_t payload_offset;
} avdtp_header;

typedef enum avdtp_status {
    AVDTP_OK = 0,
    AVDTP_INVALID_ARGUMENT = -1,
    AVDTP_TRUNCATED = -2,
    AVDTP_UNSUPPORTED_FRAGMENT = -3,
    AVDTP_PROTOCOL_ERROR = -4,
    AVDTP_NO_SPACE = -5
} avdtp_status;

avdtp_status avdtp_parse_header(const uint8_t *packet,
                                size_t packet_size,
                                avdtp_header *out);

size_t avdtp_write_single(uint8_t *out,
                          size_t out_size,
                          uint8_t transaction_label,
                          avdtp_message_type message_type,
                          uint8_t signal_id,
                          const uint8_t *payload,
                          size_t payload_size);

typedef enum avdtp_source_state {
    AVDTP_SOURCE_IDLE = 0,
    AVDTP_SOURCE_DISCOVER_SENT,
    AVDTP_SOURCE_CAPABILITIES_SENT,
    AVDTP_SOURCE_CONFIGURATION_SENT,
    AVDTP_SOURCE_OPEN_SENT,
    AVDTP_SOURCE_WAITING_FOR_MEDIA_CHANNEL,
    AVDTP_SOURCE_OPEN,
    AVDTP_SOURCE_START_SENT,
    AVDTP_SOURCE_STREAMING,
    AVDTP_SOURCE_SUSPEND_SENT,
    AVDTP_SOURCE_CLOSE_SENT,
    AVDTP_SOURCE_CLOSED,
    AVDTP_SOURCE_FAILED
} avdtp_source_state;

typedef enum avdtp_action_kind {
    AVDTP_ACTION_NONE = 0,
    AVDTP_ACTION_SEND_SIGNALING,
    AVDTP_ACTION_OPEN_MEDIA_CHANNEL,
    AVDTP_ACTION_SESSION_OPEN,
    AVDTP_ACTION_STREAM_READY,
    AVDTP_ACTION_STREAM_SUSPENDED,
    AVDTP_ACTION_SESSION_CLOSED,
    AVDTP_ACTION_ERROR
} avdtp_action_kind;

typedef struct avdtp_action {
    avdtp_action_kind kind;
    uint8_t packet[AVDTP_MAX_SIGNALING_PACKET];
    size_t packet_size;
    int error_code;
} avdtp_action;

typedef struct avdtp_source {
    avdtp_source_state state;
    ldac_capabilities local_capabilities;
    ldac_capabilities remote_capabilities;
    ldac_configuration configuration;
    unsigned preferred_sample_rate_hz;
    uint8_t local_seid;
    uint8_t remote_seid;
    uint8_t remote_seids[AVDTP_MAX_REMOTE_SEIDS];
    uint8_t remote_seid_count;
    uint8_t remote_seid_index;
    uint8_t next_transaction_label;
    uint8_t pending_transaction_label;
    uint8_t pending_signal_id;
    uint8_t used_legacy_capabilities;
} avdtp_source;

enum {
    AVDTP_SOURCE_ERROR_BAD_PACKET = 0x100,
    AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE,
    AVDTP_SOURCE_ERROR_NO_AUDIO_SINK,
    AVDTP_SOURCE_ERROR_LDAC_NOT_SUPPORTED,
    AVDTP_SOURCE_ERROR_NO_COMMON_CONFIGURATION,
    AVDTP_SOURCE_ERROR_MEDIA_CHANNEL_STATE
};

void avdtp_source_init(avdtp_source *source,
                       ldac_capabilities local_capabilities,
                       uint8_t local_seid,
                       unsigned preferred_sample_rate_hz);

avdtp_action avdtp_source_begin(avdtp_source *source);

avdtp_action avdtp_source_handle_signaling(avdtp_source *source,
                                           const uint8_t *packet,
                                           size_t packet_size);

avdtp_action avdtp_source_media_channel_opened(avdtp_source *source);

avdtp_action avdtp_source_start(avdtp_source *source);

avdtp_action avdtp_source_suspend(avdtp_source *source);

avdtp_action avdtp_source_close(avdtp_source *source);

#ifdef __cplusplus
}
#endif

#endif
