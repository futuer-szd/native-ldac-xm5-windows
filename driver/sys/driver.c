// SPDX-License-Identifier: Apache-2.0
#include "device.h"

DRIVER_INITIALIZE DriverEntry;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#endif

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
                     _In_ PUNICODE_STRING RegistryPath) {
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;

    WDF_DRIVER_CONFIG_INIT(&config, LdacNativeEvtDeviceAdd);
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    return WdfDriverCreate(DriverObject,
                           RegistryPath,
                           &attributes,
                           &config,
                           WDF_NO_HANDLE);
}

