// SPDX-License-Identifier: Apache-2.0
#include "ldac_native/avdtp.h"

#include <string.h>

static avdtp_action make_action(avdtp_action_kind kind) {
    avdtp_action action;
    memset(&action, 0, sizeof(action));
    action.kind = kind;
    return action;
}

static avdtp_action fail(avdtp_source *source, int error_code) {
    avdtp_action action = make_action(AVDTP_ACTION_ERROR);
    if (source != NULL) source->state = AVDTP_SOURCE_FAILED;
    action.error_code = error_code;
    return action;
}

avdtp_status avdtp_parse_header(const uint8_t *packet,
                                size_t packet_size,
                                avdtp_header *out) {
    uint8_t first;
    if (packet == NULL || out == NULL) return AVDTP_INVALID_ARGUMENT;
    if (packet_size < 1u) return AVDTP_TRUNCATED;
    memset(out, 0, sizeof(*out));
    first = packet[0];
    out->transaction_label = (uint8_t)(first >> 4u);
    out->packet_type = (avdtp_packet_type)((first >> 2u) & 0x03u);
    out->message_type = (avdtp_message_type)(first & 0x03u);

    switch (out->packet_type) {
        case AVDTP_PACKET_SINGLE:
            if (packet_size < 2u) return AVDTP_TRUNCATED;
            out->signal_id = (uint8_t)(packet[1] & 0x3Fu);
            out->payload_offset = 2u;
            return AVDTP_OK;
        case AVDTP_PACKET_START:
            if (packet_size < 3u) return AVDTP_TRUNCATED;
            out->packet_count = packet[1];
            out->signal_id = (uint8_t)(packet[2] & 0x3Fu);
            out->payload_offset = 3u;
            return AVDTP_OK;
        case AVDTP_PACKET_CONTINUE:
        case AVDTP_PACKET_END:
            out->payload_offset = 1u;
            return AVDTP_OK;
        default:
            return AVDTP_PROTOCOL_ERROR;
    }
}

size_t avdtp_write_single(uint8_t *out,
                          size_t out_size,
                          uint8_t transaction_label,
                          avdtp_message_type message_type,
                          uint8_t signal_id,
                          const uint8_t *payload,
                          size_t payload_size) {
    size_t required;
    if (out == NULL || transaction_label > 0x0Fu || signal_id == 0u ||
        signal_id > 0x3Fu || (payload_size != 0u && payload == NULL) ||
        payload_size > (size_t)-1 - 2u) {
        return 0u;
    }
    required = 2u + payload_size;
    if (out_size < required) return 0u;
    out[0] = (uint8_t)((transaction_label << 4u) |
                       ((uint8_t)AVDTP_PACKET_SINGLE << 2u) |
                       ((uint8_t)message_type & 0x03u));
    out[1] = signal_id;
    if (payload_size != 0u) memcpy(out + 2u, payload, payload_size);
    return required;
}

static avdtp_action send_command(avdtp_source *source,
                                 uint8_t signal_id,
                                 const uint8_t *payload,
                                 size_t payload_size) {
    avdtp_action action = make_action(AVDTP_ACTION_SEND_SIGNALING);
    uint8_t label = source->next_transaction_label;
    source->next_transaction_label = (uint8_t)((label + 1u) & 0x0Fu);
    source->pending_transaction_label = label;
    source->pending_signal_id = signal_id;
    action.packet_size = avdtp_write_single(action.packet,
                                            sizeof(action.packet),
                                            label,
                                            AVDTP_MESSAGE_COMMAND,
                                            signal_id,
                                            payload,
                                            payload_size);
    if (action.packet_size == 0u) {
        return fail(source, AVDTP_SOURCE_ERROR_BAD_PACKET);
    }
    return action;
}

static size_t collect_audio_sinks(
    const uint8_t *payload,
    size_t payload_size,
    uint8_t seids[AVDTP_MAX_REMOTE_SEIDS]) {
    size_t offset;
    size_t count = 0u;
    if (payload == NULL || seids == NULL || (payload_size % 2u) != 0u) {
        return 0u;
    }
    for (offset = 0u; offset < payload_size; offset += 2u) {
        uint8_t candidate = (uint8_t)(payload[offset] >> 2u);
        uint8_t in_use = (uint8_t)((payload[offset] >> 1u) & 0x01u);
        uint8_t media_type = (uint8_t)(payload[offset + 1u] >> 4u);
        uint8_t tsep = (uint8_t)((payload[offset + 1u] >> 3u) & 0x01u);
        if (candidate != 0u && in_use == 0u &&
            media_type == AVDTP_MEDIA_TYPE_AUDIO && tsep == 1u) {
            if (count < AVDTP_MAX_REMOTE_SEIDS) seids[count++] = candidate;
        }
    }
    return count;
}

static avdtp_action query_current_sink(avdtp_source *source) {
    uint8_t seid_payload = (uint8_t)(source->remote_seid << 2u);
    source->state = AVDTP_SOURCE_CAPABILITIES_SENT;
    source->used_legacy_capabilities = 0u;
    return send_command(source,
                        AVDTP_SIGNAL_GET_ALL_CAPABILITIES,
                        &seid_payload,
                        1u);
}

static avdtp_action query_next_sink(avdtp_source *source) {
    if ((size_t)source->remote_seid_index + 1u >=
        source->remote_seid_count) {
        return fail(source, AVDTP_SOURCE_ERROR_LDAC_NOT_SUPPORTED);
    }
    source->remote_seid_index++;
    source->remote_seid = source->remote_seids[source->remote_seid_index];
    return query_current_sink(source);
}

void avdtp_source_init(avdtp_source *source,
                       ldac_capabilities local_capabilities,
                       uint8_t local_seid,
                       unsigned preferred_sample_rate_hz) {
    if (source == NULL) return;
    memset(source, 0, sizeof(*source));
    source->state = AVDTP_SOURCE_IDLE;
    source->local_capabilities.sample_rates =
        (uint8_t)(local_capabilities.sample_rates & LDAC_SF_ALL);
    source->local_capabilities.channel_modes =
        (uint8_t)(local_capabilities.channel_modes & LDAC_CM_ALL);
    source->local_seid = local_seid;
    source->preferred_sample_rate_hz = preferred_sample_rate_hz;
}

avdtp_action avdtp_source_begin(avdtp_source *source) {
    if (source == NULL || source->state != AVDTP_SOURCE_IDLE ||
        source->local_seid == 0u || source->local_seid > 0x3Fu ||
        source->local_capabilities.sample_rates == 0u ||
        source->local_capabilities.channel_modes == 0u) {
        return fail(source, AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE);
    }
    source->state = AVDTP_SOURCE_DISCOVER_SENT;
    return send_command(source, AVDTP_SIGNAL_DISCOVER, NULL, 0u);
}

static avdtp_action handle_reject(avdtp_source *source,
                                  avdtp_message_type message_type,
                                  const uint8_t *payload,
                                  size_t payload_size) {
    if (source->state == AVDTP_SOURCE_CAPABILITIES_SENT &&
        source->pending_signal_id == AVDTP_SIGNAL_GET_ALL_CAPABILITIES &&
        source->used_legacy_capabilities == 0u) {
        uint8_t seid_payload = (uint8_t)(source->remote_seid << 2u);
        source->used_legacy_capabilities = 1u;
        return send_command(source,
                            AVDTP_SIGNAL_GET_CAPABILITIES,
                            &seid_payload,
                            1u);
    }
    if (source->state == AVDTP_SOURCE_CAPABILITIES_SENT) {
        return query_next_sink(source);
    }
    if (message_type == AVDTP_MESSAGE_REJECT && payload_size != 0u) {
        return fail(source, payload[payload_size - 1u]);
    }
    return fail(source, AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE);
}

avdtp_action avdtp_source_handle_signaling(avdtp_source *source,
                                           const uint8_t *packet,
                                           size_t packet_size) {
    avdtp_header header;
    const uint8_t *payload;
    size_t payload_size;
    avdtp_status header_status;
    if (source == NULL || packet == NULL || source->state == AVDTP_SOURCE_FAILED ||
        source->state == AVDTP_SOURCE_IDLE || source->state == AVDTP_SOURCE_STREAMING) {
        return fail(source, AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE);
    }

    header_status = avdtp_parse_header(packet, packet_size, &header);
    if (header_status != AVDTP_OK || header.packet_type != AVDTP_PACKET_SINGLE ||
        header.payload_offset > packet_size) {
        return fail(source, AVDTP_SOURCE_ERROR_BAD_PACKET);
    }
    if (header.transaction_label != source->pending_transaction_label ||
        header.signal_id != source->pending_signal_id ||
        header.message_type == AVDTP_MESSAGE_COMMAND) {
        return fail(source, AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE);
    }

    payload = packet + header.payload_offset;
    payload_size = packet_size - header.payload_offset;
    if (header.message_type == AVDTP_MESSAGE_REJECT ||
        header.message_type == AVDTP_MESSAGE_GENERAL_REJECT) {
        return handle_reject(source, header.message_type, payload, payload_size);
    }
    if (header.message_type != AVDTP_MESSAGE_ACCEPT) {
        return fail(source, AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE);
    }

    switch (source->state) {
        case AVDTP_SOURCE_DISCOVER_SENT: {
            size_t sink_count = collect_audio_sinks(
                payload,
                payload_size,
                source->remote_seids);
            if (sink_count == 0u) {
                return fail(source, AVDTP_SOURCE_ERROR_NO_AUDIO_SINK);
            }
            source->remote_seid_count = (uint8_t)sink_count;
            source->remote_seid_index = 0u;
            source->remote_seid = source->remote_seids[0];
            return query_current_sink(source);
        }
        case AVDTP_SOURCE_CAPABILITIES_SENT: {
            uint8_t configuration_payload[16];
            size_t configuration_size;
            ldac_codec_status codec_status =
                ldac_find_in_service_capabilities(payload,
                                                  payload_size,
                                                  &source->remote_capabilities);
            if (codec_status == LDAC_CODEC_NOT_FOUND) {
                return query_next_sink(source);
            }
            if (codec_status != LDAC_CODEC_OK) {
                return fail(source, AVDTP_SOURCE_ERROR_BAD_PACKET);
            }
            codec_status = ldac_choose_configuration(source->local_capabilities,
                                                     source->remote_capabilities,
                                                     source->preferred_sample_rate_hz,
                                                     &source->configuration);
            if (codec_status != LDAC_CODEC_OK) {
                return fail(source, AVDTP_SOURCE_ERROR_NO_COMMON_CONFIGURATION);
            }
            configuration_size =
                ldac_build_set_configuration_payload(configuration_payload,
                                                     sizeof(configuration_payload),
                                                     source->remote_seid,
                                                     source->local_seid,
                                                     source->configuration);
            if (configuration_size == 0u) {
                return fail(source, AVDTP_SOURCE_ERROR_BAD_PACKET);
            }
            source->state = AVDTP_SOURCE_CONFIGURATION_SENT;
            return send_command(source,
                                AVDTP_SIGNAL_SET_CONFIGURATION,
                                configuration_payload,
                                configuration_size);
        }
        case AVDTP_SOURCE_CONFIGURATION_SENT: {
            uint8_t seid_payload = (uint8_t)(source->remote_seid << 2u);
            source->state = AVDTP_SOURCE_OPEN_SENT;
            return send_command(source, AVDTP_SIGNAL_OPEN, &seid_payload, 1u);
        }
        case AVDTP_SOURCE_OPEN_SENT:
            source->state = AVDTP_SOURCE_WAITING_FOR_MEDIA_CHANNEL;
            return make_action(AVDTP_ACTION_OPEN_MEDIA_CHANNEL);
        case AVDTP_SOURCE_START_SENT:
            source->state = AVDTP_SOURCE_STREAMING;
            return make_action(AVDTP_ACTION_STREAM_READY);
        case AVDTP_SOURCE_SUSPEND_SENT:
            source->state = AVDTP_SOURCE_OPEN;
            return make_action(AVDTP_ACTION_STREAM_SUSPENDED);
        case AVDTP_SOURCE_CLOSE_SENT:
            source->state = AVDTP_SOURCE_CLOSED;
            return make_action(AVDTP_ACTION_SESSION_CLOSED);
        case AVDTP_SOURCE_WAITING_FOR_MEDIA_CHANNEL:
        case AVDTP_SOURCE_OPEN:
        case AVDTP_SOURCE_IDLE:
        case AVDTP_SOURCE_STREAMING:
        case AVDTP_SOURCE_CLOSED:
        case AVDTP_SOURCE_FAILED:
        default:
            return fail(source, AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE);
    }
}

avdtp_action avdtp_source_media_channel_opened(avdtp_source *source) {
    if (source == NULL || source->state != AVDTP_SOURCE_WAITING_FOR_MEDIA_CHANNEL) {
        return fail(source, AVDTP_SOURCE_ERROR_MEDIA_CHANNEL_STATE);
    }
    source->state = AVDTP_SOURCE_OPEN;
    return make_action(AVDTP_ACTION_SESSION_OPEN);
}

avdtp_action avdtp_source_start(avdtp_source *source) {
    uint8_t seid_payload;
    if (source == NULL || source->state != AVDTP_SOURCE_OPEN) {
        return fail(source, AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE);
    }
    seid_payload = (uint8_t)(source->remote_seid << 2u);
    source->state = AVDTP_SOURCE_START_SENT;
    return send_command(source, AVDTP_SIGNAL_START, &seid_payload, 1u);
}

avdtp_action avdtp_source_suspend(avdtp_source *source) {
    uint8_t seid_payload;
    if (source == NULL || source->state != AVDTP_SOURCE_STREAMING) {
        return fail(source, AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE);
    }
    seid_payload = (uint8_t)(source->remote_seid << 2u);
    source->state = AVDTP_SOURCE_SUSPEND_SENT;
    return send_command(source, AVDTP_SIGNAL_SUSPEND, &seid_payload, 1u);
}

avdtp_action avdtp_source_close(avdtp_source *source) {
    uint8_t seid_payload;
    if (source == NULL || source->state != AVDTP_SOURCE_OPEN) {
        return fail(source, AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE);
    }
    seid_payload = (uint8_t)(source->remote_seid << 2u);
    source->state = AVDTP_SOURCE_CLOSE_SENT;
    return send_command(source, AVDTP_SIGNAL_CLOSE, &seid_payload, 1u);
}
