// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <string.h>

#include "ldac_native/avrcp.h"

static int failures;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            failures++;                                                        \
        }                                                                       \
    } while (0)

static void check_bytes(const uint8_t *actual,
                        const uint8_t *expected,
                        size_t size) {
    CHECK(memcmp(actual, expected, size) == 0);
}

static void test_build_and_parse_metadata(void) {
    uint8_t packet[32];
    avrcp_vendor_frame parsed;
    const uint8_t get_caps[] = {
        0xA0u, 0x11u, 0x0Eu, 0x01u, 0x48u, 0x00u,
        0x00u, 0x19u, 0x58u, 0x10u, 0x00u, 0x00u, 0x01u, 0x03u
    };
    const uint8_t register_volume[] = {
        0xB0u, 0x11u, 0x0Eu, 0x03u, 0x48u, 0x00u,
        0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x05u,
        0x0Du, 0x00u, 0x00u, 0x00u, 0x00u
    };
    size_t size = avrcp_write_get_capabilities_events(
        packet, sizeof(packet), 0x0Au);
    CHECK(size == sizeof(get_caps));
    check_bytes(packet, get_caps, sizeof(get_caps));
    CHECK(avrcp_parse_vendor_frame(packet, size, &parsed) == AVRCP_OK);
    CHECK(parsed.frame.transaction_label == 0x0Au);
    CHECK(parsed.frame.command_response == 0u);
    CHECK(parsed.frame.ctype_or_response == AVRCP_CTYPE_STATUS);
    CHECK(parsed.pdu_id == AVRCP_PDU_GET_CAPABILITIES);
    CHECK(parsed.parameters_offset == 13u && parsed.parameters_size == 1u);

    size = avrcp_write_register_notification(
        packet,
        sizeof(packet),
        0x0Bu,
        AVRCP_EVENT_VOLUME_CHANGED,
        0u);
    CHECK(size == sizeof(register_volume));
    check_bytes(packet, register_volume, sizeof(register_volume));
    CHECK(avrcp_parse_vendor_frame(packet, size, &parsed) == AVRCP_OK);
    CHECK(parsed.frame.ctype_or_response == AVRCP_CTYPE_NOTIFY);
    CHECK(parsed.pdu_id == AVRCP_PDU_REGISTER_NOTIFICATION);
}

static void test_pass_through(void) {
    const uint8_t command[] = {
        0x30u, 0x11u, 0x0Eu, 0x00u, 0x48u, 0x7Cu, 0x4Bu, 0x00u
    };
    const uint8_t expected_response[] = {
        0x32u, 0x11u, 0x0Eu, 0x09u, 0x48u, 0x7Cu, 0x4Bu, 0x00u
    };
    uint8_t response[16];
    avrcp_pass_through parsed;
    size_t size;
    CHECK(avrcp_parse_pass_through(command,
                                   sizeof(command),
                                   &parsed) == AVRCP_OK);
    CHECK(parsed.frame.command_response == 0u);
    CHECK(parsed.operation_id == AVRCP_OPERATION_FORWARD);
    CHECK(parsed.released == 0u);
    size = avrcp_write_pass_through_response(
        response,
        sizeof(response),
        command,
        sizeof(command),
        AVRCP_RESPONSE_ACCEPTED);
    CHECK(size == sizeof(expected_response));
    check_bytes(response, expected_response, sizeof(expected_response));
}

static void test_observer_happy_path(void) {
    avrcp_observer observer;
    avrcp_observer_result result;
    const uint8_t capabilities[] = {
        0x02u, 0x11u, 0x0Eu, 0x0Cu, 0x48u, 0x00u,
        0x00u, 0x19u, 0x58u, 0x10u, 0x00u, 0x00u, 0x05u,
        0x03u, 0x03u, 0x01u, 0x02u, 0x0Du
    };
    const uint8_t volume_interim[] = {
        0x12u, 0x11u, 0x0Eu, 0x0Fu, 0x48u, 0x00u,
        0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x02u,
        0x0Du, 0x40u
    };
    const uint8_t volume_changed[] = {
        0x12u, 0x11u, 0x0Eu, 0x0Du, 0x48u, 0x00u,
        0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x02u,
        0x0Du, 0x41u
    };
    const uint8_t pause_press[] = {
        0x70u, 0x11u, 0x0Eu, 0x00u, 0x48u, 0x7Cu, 0x46u, 0x00u
    };

    avrcp_observer_init(&observer);
    result = avrcp_observer_begin(&observer);
    CHECK(observer.state == AVRCP_OBSERVER_WAIT_CAPABILITIES);
    CHECK(result.packet_size == 14u && result.event.kind == 0);

    result = avrcp_observer_handle_packet(
        &observer, capabilities, sizeof(capabilities));
    CHECK(result.event.kind == AVRCP_OBSERVER_EVENT_VOLUME_CAPABILITY);
    CHECK(result.event.volume_supported == 1u);
    CHECK(observer.state == AVRCP_OBSERVER_WAIT_VOLUME_INTERIM);
    CHECK(result.packet_size == 18u);

    result = avrcp_observer_handle_packet(
        &observer, volume_interim, sizeof(volume_interim));
    CHECK(result.event.kind == AVRCP_OBSERVER_EVENT_VOLUME_CHANGED);
    CHECK(result.event.absolute_volume == 0x40u);
    CHECK(result.event.response_code == AVRCP_RESPONSE_INTERIM);
    CHECK(result.packet_size == 0u);
    CHECK(observer.state == AVRCP_OBSERVER_OBSERVING);

    result = avrcp_observer_handle_packet(
        &observer, pause_press, sizeof(pause_press));
    CHECK(result.event.kind == AVRCP_OBSERVER_EVENT_PASS_THROUGH);
    CHECK(result.event.operation_id == AVRCP_OPERATION_PAUSE);
    CHECK(result.packet_size == sizeof(pause_press));
    CHECK(observer.state == AVRCP_OBSERVER_OBSERVING);

    result = avrcp_observer_handle_packet(
        &observer, volume_changed, sizeof(volume_changed));
    CHECK(result.event.kind == AVRCP_OBSERVER_EVENT_VOLUME_CHANGED);
    CHECK(result.event.absolute_volume == 0x41u);
    CHECK(result.event.response_code == AVRCP_RESPONSE_CHANGED);
    CHECK(result.packet_size == 18u);
    CHECK(observer.state == AVRCP_OBSERVER_WAIT_VOLUME_INTERIM);
    CHECK(observer.pending_transaction_label == 2u);
}


static void test_write_path(void) {
    avrcp_observer observer;
    avrcp_observer_result result;
    avrcp_vendor_frame parsed;
    uint8_t frame[64];
    uint8_t response[160];
    uint8_t playing = AVRCP_PLAYBACK_STATUS_PLAYING;
    uint8_t volume = 0x40u;
    uint8_t volume2 = 0x41u;
    const uint8_t playback_register[] = {
        0x50u, 0x11u, 0x0Eu, 0x03u, 0x48u, 0x00u,
        0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x05u,
        0x01u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    const uint8_t playback_changed_after_volume_expected[] = {
        0x52u, 0x11u, 0x0Eu, 0x0Du, 0x48u, 0x00u,
        0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x02u,
        0x01u, 0x02u
    };
    size_t size;

    size = avrcp_write_set_absolute_volume(
        frame, sizeof(frame), 0x0Au, volume);
    CHECK(size == 14u);
    CHECK(avrcp_parse_vendor_frame(frame, size, &parsed) == AVRCP_OK);
    CHECK(parsed.frame.command_response == 0u);
    CHECK(parsed.frame.transaction_label == 0x0Au);
    CHECK(parsed.pdu_id == AVRCP_PDU_SET_ABSOLUTE_VOLUME);
    CHECK(parsed.parameters_size == 1u && frame[13] == volume);

    size = avrcp_write_notification_changed(
        frame,
        sizeof(frame),
        0x0Bu,
        AVRCP_EVENT_PLAYBACK_STATUS_CHANGED,
        &playing,
        1u);
    CHECK(size == 15u);
    CHECK(avrcp_parse_vendor_frame(frame, size, &parsed) == AVRCP_OK);
    CHECK(parsed.frame.command_response != 0u);
    CHECK(parsed.frame.ctype_or_response == AVRCP_RESPONSE_CHANGED);
    CHECK(parsed.pdu_id == AVRCP_PDU_REGISTER_NOTIFICATION);
    CHECK(frame[13] == AVRCP_EVENT_PLAYBACK_STATUS_CHANGED);
    CHECK(frame[14] == AVRCP_PLAYBACK_STATUS_PLAYING);

    avrcp_observer_init(&observer);
    (void)avrcp_observer_begin(&observer);
    {
        const uint8_t capabilities[] = {
            0x02u, 0x11u, 0x0Eu, 0x0Cu, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x10u, 0x00u, 0x00u, 0x05u,
            0x03u, 0x03u, 0x01u, 0x02u, 0x0Du
        };
        const uint8_t volume_interim[] = {
            0x12u, 0x11u, 0x0Eu, 0x0Fu, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x02u,
            0x0Du, 0x40u
        };
        result = avrcp_observer_handle_packet(
            &observer, capabilities, sizeof(capabilities));
        CHECK(result.event.kind == AVRCP_OBSERVER_EVENT_VOLUME_CAPABILITY);
        result = avrcp_observer_handle_packet(
            &observer, volume_interim, sizeof(volume_interim));
        CHECK(observer.state == AVRCP_OBSERVER_OBSERVING);
    }

    result = avrcp_observer_submit_write(
        &observer,
        AVRCP_PDU_SET_ABSOLUTE_VOLUME,
        0u,
        &volume,
        1u);
    CHECK(result.packet_size == 14u);
    CHECK(observer.write_active == 1u);
    result = avrcp_observer_submit_write(
        &observer,
        AVRCP_PDU_SET_ABSOLUTE_VOLUME,
        0u,
        &volume2,
        1u);
    CHECK(result.packet_size == 0u && observer.write_active == 1u);

    memset(response, 0, sizeof(response));
    response[0] = (uint8_t)((observer.write_label << 4u) | 0x02u);
    response[1] = 0x11u;
    response[2] = 0x0Eu;
    response[3] = AVRCP_RESPONSE_ACCEPTED;
    response[4] = 0x48u;
    response[5] = 0x00u;
    response[6] = 0x00u;
    response[7] = 0x19u;
    response[8] = 0x58u;
    response[9] = AVRCP_PDU_SET_ABSOLUTE_VOLUME;
    response[10] = 0x00u;
    response[11] = 0x00u;
    response[12] = 0x01u;
    response[13] = volume;
    result = avrcp_observer_handle_packet(
        &observer, response, 14u);
    CHECK(result.event.kind == AVRCP_OBSERVER_EVENT_WRITE_RESPONSE);
    CHECK(result.event.pdu_id == AVRCP_PDU_SET_ABSOLUTE_VOLUME);
    CHECK(result.event.response_code == AVRCP_RESPONSE_ACCEPTED);
    CHECK(result.event.parameter_size == 1u);
    CHECK(result.event.parameter_bytes[0] == volume);
    CHECK(observer.write_active == 0u);
    CHECK(observer.state == AVRCP_OBSERVER_OBSERVING);

    result = avrcp_observer_handle_packet(
        &observer, playback_register, sizeof(playback_register));
    CHECK(result.event.kind == AVRCP_OBSERVER_EVENT_VENDOR_COMMAND);
    CHECK(result.event.parameter_bytes[0] ==
          AVRCP_EVENT_PLAYBACK_STATUS_CHANGED);
    CHECK(result.packet_size != 0u);
    result = avrcp_observer_submit_write(
        &observer,
        AVRCP_PDU_SET_ABSOLUTE_VOLUME,
        0u,
        &volume2,
        1u);
    CHECK(result.packet_size == 14u);
    CHECK(observer.write_active == 1u);
    result = avrcp_observer_submit_playback_status(
        &observer, AVRCP_PLAYBACK_STATUS_PAUSED);
    CHECK(result.packet_size == 0u);
    CHECK(observer.playback_notification_pending == 1u);
    memset(response, 0, sizeof(response));
    response[0] = (uint8_t)((observer.write_label << 4u) | 0x02u);
    response[1] = 0x11u;
    response[2] = 0x0Eu;
    response[3] = AVRCP_RESPONSE_ACCEPTED;
    response[4] = 0x48u;
    response[5] = 0x00u;
    response[6] = 0x00u;
    response[7] = 0x19u;
    response[8] = 0x58u;
    response[9] = AVRCP_PDU_SET_ABSOLUTE_VOLUME;
    response[10] = 0x00u;
    response[11] = 0x00u;
    response[12] = 0x01u;
    response[13] = volume2;
    result = avrcp_observer_handle_packet(
        &observer, response, 14u);
    CHECK(result.event.kind == AVRCP_OBSERVER_EVENT_WRITE_RESPONSE);
    CHECK(result.packet_size ==
          sizeof(playback_changed_after_volume_expected));
    check_bytes(result.packet,
                playback_changed_after_volume_expected,
                sizeof(playback_changed_after_volume_expected));
    CHECK(observer.write_active == 0u);
    CHECK(observer.playback_notification_active == 0u);
    CHECK(observer.playback_notification_pending == 0u);
}
static void test_padded_zero_frames(void) {
    uint8_t capabilities[160];
    uint8_t volume_interim[160];
    uint8_t pause_press[160];
    uint8_t response[64];
    avrcp_vendor_frame parsed;
    avrcp_pass_through pass;
    avrcp_observer observer;
    avrcp_observer_result result;
    const uint8_t capabilities_frame[] = {
        0x02u, 0x11u, 0x0Eu, 0x0Cu, 0x48u, 0x00u,
        0x00u, 0x19u, 0x58u, 0x10u, 0x00u, 0x00u, 0x03u,
        0x03u, 0x01u, 0x0Du
    };
    const uint8_t volume_interim_frame[] = {
        0x12u, 0x11u, 0x0Eu, 0x0Fu, 0x48u, 0x00u,
        0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x02u,
        0x0Du, 0x40u
    };
    const uint8_t pause_frame[] = {
        0x70u, 0x11u, 0x0Eu, 0x00u, 0x48u, 0x7Cu, 0x46u, 0x00u
    };
    const uint8_t expected_response[] = {
        0x72u, 0x11u, 0x0Eu, 0x09u, 0x48u, 0x7Cu, 0x46u, 0x00u
    };
    size_t size;
    memset(capabilities, 0, sizeof(capabilities));
    memcpy(capabilities, capabilities_frame, sizeof(capabilities_frame));
    CHECK(avrcp_parse_vendor_frame(capabilities,
                                   sizeof(capabilities),
                                   &parsed) == AVRCP_OK);
    CHECK(parsed.pdu_id == AVRCP_PDU_GET_CAPABILITIES);
    CHECK(parsed.parameters_size == 3u);

    capabilities[sizeof(capabilities) - 1u] = 1u;
    CHECK(avrcp_parse_vendor_frame(capabilities,
                                   sizeof(capabilities),
                                   &parsed) == AVRCP_PROTOCOL_ERROR);
    capabilities[sizeof(capabilities) - 1u] = 0u;

    memset(pause_press, 0, sizeof(pause_press));
    memcpy(pause_press, pause_frame, sizeof(pause_frame));
    CHECK(avrcp_parse_pass_through(pause_press,
                                   sizeof(pause_press),
                                   &pass) == AVRCP_OK);
    CHECK(pass.operation_id == AVRCP_OPERATION_PAUSE);
    size = avrcp_write_pass_through_response(
        response,
        sizeof(response),
        pause_press,
        sizeof(pause_press),
        AVRCP_RESPONSE_ACCEPTED);
    CHECK(size == sizeof(expected_response));
    check_bytes(response, expected_response, sizeof(expected_response));

    avrcp_observer_init(&observer);
    (void)avrcp_observer_begin(&observer);
    memset(volume_interim, 0, sizeof(volume_interim));
    memcpy(volume_interim,
           volume_interim_frame,
           sizeof(volume_interim_frame));
    result = avrcp_observer_handle_packet(
        &observer, capabilities, sizeof(capabilities));
    CHECK(result.event.kind == AVRCP_OBSERVER_EVENT_VOLUME_CAPABILITY);
    CHECK(result.event.volume_supported == 1u);
    result = avrcp_observer_handle_packet(
        &observer, volume_interim, sizeof(volume_interim));
    CHECK(result.event.kind == AVRCP_OBSERVER_EVENT_VOLUME_CHANGED);
    CHECK(result.event.absolute_volume == 0x40u);
    CHECK(observer.state == AVRCP_OBSERVER_OBSERVING);
    result = avrcp_observer_handle_packet(
        &observer, pause_press, sizeof(pause_press));
    CHECK(result.event.kind == AVRCP_OBSERVER_EVENT_PASS_THROUGH);
    CHECK(result.event.operation_id == AVRCP_OPERATION_PAUSE);
    CHECK(result.packet_size == sizeof(expected_response));
    check_bytes(result.packet, expected_response, sizeof(expected_response));

    {
        uint8_t caps_command[160];
        uint8_t register_command[160];
        uint8_t volume_changed[160];
        const uint8_t caps_command_frame[] = {
            0x30u, 0x11u, 0x0Eu, 0x01u, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x10u, 0x00u, 0x00u, 0x01u,
            0x03u
        };
        const uint8_t caps_response_expected[] = {
            0x32u, 0x11u, 0x0Eu, 0x0Cu, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x10u, 0x00u, 0x00u, 0x05u,
            0x03u, 0x03u, 0x01u, 0x02u, 0x0Du
        };
        const uint8_t register_command_frame[] = {
            0x40u, 0x11u, 0x0Eu, 0x03u, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x05u,
            0x0Du, 0x00u, 0x00u, 0x00u, 0x00u
        };
        const uint8_t volume_interim_expected[] = {
            0x42u, 0x11u, 0x0Eu, 0x0Fu, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x02u,
            0x0Du, 0x40u
        };
        const uint8_t playback_status_command_frame[] = {
            0x50u, 0x11u, 0x0Eu, 0x03u, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x05u,
            0x01u, 0x00u, 0x00u, 0x00u, 0x00u
        };
        const uint8_t playback_status_interim_expected[] = {
            0x52u, 0x11u, 0x0Eu, 0x0Fu, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x02u,
            0x01u, 0x01u
        };
        const uint8_t playback_status_changed_expected[] = {
            0x52u, 0x11u, 0x0Eu, 0x0Du, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x02u,
            0x01u, 0x02u
        };
        const uint8_t playback_status_reregister_frame[] = {
            0x60u, 0x11u, 0x0Eu, 0x03u, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x05u,
            0x01u, 0x00u, 0x00u, 0x00u, 0x00u
        };
        const uint8_t playback_status_reregister_interim_expected[] = {
            0x62u, 0x11u, 0x0Eu, 0x0Fu, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x02u,
            0x01u, 0x02u
        };
        const uint8_t playback_status_reregister_changed_expected[] = {
            0x62u, 0x11u, 0x0Eu, 0x0Du, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x02u,
            0x01u, 0x01u
        };
        const uint8_t track_changed_command_frame[] = {
            0x60u, 0x11u, 0x0Eu, 0x03u, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x05u,
            0x02u, 0x00u, 0x00u, 0x00u, 0x00u
        };
        const uint8_t track_changed_interim_expected[] = {
            0x62u, 0x11u, 0x0Eu, 0x0Fu, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x09u,
            0x02u, 0x00u, 0x00u, 0x00u, 0x00u,
            0x00u, 0x00u, 0x00u, 0x00u
        };
        const uint8_t get_play_status_command_frame[] = {
            0x80u, 0x11u, 0x0Eu, 0x01u, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x20u, 0x00u, 0x00u, 0x00u
        };
        const uint8_t get_play_status_expected[] = {
            0x82u, 0x11u, 0x0Eu, 0x0Cu, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x20u, 0x00u, 0x00u, 0x09u,
            0x01u, 0x00u, 0x00u, 0x00u, 0x00u,
            0x00u, 0x00u, 0x00u, 0x00u
        };
        const uint8_t set_volume_command_frame[] = {
            0x50u, 0x11u, 0x0Eu, 0x00u, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x50u, 0x00u, 0x00u, 0x01u,
            0x2Au
        };
        const uint8_t set_volume_not_implemented[] = {
            0x52u, 0x11u, 0x0Eu, 0x08u, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x50u, 0x00u, 0x00u, 0x00u
        };
        const uint8_t volume_changed_frame[] = {
            0x12u, 0x11u, 0x0Eu, 0x0Du, 0x48u, 0x00u,
            0x00u, 0x19u, 0x58u, 0x31u, 0x00u, 0x00u, 0x02u,
            0x0Du, 0x41u
        };
        CHECK(avrcp_observer_submit_playback_status(
                  &observer, AVRCP_PLAYBACK_STATUS_PLAYING).packet_size == 0u);
        memset(caps_command, 0, sizeof(caps_command));
        memcpy(caps_command, caps_command_frame, sizeof(caps_command_frame));
        result = avrcp_observer_handle_packet(
            &observer, caps_command, sizeof(caps_command));
        CHECK(result.event.kind == AVRCP_OBSERVER_EVENT_VENDOR_COMMAND);
        CHECK(result.event.pdu_id == AVRCP_PDU_GET_CAPABILITIES);
        CHECK(result.event.parameter_size == 1u);
        CHECK(result.event.parameter_bytes[0] ==
              AVRCP_CAPABILITY_EVENTS_SUPPORTED);
        CHECK(result.packet_size == sizeof(caps_response_expected));
        check_bytes(result.packet,
                    caps_response_expected,
                    sizeof(caps_response_expected));
        CHECK(observer.state == AVRCP_OBSERVER_OBSERVING);

        memset(register_command, 0, sizeof(register_command));
        memcpy(register_command,
               register_command_frame,
               sizeof(register_command_frame));
        result = avrcp_observer_handle_packet(
            &observer, register_command, sizeof(register_command));
        CHECK(result.event.kind == AVRCP_OBSERVER_EVENT_VENDOR_COMMAND);
        CHECK(result.event.pdu_id == AVRCP_PDU_REGISTER_NOTIFICATION);
        CHECK(result.event.parameter_size == 5u);
        CHECK(result.event.parameter_bytes[0] == AVRCP_EVENT_VOLUME_CHANGED);
        CHECK(result.packet_size == sizeof(volume_interim_expected));
        check_bytes(result.packet,
                    volume_interim_expected,
                    sizeof(volume_interim_expected));
        CHECK(observer.state == AVRCP_OBSERVER_OBSERVING);

        {
            uint8_t playback_status_command[160];
            memset(playback_status_command, 0,
                   sizeof(playback_status_command));
            memcpy(playback_status_command,
                   playback_status_command_frame,
                   sizeof(playback_status_command_frame));
            result = avrcp_observer_handle_packet(
                &observer,
                playback_status_command,
                sizeof(playback_status_command));
            CHECK(result.event.kind ==
                  AVRCP_OBSERVER_EVENT_VENDOR_COMMAND);
            CHECK(result.event.pdu_id == AVRCP_PDU_REGISTER_NOTIFICATION);
            CHECK(result.event.parameter_bytes[0] ==
                  AVRCP_EVENT_PLAYBACK_STATUS_CHANGED);
            CHECK(result.packet_size ==
                  sizeof(playback_status_interim_expected));
            check_bytes(result.packet,
                        playback_status_interim_expected,
                        sizeof(playback_status_interim_expected));
            CHECK(observer.state == AVRCP_OBSERVER_OBSERVING);
            result = avrcp_observer_submit_playback_status(
                &observer, AVRCP_PLAYBACK_STATUS_PAUSED);
            CHECK(result.packet_size ==
                  sizeof(playback_status_changed_expected));
            check_bytes(result.packet,
                        playback_status_changed_expected,
                        sizeof(playback_status_changed_expected));
            CHECK(observer.playback_notification_active == 0u);
            CHECK(observer.playback_notification_pending == 0u);
        }

        {
            result = avrcp_observer_handle_packet(
                &observer,
                playback_status_reregister_frame,
                sizeof(playback_status_reregister_frame));
            CHECK(result.event.kind ==
                  AVRCP_OBSERVER_EVENT_VENDOR_COMMAND);
            CHECK(result.event.parameter_bytes[0] ==
                  AVRCP_EVENT_PLAYBACK_STATUS_CHANGED);
            CHECK(result.packet_size ==
                  sizeof(playback_status_reregister_interim_expected));
            check_bytes(result.packet,
                        playback_status_reregister_interim_expected,
                        sizeof(playback_status_reregister_interim_expected));
            result = avrcp_observer_submit_write(
                &observer,
                AVRCP_PDU_REGISTER_NOTIFICATION,
                AVRCP_RESPONSE_CHANGED,
                (const uint8_t[]){
                    AVRCP_EVENT_PLAYBACK_STATUS_CHANGED,
                    AVRCP_PLAYBACK_STATUS_PLAYING
                },
                2u);
            CHECK(result.packet_size ==
                  sizeof(playback_status_reregister_changed_expected));
            check_bytes(result.packet,
                        playback_status_reregister_changed_expected,
                        sizeof(playback_status_reregister_changed_expected));
        }

        {
            uint8_t track_changed_command[160];
            memset(track_changed_command, 0, sizeof(track_changed_command));
            memcpy(track_changed_command,
                   track_changed_command_frame,
                   sizeof(track_changed_command_frame));
            result = avrcp_observer_handle_packet(
                &observer,
                track_changed_command,
                sizeof(track_changed_command));
            CHECK(result.event.kind ==
                  AVRCP_OBSERVER_EVENT_VENDOR_COMMAND);
            CHECK(result.event.pdu_id == AVRCP_PDU_REGISTER_NOTIFICATION);
            CHECK(result.event.parameter_bytes[0] ==
                  AVRCP_EVENT_TRACK_CHANGED);
            CHECK(result.packet_size ==
                  sizeof(track_changed_interim_expected));
            check_bytes(result.packet,
                        track_changed_interim_expected,
                        sizeof(track_changed_interim_expected));
            CHECK(observer.state == AVRCP_OBSERVER_OBSERVING);
        }

        {
            uint8_t get_play_status_command[160];
            memset(get_play_status_command, 0,
                   sizeof(get_play_status_command));
            memcpy(get_play_status_command,
                   get_play_status_command_frame,
                   sizeof(get_play_status_command_frame));
            result = avrcp_observer_handle_packet(
                &observer,
                get_play_status_command,
                sizeof(get_play_status_command));
            CHECK(result.event.kind ==
                  AVRCP_OBSERVER_EVENT_VENDOR_COMMAND);
            CHECK(result.event.pdu_id == AVRCP_PDU_GET_PLAY_STATUS);
            CHECK(result.packet_size == sizeof(get_play_status_expected));
            check_bytes(result.packet,
                        get_play_status_expected,
                        sizeof(get_play_status_expected));
            CHECK(observer.state == AVRCP_OBSERVER_OBSERVING);
        }

        {
            uint8_t set_volume_command[160];
            memset(set_volume_command, 0, sizeof(set_volume_command));
            memcpy(set_volume_command,
                   set_volume_command_frame,
                   sizeof(set_volume_command_frame));
            result = avrcp_observer_handle_packet(
                &observer, set_volume_command, sizeof(set_volume_command));
            CHECK(result.event.kind ==
                  AVRCP_OBSERVER_EVENT_VENDOR_COMMAND);
            CHECK(result.event.pdu_id == AVRCP_PDU_SET_ABSOLUTE_VOLUME);
            CHECK(result.event.parameter_size == 1u);
            CHECK(result.event.parameter_bytes[0] == 0x2Au);
            CHECK(result.packet_size == sizeof(set_volume_not_implemented));
            check_bytes(result.packet,
                        set_volume_not_implemented,
                        sizeof(set_volume_not_implemented));
            CHECK(observer.state == AVRCP_OBSERVER_OBSERVING);
        }

        memset(volume_changed, 0, sizeof(volume_changed));
        memcpy(volume_changed,
               volume_changed_frame,
               sizeof(volume_changed_frame));
        result = avrcp_observer_handle_packet(
            &observer, volume_changed, sizeof(volume_changed));
        CHECK(result.event.kind == AVRCP_OBSERVER_EVENT_VOLUME_CHANGED);
        CHECK(result.event.absolute_volume == 0x41u);
        CHECK(result.event.response_code == AVRCP_RESPONSE_CHANGED);
        CHECK(observer.state == AVRCP_OBSERVER_WAIT_VOLUME_INTERIM);
    }
}
static void test_fail_closed_boundaries(void) {
    avrcp_frame frame;
    avrcp_observer observer;
    avrcp_observer_result result;
    const uint8_t fragmented[] = {
        0x04u, 0x11u, 0x0Eu, 0x01u, 0x48u, 0x00u
    };
    const uint8_t wrong_profile[] = {
        0x00u, 0x11u, 0x0Cu, 0x01u, 0x48u, 0x00u
    };
    const uint8_t no_volume[] = {
        0x02u, 0x11u, 0x0Eu, 0x0Cu, 0x48u, 0x00u,
        0x00u, 0x19u, 0x58u, 0x10u, 0x00u, 0x00u, 0x04u,
        0x03u, 0x02u, 0x01u, 0x02u
    };
    CHECK(avrcp_parse_frame(fragmented,
                            sizeof(fragmented),
                            &frame) == AVRCP_UNSUPPORTED_FRAGMENT);
    CHECK(avrcp_parse_frame(wrong_profile,
                            sizeof(wrong_profile),
                            &frame) == AVRCP_PROTOCOL_ERROR);
    CHECK(avrcp_parse_frame(NULL, 0u, &frame) == AVRCP_INVALID_ARGUMENT);

    avrcp_observer_init(&observer);
    (void)avrcp_observer_begin(&observer);
    result = avrcp_observer_handle_packet(
        &observer, no_volume, sizeof(no_volume));
    CHECK(result.event.kind == AVRCP_OBSERVER_EVENT_VOLUME_CAPABILITY);
    CHECK(result.event.volume_supported == 0u);
    CHECK(result.packet_size == 0u);
    CHECK(observer.state == AVRCP_OBSERVER_UNSUPPORTED);
}

int main(void) {
    test_build_and_parse_metadata();
    test_pass_through();
    test_observer_happy_path();
    test_write_path();
    test_padded_zero_frames();
    test_fail_closed_boundaries();
    if (failures == 0) puts("AVRCP protocol observer tests passed.");
    return failures == 0 ? 0 : 1;
}
