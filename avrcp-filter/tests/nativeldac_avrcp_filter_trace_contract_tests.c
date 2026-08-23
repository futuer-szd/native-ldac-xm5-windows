// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "nativeldac_avrcp_filter_trace_contract.h"

static int failures;

_Static_assert(NLD_AVRCP_FILTER_ABI_MAJOR == 0u, "ABI major changed");
_Static_assert(NLD_AVRCP_FILTER_ABI_MINOR == 2u, "ABI minor changed");
_Static_assert(sizeof(NLD_AVRCP_FILTER_ABI_VERSION) == 16u,
               "version ABI size changed");
_Static_assert(sizeof(NLD_AVRCP_FILTER_STATUS) == 64u,
               "status ABI size changed");
_Static_assert(sizeof(NLD_AVRCP_FILTER_EVENT) == 96u,
               "event ABI size changed");
_Static_assert((IOCTL_NLD_AVRCP_FILTER_GET_VERSION & 3u) ==
                   METHOD_BUFFERED,
               "version method changed");
_Static_assert((IOCTL_NLD_AVRCP_FILTER_GET_STATUS & 3u) ==
                   METHOD_BUFFERED,
               "status method changed");
_Static_assert((IOCTL_NLD_AVRCP_FILTER_DEQUEUE_EVENT & 3u) ==
                   METHOD_BUFFERED,
               "dequeue method changed");
_Static_assert((IOCTL_NLD_AVRCP_FILTER_SET_ABSOLUTE_VOLUME & 3u) ==
                   METHOD_BUFFERED,
               "volume write method changed");
_Static_assert(sizeof(NLD_AVRCP_FILTER_SET_VOLUME_REQUEST) == 16u,
               "volume write ABI size changed");

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            failures++;                                                        \
        }                                                                       \
    } while (0)

static void test_request_completion_pair(void) {
    NLD_AVRCP_FILTER_TRACE_QUEUE queue;
    NLD_AVRCP_FILTER_EVENT event;
    NLD_AVRCP_FILTER_STATUS status;
    unsigned char input[40];
    ULONGLONG request_id;
    unsigned index;
    for (index = 0u; index < sizeof(input); ++index) {
        input[index] = (unsigned char)index;
    }
    NldAvrcpFilterTraceInitialize(&queue);
    request_id = NldAvrcpFilterTraceAllocateRequestId(&queue);
    CHECK(request_id == 1ull);
    NldAvrcpFilterTracePush(
        &queue,
        NldAvrcpFilterEventRequest,
        request_id,
        NLD_AVRCP_FILTER_EVENT_INTERNAL_CONTROL |
            NLD_AVRCP_FILTER_EVENT_INPUT_PREFIX,
        0x12345678u,
        sizeof(input),
        64u,
        0x00000103L,
        0ull,
        input,
        sizeof(input),
        10ull);
    NldAvrcpFilterTracePush(
        &queue,
        NldAvrcpFilterEventCompletion,
        request_id,
        NLD_AVRCP_FILTER_EVENT_INTERNAL_CONTROL,
        0x12345678u,
        sizeof(input),
        64u,
        0L,
        12ull,
        NULL,
        0u,
        20ull);
    NldAvrcpFilterTraceGetStatus(
        &queue,
        NLD_AVRCP_FILTER_STATUS_ONLINE,
        &status);
    CHECK(status.RequestsObserved == 1ull);
    CHECK(status.CompletionsObserved == 1ull);
    CHECK(status.QueueDepth == 2u);
    CHECK(status.LastCompletionStatus == 0L);
    CHECK(NldAvrcpFilterTracePop(&queue, &event));
    CHECK(event.Type == NldAvrcpFilterEventRequest);
    CHECK(event.RequestId == request_id);
    CHECK(event.RawSize == NLD_AVRCP_FILTER_RAW_PREFIX_CAPACITY);
    CHECK(event.RawPrefix[0] == 0u && event.RawPrefix[31] == 31u);
    CHECK(NldAvrcpFilterTracePop(&queue, &event));
    CHECK(event.Type == NldAvrcpFilterEventCompletion);
    CHECK(event.Information == 12ull);
    CHECK(!NldAvrcpFilterTracePop(&queue, &event));
}

static void test_overflow_and_wrap(void) {
    NLD_AVRCP_FILTER_TRACE_QUEUE queue;
    NLD_AVRCP_FILTER_EVENT event;
    NLD_AVRCP_FILTER_STATUS status;
    ULONGLONG request_id;
    ULONG index;
    NldAvrcpFilterTraceInitialize(&queue);
    queue.NextRequestId = ULLONG_MAX;
    CHECK(NldAvrcpFilterTraceAllocateRequestId(&queue) == ULLONG_MAX);
    CHECK(NldAvrcpFilterTraceAllocateRequestId(&queue) == 1ull);
    for (index = 0u; index < NLD_AVRCP_FILTER_TRACE_CAPACITY + 5u;
         ++index) {
        request_id = NldAvrcpFilterTraceAllocateRequestId(&queue);
        NldAvrcpFilterTracePush(
            &queue,
            NldAvrcpFilterEventRequest,
            request_id,
            0u,
            index,
            0u,
            0u,
            0L,
            0ull,
            NULL,
            0u,
            index);
    }
    NldAvrcpFilterTraceGetStatus(&queue, 0u, &status);
    CHECK(status.QueueDepth == NLD_AVRCP_FILTER_TRACE_CAPACITY);
    CHECK(status.DroppedEvents == 5u);
    CHECK((status.Flags &
           NLD_AVRCP_FILTER_STATUS_QUEUE_OVERFLOW) != 0u);
    CHECK(NldAvrcpFilterTracePop(&queue, &event));
    CHECK(event.ControlCode == 5u);
}

static void test_capture_failure_counter(void) {
    NLD_AVRCP_FILTER_TRACE_QUEUE queue;
    NLD_AVRCP_FILTER_STATUS status;
    NldAvrcpFilterTraceInitialize(&queue);
    NldAvrcpFilterTracePush(
        &queue,
        NldAvrcpFilterEventCaptureFailure,
        9ull,
        NLD_AVRCP_FILTER_EVENT_FORWARD_UNTRACKED,
        0x44u,
        0u,
        0u,
        -1L,
        0ull,
        NULL,
        0u,
        1ull);
    NldAvrcpFilterTraceGetStatus(&queue, 0u, &status);
    CHECK(status.CaptureFailures == 1ull);
    CHECK(status.RequestsObserved == 0ull);
    CHECK(status.CompletionsObserved == 0ull);
}

int main(void) {
    test_request_completion_pair();
    test_overflow_and_wrap();
    test_capture_failure_counter();
    return failures == 0 ? 0 : 1;
}
