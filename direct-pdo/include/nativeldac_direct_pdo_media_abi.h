// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_DIRECT_PDO_MEDIA_ABI_H
#define NATIVE_LDAC_DIRECT_PDO_MEDIA_ABI_H

#if defined(_MSC_VER)
typedef unsigned __int32 NLD_DIRECT_PDO_MEDIA_U32;
typedef signed __int32 NLD_DIRECT_PDO_MEDIA_I32;
typedef unsigned __int64 NLD_DIRECT_PDO_MEDIA_U64;
#else
#include <stdint.h>
typedef uint32_t NLD_DIRECT_PDO_MEDIA_U32;
typedef int32_t NLD_DIRECT_PDO_MEDIA_I32;
typedef uint64_t NLD_DIRECT_PDO_MEDIA_U64;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NLD_DIRECT_PDO_MEDIA_ABI_VERSION 3ul
#define NLD_DIRECT_PDO_MEDIA_PACKET_HEADER_SIZE 32ul
#define NLD_DIRECT_PDO_MEDIA_MAX_PACKET_SIZE 1021ul
#define NLD_DIRECT_PDO_RECOVERY_REQUEST_SIZE 32ul

#define NLD_DIRECT_PDO_MEDIA_STATUS_PNP_STARTED 0x00000001ul
#define NLD_DIRECT_PDO_MEDIA_STATUS_WAVERT_RUN 0x00000002ul
#define NLD_DIRECT_PDO_MEDIA_STATUS_CHANNEL_OPEN 0x00000004ul
#define NLD_DIRECT_PDO_MEDIA_STATUS_STREAMING 0x00000008ul
#define NLD_DIRECT_PDO_MEDIA_STATUS_STOPPING 0x00000010ul
#define NLD_DIRECT_PDO_MEDIA_STATUS_BACKEND_ACTIVE 0x00000020ul
#define NLD_DIRECT_PDO_MEDIA_STATUS_OPEN_ATTEMPTS_MASK 0x0000FF00ul
#define NLD_DIRECT_PDO_MEDIA_STATUS_OPEN_ATTEMPTS_SHIFT 8ul
#define NLD_DIRECT_PDO_MEDIA_STATUS_PROTOCOL_PHASE_MASK 0x000F0000ul
#define NLD_DIRECT_PDO_MEDIA_STATUS_PROTOCOL_PHASE_SHIFT 16ul
#define NLD_DIRECT_PDO_MEDIA_STATUS_COMMANDS_COMPLETED_MASK 0x00F00000ul
#define NLD_DIRECT_PDO_MEDIA_STATUS_COMMANDS_COMPLETED_SHIFT 20ul
#define NLD_DIRECT_PDO_MEDIA_STATUS_SIGNAL_ID_MASK 0x3F000000ul
#define NLD_DIRECT_PDO_MEDIA_STATUS_SIGNAL_ID_SHIFT 24ul

typedef enum NLD_DIRECT_PDO_MEDIA_STATE {
    NldDirectPdoMediaOffline = 0,
    NldDirectPdoMediaIdle = 1,
    NldDirectPdoMediaOpen = 2,
    NldDirectPdoMediaStreaming = 3,
    NldDirectPdoMediaStopping = 4,
    NldDirectPdoMediaFaulted = 5
} NLD_DIRECT_PDO_MEDIA_STATE;

typedef enum NLD_DIRECT_PDO_FAILURE_REASON {
    NldDirectPdoFailureNone = 0,
    NldDirectPdoFailureRemoteDisconnect = 1,
    NldDirectPdoFailureMediaTimeout = 2,
    NldDirectPdoFailureBackend = 3
} NLD_DIRECT_PDO_FAILURE_REASON;

typedef enum NLD_DIRECT_PDO_MEDIA_BACKEND_ACTION {
    NldDirectPdoMediaBackendActionNone = 0,
    NldDirectPdoMediaBackendActionOpen = 1,
    NldDirectPdoMediaBackendActionStart = 2,
    NldDirectPdoMediaBackendActionSuspend = 3,
    NldDirectPdoMediaBackendActionClose = 4,
    NldDirectPdoMediaBackendActionCancelAndClose = 5
} NLD_DIRECT_PDO_MEDIA_BACKEND_ACTION;

typedef enum NLD_DIRECT_PDO_PROTOCOL_PHASE {
    NldDirectPdoProtocolPhaseNone = 0,
    NldDirectPdoProtocolPhaseWrite = 1,
    NldDirectPdoProtocolPhaseRead = 2,
    NldDirectPdoProtocolPhaseHandle = 3,
    NldDirectPdoProtocolPhaseOpenMedia = 4,
    NldDirectPdoProtocolPhaseComplete = 5
} NLD_DIRECT_PDO_PROTOCOL_PHASE;

typedef enum NLD_DIRECT_PDO_MEDIA_VALIDATION {
    NldDirectPdoMediaValidationOk = 0,
    NldDirectPdoMediaValidationNull = 1,
    NldDirectPdoMediaValidationBufferTooSmall = 2,
    NldDirectPdoMediaValidationSize = 3,
    NldDirectPdoMediaValidationVersion = 4,
    NldDirectPdoMediaValidationFlags = 5,
    NldDirectPdoMediaValidationReserved = 6,
    NldDirectPdoMediaValidationPayload = 7,
    NldDirectPdoMediaValidationOffline = 8,
    NldDirectPdoMediaValidationGeneration = 9,
    NldDirectPdoMediaValidationMtu = 10
} NLD_DIRECT_PDO_MEDIA_VALIDATION;

typedef enum NLD_DIRECT_PDO_RECOVERY_VALIDATION {
    NldDirectPdoRecoveryValidationOk = 0,
    NldDirectPdoRecoveryValidationNull = 1,
    NldDirectPdoRecoveryValidationBufferTooSmall = 2,
    NldDirectPdoRecoveryValidationSize = 3,
    NldDirectPdoRecoveryValidationVersion = 4,
    NldDirectPdoRecoveryValidationFlags = 5,
    NldDirectPdoRecoveryValidationReserved = 6,
    NldDirectPdoRecoveryValidationGeneration = 7,
    NldDirectPdoRecoveryValidationReason = 8
} NLD_DIRECT_PDO_RECOVERY_VALIDATION;

#pragma pack(push, 4)

typedef struct NLD_DIRECT_PDO_MEDIA_STATUS_V1 {
    NLD_DIRECT_PDO_MEDIA_U32 Size;
    NLD_DIRECT_PDO_MEDIA_U32 Version;
    NLD_DIRECT_PDO_MEDIA_U32 State;
    NLD_DIRECT_PDO_MEDIA_U32 Flags;
    NLD_DIRECT_PDO_MEDIA_U32 MediaGeneration;
    NLD_DIRECT_PDO_MEDIA_U32 OutgoingMtu;
    NLD_DIRECT_PDO_MEDIA_I32 LastStatus;
    NLD_DIRECT_PDO_MEDIA_U32 SessionGeneration;
    NLD_DIRECT_PDO_MEDIA_U64 PacketsAccepted;
    NLD_DIRECT_PDO_MEDIA_U64 BytesAccepted;
    NLD_DIRECT_PDO_MEDIA_U32 FailureReason;
    NLD_DIRECT_PDO_MEDIA_U32 LastBackendAction;
    NLD_DIRECT_PDO_MEDIA_I32 LastBackendStatus;
    NLD_DIRECT_PDO_MEDIA_I32 LastSignalingOpenStatus;
} NLD_DIRECT_PDO_MEDIA_STATUS_V1;

typedef struct NLD_DIRECT_PDO_MEDIA_PACKET_V1 {
    NLD_DIRECT_PDO_MEDIA_U32 Size;
    NLD_DIRECT_PDO_MEDIA_U32 Version;
    NLD_DIRECT_PDO_MEDIA_U32 MediaGeneration;
    NLD_DIRECT_PDO_MEDIA_U32 PayloadLength;
    NLD_DIRECT_PDO_MEDIA_U32 Flags;
    NLD_DIRECT_PDO_MEDIA_U32 Reserved[3];
    unsigned char Payload[1];
} NLD_DIRECT_PDO_MEDIA_PACKET_V1;

typedef struct NLD_DIRECT_PDO_RECOVERY_REQUEST_V1 {
    NLD_DIRECT_PDO_MEDIA_U32 Size;
    NLD_DIRECT_PDO_MEDIA_U32 Version;
    NLD_DIRECT_PDO_MEDIA_U32 ExpectedSessionGeneration;
    NLD_DIRECT_PDO_MEDIA_U32 ExpectedFailureReason;
    NLD_DIRECT_PDO_MEDIA_U32 Flags;
    NLD_DIRECT_PDO_MEDIA_U32 Reserved[3];
} NLD_DIRECT_PDO_RECOVERY_REQUEST_V1;

#pragma pack(pop)

NLD_DIRECT_PDO_MEDIA_VALIDATION
NldDirectPdoMediaValidatePacket(
    const NLD_DIRECT_PDO_MEDIA_PACKET_V1* packet,
    NLD_DIRECT_PDO_MEDIA_U32 buffer_size,
    const NLD_DIRECT_PDO_MEDIA_STATUS_V1* status);

NLD_DIRECT_PDO_RECOVERY_VALIDATION
NldDirectPdoValidateRecoveryRequest(
    const NLD_DIRECT_PDO_RECOVERY_REQUEST_V1* request,
    NLD_DIRECT_PDO_MEDIA_U32 buffer_size,
    NLD_DIRECT_PDO_MEDIA_U32 observed_session_generation,
    NLD_DIRECT_PDO_MEDIA_U32 observed_failure_reason);

#ifdef __cplusplus
}
#endif

#endif
