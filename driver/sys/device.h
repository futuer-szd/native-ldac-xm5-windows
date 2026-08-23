// SPDX-License-Identifier: MS-PL
// Portions derived from Microsoft's Windows-driver-samples Bluetooth
// BthEcho sample. Copyright (c) Microsoft Corporation.
#ifndef LDAC_NATIVE_DRIVER_DEVICE_H
#define LDAC_NATIVE_DRIVER_DEVICE_H

#include <ntddk.h>
#include <wdf.h>
#include <bthdef.h>
#include <bthguid.h>
#include <bthioctl.h>
#include <bthddi.h>
#include "../inc/ldac_native_ioctl.h"

#define LDAC_NATIVE_POOL_TAG 'NcdL'
#define LDAC_NATIVE_AVDTP_PSM 0x0019u

typedef enum _LDAC_NATIVE_CHANNEL_STATE {
    LdacNativeChannelDisconnected = LDAC_NATIVE_CHANNEL_DISCONNECTED,
    LdacNativeChannelConnecting = LDAC_NATIVE_CHANNEL_CONNECTING,
    LdacNativeChannelConnected = LDAC_NATIVE_CHANNEL_CONNECTED,
    LdacNativeChannelDisconnecting = LDAC_NATIVE_CHANNEL_DISCONNECTING
} LDAC_NATIVE_CHANNEL_STATE;

typedef struct _LDAC_NATIVE_DEVICE_CONTEXT {
    WDFDEVICE Device;
    WDFIOTARGET IoTarget;
    WDFREQUEST InitializationRequest;
    BTH_PROFILE_DRIVER_INTERFACE ProfileInterface;
    BOOLEAN ProfileInterfaceReferenced;
    BTH_ADDR RemoteAddress;
    BTH_ADDR LocalAddress;
    ULONG InfoFlags;
    WDFSPINLOCK SignalingLock;
    WDFWAITLOCK OperationLock;
    BOOLEAN PnpStarted;
    BOOLEAN ShuttingDown;
    L2CAP_SERVER_HANDLE SignalingServerHandle;
    BOOLEAN SignalingServerDraining;
    BOOLEAN SignalingServerRundownQueued;
    WDFWORKITEM SignalingServerRundownWorkItem;
    NTSTATUS SignalingServerRundownStatus;
    LDAC_NATIVE_CHANNEL_STATE SignalingState;
    BOOLEAN SignalingChannelIsIncoming;
    L2CAP_CHANNEL_HANDLE SignalingChannelHandle;
    USHORT SignalingIncomingMtu;
    USHORT SignalingOutgoingMtu;
    BOOLEAN SignalingReadPending;
    BOOLEAN SignalingWritePending;
    LDAC_NATIVE_TRANSFER_RESULT SignalingReadDiagnostics;
    LDAC_NATIVE_TRANSFER_RESULT SignalingWriteDiagnostics;
    LDAC_NATIVE_OPEN_DIAGNOSTICS OpenDiagnostics;
    WDFREQUEST SignalingOpenRequest;
    WDFREQUEST IncomingSignalingRequest;
    WDFREQUEST SignalingReadRequest;
    WDFREQUEST SignalingWriteRequest;
    KEVENT SignalingDisconnectedEvent;
    KEVENT SignalingOpenCompletedEvent;
    KEVENT IncomingSignalingCompletedEvent;
    KEVENT SignalingReadCompletedEvent;
    KEVENT SignalingWriteCompletedEvent;
    LDAC_NATIVE_CHANNEL_STATE MediaState;
    L2CAP_CHANNEL_HANDLE MediaChannelHandle;
    USHORT MediaIncomingMtu;
    USHORT MediaOutgoingMtu;
    BOOLEAN MediaWritePending;
    LDAC_NATIVE_TRANSFER_RESULT MediaWriteDiagnostics;
    WDFREQUEST MediaOpenRequest;
    WDFREQUEST MediaWriteRequest;
    KEVENT MediaDisconnectedEvent;
    KEVENT MediaOpenCompletedEvent;
    KEVENT MediaWriteCompletedEvent;
} LDAC_NATIVE_DEVICE_CONTEXT, *PLDAC_NATIVE_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(LDAC_NATIVE_DEVICE_CONTEXT,
                                   LdacNativeGetDeviceContext);

typedef struct _LDAC_NATIVE_FILE_CONTEXT {
    BOOLEAN OwnsTransport;
} LDAC_NATIVE_FILE_CONTEXT, *PLDAC_NATIVE_FILE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(LDAC_NATIVE_FILE_CONTEXT,
                                   LdacNativeGetFileContext);

EVT_WDF_DRIVER_DEVICE_ADD LdacNativeEvtDeviceAdd;
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT LdacNativeEvtSelfManagedIoInit;
EVT_WDF_DEVICE_SELF_MANAGED_IO_SUSPEND LdacNativeEvtSelfManagedIoSuspend;
EVT_WDF_DEVICE_SELF_MANAGED_IO_RESTART LdacNativeEvtSelfManagedIoRestart;
EVT_WDF_DEVICE_SELF_MANAGED_IO_CLEANUP LdacNativeEvtSelfManagedIoCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL LdacNativeEvtIoDeviceControl;
EVT_WDF_FILE_CLEANUP LdacNativeEvtFileCleanup;
EVT_WDF_OBJECT_CONTEXT_CLEANUP LdacNativeEvtDeviceContextCleanup;

#endif
