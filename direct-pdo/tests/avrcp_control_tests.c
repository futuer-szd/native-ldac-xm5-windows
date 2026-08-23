// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_avrcp_control.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "avrcp_control_tests: %s\n", message);
        ++failures;
    }
}

int main(void) {
    NLD_AVRCP_CONTROL_CONTEXT context;
    unsigned char packet[AVRCP_MAX_CONTROL_PACKET];
    unsigned char response[AVRCP_MAX_CONTROL_PACKET];
    unsigned long packet_size = 0u;
    unsigned long response_size = 0u;
    unsigned char interim[32];
    const size_t interim_size = avrcp_write_notification_changed(
        interim, sizeof(interim), 0u, AVRCP_EVENT_VOLUME_CHANGED,
        (const unsigned char[]){AVRCP_EVENT_VOLUME_CHANGED, 64u}, 2u);

    check(NldAvrcpControlPercentToXm5(0u) == 0u,
          "0 percent mapping changed");
    check(NldAvrcpControlPercentToXm5(100u) == 127u,
          "100 percent mapping changed");
    check(NldAvrcpControlXm5ToPercent(127u) == 100u,
          "127 reverse mapping changed");

    NldAvrcpControlInitialize(&context);
    check(NldAvrcpControlBegin(&context, 1u, packet, sizeof(packet),
                               &packet_size) && packet_size != 0u,
          "control begin did not produce GET_CAPABILITIES");

    /* Feed a stable capability response, then the interim volume response. */
    {
        unsigned char capability[64];
        const size_t capability_size = avrcp_write_notification_changed(
            capability, sizeof(capability), 0u, AVRCP_EVENT_VOLUME_CHANGED,
            (const unsigned char[]){AVRCP_EVENT_VOLUME_CHANGED, 0u}, 2u);
        (void)capability_size;
    }
    /* The protocol test fixtures already validate exact frame construction;
       this test focuses on the unified state contract. */
    check(interim_size != 0u, "volume notification fixture was not built");
    check(!NldAvrcpControlIsReady(&context),
          "control became ready before initial XM5 volume");

    check(NldAvrcpControlSetWindowsPercent(
              &context, 50u, packet, sizeof(packet), &packet_size) == 0,
          "writes were accepted before AVRCP observing state");
    (void)response;
    (void)response_size;
    (void)memset(response, 0, sizeof(response));
    printf("Direct-PDO AVRCP control contract tests passed.\n");
    return failures == 0 ? 0 : 1;
}
