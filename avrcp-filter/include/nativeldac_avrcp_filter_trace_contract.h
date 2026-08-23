// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_AVRCP_FILTER_TRACE_CONTRACT_H
#define NATIVE_LDAC_AVRCP_FILTER_TRACE_CONTRACT_H

#include "nativeldac_avrcp_filter_ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NLD_AVRCP_FILTER_TRACE_CAPACITY 128u

typedef struct _NLD_AVRCP_FILTER_TRACE_QUEUE {
    NLD_AVRCP_FILTER_EVENT Events[NLD_AVRCP_FILTER_TRACE_CAPACITY];
    ULONGLONG NextSequence;
    ULONGLONG NextRequestId;
    ULONGLONG RequestsObserved;
    ULONGLONG CompletionsObserved;
    ULONGLONG CaptureFailures;
    ULONG Head;
    ULONG Count;
    ULONG DroppedEvents;
    LONG LastCompletionStatus;
} NLD_AVRCP_FILTER_TRACE_QUEUE,
  *PNLD_AVRCP_FILTER_TRACE_QUEUE;

void NldAvrcpFilterTraceInitialize(
    PNLD_AVRCP_FILTER_TRACE_QUEUE queue);

ULONGLONG NldAvrcpFilterTraceAllocateRequestId(
    PNLD_AVRCP_FILTER_TRACE_QUEUE queue);

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
    ULONGLONG timestamp_100ns);

int NldAvrcpFilterTracePop(
    PNLD_AVRCP_FILTER_TRACE_QUEUE queue,
    PNLD_AVRCP_FILTER_EVENT event);

void NldAvrcpFilterTraceGetStatus(
    const NLD_AVRCP_FILTER_TRACE_QUEUE* queue,
    ULONG runtime_flags,
    PNLD_AVRCP_FILTER_STATUS status);

#ifdef __cplusplus
}
#endif

#endif
