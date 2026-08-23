// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include <stdio.h>

#include "nativeldac_avrcp_event_queue_contract.h"

static int failures;

_Static_assert(NLD_AVRCP_OBSERVER_ABI_MAJOR == 0u, "ABI major changed");
_Static_assert(NLD_AVRCP_OBSERVER_ABI_MINOR == 11u, "ABI minor changed");
_Static_assert(sizeof(NLD_AVRCP_OBSERVER_ABI_VERSION) == 16u,
               "version ABI size changed");
_Static_assert(sizeof(NLD_AVRCP_OBSERVER_STATUS) == 56u,
               "status ABI size changed");
_Static_assert(sizeof(NLD_AVRCP_OBSERVER_EVENT) == 104u,
               "event ABI size changed");
_Static_assert((IOCTL_NLD_AVRCP_OBSERVER_GET_VERSION & 3u) ==
                   METHOD_BUFFERED,
               "version IOCTL method changed");
_Static_assert((IOCTL_NLD_AVRCP_OBSERVER_GET_STATUS & 3u) ==
                   METHOD_BUFFERED,
               "status IOCTL method changed");
_Static_assert((IOCTL_NLD_AVRCP_OBSERVER_DEQUEUE_EVENT & 3u) ==
                   METHOD_BUFFERED,
               "event IOCTL method changed");
_Static_assert((IOCTL_NLD_AVRCP_OBSERVER_BEGIN_OBSERVATION & 3u) ==
                   METHOD_BUFFERED,
               "activation IOCTL method changed");

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            failures++;                                                        \
        }                                                                       \
    } while (0)

static void test_generation_and_order(void) {
    NLD_AVRCP_EVENT_QUEUE queue;
    NLD_AVRCP_OBSERVER_EVENT event;
    NLD_AVRCP_OBSERVER_STATUS status;
    NldAvrcpEventQueueInitialize(&queue);
    CHECK(NldAvrcpEventQueueBeginGeneration(&queue, 1ull, 100ull));
    CHECK(!NldAvrcpEventQueueBeginGeneration(&queue, 1ull, 101ull));
    CHECK(!NldAvrcpEventQueuePush(
        &queue, 2ull, NldAvrcpObserverEventAbsoluteVolume,
        0u, 64u, 0, 0u, NULL, 102ull));
    CHECK(NldAvrcpEventQueuePush(
        &queue, 1ull, NldAvrcpObserverEventAbsoluteVolume,
        NLD_AVRCP_EVENT_FLAG_INTERIM, 64u, 0, 0u, NULL, 103ull));
    CHECK(NldAvrcpEventQueueEndGeneration(&queue, 1ull, 104ull));
    CHECK(!NldAvrcpEventQueuePush(
        &queue, 1ull, NldAvrcpObserverEventPassThrough,
        0u, 0x44u, 0, 0u, NULL, 105ull));

    NldAvrcpEventQueueGetStatus(&queue, &status);
    CHECK(status.Size == sizeof(status));
    CHECK(status.AclGeneration == 1ull && status.QueueDepth == 3u);
    CHECK(NldAvrcpEventQueuePop(&queue, &event));
    CHECK(event.Type == NldAvrcpObserverEventAclConnected);
    CHECK(event.Sequence == 1ull && event.Timestamp100ns == 100ull);
    CHECK(NldAvrcpEventQueuePop(&queue, &event));
    CHECK(event.Type == NldAvrcpObserverEventAbsoluteVolume);
    CHECK(event.Value0 == 64u && event.Sequence == 2ull);
    CHECK(NldAvrcpEventQueuePop(&queue, &event));
    CHECK(event.Type == NldAvrcpObserverEventAclDisconnected);
    CHECK(!NldAvrcpEventQueuePop(&queue, &event));
}

static void test_overflow_keeps_freshest(void) {
    NLD_AVRCP_EVENT_QUEUE queue;
    NLD_AVRCP_OBSERVER_EVENT event;
    NLD_AVRCP_OBSERVER_STATUS status;
    ULONG i;
    NldAvrcpEventQueueInitialize(&queue);
    CHECK(NldAvrcpEventQueueBeginGeneration(&queue, 5ull, 1ull));
    for (i = 0u; i < NLD_AVRCP_EVENT_QUEUE_CAPACITY + 10u; ++i) {
        CHECK(NldAvrcpEventQueuePush(
            &queue, 5ull, NldAvrcpObserverEventPassThrough,
            0u, i, 0, 0u, NULL, (ULONGLONG)i + 2ull));
    }
    NldAvrcpEventQueueGetStatus(&queue, &status);
    CHECK(status.QueueDepth == NLD_AVRCP_EVENT_QUEUE_CAPACITY);
    CHECK(status.DroppedEvents == 11u);
    CHECK((status.Flags & NLD_AVRCP_OBSERVER_STATUS_QUEUE_OVERFLOW) != 0u);
    CHECK(NldAvrcpEventQueuePop(&queue, &event));
    CHECK(event.Type == NldAvrcpObserverEventPassThrough);
    CHECK(event.Value0 == 10u);
}

static void test_generation_wrap(void) {
    NLD_AVRCP_EVENT_QUEUE queue;
    NldAvrcpEventQueueInitialize(&queue);
    CHECK(NldAvrcpEventQueueBeginGeneration(
        &queue, ULLONG_MAX, 1ull));
    CHECK(NldAvrcpEventQueueEndGeneration(
        &queue, ULLONG_MAX, 2ull));
    CHECK(NldAvrcpEventQueueBeginGeneration(&queue, 1ull, 3ull));
    CHECK(queue.AclGeneration == 1ull && queue.GenerationCurrent);
}



static void test_vendor_command_event(void) {
    NLD_AVRCP_EVENT_QUEUE queue;
    NLD_AVRCP_OBSERVER_EVENT event;
    ULONG words[15] = {0u};
    NldAvrcpEventQueueInitialize(&queue);
    CHECK(NldAvrcpEventQueueBeginGeneration(&queue, 9ull, 1ull));
    words[0] = 0x03020100u;
    words[1] = 0x07060504u;
    CHECK(NldAvrcpEventQueuePush(
        &queue, 9ull, NldAvrcpObserverEventVendorCommand,
        0u, 0x50u, 0, 0u, words, 2ull));
    CHECK(NldAvrcpEventQueuePop(&queue, &event));
    CHECK(NldAvrcpEventQueuePop(&queue, &event));
    CHECK(event.Type == NldAvrcpObserverEventVendorCommand);
    CHECK(event.Value0 == 0x50u);
    CHECK(event.RawPrefixHigh == 0x03020100u);
    CHECK(event.RawPrefixHigh2 == 0x07060504u);
}

static void test_write_response_event(void) {
    NLD_AVRCP_EVENT_QUEUE queue;
    NLD_AVRCP_OBSERVER_EVENT event;
    ULONG words[15] = {0u};
    NldAvrcpEventQueueInitialize(&queue);
    CHECK(NldAvrcpEventQueueBeginGeneration(&queue, 11ull, 1ull));
    words[0] = 0x00000040u;
    CHECK(NldAvrcpEventQueuePush(
        &queue, 11ull, NldAvrcpObserverEventWriteResponse,
        0u, 0x50u, 0x09, 0u, words, 2ull));
    CHECK(NldAvrcpEventQueuePop(&queue, &event));
    CHECK(NldAvrcpEventQueuePop(&queue, &event));
    CHECK(event.Type == NldAvrcpObserverEventWriteResponse);
    CHECK(event.Value0 == 0x50u);
    CHECK(event.ProtocolStatus == 0x09);
    CHECK(event.RawPrefixHigh == 0x00000040u);
}
static void test_protocol_error_raw_diagnostic(void) {
    NLD_AVRCP_EVENT_QUEUE queue;
    NLD_AVRCP_OBSERVER_EVENT event;
    NldAvrcpEventQueueInitialize(&queue);
    CHECK(NldAvrcpEventQueueBeginGeneration(&queue, 7ull, 1ull));
    CHECK(NldAvrcpEventQueuePush(
        &queue, 7ull, NldAvrcpObserverEventProtocolError,
        NLD_AVRCP_EVENT_FLAG_RAW_PREFIX |
            (4u << NLD_AVRCP_EVENT_RAW_LENGTH_SHIFT) |
            (14u << NLD_AVRCP_EVENT_PACKET_SIZE_SHIFT) |
            (1u << NLD_AVRCP_EVENT_PARSE_STAGE_SHIFT),
        0x0E0E1522u, -4, 4u, NULL, 2ull));
    CHECK(NldAvrcpEventQueuePop(&queue, &event));
    CHECK(NldAvrcpEventQueuePop(&queue, &event));
    CHECK(event.Type == NldAvrcpObserverEventProtocolError);
    CHECK(event.ProtocolStatus == -4);
    CHECK((event.Flags & NLD_AVRCP_EVENT_FLAG_RAW_PREFIX) != 0u);
    CHECK(((event.Flags & NLD_AVRCP_EVENT_RAW_LENGTH_MASK) >>
        NLD_AVRCP_EVENT_RAW_LENGTH_SHIFT) == 4u);
    CHECK(((event.Flags & NLD_AVRCP_EVENT_PACKET_SIZE_MASK) >>
        NLD_AVRCP_EVENT_PACKET_SIZE_SHIFT) == 14u);
    CHECK(((event.Flags & NLD_AVRCP_EVENT_PARSE_STAGE_MASK) >>
        NLD_AVRCP_EVENT_PARSE_STAGE_SHIFT) == 1u);
    CHECK((event.Flags & 0xFFu) == NLD_AVRCP_EVENT_FLAG_RAW_PREFIX);
    CHECK(event.Value0 == 0x0E0E1522u);
    CHECK(event.RawPrefixHigh == 0u);
    {
        ULONG raw_words8[15] = {0u};
        raw_words8[0] = 0x07060504u;
        CHECK(NldAvrcpEventQueuePush(
            &queue, 7ull, NldAvrcpObserverEventProtocolError,
            NLD_AVRCP_EVENT_FLAG_RAW_PREFIX |
                (8u << NLD_AVRCP_EVENT_RAW_LENGTH_SHIFT) |
                (21u << NLD_AVRCP_EVENT_PACKET_SIZE_SHIFT) |
                (2u << NLD_AVRCP_EVENT_PARSE_STAGE_SHIFT),
            0x03020100u, -4, 8u, raw_words8, 3ull));
        CHECK(NldAvrcpEventQueuePop(&queue, &event));
        CHECK(event.Value0 == 0x03020100u);
        CHECK(event.RawPrefixHigh == 0x07060504u);
        CHECK(event.RawPrefixHigh2 == 0u);
        CHECK(event.RawPrefixHigh3 == 0u);
        CHECK(((event.Flags & NLD_AVRCP_EVENT_RAW_LENGTH_MASK) >>
            NLD_AVRCP_EVENT_RAW_LENGTH_SHIFT) == 8u);
    }
    {
        ULONG raw_words16[15] = {0u};
        raw_words16[0] = 0x07060504u;
        raw_words16[1] = 0x0B0A0908u;
        raw_words16[2] = 0x0F0E0D0Cu;
        CHECK(NldAvrcpEventQueuePush(
            &queue, 7ull, NldAvrcpObserverEventProtocolError,
            NLD_AVRCP_EVENT_FLAG_RAW_PREFIX |
                (16u << NLD_AVRCP_EVENT_RAW_LENGTH_SHIFT) |
                (160u << NLD_AVRCP_EVENT_PACKET_SIZE_SHIFT) |
                (2u << NLD_AVRCP_EVENT_PARSE_STAGE_SHIFT),
            0x03020100u, -4, 16u, raw_words16, 4ull));
        CHECK(NldAvrcpEventQueuePop(&queue, &event));
        CHECK(event.Value0 == 0x03020100u);
        CHECK(event.RawPrefixHigh == 0x07060504u);
        CHECK(event.RawPrefixHigh2 == 0x0B0A0908u);
        CHECK(event.RawPrefixHigh3 == 0x0F0E0D0Cu);
        CHECK(((event.Flags & NLD_AVRCP_EVENT_RAW_LENGTH_MASK) >>
            NLD_AVRCP_EVENT_RAW_LENGTH_SHIFT) == 16u);
        CHECK(((event.Flags & NLD_AVRCP_EVENT_PACKET_SIZE_MASK) >>
            NLD_AVRCP_EVENT_PACKET_SIZE_SHIFT) == 160u);
        CHECK(((event.Flags & NLD_AVRCP_EVENT_PARSE_STAGE_MASK) >>
            NLD_AVRCP_EVENT_PARSE_STAGE_SHIFT) == 2u);
    }
    {
        ULONG raw_words64[15] = {
            0x07060504u, 0x0B0A0908u, 0x0F0E0D0Cu,
            0x13121110u, 0x17161514u, 0x1B1A1918u,
            0x1F1E1D1Cu, 0x23222120u, 0x27262524u,
            0x2B2A2928u, 0x2F2E2D2Cu, 0x33323130u,
            0x37363534u, 0x3B3A3938u, 0x3F3E3D3Cu
        };
        CHECK(NldAvrcpEventQueuePush(
            &queue, 7ull, NldAvrcpObserverEventProtocolError,
            NLD_AVRCP_EVENT_FLAG_RAW_PREFIX |
                (64u << NLD_AVRCP_EVENT_RAW_LENGTH_SHIFT) |
                (160u << NLD_AVRCP_EVENT_PACKET_SIZE_SHIFT) |
                (2u << NLD_AVRCP_EVENT_PARSE_STAGE_SHIFT),
            0x03020100u, -4, 64u, raw_words64, 5ull));
        CHECK(NldAvrcpEventQueuePop(&queue, &event));
        CHECK(event.Value0 == 0x03020100u);
        CHECK(event.RawPrefixHigh == 0x07060504u);
        CHECK(event.RawPrefixHigh2 == 0x0B0A0908u);
        CHECK(event.RawPrefixHigh3 == 0x0F0E0D0Cu);
        CHECK(event.RawPrefixHigh4 == 0x13121110u);
        CHECK(event.RawPrefixHigh5 == 0x17161514u);
        CHECK(event.RawPrefixHigh6 == 0x1B1A1918u);
        CHECK(event.RawPrefixHigh7 == 0x1F1E1D1Cu);
        CHECK(event.RawPrefixHigh8 == 0x23222120u);
        CHECK(event.RawPrefixHigh9 == 0x27262524u);
        CHECK(event.RawPrefixHigh10 == 0x2B2A2928u);
        CHECK(event.RawPrefixHigh11 == 0x2F2E2D2Cu);
        CHECK(event.RawPrefixHigh12 == 0x33323130u);
        CHECK(event.RawPrefixHigh13 == 0x37363534u);
        CHECK(event.RawPrefixHigh14 == 0x3B3A3938u);
        CHECK(event.RawPrefixHigh15 == 0x3F3E3D3Cu);
        CHECK(((event.Flags & NLD_AVRCP_EVENT_RAW_LENGTH_MASK) >>
            NLD_AVRCP_EVENT_RAW_LENGTH_SHIFT) == 64u);
        CHECK(((event.Flags & NLD_AVRCP_EVENT_PACKET_SIZE_MASK) >>
            NLD_AVRCP_EVENT_PACKET_SIZE_SHIFT) == 160u);
        CHECK(((event.Flags & NLD_AVRCP_EVENT_PARSE_STAGE_MASK) >>
            NLD_AVRCP_EVENT_PARSE_STAGE_SHIFT) == 2u);
    }
}
int main(void) {
    test_generation_and_order();
    test_overflow_keeps_freshest();
    test_generation_wrap();
    test_protocol_error_raw_diagnostic();
    test_vendor_command_event();
    test_write_response_event();
    return failures == 0 ? 0 : 1;
}
