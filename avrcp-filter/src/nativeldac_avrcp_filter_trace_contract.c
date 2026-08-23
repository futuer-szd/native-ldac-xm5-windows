// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_avrcp_filter_trace_contract.h"

#include <string.h>

static ULONGLONG NldAvrcpFilterNextNonzero(ULONGLONG value) {
    return value == ~0ull ? 1ull : value + 1ull;
}

void NldAvrcpFilterTraceInitialize(
    PNLD_AVRCP_FILTER_TRACE_QUEUE queue) {
    if (queue == NULL) return;
    memset(queue, 0, sizeof(*queue));
    queue->NextSequence = 1ull;
    queue->NextRequestId = 1ull;
}

ULONGLONG NldAvrcpFilterTraceAllocateRequestId(
    PNLD_AVRCP_FILTER_TRACE_QUEUE queue) {
    ULONGLONG request_id;
    if (queue == NULL) return 0ull;
    request_id = queue->NextRequestId;
    queue->NextRequestId = NldAvrcpFilterNextNonzero(
        queue->NextRequestId);
    return request_id;
}

void NldAvrcpFilterTracePush(
    PNLD_AVRCP_FILTER_TRACE_QUEUE queue,
    NLD_AVRCP_FILTER_EVENT_TYPE type,
    ULONGLONG request_id,
    ULONG flags,
    ULONG control_code,
    ULONG input_size,
    ULONG output_size,
    LONG status,
    ULONGLONG information,
    const void* raw_prefix,
    ULONG raw_size,
    ULONGLONG timestamp_100ns) {
    ULONG index;
    PNLD_AVRCP_FILTER_EVENT event;
    ULONG bounded_raw_size;
    if (queue == NULL ||
        type < NldAvrcpFilterEventLifecycle ||
        type > NldAvrcpFilterEventCaptureFailure) {
        return;
    }
    if (queue->Count == NLD_AVRCP_FILTER_TRACE_CAPACITY) {
        queue->Head = (queue->Head + 1u) %
            NLD_AVRCP_FILTER_TRACE_CAPACITY;
        queue->Count--;
        queue->DroppedEvents++;
    }
    index = (queue->Head + queue->Count) %
        NLD_AVRCP_FILTER_TRACE_CAPACITY;
    event = &queue->Events[index];
    memset(event, 0, sizeof(*event));
    event->Size = sizeof(*event);
    event->Type = (ULONG)type;
    event->Sequence = queue->NextSequence;
    queue->NextSequence = NldAvrcpFilterNextNonzero(
        queue->NextSequence);
    event->RequestId = request_id;
    event->Timestamp100ns = timestamp_100ns;
    event->Flags = flags;
    event->ControlCode = control_code;
    event->InputSize = input_size;
    event->OutputSize = output_size;
    event->Status = status;
    event->Information = information;
    bounded_raw_size = raw_size;
    if (bounded_raw_size > NLD_AVRCP_FILTER_RAW_PREFIX_CAPACITY) {
        bounded_raw_size = NLD_AVRCP_FILTER_RAW_PREFIX_CAPACITY;
    }
    if (raw_prefix != NULL && bounded_raw_size != 0u) {
        memcpy(event->RawPrefix, raw_prefix, bounded_raw_size);
        event->RawSize = bounded_raw_size;
    }
    if (type == NldAvrcpFilterEventRequest) {
        queue->RequestsObserved++;
    } else if (type == NldAvrcpFilterEventCompletion) {
        queue->CompletionsObserved++;
        queue->LastCompletionStatus = status;
    } else if (type == NldAvrcpFilterEventCaptureFailure) {
        queue->CaptureFailures++;
    }
    queue->Count++;
}

int NldAvrcpFilterTracePop(
    PNLD_AVRCP_FILTER_TRACE_QUEUE queue,
    PNLD_AVRCP_FILTER_EVENT event) {
    if (queue == NULL || event == NULL || queue->Count == 0u) return 0;
    *event = queue->Events[queue->Head];
    memset(&queue->Events[queue->Head],
           0,
           sizeof(queue->Events[queue->Head]));
    queue->Head = (queue->Head + 1u) % NLD_AVRCP_FILTER_TRACE_CAPACITY;
    queue->Count--;
    return 1;
}

void NldAvrcpFilterTraceGetStatus(
    const NLD_AVRCP_FILTER_TRACE_QUEUE* queue,
    ULONG runtime_flags,
    PNLD_AVRCP_FILTER_STATUS status) {
    if (status == NULL) return;
    memset(status, 0, sizeof(*status));
    status->Size = sizeof(*status);
    status->Flags = runtime_flags;
    if (queue == NULL) return;
    status->QueueDepth = queue->Count;
    status->DroppedEvents = queue->DroppedEvents;
    status->NextSequence = queue->NextSequence;
    status->NextRequestId = queue->NextRequestId;
    status->RequestsObserved = queue->RequestsObserved;
    status->CompletionsObserved = queue->CompletionsObserved;
    status->CaptureFailures = queue->CaptureFailures;
    status->LastCompletionStatus = queue->LastCompletionStatus;
    if (queue->DroppedEvents != 0u) {
        status->Flags |= NLD_AVRCP_FILTER_STATUS_QUEUE_OVERFLOW;
    }
}
