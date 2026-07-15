/*
 * MinNT - ke/dispatch.c
 * Dispatcher objects: KEVENT, KSEMAPHORE, KMUTANT + KeWaitForSingleObject.
 * Threads that wait on non-signaled objects are moved off the ready queue
 * into the object's wait list. When the object becomes signaled, waiting
 * threads are moved back to the ready queue.
 */

#include <nt/ke.h>
#include <nt/ps.h>
#include <nt/dispatcher.h>
#include <nt/rtl.h>
#include <nt/hal.h>

extern LIST_ENTRY KiReadyListHead;

#define TAG_DISP 0x50534944  /* 'DISP' */

/* ---- Forward declarations from ps/psmgr.c ----------------------------- */

extern LIST_ENTRY KiReadyListHead;
extern KSPIN_LOCK KiDispatcherLock;
extern VOID KiSwapContext(ULONG_PTR *OldRsp, ULONG_PTR NewRsp);

/* ---- Wait: block the current thread on an object's wait list ----------- */

VOID NTAPI KiWaitThread(PKTHREAD Thread, PDISPATCHER_HEADER Object)
{
    KIRQL irql;

    KeAcquireSpinLock(&KiDispatcherLock, &irql);

    Thread->State = Waiting;
    Thread->WaitObject = Object;
    InsertTailList(&Object->WaitListHead, &Thread->WaitListEntry);

    KeReleaseSpinLock(&KiDispatcherLock, irql);

    KiDispatchNextThread();

    /* After KiDispatchNextThread returns, we were dispatched back.
     * Check if we were woken (State changed from Waiting to Ready).
     * If still Waiting, something went wrong - but in cooperative
     * scheduling we should have been woken by the event signal.
     */
    KeAcquireSpinLock(&KiDispatcherLock, &irql);
    if (Thread->State == Waiting)
    {
        /* We timed out or weren't woken properly */
        Thread->State = Ready;
        RemoveEntryList(&Thread->WaitListEntry);
        Thread->WaitListEntry.Flink = NULL;
        Thread->WaitListEntry.Blink = NULL;
        InsertTailList(&KiReadyListHead, &Thread->WaitListEntry);
    }
    KeReleaseSpinLock(&KiDispatcherLock, irql);
}

/* ---- Wake: move a thread from wait list to ready queue ---------------- */

VOID NTAPI KiWakeThread(PKTHREAD Thread, NTSTATUS WaitStatus)
{
    KIRQL irql;

    KeAcquireSpinLock(&KiDispatcherLock, &irql);

    /* The caller already unlinked the thread from the wait list. */
    Thread->WaitListEntry.Flink = NULL;
    Thread->WaitListEntry.Blink = NULL;

    /* Set return status and make ready */
    Thread->WaitStatus = (ULONG_PTR)WaitStatus;
    Thread->State = Ready;
    Thread->WaitObject = NULL;

    /* Add to ready queue */
    InsertTailList(&KiReadyListHead, &Thread->WaitListEntry);

    KeReleaseSpinLock(&KiDispatcherLock, irql);
}



/* Caller must hold KiDispatcherLock. Does NOT acquire or release it. */
static PKTHREAD KiWakeFirstWaiterUnsafe(PDISPATCHER_HEADER Object, NTSTATUS WaitStatus)
{
    if (IsListEmpty(&Object->WaitListHead)) {
        return NULL;
    }

    PLIST_ENTRY Entry = RemoveHeadList(&Object->WaitListHead);
    PKTHREAD Thread = CONTAINING_RECORD(Entry, KTHREAD, WaitListEntry);

    Thread->WaitListEntry.Flink = NULL;
    Thread->WaitListEntry.Blink = NULL;
    Thread->WaitStatus = (ULONG_PTR)WaitStatus;
    Thread->State = Ready;
    Thread->WaitObject = NULL;
    InsertTailList(&KiReadyListHead, &Thread->WaitListEntry);

    return Thread;
}

static PKTHREAD KiWakeFirstWaiter(PDISPATCHER_HEADER Object, NTSTATUS WaitStatus)
{
    KIRQL irql;
    KeAcquireSpinLock(&KiDispatcherLock, &irql);
    PKTHREAD Thread = KiWakeFirstWaiterUnsafe(Object, WaitStatus);
    KeReleaseSpinLock(&KiDispatcherLock, irql);
    return Thread;
}

/* Caller must hold KiDispatcherLock. Does NOT acquire or release it. */
static VOID KiWakeAllWaitersUnsafe(PDISPATCHER_HEADER Object, NTSTATUS WaitStatus)
{
    while (!IsListEmpty(&Object->WaitListHead))
    {
        PKTHREAD Thread = KiWakeFirstWaiterUnsafe(Object, WaitStatus);
        (VOID)Thread;
    }
}

/* ========================================================================
 * KEVENT
 * ====================================================================== */

VOID NTAPI KeInitializeEvent(PKEVENT Event, ULONG Type, BOOLEAN State)
{
    RtlZeroMemory(Event, sizeof(KEVENT));
    Event->Header.Type = (UCHAR)(Type == NotificationEvent ? EventObject : SynchronizationEvent);
    Event->Header.SignalState = (UCHAR)(State ? 1 : 0);
    Event->Header.Size = sizeof(KEVENT);
    InitializeListHead(&Event->Header.WaitListHead);
}

LONG NTAPI KeSetEvent(PKEVENT Event, KPRIORITY Increment, BOOLEAN Wait)
{
    KIRQL irql;
    UNREFERENCED_PARAMETER(Increment);
    UNREFERENCED_PARAMETER(Wait);

    KeAcquireSpinLock(&KiDispatcherLock, &irql);

    LONG PreviousState = (LONG)Event->Header.SignalState;
    Event->Header.SignalState = 1;

    if (Event->Header.Type == EventObject)
    {
        KiWakeAllWaitersUnsafe(&Event->Header, STATUS_WAIT_0);
    }
    else
    {
        PKTHREAD Wakened = KiWakeFirstWaiterUnsafe(&Event->Header, STATUS_WAIT_0);
        if (!Wakened)
        {
            KeReleaseSpinLock(&KiDispatcherLock, irql);
            return PreviousState;
        }
        Event->Header.SignalState = 0;
    }

    KeReleaseSpinLock(&KiDispatcherLock, irql);
    return PreviousState;
}

LONG NTAPI KeResetEvent(PKEVENT Event)
{
    KIRQL irql;
    KeAcquireSpinLock(&KiDispatcherLock, &irql);
    LONG PreviousState = (LONG)Event->Header.SignalState;
    Event->Header.SignalState = 0;
    KeReleaseSpinLock(&KiDispatcherLock, irql);
    return PreviousState;
}

LONG NTAPI KeClearEvent(PKEVENT Event)
{
    return KeResetEvent(Event);
}

BOOLEAN NTAPI KeReadStateEvent(PKEVENT Event)
{
    return (BOOLEAN)Event->Header.SignalState;
}

/* ========================================================================
 * KSEMAPHORE
 * ====================================================================== */

VOID NTAPI KeInitializeSemaphore(PKSEMAPHORE Semaphore, LONG Count, LONG Limit)
{
    RtlZeroMemory(Semaphore, sizeof(KSEMAPHORE));
    Semaphore->Header.Type = SemaphoreObject;
    Semaphore->CurrentCount = Count;
    Semaphore->Header.SignalState = (UCHAR)(Count > 0 ? 1 : 0);
    Semaphore->Limit = (ULONG)Limit;
    InitializeListHead(&Semaphore->Header.WaitListHead);
}

LONG NTAPI KeReleaseSemaphore(PKSEMAPHORE Semaphore, KPRIORITY Increment,
                                LONG Adjustment, BOOLEAN Wait)
{
    KIRQL irql;
    UNREFERENCED_PARAMETER(Increment);
    UNREFERENCED_PARAMETER(Wait);

    LONG PreviousCount = Semaphore->CurrentCount;

    /* Acquire dispatcher lock for SMP safety */
    KeAcquireSpinLock(&KiDispatcherLock, &irql);

    /* Increment the count, capped at Limit */
    Semaphore->CurrentCount += Adjustment;
    if (Semaphore->CurrentCount > (LONG)Semaphore->Limit)
        Semaphore->CurrentCount = Semaphore->Limit;

    Semaphore->Header.SignalState = (UCHAR)(Semaphore->CurrentCount > 0 ? 1 : 0);

    /* Wake one waiter per adjustment count */
    for (LONG i = 0; i < Adjustment; i++)
    {
        if (!IsListEmpty(&Semaphore->Header.WaitListHead))
        {
            KiWakeFirstWaiterUnsafe(&Semaphore->Header, STATUS_WAIT_0);
        }
        else
        {
            /* No waiters — count stays incremented */
            break;
        }
    }

    KeReleaseSpinLock(&KiDispatcherLock, irql);

    return PreviousCount;
}

LONG NTAPI KeReadStateSemaphore(PKSEMAPHORE Semaphore)
{
    if (!Semaphore) return 0;
    return Semaphore->CurrentCount;
}

/* ========================================================================
 * KMUTANT (KMUTEX)
 * ====================================================================== */

VOID NTAPI KeInitializeMutex(PKMUTANT Mutex, ULONG Level)
{
    UNREFERENCED_PARAMETER(Level);
    RtlZeroMemory(Mutex, sizeof(KMUTANT));
    Mutex->Header.Type = MutantObject;
    Mutex->Header.SignalState = 1;  /* initially signaled (available) */
    Mutex->Header.Size = sizeof(KMUTANT);
    InitializeListHead(&Mutex->Header.WaitListHead);
    Mutex->OwnerThread = NULL;
}

LONG NTAPI KeReleaseMutex(PKMUTANT Mutex, BOOLEAN Wait)
{
    UNREFERENCED_PARAMETER(Wait);

    PKTHREAD CurrentThread = KeGetCurrentThread();

    /* Check ownership — only the owner can release */
    if (Mutex->OwnerThread != CurrentThread)
    {
        DbgPrint("DISPATCH: mutex release by non-owner\n");
        return 0;
    }

    Mutex->OwnerThread = NULL;

    /* Wake one waiter */
    if (!IsListEmpty(&Mutex->Header.WaitListHead))
    {
        PKTHREAD Thread = KiWakeFirstWaiter(&Mutex->Header, STATUS_WAIT_0);
        if (Thread)
        {
            Mutex->OwnerThread = Thread;
            Mutex->Header.SignalState = 0;  /* now owned */
        }
    }
    else
    {
        Mutex->Header.SignalState = 1;  /* no waiters, available */
    }

    return 0;
}

/* ========================================================================
 * KeWaitForSingleObject
 * ====================================================================== */

NTSTATUS NTAPI KeWaitForSingleObject(PVOID Object, KWAIT_REASON WaitReason,
                                     KPROCESSOR_MODE WaitMode, BOOLEAN Alertable,
                                     PLARGE_INTEGER Timeout)
{
    PDISPATCHER_HEADER Header = (PDISPATCHER_HEADER)Object;
    PKTHREAD CurrentThread = KeGetCurrentThread();

    UNREFERENCED_PARAMETER(WaitReason);
    UNREFERENCED_PARAMETER(WaitMode);
    UNREFERENCED_PARAMETER(Alertable);

    /* Check if object is already signaled */
    if (Header->SignalState)
    {
        /* Consume the signal for synchronization objects */
        if (Header->Type == SynchronizationEvent ||
            Header->Type == MutantObject)
        {
            Header->SignalState = 0;
            if (Header->Type == MutantObject)
                ((PKMUTANT)Object)->OwnerThread = CurrentThread;
        }
        return STATUS_WAIT_0;
    }

    /* Check for timeout of 0 (poll) */
    if (Timeout && Timeout->QuadPart == 0)
        return STATUS_TIMEOUT;

    /* Block the thread on this object's wait list */
    KiWaitThread(CurrentThread, Header);

    /* When we wake up, return the status that was stored */
    return (NTSTATUS)CurrentThread->WaitStatus;
}

NTSTATUS NTAPI KeWaitForMultipleObjects(ULONG Count, PVOID *Object,
                                        WAIT_TYPE WaitType,
                                        KWAIT_REASON WaitReason,
                                        KPROCESSOR_MODE WaitMode,
                                        BOOLEAN Alertable,
                                        PLARGE_INTEGER Timeout)
{
    UNREFERENCED_PARAMETER(WaitType);

    /* Simplified: wait on the first non-signaled object */
    for (ULONG i = 0; i < Count; i++)
    {
        PDISPATCHER_HEADER Header = (PDISPATCHER_HEADER)Object[i];
        if (!Header->SignalState)
        {
            NTSTATUS Status = KeWaitForSingleObject(Object[i], WaitReason,
                                                     WaitMode, Alertable,
                                                     Timeout);
            if (NT_SUCCESS(Status))
                return (NTSTATUS)(STATUS_WAIT_0 + i);
            return Status;
        }
    }

    /* All objects are already signaled — return the first */
    return STATUS_WAIT_0;
}
