/*
 * MinNT - io/iomgr.c
 * I/O Manager implementation. Provides DRIVER_OBJECT, DEVICE_OBJECT, IRP,
 * IoCallDriver, and IoCompleteRequest. Minimal but functional — enough to
 * load a driver, create a device, and dispatch IRPs.
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/io.h>
#include <nt/rtl.h>
#include <nt/hal.h>

/* ---- Internal state ----------------------------------------------------- */

#define IO_TYPE_DRIVER  0x4452  /* 'DR' */
#define IO_TYPE_DEVICE  0x4456  /* 'DV' */
#define IO_TYPE_IRP     0x4952  /* 'IR' */

#define TAG_IOMGR  0x4D4F49  /* 'IOM' */
#define TAG_IRP    0x505249  /* 'IRP' */
#define TAG_DEV    0x564544  /* 'DEV' */

static LIST_ENTRY IopDriverList;
static KSPIN_LOCK IopSpinLock;

/* ---- IRP allocation ----------------------------------------------------- */

PIRP NTAPI IoAllocateIrp(UCHAR StackSize)
{
    PIRP Irp;
    SIZE_T IrpSize;
    PIO_STACK_LOCATION Stack;
    UCHAR i;

    if (StackSize < 1) StackSize = 1;

    IrpSize = sizeof(IRP) + StackSize * sizeof(IO_STACK_LOCATION);
    Irp = ExAllocatePoolWithTag(NonPagedPool, IrpSize, TAG_IRP);
    if (!Irp)
        return NULL;

    RtlZeroMemory(Irp, IrpSize);
    Irp->Type = IO_TYPE_IRP;
    Irp->Size = (USHORT)IrpSize;
    Irp->StackCount = StackSize;
    Irp->CurrentLocation = StackSize;   /* 1-based, starts at top */

    /* Initialize stack locations with invalid major function */
    Stack = (PIO_STACK_LOCATION)(Irp + 1);
    for (i = 0; i < StackSize; i++)
        Stack[i].MajorFunction = 0xFF;

    return Irp;
}

VOID NTAPI IoFreeIrp(PIRP Irp)
{
    if (Irp)
        ExFreePoolWithTag(Irp, TAG_IRP);
}

/* ---- Stack location helpers --------------------------------------------- */

PIO_STACK_LOCATION NTAPI IoGetNextIrpStackLocation(PIRP Irp)
{
    PIO_STACK_LOCATION Stack = (PIO_STACK_LOCATION)(Irp + 1);
    return &Stack[Irp->CurrentLocation - 1];
}

PIO_STACK_LOCATION NTAPI IoGetCurrentIrpStackLocation(PIRP Irp)
{
    PIO_STACK_LOCATION Stack = (PIO_STACK_LOCATION)(Irp + 1);
    return &Stack[Irp->CurrentLocation - 1];
}

VOID NTAPI IoSetNextIrpStackLocation(PIRP Irp)
{
    UNREFERENCED_PARAMETER(Irp);
}

VOID NTAPI IoSkipCurrentIrpStackLocation(PIRP Irp)
{
    if (Irp->CurrentLocation > 1)
        Irp->CurrentLocation--;
}

VOID NTAPI IoCopyCurrentIrpStackLocationToNext(PIRP Irp)
{
    PIO_STACK_LOCATION Current = IoGetCurrentIrpStackLocation(Irp);
    if (Irp->CurrentLocation > 1)
    {
        PIO_STACK_LOCATION Next = IoGetNextIrpStackLocation(Irp);
        RtlCopyMemory(Next, Current, sizeof(IO_STACK_LOCATION));
    }
}

/* ---- Driver creation ---------------------------------------------------- */

NTSTATUS NTAPI IoCreateDriver(PUNICODE_STRING DriverName,
                              PDRIVER_DISPATCH MajorFunction,
                              PDRIVER_OBJECT *OutDriverObject)
{
    PDRIVER_OBJECT Driver;
    ULONG i;

    Driver = ExAllocatePoolWithTag(NonPagedPool,
                                   sizeof(DRIVER_OBJECT), TAG_IOMGR);
    if (!Driver)
        return STATUS_NO_MEMORY;

    RtlZeroMemory(Driver, sizeof(DRIVER_OBJECT));
    Driver->Type = IO_TYPE_DRIVER;
    Driver->Size = sizeof(DRIVER_OBJECT);

    if (DriverName)
    {
        Driver->DriverName.Length = DriverName->Length;
        Driver->DriverName.MaximumLength = DriverName->MaximumLength;
        Driver->DriverName.Buffer = DriverName->Buffer;
    }

    /* Set all dispatch routines to the provided handler */
    if (MajorFunction)
    {
        for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
            Driver->MajorFunction[i] = MajorFunction;
    }

    /* Link into global driver list */
    InsertTailList(&IopDriverList, &Driver->DriverLink);

    *OutDriverObject = Driver;
    return STATUS_SUCCESS;
}

/* ---- Device creation ---------------------------------------------------- */

NTSTATUS NTAPI IoCreateDevice(PDRIVER_OBJECT DriverObject,
                              ULONG DeviceExtensionSize,
                              PCWSTR DeviceName,
                              ULONG DeviceType,
                              PDEVICE_OBJECT *OutDeviceObject)
{
    PDEVICE_OBJECT Device;
    SIZE_T TotalSize;
    ULONG NameLen;

    TotalSize = sizeof(DEVICE_OBJECT) + DeviceExtensionSize;
    Device = ExAllocatePoolWithTag(NonPagedPool, TotalSize, TAG_DEV);
    if (!Device)
        return STATUS_NO_MEMORY;

    RtlZeroMemory(Device, TotalSize);
    Device->Type = IO_TYPE_DEVICE;
    Device->Size = sizeof(DEVICE_OBJECT);
    Device->ReferenceCount = 1;
    Device->DriverObject = DriverObject;
    Device->DeviceType = DeviceType;
    Device->DeviceExtension = (PVOID)((PUCHAR)Device + sizeof(DEVICE_OBJECT));

    if (DeviceName)
    {
        for (NameLen = 0; NameLen < 63 && DeviceName[NameLen]; NameLen++)
            Device->DeviceName[NameLen] = DeviceName[NameLen];
        Device->DeviceName[NameLen] = L'\0';
    }

    /* Link into driver's device list */
    Device->NextDevice = DriverObject->DeviceObject;
    DriverObject->DeviceObject = Device;

    *OutDeviceObject = Device;
    return STATUS_SUCCESS;
}

VOID NTAPI IoDeleteDevice(PDEVICE_OBJECT DeviceObject)
{
    if (DeviceObject)
    {
        /* Unlink from driver */
        if (DeviceObject->DriverObject)
        {
            if (DeviceObject->DriverObject->DeviceObject == DeviceObject)
                DeviceObject->DriverObject->DeviceObject = DeviceObject->NextDevice;
        }
        ExFreePoolWithTag(DeviceObject, TAG_DEV);
    }
}

/* ---- IRP dispatch ------------------------------------------------------- */

NTSTATUS NTAPI IoCallDriver(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PDRIVER_OBJECT DriverObject;

    if (!DeviceObject || !Irp)
        return STATUS_INVALID_PARAMETER;

    DriverObject = DeviceObject->DriverObject;
    if (!DriverObject)
        return STATUS_UNSUCCESSFUL;

    /* Set the current stack location's device object */
    Stack = IoGetCurrentIrpStackLocation(Irp);
    Stack->DeviceObject = DeviceObject;

    /* Call the driver's dispatch routine */
    if (Stack->MajorFunction < IRP_MJ_MAXIMUM_FUNCTION &&
        DriverObject->MajorFunction[Stack->MajorFunction])
    {
        return DriverObject->MajorFunction[Stack->MajorFunction](DeviceObject, Irp);
    }

    return STATUS_UNSUCCESSFUL;
}

NTSTATUS NTAPI IoCompleteRequest(PIRP Irp, NTSTATUS Status)
{
    if (!Irp)
        return STATUS_INVALID_PARAMETER;

    Irp->IoStatus.Status = Status;
    return Status;
}

/* ---- Build IOCTL IRP ---------------------------------------------------- */

NTSTATUS NTAPI
IoBuildDeviceIoControlRequest(ULONG IoControlCode,
                              PDEVICE_OBJECT DeviceObject,
                              PVOID InputBuffer, ULONG InputBufferLength,
                              PVOID OutputBuffer, ULONG OutputBufferLength,
                              PIRP *OutIrp)
{
    PIRP Irp;
    PIO_STACK_LOCATION Stack;

    UNREFERENCED_PARAMETER(DeviceObject);

    Irp = IoAllocateIrp(1);
    if (!Irp)
        return STATUS_NO_MEMORY;

    Irp->UserBuffer = OutputBuffer;

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    Stack->Parameters.DeviceIoControl.IoControlCode = IoControlCode;
    Stack->Parameters.DeviceIoControl.InputBufferLength = InputBufferLength;
    Stack->Parameters.DeviceIoControl.OutputBufferLength = OutputBufferLength;
    Stack->Parameters.DeviceIoControl.Type3InputBuffer = InputBuffer;

    *OutIrp = Irp;
    return STATUS_SUCCESS;
}

/* ---- I/O Manager initialization ----------------------------------------- */

NTSTATUS NTAPI IoInitSystem(VOID)
{
    InitializeListHead(&IopDriverList);
    KeInitializeSpinLock(&IopSpinLock);

    DbgPrint("IO: I/O manager online\n");
    return STATUS_SUCCESS;
}

/* ---- Built-in null device driver ---------------------------------------- */

static NTSTATUS NTAPI IopNullDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION Stack;

    UNREFERENCED_PARAMETER(DeviceObject);
    Stack = IoGetCurrentIrpStackLocation(Irp);

    switch (Stack->MajorFunction)
    {
        case IRP_MJ_CREATE:
            DbgPrint("IO: null\\null: IRP_MJ_CREATE\n");
            break;
        case IRP_MJ_CLOSE:
            DbgPrint("IO: null\\null: IRP_MJ_CLOSE\n");
            break;
        case IRP_MJ_DEVICE_CONTROL:
        {
            ULONG Code = Stack->Parameters.DeviceIoControl.IoControlCode;
            DbgPrint("IO: null\\null: IOCTL 0x%08lx\n", Code);
            break;
        }
        default:
            break;
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI IoInitNullDriver(VOID)
{
    PDRIVER_OBJECT NullDriver = NULL;
    PDEVICE_OBJECT NullDevice = NULL;
    UNICODE_STRING DriverName = RTL_CONSTANT_STRING(L"\\Driver\\Null");
    NTSTATUS Status;

    Status = IoCreateDriver(&DriverName, IopNullDispatch, &NullDriver);
    if (!NT_SUCCESS(Status))
        return Status;

    static const WCHAR NullDeviceName[] = { '\\','D','e','v','i','c','e','\\','N','u','l','l',0 };
    Status = IoCreateDevice(NullDriver, 0, NullDeviceName,
                            FILE_DEVICE_NULL, &NullDevice);
    if (!NT_SUCCESS(Status))
        return Status;

    DbgPrint("IO: \\Device\\Null ready\n");
    return STATUS_SUCCESS;
}
