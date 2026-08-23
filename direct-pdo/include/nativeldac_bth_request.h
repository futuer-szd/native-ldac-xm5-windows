// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_BTH_REQUEST_H
#define NATIVE_LDAC_BTH_REQUEST_H

#include <ntddk.h>
#include <bthdef.h>
#include <bthddi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS 10000u
#define NLD_BTH_MAX_REQUEST_TIMEOUT_MS 60000u

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthQueryRemoteDeviceInfoSynchronously(
    _In_ PDEVICE_OBJECT target_device_object,
    _Out_ PBTH_DEVICE_INFO device_info,
    _In_ ULONG timeout_ms);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthSubmitBrbSynchronously(
    _In_ PDEVICE_OBJECT target_device_object,
    _Inout_ PBRB brb,
    _In_ SIZE_T brb_size,
    _In_ ULONG timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
