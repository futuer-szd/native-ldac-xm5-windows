// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_BTH_SIGNALING_H
#define NATIVE_LDAC_BTH_SIGNALING_H

#include <ntddk.h>
#include <bthdef.h>
#include <bthddi.h>

#include "nativeldac_bth_profile.h"
#include "nativeldac_bth_signaling_contract.h"
#include "nativeldac_bth_transfer_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NLD_BTH_AVDTP_PSM 0x0019u
#define NLD_BTH_MAX_SIGNALING_MTU 4096u
#define NLD_BTH_DISCOVER_TIMEOUT_MS 2000u

typedef struct _NLD_BTH_ASYNC_OPEN_REQUEST
    NLD_BTH_ASYNC_OPEN_REQUEST;

typedef void (*NLD_BTH_DISCONNECT_CALLBACK)(
    _In_opt_ PVOID callback_context,
    _In_ ULONG channel_generation);

typedef struct _NLD_BTH_SIGNALING_CONTEXT {
    KMUTEX OperationMutex;
    KSPIN_LOCK Lock;
    KEVENT RequestDrainedEvent;
    NLD_BTH_SIGNALING_OWNER Owner;
    NLD_BTH_TRANSFER_OWNER TransferOwner;
    const NLD_BTH_PROFILE_CONTEXT* Profile;
    PDEVICE_OBJECT ReferenceDeviceObject;
    USHORT Psm;
    NLD_BTH_ASYNC_OPEN_REQUEST* PendingOpen;
    NLD_BTH_ASYNC_OPEN_REQUEST* ActiveChannel;
    L2CAP_CHANNEL_HANDLE ChannelHandle;
    USHORT IncomingMtu;
    USHORT OutgoingMtu;
    NTSTATUS LastOpenStatus;
    NTSTATUS LastCloseStatus;
    NLD_BTH_DISCONNECT_CALLBACK DisconnectCallback;
    PVOID DisconnectCallbackContext;
} NLD_BTH_SIGNALING_CONTEXT, *PNLD_BTH_SIGNALING_CONTEXT;

typedef struct _NLD_BTH_SIGNALING_SNAPSHOT {
    NLD_BTH_SIGNALING_STATE State;
    ULONG Generation;
    USHORT Psm;
    USHORT IncomingMtu;
    USHORT OutgoingMtu;
    BOOLEAN OpenPending;
    BOOLEAN ChannelHeld;
    BOOLEAN RemoteDisconnected;
    NTSTATUS LastOpenStatus;
    NTSTATUS LastCloseStatus;
} NLD_BTH_SIGNALING_SNAPSHOT, *PNLD_BTH_SIGNALING_SNAPSHOT;

void NldBthSignalingInitialize(
    _Out_ PNLD_BTH_SIGNALING_CONTEXT context);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSignalingStart(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ const NLD_BTH_PROFILE_CONTEXT* profile,
    _In_ PDEVICE_OBJECT reference_device_object);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthChannelStart(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ const NLD_BTH_PROFILE_CONTEXT* profile,
    _In_ PDEVICE_OBJECT reference_device_object,
    _In_ USHORT psm);

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldBthSignalingSetDisconnectCallback(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_opt_ NLD_BTH_DISCONNECT_CALLBACK callback,
    _In_opt_ PVOID callback_context);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSignalingOpen(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ USHORT preferred_mtu);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSignalingClose(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ ULONG timeout_ms);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSignalingWaitForRequestDrain(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ ULONG timeout_ms);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSignalingWrite(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_reads_bytes_(buffer_length) const void* buffer,
    _In_ ULONG buffer_length,
    _In_ ULONG timeout_ms);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSignalingWriteGeneration(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ ULONG expected_generation,
    _In_reads_bytes_(buffer_length) const void* buffer,
    _In_ ULONG buffer_length,
    _In_ ULONG timeout_ms);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSignalingRead(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _Out_writes_bytes_(buffer_capacity) void* buffer,
    _In_ ULONG buffer_capacity,
    _In_ ULONG timeout_ms,
    _Out_ ULONG* bytes_read);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSignalingDiscover(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _Out_writes_bytes_(response_capacity) unsigned char* response,
    _In_ ULONG response_capacity,
    _In_ ULONG timeout_ms,
    _Out_ ULONG* response_length,
    _Out_ ULONG* payload_offset);

_IRQL_requires_max_(PASSIVE_LEVEL)
void NldBthSignalingStop(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _In_ ULONG timeout_ms);

_IRQL_requires_max_(DISPATCH_LEVEL)
void NldBthSignalingGetSnapshot(
    _Inout_ PNLD_BTH_SIGNALING_CONTEXT context,
    _Out_ PNLD_BTH_SIGNALING_SNAPSHOT snapshot);

#ifdef __cplusplus
}
#endif

#endif
