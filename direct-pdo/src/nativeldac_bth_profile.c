// SPDX-License-Identifier: Apache-2.0
#include <initguid.h>
#include "nativeldac_bth_profile.h"
#include <bthguid.h>

#define NLD_BTH_POOL_TAG 'PcdL'

static NTSTATUS NldBthQueryProfileInterface(
    _In_ PDEVICE_OBJECT target_device_object,
    _Out_ PBTH_PROFILE_DRIVER_INTERFACE profile_interface);

static BOOLEAN NldBthValidateProfileInterface(
    _In_ const BTH_PROFILE_DRIVER_INTERFACE* profile_interface);

static void NldBthDereferenceProfileInterface(
    _Inout_ PBTH_PROFILE_DRIVER_INTERFACE profile_interface);

static BOOLEAN NldBthValidateBluetoothAddress(_In_ BTH_ADDR address);

static NTSTATUS NldBthRetrieveLocalAddress(
    _In_ const NLD_BTH_PROFILE_CONTEXT* context,
    _Out_ BTH_ADDR* local_address);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, NldBthProfileStart)
#pragma alloc_text(PAGE, NldBthProfileStop)
#pragma alloc_text(PAGE, NldBthQueryProfileInterface)
#pragma alloc_text(PAGE, NldBthDereferenceProfileInterface)
#pragma alloc_text(PAGE, NldBthRetrieveLocalAddress)
#pragma alloc_text(PAGE, NldBthProfileSubmitBrb)
#endif

static NTSTATUS NldBthQueryProfileInterface(
    _In_ PDEVICE_OBJECT target_device_object,
    _Out_ PBTH_PROFILE_DRIVER_INTERFACE profile_interface) {
    KEVENT event;
    IO_STATUS_BLOCK io_status;
    PIRP irp;
    PIO_STACK_LOCATION stack;
    NTSTATUS status;

    PAGED_CODE();
    if (target_device_object == NULL || profile_interface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(profile_interface, sizeof(*profile_interface));
    KeInitializeEvent(&event, NotificationEvent, FALSE);
    io_status.Status = STATUS_NOT_SUPPORTED;
    io_status.Information = 0u;

    irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                       target_device_object,
                                       NULL,
                                       0u,
                                       NULL,
                                       &event,
                                       &io_status);
    if (irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    irp->RequestorMode = KernelMode;
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0u;
    stack = IoGetNextIrpStackLocation(irp);
    stack->MajorFunction = IRP_MJ_PNP;
    stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    stack->Parameters.QueryInterface.InterfaceType =
        &GUID_BTHDDI_PROFILE_DRIVER_INTERFACE;
    stack->Parameters.QueryInterface.Size =
        (USHORT)sizeof(*profile_interface);
    stack->Parameters.QueryInterface.Version =
        BTHDDI_PROFILE_DRIVER_INTERFACE_VERSION_FOR_QI;
    stack->Parameters.QueryInterface.Interface =
        (PINTERFACE)profile_interface;
    stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    status = IoCallDriver(target_device_object, irp);
    if (status == STATUS_PENDING) {
        status = KeWaitForSingleObject(&event,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
        if (NT_SUCCESS(status)) status = io_status.Status;
    }
    return status;
}

static BOOLEAN NldBthValidateProfileInterface(
    _In_ const BTH_PROFILE_DRIVER_INTERFACE* profile_interface) {
    if (profile_interface == NULL) return FALSE;
    return profile_interface->Interface.Size >=
               sizeof(BTH_PROFILE_DRIVER_INTERFACE) &&
           profile_interface->Interface.Version ==
               BTHDDI_PROFILE_DRIVER_INTERFACE_VERSION_FOR_QI &&
           profile_interface->Interface.InterfaceReference != NULL &&
           profile_interface->Interface.InterfaceDereference != NULL &&
           profile_interface->BthAllocateBrb != NULL &&
           profile_interface->BthFreeBrb != NULL &&
           profile_interface->BthInitializeBrb != NULL &&
           profile_interface->BthReuseBrb != NULL &&
           profile_interface->IsBluetoothVersionAvailable != NULL;
}

static void NldBthDereferenceProfileInterface(
    _Inout_ PBTH_PROFILE_DRIVER_INTERFACE profile_interface) {
    PINTERFACE_DEREFERENCE dereference;
    PVOID interface_context;

    PAGED_CODE();
    if (profile_interface == NULL) return;
    dereference = profile_interface->Interface.InterfaceDereference;
    interface_context = profile_interface->Interface.Context;
    RtlZeroMemory(profile_interface, sizeof(*profile_interface));
    if (dereference != NULL) dereference(interface_context);
}

static BOOLEAN NldBthValidateBluetoothAddress(_In_ BTH_ADDR address) {
    return address != 0ull &&
           (address & ~0x0000FFFFFFFFFFFFull) == 0ull;
}

static NTSTATUS NldBthRetrieveLocalAddress(
    _In_ const NLD_BTH_PROFILE_CONTEXT* context,
    _Out_ BTH_ADDR* local_address) {
    struct _BRB_GET_LOCAL_BD_ADDR* brb;
    NTSTATUS status;

    PAGED_CODE();
    if (context == NULL || local_address == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *local_address = 0ull;
    brb = (struct _BRB_GET_LOCAL_BD_ADDR*)
        context->Interface.BthAllocateBrb(BRB_HCI_GET_LOCAL_BD_ADDR,
                                          NLD_BTH_POOL_TAG);
    if (brb == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    status = NldBthProfileSubmitBrb(context,
                                    (PBRB)brb,
                                    sizeof(*brb),
                                    NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);
    if (NT_SUCCESS(status)) *local_address = brb->BtAddress;
    context->Interface.BthFreeBrb((PBRB)brb);
    return status;
}

void NldBthProfileInitialize(
    _Out_ PNLD_BTH_PROFILE_CONTEXT context) {
    if (context == NULL) return;
    RtlZeroMemory(context, sizeof(*context));
    NldBthOwnerInitialize(&context->Owner);
    NldBthAddressInitialize(&context->AddressDiscovery);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthProfileStart(
    _In_ PDEVICE_OBJECT target_device_object,
    _Inout_ PNLD_BTH_PROFILE_CONTEXT context) {
    BTH_PROFILE_DRIVER_INTERFACE queried_interface;
    NLD_BTH_OWNER_ACTION action;
    NLD_BTH_ADDRESS_ACTION address_action;
    BTH_DEVICE_INFO device_info;
    unsigned long generation;
    unsigned long address_generation;
    NTSTATUS status;
    BOOLEAN referenced;
    BOOLEAN valid;

    PAGED_CODE();
    if (target_device_object == NULL || context == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (NldBthProfileIsReady(context)) return STATUS_SUCCESS;

    action = NldBthOwnerOnPnpStart(&context->Owner);
    if (action != NldBthOwnerActionQuery) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    generation = context->Owner.Generation;
    RtlZeroMemory(&queried_interface, sizeof(queried_interface));
    status = NldBthQueryProfileInterface(target_device_object,
                                         &queried_interface);
    referenced = queried_interface.Interface.InterfaceDereference != NULL;
    valid = NT_SUCCESS(status) &&
            NldBthValidateProfileInterface(&queried_interface);
    action = NldBthOwnerOnQueryComplete(&context->Owner,
                                        generation,
                                        valid,
                                        referenced);
    if (action == NldBthOwnerActionDereference) {
        NldBthDereferenceProfileInterface(&queried_interface);
    }
    if (!valid || context->Owner.State != NldBthOwnerReady) {
        if (NT_SUCCESS(status)) status = STATUS_NOINTERFACE;
        NldBthProfileStop(context);
        return status;
    }

    context->Interface = queried_interface;
    context->TargetDeviceObject = target_device_object;
    ObReferenceObject(context->TargetDeviceObject);

    address_action = NldBthAddressOnPnpStart(
        &context->AddressDiscovery);
    if (address_action != NldBthAddressActionQueryRemote) {
        status = STATUS_INVALID_DEVICE_STATE;
        goto fail;
    }
    address_generation = context->AddressDiscovery.Generation;
    status = NldBthQueryRemoteDeviceInfoSynchronously(
        context->TargetDeviceObject,
        &device_info,
        NLD_BTH_DEFAULT_REQUEST_TIMEOUT_MS);
    if (NT_SUCCESS(status) &&
        !NldBthValidateBluetoothAddress(device_info.address)) {
        status = STATUS_DEVICE_DATA_ERROR;
    }
    address_action = NldBthAddressOnRemoteComplete(
        &context->AddressDiscovery,
        address_generation,
        NT_SUCCESS(status));
    if (!NT_SUCCESS(status) ||
        address_action != NldBthAddressActionQueryLocal) {
        if (NT_SUCCESS(status)) status = STATUS_INVALID_DEVICE_STATE;
        goto fail;
    }
    context->RemoteAddress = device_info.address;

    status = NldBthRetrieveLocalAddress(context,
                                        &context->LocalAddress);
    if (NT_SUCCESS(status) &&
        !NldBthValidateBluetoothAddress(context->LocalAddress)) {
        status = STATUS_DEVICE_DATA_ERROR;
    }
    (void)NldBthAddressOnLocalComplete(
        &context->AddressDiscovery,
        address_generation,
        NT_SUCCESS(status));
    if (!NT_SUCCESS(status) ||
        context->AddressDiscovery.State != NldBthAddressReady) {
        if (NT_SUCCESS(status)) status = STATUS_INVALID_DEVICE_STATE;
        goto fail;
    }
    return STATUS_SUCCESS;

fail:
    NldBthProfileStop(context);
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
void NldBthProfileStop(
    _Inout_ PNLD_BTH_PROFILE_CONTEXT context) {
    NLD_BTH_OWNER_ACTION action;
    PDEVICE_OBJECT target_device_object;

    PAGED_CODE();
    if (context == NULL) return;
    NldBthAddressOnPnpStop(&context->AddressDiscovery);
    context->RemoteAddress = 0ull;
    context->LocalAddress = 0ull;
    target_device_object = context->TargetDeviceObject;
    context->TargetDeviceObject = NULL;
    action = NldBthOwnerOnPnpStop(&context->Owner);
    if (action == NldBthOwnerActionDereference) {
        NldBthDereferenceProfileInterface(&context->Interface);
    } else if (!context->Owner.ReferenceHeld) {
        RtlZeroMemory(&context->Interface, sizeof(context->Interface));
    }
    if (target_device_object != NULL) {
        ObDereferenceObject(target_device_object);
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN NldBthProfileIsReady(
    _In_ const NLD_BTH_PROFILE_CONTEXT* context) {
    if (context == NULL) return FALSE;
    return context->Owner.State == NldBthOwnerReady &&
           context->Owner.ReferenceHeld &&
           context->AddressDiscovery.State == NldBthAddressReady &&
           context->TargetDeviceObject != NULL &&
           NldBthValidateBluetoothAddress(context->RemoteAddress) &&
           NldBthValidateBluetoothAddress(context->LocalAddress) &&
           NldBthValidateProfileInterface(&context->Interface);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS NldBthProfileGetAddresses(
    _In_ const NLD_BTH_PROFILE_CONTEXT* context,
    _Out_ PNLD_BTH_ADDRESS_INFO address_info) {
    if (context == NULL || address_info == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!NldBthProfileIsReady(context)) return STATUS_DEVICE_NOT_READY;
    address_info->RemoteAddress = context->RemoteAddress;
    address_info->LocalAddress = context->LocalAddress;
    address_info->Generation = context->AddressDiscovery.Generation;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS NldBthProfileSubmitBrb(
    _In_ const NLD_BTH_PROFILE_CONTEXT* context,
    _Inout_ PBRB brb,
    _In_ SIZE_T brb_size,
    _In_ ULONG timeout_ms) {
    PAGED_CODE();
    if (context == NULL || brb == NULL) return STATUS_INVALID_PARAMETER;
    if (context->Owner.State != NldBthOwnerReady ||
        !context->Owner.ReferenceHeld ||
        context->TargetDeviceObject == NULL ||
        !NldBthValidateProfileInterface(&context->Interface)) {
        return STATUS_DEVICE_NOT_READY;
    }
    return NldBthSubmitBrbSynchronously(context->TargetDeviceObject,
                                        brb,
                                        brb_size,
                                        timeout_ms);
}
