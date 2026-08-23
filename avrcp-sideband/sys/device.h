// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_AVRCP_OBSERVER_DEVICE_H
#define NATIVE_LDAC_AVRCP_OBSERVER_DEVICE_H

#include <ntddk.h>
#include <wdf.h>

#include "nativeldac_avrcp_observer_ioctl.h"
#include "nativeldac_avrcp_event_queue_contract.h"
#include "nativeldac_bth_profile.h"
#include "nativeldac_bth_signaling.h"
#include "ldac_native/avrcp.h"

#define NLD_AVRCP_CONTROL_PSM 0x0017u
#define NLD_AVRCP_READ_TIMEOUT_MS 1000u
#define NLD_AVRCP_WRITE_TIMEOUT_MS 2000u

typedef struct _NLD_AVRCP_OBSERVER_DEVICE_CONTEXT {
    WDFDEVICE Device;
    WDFSPINLOCK StateLock;
    WDFWORKITEM ObserverWorkItem;
    KEVENT StopEvent;
    volatile LONG WorkerRunning;
    NLD_BTH_PROFILE_CONTEXT Profile;
    NLD_BTH_SIGNALING_CONTEXT Channel;
    NLD_AVRCP_EVENT_QUEUE EventQueue;
    avrcp_observer Observer;
    BOOLEAN ActivationRequested;
    BOOLEAN PendingWriteValid;
    BOOLEAN WriteTransactionActive;
    NLD_AVRCP_OBSERVER_WRITE_REQUEST PendingWrite;
    ULONGLONG AclGeneration;
    ULONG RuntimeFlags;
    LONG LastProtocolStatus;
} NLD_AVRCP_OBSERVER_DEVICE_CONTEXT,
  *PNLD_AVRCP_OBSERVER_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    NLD_AVRCP_OBSERVER_DEVICE_CONTEXT,
    NldAvrcpObserverGetDeviceContext);

EVT_WDF_DRIVER_DEVICE_ADD NldAvrcpObserverEvtDeviceAdd;
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT NldAvrcpObserverEvtSelfManagedIoInit;
EVT_WDF_DEVICE_SELF_MANAGED_IO_SUSPEND
    NldAvrcpObserverEvtSelfManagedIoSuspend;
EVT_WDF_DEVICE_SELF_MANAGED_IO_RESTART
    NldAvrcpObserverEvtSelfManagedIoRestart;
EVT_WDF_DEVICE_SELF_MANAGED_IO_CLEANUP
    NldAvrcpObserverEvtSelfManagedIoCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL NldAvrcpObserverEvtIoDeviceControl;
EVT_WDF_WORKITEM NldAvrcpObserverWorker;
EVT_WDF_FILE_CLEANUP NldAvrcpObserverEvtFileCleanup;

#endif
