// SPDX-License-Identifier: Apache-2.0
#ifndef LDAC_NATIVE_AVRCP_H
#define LDAC_NATIVE_AVRCP_H

#ifdef NATIVE_LDAC_KERNEL_MODE
#include <ntddk.h>
#ifndef NATIVE_LDAC_UINT8_DEFINED
typedef unsigned char uint8_t;
#define NATIVE_LDAC_UINT8_DEFINED 1
#endif
#ifndef NATIVE_LDAC_UINT16_DEFINED
typedef unsigned short uint16_t;
#define NATIVE_LDAC_UINT16_DEFINED 1
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define AVRCP_PROFILE_ID 0x110Eu
#define AVRCP_COMPANY_ID_BLUETOOTH_SIG 0x001958u

#define AVRCP_OPCODE_VENDOR_DEPENDENT 0x00u
#define AVRCP_OPCODE_PASS_THROUGH 0x7Cu

#define AVRCP_CTYPE_CONTROL 0x00u
#define AVRCP_CTYPE_STATUS 0x01u
#define AVRCP_CTYPE_NOTIFY 0x03u
#define AVRCP_RESPONSE_NOT_IMPLEMENTED 0x08u
#define AVRCP_RESPONSE_ACCEPTED 0x09u
#define AVRCP_RESPONSE_REJECTED 0x0Au
#define AVRCP_RESPONSE_STABLE 0x0Cu
#define AVRCP_RESPONSE_CHANGED 0x0Du
#define AVRCP_RESPONSE_INTERIM 0x0Fu

#define AVRCP_PDU_GET_CAPABILITIES 0x10u
#define AVRCP_PDU_GET_PLAY_STATUS 0x20u
#define AVRCP_PDU_REGISTER_NOTIFICATION 0x31u
#define AVRCP_PDU_SET_ABSOLUTE_VOLUME 0x50u

#define AVRCP_CAPABILITY_EVENTS_SUPPORTED 0x03u
#define AVRCP_EVENT_PLAYBACK_STATUS_CHANGED 0x01u
#define AVRCP_EVENT_TRACK_CHANGED 0x02u
#define AVRCP_EVENT_VOLUME_CHANGED 0x0Du

#define AVRCP_PLAYBACK_STATUS_STOPPED 0x00u
#define AVRCP_PLAYBACK_STATUS_PLAYING 0x01u
#define AVRCP_PLAYBACK_STATUS_PAUSED 0x02u

#define AVRCP_OPERATION_VOLUME_UP 0x41u
#define AVRCP_OPERATION_VOLUME_DOWN 0x42u
#define AVRCP_OPERATION_MUTE 0x43u
#define AVRCP_OPERATION_PLAY 0x44u
#define AVRCP_OPERATION_STOP 0x45u
#define AVRCP_OPERATION_PAUSE 0x46u
#define AVRCP_OPERATION_FORWARD 0x4Bu
#define AVRCP_OPERATION_BACKWARD 0x4Cu

#define AVRCP_MAX_CONTROL_PACKET 64u

typedef enum avrcp_status {
    AVRCP_OK = 0,
    AVRCP_INVALID_ARGUMENT = -1,
    AVRCP_TRUNCATED = -2,
    AVRCP_UNSUPPORTED_FRAGMENT = -3,
    AVRCP_PROTOCOL_ERROR = -4,
    AVRCP_NO_SPACE = -5,
    AVRCP_UNEXPECTED_RESPONSE = -6
} avrcp_status;

typedef enum avctp_packet_type {
    AVCTP_PACKET_SINGLE = 0,
    AVCTP_PACKET_START = 1,
    AVCTP_PACKET_CONTINUE = 2,
    AVCTP_PACKET_END = 3
} avctp_packet_type;

typedef struct avrcp_frame {
    uint8_t transaction_label;
    avctp_packet_type packet_type;
    uint8_t command_response;
    uint8_t invalid_profile_id;
    unsigned profile_id;
    uint8_t ctype_or_response;
    uint8_t subunit_type;
    uint8_t subunit_id;
    uint8_t opcode;
    size_t operands_offset;
    size_t operands_size;
} avrcp_frame;

typedef struct avrcp_vendor_frame {
    avrcp_frame frame;
    unsigned company_id;
    uint8_t pdu_id;
    uint8_t pdu_packet_type;
    size_t parameters_offset;
    size_t parameters_size;
} avrcp_vendor_frame;

typedef struct avrcp_pass_through {
    avrcp_frame frame;
    uint8_t operation_id;
    uint8_t released;
    size_t operation_data_offset;
    size_t operation_data_size;
} avrcp_pass_through;

avrcp_status avrcp_parse_frame(const uint8_t *packet,
                               size_t packet_size,
                               avrcp_frame *out);

avrcp_status avrcp_parse_vendor_frame(const uint8_t *packet,
                                      size_t packet_size,
                                      avrcp_vendor_frame *out);

avrcp_status avrcp_parse_pass_through(const uint8_t *packet,
                                      size_t packet_size,
                                      avrcp_pass_through *out);

size_t avrcp_write_get_capabilities_events(uint8_t *out,
                                           size_t out_size,
                                           uint8_t transaction_label);

size_t avrcp_write_register_notification(uint8_t *out,
                                         size_t out_size,
                                         uint8_t transaction_label,
                                         uint8_t event_id,
                                         unsigned playback_interval_seconds);

size_t avrcp_write_pass_through_response(uint8_t *out,
                                         size_t out_size,
                                         const uint8_t *command,
                                         size_t command_size,
                                         uint8_t response_code);

typedef enum avrcp_observer_state {
    AVRCP_OBSERVER_IDLE = 0,
    AVRCP_OBSERVER_WAIT_CAPABILITIES,
    AVRCP_OBSERVER_WAIT_VOLUME_INTERIM,
    AVRCP_OBSERVER_OBSERVING,
    AVRCP_OBSERVER_UNSUPPORTED,
    AVRCP_OBSERVER_FAILED
} avrcp_observer_state;

typedef enum avrcp_observer_event_kind {
    AVRCP_OBSERVER_EVENT_NONE = 0,
    AVRCP_OBSERVER_EVENT_VOLUME_CAPABILITY,
    AVRCP_OBSERVER_EVENT_VOLUME_CHANGED,
    AVRCP_OBSERVER_EVENT_PASS_THROUGH,
    AVRCP_OBSERVER_EVENT_VENDOR_COMMAND,
    AVRCP_OBSERVER_EVENT_WRITE_RESPONSE,
    AVRCP_OBSERVER_EVENT_PROTOCOL_ERROR
} avrcp_observer_event_kind;

typedef struct avrcp_observer_event {
    avrcp_observer_event_kind kind;
    uint8_t volume_supported;
    uint8_t absolute_volume;
    uint8_t operation_id;
    uint8_t released;
    uint8_t response_code;
    uint8_t pdu_id;
    uint8_t parameter_size;
    uint8_t parameter_bytes[8];
    uint8_t error_stage;
    int error_code;
    uint8_t raw_prefix[64];
    uint8_t raw_prefix_size;
    uint16_t raw_total_size;
} avrcp_observer_event;

typedef struct avrcp_observer_result {
    uint8_t packet[AVRCP_MAX_CONTROL_PACKET];
    size_t packet_size;
    avrcp_observer_event event;
} avrcp_observer_result;

typedef struct avrcp_observer {
    avrcp_observer_state state;
    uint8_t next_transaction_label;
    uint8_t pending_transaction_label;
    uint8_t pending_pdu_id;
    uint8_t write_label;
    uint8_t write_pdu_id;
    uint8_t write_active;
    uint8_t volume_supported;
    uint8_t last_absolute_volume;
    uint8_t last_absolute_volume_valid;
    uint8_t playback_status;
    uint8_t playback_status_valid;
    uint8_t playback_notification_label;
    uint8_t playback_notification_active;
    uint8_t playback_notification_pending;
} avrcp_observer;

void avrcp_observer_init(avrcp_observer *observer);

avrcp_observer_result avrcp_observer_begin(avrcp_observer *observer);

avrcp_observer_result avrcp_observer_handle_packet(
    avrcp_observer *observer,
    const uint8_t *packet,
    size_t packet_size);

/* Submits one outbound AV/C vendor-dependent frame (command or response).
 * Ordinary commands and responses use the observer's next transaction label.
 * A playback-status CHANGED response (PDU 0x31, response 0x0D, event 0x01)
 * uses the transaction label saved from XM5's REGISTER_NOTIFICATION command.
 * The frame is returned in result.packet; ordinary command responses are
 * later emitted as WRITE_RESPONSE events. Returns packet_size == 0 when the
 * write is queued as state-only, a write is already outstanding, or the
 * observer is not in a writable state. */
avrcp_observer_result avrcp_observer_submit_write(
    avrcp_observer *observer,
    uint8_t pdu_id,
    uint8_t response_code,
    const uint8_t *parameters,
    size_t parameter_size);

/* Updates the PC-authoritative playback status. If XM5 has an active
 * playback-status registration, builds the matching CHANGED response using
 * XM5's original transaction label. */
avrcp_observer_result avrcp_observer_submit_playback_status(
    avrcp_observer *observer,
    uint8_t playback_status);

/* Builds SET_ABSOLUTE_VOLUME (PDU 0x50) with the 0..127 volume value. */
size_t avrcp_write_set_absolute_volume(uint8_t *out,
                                       size_t out_size,
                                       uint8_t transaction_label,
                                       uint8_t volume);

/* Builds a RegisterNotification CHANGED response (PDU 0x31) for one event. */
size_t avrcp_write_notification_changed(uint8_t *out,
                                        size_t out_size,
                                        uint8_t transaction_label,
                                        uint8_t event_id,
                                        const uint8_t *payload,
                                        size_t payload_size);

#ifdef __cplusplus
}
#endif

#endif
