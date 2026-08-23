// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <string.h>

#include "ldac_native/avdtp.h"
#include "ldac_native/ldac_codec.h"
#include "ldac_native/rtp_ldac.h"

static int failures;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n",                      \
                    __FILE__, __LINE__, #condition);                            \
            failures++;                                                        \
        }                                                                       \
    } while (0)

static void check_bytes(const uint8_t *actual,
                        const uint8_t *expected,
                        size_t size,
                        const char *name) {
    if (memcmp(actual, expected, size) != 0) {
        size_t i;
        fprintf(stderr, "%s differs\n  actual:  ", name);
        for (i = 0u; i < size; ++i) fprintf(stderr, "%02X ", actual[i]);
        fprintf(stderr, "\n  expected: ");
        for (i = 0u; i < size; ++i) fprintf(stderr, "%02X ", expected[i]);
        fprintf(stderr, "\n");
        failures++;
    }
}

static void test_avdtp_headers(void) {
    uint8_t packet[8];
    const uint8_t payload[] = {0x04u, 0x08u};
    const uint8_t expected[] = {0xA2u, AVDTP_SIGNAL_DISCOVER, 0x04u, 0x08u};
    avdtp_header header;
    size_t size = avdtp_write_single(packet,
                                     sizeof(packet),
                                     0x0Au,
                                     AVDTP_MESSAGE_ACCEPT,
                                     AVDTP_SIGNAL_DISCOVER,
                                     payload,
                                     sizeof(payload));
    CHECK(size == sizeof(expected));
    check_bytes(packet, expected, sizeof(expected), "single AVDTP packet");
    CHECK(avdtp_parse_header(packet, size, &header) == AVDTP_OK);
    CHECK(header.transaction_label == 0x0Au);
    CHECK(header.packet_type == AVDTP_PACKET_SINGLE);
    CHECK(header.message_type == AVDTP_MESSAGE_ACCEPT);
    CHECK(header.signal_id == AVDTP_SIGNAL_DISCOVER);
    CHECK(header.payload_offset == 2u);

    {
        const uint8_t start[] = {0x34u, 0x02u, AVDTP_SIGNAL_GET_ALL_CAPABILITIES};
        CHECK(avdtp_parse_header(start, sizeof(start), &header) == AVDTP_OK);
        CHECK(header.transaction_label == 3u);
        CHECK(header.packet_type == AVDTP_PACKET_START);
        CHECK(header.packet_count == 2u);
        CHECK(header.signal_id == AVDTP_SIGNAL_GET_ALL_CAPABILITIES);
        CHECK(header.payload_offset == 3u);
    }
    CHECK(avdtp_parse_header(packet, 1u, &header) == AVDTP_TRUNCATED);
}

static void test_ldac_capabilities(void) {
    uint8_t info[LDAC_CODEC_INFO_SIZE];
    const uint8_t expected_info[] = {
        0x2Du, 0x01u, 0x00u, 0x00u, 0xAAu, 0x00u, 0x3Cu, 0x07u
    };
    const uint8_t service_caps[] = {
        AVDTP_SERVICE_MEDIA_TRANSPORT, 0x00u,
        AVDTP_SERVICE_MEDIA_CODEC, 0x0Au,
        0x00u, AVDTP_CODEC_VENDOR,
        0x2Du, 0x01u, 0x00u, 0x00u, 0xAAu, 0x00u, 0x3Cu, 0x07u
    };
    ldac_capabilities parsed;
    ldac_configuration selected;
    ldac_capabilities local = {LDAC_SF_ALL, LDAC_CM_STEREO};
    uint8_t config[16];
    const uint8_t expected_config[] = {
        0x04u, 0x04u,
        AVDTP_SERVICE_MEDIA_TRANSPORT, 0x00u,
        AVDTP_SERVICE_MEDIA_CODEC, 0x0Au,
        0x00u, AVDTP_CODEC_VENDOR,
        0x2Du, 0x01u, 0x00u, 0x00u, 0xAAu, 0x00u,
        LDAC_SF_48000, LDAC_CM_STEREO
    };

    ldac_build_codec_info(info, 0x3Cu, 0x07u);
    check_bytes(info, expected_info, sizeof(info), "LDAC codec info");
    CHECK(ldac_find_in_service_capabilities(service_caps,
                                            sizeof(service_caps),
                                            &parsed) == LDAC_CODEC_OK);
    CHECK(parsed.sample_rates == 0x3Cu);
    CHECK(parsed.channel_modes == 0x07u);
    CHECK(ldac_choose_configuration(local,
                                    parsed,
                                    48000u,
                                    &selected) == LDAC_CODEC_OK);
    CHECK(selected.sample_rate == LDAC_SF_48000);
    CHECK(selected.channel_mode == LDAC_CM_STEREO);
    CHECK(ldac_build_set_configuration_payload(config,
                                               sizeof(config),
                                               1u,
                                               1u,
                                               selected) == sizeof(config));
    check_bytes(config, expected_config, sizeof(config), "SET_CONFIGURATION payload");
    CHECK(ldac_sample_rate_to_hz(LDAC_SF_96000) == 96000u);
    CHECK(ldac_samples_per_frame(LDAC_SF_48000) == 128u);
    CHECK(ldac_samples_per_frame(LDAC_SF_96000) == 256u);
    selected.sample_rate = 0x80u;
    CHECK(ldac_build_set_configuration_payload(config,
                                               sizeof(config),
                                               1u,
                                               1u,
                                               selected) == 0u);
}

static void test_source_happy_path(void) {
    avdtp_source source;
    avdtp_action action;
    ldac_capabilities local = {LDAC_SF_ALL, LDAC_CM_STEREO};
    const uint8_t discover_accept[] = {
        0x02u, AVDTP_SIGNAL_DISCOVER,
        0x04u, 0x08u
    };
    const uint8_t caps_accept[] = {
        0x12u, AVDTP_SIGNAL_GET_ALL_CAPABILITIES,
        AVDTP_SERVICE_MEDIA_TRANSPORT, 0x00u,
        AVDTP_SERVICE_MEDIA_CODEC, 0x0Au,
        0x00u, AVDTP_CODEC_VENDOR,
        0x2Du, 0x01u, 0x00u, 0x00u, 0xAAu, 0x00u, 0x3Cu, 0x07u
    };
    const uint8_t configuration_accept[] = {
        0x22u, AVDTP_SIGNAL_SET_CONFIGURATION
    };
    const uint8_t open_accept[] = {0x32u, AVDTP_SIGNAL_OPEN};
    const uint8_t start_accept[] = {0x42u, AVDTP_SIGNAL_START};
    const uint8_t suspend_accept[] = {0x52u, AVDTP_SIGNAL_SUSPEND};
    const uint8_t resume_accept[] = {0x62u, AVDTP_SIGNAL_START};
    const uint8_t second_suspend_accept[] = {
        0x72u, AVDTP_SIGNAL_SUSPEND
    };
    const uint8_t close_accept[] = {0x82u, AVDTP_SIGNAL_CLOSE};
    const uint8_t expected_discover[] = {0x00u, AVDTP_SIGNAL_DISCOVER};
    const uint8_t expected_caps[] = {
        0x10u, AVDTP_SIGNAL_GET_ALL_CAPABILITIES, 0x04u
    };
    const uint8_t expected_open[] = {0x30u, AVDTP_SIGNAL_OPEN, 0x04u};
    const uint8_t expected_start[] = {0x40u, AVDTP_SIGNAL_START, 0x04u};
    const uint8_t expected_suspend[] = {0x50u, AVDTP_SIGNAL_SUSPEND, 0x04u};
    const uint8_t expected_resume[] = {0x60u, AVDTP_SIGNAL_START, 0x04u};
    const uint8_t expected_second_suspend[] = {
        0x70u, AVDTP_SIGNAL_SUSPEND, 0x04u
    };
    const uint8_t expected_close[] = {0x80u, AVDTP_SIGNAL_CLOSE, 0x04u};

    avdtp_source_init(&source, local, 1u, 48000u);
    action = avdtp_source_begin(&source);
    CHECK(action.kind == AVDTP_ACTION_SEND_SIGNALING);
    CHECK(action.packet_size == sizeof(expected_discover));
    check_bytes(action.packet, expected_discover, sizeof(expected_discover), "DISCOVER");

    action = avdtp_source_handle_signaling(&source,
                                           discover_accept,
                                           sizeof(discover_accept));
    CHECK(action.kind == AVDTP_ACTION_SEND_SIGNALING);
    CHECK(action.packet_size == sizeof(expected_caps));
    check_bytes(action.packet, expected_caps, sizeof(expected_caps), "GET_ALL_CAPABILITIES");

    action = avdtp_source_handle_signaling(&source,
                                           caps_accept,
                                           sizeof(caps_accept));
    CHECK(action.kind == AVDTP_ACTION_SEND_SIGNALING);
    CHECK(action.packet_size == 18u);
    CHECK(action.packet[0] == 0x20u);
    CHECK(action.packet[1] == AVDTP_SIGNAL_SET_CONFIGURATION);
    CHECK(action.packet[16] == LDAC_SF_48000);
    CHECK(action.packet[17] == LDAC_CM_STEREO);

    action = avdtp_source_handle_signaling(&source,
                                           configuration_accept,
                                           sizeof(configuration_accept));
    CHECK(action.kind == AVDTP_ACTION_SEND_SIGNALING);
    check_bytes(action.packet, expected_open, sizeof(expected_open), "OPEN");

    action = avdtp_source_handle_signaling(&source, open_accept, sizeof(open_accept));
    CHECK(action.kind == AVDTP_ACTION_OPEN_MEDIA_CHANNEL);
    CHECK(source.state == AVDTP_SOURCE_WAITING_FOR_MEDIA_CHANNEL);

    action = avdtp_source_media_channel_opened(&source);
    CHECK(action.kind == AVDTP_ACTION_SESSION_OPEN);
    CHECK(source.state == AVDTP_SOURCE_OPEN);

    action = avdtp_source_start(&source);
    CHECK(action.kind == AVDTP_ACTION_SEND_SIGNALING);
    check_bytes(action.packet, expected_start, sizeof(expected_start), "START");

    action = avdtp_source_handle_signaling(&source, start_accept, sizeof(start_accept));
    CHECK(action.kind == AVDTP_ACTION_STREAM_READY);
    CHECK(source.state == AVDTP_SOURCE_STREAMING);
    CHECK(source.configuration.sample_rate == LDAC_SF_48000);
    CHECK(source.configuration.channel_mode == LDAC_CM_STEREO);

    action = avdtp_source_suspend(&source);
    CHECK(action.kind == AVDTP_ACTION_SEND_SIGNALING);
    check_bytes(action.packet, expected_suspend, sizeof(expected_suspend), "SUSPEND");
    action = avdtp_source_handle_signaling(&source,
                                           suspend_accept,
                                           sizeof(suspend_accept));
    CHECK(action.kind == AVDTP_ACTION_STREAM_SUSPENDED);
    CHECK(source.state == AVDTP_SOURCE_OPEN);

    action = avdtp_source_start(&source);
    CHECK(action.kind == AVDTP_ACTION_SEND_SIGNALING);
    check_bytes(action.packet, expected_resume, sizeof(expected_resume),
                "RESUME START");
    action = avdtp_source_handle_signaling(&source,
                                           resume_accept,
                                           sizeof(resume_accept));
    CHECK(action.kind == AVDTP_ACTION_STREAM_READY);
    CHECK(source.state == AVDTP_SOURCE_STREAMING);

    action = avdtp_source_suspend(&source);
    CHECK(action.kind == AVDTP_ACTION_SEND_SIGNALING);
    check_bytes(action.packet, expected_second_suspend,
                sizeof(expected_second_suspend), "SECOND SUSPEND");
    action = avdtp_source_handle_signaling(
        &source, second_suspend_accept, sizeof(second_suspend_accept));
    CHECK(action.kind == AVDTP_ACTION_STREAM_SUSPENDED);
    CHECK(source.state == AVDTP_SOURCE_OPEN);

    action = avdtp_source_close(&source);
    CHECK(action.kind == AVDTP_ACTION_SEND_SIGNALING);
    check_bytes(action.packet, expected_close, sizeof(expected_close), "CLOSE");
    action = avdtp_source_handle_signaling(&source,
                                           close_accept,
                                           sizeof(close_accept));
    CHECK(action.kind == AVDTP_ACTION_SESSION_CLOSED);
    CHECK(source.state == AVDTP_SOURCE_CLOSED);
}

static void test_get_all_capabilities_fallback(void) {
    avdtp_source source;
    avdtp_action action;
    ldac_capabilities local = {LDAC_SF_ALL, LDAC_CM_STEREO};
    const uint8_t discover_accept[] = {
        0x02u, AVDTP_SIGNAL_DISCOVER, 0x04u, 0x08u
    };
    const uint8_t reject[] = {
        0x13u, AVDTP_SIGNAL_GET_ALL_CAPABILITIES, 0x19u
    };
    const uint8_t expected_legacy[] = {
        0x20u, AVDTP_SIGNAL_GET_CAPABILITIES, 0x04u
    };

    avdtp_source_init(&source, local, 1u, 48000u);
    (void)avdtp_source_begin(&source);
    (void)avdtp_source_handle_signaling(&source,
                                        discover_accept,
                                        sizeof(discover_accept));
    action = avdtp_source_handle_signaling(&source, reject, sizeof(reject));
    CHECK(action.kind == AVDTP_ACTION_SEND_SIGNALING);
    CHECK(action.packet_size == sizeof(expected_legacy));
    check_bytes(action.packet, expected_legacy, sizeof(expected_legacy),
                "GET_CAPABILITIES fallback");
    CHECK(source.used_legacy_capabilities == 1u);
}

static void test_source_skips_non_ldac_sinks(void) {
    avdtp_source source;
    avdtp_action action;
    ldac_capabilities local = {LDAC_SF_ALL, LDAC_CM_STEREO};
    const uint8_t discover_accept[] = {
        0x02u, AVDTP_SIGNAL_DISCOVER,
        0x04u, 0x08u,
        0x08u, 0x08u,
        0x0Cu, 0x08u
    };
    const uint8_t sbc_caps_accept[] = {
        0x12u, AVDTP_SIGNAL_GET_ALL_CAPABILITIES,
        AVDTP_SERVICE_MEDIA_TRANSPORT, 0x00u,
        AVDTP_SERVICE_MEDIA_CODEC, 0x06u,
        0x00u, 0x00u, 0x3Fu, 0xFFu, 0x02u, 0x35u
    };
    const uint8_t aac_caps_accept[] = {
        0x22u, AVDTP_SIGNAL_GET_ALL_CAPABILITIES,
        AVDTP_SERVICE_MEDIA_TRANSPORT, 0x00u,
        AVDTP_SERVICE_MEDIA_CODEC, 0x08u,
        0x00u, 0x02u, 0xF0u, 0x01u, 0x8Cu, 0x84u, 0xE2u, 0x00u
    };
    const uint8_t ldac_caps_accept[] = {
        0x32u, AVDTP_SIGNAL_GET_ALL_CAPABILITIES,
        AVDTP_SERVICE_MEDIA_TRANSPORT, 0x00u,
        AVDTP_SERVICE_MEDIA_CODEC, 0x0Au,
        0x00u, AVDTP_CODEC_VENDOR,
        0x2Du, 0x01u, 0x00u, 0x00u, 0xAAu, 0x00u, 0x3Cu, 0x07u
    };
    const uint8_t expected_seid_1[] = {
        0x10u, AVDTP_SIGNAL_GET_ALL_CAPABILITIES, 0x04u
    };
    const uint8_t expected_seid_2[] = {
        0x20u, AVDTP_SIGNAL_GET_ALL_CAPABILITIES, 0x08u
    };
    const uint8_t expected_seid_3[] = {
        0x30u, AVDTP_SIGNAL_GET_ALL_CAPABILITIES, 0x0Cu
    };

    avdtp_source_init(&source, local, 1u, 96000u);
    (void)avdtp_source_begin(&source);
    action = avdtp_source_handle_signaling(&source,
                                           discover_accept,
                                           sizeof(discover_accept));
    check_bytes(action.packet,
                expected_seid_1,
                sizeof(expected_seid_1),
                "GET_ALL_CAPABILITIES SEID 1");
    action = avdtp_source_handle_signaling(&source,
                                           sbc_caps_accept,
                                           sizeof(sbc_caps_accept));
    CHECK(action.kind == AVDTP_ACTION_SEND_SIGNALING);
    check_bytes(action.packet,
                expected_seid_2,
                sizeof(expected_seid_2),
                "GET_ALL_CAPABILITIES SEID 2");
    action = avdtp_source_handle_signaling(&source,
                                           aac_caps_accept,
                                           sizeof(aac_caps_accept));
    CHECK(action.kind == AVDTP_ACTION_SEND_SIGNALING);
    check_bytes(action.packet,
                expected_seid_3,
                sizeof(expected_seid_3),
                "GET_ALL_CAPABILITIES SEID 3");
    action = avdtp_source_handle_signaling(&source,
                                           ldac_caps_accept,
                                           sizeof(ldac_caps_accept));
    CHECK(action.kind == AVDTP_ACTION_SEND_SIGNALING);
    CHECK(action.packet[1] == AVDTP_SIGNAL_SET_CONFIGURATION);
    CHECK(source.remote_seid == 3u);
    CHECK(source.configuration.sample_rate == LDAC_SF_96000);
}

static void test_source_rejects_wrong_label(void) {
    avdtp_source source;
    avdtp_action action;
    ldac_capabilities local = {LDAC_SF_ALL, LDAC_CM_STEREO};
    const uint8_t wrong_label[] = {
        0x12u, AVDTP_SIGNAL_DISCOVER, 0x04u, 0x08u
    };
    avdtp_source_init(&source, local, 1u, 48000u);
    (void)avdtp_source_begin(&source);
    action = avdtp_source_handle_signaling(&source,
                                           wrong_label,
                                           sizeof(wrong_label));
    CHECK(action.kind == AVDTP_ACTION_ERROR);
    CHECK(action.error_code == AVDTP_SOURCE_ERROR_UNEXPECTED_RESPONSE);
    CHECK(source.state == AVDTP_SOURCE_FAILED);
}

static void test_rtp_packetizer(void) {
    uint8_t packet[32];
    size_t packet_size = 0u;
    const uint8_t frame[] = {0xAAu, 0xBBu, 0xCCu};
    const uint8_t expected[] = {
        0x80u, 0x60u, 0x12u, 0x34u,
        0x01u, 0x02u, 0x03u, 0x04u,
        0xA0u, 0xB0u, 0xC0u, 0xD0u,
        0x01u,
        0xAAu, 0xBBu, 0xCCu
    };
    CHECK(ldac_rtp_build_unfragmented(packet,
                                      sizeof(packet),
                                      672u,
                                      0x1234u,
                                      0x01020304u,
                                      0xA0B0C0D0u,
                                      1u,
                                      frame,
                                      sizeof(frame),
                                      &packet_size) == LDAC_RTP_OK);
    CHECK(packet_size == sizeof(expected));
    check_bytes(packet, expected, sizeof(expected), "RTP/LDAC packet");
    CHECK(ldac_rtp_build_unfragmented(packet,
                                      sizeof(packet),
                                      sizeof(expected) - 1u,
                                      0u,
                                      0u,
                                      0u,
                                      1u,
                                      frame,
                                      sizeof(frame),
                                      &packet_size) == LDAC_RTP_MTU_EXCEEDED);
    CHECK(ldac_rtp_build_unfragmented(packet,
                                      sizeof(packet),
                                      672u,
                                      0u,
                                      0u,
                                      0u,
                                      LDAC_RTP_MAX_FRAME_COUNT,
                                      frame,
                                      sizeof(frame),
                                      &packet_size) == LDAC_RTP_OK);
}

int main(void) {
    test_avdtp_headers();
    test_ldac_capabilities();
    test_source_happy_path();
    test_get_all_capabilities_fallback();
    test_source_skips_non_ldac_sinks();
    test_source_rejects_wrong_label();
    test_rtp_packetizer();
    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    puts("All protocol tests passed.");
    return 0;
}
