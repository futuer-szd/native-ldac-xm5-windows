// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_avrcp_event_queue_contract.h"

#include <string.h>

static int NldAvrcpGenerationIsNewer(ULONGLONG candidate,
                                     ULONGLONG current) {
    ULONGLONG distance;
    if (candidate == 0ull) return 0;
    if (current == 0ull) return 1;
    distance = candidate - current;
    return distance != 0ull && distance < (1ull << 63u);
}

static ULONGLONG NldAvrcpNextNonzero(ULONGLONG value) {
    return value == ~0ull ? 1ull : value + 1ull;
}

static void NldAvrcpEventQueueAppend(
    PNLD_AVRCP_EVENT_QUEUE queue,
    NLD_AVRCP_OBSERVER_EVENT_TYPE type,
    ULONG flags,
    ULONG value0,
    LONG protocol_status,
    ULONG raw_prefix_size,
    const ULONG* raw_prefix_high_words,
    ULONGLONG timestamp_100ns) {
    ULONG index;
    PNLD_AVRCP_OBSERVER_EVENT event;
    if (queue->Count == NLD_AVRCP_EVENT_QUEUE_CAPACITY) {
        queue->Head = (queue->Head + 1u) % NLD_AVRCP_EVENT_QUEUE_CAPACITY;
        queue->Count--;
        queue->DroppedEvents++;
    }
    index = (queue->Head + queue->Count) %
        NLD_AVRCP_EVENT_QUEUE_CAPACITY;
    event = &queue->Events[index];
    memset(event, 0, sizeof(*event));
    event->Size = sizeof(*event);
    event->Type = (ULONG)type;
    event->AclGeneration = queue->AclGeneration;
    event->Sequence = queue->NextSequence;
    queue->NextSequence = NldAvrcpNextNonzero(queue->NextSequence);
    event->Timestamp100ns = timestamp_100ns;
    event->Flags = flags;
    event->Value0 = value0;
    event->ProtocolStatus = protocol_status;
    if (raw_prefix_high_words != NULL) {
        ULONG* raw_words[15];
        unsigned word_index;
        raw_words[0] = &event->RawPrefixHigh;
        raw_words[1] = &event->RawPrefixHigh2;
        raw_words[2] = &event->RawPrefixHigh3;
        raw_words[3] = &event->RawPrefixHigh4;
        raw_words[4] = &event->RawPrefixHigh5;
        raw_words[5] = &event->RawPrefixHigh6;
        raw_words[6] = &event->RawPrefixHigh7;
        raw_words[7] = &event->RawPrefixHigh8;
        raw_words[8] = &event->RawPrefixHigh9;
        raw_words[9] = &event->RawPrefixHigh10;
        raw_words[10] = &event->RawPrefixHigh11;
        raw_words[11] = &event->RawPrefixHigh12;
        raw_words[12] = &event->RawPrefixHigh13;
        raw_words[13] = &event->RawPrefixHigh14;
        raw_words[14] = &event->RawPrefixHigh15;
        for (word_index = 0u; word_index < 15u; ++word_index) {
            *raw_words[word_index] = raw_prefix_high_words[word_index];
        }
    }
    if (raw_prefix_size != 0u && raw_prefix_size <= 64u) {
        event->Flags |= NLD_AVRCP_EVENT_FLAG_RAW_PREFIX |
            (raw_prefix_size << NLD_AVRCP_EVENT_RAW_LENGTH_SHIFT);
    }
    queue->Count++;
}

void NldAvrcpEventQueueInitialize(
    PNLD_AVRCP_EVENT_QUEUE queue) {
    if (queue == NULL) return;
    memset(queue, 0, sizeof(*queue));
    queue->NextSequence = 1ull;
}

int NldAvrcpEventQueueBeginGeneration(
    PNLD_AVRCP_EVENT_QUEUE queue,
    ULONGLONG acl_generation,
    ULONGLONG timestamp_100ns) {
    if (queue == NULL ||
        !NldAvrcpGenerationIsNewer(acl_generation,
                                   queue->AclGeneration)) {
        return 0;
    }
    queue->AclGeneration = acl_generation;
    queue->NextSequence = 1ull;
    queue->Head = 0u;
    queue->Count = 0u;
    queue->DroppedEvents = 0u;
    queue->GenerationCurrent = 1;
    NldAvrcpEventQueueAppend(queue,
                             NldAvrcpObserverEventAclConnected,
                             0u,
                             0u,
                             0,
                             0u,
                             NULL,
                             timestamp_100ns);
    return 1;
}

int NldAvrcpEventQueueEndGeneration(
    PNLD_AVRCP_EVENT_QUEUE queue,
    ULONGLONG acl_generation,
    ULONGLONG timestamp_100ns) {
    if (queue == NULL || !queue->GenerationCurrent ||
        queue->AclGeneration != acl_generation) {
        return 0;
    }
    NldAvrcpEventQueueAppend(queue,
                             NldAvrcpObserverEventAclDisconnected,
                             0u,
                             0u,
                             0,
                             0u,
                             NULL,
                             timestamp_100ns);
    queue->GenerationCurrent = 0;
    return 1;
}

int NldAvrcpEventQueuePush(
    PNLD_AVRCP_EVENT_QUEUE queue,
    ULONGLONG acl_generation,
    NLD_AVRCP_OBSERVER_EVENT_TYPE type,
    ULONG flags,
    ULONG value0,
    LONG protocol_status,
    ULONG raw_prefix_size,
    const ULONG* raw_prefix_high_words,
    ULONGLONG timestamp_100ns) {
    if (queue == NULL || !queue->GenerationCurrent ||
        queue->AclGeneration != acl_generation ||
        type < NldAvrcpObserverEventVolumeCapability ||
        type > NldAvrcpObserverEventWriteResponse) {
        return 0;
    }
    NldAvrcpEventQueueAppend(queue,
                             type,
                             flags,
                             value0,
                             protocol_status,
                             raw_prefix_size,
                             raw_prefix_high_words,
                             timestamp_100ns);
    return 1;
}

int NldAvrcpEventQueuePop(
    PNLD_AVRCP_EVENT_QUEUE queue,
    PNLD_AVRCP_OBSERVER_EVENT event) {
    if (queue == NULL || event == NULL || queue->Count == 0u) return 0;
    *event = queue->Events[queue->Head];
    memset(&queue->Events[queue->Head],
           0,
           sizeof(queue->Events[queue->Head]));
    queue->Head = (queue->Head + 1u) % NLD_AVRCP_EVENT_QUEUE_CAPACITY;
    queue->Count--;
    return 1;
}

void NldAvrcpEventQueueGetStatus(
    const NLD_AVRCP_EVENT_QUEUE* queue,
    PNLD_AVRCP_OBSERVER_STATUS status) {
    if (status == NULL) return;
    memset(status, 0, sizeof(*status));
    status->Size = sizeof(*status);
    if (queue == NULL) return;
    status->AclGeneration = queue->AclGeneration;
    status->QueueDepth = queue->Count;
    status->DroppedEvents = queue->DroppedEvents;
    if (queue->DroppedEvents != 0u) {
        status->Flags |= NLD_AVRCP_OBSERVER_STATUS_QUEUE_OVERFLOW;
    }
}