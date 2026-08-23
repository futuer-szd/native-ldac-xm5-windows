// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_direct_pdo_contract.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", \
                __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void complete(NLD_DIRECT_PDO_SESSION* session,
                     NLD_DIRECT_PDO_ACTION action,
                     NLD_DIRECT_PDO_ACTION expected_next) {
    CHECK(NldDirectPdoCompleteAction(session, action, 1) == expected_next);
    CHECK(NldDirectPdoIsConsistent(session));
}

static void test_normal_run_pause_stop(void) {
    NLD_DIRECT_PDO_SESSION session;
    NldDirectPdoInitialize(&session);
    CHECK(NldDirectPdoOnPnpStart(&session) == NldDirectPdoActionNone);
    CHECK(session.Generation == 1ul);
    CHECK(NldDirectPdoSetKsIntent(&session, NldDirectPdoKsAcquired) ==
          NldDirectPdoActionOpen);
    complete(&session, NldDirectPdoActionOpen, NldDirectPdoActionNone);
    CHECK(session.TransportState == NldDirectPdoTransportOpen);

    CHECK(NldDirectPdoSetKsIntent(&session, NldDirectPdoKsRunning) ==
          NldDirectPdoActionStart);
    complete(&session, NldDirectPdoActionStart, NldDirectPdoActionNone);
    CHECK(session.TransportState == NldDirectPdoTransportStreaming);

    CHECK(NldDirectPdoSetKsIntent(&session, NldDirectPdoKsAcquired) ==
          NldDirectPdoActionSuspend);
    complete(&session, NldDirectPdoActionSuspend, NldDirectPdoActionNone);
    CHECK(session.TransportState == NldDirectPdoTransportOpen);

    CHECK(NldDirectPdoSetKsIntent(&session, NldDirectPdoKsStopped) ==
          NldDirectPdoActionClose);
    complete(&session, NldDirectPdoActionClose, NldDirectPdoActionNone);
    CHECK(session.TransportState == NldDirectPdoTransportClosed);
}

static void test_run_requested_while_opening(void) {
    NLD_DIRECT_PDO_SESSION session;
    NldDirectPdoInitialize(&session);
    (void)NldDirectPdoOnPnpStart(&session);
    CHECK(NldDirectPdoSetKsIntent(&session, NldDirectPdoKsAcquired) ==
          NldDirectPdoActionOpen);
    CHECK(NldDirectPdoSetKsIntent(&session, NldDirectPdoKsRunning) ==
          NldDirectPdoActionNone);
    complete(&session, NldDirectPdoActionOpen, NldDirectPdoActionStart);
    complete(&session, NldDirectPdoActionStart, NldDirectPdoActionNone);
    CHECK(session.TransportState == NldDirectPdoTransportStreaming);
}

static void test_stop_requested_while_starting(void) {
    NLD_DIRECT_PDO_SESSION session;
    NldDirectPdoInitialize(&session);
    (void)NldDirectPdoOnPnpStart(&session);
    CHECK(NldDirectPdoSetKsIntent(&session, NldDirectPdoKsRunning) ==
          NldDirectPdoActionOpen);
    complete(&session, NldDirectPdoActionOpen, NldDirectPdoActionStart);
    CHECK(NldDirectPdoSetKsIntent(&session, NldDirectPdoKsStopped) ==
          NldDirectPdoActionNone);
    complete(&session, NldDirectPdoActionStart,
             NldDirectPdoActionSuspend);
    complete(&session, NldDirectPdoActionSuspend, NldDirectPdoActionClose);
    complete(&session, NldDirectPdoActionClose, NldDirectPdoActionNone);
    CHECK(session.TransportState == NldDirectPdoTransportClosed);
}

static void test_pnp_stop_cancels_pending(void) {
    NLD_DIRECT_PDO_SESSION session;
    NldDirectPdoInitialize(&session);
    (void)NldDirectPdoOnPnpStart(&session);
    CHECK(NldDirectPdoSetKsIntent(&session, NldDirectPdoKsRunning) ==
          NldDirectPdoActionOpen);
    CHECK(NldDirectPdoOnPnpStop(&session) ==
          NldDirectPdoActionCancelAndClose);
    complete(&session,
             NldDirectPdoActionCancelAndClose,
             NldDirectPdoActionNone);
    CHECK(session.TransportState == NldDirectPdoTransportOffline);
    CHECK(!session.PnpStarted);
}

static void test_failure_and_retry(void) {
    NLD_DIRECT_PDO_SESSION session;
    NldDirectPdoInitialize(&session);
    (void)NldDirectPdoOnPnpStart(&session);
    CHECK(NldDirectPdoSetKsIntent(&session, NldDirectPdoKsRunning) ==
          NldDirectPdoActionOpen);
    CHECK(NldDirectPdoCompleteAction(&session,
                                     NldDirectPdoActionOpen,
                                     0) == NldDirectPdoActionNone);
    CHECK(session.TransportState == NldDirectPdoTransportFaulted);
    CHECK(NldDirectPdoRetry(&session) == NldDirectPdoActionNone);
    CHECK(session.TransportState == NldDirectPdoTransportFaulted);
    CHECK(NldDirectPdoSetKsIntent(&session, NldDirectPdoKsStopped) ==
          NldDirectPdoActionNone);
    CHECK(NldDirectPdoRetry(&session) == NldDirectPdoActionNone);
    CHECK(session.Generation == 2ul);
    CHECK(session.TransportState == NldDirectPdoTransportClosed);
}

static void test_stale_completion_is_ignored(void) {
    NLD_DIRECT_PDO_SESSION session;
    NldDirectPdoInitialize(&session);
    (void)NldDirectPdoOnPnpStart(&session);
    CHECK(NldDirectPdoCompleteAction(&session,
                                     NldDirectPdoActionOpen,
                                     1) == NldDirectPdoActionNone);
    CHECK(session.TransportState == NldDirectPdoTransportClosed);
    CHECK(NldDirectPdoIsConsistent(&session));
}

static void test_transport_loss_fails_closed_until_explicit_retry(void) {
    NLD_DIRECT_PDO_SESSION session;
    unsigned long streaming_generation;

    NldDirectPdoInitialize(&session);
    (void)NldDirectPdoOnPnpStart(&session);
    (void)NldDirectPdoSetKsIntent(&session, NldDirectPdoKsRunning);
    complete(&session, NldDirectPdoActionOpen,
             NldDirectPdoActionStart);
    complete(&session, NldDirectPdoActionStart,
             NldDirectPdoActionNone);
    streaming_generation = session.Generation;

    CHECK(NldDirectPdoOnTransportLost(&session) ==
          NldDirectPdoActionCancelAndClose);
    CHECK(session.Generation != streaming_generation);
    CHECK(session.RecoveryRequired);
    complete(&session, NldDirectPdoActionCancelAndClose,
             NldDirectPdoActionNone);
    CHECK(session.TransportState == NldDirectPdoTransportFaulted);
    CHECK(NldDirectPdoSetKsIntent(&session, NldDirectPdoKsRunning) ==
          NldDirectPdoActionNone);
    CHECK(NldDirectPdoRetry(&session) == NldDirectPdoActionNone);
    CHECK(session.TransportState == NldDirectPdoTransportFaulted);
    CHECK(NldDirectPdoSetKsIntent(&session, NldDirectPdoKsStopped) ==
          NldDirectPdoActionNone);
    CHECK(NldDirectPdoRetry(&session) == NldDirectPdoActionNone);
    CHECK(session.TransportState == NldDirectPdoTransportClosed);
    CHECK(!session.RecoveryRequired);
}

static void test_duplicate_transport_loss_is_idempotent(void) {
    NLD_DIRECT_PDO_SESSION session;

    NldDirectPdoInitialize(&session);
    (void)NldDirectPdoOnPnpStart(&session);
    (void)NldDirectPdoSetKsIntent(&session, NldDirectPdoKsAcquired);
    complete(&session, NldDirectPdoActionOpen,
             NldDirectPdoActionNone);
    CHECK(NldDirectPdoOnTransportLost(&session) ==
          NldDirectPdoActionCancelAndClose);
    CHECK(NldDirectPdoOnTransportLost(&session) ==
          NldDirectPdoActionCancelAndClose);
    complete(&session, NldDirectPdoActionCancelAndClose,
             NldDirectPdoActionNone);
    CHECK(session.TransportState == NldDirectPdoTransportFaulted);
    CHECK(NldDirectPdoIsConsistent(&session));
}

static void test_media_fault_does_not_remove_endpoint(void) {
    CHECK(NldDirectPdoShouldPublishEndpoint(
        1, 0, NldDirectPdoTransportClosed, 0));
    CHECK(NldDirectPdoShouldPublishEndpoint(
        1, 0, NldDirectPdoTransportOpen, 0));
    CHECK(NldDirectPdoShouldPublishEndpoint(
        1, 0, NldDirectPdoTransportStreaming, 0));
    CHECK(NldDirectPdoShouldPublishEndpoint(
        1, 0, NldDirectPdoTransportFaulted, 0));
    CHECK(!NldDirectPdoShouldPublishEndpoint(
        1, 0, NldDirectPdoTransportFaulted, 1));
    CHECK(!NldDirectPdoShouldPublishEndpoint(
        1, 0, NldDirectPdoTransportOffline, 0));
    CHECK(!NldDirectPdoShouldPublishEndpoint(
        1, 1, NldDirectPdoTransportClosed, 0));
    CHECK(!NldDirectPdoShouldPublishEndpoint(
        0, 0, NldDirectPdoTransportClosed, 0));
}

int main(void) {
    test_normal_run_pause_stop();
    test_run_requested_while_opening();
    test_stop_requested_while_starting();
    test_pnp_stop_cancels_pending();
    test_failure_and_retry();
    test_stale_completion_is_ignored();
    test_transport_loss_fails_closed_until_explicit_retry();
    test_duplicate_transport_loss_is_idempotent();
    test_media_fault_does_not_remove_endpoint();
    puts("direct PDO contract tests passed");
    return 0;
}
