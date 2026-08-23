// SPDX-License-Identifier: Apache-2.0
#include "ldac_native/avrcp.h"

#include <string.h>

#define AVRCP_AVCTP_HEADER_SIZE 3u
#define AVRCP_AVC_HEADER_SIZE 3u
#define AVRCP_VENDOR_HEADER_SIZE 7u
#define AVRCP_PANEL_SUBUNIT_TYPE 0x09u
#define AVRCP_PANEL_SUBUNIT_ID 0x00u
#define AVRCP_PASS_THROUGH_RELEASED_MASK 0x80u
#define AVRCP_PASS_THROUGH_OPERATION_MASK 0x7Fu

static avrcp_observer_result avrcp_make_result(void) {
    avrcp_observer_result result;
    memset(&result, 0, sizeof(result));
    return result;
}

static uint8_t avrcp_next_label(avrcp_observer *observer) {
    uint8_t label = observer->next_transaction_label;
    observer->next_transaction_label = (uint8_t)((label + 1u) & 0x0Fu);
    return label;
}

static int avrcp_is_valid_playback_status(uint8_t status) {
    return status == AVRCP_PLAYBACK_STATUS_STOPPED ||
        status == AVRCP_PLAYBACK_STATUS_PLAYING ||
        status == AVRCP_PLAYBACK_STATUS_PAUSED;
}

static uint8_t avrcp_current_playback_status(
    const avrcp_observer *observer) {
    if (observer != NULL && observer->playback_status_valid &&
        avrcp_is_valid_playback_status(observer->playback_status)) {
        return observer->playback_status;
    }
    return AVRCP_PLAYBACK_STATUS_STOPPED;
}

static size_t avrcp_write_vendor_frame(uint8_t *out,
                                       size_t out_size,
                                       uint8_t header0,
                                       uint8_t ctype,
                                       uint8_t pdu_id,
                                       const uint8_t *parameters,
                                       size_t parameters_size) {
    size_t required = 13u + parameters_size;
    if (out == NULL || (header0 & 0x0Du) != 0u ||
        parameters_size > 0xFFFFu ||
        (parameters_size != 0u && parameters == NULL) ||
        out_size < required) {
        return 0u;
    }
    out[0] = header0;
    out[1] = (uint8_t)(AVRCP_PROFILE_ID >> 8u);
    out[2] = (uint8_t)(AVRCP_PROFILE_ID & 0xFFu);
    out[3] = ctype;
    out[4] = (uint8_t)((AVRCP_PANEL_SUBUNIT_TYPE << 3u) |
                       AVRCP_PANEL_SUBUNIT_ID);
    out[5] = AVRCP_OPCODE_VENDOR_DEPENDENT;
    out[6] = (uint8_t)(AVRCP_COMPANY_ID_BLUETOOTH_SIG >> 16u);
    out[7] = (uint8_t)(AVRCP_COMPANY_ID_BLUETOOTH_SIG >> 8u);
    out[8] = (uint8_t)(AVRCP_COMPANY_ID_BLUETOOTH_SIG & 0xFFu);
    out[9] = pdu_id;
    out[10] = 0u;
    out[11] = (uint8_t)(parameters_size >> 8u);
    out[12] = (uint8_t)parameters_size;
    if (parameters_size != 0u) {
        memcpy(out + 13u, parameters, parameters_size);
    }
    return required;
}

static size_t avrcp_write_vendor_command(uint8_t *out,
                                         size_t out_size,
                                         uint8_t transaction_label,
                                         uint8_t ctype,
                                         uint8_t pdu_id,
                                         const uint8_t *parameters,
                                         size_t parameters_size) {
    if (transaction_label > 0x0Fu) return 0u;
    return avrcp_write_vendor_frame(out,
                                    out_size,
                                    (uint8_t)(transaction_label << 4u),
                                    ctype,
                                    pdu_id,
                                    parameters,
                                    parameters_size);
}

static size_t avrcp_write_vendor_response(uint8_t *out,
                                          size_t out_size,
                                          uint8_t transaction_label,
                                          uint8_t response,
                                          uint8_t pdu_id,
                                          const uint8_t *parameters,
                                          size_t parameters_size) {
    if (transaction_label > 0x0Fu) return 0u;
    return avrcp_write_vendor_frame(out,
                                    out_size,
                                    (uint8_t)((transaction_label << 4u) |
                                              0x02u),
                                    response,
                                    pdu_id,
                                    parameters,
                                    parameters_size);
}

avrcp_status avrcp_parse_frame(const uint8_t *packet,
                               size_t packet_size,
                               avrcp_frame *out) {
    uint8_t first;
    uint8_t subunit;
    if (packet == NULL || out == NULL) return AVRCP_INVALID_ARGUMENT;
    if (packet_size < AVRCP_AVCTP_HEADER_SIZE + AVRCP_AVC_HEADER_SIZE) {
        return AVRCP_TRUNCATED;
    }
    memset(out, 0, sizeof(*out));
    first = packet[0];
    out->transaction_label = (uint8_t)(first >> 4u);
    out->packet_type = (avctp_packet_type)((first >> 2u) & 0x03u);
    out->command_response = (uint8_t)((first >> 1u) & 0x01u);
    out->invalid_profile_id = (uint8_t)(first & 0x01u);
    out->profile_id = ((unsigned)packet[1] << 8u) | packet[2];
    if (out->packet_type != AVCTP_PACKET_SINGLE) {
        return AVRCP_UNSUPPORTED_FRAGMENT;
    }
    if (out->invalid_profile_id != 0u ||
        out->profile_id != AVRCP_PROFILE_ID ||
        (packet[3] & 0xF0u) != 0u) {
        return AVRCP_PROTOCOL_ERROR;
    }
    out->ctype_or_response = (uint8_t)(packet[3] & 0x0Fu);
    subunit = packet[4];
    out->subunit_type = (uint8_t)(subunit >> 3u);
    out->subunit_id = (uint8_t)(subunit & 0x07u);
    out->opcode = packet[5];
    out->operands_offset = 6u;
    out->operands_size = packet_size - 6u;
    if (out->subunit_type != AVRCP_PANEL_SUBUNIT_TYPE ||
        out->subunit_id != AVRCP_PANEL_SUBUNIT_ID) {
        return AVRCP_PROTOCOL_ERROR;
    }
    return AVRCP_OK;
}

avrcp_status avrcp_parse_vendor_frame(const uint8_t *packet,
                                      size_t packet_size,
                                      avrcp_vendor_frame *out) {
    avrcp_status status;
    size_t parameters_size;
    if (out == NULL) return AVRCP_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    status = avrcp_parse_frame(packet, packet_size, &out->frame);
    if (status != AVRCP_OK) return status;
    if (out->frame.opcode != AVRCP_OPCODE_VENDOR_DEPENDENT) {
        return AVRCP_PROTOCOL_ERROR;
    }
    if (out->frame.operands_size < AVRCP_VENDOR_HEADER_SIZE) {
        return AVRCP_TRUNCATED;
    }
    out->company_id = ((unsigned)packet[6] << 16u) |
                      ((unsigned)packet[7] << 8u) | packet[8];
    out->pdu_id = packet[9];
    out->pdu_packet_type = (uint8_t)(packet[10] & 0x03u);
    parameters_size = ((size_t)packet[11] << 8u) | packet[12];
    out->parameters_offset = 13u;
    out->parameters_size = parameters_size;
    if (out->company_id != AVRCP_COMPANY_ID_BLUETOOTH_SIG ||
        (packet[10] & 0xFCu) != 0u || out->pdu_packet_type != 0u) {
        return AVRCP_PROTOCOL_ERROR;
    }
    if (parameters_size > packet_size - out->parameters_offset) {
        return AVRCP_TRUNCATED;
    }
    if (parameters_size < packet_size - out->parameters_offset) {
        size_t index;
        for (index = out->parameters_offset + parameters_size;
             index < packet_size; ++index) {
            if (packet[index] != 0u) return AVRCP_PROTOCOL_ERROR;
        }
    }
    return AVRCP_OK;
}

avrcp_status avrcp_parse_pass_through(const uint8_t *packet,
                                      size_t packet_size,
                                      avrcp_pass_through *out) {
    avrcp_status status;
    size_t operation_data_size;
    if (out == NULL) return AVRCP_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    status = avrcp_parse_frame(packet, packet_size, &out->frame);
    if (status != AVRCP_OK) return status;
    if (out->frame.opcode != AVRCP_OPCODE_PASS_THROUGH ||
        out->frame.operands_size < 2u) {
        return out->frame.operands_size < 2u
            ? AVRCP_TRUNCATED
            : AVRCP_PROTOCOL_ERROR;
    }
    out->operation_id = (uint8_t)(packet[6] &
                                   AVRCP_PASS_THROUGH_OPERATION_MASK);
    out->released = (uint8_t)((packet[6] &
                               AVRCP_PASS_THROUGH_RELEASED_MASK) != 0u);
    operation_data_size = packet[7];
    out->operation_data_offset = 8u;
    out->operation_data_size = operation_data_size;
    if (operation_data_size > packet_size - out->operation_data_offset) {
        return AVRCP_TRUNCATED;
    }
    if (operation_data_size < packet_size - out->operation_data_offset) {
        size_t index;
        for (index = out->operation_data_offset + operation_data_size;
             index < packet_size; ++index) {
            if (packet[index] != 0u) return AVRCP_PROTOCOL_ERROR;
        }
    }
    return AVRCP_OK;
}

size_t avrcp_write_get_capabilities_events(uint8_t *out,
                                           size_t out_size,
                                           uint8_t transaction_label) {
    const uint8_t parameters[] = {AVRCP_CAPABILITY_EVENTS_SUPPORTED};
    return avrcp_write_vendor_command(out,
                                      out_size,
                                      transaction_label,
                                      AVRCP_CTYPE_STATUS,
                                      AVRCP_PDU_GET_CAPABILITIES,
                                      parameters,
                                      sizeof(parameters));
}

size_t avrcp_write_set_absolute_volume(uint8_t *out,
                                       size_t out_size,
                                       uint8_t transaction_label,
                                       uint8_t volume) {
    if (volume > 0x7Fu) return 0u;
    return avrcp_write_vendor_command(out,
                                      out_size,
                                      transaction_label,
                                      AVRCP_CTYPE_CONTROL,
                                      AVRCP_PDU_SET_ABSOLUTE_VOLUME,
                                      &volume,
                                      1u);
}

size_t avrcp_write_notification_changed(uint8_t *out,
                                        size_t out_size,
                                        uint8_t transaction_label,
                                        uint8_t event_id,
                                        const uint8_t *payload,
                                        size_t payload_size) {
    uint8_t parameters[1u + 8u];
    if (payload_size > 8u ||
        (payload_size != 0u && payload == NULL)) {
        return 0u;
    }
    parameters[0] = event_id;
    if (payload_size != 0u) {
        memcpy(parameters + 1u, payload, payload_size);
    }
    return avrcp_write_vendor_response(out,
                                       out_size,
                                       transaction_label,
                                       AVRCP_RESPONSE_CHANGED,
                                       AVRCP_PDU_REGISTER_NOTIFICATION,
                                       parameters,
                                       payload_size + 1u);
}

size_t avrcp_write_register_notification(uint8_t *out,
                                         size_t out_size,
                                         uint8_t transaction_label,
                                         uint8_t event_id,
                                         unsigned playback_interval_seconds) {
    uint8_t parameters[5];
    parameters[0] = event_id;
    parameters[1] = (uint8_t)(playback_interval_seconds >> 24u);
    parameters[2] = (uint8_t)(playback_interval_seconds >> 16u);
    parameters[3] = (uint8_t)(playback_interval_seconds >> 8u);
    parameters[4] = (uint8_t)playback_interval_seconds;
    return avrcp_write_vendor_command(out,
                                      out_size,
                                      transaction_label,
                                      AVRCP_CTYPE_NOTIFY,
                                      AVRCP_PDU_REGISTER_NOTIFICATION,
                                      parameters,
                                      sizeof(parameters));
}

size_t avrcp_write_pass_through_response(uint8_t *out,
                                         size_t out_size,
                                         const uint8_t *command,
                                         size_t command_size,
                                         uint8_t response_code) {
    avrcp_pass_through parsed;
    size_t frame_size;
    if (response_code != AVRCP_RESPONSE_ACCEPTED &&
        response_code != AVRCP_RESPONSE_REJECTED &&
        response_code != AVRCP_RESPONSE_NOT_IMPLEMENTED) {
        return 0u;
    }
    if (out == NULL ||
        avrcp_parse_pass_through(command,
                                 command_size,
                                 &parsed) != AVRCP_OK ||
        parsed.frame.command_response != 0u) {
        return 0u;
    }
    frame_size = command_size < 8u ? command_size : 8u;
    if (out_size < frame_size) return 0u;
    memcpy(out, command, frame_size);
    out[0] = (uint8_t)(out[0] | 0x02u);
    out[3] = response_code;
    return frame_size;
}

void avrcp_observer_init(avrcp_observer *observer) {
    if (observer == NULL) return;
    memset(observer, 0, sizeof(*observer));
    observer->state = AVRCP_OBSERVER_IDLE;
}

static avrcp_observer_result avrcp_observer_fail_stage(
    avrcp_observer *observer,
    int error_code,
    uint8_t error_stage,
    const uint8_t *packet,
    size_t packet_size) {
    avrcp_observer_result result = avrcp_make_result();
    observer->state = AVRCP_OBSERVER_FAILED;
    result.event.kind = AVRCP_OBSERVER_EVENT_PROTOCOL_ERROR;
    result.event.error_stage = error_stage;
    result.event.error_code = error_code;
    result.event.raw_total_size = packet_size > 0xFFFFu
        ? 0xFFFFu
        : (uint16_t)packet_size;
    if (packet != NULL && packet_size != 0u) {
        size_t prefix = packet_size < 64u ? packet_size : 64u;
        size_t index;
        for (index = 0u; index < prefix; ++index) {
            result.event.raw_prefix[index] = packet[index];
        }
        result.event.raw_prefix_size = (uint8_t)prefix;
    }
    return result;
}

static avrcp_observer_result avrcp_observer_fail(
    avrcp_observer *observer,
    int error_code) {
    return avrcp_observer_fail_stage(observer, error_code, 0u, NULL, 0u);
}

static avrcp_observer_result avrcp_observer_register_volume(
    avrcp_observer *observer,
    avrcp_observer_event event) {
    avrcp_observer_result result = avrcp_make_result();
    uint8_t label = avrcp_next_label(observer);
    observer->pending_transaction_label = label;
    observer->pending_pdu_id = AVRCP_PDU_REGISTER_NOTIFICATION;
    observer->state = AVRCP_OBSERVER_WAIT_VOLUME_INTERIM;
    result.packet_size = avrcp_write_register_notification(
        result.packet,
        sizeof(result.packet),
        label,
        AVRCP_EVENT_VOLUME_CHANGED,
        0u);
    result.event = event;
    if (result.packet_size == 0u) {
        return avrcp_observer_fail(observer, AVRCP_NO_SPACE);
    }
    return result;
}

avrcp_observer_result avrcp_observer_begin(avrcp_observer *observer) {
    avrcp_observer_result result = avrcp_make_result();
    uint8_t label;
    if (observer == NULL || observer->state != AVRCP_OBSERVER_IDLE) {
        if (observer != NULL) {
            return avrcp_observer_fail(observer,
                                       AVRCP_UNEXPECTED_RESPONSE);
        }
        result.event.kind = AVRCP_OBSERVER_EVENT_PROTOCOL_ERROR;
        result.event.error_code = AVRCP_INVALID_ARGUMENT;
        return result;
    }
    label = avrcp_next_label(observer);
    observer->pending_transaction_label = label;
    observer->pending_pdu_id = AVRCP_PDU_GET_CAPABILITIES;
    observer->state = AVRCP_OBSERVER_WAIT_CAPABILITIES;
    result.packet_size = avrcp_write_get_capabilities_events(
        result.packet, sizeof(result.packet), label);
    if (result.packet_size == 0u) {
        return avrcp_observer_fail(observer, AVRCP_NO_SPACE);
    }
    return result;
}

static avrcp_observer_result avrcp_observer_handle_pass_through(
    avrcp_observer *observer,
    const uint8_t *packet,
    size_t packet_size) {
    avrcp_observer_result result = avrcp_make_result();
    avrcp_pass_through parsed;
    uint8_t response = AVRCP_RESPONSE_ACCEPTED;
    (void)observer;
    if (avrcp_parse_pass_through(packet,
                                 packet_size,
                                 &parsed) != AVRCP_OK ||
        parsed.frame.command_response != 0u ||
        parsed.frame.ctype_or_response != AVRCP_CTYPE_CONTROL) {
        return avrcp_observer_fail_stage(observer,
                                         AVRCP_PROTOCOL_ERROR,
                                         5u,
                                         packet,
                                         packet_size);
    }
    result.event.kind = AVRCP_OBSERVER_EVENT_PASS_THROUGH;
    result.event.operation_id = parsed.operation_id;
    result.event.released = parsed.released;
    switch (parsed.operation_id) {
        case AVRCP_OPERATION_VOLUME_UP:
        case AVRCP_OPERATION_VOLUME_DOWN:
        case AVRCP_OPERATION_MUTE:
        case AVRCP_OPERATION_PLAY:
        case AVRCP_OPERATION_STOP:
        case AVRCP_OPERATION_PAUSE:
        case AVRCP_OPERATION_FORWARD:
        case AVRCP_OPERATION_BACKWARD:
            break;
        default:
            response = AVRCP_RESPONSE_NOT_IMPLEMENTED;
            break;
    }
    result.packet_size = avrcp_write_pass_through_response(
        result.packet,
        sizeof(result.packet),
        packet,
        packet_size,
        response);
    if (result.packet_size == 0u) {
        return avrcp_observer_fail(observer, AVRCP_NO_SPACE);
    }
    return result;
}

static int avrcp_capabilities_include_volume(const uint8_t *parameters,
                                             size_t parameters_size) {
    size_t index;
    uint8_t count;
    if (parameters == NULL || parameters_size < 2u ||
        parameters[0] != AVRCP_CAPABILITY_EVENTS_SUPPORTED) {
        return 0;
    }
    count = parameters[1];
    if (parameters_size != (size_t)count + 2u) return 0;
    for (index = 0u; index < count; ++index) {
        if (parameters[2u + index] == AVRCP_EVENT_VOLUME_CHANGED) {
            return 1;
        }
    }
    return 0;
}

static avrcp_observer_result avrcp_observer_handle_vendor_command(
    avrcp_observer *observer,
    const uint8_t *packet,
    const avrcp_vendor_frame *vendor) {
    avrcp_observer_result result = avrcp_make_result();
    uint8_t response = AVRCP_RESPONSE_NOT_IMPLEMENTED;
    const uint8_t *parameters;
    uint8_t response_parameters[9] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    size_t response_parameters_size = 0u;
    size_t parameter_size;
    size_t index;
    parameters = packet + vendor->parameters_offset;
    parameter_size = vendor->parameters_size;
    if (vendor->pdu_id == AVRCP_PDU_GET_CAPABILITIES &&
        parameter_size >= 1u &&
        parameters[0] == AVRCP_CAPABILITY_EVENTS_SUPPORTED) {
        response = AVRCP_RESPONSE_STABLE;
        response_parameters[0] = AVRCP_CAPABILITY_EVENTS_SUPPORTED;
        response_parameters[1] = 3u;
        response_parameters[2] = AVRCP_EVENT_PLAYBACK_STATUS_CHANGED;
        response_parameters[3] = AVRCP_EVENT_TRACK_CHANGED;
        response_parameters[4] = AVRCP_EVENT_VOLUME_CHANGED;
        response_parameters_size = 5u;
    } else if (vendor->pdu_id == AVRCP_PDU_REGISTER_NOTIFICATION &&
               parameter_size >= 1u) {
        uint8_t event_id = parameters[0];
        if (event_id == AVRCP_EVENT_PLAYBACK_STATUS_CHANGED) {
            observer->playback_notification_label =
                vendor->frame.transaction_label;
            observer->playback_notification_active = 1u;
            observer->playback_notification_pending = 0u;
            response = AVRCP_RESPONSE_INTERIM;
            response_parameters[0] = AVRCP_EVENT_PLAYBACK_STATUS_CHANGED;
            response_parameters[1] = avrcp_current_playback_status(observer);
            response_parameters_size = 2u;
        } else if (event_id == AVRCP_EVENT_TRACK_CHANGED) {
            response = AVRCP_RESPONSE_INTERIM;
            response_parameters[0] = AVRCP_EVENT_TRACK_CHANGED;
            response_parameters_size = 9u;
        } else if (event_id == AVRCP_EVENT_VOLUME_CHANGED) {
            response = AVRCP_RESPONSE_INTERIM;
            response_parameters[0] = AVRCP_EVENT_VOLUME_CHANGED;
            response_parameters[1] = observer->last_absolute_volume_valid
                ? observer->last_absolute_volume
                : 0u;
            response_parameters_size = 2u;
        }
    } else if (vendor->pdu_id == AVRCP_PDU_GET_PLAY_STATUS) {
        response = AVRCP_RESPONSE_STABLE;
        response_parameters[0] = avrcp_current_playback_status(observer);
        response_parameters_size = 9u;
    }
    result.event.kind = AVRCP_OBSERVER_EVENT_VENDOR_COMMAND;
    result.event.pdu_id = vendor->pdu_id;
    result.event.parameter_size = (uint8_t)(parameter_size < 8u
        ? parameter_size : 8u);
    for (index = 0u; index < result.event.parameter_size; ++index) {
        result.event.parameter_bytes[index] = parameters[index];
    }
    result.packet_size = avrcp_write_vendor_response(
        result.packet,
        sizeof(result.packet),
        vendor->frame.transaction_label,
        response,
        vendor->pdu_id,
        response_parameters,
        response_parameters_size);
    if (result.packet_size == 0u) {
        return avrcp_observer_fail(observer, AVRCP_NO_SPACE);
    }
    return result;
}

avrcp_observer_result avrcp_observer_submit_playback_status(
    avrcp_observer *observer,
    uint8_t playback_status) {
    avrcp_observer_result result = avrcp_make_result();
    uint8_t parameters[2];
    if (observer == NULL ||
        !avrcp_is_valid_playback_status(playback_status) ||
        observer->state == AVRCP_OBSERVER_IDLE ||
        observer->state == AVRCP_OBSERVER_FAILED) {
        return result;
    }
    observer->playback_status = playback_status;
    observer->playback_status_valid = 1u;
    if (!observer->playback_notification_active) {
        observer->playback_notification_pending = 0u;
        return result;
    }
    observer->playback_notification_pending = 1u;
    if (observer->write_active) return result;
    parameters[0] = AVRCP_EVENT_PLAYBACK_STATUS_CHANGED;
    parameters[1] = playback_status;
    result.packet_size = avrcp_write_vendor_response(
        result.packet,
        sizeof(result.packet),
        observer->playback_notification_label,
        AVRCP_RESPONSE_CHANGED,
        AVRCP_PDU_REGISTER_NOTIFICATION,
        parameters,
        sizeof(parameters));
    if (result.packet_size != 0u) {
        observer->playback_notification_active = 0u;
        observer->playback_notification_pending = 0u;
    }
    return result;
}

avrcp_observer_result avrcp_observer_submit_write(
    avrcp_observer *observer,
    uint8_t pdu_id,
    uint8_t response_code,
    const uint8_t *parameters,
    size_t parameter_size) {
    avrcp_observer_result result = avrcp_make_result();
    uint8_t label;
    if (observer == NULL || pdu_id == 0u ||
        parameter_size > 8u ||
        (parameter_size != 0u && parameters == NULL)) {
        return result;
    }
    if (pdu_id == AVRCP_PDU_REGISTER_NOTIFICATION &&
        response_code == AVRCP_RESPONSE_CHANGED &&
        parameter_size == 2u &&
        parameters[0] == AVRCP_EVENT_PLAYBACK_STATUS_CHANGED) {
        return avrcp_observer_submit_playback_status(
            observer, parameters[1]);
    }
    if (observer->state == AVRCP_OBSERVER_IDLE ||
        observer->state == AVRCP_OBSERVER_FAILED ||
        observer->write_active) {
        return result;
    }
    label = avrcp_next_label(observer);
    if (response_code == 0u) {
        result.packet_size = avrcp_write_vendor_command(
            result.packet,
            sizeof(result.packet),
            label,
            AVRCP_CTYPE_CONTROL,
            pdu_id,
            parameters,
            parameter_size);
    } else {
        result.packet_size = avrcp_write_vendor_response(
            result.packet,
            sizeof(result.packet),
            label,
            response_code,
            pdu_id,
            parameters,
            parameter_size);
    }
    if (result.packet_size == 0u) return result;
    observer->write_label = label;
    observer->write_pdu_id = pdu_id;
    observer->write_active = 1u;
    return result;
}

avrcp_observer_result avrcp_observer_handle_packet(
    avrcp_observer *observer,
    const uint8_t *packet,
    size_t packet_size) {
    avrcp_frame frame;
    avrcp_vendor_frame vendor;
    avrcp_observer_event event;
    const uint8_t *parameters;
    avrcp_status status;
    if (observer == NULL || packet == NULL ||
        observer->state == AVRCP_OBSERVER_IDLE ||
        observer->state == AVRCP_OBSERVER_FAILED) {
        if (observer != NULL) {
            return avrcp_observer_fail(observer,
                                       AVRCP_UNEXPECTED_RESPONSE);
        }
        {
            avrcp_observer_result result = avrcp_make_result();
            result.event.kind = AVRCP_OBSERVER_EVENT_PROTOCOL_ERROR;
            result.event.error_code = AVRCP_INVALID_ARGUMENT;
            return result;
        }
    }
    status = avrcp_parse_frame(packet, packet_size, &frame);
    if (status != AVRCP_OK) {
        return avrcp_observer_fail_stage(observer,
                                         status,
                                         1u,
                                         packet,
                                         packet_size);
    }
    if (frame.opcode == AVRCP_OPCODE_PASS_THROUGH &&
        frame.command_response == 0u) {
        return avrcp_observer_handle_pass_through(observer,
                                                  packet,
                                                  packet_size);
    }
    status = avrcp_parse_vendor_frame(packet, packet_size, &vendor);
    if (status != AVRCP_OK) {
        return avrcp_observer_fail_stage(observer,
                                         status,
                                         2u,
                                         packet,
                                         packet_size);
    }
    if (observer->write_active &&
        vendor.frame.command_response != 0u &&
        vendor.frame.transaction_label == observer->write_label &&
        vendor.pdu_id == observer->write_pdu_id) {
        avrcp_observer_result result = avrcp_make_result();
        avrcp_observer_result notification;
        size_t index;
        observer->write_active = 0u;
        result.event.kind = AVRCP_OBSERVER_EVENT_WRITE_RESPONSE;
        result.event.response_code = vendor.frame.ctype_or_response;
        result.event.pdu_id = vendor.pdu_id;
        result.event.parameter_size = (uint8_t)(vendor.parameters_size < 8u
            ? vendor.parameters_size : 8u);
        for (index = 0u;
             index < result.event.parameter_size;
             ++index) {
            result.event.parameter_bytes[index] =
                packet[vendor.parameters_offset + index];
        }
        notification = avrcp_observer_submit_playback_status(
            observer, observer->playback_status);
        if (notification.packet_size != 0u) {
            memcpy(result.packet,
                   notification.packet,
                   notification.packet_size);
            result.packet_size = notification.packet_size;
        }
        return result;
    }
    if (vendor.frame.command_response == 0u) {
        return avrcp_observer_handle_vendor_command(
            observer, packet, &vendor);
    }
    if (vendor.frame.transaction_label !=
            observer->pending_transaction_label ||
        vendor.pdu_id != observer->pending_pdu_id) {
        return avrcp_observer_fail_stage(observer,
                                         AVRCP_UNEXPECTED_RESPONSE,
                                         3u,
                                         packet,
                                         packet_size);
    }
    parameters = packet + vendor.parameters_offset;
    memset(&event, 0, sizeof(event));

    if (observer->state == AVRCP_OBSERVER_WAIT_CAPABILITIES) {
        int supported;
        avrcp_observer_result result;
        if (vendor.frame.ctype_or_response != AVRCP_RESPONSE_STABLE) {
            return avrcp_observer_fail_stage(observer,
                                             AVRCP_UNEXPECTED_RESPONSE,
                                             4u,
                                             packet,
                                             packet_size);
        }
        supported = avrcp_capabilities_include_volume(
            parameters, vendor.parameters_size);
        observer->volume_supported = (uint8_t)(supported != 0);
        event.kind = AVRCP_OBSERVER_EVENT_VOLUME_CAPABILITY;
        event.volume_supported = observer->volume_supported;
        if (!supported) {
            result = avrcp_make_result();
            observer->state = AVRCP_OBSERVER_UNSUPPORTED;
            result.event = event;
            return result;
        }
        return avrcp_observer_register_volume(observer, event);
    }

    if ((observer->state == AVRCP_OBSERVER_WAIT_VOLUME_INTERIM ||
         observer->state == AVRCP_OBSERVER_OBSERVING) &&
        vendor.parameters_size == 2u &&
        parameters[0] == AVRCP_EVENT_VOLUME_CHANGED &&
        parameters[1] <= 0x7Fu) {
        observer->last_absolute_volume = parameters[1];
        observer->last_absolute_volume_valid = 1u;
        event.kind = AVRCP_OBSERVER_EVENT_VOLUME_CHANGED;
        event.volume_supported = 1u;
        event.absolute_volume = parameters[1];
        event.response_code = vendor.frame.ctype_or_response;
        if (vendor.frame.ctype_or_response == AVRCP_RESPONSE_INTERIM &&
            observer->state == AVRCP_OBSERVER_WAIT_VOLUME_INTERIM) {
            avrcp_observer_result result = avrcp_make_result();
            observer->state = AVRCP_OBSERVER_OBSERVING;
            result.event = event;
            return result;
        }
        if (vendor.frame.ctype_or_response == AVRCP_RESPONSE_CHANGED &&
            observer->state == AVRCP_OBSERVER_OBSERVING) {
            return avrcp_observer_register_volume(observer, event);
        }
    }
    return avrcp_observer_fail_stage(observer,
                                     AVRCP_UNEXPECTED_RESPONSE,
                                     4u,
                                     packet,
                                     packet_size);
}
