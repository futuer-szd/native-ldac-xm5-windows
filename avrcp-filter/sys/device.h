// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_AVRCP_FILTER_DEVICE_H
#define NATIVE_LDAC_AVRCP_FILTER_DEVICE_H

#include <ntddk.h>
#include <wdf.h>

#include "nativeldac_avrcp_filter_ioctl.h"
#include "nativeldac_avrcp_filter_trace_contract.h"

typedef struct _NLD_AVRCP_FILTER_DEVICE_CONTEXT {
    WDFDEVICE Device;
    WDFSPINLOCK TraceLock;
    WDFDEVICE ControlDevice;
    WDFQUEUE ControlQueue;
    NLD_AVRCP_FILTER_TRACE_QUEUE TraceQueue;
    BOOLEAN TraceReady;
    BOOLEAN Online;
    UCHAR NextTransactionLabel;
    BOOLEAN WriteActive;
} NLD_AVRCP_FILTER_DEVICE_CONTEXT,
  *PNLD_AVRCP_FILTER_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    NLD_AVRCP_FILTER_DEVICE_CONTEXT,
    NldAvrcpFilterGetDeviceContext);

typedef struct _NLD_AVRCP_FILTER_REQUEST_CONTEXT {
    ULONGLONG RequestId;
    ULONG ControlCode;
    ULONG Flags;
    ULONG InputSize;
    ULONG OutputSize;
} NLD_AVRCP_FILTER_REQUEST_CONTEXT,
  *PNLD_AVRCP_FILTER_REQUEST_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    NLD_AVRCP_FILTER_REQUEST_CONTEXT,
    NldAvrcpFilterGetRequestContext);

typedef struct _NLD_AVRCP_FILTER_CONTROL_CONTEXT {
    WDFDEVICE FilterDevice;
} NLD_AVRCP_FILTER_CONTROL_CONTEXT,
  *PNLD_AVRCP_FILTER_CONTROL_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    NLD_AVRCP_FILTER_CONTROL_CONTEXT,
    NldAvrcpFilterGetControlContext);

EVT_WDF_DRIVER_DEVICE_ADD NldAvrcpFilterEvtDeviceAdd;
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT
    NldAvrcpFilterEvtSelfManagedIoInit;
EVT_WDF_DEVICE_SELF_MANAGED_IO_SUSPEND
    NldAvrcpFilterEvtSelfManagedIoSuspend;
EVT_WDF_DEVICE_SELF_MANAGED_IO_RESTART
    NldAvrcpFilterEvtSelfManagedIoRestart;
EVT_WDF_DEVICE_SELF_MANAGED_IO_CLEANUP
    NldAvrcpFilterEvtSelfManagedIoCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL
    NldAvrcpFilterEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL
    NldAvrcpFilterEvtIoInternalDeviceControl;
EVT_WDF_REQUEST_COMPLETION_ROUTINE
    NldAvrcpFilterRequestCompletion;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL
    NldAvrcpFilterControlEvtIoDeviceControl;

#endif
