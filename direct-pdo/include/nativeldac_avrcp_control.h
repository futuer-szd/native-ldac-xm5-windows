// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_AVRCP_CONTROL_H
#define NATIVE_LDAC_AVRCP_CONTROL_H

#include "ldac_native/avrcp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NLD_AVRCP_CONTROL_STATE {
    NldAvrcpControlOffline = 0,
    NldAvrcpControlOpening = 1,
    NldAvrcpControlObserving = 2,
    NldAvrcpControlFailed = 3
} NLD_AVRCP_CONTROL_STATE;

typedef struct NLD_AVRCP_CONTROL_CONTEXT {
    avrcp_observer Observer;
    NLD_AVRCP_CONTROL_STATE State;
    unsigned long Generation;
    unsigned char WindowsPercent;
    unsigned char Xm5Volume;
    unsigned char Xm5VolumeValid;
    unsigned char WindowsVolumePending;
    unsigned char PendingWindowsPercent;
    unsigned char WriteInFlight;
} NLD_AVRCP_CONTROL_CONTEXT;

void NldAvrcpControlInitialize(
    NLD_AVRCP_CONTROL_CONTEXT* context);

int NldAvrcpControlBegin(
    NLD_AVRCP_CONTROL_CONTEXT* context,
    unsigned long generation,
    unsigned char* packet,
    unsigned long packet_capacity,
    unsigned long* packet_size);

int NldAvrcpControlHandlePacket(
    NLD_AVRCP_CONTROL_CONTEXT* context,
    const unsigned char* packet,
    unsigned long packet_size,
    unsigned char* response,
    unsigned long response_capacity,
    unsigned long* response_size);

int NldAvrcpControlSetWindowsPercent(
    NLD_AVRCP_CONTROL_CONTEXT* context,
    unsigned char percent,
    unsigned char* packet,
    unsigned long packet_capacity,
    unsigned long* packet_size);

int NldAvrcpControlIsReady(
    const NLD_AVRCP_CONTROL_CONTEXT* context);

unsigned char NldAvrcpControlPercentToXm5(unsigned char percent);
unsigned char NldAvrcpControlXm5ToPercent(unsigned char volume);

#ifdef __cplusplus
}
#endif

#endif
