/*
 * MinNT - ke/timer.c
 *
 * Timer and DPC infrastructure for the MinNT kernel.
 *
 * Provides KTIMER/KDPC objects, KeSetTimer, KeCancelTimer,
 * KeInitializeDpc, etc. Timer expiration is driven by a dedicated
 * system thread that polls the timer list and fires expired timers'
 * DPCs. DPCs run on the polling thread (passive level, below
 * DISPATCH_LEVEL) - simple but correct.
 *
 * Time units: KeTickCount is incremented by the PIT ISR on every
 * timer tick (~1ms). Timer DueTime values are interpreted as
 * KeTickCount deltas when negative, or absolute tick counts when
 * positive (LARGE_INTEGER convention matches NT).
 */

#include <nt/ke.h>
#include <nt/ps.h>
#include <nt/hal.h>
#include <nt/dispatcher.h>
#include <nt/rtl.h>

/* Maximum number of concurrent timers */
#define MAX_TIMERS 64

/* Polling period for the timer thread */
#define TIMER_THREAD_PERIOD_MS 1

static KTIMER g_Timers[MAX_TIMERS];
static LIST_ENTRY g_TimerListHead;
static KSPIN_LOCK g_TimerListLock;
static LONG g_TimerThreadShouldExit = 0;
static PETHREAD g_TimerThread = NULL;

/* Initialize a timer object to an inert state. */
VOID NTAPI KeInitializeTimer(PKTIMER Timer)
{
    if (!Timer) return;
    RtlZeroMemory(Timer, sizeof(KTIMER));
    InitializeListHead(&Timer->TimerListEntry);
    Timer->InUse = FALSE;
    Timer->Cancelled = FALSE;
    Timer->DueTime = 0;
    Timer->Period = 0;
    Timer->Dpc = NULL;
}

/* Initialize a DPC object. The Routine is invoked when the associated
 * KTIMER fires. */
VOID NTAPI KeInitializeDpc(PKDPC Dpc, KDPC_Routine Routine, PVOID Context)
{
    if (!Dpc) return;
    RtlZeroMemory(Dpc, sizeof(KDPC));
    Dpc->DeferredRoutine = Routine;
    Dpc->DeferredContext = Context;
}

/* Initialize a periodic timer (KTIMER extension type). */
VOID NTAPI KeInitializeTimerEx(PKTIMER Timer, LONG Type)
{
    UNREFERENCED_PARAMETER(Type);
    KeInitializeTimer(Timer);
}

/* Return the current tick count. */
VOID NTAPI KeQueryTickCount(PULONG64 TickCount)
{
    if (!TickCount) return;
    *TickCount = KeTickCount;
}

/* High-resolution performance counter. We use the PIT tick count as the
 * counter and report the frequency (1ms = 1 tick) so callers can compute
 * elapsed time. On hardware with a TSC, this would read the TSC instead. */
VOID NTAPI KeQueryPerformanceCounter(PLARGE_INTEGER Counter, PLARGE_INTEGER Frequency)
{
    if (Counter) Counter->QuadPart = (LONGLONG)KeTickCount;
    if (Frequency) Frequency->QuadPart = 1000LL; /* 1000 ticks/sec */
}

/* Internal: find a free timer slot */
static PKTIMER AllocTimerSlot(VOID)
{
    ULONG i;
    for (i = 0; i < MAX_TIMERS; i++) {
        if (!g_Timers[i].InUse) {
            g_Timers[i].InUse = TRUE;
            g_Timers[i].Cancelled = FALSE;
            return &g_Timers[i];
        }
    }
    return NULL;
}

/* Internal: free a timer slot */
static VOID FreeTimerSlot(PKTIMER Timer)
{
    if (!Timer) return;
    Timer->InUse = FALSE;
    Timer->Cancelled = FALSE;
}
/* Convert a LARGE_INTEGER DueTime (100ns units, negative=relative ms)
 * into an absolute KeTickCount deadline. */
static ULONG64 DueTimeToTicks(LARGE_INTEGER DueTime)
{
    if (DueTime.QuadPart < 0) {
        /* Negative = relative timeout in 100ns units */
        LONGLONG relTicks = (-DueTime.QuadPart) / 10000;
        if (relTicks <= 0) relTicks = 1;
        return KeTickCount + (ULONG64)relTicks;
    } else {
        /* Positive = absolute 100ns since 1601. Convert to ticks using
         * the same formula KeQuerySystemTime uses (1ms = 1 tick). */
        return (ULONG64)(DueTime.QuadPart / 10000);
    }
}

/* Arm a one-shot timer.
 * Returns TRUE if the timer was already in the timer table. */
BOOLEAN NTAPI KeSetTimer(PKTIMER Timer, LARGE_INTEGER DueTime, PKDPC Dpc)
{
    return KeSetTimerEx(Timer, DueTime, 0, Dpc);
}

/* Arm a timer with optional periodic reload (Period in milliseconds). */
BOOLEAN NTAPI KeSetTimerEx(PKTIMER Timer, LARGE_INTEGER DueTime,
                            LONG Period, PKDPC Dpc)
{
    KIRQL Irql;
    BOOLEAN wasInserted;

    if (!Timer) return FALSE;

    /* If this is a pre-existing timer in the active list, remove it. */
    wasInserted = !IsListEmpty(&Timer->TimerListEntry);

    KeAcquireSpinLock(&g_TimerListLock, &Irql);
    if (wasInserted) {
        RemoveEntryList(&Timer->TimerListEntry);
        InitializeListHead(&Timer->TimerListEntry);
    }

    Timer->DueTime = DueTimeToTicks(DueTime);
    Timer->Period = (ULONG64)Period;  /* 0 = one-shot */
    Timer->Dpc = Dpc;
    Timer->DpcContext = Dpc ? Dpc->DeferredContext : NULL;
    Timer->Cancelled = FALSE;

    /* Insert into the global timer list (sorted by DueTime would be nice,
     * but linear insertion is fine for our small MAX_TIMERS). */
    InsertTailList(&g_TimerListHead, &Timer->TimerListEntry);

    KeReleaseSpinLock(&g_TimerListLock, Irql);

    return wasInserted;
}

/* Cancel a previously-armed timer. Returns TRUE if the timer was
 * still in the timer table (had not yet fired). */
BOOLEAN NTAPI KeCancelTimer(PKTIMER Timer)
{
    KIRQL Irql;
    BOOLEAN wasInserted;

    if (!Timer) return FALSE;

    KeAcquireSpinLock(&g_TimerListLock, &Irql);
    wasInserted = !IsListEmpty(&Timer->TimerListEntry);
    if (wasInserted) {
        RemoveEntryList(&Timer->TimerListEntry);
        InitializeListHead(&Timer->TimerListEntry);
    }
    Timer->Cancelled = TRUE;
    KeReleaseSpinLock(&g_TimerListLock, Irql);

    return wasInserted;
}

/* The timer scanning thread. Wakes every TIMER_THREAD_PERIOD_MS,
 * scans the timer list for entries whose DueTime has passed, removes
 * them, releases the spinlock, then fires their DPCs. */
static VOID NTAPI TimerScanThread(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    while (!g_TimerThreadShouldExit) {
        LIST_ENTRY firedList;
        PLIST_ENTRY pEntry;
        BOOLEAN fired = FALSE;

        InitializeListHead(&firedList);

        /* Walk the timer list under spinlock, collect expired entries. */
        {
            KIRQL Irql;
            KeAcquireSpinLock(&g_TimerListLock, &Irql);

            pEntry = g_TimerListHead.Flink;
            while (pEntry != &g_TimerListHead) {
                PKTIMER pTimer = CONTAINING_RECORD(pEntry, KTIMER, TimerListEntry);
                PLIST_ENTRY pNext = pEntry->Flink;

                if (KeTickCount >= pTimer->DueTime && !pTimer->Cancelled) {
                    /* Remove from active list and queue for firing */
                    RemoveEntryList(&pTimer->TimerListEntry);
                    InitializeListHead(&pTimer->TimerListEntry);
                    InsertTailList(&firedList, &pTimer->TimerListEntry);
                    fired = TRUE;
                }

                pEntry = pNext;
            }

            KeReleaseSpinLock(&g_TimerListLock, Irql);
        }

        /* Fire DPCs outside the spinlock. */
        while (!IsListEmpty(&firedList)) {
            PLIST_ENTRY pE = RemoveHeadList(&firedList);
            PKTIMER pTimer = CONTAINING_RECORD(pE, KTIMER, TimerListEntry);
            PKDPC pDpc = pTimer->Dpc;

            InitializeListHead(&pTimer->TimerListEntry);

            if (pDpc && pDpc->DeferredRoutine && !pTimer->Cancelled) {
                pDpc->DeferredRoutine(pDpc, pTimer->DpcContext,
                                       pDpc->SystemArgument1,
                                       pDpc->SystemArgument2);
            }

            /* Periodic timer: re-arm with a fresh DueTime */
            if (pTimer->Period > 0 && !pTimer->Cancelled) {
                LARGE_INTEGER newDue;
                newDue.QuadPart = -((LONGLONG)pTimer->Period) * 10000;  /* ms -> 100ns */
                KeSetTimerEx(pTimer, newDue, (LONG)pTimer->Period, pDpc);
            } else {
                /* One-shot: free the slot */
                FreeTimerSlot(pTimer);
            }
        }

        if (fired) {
            /* Tight loop: a timer fired, scan again immediately to
             * catch any others that may have expired in the meantime. */
            continue;
        }

        /* No timers fired - sleep for the polling period. */
        KeStallExecutionProcessor(TIMER_THREAD_PERIOD_MS * 1000);
    }

    DbgPrint("TIMER: scan thread exiting\n");
}

/* Initialize the timer subsystem and start the scan thread. */
VOID NTAPI KeTimerInitSystem(VOID)
{
    NTSTATUS Status;
    PEPROCESS Process = NULL;

    RtlZeroMemory(g_Timers, sizeof(g_Timers));
    InitializeListHead(&g_TimerListHead);
    KeInitializeSpinLock(&g_TimerListLock);

    /* Get the System process to host the timer thread. */
    Status = PsCreateSystemProcess("TimerScan", &Process);
    if (!NT_SUCCESS(Status) || !Process) {
        DbgPrint("TIMER: PsCreateSystemProcess failed 0x%08X\n", Status);
        return;
    }

    Status = PsCreateSystemThread(Process, TimerScanThread, NULL, &g_TimerThread);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("TIMER: PsCreateSystemThread failed 0x%08X\n", Status);
        return;
    }

    DbgPrint("TIMER: subsystem initialized (max %d timers)\n", MAX_TIMERS);
}
