// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_avrcp_control.h"

#include <string.h>

static unsigned char clamp_percent(unsigned char percent) {
    return percent > 100u ? 100u : percent;
}

unsigned char NldAvrcpControlPercentToXm5(unsigned char percent) {
    const unsigned int clamped = clamp_percent(percent);
    return (unsigned char)((clamped * 127u + 50u) / 100u);
}

unsigned char NldAvrcpControlXm5ToPercent(unsigned char volume) {
    const unsigned int clamped = volume > 127u ? 127u : volume;
    return (unsigned char)((clamped * 100u + 63u) / 127u);
}

void NldAvrcpControlInitialize(NLD_AVRCP_CONTROL_CONTEXT* context) {
    if (context == NULL) return;
    memset(context, 0, sizeof(*context));
    avrcp_observer_init(&context->Observer);
    context->State = NldAvrcpControlOffline;
}

int NldAvrcpControlBegin(NLD_AVRCP_CONTROL_CONTEXT* context,
                         unsigned long generation,
                         unsigned char* packet,
                         unsigned long packet_capacity,
                         unsigned long* packet_size) {
    avrcp_observer_result result;
    if (packet_size != NULL) *packet_size = 0u;
    if (context == NULL || generation == 0u || packet == NULL ||
        packet_size == NULL || context->State != NldAvrcpControlOffline) {
        return 0;
    }
    avrcp_observer_init(&context->Observer);
    result = avrcp_observer_begin(&context->Observer);
    if (result.packet_size == 0u || result.packet_size > packet_capacity) {
        context->State = NldAvrcpControlFailed;
        return 0;
    }
    memcpy(packet, result.packet, result.packet_size);
    *packet_size = (unsigned long)result.packet_size;
    context->Generation = generation;
    context->State = NldAvrcpControlOpening;
    return 1;
}

int NldAvrcpControlHandlePacket(NLD_AVRCP_CONTROL_CONTEXT* context,
                                const unsigned char* packet,
                                unsigned long packet_size,
                                unsigned char* response,
                                unsigned long response_capacity,
                                unsigned long* response_size) {
    avrcp_observer_result result;
    if (response_size != NULL) *response_size = 0u;
    if (context == NULL || packet == NULL || packet_size == 0u ||
        response_size == NULL || context->State == NldAvrcpControlOffline ||
        context->State == NldAvrcpControlFailed) {
        return 0;
    }
    result = avrcp_observer_handle_packet(
        &context->Observer, packet, packet_size);
    if (result.event.kind == AVRCP_OBSERVER_EVENT_PROTOCOL_ERROR) {
        context->State = NldAvrcpControlFailed;
        return 0;
    }
    if (result.event.kind == AVRCP_OBSERVER_EVENT_VOLUME_CHANGED) {
        context->Xm5Volume = result.event.absolute_volume;
        context->Xm5VolumeValid = 1u;
        context->WindowsPercent = NldAvrcpControlXm5ToPercent(
            context->Xm5Volume);
    }
    if (context->Observer.state == AVRCP_OBSERVER_OBSERVING) {
        context->State = NldAvrcpControlObserving;
    }
    if (result.packet_size != 0u) {
        if (response == NULL || result.packet_size > response_capacity) {
            context->State = NldAvrcpControlFailed;
            return 0;
        }
        memcpy(response, result.packet, result.packet_size);
        *response_size = (unsigned long)result.packet_size;
    }
    context->WriteInFlight = context->Observer.write_active != 0u;
    return 1;
}

int NldAvrcpControlSetWindowsPercent(NLD_AVRCP_CONTROL_CONTEXT* context,
                                     unsigned char percent,
                                     unsigned char* packet,
                                     unsigned long packet_capacity,
                                     unsigned long* packet_size) {
    avrcp_observer_result result;
    const unsigned char volume = NldAvrcpControlPercentToXm5(percent);
    if (packet_size != NULL) *packet_size = 0u;
    if (context == NULL || packet_size == NULL ||
        context->State != NldAvrcpControlObserving) {
        return 0;
    }
    context->WindowsPercent = clamp_percent(percent);
    context->PendingWindowsPercent = context->WindowsPercent;
    context->WindowsVolumePending = 1u;
    result = avrcp_observer_submit_write(
        &context->Observer, AVRCP_PDU_SET_ABSOLUTE_VOLUME, 0u,
        &volume, 1u);
    if (result.packet_size == 0u) {
        context->WriteInFlight = context->Observer.write_active != 0u;
        return 1;
    }
    if (packet == NULL || result.packet_size > packet_capacity) return 0;
    memcpy(packet, result.packet, result.packet_size);
    *packet_size = (unsigned long)result.packet_size;
    context->WindowsVolumePending = 0u;
    context->WriteInFlight = 1u;
    return 1;
}

int NldAvrcpControlIsReady(const NLD_AVRCP_CONTROL_CONTEXT* context) {
    return context != NULL &&
        context->State == NldAvrcpControlObserving &&
        context->Observer.volume_supported != 0u &&
        context->Xm5VolumeValid != 0u;
}
