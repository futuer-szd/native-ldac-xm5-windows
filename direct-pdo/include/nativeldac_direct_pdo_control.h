// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_DIRECT_PDO_CONTROL_H
#define NATIVE_LDAC_DIRECT_PDO_CONTROL_H

#include <ntddk.h>

#include "nativeldac_direct_pdo_arbiter.h"
#include "nativeldac_direct_pdo_control_abi.h"
#include "nativeldac_direct_pdo_diagnostic.h"

#ifdef __cplusplus
extern "C" {
#endif

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldDirectPdoControlBuildSnapshot(
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT diagnostic,
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT arbiter,
    _Out_ NLD_DIRECT_PDO_CONTROL_SNAPSHOT_V1* snapshot);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS NldDirectPdoControlExecute(
    _Inout_ PNLD_DIRECT_PDO_DIAGNOSTIC_CONTEXT diagnostic,
    _Inout_ PNLD_DIRECT_PDO_ARBITER_CONTEXT arbiter,
    _In_ const NLD_DIRECT_PDO_CONTROL_REQUEST_V1* request,
    _In_ NLD_DIRECT_PDO_CONTROL_U32 request_size,
    _In_ NLD_DIRECT_PDO_CONTROL_ACCESS access,
    _Out_ NLD_DIRECT_PDO_CONTROL_RESPONSE_V1* response,
    _Out_opt_ NLD_DIRECT_PDO_CONTROL_SNAPSHOT_V1* snapshot);

#ifdef __cplusplus
}
#endif

#endif
