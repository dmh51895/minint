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

/* Forward declaration for KTHREAD (defined in ps.h) */
struct _KTHREAD;
typedef struct _KTHREAD *PKTHREAD;

/* ---- Dispatcher header — common to all dispatcher objects -------------- */

typedef struct _DISPATCHER_HEADER {
    UCHAR   Type;               /* EventObject, SemaphoreObject, etc. */
    UCHAR   SignalState;        /* 0 = not signaled, 1 = signaled     */
    USHORT  Size;
    LIST_ENTRY WaitListHead;    /* threads waiting on this object     */
} DISPATCHER_HEADER, *PDISPATCHER_HEADER;

/* ---- Dispatcher object types ------------------------------------------- */

#define EventObject             0
#define EventPairObject         1
#define SemaphoreObject         3
#define TimerObject             4
#define MutantObject            5

/* ---- Event types for KeInitializeEvent --------------------------------- */

#define NotificationEvent       0   /* stays signaled until reset       */
#define SynchronizationEvent    1   /* resets automatically on release  */

/* ---- KEVENT ------------------------------------------------------------ */

typedef struct _KEVENT {
    DISPATCHER_HEADER Header;
} KEVENT, *PKEVENT;

/* ---- KSEMAPHORE -------------------------------------------------------- */

typedef struct _KSEMAPHORE {
    DISPATCHER_HEADER Header;
    ULONG    Limit;              /* maximum count                       */
    LONG     CurrentCount;       /* actual semaphore count              */
} KSEMAPHORE, *PKSEMAPHORE;

/* ---- KMUTEX (called KMUTANT in NT internals) --------------------------- */

typedef struct _KMUTANT {
    DISPATCHER_HEADER Header;
    PVOID   OwnerThread;         /* current owning thread               */
    ULONG   Abandoned;           /* non-zero if owner terminated        */
    ULONG   ApcDisableCount;
} KMUTANT, *PKMUTANT;

/* ---- Wait reasons returned by KeWaitForSingleObject --------------------- */

#define STATUS_WAIT_0           0x00000000L
#define STATUS_TIMEOUT          0x00000102L
#define STATUS_ABANDONED_WAIT_0 0x00000080L

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
