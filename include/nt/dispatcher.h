/*
 * MinNT - dispatcher.h
 * Dispatcher objects: KEVENT, KSEMAPHORE, KMUTEX + wait API.
 * Real NT 6.x semantics: threads block on wait objects, are woken by
 * KeSetEvent/KeReleaseSemaphore/KeReleaseMutex, and the scheduler
 * only runs ready threads.
 */

#ifndef _DISPATCHER_H_
#define _DISPATCHER_H_

#include <nt/ntdef.h>
#include <nt/ke.h>

/* Forward declarations (defined in other headers) */
struct _KTHREAD;
typedef struct _KTHREAD *PKTHREAD;
struct _KDPC;
typedef struct _KDPC *PKDPC;
struct _DEVICE_OBJECT;
typedef struct _DEVICE_OBJECT *PDEVICE_OBJECT;
struct _IRP;
typedef struct _IRP *PIRP;
struct _DRIVER_OBJECT;
typedef struct _DRIVER_OBJECT *PDRIVER_OBJECT;

/* ---- Dispatcher header — common to all dispatcher objects -------------- */

typedef struct _DISPATCHER_HEADER {
    UCHAR   Type;
    LONG    SignalState;
    USHORT  Size;
    LIST_ENTRY WaitListHead;
} DISPATCHER_HEADER, *PDISPATCHER_HEADER;

/* ---- Dispatcher object types ------------------------------------------- */

#define EventObject             0
#define EventPairObject         1
#define SemaphoreObject         3
#define TimerObject             4
#define MutantObject            5

/* ---- Event types for KeInitializeEvent --------------------------------- */

#define NotificationEvent       0
#define SynchronizationEvent    1

/* ---- KEVENT ------------------------------------------------------------ */

typedef struct _KEVENT {
    DISPATCHER_HEADER Header;
} KEVENT, *PKEVENT;

/* ---- KSEMAPHORE -------------------------------------------------------- */

typedef struct _KSEMAPHORE {
    DISPATCHER_HEADER Header;
    ULONG    Limit;
    LONG     CurrentCount;
} KSEMAPHORE, *PKSEMAPHORE;

/* ---- KMUTEX (called KMUTANT in NT internals) --------------------------- */

typedef struct _KMUTANT {
    DISPATCHER_HEADER Header;
    PVOID   OwnerThread;
    ULONG   Abandoned;
    ULONG   ApcDisableCount;
} KMUTANT, *PKMUTANT;

/* ---- Wait reasons returned by KeWaitForSingleObject --------------------- */

#define STATUS_WAIT_0           0x00000000L
#define STATUS_TIMEOUT          0x00000102L
#define STATUS_ABANDONED_WAIT_0 0x00000080L

/* ---- KAPC ---------------------------------------------------------------- */

typedef struct _KAPC {
    UCHAR   Type;
    UCHAR   ApcMode;
    BOOLEAN Inserted;
    BOOLEAN KernelApc;
    PVOID   NormalRoutine;
    PVOID   NormalContext;
    PVOID   SystemArgument1;
    PVOID   SystemArgument2;
    LIST_ENTRY ApcListEntry;
    PKTHREAD Thread;
} KAPC, *PKAPC;

/* ---- KDPC ---------------------------------------------------------------- */

typedef VOID (NTAPI *KDPC_Routine)(struct _KDPC *Dpc, PVOID DeferredContext,
                                   PVOID SystemArgument1, PVOID SystemArgument2);

typedef struct _KDPC {
    UCHAR   Type;
    UCHAR   Importance;
    USHORT  Number;
    KDPC_Routine DeferredRoutine;
    PVOID   DeferredContext;
    PVOID   SystemArgument1;
    PVOID   SystemArgument2;
    PVOID   Lock;
} KDPC, *PKDPC;

/* ---- KDEVICE_QUEUE_ENTRY ------------------------------------------------- */

typedef struct _KDEVICE_QUEUE_ENTRY {
    LIST_ENTRY DeviceListEntry;
} KDEVICE_QUEUE_ENTRY, *PKDEVICE_QUEUE_ENTRY;

/* ---- KDEVICE_QUEUE ------------------------------------------------------- */

typedef struct _KDEVICE_QUEUE {
    ULONG   Type;
    ULONG   Size;
    LIST_ENTRY DeviceListHead;
    KSPIN_LOCK Lock;
    BOOLEAN Busy;
} KDEVICE_QUEUE, *PKDEVICE_QUEUE;

/* ---- WAIT_CONTEXT_BLOCK -------------------------------------------------- */

typedef NTSTATUS (NTAPI *PDRIVER_CONTROL)(struct _DEVICE_OBJECT *DeviceObject,
                                            struct _IRP *Irp, PVOID Context,
                                            ULONG MapRegisterCount);

typedef struct _WAIT_CONTEXT_BLOCK {
    KDEVICE_QUEUE_ENTRY WaitQueueEntry;
    PDRIVER_CONTROL DeviceRoutine;
    PVOID DeviceContext;
    ULONG NumberOfMapRegisters;
    PVOID DeviceObject;
    PVOID CurrentIrp;
    PKDPC BufferChainingDpc;
} WAIT_CONTEXT_BLOCK, *PWAIT_CONTEXT_BLOCK;

/* ---- GUID ---------------------------------------------------------------- */

#ifndef GUID_DEFINED
#define GUID_DEFINED
typedef struct _GUID {
    ULONG   Data1;
    USHORT  Data2;
    USHORT  Data3;
    UCHAR   Data4[8];
} GUID;
typedef GUID OLE_GUID;
typedef GUID CLSID;
typedef GUID IID;
#endif

/* ---- PDRIVER_REINITIALIZE ------------------------------------------------ */

typedef VOID (NTAPI *PDRIVER_REINITIALIZE)(struct _DRIVER_OBJECT *DriverObject,
                                            PVOID Context, ULONG Count);

/* ---- API --------------------------------------------------------------- */

/* Event manipulation */
VOID     NTAPI KeInitializeEvent(PKEVENT Event, ULONG Type, BOOLEAN State);
LONG     NTAPI KeSetEvent(PKEVENT Event, KPRIORITY Increment, BOOLEAN Wait);
LONG     NTAPI KeResetEvent(PKEVENT Event);
LONG     NTAPI KeClearEvent(PKEVENT Event);
BOOLEAN  NTAPI KeReadStateEvent(PKEVENT Event);

/* Semaphore manipulation */
VOID     NTAPI KeInitializeSemaphore(PKSEMAPHORE Semaphore, LONG Count, LONG Limit);
LONG     NTAPI KeReleaseSemaphore(PKSEMAPHORE Semaphore, KPRIORITY Increment, LONG Adjustment, BOOLEAN Wait);
LONG     NTAPI KeReadStateSemaphore(PKSEMAPHORE Semaphore);

/* Mutex manipulation */
VOID     NTAPI KeInitializeMutex(PKMUTANT Mutex, ULONG Level);
LONG     NTAPI KeReleaseMutex(PKMUTANT Mutex, BOOLEAN Wait);

/* Wait functions */
NTSTATUS NTAPI KeWaitForSingleObject(PVOID Object, KWAIT_REASON WaitReason,
                                     KPROCESSOR_MODE WaitMode, BOOLEAN Alertable,
                                     PLARGE_INTEGER Timeout);

NTSTATUS NTAPI KeWaitForMultipleObjects(ULONG Count, PVOID *Object,
                                        WAIT_TYPE WaitType,
                                        KWAIT_REASON WaitReason,
                                        KPROCESSOR_MODE WaitMode,
                                        BOOLEAN Alertable,
                                        PLARGE_INTEGER Timeout);

/* Internal dispatcher helpers */
VOID NTAPI KiWaitThread(PKTHREAD Thread, PDISPATCHER_HEADER Object);
VOID NTAPI KiWakeThread(PKTHREAD Thread, NTSTATUS WaitStatus);

/* Scheduler globals (defined in ps/psmgr.c) */
extern LIST_ENTRY KiReadyListHead;
extern KSPIN_LOCK KiDispatcherLock;

#endif /* _DISPATCHER_H_ */
