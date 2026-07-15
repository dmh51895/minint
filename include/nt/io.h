/*
 * MinNT - io.h
 * I/O Manager: DRIVER_OBJECT, DEVICE_OBJECT, IRP, IoCallDriver.
 * Minimal NT 6.x I/O model — enough to load a driver, create a device,
 * send an IRP, and get a result back.
 */

#ifndef _IO_H_
#define _IO_H_

#include <nt/ntdef.h>
#include <nt/ke.h>

/* ---- IRP major function codes (subset) --------------------------------- */

#define IRP_MJ_CREATE                   0x00
#define IRP_MJ_CLOSE                    0x02
#define IRP_MJ_DEVICE_CONTROL           0x0E
#define IRP_MJ_INTERNAL_DEVICE_CONTROL  0x0F
#define IRP_MJ_MAXIMUM_FUNCTION         0x1B

/* ---- Device types ------------------------------------------------------- */

#define FILE_DEVICE_UNKNOWN             0x00000022
#define FILE_DEVICE_NULL                0x00000015
#define FILE_DEVICE_BEEP                0x00000001

/* ---- IOCTL codes -------------------------------------------------------- */

#define METHOD_BUFFERED                 0
#define METHOD_IN_DIRECT                1
#define METHOD_OUT_DIRECT               2
#define METHOD_NEITHER                  3

#define CTL_CODE(DeviceType, Function, Method, Access) \
    (((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))

/* ---- Forward declarations ----------------------------------------------- */

struct _DEVICE_OBJECT;
struct _DRIVER_OBJECT;
struct _IRP;

/* ---- Dispatch function type --------------------------------------------- */

typedef NTSTATUS (NTAPI *PDRIVER_DISPATCH)(struct _DEVICE_OBJECT *, struct _IRP *);

/* ---- I/O status block --------------------------------------------------- */

typedef struct _IO_STATUS_BLOCK {
    NTSTATUS Status;
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

/* ---- I/O stack location (per-driver IRP parameters) --------------------- */

typedef struct _IO_STACK_LOCATION {
    UCHAR   MajorFunction;
    UCHAR   MinorFunction;
    UCHAR   Flags;
    UCHAR   Control;
    union {
        struct {
            ULONG  OutputBufferLength;
            ULONG  InputBufferLength;
            ULONG  IoControlCode;
            PVOID  Type3InputBuffer;
        } DeviceIoControl;
        struct {
            PVOID  Argument1;
            PVOID  Argument2;
        } Others;
    } Parameters;
    struct _DEVICE_OBJECT *DeviceObject;
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;

/* ---- IRP ---------------------------------------------------------------- */

typedef struct _IRP {
    USHORT          Type;               /* IO_TYPE_IRP                       */
    USHORT          Size;
    IO_STATUS_BLOCK IoStatus;
    PVOID           UserBuffer;         /* output buffer for buffered IOCTL  */
    BOOLEAN         Cancel;
    BOOLEAN         CancelIrql;         /* (packing shim)                    */
    UCHAR           StackCount;         /* total stack locations             */
    UCHAR           CurrentLocation;    /* 1-based, starts at StackCount     */
    LIST_ENTRY      ListEntry;
    /* Stack locations follow in memory (variable-length) */
} IRP, *PIRP;

/* ---- Driver object ------------------------------------------------------ */

typedef struct _DRIVER_OBJECT {
    USHORT          Type;               /* IO_TYPE_DRIVER                    */
    USHORT          Size;
    struct _DEVICE_OBJECT *DeviceObject; /* first device created             */
    UNICODE_STRING  DriverName;
    PDRIVER_DISPATCH MajorFunction[IRP_MJ_MAXIMUM_FUNCTION];
    LIST_ENTRY      DriverLink;         /* global driver list                */
    PVOID           Reserved;
} DRIVER_OBJECT, *PDRIVER_OBJECT;

/* ---- Device object ------------------------------------------------------ */

typedef struct _DEVICE_OBJECT {
    USHORT          Type;               /* IO_TYPE_DEVICE                    */
    USHORT          Size;
    ULONG           ReferenceCount;
    struct _DRIVER_OBJECT  *DriverObject;
    struct _DEVICE_OBJECT  *NextDevice; /* next device created by same driver*/
    struct _DEVICE_OBJECT  *AttachedDevice; /* lower device in the stack     */
    ULONG           Flags;
    ULONG           Characteristics;
    PVOID           DeviceExtension;    /* per-device driver data            */
    ULONG           DeviceType;
    WCHAR           DeviceName[64];     /* flat name for now                 */
} DEVICE_OBJECT, *PDEVICE_OBJECT;

/* ---- I/O Manager API ---------------------------------------------------- */

NTSTATUS NTAPI IoInitSystem(VOID);

NTSTATUS NTAPI IoCreateDevice(PDRIVER_OBJECT DriverObject,
                              ULONG DeviceExtensionSize,
                              PCWSTR DeviceName,
                              ULONG DeviceType,
                              PDEVICE_OBJECT *OutDeviceObject);

VOID NTAPI IoDeleteDevice(PDEVICE_OBJECT DeviceObject);

NTSTATUS NTAPI IoCreateDriver(PUNICODE_STRING DriverName,
                              PDRIVER_DISPATCH MajorFunction,
                              PDRIVER_OBJECT *OutDriverObject);

PIO_STACK_LOCATION NTAPI IoGetNextIrpStackLocation(PIRP Irp);
PIO_STACK_LOCATION NTAPI IoGetCurrentIrpStackLocation(PIRP Irp);

VOID NTAPI IoSetNextIrpStackLocation(PIRP Irp);
VOID NTAPI IoSkipCurrentIrpStackLocation(PIRP Irp);
VOID NTAPI IoCopyCurrentIrpStackLocationToNext(PIRP Irp);

NTSTATUS NTAPI IoCallDriver(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI IoCompleteRequest(PIRP Irp, NTSTATUS Status);

PIRP NTAPI IoAllocateIrp(UCHAR StackSize);
VOID  NTAPI IoFreeIrp(PIRP Irp);

/* ---- Helper for building IOCTL IRPs ------------------------------------- */

NTSTATUS NTAPI
IoBuildDeviceIoControlRequest(ULONG IoControlCode,
                              PDEVICE_OBJECT DeviceObject,
                              PVOID InputBuffer, ULONG InputBufferLength,
                              PVOID OutputBuffer, ULONG OutputBufferLength,
                              PIRP *OutIrp);

/* ---- Built-in null driver init ------------------------------------------ */

NTSTATUS NTAPI IoInitNullDriver(VOID);

#endif /* _IO_H_ */
