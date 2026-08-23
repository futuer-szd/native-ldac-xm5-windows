// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_BTH_PROFILE_H
#define NATIVE_LDAC_BTH_PROFILE_H

#include <ntddk.h>
#include <bthdef.h>
#include <bthddi.h>

#include "nativeldac_bth_address_contract.h"
#include "nativeldac_bth_owner_contract.h"
#include "nativeldac_bth_request.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _NLD_BTH_PROFILE_CONTEXT {
    NLD_BTH_INTERFACE_OWNER Owner;
    NLD_BTH_ADDRESS_DISCOVERY AddressDiscovery;
    BTH_PROFILE_DRIVER_INTERFACE Interface;
    PDEVICE_OBJECT TargetDeviceObject;
    BTH_ADDR RemoteAddress;
    BTH_ADDR LocalAddress;
} NLD_BTH_PROFILE_CONTEXT, *PNLD_BTH_PROFILE_CONTEXT;

typedef struct _NLD_BTH_ADDRESS_INFO {
    BTH_ADDR RemoteAddress;
    BTH_ADDR LocalAddress;
    ULONG Generation;
} NLD_BTH_ADDRESS_INFO, *PNLD_BTH_ADDRESS_INFO;

void NldBthProfileInitialize(
    _Out_ PNLD_BTH_PROFILE_CONTEXT context);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthProfileStart(
    _In_ PDEVICE_OBJECT target_device_object,
    _Inout_ PNLD_BTH_PROFILE_CONTEXT context);

_IRQL_requires_max_(PASSIVE_LEVEL)
void NldBthProfileStop(
    _Inout_ PNLD_BTH_PROFILE_CONTEXT context);

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN NldBthProfileIsReady(
    _In_ const NLD_BTH_PROFILE_CONTEXT* context);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS NldBthProfileGetAddresses(
    _In_ const NLD_BTH_PROFILE_CONTEXT* context,
    _Out_ PNLD_BTH_ADDRESS_INFO address_info);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthProfileSubmitBrb(
    _In_ const NLD_BTH_PROFILE_CONTEXT* context,
    _Inout_ PBRB brb,
    _In_ SIZE_T brb_size,
    _In_ ULONG timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
