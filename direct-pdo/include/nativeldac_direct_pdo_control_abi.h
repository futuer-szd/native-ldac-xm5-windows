// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_DIRECT_PDO_CONTROL_ABI_H
#define NATIVE_LDAC_DIRECT_PDO_CONTROL_ABI_H

#if defined(_MSC_VER)
typedef unsigned __int32 NLD_DIRECT_PDO_CONTROL_U32;
typedef signed __int32 NLD_DIRECT_PDO_CONTROL_I32;
#else
#include <stdint.h>
typedef uint32_t NLD_DIRECT_PDO_CONTROL_U32;
typedef int32_t NLD_DIRECT_PDO_CONTROL_I32;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NLD_DIRECT_PDO_CONTROL_ABI_VERSION 1ul
#define NLD_DIRECT_PDO_CONTROL_RESPONSE_PREFIX 32ul

#define NLD_DIRECT_PDO_CONTROL_FLAG_EXPLICIT_REQUEST 0x00000001ul

#define NLD_DIRECT_PDO_CONTROL_SNAPSHOT_STARTED 0x00000001ul
#define NLD_DIRECT_PDO_CONTROL_SNAPSHOT_WORKER_OWNED 0x00000002ul
#define NLD_DIRECT_PDO_CONTROL_SNAPSHOT_STOP_REQUESTED 0x00000004ul
#define NLD_DIRECT_PDO_CONTROL_SNAPSHOT_RESPONSE_TRUNCATED 0x00000008ul
#define NLD_DIRECT_PDO_CONTROL_SNAPSHOT_RENDER_DEMAND 0x00000010ul
#define NLD_DIRECT_PDO_CONTROL_SNAPSHOT_PNP_STARTED 0x00000020ul
#define NLD_DIRECT_PDO_CONTROL_SNAPSHOT_CANCEL_REQUESTED 0x00000040ul

typedef enum NLD_DIRECT_PDO_CONTROL_COMMAND {
    NldDirectPdoControlCommandQuerySnapshot = 1,
    NldDirectPdoControlCommandRequestDiscover = 2
} NLD_DIRECT_PDO_CONTROL_COMMAND;

typedef enum NLD_DIRECT_PDO_CONTROL_ACCESS {
    NldDirectPdoControlAccessNone = 0,
    NldDirectPdoControlAccessQuery = 1,
    NldDirectPdoControlAccessExecute = 2
} NLD_DIRECT_PDO_CONTROL_ACCESS;

typedef enum NLD_DIRECT_PDO_CONTROL_VALIDATION {
    NldDirectPdoControlValidationOk = 0,
    NldDirectPdoControlValidationNull = 1,
    NldDirectPdoControlValidationBufferTooSmall = 2,
    NldDirectPdoControlValidationSize = 3,
    NldDirectPdoControlValidationVersion = 4,
    NldDirectPdoControlValidationCommand = 5,
    NldDirectPdoControlValidationFlags = 6,
    NldDirectPdoControlValidationAccess = 7,
    NldDirectPdoControlValidationReserved = 8
} NLD_DIRECT_PDO_CONTROL_VALIDATION;

typedef enum NLD_DIRECT_PDO_CONTROL_DISPOSITION {
    NldDirectPdoControlDispositionOffline = 0,
    NldDirectPdoControlDispositionIdle = 1,
    NldDirectPdoControlDispositionAccepted = 2,
    NldDirectPdoControlDispositionBusyRender = 3,
    NldDirectPdoControlDispositionBusyDiagnostic = 4,
    NldDirectPdoControlDispositionPreemptDiagnostic = 5,
    NldDirectPdoControlDispositionStopping = 6,
    NldDirectPdoControlDispositionComplete = 7,
    NldDirectPdoControlDispositionFaulted = 8
} NLD_DIRECT_PDO_CONTROL_DISPOSITION;

#pragma pack(push, 4)

typedef struct NLD_DIRECT_PDO_CONTROL_REQUEST_V1 {
    NLD_DIRECT_PDO_CONTROL_U32 Size;
    NLD_DIRECT_PDO_CONTROL_U32 Version;
    NLD_DIRECT_PDO_CONTROL_U32 Command;
    NLD_DIRECT_PDO_CONTROL_U32 Flags;
    NLD_DIRECT_PDO_CONTROL_U32 ClientToken;
    NLD_DIRECT_PDO_CONTROL_U32 Reserved[3];
} NLD_DIRECT_PDO_CONTROL_REQUEST_V1;

typedef struct NLD_DIRECT_PDO_CONTROL_RESPONSE_V1 {
    NLD_DIRECT_PDO_CONTROL_U32 Size;
    NLD_DIRECT_PDO_CONTROL_U32 Version;
    NLD_DIRECT_PDO_CONTROL_U32 Command;
    NLD_DIRECT_PDO_CONTROL_U32 Disposition;
    NLD_DIRECT_PDO_CONTROL_I32 Status;
    NLD_DIRECT_PDO_CONTROL_U32 RequestGeneration;
    NLD_DIRECT_PDO_CONTROL_U32 ResultGeneration;
    NLD_DIRECT_PDO_CONTROL_U32 ClientToken;
} NLD_DIRECT_PDO_CONTROL_RESPONSE_V1;

typedef struct NLD_DIRECT_PDO_CONTROL_SNAPSHOT_V1 {
    NLD_DIRECT_PDO_CONTROL_U32 Size;
    NLD_DIRECT_PDO_CONTROL_U32 Version;
    NLD_DIRECT_PDO_CONTROL_U32 Flags;
    NLD_DIRECT_PDO_CONTROL_U32 Disposition;
    NLD_DIRECT_PDO_CONTROL_U32 DiagnosticState;
    NLD_DIRECT_PDO_CONTROL_U32 PendingAction;
    NLD_DIRECT_PDO_CONTROL_U32 ActiveAction;
    NLD_DIRECT_PDO_CONTROL_U32 LastAction;
    NLD_DIRECT_PDO_CONTROL_U32 ArbiterState;
    NLD_DIRECT_PDO_CONTROL_U32 ArbiterClient;
    NLD_DIRECT_PDO_CONTROL_U32 Generation;
    NLD_DIRECT_PDO_CONTROL_U32 ResultGeneration;
    NLD_DIRECT_PDO_CONTROL_U32 ArbiterGeneration;
    NLD_DIRECT_PDO_CONTROL_U32 ResponseLength;
    NLD_DIRECT_PDO_CONTROL_U32 PayloadOffset;
    NLD_DIRECT_PDO_CONTROL_U32 ResponsePrefixLength;
    NLD_DIRECT_PDO_CONTROL_I32 LastStatus;
    NLD_DIRECT_PDO_CONTROL_I32 DiscoverStatus;
    NLD_DIRECT_PDO_CONTROL_I32 CloseStatus;
    unsigned char ResponsePrefix[
        NLD_DIRECT_PDO_CONTROL_RESPONSE_PREFIX];
    NLD_DIRECT_PDO_CONTROL_U32 Reserved[5];
} NLD_DIRECT_PDO_CONTROL_SNAPSHOT_V1;

#pragma pack(pop)

NLD_DIRECT_PDO_CONTROL_VALIDATION
NldDirectPdoControlValidateRequest(
    const NLD_DIRECT_PDO_CONTROL_REQUEST_V1* request,
    NLD_DIRECT_PDO_CONTROL_U32 buffer_size,
    NLD_DIRECT_PDO_CONTROL_ACCESS access);

NLD_DIRECT_PDO_CONTROL_DISPOSITION
NldDirectPdoControlDeriveDisposition(
    NLD_DIRECT_PDO_CONTROL_U32 diagnostic_state,
    NLD_DIRECT_PDO_CONTROL_U32 arbiter_state,
    NLD_DIRECT_PDO_CONTROL_U32 arbiter_client,
    int render_demand,
    int started);

#ifdef __cplusplus
}
#endif

#endif
