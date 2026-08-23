// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_direct_pdo_control.h"

static NTSTATUS NldDirectPdoControlValidationStatus(
    _In_ NLD_DIRECT_PDO_CONTROL_VALIDATION validation) {
    return validation == NldDirectPdoControlValidationAccess
        ? STATUS_ACCESS_DENIED
        : STATUS_INVALID_PARAMETER;
}

static NTSTATUS NldDirectPdoControlDispositionStatus(
    _In_ NLD_DIRECT_PDO_CONTROL_DISPOSITION disposition) {
    switch (disposition) {
        case NldDirectPdoControlDispositionIdle:
        case NldDirectPdoControlDispositionComplete:
        case NldDirectPdoControlDispositionFaulted:
            return STATUS_SUCCESS;
        case NldDirectPdoControlDispositionBusyRender:
        case NldDirectPdoControlDispositionBusyDiagnostic:
        case NldDirectPdoControlDispositionPreemptDiagnostic:
            return STATUS_DEVICE_BUSY;
        case NldDirectPdoControlDispositionStopping:
        case NldDirectPdoControlDispositionOffline:
        default:
            return STATUS_DEVICE_NOT_READY;
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldDirectPdoControlBuildSnapshot(
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT diagnostic,
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT arbiter,
    _Out_ NLD_DIRECT_PDO_CONTROL_SNAPSHOT_V1* snapshot) {
    NLD_DIRECT_PDO_DIAGNOSTIC_SNAPSHOT diagnostic_snapshot;
    NLD_DIRECT_PDO_ARBITER_SNAPSHOT arbiter_snapshot;
    NLD_DIRECT_PDO_CONTROL_U32 flags = 0u;

    if (snapshot == NULL) return;
    RtlZeroMemory(snapshot, sizeof(*snapshot));
    snapshot->Size = sizeof(*snapshot);
    snapshot->Version = NLD_DIRECT_PDO_CONTROL_ABI_VERSION;
    if (diagnostic == NULL || arbiter == NULL) {
        snapshot->Disposition =
            NldDirectPdoControlDispositionOffline;
        return;
    }

    NldDirectPdoDiagnosticRuntimeGetSnapshot(
        diagnostic,
        &diagnostic_snapshot);
    NldDirectPdoArbiterRuntimeGetSnapshot(arbiter,
                                          &arbiter_snapshot);
    if (diagnostic_snapshot.Started) {
        flags |= NLD_DIRECT_PDO_CONTROL_SNAPSHOT_STARTED;
    }
    if (diagnostic_snapshot.WorkerOwned) {
        flags |= NLD_DIRECT_PDO_CONTROL_SNAPSHOT_WORKER_OWNED;
    }
    if (diagnostic_snapshot.StopRequested) {
        flags |= NLD_DIRECT_PDO_CONTROL_SNAPSHOT_STOP_REQUESTED;
    }
    if (diagnostic_snapshot.CancelRequested) {
        flags |= NLD_DIRECT_PDO_CONTROL_SNAPSHOT_CANCEL_REQUESTED;
    }
    if (diagnostic_snapshot.ResponseTruncated) {
        flags |= NLD_DIRECT_PDO_CONTROL_SNAPSHOT_RESPONSE_TRUNCATED;
    }
    if (arbiter_snapshot.RenderDemand) {
        flags |= NLD_DIRECT_PDO_CONTROL_SNAPSHOT_RENDER_DEMAND;
    }
    if (arbiter_snapshot.PnpStarted) {
        flags |= NLD_DIRECT_PDO_CONTROL_SNAPSHOT_PNP_STARTED;
    }

    snapshot->Flags = flags;
    snapshot->Disposition = NldDirectPdoControlDeriveDisposition(
        diagnostic_snapshot.State,
        arbiter_snapshot.State,
        arbiter_snapshot.Client,
        arbiter_snapshot.RenderDemand,
        diagnostic_snapshot.Started);
    snapshot->DiagnosticState = diagnostic_snapshot.State;
    snapshot->PendingAction = diagnostic_snapshot.PendingAction;
    snapshot->ActiveAction = diagnostic_snapshot.ActiveAction;
    snapshot->LastAction = diagnostic_snapshot.LastAction;
    snapshot->ArbiterState = arbiter_snapshot.State;
    snapshot->ArbiterClient = arbiter_snapshot.Client;
    snapshot->Generation = diagnostic_snapshot.Generation;
    snapshot->ResultGeneration =
        diagnostic_snapshot.ResultGeneration;
    snapshot->ArbiterGeneration =
        diagnostic_snapshot.ArbiterGeneration;
    snapshot->ResponseLength = diagnostic_snapshot.ResponseLength;
    snapshot->PayloadOffset = diagnostic_snapshot.PayloadOffset;
    snapshot->ResponsePrefixLength =
        diagnostic_snapshot.ResponsePrefixLength;
    snapshot->LastStatus = diagnostic_snapshot.LastStatus;
    snapshot->DiscoverStatus = diagnostic_snapshot.DiscoverStatus;
    snapshot->CloseStatus = diagnostic_snapshot.CloseStatus;
    RtlCopyMemory(snapshot->ResponsePrefix,
                  diagnostic_snapshot.ResponsePrefix,
                  sizeof(snapshot->ResponsePrefix));
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS NldDirectPdoControlExecute(
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT diagnostic,
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT arbiter,
    _In_ const NLD_DIRECT_PDO_CONTROL_REQUEST_V1* request,
    _In_ NLD_DIRECT_PDO_CONTROL_U32 request_size,
    _In_ NLD_DIRECT_PDO_CONTROL_ACCESS access,
    _Out_ NLD_DIRECT_PDO_CONTROL_RESPONSE_V1* response,
    _Out_opt_ NLD_DIRECT_PDO_CONTROL_SNAPSHOT_V1* snapshot) {
    NLD_DIRECT_PDO_CONTROL_VALIDATION validation;
    NLD_DIRECT_PDO_CONTROL_SNAPSHOT_V1 local_snapshot;
    NLD_DIRECT_PDO_DIAGNOSTIC_SNAPSHOT diagnostic_snapshot;
    NTSTATUS status;

    if (response == NULL) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(response, sizeof(*response));
    response->Size = sizeof(*response);
    response->Version = NLD_DIRECT_PDO_CONTROL_ABI_VERSION;
    if (snapshot != NULL) {
        RtlZeroMemory(snapshot, sizeof(*snapshot));
        snapshot->Size = sizeof(*snapshot);
        snapshot->Version = NLD_DIRECT_PDO_CONTROL_ABI_VERSION;
        snapshot->Disposition =
            NldDirectPdoControlDispositionOffline;
    }
    if (request == NULL) {
        response->Status = STATUS_INVALID_PARAMETER;
        return STATUS_INVALID_PARAMETER;
    }
    validation = NldDirectPdoControlValidateRequest(request,
                                                     request_size,
                                                     access);
    if (validation != NldDirectPdoControlValidationOk) {
        status = NldDirectPdoControlValidationStatus(validation);
        response->Status = status;
        return status;
    }
    response->Command = request->Command;
    response->ClientToken = request->ClientToken;
    if (diagnostic == NULL || arbiter == NULL) {
        response->Disposition =
            NldDirectPdoControlDispositionOffline;
        response->Status = STATUS_DEVICE_NOT_READY;
        return STATUS_DEVICE_NOT_READY;
    }

    NldDirectPdoControlBuildSnapshot(diagnostic,
                                     arbiter,
                                     &local_snapshot);
    response->Disposition = local_snapshot.Disposition;
    response->RequestGeneration = local_snapshot.Generation;
    response->ResultGeneration = local_snapshot.ResultGeneration;
    if (snapshot != NULL) {
        RtlCopyMemory(snapshot,
                      &local_snapshot,
                      sizeof(local_snapshot));
    }
    if (request->Command ==
        NldDirectPdoControlCommandQuerySnapshot) {
        if (snapshot == NULL) {
            response->Status = STATUS_BUFFER_TOO_SMALL;
            return STATUS_BUFFER_TOO_SMALL;
        }
        response->Status = STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }

    status = NldDirectPdoControlDispositionStatus(
        (NLD_DIRECT_PDO_CONTROL_DISPOSITION)
            local_snapshot.Disposition);
    if (!NT_SUCCESS(status)) {
        response->Status = status;
        return status;
    }
    status = NldDirectPdoDiagnosticRuntimeRequestDiscover(
        diagnostic);
    if (NT_SUCCESS(status)) {
        NldDirectPdoDiagnosticRuntimeGetSnapshot(
            diagnostic,
            &diagnostic_snapshot);
        response->Disposition =
            NldDirectPdoControlDispositionAccepted;
        response->RequestGeneration =
            diagnostic_snapshot.Generation;
        if (snapshot != NULL) {
            NldDirectPdoControlBuildSnapshot(diagnostic,
                                             arbiter,
                                             snapshot);
        }
    }
    response->Status = status;
    return status;
}
