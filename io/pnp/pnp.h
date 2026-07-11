/*
 * MinNT - io/pnp/pnp.h
 * Plug & Play Manager: device enumeration, driver loading, resource management.
 */

#ifndef _PNP_H_
#define _PNP_H_

#include <nt/ntdef.h>
#include <nt/ob.h>

/* ----------------------------------------------------------------------------
 * Device object flags
 * -------------------------------------------------------------------------- */
#define DO_DEVICE_INITIALIZING      0x00000001
#define DO_BUFFERED_IO              0x00000004
#define DO_DIRECT_IO                0x00000008
#define DO_EXCLUSIVE                0x00000010
#define DO_DEVICE_HAS_NAME          0x00000020
#define DO_DEVICE_TO_BE_DELETED     0x00000040

/* ----------------------------------------------------------------------------
 * Device types
 * -------------------------------------------------------------------------- */
typedef enum _DEVICE_TYPE {
    FILE_DEVICE_BEEP                = 0x00000001,
    FILE_DEVICE_CD_ROM              = 0x00000002,
    FILE_DEVICE_CD_ROM_FILE_SYSTEM  = 0x00000003,
    FILE_DEVICE_CONTROLLER          = 0x00000004,
    FILE_DEVICE_DATALINK            = 0x00000005,
    FILE_DEVICE_DFS                 = 0x00000006,
    FILE_DEVICE_DISK                = 0x00000007,
    FILE_DEVICE_DISK_FILE_SYSTEM    = 0x00000008,
    FILE_DEVICE_FILE_SYSTEM         = 0x00000009,
    FILE_DEVICE_INPORT_PORT         = 0x0000000a,
    FILE_DEVICE_KEYBOARD            = 0x0000000b,
    FILE_DEVICE_MAILSLOT            = 0x0000000b,
    FILE_DEVICE_MIDI_IN             = 0x0000000c,
    FILE_DEVICE_MIDI_OUT            = 0x0000000d,
    FILE_DEVICE_MOUSE               = 0x0000000d,
    FILE_DEVICE_MULTI_UNC_PROVIDER  = 0x0000000e,
    FILE_DEVICE_NAMED_PIPE          = 0x0000000f,
    FILE_DEVICE_NETWORK             = 0x00000010,
    FILE_DEVICE_NETWORK_BROWSER     = 0x00000011,
    FILE_DEVICE_NETWORK_FILE_SYSTEM = 0x00000012,
    FILE_DEVICE_NULL                = 0x00000013,
    FILE_DEVICE_PARALLEL_PORT       = 0x00000014,
    FILE_DEVICE_PHYSICAL_NETCARD    = 0x00000015,
    FILE_DEVICE_PRINTER             = 0x00000016,
    FILE_DEVICE_SCANNER             = 0x00000017,
    FILE_DEVICE_SERIAL_MOUSE_PORT   = 0x00000018,
    FILE_DEVICE_SERIAL_PORT         = 0x00000019,
    FILE_DEVICE_SCREEN              = 0x0000001a,
    FILE_DEVICE_SOUND               = 0x0000001b,
    FILE_DEVICE_STREAMS             = 0x0000001c,
    FILE_DEVICE_TAPE                = 0x0000001d,
    FILE_DEVICE_TAPE_FILE_SYSTEM    = 0x0000001e,
    FILE_DEVICE_TRANSPORT           = 0x0000001f,
    FILE_DEVICE_UNKNOWN             = 0x00000020,
    FILE_DEVICE_VIDEO               = 0x00000021,
    FILE_DEVICE_VIRTUAL_DISK        = 0x00000022,
    FILE_DEVICE_WAVE_IN             = 0x00000023,
    FILE_DEVICE_WAVE_OUT            = 0x00000024,
    FILE_DEVICE_8042_PORT           = 0x00000025,
    FILE_DEVICE_NETWORK_REDIRECTOR  = 0x00000026,
    FILE_DEVICE_BATTERY             = 0x00000026,
    FILE_DEVICE_BUS_EXTENDER        = 0x00000027,
    FILE_DEVICE_MODEM               = 0x00000027,
    FILE_DEVICE_VDM                 = 0x00000027,
    FILE_DEVICE_MASS_STORAGE        = 0x00000027,
    FILE_DEVICE_SMB                 = 0x00000028,
    FILE_DEVICE_KS                  = 0x00000028,
    FILE_DEVICE_CHANGER             = 0x00000029,
    FILE_DEVICE_SMARTCARD           = 0x00000029,
    FILE_DEVICE_ACPI                = 0x0000002a,
    FILE_DEVICE_DVD                 = 0x0000002a,
    FILE_DEVICE_FULLSCREEN_VIDEO    = 0x0000002b,
    FILE_DEVICE_DFS_FILE_SYSTEM     = 0x0000002b,
    FILE_DEVICE_DFS_VOLUME          = 0x0000002c,
    FILE_DEVICE_SERENUM             = 0x0000002d,
    FILE_DEVICE_TERMSRV             = 0x0000002d,
    FILE_DEVICE_KSEC                = 0x0000002e,
    FILE_DEVICE_FIPS                = 0x0000002f,
    FILE_DEVICE_INFINIBAND          = 0x0000002f,
    FILE_DEVICE_VMBUS               = 0x00000030,
    FILE_DEVICE_NETWORK_SENSOR      = 0x00000030,
    FILE_DEVICE_POINT_OF_SERVICE    = 0x00000031,
} DEVICE_TYPE;

/* ----------------------------------------------------------------------------
 * IRP Major Functions
 * -------------------------------------------------------------------------- */
#define IRP_MJ_CREATE                   0x00
#define IRP_MJ_CREATE_NAMED_PIPE        0x01
#define IRP_MJ_CLOSE                    0x02
#define IRP_MJ_READ                     0x03
#define IRP_MJ_WRITE                    0x04
#define IRP_MJ_QUERY_INFORMATION        0x05
#define IRP_MJ_SET_INFORMATION          0x05
#define IRP_MJ_QUERY_EA                 0x07
#define IRP_MJ_SET_EA                   0x08
#define IRP_MJ_FLUSH_BUFFERS            0x08
#define IRP_MJ_QUERY_VOLUME_INFORMATION 0x09
#define IRP_MJ_SET_VOLUME_INFORMATION   0x0a
#define IRP_MJ_DIRECTORY_CONTROL        0x0b
#define IRP_MJ_FILE_SYSTEM_CONTROL      0x0c
#define IRP_MJ_DEVICE_CONTROL           0x0e
#define IRP_MJ_INTERNAL_DEVICE_CONTROL  0x0f
#define IRP_MJ_SHUTDOWN                 0x10
#define IRP_MJ_LOCK_CONTROL             0x11
#define IRP_MJ_CLEANUP                  0x12
#define IRP_MJ_CREATE_MAILSLOT          0x13
#define IRP_MJ_QUERY_SECURITY           0x13
#define IRP_MJ_SET_SECURITY             0x14
#define IRP_MJ_QUERY_QUOTA              0x15
#define IRP_MJ_SET_QUOTA                0x16
#define IRP_MJ_PNP                      0x1b

/* ----------------------------------------------------------------------------
 * PNP Minor Functions
 * -------------------------------------------------------------------------- */
#define IRP_MN_START_DEVICE                 0x00
#define IRP_MN_QUERY_REMOVE_DEVICE          0x01
#define IRP_MN_REMOVE_DEVICE                0x02
#define IRP_MN_CANCEL_REMOVE_DEVICE         0x03
#define IRP_MN_STOP_DEVICE                  0x04
#define IRP_MN_QUERY_STOP_DEVICE            0x05
#define IRP_MN_CANCEL_STOP_DEVICE           0x06
#define IRP_MN_QUERY_DEVICE_RELATIONS       0x07
#define IRP_MN_QUERY_INTERFACE              0x08
#define IRP_MN_QUERY_CAPABILITIES           0x09
#define IRP_MN_QUERY_RESOURCES              0x0a
#define IRP_MN_QUERY_RESOURCE_REQUIREMENTS  0x0b
#define IRP_MN_QUERY_DEVICE_TEXT            0x0c
#define IRP_MN_FILTER_RESOURCE_REQUIREMENTS 0x0c
#define IRP_MN_READ_CONFIG                  0x0d
#define IRP_MN_WRITE_CONFIG                 0x0e
#define IRP_MN_EJECT                        0x0e
#define IRP_MN_SET_LOCK                     0x0f
#define IRP_MN_QUERY_ID                     0x0f
#define IRP_MN_QUERY_PNP_DEVICE_STATE       0x10
#define IRP_MN_QUERY_BUS_INFORMATION        0x10
#define IRP_MN_DEVICE_USAGE_NOTIFICATION    0x11
#define IRP_MN_SURPRISE_REMOVAL             0x11
#define IRP_MN_QUERY_LEGACY_BUS_INFORMATION 0x12
#define IRP_MN_DEVICE_ENUMERATED            0x12
#define IRP_MN_QUERY_INTERFACE              0x13

/* ----------------------------------------------------------------------------
 * Device Relations
 * -------------------------------------------------------------------------- */
typedef enum _DEVICE_RELATION_TYPE {
    BusRelations = 0,
    EjectionRelations = 1,
    PowerRelations = 2,
    RemovalRelations = 3,
    TargetDeviceRelation = 3,
    SingleBusRelations = 4,
} DEVICE_RELATION_TYPE;

typedef struct _DEVICE_RELATIONS {
    ULONG Count;
    struct _DEVICE_OBJECT *Objects[1];
} DEVICE_RELATIONS, *PDEVICE_RELATIONS;

/* ----------------------------------------------------------------------------
 * Device Object
 * -------------------------------------------------------------------------- */
struct _DEVICE_OBJECT;
struct _DRIVER_OBJECT;
struct _FILE_OBJECT;
struct _IRP;

typedef struct _DEVICE_OBJECT {
    CSHORT Type;
    USHORT Size;
    LONG ReferenceCount;
    struct _DRIVER_OBJECT *DriverObject;
    struct _DEVICE_OBJECT *NextDevice;
    struct _DEVICE_OBJECT *AttachedDevice;
    struct _DRIVER_OBJECT *CurrentIrp;
    void *Timer;
    ULONG Flags;
    ULONG Characteristics;
    void *Vpb;
    void *DeviceExtension;
    ULONG DeviceType;
    CCHAR StackSize;
    union {
        LIST_ENTRY ListEntry;
        WAIT_CONTEXT_BLOCK Wcb;
    } Queue;
    ULONG AlignmentRequirement;
    KDEVICE_QUEUE DeviceQueue;
    KDPC Dpc;
    ULONG ActiveThreadCount;
    void *SecurityDescriptor;
    KEVENT DeviceLock;
    USHORT SectorSize;
    USHORT Spare1;
    struct _DEVOBJ_EXTENSION *DeviceObjectExtension;
    ULONG Reserved;
} DEVICE_OBJECT, *PDEVICE_OBJECT;

typedef struct _DEVOBJ_EXTENSION {
    CSHORT Type;
    USHORT Size;
    struct _DEVICE_OBJECT *DeviceObject;
    ULONG PowerFlags;
    struct _DEVICE_OBJECT_POWER_EXTENSION *Dope;
} DEVOBJ_EXTENSION, *PDEVOBJ_EXTENSION;

/* ----------------------------------------------------------------------------
 * Driver Object
 * -------------------------------------------------------------------------- */
#define DRIVER_OBJECT_MAX_IRP 28

typedef struct _DRIVER_OBJECT {
    CSHORT Type;
    USHORT Size;
    struct _DEVICE_OBJECT *DeviceObject;
    ULONG Flags;
    PVOID DriverStart;
    ULONG DriverSize;
    PVOID DriverSection;
    struct _DRIVER_EXTENSION *DriverExtension;
    UNICODE_STRING DriverName;
    PUNICODE_STRING HardwareDatabase;
    PFAST_IO_DISPATCH FastIoDispatch;
    NTSTATUS (*DriverInit)(struct _DRIVER_OBJECT *, PUNICODE_STRING);
    void (*DriverStartIo)(struct _DEVICE_OBJECT *, struct _IRP *);
    void (*DriverUnload)(struct _DRIVER_OBJECT *);
    NTSTATUS (*MajorFunction[28])(struct _DEVICE_OBJECT *, struct _IRP *);
} DRIVER_OBJECT, *PDRIVER_OBJECT;

typedef struct _DRIVER_EXTENSION {
    struct _DRIVER_OBJECT *DriverObject;
    NTSTATUS (*AddDevice)(struct _DRIVER_OBJECT *, struct _DEVICE_OBJECT *);
    ULONG Count;
    UNICODE_STRING ServiceKeyName;
} DRIVER_EXTENSION, *PDRIVER_EXTENSION;

/* ----------------------------------------------------------------------------
 * IRP (I/O Request Packet)
 * -------------------------------------------------------------------------- */
typedef struct _IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef struct _IO_STACK_LOCATION {
    UCHAR MajorFunction;
    UCHAR MinorFunction;
    UCHAR Flags;
    UCHAR Control;
    union {
        struct {
            PVOID SecurityContext;
            ULONG Options;
            USHORT FileAttributes;
            USHORT ShareAccess;
            ULONG EaLength;
        } Create;
        struct {
            PVOID Key;
            ULONG Flags;
        } QueryDirectory;
        struct {
            ULONG Length;
            PVOID Key;
            ULONG Flags;
        } QueryFile;
        struct {
            ULONG Length;
            PVOID CompletionFilter;
        } NotifyDirectory;
        struct {
            ULONG Length;
            ULONG CompletionFilter;
            PVOID Buffer;
        } DirectoryControl;
        struct {
            ULONG OutputBufferLength;
            ULONG InputBufferLength;
            ULONG IoControlCode;
            PVOID Type3InputBuffer;
        } DeviceIoControl;
        struct {
            ULONG SecurityInformation;
            ULONG Length;
        } QuerySecurity;
        struct {
            ULONG SecurityInformation;
            PVOID SecurityDescriptor;
        } SetSecurity;
        struct {
            PVOID Buffer;
            ULONG Length;
        } QueryQuota;
        struct {
            PVOID Buffer;
            ULONG Length;
        } SetQuota;
        struct {
            PVOID SystemBuffer;
            ULONG Length;
        } FileSystemControl;
        struct {
            ULONG_PTR Argument1;
            ULONG_PTR Argument2;
            ULONG_PTR Argument3;
            ULONG_PTR Argument4;
        } Others;
    } Parameters;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    NTSTATUS (*CompletionRoutine)(PDEVICE_OBJECT, PIRP, PVOID);
    PVOID Context;
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;

typedef struct _IRP {
    CSHORT Type;
    USHORT Size;
    PVOID MdlAddress;
    ULONG Flags;
    union {
        struct _IRP *MasterIrp;
        LONG IrpCount;
        PVOID SystemBuffer;
    } AssociatedIrp;
    IO_STATUS_BLOCK IoStatus;
    PVOID UserBuffer;
    struct _IRP *Next;
    struct _KTHREAD *Tail;
    KPROCESSOR_MODE RequestorMode;
    PVOID UserEvent;
    struct _IO_STATUS_BLOCK UserIosb;
    PKEVENT UserEvent;
    CCHAR StackCount;
    CCHAR CurrentLocation;
    UCHAR Cancel;
    KIRQL CancelIrql;
    CCHAR ApcEnvironment;
    UCHAR AllocationFlags;
    PIO_STATUS_BLOCK UserIosb;
    PKEVENT UserEvent;
    union {
        struct {
            PVOID UserApcRoutine;
            PVOID UserApcContext;
        } AsynchronousParameters;
        PVOID AllocationSize;
    } Overlay;
    PVOID CancelRoutine;
    PVOID UserBuffer;
    union {
        struct {
            union {
                PVOID DriverContext[4];
                PVOID Pointer;
            };
            PETHREAD Thread;
            PCHAR AuxiliaryBuffer;
            struct {
                LIST_ENTRY ListEntry;
                struct _IO_STACK_LOCATION *CurrentStackLocation;
                ULONG PacketType;
                PVOID OriginalFileObject;
            } Overlay;
        } Overlay;
        KAPC Apc;
        PVOID CompletionKey;
    } Tail;
} IRP, *PIRP;

/* ----------------------------------------------------------------------------
 * PNP Manager API
 * -------------------------------------------------------------------------- */

NTSTATUS NTAPI IoCreateDevice(PDRIVER_OBJECT DriverObject, ULONG DeviceExtensionSize,
                               PUNICODE_STRING DeviceName, ULONG DeviceType,
                               ULONG DeviceCharacteristics, BOOLEAN Exclusive,
                               PDEVICE_OBJECT *DeviceObject);

VOID NTAPI IoDeleteDevice(PDEVICE_OBJECT DeviceObject);

NTSTATUS NTAPI IoRegisterDriverReinitialization(PDRIVER_OBJECT DriverObject,
                                                 PDRIVER_REINITIALIZE DriverReinitializationRoutine,
                                                 PVOID Context);

NTSTATUS NTAPI IoRegisterDeviceInterface(PDEVICE_OBJECT PhysicalDeviceObject,
                                          const GUID *InterfaceClassGuid,
                                          PUNICODE_STRING ReferenceString,
                                          PUNICODE_STRING SymbolicLinkName);

NTSTATUS NTAPI IoSetDeviceInterfaceState(PUNICODE_STRING SymbolicLinkName, BOOLEAN Enable);

NTSTATUS NTAPI IoGetDeviceInterfaces(const GUID *InterfaceClassGuid,
                                      PVOID PhysicalDeviceObject,
                                      ULONG Flags,
                                      PUNICODE_STRING *SymbolicLinkList);

VOID NTAPI IoFreeDeviceInterfaces(PUNICODE_STRING SymbolicLinkList);

/* Device enumeration */
NTSTATUS NTAPI IoEnumerateDeviceObjectList(PDRIVER_OBJECT DriverObject,
                                            PDEVICE_OBJECT *DeviceObjectList,
                                            ULONG DeviceObjectListSize,
                                            PULONG ActualNumberDeviceObjects);

NTSTATUS NTAPI IoGetDeviceObjectPointer(PUNICODE_STRING ObjectName,
                                         ACCESS_MASK DesiredAccess,
                                         PFILE_OBJECT *FileObject,
                                         PDEVICE_OBJECT *DeviceObject);

VOID NTAPI IoAttachDeviceToDeviceStack(PDEVICE_OBJECT SourceDevice, PDEVICE_OBJECT TargetDevice);
PDEVICE_OBJECT NTAPI IoAttachDeviceToDeviceStackSafe(PDEVICE_OBJECT SourceDevice, PDEVICE_OBJECT TargetDevice);
VOID NTAPI IoDetachDevice(PDEVICE_OBJECT TargetDevice);

/* Resource allocation */
NTSTATUS NTAPI IoReportResourceUsage(PUNICODE_STRING DriverClassName,
                                      PDRIVER_OBJECT DriverObject,
                                      PCM_RESOURCE_LIST DriverList,
                                      ULONG DriverListSize,
                                      PDEVICE_OBJECT DeviceObject,
                                      PCM_RESOURCE_LIST DeviceList,
                                      ULONG DeviceListSize,
                                      BOOLEAN OverrideConflict,
                                      PBOOLEAN ConflictDetected);

/* PnP Notification */
typedef enum _BUS_QUERY_ID_TYPE {
    BusQueryDeviceID = 0,
    BusQueryHardwareIDs = 1,
    BusQueryCompatibleIDs = 2,
    BusQueryInstanceID = 3,
    BusQueryDeviceDescription = 4,
} BUS_QUERY_ID_TYPE;

typedef enum _DEVICE_TEXT_TYPE {
    DeviceTextDescription = 0,
    DeviceTextLocationInformation = 1,
} DEVICE_TEXT_TYPE;

NTSTATUS NTAPI IoRegisterPlugPlayNotification(ULONG EventCategory,
                                               ULONG Flags,
                                               PVOID EventData,
                                               PDRIVER_OBJECT DriverObject,
                                               PDRIVER_NOTIFICATION_CALLBACK Callback,
                                               PVOID Context,
                                               PVOID *NotificationEntry);

VOID NTAPI IoUnregisterPlugPlayNotification(PVOID NotificationEntry);

/* Fast I/O */
typedef struct _FAST_IO_DISPATCH {
    ULONG SizeOfFastIoDispatch;
    BOOLEAN (*FastIoCheckIfPossible)(PFILE_OBJECT, PLARGE_INTEGER, ULONG, BOOLEAN, BOOLEAN, PIO_STATUS_BLOCK, PDEVICE_OBJECT);
    BOOLEAN (*FastIoRead)(PFILE_OBJECT, PLARGE_INTEGER, ULONG, BOOLEAN, BOOLEAN, PVOID, PIO_STATUS_BLOCK, PDEVICE_OBJECT);
    BOOLEAN (*FastIoWrite)(PFILE_OBJECT, PLARGE_INTEGER, ULONG, BOOLEAN, BOOLEAN, PVOID, PIO_STATUS_BLOCK, PDEVICE_OBJECT);
    BOOLEAN (*FastIoQueryBasicInfo)(PFILE_OBJECT, PVOID, PIO_STATUS_BLOCK, PDEVICE_OBJECT);
    BOOLEAN (*FastIoQueryStandardInfo)(PFILE_OBJECT, PVOID, PIO_STATUS_BLOCK, PDEVICE_OBJECT);
    BOOLEAN (*FastIoLock)(PFILE_OBJECT, PLARGE_INTEGER, PLARGE_INTEGER, PDEVICE_OBJECT);
    BOOLEAN (*FastIoUnlockSingle)(PFILE_OBJECT, PLARGE_INTEGER, PLARGE_INTEGER, PDEVICE_OBJECT);
    BOOLEAN (*FastIoUnlockAll)(PFILE_OBJECT, PLARGE_INTEGER, PLARGE_INTEGER, PDEVICE_OBJECT);
    BOOLEAN (*FastIoUnlockAllByKey)(PFILE_OBJECT, PVOID, PLARGE_INTEGER, PDEVICE_OBJECT);
    BOOLEAN (*FastIoDeviceControl)(PFILE_OBJECT, BOOLEAN, PVOID, ULONG, PVOID, ULONG, ULONG, PIO_STATUS_BLOCK, PDEVICE_OBJECT);
    VOID (*MdlReadComplete)(PFILE_OBJECT, PMDL, PDEVICE_OBJECT);
    VOID (*MdlWriteComplete)(PFILE_OBJECT, PMDL, PDEVICE_OBJECT);
    BOOLEAN (*FastIoQueryOpen)(PFILE_OBJECT, PIO_STATUS_BLOCK, PDEVICE_OBJECT);
    BOOLEAN (*FastIoQueryNetworkOpenInfo)(PFILE_OBJECT, PVOID, PIO_STATUS_BLOCK, PDEVICE_OBJECT);
    BOOLEAN (*FastIoMdlRead)(PFILE_OBJECT, PLARGE_INTEGER, ULONG, ULONG, PMDL, PIO_STATUS_BLOCK, PDEVICE_OBJECT);
    BOOLEAN (*FastIoMdlReadComplete)(PFILE_OBJECT, PMDL, PDEVICE_OBJECT);
    BOOLEAN (*FastIoPrepareMdlWrite)(PFILE_OBJECT, PLARGE_INTEGER, ULONG, ULONG, PMDL, PIO_STATUS_BLOCK, PDEVICE_OBJECT);
    BOOLEAN (*FastIoMdlWriteComplete)(PFILE_OBJECT, PLARGE_INTEGER, PMDL, PDEVICE_OBJECT);
    BOOLEAN (*FastIoReadCompressed)(PFILE_OBJECT, PLARGE_INTEGER, ULONG, ULONG, PVOID, PIO_STATUS_BLOCK, PDEVICE_OBJECT);
    BOOLEAN (*FastIoWriteCompressed)(PFILE_OBJECT, PLARGE_INTEGER, ULONG, ULONG, PVOID, PIO_STATUS_BLOCK, PDEVICE_OBJECT);
    BOOLEAN (*MdlReadCompleteCompressed)(PFILE_OBJECT, PMDL, PDEVICE_OBJECT);
    BOOLEAN (*MdlWriteCompleteCompressed)(PFILE_OBJECT, PMDL, PDEVICE_OBJECT);
    BOOLEAN (*FastIoQueryStandardInfo)(PFILE_OBJECT, PVOID, PIO_STATUS_BLOCK, PDEVICE_OBJECT);
} FAST_IO_DISPATCH, *PFAST_IO_DISPATCH;

#define FAST_IO_DISPATCH_VERSION 1

/* -------------------------------------------------------------------------- */
VOID NTAPI PnpInitSystem(VOID);
NTSTATUS NTAPI PnpEnumerateDevices(VOID);
NTSTATUS NTAPI PnpRegisterDeviceDriver(PDRIVER_OBJECT DriverObject, PWSTR ServiceName);
NTSTATUS NTAPI PnpAddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject);
NTSTATUS NTAPI PnpDispatchPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp);

#endif /* _PNP_H_ */
