// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_AVRCP_EVENT_QUEUE_CONTRACT_H
#define NATIVE_LDAC_AVRCP_EVENT_QUEUE_CONTRACT_H

#include "nativeldac_avrcp_observer_ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NLD_AVRCP_EVENT_QUEUE_CAPACITY 64u

typedef struct _NLD_AVRCP_EVENT_QUEUE {
    NLD_AVRCP_OBSERVER_EVENT Events[NLD_AVRCP_EVENT_QUEUE_CAPACITY];
    ULONGLONG AclGeneration;
    ULONGLONG NextSequence;
    ULONG Head;
    ULONG Count;
    ULONG DroppedEvents;
    int GenerationCurrent;
} NLD_AVRCP_EVENT_QUEUE, *PNLD_AVRCP_EVENT_QUEUE;

void NldAvrcpEventQueueInitialize(
    PNLD_AVRCP_EVENT_QUEUE queue);

int NldAvrcpEventQueueBeginGeneration(
    PNLD_AVRCP_EVENT_QUEUE queue,
    ULONGLONG acl_generation,
    ULONGLONG timestamp_100ns);

int NldAvrcpEventQueueEndGeneration(
    PNLD_AVRCP_EVENT_QUEUE queue,
    ULONGLONG acl_generation,
    ULONGLONG timestamp_100ns);

int NldAvrcpEventQueuePush(
    PNLD_AVRCP_EVENT_QUEUE queue,
    ULONGLONG acl_generation,
    NLD_AVRCP_OBSERVER_EVENT_TYPE type,
    ULONG flags,
    ULONG value0,
    LONG protocol_status,
    ULONG raw_prefix_size,
    const ULONG* raw_prefix_high_words,
    ULONGLONG timestamp_100ns);

int NldAvrcpEventQueuePop(
    PNLD_AVRCP_EVENT_QUEUE queue,
    PNLD_AVRCP_OBSERVER_EVENT event);

void NldAvrcpEventQueueGetStatus(
    const NLD_AVRCP_EVENT_QUEUE* queue,
    PNLD_AVRCP_OBSERVER_STATUS status);

#ifdef __cplusplus
}
#endif

#endif
