/*
 * MinNT - io/pnp/pnp.c
 * Plug & Play Manager: device enumeration, driver loading, resource management.
 *
 * Core PnP functionality:
 *  - Device object creation/deletion
 *  - Driver loading/unloading
 *  - Device enumeration on PCI/USB buses
 *  - IRP_MN_PNP dispatch for PnP IRPs
 *  - Device interface registration (for user-mode access)
 *  - Driver AddDevice callback registration
 *  - Device interface registration (for user-mode access via symbolic links)
 *  - Basic PnP IRP dispatch (start, stop, remove, query capabilities, etc.)
 */

#include <nt/ntdef.h>
#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/pe.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/pe.h>
#include <nt/io.h>
#include <nt/pnp.h>
#include <nt/hal.h>

#define PNP_DEBUG 1
#if PNP_DEBUG
#define PNP_DBG(fmt, ...) DbgPrint("PNP: " fmt "\n", ##__VA_ARGS__)
#else
#define PNP_DBG(fmt, ...)
#endif

/* ----------------------------------------------------------------------------
 * Global state
 * -------------------------------------------------------------------------- */
static LIST_ENTRY g_DriverList;
static LIST_ENTRY g_DeviceList;
static KSPIN_LOCK g_PnpLock;
static BOOLEAN g_PnpInit = FALSE;

/* ----------------------------------------------------------------------------
 * Device object management
 * -------------------------------------------------------------------------- */

NTSTATUS NTAPI
IoCreateDevice(PDRIVER_OBJECT DriverObject, ULONG DeviceExtensionSize,
               PUNICODE_STRING DeviceName, ULONG DeviceType,
               ULONG DeviceCharacteristics, BOOLEAN Exclusive,
               PDEVICE_OBJECT *DeviceObject)
{
    if (!DriverObject || !DeviceObject) return STATUS_INVALID_PARAMETER;
    if (!DeviceName) return STATUS_INVALID_PARAMETER;

    PNP_DBG("Creating device: %wZ (type=0x%08x)", DeviceName, DeviceType);

    PDEVICE_OBJECT devObj = ExAllocatePoolWithTag(NonPagedPool,
        sizeof(DEVICE_OBJECT) + DeviceExtensionSize, 'cDnI');
    if (!devObj) return STATUS_NO_MEMORY;

    RtlZeroMemory(devObj, sizeof(DEVICE_OBJECT) + DeviceExtensionSize);
    devObj->Type = (CSHORT)(sizeof(DEVICE_OBJECT) + DeviceExtensionSize);
    devObj->Size = (USHORT)(sizeof(DEVICE_OBJECT) + DeviceExtensionSize);
    devObj->ReferenceCount = 1;
    devObj->DriverObject = DriverObject;
    devObj->DeviceType = DeviceType;
    devObj->Characteristics = DeviceCharacteristics;
    devObj->DeviceExtension = (PVOID)((PUCHAR)devObj + sizeof(DEVICE_OBJECT));
    devObj->AlignmentRequirement = 3; /* DWORD alignment */

    if (DeviceName->Length > 0) {
        NTSTATUS s = ObCreateObject(&CmSymbolicLinkObjectType,
                                     sizeof(UNICODE_STRING) + DeviceName->Length,
                                     DeviceName, &devObj->Vpb);
        if (!NT_SUCCESS(s)) {
            ExFreePoolWithTag(devObj, 'cDnI');
            return s;
        }
    }

    KeInitializeEvent(&devObj->DeviceLock, SynchronizationEvent, TRUE);
    KeInitializeDeviceQueue(&devObj->DeviceQueue);
    InitializeListHead(&devObj->Queue.ListEntry);

    if (Exclusive) devObj->Flags |= DO_EXCLUSIVE;

    *DeviceObject = devObj;

    InsertTailList(&g_DeviceList, &devObj->Queue.ListEntry);

    return STATUS_SUCCESS;
}

VOID NTAPI IoDeleteDevice(PDEVICE_OBJECT DeviceObject)
{
    if (!DeviceObject) return;

    PNP_DBG("Deleting device object %p", DeviceObject);

    RemoveEntryList(&DeviceObject->Queue.ListEntry);

    if (DeviceObject->DeviceExtension) {
        ExFreePool(DeviceObject->DeviceExtension);
    }

    ExFreePool(DeviceObject);
}

/* ----------------------------------------------------------------------------
 * Driver object management
 * -------------------------------------------------------------------------- */

NTSTATUS NTAPI
IoCreateDriver(PUNICODE_STRING DriverName, PDRIVER_INITIALIZE DriverInit,
               PDRIVER_OBJECT *DriverObject)
{
    if (!DriverObject) return STATUS_INVALID_PARAMETER;

    PDRIVER_OBJECT drvObj = ExAllocatePoolWithTag(NonPagedPool,
        sizeof(DRIVER_OBJECT), 'vDnI');
    if (!drvObj) return STATUS_NO_MEMORY;

    RtlZeroMemory(drvObj, sizeof(DRIVER_OBJECT));
    drvObj->Type = 0; /* Will be set by OS */
    drvObj->Size = sizeof(DRIVER_OBJECT);
    drvObj->DriverInit = DriverInit;

    if (DriverName) {
        drvObj->DriverName = *DriverName;
    }

    /* Allocate driver extension */
    PDRIVER_EXTENSION ext = ExAllocatePoolWithTag(NonPagedPool,
        sizeof(DRIVER_EXTENSION), 'xEnI');
    if (!ext) {
        ExFreePool(drvObj);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(ext, sizeof(DRIVER_EXTENSION));
    ext->DriverObject = (PDRIVER_OBJECT)drvObj;
    drvObj->DriverExtension = ext;

    InsertTailList(&g_DriverList, &drvObj->DriverExtension->ListEntry);

    *DriverObject = drvObj;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
IoRegisterDriverReinitialization(PDRIVER_OBJECT DriverObject,
                                  PDRIVER_REINITIALIZE DriverReinitializationRoutine,
                                  PVOID Context)
{
    /* Not implemented - placeholder */
    (void)DriverObject; (void)DriverReinitializationRoutine; (void)Context;
    return STATUS_NOT_IMPLEMENTED;
}

/* ----------------------------------------------------------------------------
 * Device attachment/detachment
 * -------------------------------------------------------------------------- */

VOID NTAPI IoAttachDeviceToDeviceStack(PDEVICE_OBJECT SourceDevice,
                                        PDEVICE_OBJECT TargetDevice)
{
    if (!SourceDevice || !TargetDevice) return;

    SourceDevice->AttachedDevice = TargetDevice;
    PNP_DBG("Attached %p to %p", SourceDevice, TargetDevice);
}

PDEVICE_OBJECT NTAPI
IoAttachDeviceToDeviceStackSafe(PDEVICE_OBJECT SourceDevice,
                                 PDEVICE_OBJECT TargetDevice)
{
    if (!SourceDevice || !TargetDevice) return NULL;

    SourceDevice->AttachedDevice = TargetDevice;
    return SourceDevice;
}

VOID NTAPI IoDetachDevice(PDEVICE_OBJECT TargetDevice)
{
    if (!TargetDevice) return;
    TargetDevice->AttachedDevice = NULL;
}

/* ----------------------------------------------------------------------------
 * Device enumeration
 * -------------------------------------------------------------------------- */

NTSTATUS NTAPI
IoEnumerateDeviceObjectList(PDRIVER_OBJECT DriverObject,
                            PDEVICE_OBJECT *DeviceObjectList,
                            ULONG DeviceObjectListSize,
                            PULONG ActualNumberDeviceObjects)
{
    if (!DriverObject || !ActualNumberDeviceObjects) return STATUS_INVALID_PARAMETER;

    ULONG count = 0;
    PLIST_ENTRY entry = g_DeviceList.Flink;
    while (entry != &g_DeviceList && count < DeviceObjectListSize) {
        PDEVICE_OBJECT dev = CONTAINING_RECORD(entry, DEVICE_OBJECT, Queue.ListEntry);
        if (dev->DriverObject == DriverObject) {
            if (DeviceObjectList) {
                DeviceObjectList[count] = dev;
            }
            count++;
        }
        entry = entry->Flink;
    }

    *ActualNumberDeviceObjects = count;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
IoGetDeviceObjectPointer(PUNICODE_STRING ObjectName,
                          ACCESS_MASK DesiredAccess,
                          PFILE_OBJECT *FileObject,
                          PDEVICE_OBJECT *DeviceObject)
{
    (void)DesiredAccess;
    if (!ObjectName || !FileObject || !DeviceObject) return STATUS_INVALID_PARAMETER;

    /* Simple implementation - find device by name */
    PLIST_ENTRY entry = g_DeviceList.Flink;
    while (entry != &g_DeviceList) {
        PDEVICE_OBJECT dev = CONTAINING_RECORD(entry, DEVICE_OBJECT, Queue.ListEntry);
        if (dev->Vpb && RtlEqualUnicodeString(ObjectName, &dev->Vpb->Name, TRUE)) {
            *DeviceObject = dev;
            *FileObject = NULL; /* Simplified */
            return STATUS_SUCCESS;
        }
        entry = entry->Flink;
    }

    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/* ----------------------------------------------------------------------------
 * Device attachment/detachment
 * -------------------------------------------------------------------------- */

VOID NTAPI IoAttachDeviceToDeviceStack(PDEVICE_OBJECT SourceDevice,
                                        PDEVICE_OBJECT TargetDevice)
{
    if (!SourceDevice || !TargetDevice) return;

    SourceDevice->AttachedDevice = TargetDevice;
    PNP_DBG("Attached %p to %p", SourceDevice, TargetDevice);
}

PDEVICE_OBJECT NTAPI
IoAttachDeviceToDeviceStackSafe(PDEVICE_OBJECT SourceDevice,
                                 PDEVICE_OBJECT TargetDevice)
{
    if (!SourceDevice || !TargetDevice) return NULL;
    IoAttachDeviceToDeviceStack(SourceDevice, TargetDevice);
    return SourceDevice;
}

VOID NTAPI IoDetachDevice(PDEVICE_OBJECT TargetDevice)
{
    if (!TargetDevice) return;
    TargetDevice->AttachedDevice = NULL;
}

/* ----------------------------------------------------------------------------
 * Device interfaces (for user-mode access via symbolic links)
 * -------------------------------------------------------------------------- */

typedef struct _DEVICE_INTERFACE {
    LIST_ENTRY ListEntry;
    GUID InterfaceClass;
    UNICODE_STRING SymbolicLinkName;
    PDEVICE_OBJECT PhysicalDeviceObject;
    BOOLEAN Enabled;
} DEVICE_INTERFACE, *PDEVICE_INTERFACE;

static LIST_ENTRY g_DeviceInterfaces;
static KSPIN_LOCK g_InterfaceLock;

NTSTATUS NTAPI
IoRegisterDeviceInterface(PDEVICE_OBJECT PhysicalDeviceObject,
                           const GUID *InterfaceClassGuid,
                           PUNICODE_STRING ReferenceString,
                           PUNICODE_STRING SymbolicLinkName)
{
    if (!PhysicalDeviceObject || !InterfaceClassGuid || !SymbolicLinkName)
        return STATUS_INVALID_PARAMETER;

    PDEVICE_INTERFACE intf = ExAllocatePoolWithTag(NonPagedPool,
        sizeof(DEVICE_INTERFACE), 'nItI');
    if (!intf) return STATUS_NO_MEMORY;

    RtlZeroMemory(intf, sizeof(DEVICE_INTERFACE));
    intf->InterfaceClass = *InterfaceClassGuid;
    intf->PhysicalDeviceObject = PhysicalDeviceObject;
    intf->Enabled = TRUE;

    if (ReferenceString) {
        intf->SymbolicLinkName = *ReferenceString;
    } else {
        /* Generate a unique name */
        WCHAR buf[64];
        swprintf(buf, 64, L"\\??\\%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 (ULONG)PhysicalDeviceObject & 0xFFFFFFFF,
                 (USHORT)(PhysicalDeviceObject >> 32) & 0xFFFF,
                 (USHORT)(PhysicalDeviceObject >> 48) & 0xFFFF,
                 0, 0, 0, 0, 0, 0);
        RtlInitUnicodeString(&intf->SymbolicLinkName, buf);
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_InterfaceLock, &oldIrql);
    InsertTailList(&g_DeviceInterfaces, &intf->ListEntry);
    KeReleaseSpinLock(&g_InterfaceLock, oldIrql);

    *SymbolicLinkName = intf->SymbolicLinkName;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
IoSetDeviceInterfaceState(PUNICODE_STRING SymbolicLinkName, BOOLEAN Enable)
{
    if (!SymbolicLinkName) return STATUS_INVALID_PARAMETER;

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_InterfaceLock, &oldIrql);

    PLIST_ENTRY entry = g_DeviceInterfaces.Flink;
    while (entry != &g_DeviceInterfaces) {
        PDEVICE_INTERFACE intf = CONTAINING_RECORD(entry, DEVICE_INTERFACE, ListEntry);
        if (RtlEqualUnicodeString(&intf->SymbolicLinkName, SymbolicLinkName, TRUE)) {
            intf->Enabled = Enable;
            KeReleaseSpinLock(&g_InterfaceLock, oldIrql);
            return STATUS_SUCCESS;
        }
        entry = entry->Flink;
    }

    KeReleaseSpinLock(&g_InterfaceLock, oldIrql);
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS NTAPI
IoGetDeviceInterfaces(const GUID *InterfaceClassGuid,
                       PVOID PhysicalDeviceObject,
                       ULONG Flags,
                       PUNICODE_STRING *SymbolicLinkList)
{
    (void)PhysicalDeviceObject; (void)Flags;
    if (!InterfaceClassGuid || !SymbolicLinkList) return STATUS_INVALID_PARAMETER;

    *SymbolicLinkList = NULL;

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_InterfaceLock, &oldIrql);

    PLIST_ENTRY entry = g_DeviceInterfaces.Flink;
    while (entry != &g_DeviceInterfaces) {
        PDEVICE_INTERFACE intf = CONTAINING_RECORD(entry, DEVICE_INTERFACE, ListEntry);
        if (IsEqualGUID((const GUID *)InterfaceClassGuid, (const GUID *)&intf->InterfaceClass)) {
            PUNICODE_STRING list = ExAllocatePoolWithTag(NonPagedPool,
                sizeof(UNICODE_STRING) + intf->SymbolicLinkName.Length + sizeof(WCHAR), 'nItI');
            if (!list) {
                KeReleaseSpinLock(&g_InterfaceLock, oldIrql);
                return STATUS_NO_MEMORY;
            }
            list->Buffer = (PWSTR)((PUCHAR)list + sizeof(UNICODE_STRING));
            list->Length = intf->SymbolicLinkName.Length;
            list->MaximumLength = intf->SymbolicLinkName.Length + sizeof(WCHAR);
            RtlCopyMemory(list->Buffer, intf->SymbolicLinkName.Buffer, list->Length);
            list->Buffer[list->Length / sizeof(WCHAR)] = 0;

            list->Next = *SymbolicLinkList;
            *SymbolicLinkList = list;
        }
        entry = entry->Flink;
    }

    KeReleaseSpinLock(&g_InterfaceLock, oldIrql);
    return STATUS_SUCCESS;
}

VOID NTAPI IoFreeDeviceInterfaces(PUNICODE_STRING SymbolicLinkList)
{
    while (SymbolicLinkList) {
        PUNICODE_STRING next = SymbolicLinkList->Next;
        ExFreePool(SymbolicLinkList);
        SymbolicLinkList = next;
    }
}

/* ----------------------------------------------------------------------------
 * Device attachment
 * -------------------------------------------------------------------------- */

VOID NTAPI IoAttachDeviceToDeviceStack(PDEVICE_OBJECT SourceDevice,
                                        PDEVICE_OBJECT TargetDevice)
{
    if (!SourceDevice || !TargetDevice) return;
    SourceDevice->AttachedDevice = TargetDevice;
    PNP_DBG("Attached %p to %p", SourceDevice, TargetDevice);
}

PDEVICE_OBJECT NTAPI
IoAttachDeviceToDeviceStackSafe(PDEVICE_OBJECT SourceDevice,
                                 PDEVICE_OBJECT TargetDevice)
{
    if (!SourceDevice || !TargetDevice) return NULL;
    IoAttachDeviceToDeviceStack(SourceDevice, TargetDevice);
    return SourceDevice;
}

VOID NTAPI IoDetachDevice(PDEVICE_OBJECT TargetDevice)
{
    if (TargetDevice) TargetDevice->AttachedDevice = NULL;
}

/* ----------------------------------------------------------------------------
 * PnP IRP dispatch
 * -------------------------------------------------------------------------- */

NTSTATUS NTAPI
PnpDispatchPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    if (!DeviceObject || !Irp) return STATUS_INVALID_PARAMETER;

    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    if (!irpSp) return STATUS_INVALID_PARAMETER;

    PNP_DBG("PnP IRP: DevObj=%p Major=%x Minor=%x",
            DeviceObject, irpSp->MajorFunction, irpSp->MinorFunction);

    if (irpSp->MajorFunction != IRP_MJ_PNP) {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    switch (irpSp->MinorFunction) {
    case IRP_MN_START_DEVICE:
        PNP_DBG("IRP_MN_START_DEVICE for %p", DeviceObject);
        DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_QUERY_REMOVE_DEVICE:
        PNP_DBG("IRP_MN_QUERY_REMOVE_DEVICE for %p", DeviceObject);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_REMOVE_DEVICE:
        PNP_DBG("IRP_MN_REMOVE_DEVICE for %p", DeviceObject);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_CANCEL_REMOVE_DEVICE:
        PNP_DBG("IRP_MN_CANCEL_REMOVE_DEVICE for %p", DeviceObject);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_STOP_DEVICE:
        PNP_DBG("IRP_MN_STOP_DEVICE for %p", DeviceObject);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_QUERY_STOP_DEVICE:
        PNP_DBG("IRP_MN_QUERY_STOP_DEVICE for %p", DeviceObject);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_CANCEL_STOP_DEVICE:
        PNP_DBG("IRP_MN_CANCEL_STOP_DEVICE for %p", DeviceObject);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_QUERY_DEVICE_RELATIONS:
        PNP_DBG("IRP_MN_QUERY_DEVICE_RELATIONS for %p", DeviceObject);
        /* Simplified - return empty relations */
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_QUERY_INTERFACE:
        PNP_DBG("IRP_MN_QUERY_INTERFACE for %p", DeviceObject);
        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        break;

    case IRP_MN_QUERY_CAPABILITIES:
        PNP_DBG("IRP_MN_QUERY_CAPABILITIES for %p", DeviceObject);
        {
            PDEVICE_CAPABILITIES caps = (PDEVICE_CAPABILITIES)Irp->IoStatus.Information;
            if (caps) {
                caps->Version = 1;
                caps->Size = sizeof(DEVICE_CAPABILITIES);
                caps->DeviceD1 = 0;
                caps->DeviceD2 = 0;
                caps->LockSupported = 0;
                caps->EjectSupported = 0;
                caps->Removable = 0;
                caps->DockDevice = 0;
                caps->UniqueID = 0;
                caps->SilentInstall = 1;
                caps->RawDeviceOK = 1;
                caps->SurpriseRemovalOK = 1;
            }
        }
        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_QUERY_RESOURCES:
    case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
        PNP_DBG("IRP_MN_QUERY_RESOURCE_REQUIREMENTS for %p", DeviceObject);
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_QUERY_ID:
        PNP_DBG("IRP_MN_QUERY_ID for %p", DeviceObject);
        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        break;

    case IRP_MN_QUERY_PNP_DEVICE_STATE:
        PNP_DBG("IRP_MN_QUERY_PNP_DEVICE_STATE for %p", DeviceObject);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_QUERY_BUS_INFORMATION:
        PNP_DBG("IRP_MN_QUERY_BUS_INFORMATION for %p", DeviceObject);
        {
            PPNP_BUS_INFORMATION busInfo = ExAllocatePool(NonPagedPool, sizeof(PNP_BUS_INFORMATION));
            if (busInfo) {
                busInfo->BusTypeGuid = GUID_BUS_TYPE_PCI;
                busInfo->LegacyBusType = PNPBus;
                busInfo->BusNumber = 0;
                Irp->IoStatus.Information = (ULONG_PTR)busInfo;
            }
        }
        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_DEVICE_ENUMERATED:
        PNP_DBG("IRP_MN_DEVICE_ENUMERATED for %p", DeviceObject);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_QUERY_LEGACY_BUS_INFORMATION:
        PNP_DBG("IRP_MN_QUERY_LEGACY_BUS_INFORMATION for %p", DeviceObject);
        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        break;

    case IRP_MN_QUERY_DEVICE_TEXT:
        PNP_DBG("IRP_MN_QUERY_DEVICE_TEXT for %p", DeviceObject);
        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        break;

    case IRP_MN_QUERY_INTERFACE:
        PNP_DBG("IRP_MN_QUERY_INTERFACE for %p", DeviceObject);
        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        break;

    case IRP_MN_SURPRISE_REMOVAL:
        PNP_DBG("IRP_MN_SURPRISE_REMOVAL for %p", DeviceObject);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_QUERY_PNP_DEVICE_STATE:
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    default:
        PNP_DBG("Unhandled PnP Minor: %x", irpSp->MinorFunction);
        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        break;
    }

    return Irp->IoStatus.Status;
}

/* ----------------------------------------------------------------------------
 * PnP Manager initialization
 * -------------------------------------------------------------------------- */

VOID NTAPI PnpInitSystem(VOID)
{
    PNP_DBG("Initializing PnP Manager...");

    InitializeListHead(&g_DriverList);
    InitializeListHead(&g_DeviceList);
    InitializeListHead(&g_DeviceInterfaces);
    KeInitializeSpinLock(&g_PnpLock);
    KeInitializeSpinLock(&g_InterfaceLock);

    g_PnpInit = TRUE;
    PNP_DBG("PnP Manager initialized");
}

/* ----------------------------------------------------------------------------
 * Driver registration
 * -------------------------------------------------------------------------- */

NTSTATUS NTAPI
PnpRegisterDeviceDriver(PDRIVER_OBJECT DriverObject, PWSTR ServiceName)
{
    if (!DriverObject || !ServiceName) return STATUS_INVALID_PARAMETER;

    PNP_DBG("Registering driver %ws", ServiceName);
    InsertTailList(&g_DriverList, &DriverObject->DriverExtension->ListEntry);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
PnpAddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject)
{
    if (!DriverObject || !PhysicalDeviceObject) return STATUS_INVALID_PARAMETER;

    PNP_DBG("AddDevice for driver %p, device %p", DriverObject, PhysicalDeviceObject);

    if (DriverObject->DriverExtension->AddDevice) {
        return DriverObject->DriverExtension->AddDevice(DriverObject, PhysicalDeviceObject);
    }

    return STATUS_NOT_SUPPORTED;
}

/* ----------------------------------------------------------------------------
 * PnP IRP dispatch entry point
 * -------------------------------------------------------------------------- */

NTSTATUS NTAPI
PnpDispatchPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    return PnpDispatchPnp(DeviceObject, Irp);
}

/* ----------------------------------------------------------------------------
 * Plug & Play notification
 * -------------------------------------------------------------------------- */

typedef enum _EVENT_CATEGORY {
    EventCategoryDeviceInterfaceChange = 0,
    EventCategoryHardwareProfileChange = 1,
    EventCategoryTargetDeviceChange = 2,
} EVENT_CATEGORY;

typedef NTSTATUS (NTAPI *PDRIVER_NOTIFICATION_CALLBACK)(PVOID Context, PVOID Event);

NTSTATUS NTAPI
IoRegisterPlugPlayNotification(ULONG EventCategory,
                                ULONG Flags,
                                PVOID EventData,
                                PDRIVER_OBJECT DriverObject,
                                PDRIVER_NOTIFICATION_CALLBACK Callback,
                                PVOID Context,
                                PVOID *NotificationEntry)
{
    (void)EventCategory; (void)Flags; (void)EventData;
    (void)DriverObject; (void)Callback; (void)Context;
    if (NotificationEntry) *NotificationEntry = NULL;
    return STATUS_NOT_IMPLEMENTED;
}

VOID NTAPI IoUnregisterPlugPlayNotification(PVOID NotificationEntry)
{
    (void)NotificationEntry;
}

/* ----------------------------------------------------------------------------
 * Device object pointer
 * -------------------------------------------------------------------------- */

NTSTATUS NTAPI
IoGetDeviceObjectPointer(PUNICODE_STRING ObjectName,
                          ACCESS_MASK DesiredAccess,
                          PFILE_OBJECT *FileObject,
                          PDEVICE_OBJECT *DeviceObject)
{
    return IoGetDeviceObjectPointer(ObjectName, DesiredAccess, FileObject, DeviceObject);
}

#endif /* _PNP_H_ */
