// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_direct_pdo_control_abi.h"

#include "nativeldac_direct_pdo_arbiter_contract.h"
#include "nativeldac_direct_pdo_diagnostic_contract.h"

NLD_DIRECT_PDO_CONTROL_VALIDATION
NldDirectPdoControlValidateRequest(
    const NLD_DIRECT_PDO_CONTROL_REQUEST_V1* request,
    NLD_DIRECT_PDO_CONTROL_U32 buffer_size,
    NLD_DIRECT_PDO_CONTROL_ACCESS access) {
    NLD_DIRECT_PDO_CONTROL_U32 index;

    if (request == 0) return NldDirectPdoControlValidationNull;
    if (access < NldDirectPdoControlAccessNone ||
        access > NldDirectPdoControlAccessExecute) {
        return NldDirectPdoControlValidationAccess;
    }
    if (buffer_size < sizeof(*request)) {
        return NldDirectPdoControlValidationBufferTooSmall;
    }
    if (request->Size != sizeof(*request) ||
        request->Size != buffer_size) {
        return NldDirectPdoControlValidationSize;
    }
    if (request->Version != NLD_DIRECT_PDO_CONTROL_ABI_VERSION) {
        return NldDirectPdoControlValidationVersion;
    }
    for (index = 0ul; index < 3ul; ++index) {
        if (request->Reserved[index] != 0ul) {
            return NldDirectPdoControlValidationReserved;
        }
    }

    switch ((NLD_DIRECT_PDO_CONTROL_COMMAND)request->Command) {
        case NldDirectPdoControlCommandQuerySnapshot:
            if (request->Flags != 0ul) {
                return NldDirectPdoControlValidationFlags;
            }
            if (access < NldDirectPdoControlAccessQuery) {
                return NldDirectPdoControlValidationAccess;
            }
            return NldDirectPdoControlValidationOk;

        case NldDirectPdoControlCommandRequestDiscover:
            if (request->Flags !=
                NLD_DIRECT_PDO_CONTROL_FLAG_EXPLICIT_REQUEST) {
                return NldDirectPdoControlValidationFlags;
            }
            if (access < NldDirectPdoControlAccessExecute) {
                return NldDirectPdoControlValidationAccess;
            }
            return NldDirectPdoControlValidationOk;

        default:
            return NldDirectPdoControlValidationCommand;
    }
}

NLD_DIRECT_PDO_CONTROL_DISPOSITION
NldDirectPdoControlDeriveDisposition(
    NLD_DIRECT_PDO_CONTROL_U32 diagnostic_state,
    NLD_DIRECT_PDO_CONTROL_U32 arbiter_state,
    NLD_DIRECT_PDO_CONTROL_U32 arbiter_client,
    int render_demand,
    int started) {
    if (!started ||
        diagnostic_state == NldDirectPdoDiagnosticOffline ||
        arbiter_state == NldDirectPdoArbiterOffline) {
        return NldDirectPdoControlDispositionOffline;
    }
    if (diagnostic_state == NldDirectPdoDiagnosticStopping ||
        arbiter_state == NldDirectPdoArbiterStopping) {
        return NldDirectPdoControlDispositionStopping;
    }
    if (render_demand &&
        arbiter_client == NldDirectPdoArbiterClientDiagnostic) {
        return NldDirectPdoControlDispositionPreemptDiagnostic;
    }
    if (render_demand ||
        arbiter_client == NldDirectPdoArbiterClientRender) {
        return NldDirectPdoControlDispositionBusyRender;
    }
    if (arbiter_client == NldDirectPdoArbiterClientDiagnostic ||
        diagnostic_state == NldDirectPdoDiagnosticOpening ||
        diagnostic_state == NldDirectPdoDiagnosticDiscovering ||
        diagnostic_state == NldDirectPdoDiagnosticClosing) {
        return NldDirectPdoControlDispositionBusyDiagnostic;
    }
    if (diagnostic_state == NldDirectPdoDiagnosticComplete) {
        return NldDirectPdoControlDispositionComplete;
    }
    if (diagnostic_state == NldDirectPdoDiagnosticFaulted) {
        return NldDirectPdoControlDispositionFaulted;
    }
    return NldDirectPdoControlDispositionIdle;
}
