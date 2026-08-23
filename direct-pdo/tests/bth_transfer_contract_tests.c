// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_bth_transfer_contract.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", \
                __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void test_one_read_and_write_can_run_together(void) {
    NLD_BTH_TRANSFER_OWNER owner;
    unsigned long channel_generation;
    unsigned long read_generation;
    unsigned long write_generation;

    NldBthTransferInitialize(&owner);
    CHECK(NldBthTransferOpenChannel(&owner) != 0ul);
    CHECK(NldBthTransferBegin(&owner, NldBthTransferRead,
                              &channel_generation,
                              &read_generation) == NldBthTransferOk);
    CHECK(NldBthTransferBegin(&owner, NldBthTransferWrite,
                              &channel_generation,
                              &write_generation) == NldBthTransferOk);
    CHECK(read_generation != write_generation);
    CHECK(NldBthTransferComplete(&owner, NldBthTransferWrite,
                                 channel_generation,
                                 write_generation) == NldBthTransferOk);
    CHECK(NldBthTransferComplete(&owner, NldBthTransferRead,
                                 channel_generation,
                                 read_generation) == NldBthTransferOk);
    CHECK(NldBthTransferIsConsistent(&owner));
}

static void test_duplicate_direction_is_busy(void) {
    NLD_BTH_TRANSFER_OWNER owner;
    unsigned long channel_generation;
    unsigned long request_generation;

    NldBthTransferInitialize(&owner);
    (void)NldBthTransferOpenChannel(&owner);
    CHECK(NldBthTransferBegin(&owner, NldBthTransferRead,
                              &channel_generation,
                              &request_generation) == NldBthTransferOk);
    CHECK(NldBthTransferBegin(&owner, NldBthTransferRead,
                              &channel_generation,
                              &request_generation) == NldBthTransferBusy);
}

static void test_close_cancels_and_drains_both(void) {
    NLD_BTH_TRANSFER_OWNER owner;
    unsigned long channel_generation;
    unsigned long read_generation;
    unsigned long write_generation;

    NldBthTransferInitialize(&owner);
    channel_generation = NldBthTransferOpenChannel(&owner);
    CHECK(NldBthTransferBegin(&owner, NldBthTransferRead,
                              &channel_generation,
                              &read_generation) == NldBthTransferOk);
    CHECK(NldBthTransferBegin(&owner, NldBthTransferWrite,
                              &channel_generation,
                              &write_generation) == NldBthTransferOk);
    CHECK(NldBthTransferRequestClose(&owner, 0) ==
          (NLD_BTH_TRANSFER_CANCEL_READ |
           NLD_BTH_TRANSFER_CANCEL_WRITE));
    CHECK(NldBthTransferComplete(&owner, NldBthTransferRead,
                                 channel_generation,
                                 read_generation) == NldBthTransferOk);
    CHECK(NldBthTransferComplete(&owner, NldBthTransferWrite,
                                 channel_generation,
                                 write_generation) ==
          NldBthTransferDrained);
    CHECK(NldBthTransferFinishClose(&owner) == NldBthTransferOk);
    CHECK(NldBthTransferIsConsistent(&owner));
}

static void test_stale_completion_cannot_finish_new_request(void) {
    NLD_BTH_TRANSFER_OWNER owner;
    unsigned long old_channel;
    unsigned long old_request;
    unsigned long channel_generation;
    unsigned long request_generation;

    NldBthTransferInitialize(&owner);
    old_channel = NldBthTransferOpenChannel(&owner);
    CHECK(NldBthTransferBegin(&owner, NldBthTransferWrite,
                              &old_channel,
                              &old_request) == NldBthTransferOk);
    (void)NldBthTransferRequestClose(&owner, 0);
    CHECK(NldBthTransferComplete(&owner, NldBthTransferWrite,
                                 old_channel,
                                 old_request) == NldBthTransferDrained);
    CHECK(NldBthTransferFinishClose(&owner) == NldBthTransferOk);
    channel_generation = NldBthTransferOpenChannel(&owner);
    CHECK(NldBthTransferBegin(&owner, NldBthTransferWrite,
                              &channel_generation,
                              &request_generation) == NldBthTransferOk);
    CHECK(NldBthTransferComplete(&owner, NldBthTransferWrite,
                                 old_channel,
                                 old_request) == NldBthTransferStale);
    CHECK(owner.WritePending);
}

static void test_remote_disconnect_blocks_new_io(void) {
    NLD_BTH_TRANSFER_OWNER owner;
    unsigned long channel_generation;
    unsigned long request_generation;

    NldBthTransferInitialize(&owner);
    channel_generation = NldBthTransferOpenChannel(&owner);
    CHECK(NldBthTransferRequestClose(&owner, 1) ==
          NLD_BTH_TRANSFER_CANCEL_NONE);
    CHECK(owner.RemoteDisconnected);
    CHECK(NldBthTransferBegin(&owner, NldBthTransferRead,
                              &channel_generation,
                              &request_generation) ==
          NldBthTransferDisconnected);
    CHECK(NldBthTransferFinishClose(&owner) == NldBthTransferOk);
    CHECK(NldBthTransferIsConsistent(&owner));
}

int main(void) {
    test_one_read_and_write_can_run_together();
    test_duplicate_direction_is_busy();
    test_close_cancels_and_drains_both();
    test_stale_completion_cannot_finish_new_request();
    test_remote_disconnect_blocks_new_io();
    puts("Bth transfer contract tests passed");
    return 0;
}
