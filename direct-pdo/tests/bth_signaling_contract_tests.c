// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_bth_signaling_contract.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", \
                __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void test_normal_open_and_close(void) {
    NLD_BTH_SIGNALING_OWNER owner;

    NldBthSignalingOwnerInitialize(&owner);
    (void)NldBthSignalingOwnerOnPnpStart(&owner);
    CHECK(NldBthSignalingOwnerRequestOpen(&owner) ==
          NldBthSignalingActionSubmitOpen);
    CHECK(NldBthSignalingOwnerOnOpenComplete(&owner,
                                             owner.Generation,
                                             1,
                                             1) ==
          NldBthSignalingActionNone);
    CHECK(owner.State == NldBthSignalingChannelOpen);
    CHECK(NldBthSignalingOwnerRequestClose(&owner, 0) ==
          NldBthSignalingActionSubmitClose);
    NldBthSignalingOwnerOnCloseComplete(&owner, 1);
    CHECK(owner.State == NldBthSignalingClosed);
    CHECK(NldBthSignalingOwnerIsConsistent(&owner));
}

static void test_stop_cancels_pending_open(void) {
    NLD_BTH_SIGNALING_OWNER owner;
    unsigned long generation;

    NldBthSignalingOwnerInitialize(&owner);
    (void)NldBthSignalingOwnerOnPnpStart(&owner);
    (void)NldBthSignalingOwnerRequestOpen(&owner);
    generation = owner.Generation;
    CHECK(NldBthSignalingOwnerRequestClose(&owner, 1) ==
          NldBthSignalingActionCancelOpen);
    CHECK(NldBthSignalingOwnerOnOpenComplete(&owner,
                                             generation,
                                             0,
                                             0) ==
          NldBthSignalingActionNone);
    CHECK(owner.State == NldBthSignalingOffline);
    CHECK(NldBthSignalingOwnerIsConsistent(&owner));
}

static void test_open_wins_cancel_race_then_closes(void) {
    NLD_BTH_SIGNALING_OWNER owner;
    unsigned long generation;

    NldBthSignalingOwnerInitialize(&owner);
    (void)NldBthSignalingOwnerOnPnpStart(&owner);
    (void)NldBthSignalingOwnerRequestOpen(&owner);
    generation = owner.Generation;
    CHECK(NldBthSignalingOwnerRequestClose(&owner, 1) ==
          NldBthSignalingActionCancelOpen);
    CHECK(NldBthSignalingOwnerOnOpenComplete(&owner,
                                             generation,
                                             1,
                                             1) ==
          NldBthSignalingActionSubmitClose);
    CHECK(owner.State == NldBthSignalingClosing);
    NldBthSignalingOwnerOnCloseComplete(&owner, 1);
    CHECK(owner.State == NldBthSignalingOffline);
}

static void test_open_failure_is_faulted(void) {
    NLD_BTH_SIGNALING_OWNER owner;

    NldBthSignalingOwnerInitialize(&owner);
    (void)NldBthSignalingOwnerOnPnpStart(&owner);
    (void)NldBthSignalingOwnerRequestOpen(&owner);
    CHECK(NldBthSignalingOwnerOnOpenComplete(&owner,
                                             owner.Generation,
                                             0,
                                             0) ==
          NldBthSignalingActionNone);
    CHECK(owner.State == NldBthSignalingFaulted);
    CHECK(NldBthSignalingOwnerIsConsistent(&owner));
}

static void test_stale_channel_must_be_closed(void) {
    NLD_BTH_SIGNALING_OWNER owner;
    unsigned long stale_generation;

    NldBthSignalingOwnerInitialize(&owner);
    (void)NldBthSignalingOwnerOnPnpStart(&owner);
    (void)NldBthSignalingOwnerRequestOpen(&owner);
    stale_generation = owner.Generation;
    (void)NldBthSignalingOwnerRequestClose(&owner, 1);
    (void)NldBthSignalingOwnerOnOpenComplete(&owner,
                                             stale_generation,
                                             0,
                                             0);
    (void)NldBthSignalingOwnerOnPnpStart(&owner);
    CHECK(NldBthSignalingOwnerOnOpenComplete(&owner,
                                             stale_generation,
                                             1,
                                             1) ==
          NldBthSignalingActionSubmitClose);
    CHECK(owner.State == NldBthSignalingClosed);
}

static void test_remote_disconnect_fails_closed_and_invalidates_generation(
    void) {
    NLD_BTH_SIGNALING_OWNER owner;
    unsigned long channel_generation;

    NldBthSignalingOwnerInitialize(&owner);
    (void)NldBthSignalingOwnerOnPnpStart(&owner);
    (void)NldBthSignalingOwnerRequestOpen(&owner);
    channel_generation = owner.Generation;
    (void)NldBthSignalingOwnerOnOpenComplete(&owner,
                                             channel_generation,
                                             1,
                                             1);
    CHECK(NldBthSignalingOwnerOnRemoteDisconnect(
        &owner,
        channel_generation));
    CHECK(owner.State == NldBthSignalingFaulted);
    CHECK(!owner.ChannelHeld);
    CHECK(owner.Generation != channel_generation);
    CHECK(!NldBthSignalingOwnerOnRemoteDisconnect(
        &owner,
        channel_generation));
    CHECK(NldBthSignalingOwnerRequestClose(&owner, 0) ==
          NldBthSignalingActionNone);
    CHECK(owner.State == NldBthSignalingClosed);
    CHECK(NldBthSignalingOwnerIsConsistent(&owner));
}

static void test_stale_remote_disconnect_cannot_close_new_channel(void) {
    NLD_BTH_SIGNALING_OWNER owner;
    unsigned long stale_generation;
    unsigned long current_generation;

    NldBthSignalingOwnerInitialize(&owner);
    (void)NldBthSignalingOwnerOnPnpStart(&owner);
    (void)NldBthSignalingOwnerRequestOpen(&owner);
    stale_generation = owner.Generation;
    (void)NldBthSignalingOwnerOnOpenComplete(&owner,
                                             stale_generation,
                                             1,
                                             1);
    CHECK(NldBthSignalingOwnerOnRemoteDisconnect(&owner,
                                                  stale_generation));
    (void)NldBthSignalingOwnerRequestClose(&owner, 0);
    (void)NldBthSignalingOwnerRequestOpen(&owner);
    current_generation = owner.Generation;
    (void)NldBthSignalingOwnerOnOpenComplete(&owner,
                                             current_generation,
                                             1,
                                             1);
    CHECK(!NldBthSignalingOwnerOnRemoteDisconnect(&owner,
                                                   stale_generation));
    CHECK(owner.State == NldBthSignalingChannelOpen);
    CHECK(owner.ChannelHeld);
}

int main(void) {
    test_normal_open_and_close();
    test_stop_cancels_pending_open();
    test_open_wins_cancel_race_then_closes();
    test_open_failure_is_faulted();
    test_stale_channel_must_be_closed();
    test_remote_disconnect_fails_closed_and_invalidates_generation();
    test_stale_remote_disconnect_cannot_close_new_channel();
    puts("Bth signaling ownership contract tests passed");
    return 0;
}
