// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_direct_pdo_control_abi.h"

#include "nativeldac_direct_pdo_arbiter_contract.h"
#include "nativeldac_direct_pdo_diagnostic_contract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", \
                __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static NLD_DIRECT_PDO_CONTROL_REQUEST_V1 make_request(
    NLD_DIRECT_PDO_CONTROL_COMMAND command) {
    NLD_DIRECT_PDO_CONTROL_REQUEST_V1 request;

    memset(&request, 0, sizeof(request));
    request.Size = sizeof(request);
    request.Version = NLD_DIRECT_PDO_CONTROL_ABI_VERSION;
    request.Command = (NLD_DIRECT_PDO_CONTROL_U32)command;
    return request;
}

static void test_layout_is_fixed(void) {
    volatile size_t request_size =
        sizeof(NLD_DIRECT_PDO_CONTROL_REQUEST_V1);
    volatile size_t response_size =
        sizeof(NLD_DIRECT_PDO_CONTROL_RESPONSE_V1);
    volatile size_t snapshot_size =
        sizeof(NLD_DIRECT_PDO_CONTROL_SNAPSHOT_V1);

    CHECK(request_size == 32u);
    CHECK(response_size == 32u);
    CHECK(snapshot_size == 128u);
}

static void test_query_requires_query_access_and_no_flags(void) {
    NLD_DIRECT_PDO_CONTROL_REQUEST_V1 request = make_request(
        NldDirectPdoControlCommandQuerySnapshot);

    CHECK(NldDirectPdoControlValidateRequest(
        &request, sizeof(request), NldDirectPdoControlAccessQuery) ==
          NldDirectPdoControlValidationOk);
    CHECK(NldDirectPdoControlValidateRequest(
        &request, sizeof(request), NldDirectPdoControlAccessNone) ==
          NldDirectPdoControlValidationAccess);
    CHECK(NldDirectPdoControlValidateRequest(
        &request, sizeof(request),
        (NLD_DIRECT_PDO_CONTROL_ACCESS)99) ==
          NldDirectPdoControlValidationAccess);
    request.Flags = NLD_DIRECT_PDO_CONTROL_FLAG_EXPLICIT_REQUEST;
    CHECK(NldDirectPdoControlValidateRequest(
        &request, sizeof(request), NldDirectPdoControlAccessExecute) ==
          NldDirectPdoControlValidationFlags);
}

static void test_discover_requires_explicit_execute_access(void) {
    NLD_DIRECT_PDO_CONTROL_REQUEST_V1 request = make_request(
        NldDirectPdoControlCommandRequestDiscover);

    CHECK(NldDirectPdoControlValidateRequest(
        &request, sizeof(request), NldDirectPdoControlAccessExecute) ==
          NldDirectPdoControlValidationFlags);
    request.Flags = NLD_DIRECT_PDO_CONTROL_FLAG_EXPLICIT_REQUEST;
    CHECK(NldDirectPdoControlValidateRequest(
        &request, sizeof(request), NldDirectPdoControlAccessQuery) ==
          NldDirectPdoControlValidationAccess);
    CHECK(NldDirectPdoControlValidateRequest(
        &request, sizeof(request), NldDirectPdoControlAccessExecute) ==
          NldDirectPdoControlValidationOk);
}

static void test_version_size_command_and_reserved_fail_closed(void) {
    NLD_DIRECT_PDO_CONTROL_REQUEST_V1 request = make_request(
        NldDirectPdoControlCommandQuerySnapshot);

    CHECK(NldDirectPdoControlValidateRequest(
        0, sizeof(request), NldDirectPdoControlAccessQuery) ==
          NldDirectPdoControlValidationNull);
    CHECK(NldDirectPdoControlValidateRequest(
        &request, sizeof(request) - 1u,
        NldDirectPdoControlAccessQuery) ==
          NldDirectPdoControlValidationBufferTooSmall);
    request.Size += 4u;
    CHECK(NldDirectPdoControlValidateRequest(
        &request, sizeof(request), NldDirectPdoControlAccessQuery) ==
          NldDirectPdoControlValidationSize);
    request = make_request(NldDirectPdoControlCommandQuerySnapshot);
    request.Version += 1u;
    CHECK(NldDirectPdoControlValidateRequest(
        &request, sizeof(request), NldDirectPdoControlAccessQuery) ==
          NldDirectPdoControlValidationVersion);
    request = make_request(NldDirectPdoControlCommandQuerySnapshot);
    request.Command = 99u;
    CHECK(NldDirectPdoControlValidateRequest(
        &request, sizeof(request), NldDirectPdoControlAccessExecute) ==
          NldDirectPdoControlValidationCommand);
    request = make_request(NldDirectPdoControlCommandQuerySnapshot);
    request.Reserved[1] = 1u;
    CHECK(NldDirectPdoControlValidateRequest(
        &request, sizeof(request), NldDirectPdoControlAccessQuery) ==
          NldDirectPdoControlValidationReserved);
}

static void test_disposition_preserves_render_priority(void) {
    CHECK(NldDirectPdoControlDeriveDisposition(
        NldDirectPdoDiagnosticIdle,
        NldDirectPdoArbiterIdle,
        NldDirectPdoArbiterClientNone,
        0,
        1) == NldDirectPdoControlDispositionIdle);
    CHECK(NldDirectPdoControlDeriveDisposition(
        NldDirectPdoDiagnosticDiscovering,
        NldDirectPdoArbiterDiagnosticOwned,
        NldDirectPdoArbiterClientDiagnostic,
        1,
        1) == NldDirectPdoControlDispositionPreemptDiagnostic);
    CHECK(NldDirectPdoControlDeriveDisposition(
        NldDirectPdoDiagnosticIdle,
        NldDirectPdoArbiterRenderOwned,
        NldDirectPdoArbiterClientRender,
        0,
        1) == NldDirectPdoControlDispositionBusyRender);
    CHECK(NldDirectPdoControlDeriveDisposition(
        NldDirectPdoDiagnosticOpening,
        NldDirectPdoArbiterDiagnosticOwned,
        NldDirectPdoArbiterClientDiagnostic,
        0,
        1) == NldDirectPdoControlDispositionBusyDiagnostic);
}

static void test_terminal_and_stop_dispositions(void) {
    CHECK(NldDirectPdoControlDeriveDisposition(
        NldDirectPdoDiagnosticComplete,
        NldDirectPdoArbiterIdle,
        NldDirectPdoArbiterClientNone,
        0,
        1) == NldDirectPdoControlDispositionComplete);
    CHECK(NldDirectPdoControlDeriveDisposition(
        NldDirectPdoDiagnosticFaulted,
        NldDirectPdoArbiterIdle,
        NldDirectPdoArbiterClientNone,
        0,
        1) == NldDirectPdoControlDispositionFaulted);
    CHECK(NldDirectPdoControlDeriveDisposition(
        NldDirectPdoDiagnosticStopping,
        NldDirectPdoArbiterStopping,
        NldDirectPdoArbiterClientDiagnostic,
        0,
        1) == NldDirectPdoControlDispositionStopping);
    CHECK(NldDirectPdoControlDeriveDisposition(
        NldDirectPdoDiagnosticOffline,
        NldDirectPdoArbiterOffline,
        NldDirectPdoArbiterClientNone,
        0,
        0) == NldDirectPdoControlDispositionOffline);
}

int main(void) {
    test_layout_is_fixed();
    test_query_requires_query_access_and_no_flags();
    test_discover_requires_explicit_execute_access();
    test_version_size_command_and_reserved_fail_closed();
    test_disposition_preserves_render_priority();
    test_terminal_and_stop_dispositions();
    puts("Direct-PDO control ABI tests passed");
    return 0;
}
