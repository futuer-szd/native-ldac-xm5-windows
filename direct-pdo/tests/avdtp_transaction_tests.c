// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_avdtp_transaction.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", \
                __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static unsigned long begin_discover(
    NLD_AVDTP_TRANSACTION* transaction,
    unsigned char packet[2]) {
    size_t packet_size = 0u;
    unsigned long generation = 99ul;

    CHECK(NldAvdtpTransactionBegin(transaction,
                                    NLD_AVDTP_SIGNAL_DISCOVER,
                                    packet,
                                    2u,
                                    &packet_size,
                                    &generation) ==
          NldAvdtpTransactionOk);
    CHECK(packet_size == 2u);
    CHECK(packet[1] == NLD_AVDTP_SIGNAL_DISCOVER);
    return generation;
}

static void test_discover_accept(void) {
    NLD_AVDTP_TRANSACTION transaction;
    unsigned char command[2];
    unsigned char response[] = {0x02u, 0x01u, 0x04u, 0x08u};
    unsigned long generation;
    size_t payload_offset = 0u;

    NldAvdtpTransactionInitialize(&transaction);
    NldAvdtpTransactionReset(&transaction);
    generation = begin_discover(&transaction, command);
    CHECK(command[0] == 0x00u);
    CHECK(NldAvdtpTransactionAcceptResponse(&transaction,
                                            generation,
                                            response,
                                            sizeof(response),
                                            &payload_offset) ==
          NldAvdtpTransactionOk);
    CHECK(payload_offset == 2u);
    CHECK(!transaction.Pending);
    CHECK(NldAvdtpTransactionIsConsistent(&transaction));
}

static void test_busy_and_label_wrap(void) {
    NLD_AVDTP_TRANSACTION transaction;
    unsigned char command[2];
    unsigned char response[2];
    unsigned long generation;
    size_t size = 0u;
    size_t payload_offset;
    unsigned index;

    NldAvdtpTransactionInitialize(&transaction);
    NldAvdtpTransactionReset(&transaction);
    for (index = 0u; index < 17u; ++index) {
        generation = begin_discover(&transaction, command);
        CHECK(NldAvdtpTransactionBegin(&transaction,
                                       NLD_AVDTP_SIGNAL_DISCOVER,
                                       command,
                                       sizeof(command),
                                       &size,
                                       &generation) ==
              NldAvdtpTransactionBusy);
        response[0] = (unsigned char)(command[0] | 0x02u);
        response[1] = command[1];
        CHECK(NldAvdtpTransactionAcceptResponse(&transaction,
                                                transaction.Generation,
                                                response,
                                                sizeof(response),
                                                &payload_offset) ==
              NldAvdtpTransactionOk);
    }
    CHECK((command[0] >> 4u) == 0u);
}

static void test_stale_and_mismatched_responses_do_not_mutate(void) {
    NLD_AVDTP_TRANSACTION transaction;
    unsigned char command[2];
    unsigned char response[] = {0x12u, 0x01u};
    unsigned long generation;
    size_t payload_offset;

    NldAvdtpTransactionInitialize(&transaction);
    NldAvdtpTransactionReset(&transaction);
    generation = begin_discover(&transaction, command);
    CHECK(NldAvdtpTransactionAcceptResponse(&transaction,
                                            generation - 1ul,
                                            response,
                                            sizeof(response),
                                            &payload_offset) ==
          NldAvdtpTransactionStale);
    CHECK(transaction.Pending);
    CHECK(NldAvdtpTransactionAcceptResponse(&transaction,
                                            generation,
                                            response,
                                            sizeof(response),
                                            &payload_offset) ==
          NldAvdtpTransactionUnexpectedResponse);
    CHECK(transaction.Pending);
}

static void test_fragment_and_truncation_fail_closed(void) {
    NLD_AVDTP_TRANSACTION transaction;
    unsigned char command[2];
    unsigned char fragment[] = {0x06u, 0x01u};
    unsigned char truncated[] = {0x02u};
    unsigned long generation;
    size_t payload_offset;

    NldAvdtpTransactionInitialize(&transaction);
    NldAvdtpTransactionReset(&transaction);
    generation = begin_discover(&transaction, command);
    CHECK(NldAvdtpTransactionAcceptResponse(&transaction,
                                            generation,
                                            fragment,
                                            sizeof(fragment),
                                            &payload_offset) ==
          NldAvdtpTransactionUnsupportedFragment);
    CHECK(transaction.Faulted);
    NldAvdtpTransactionReset(&transaction);
    generation = begin_discover(&transaction, command);
    CHECK(NldAvdtpTransactionAcceptResponse(&transaction,
                                            generation,
                                            truncated,
                                            sizeof(truncated),
                                            &payload_offset) ==
          NldAvdtpTransactionTruncated);
    CHECK(transaction.Faulted);
}

static void test_reject_completes_without_fault(void) {
    NLD_AVDTP_TRANSACTION transaction;
    unsigned char command[2];
    unsigned char reject[] = {0x03u, 0x01u, 0x31u};
    unsigned long generation;
    size_t payload_offset;

    NldAvdtpTransactionInitialize(&transaction);
    NldAvdtpTransactionReset(&transaction);
    generation = begin_discover(&transaction, command);
    CHECK(NldAvdtpTransactionAcceptResponse(&transaction,
                                            generation,
                                            reject,
                                            sizeof(reject),
                                            &payload_offset) ==
          NldAvdtpTransactionRejected);
    CHECK(!transaction.Pending);
    CHECK(!transaction.Faulted);
}

int main(void) {
    test_discover_accept();
    test_busy_and_label_wrap();
    test_stale_and_mismatched_responses_do_not_mutate();
    test_fragment_and_truncation_fail_closed();
    test_reject_completes_without_fault();
    puts("AVDTP transaction tests passed");
    return 0;
}
