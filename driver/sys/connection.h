// SPDX-License-Identifier: MS-PL
// Derived in part from Microsoft's Windows-driver-samples BthEcho sample.
#ifndef LDAC_NATIVE_DRIVER_CONNECTION_H
#define LDAC_NATIVE_DRIVER_CONNECTION_H

#include "device.h"

typedef enum _LDAC_NATIVE_BRB_OPERATION {
    LdacNativeBrbOperationNone = 0,
    LdacNativeBrbOperationOpenSignaling,
    LdacNativeBrbOperationReadSignaling,
    LdacNativeBrbOperationWriteSignaling,
    LdacNativeBrbOperationOpenMedia,
    LdacNativeBrbOperationWriteMedia
} LDAC_NATIVE_BRB_OPERATION;

typedef struct _LDAC_NATIVE_BRB_REQUEST_CONTEXT {
    PLDAC_NATIVE_DEVICE_CONTEXT DeviceContext;
    PBRB Brb;
    WDFMEMORY BrbMemory;
    WDFMEMORY TransferMemory;
    LDAC_NATIVE_BRB_OPERATION Operation;
    PVOID OutputBuffer;
    size_t OutputBufferLength;
} LDAC_NATIVE_BRB_REQUEST_CONTEXT, *PLDAC_NATIVE_BRB_REQUEST_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(LDAC_NATIVE_BRB_REQUEST_CONTEXT,
                                   LdacNativeGetBrbRequestContext);

typedef struct _LDAC_NATIVE_INCOMING_REQUEST_CONTEXT {
    PLDAC_NATIVE_DEVICE_CONTEXT DeviceContext;
    PBRB Brb;
    WDFMEMORY BrbMemory;
    BOOLEAN Accepted;
} LDAC_NATIVE_INCOMING_REQUEST_CONTEXT,
  *PLDAC_NATIVE_INCOMING_REQUEST_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    LDAC_NATIVE_INCOMING_REQUEST_CONTEXT,
    LdacNativeGetIncomingRequestContext);

NTSTATUS LdacNativeConnectionInitialize(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context);

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID LdacNativeSetLifecycleState(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ BOOLEAN PnpStarted,
    _In_ BOOLEAN ShuttingDown);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeRegisterSignalingServer(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeUnregisterSignalingServer(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ BOOLEAN PreserveReadyFlag);

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID LdacNativeGetOpenDiagnostics(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _Out_ PLDAC_NATIVE_OPEN_DIAGNOSTICS Diagnostics);

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID LdacNativeGetTransferDiagnostics(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _Out_ PLDAC_NATIVE_TRANSFER_DIAGNOSTICS Diagnostics);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeSendBrbSynchronously(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ PBRB Brb,
    _In_ size_t BrbSize,
    _In_ ULONG TimeoutMs);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeOpenSignaling(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeCloseSignaling(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ ULONG TimeoutMs);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeOpenMedia(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeWriteMedia(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeCloseChannels(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ ULONG TimeoutMs);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS LdacNativeTransferSignaling(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength,
    _In_ BOOLEAN ReadTransfer);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID LdacNativeConnectionShutdown(
    _In_ PLDAC_NATIVE_DEVICE_CONTEXT Context);

#endif
