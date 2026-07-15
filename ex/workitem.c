/*
 * MinNT - ex/workitem.c
 * Executive worker threads / ExQueueWorkItem.
 *
 * The Ex worker thread pool services work items queued from any
 * IRQL. Items come in two flavors:
 *   - DelayedWorkItem: must run at PASSIVE_LEVEL after a delay
 *   - CriticalWorkItem: should run as soon as a worker is free
 *
 * Implementation: a fixed pool of worker threads, each parked on a
 * semaphore. ExQueueWorkItem wakes one worker by releasing the
 * semaphore. The worker dequeues an item from the FIFO list, runs it,
 * and loops.
 */

#include <nt/ke.h>
#include <nt/ex.h>
#include <nt/rtl.h>
#include <nt/hal.h>
#include <nt/ps.h>
#include <nt/framework.h>

#define WORKITEM_MAX_THREADS 4
#define WORKITEM_QUEUE_MAX   256

/* Type defs in framework.h. */

static WORK_ITEM g_Queue[WORKITEM_QUEUE_MAX];
static KSEMAPHORE g_WorkAvailable;
static KSEMAPHORE g_WorkDone;  /* tracks shutdown completion */
static ETHREAD g_WorkerThreads[WORKITEM_MAX_THREADS];
static BOOLEAN g_WorkerStarted[WORKITEM_MAX_THREADS];
static KSPIN_LOCK g_QueueLock;
static BOOLEAN g_Init;

static WORK_ITEM *WorkItemAlloc(VOID)
{
    for (ULONG i = 0; i < WORKITEM_QUEUE_MAX; i++) {
        if (!g_Queue[i].InUse) {
            RtlZeroMemory(&g_Queue[i], sizeof(WORK_ITEM));
            g_Queue[i].InUse = TRUE;
            return &g_Queue[i];
        }
    }
    return NULL;
}

static VOID WorkItemFree(WORK_ITEM *item)
{
    if (!item) return;
    item->InUse = FALSE;
    item->Routine = NULL;
    item->Context = NULL;
}

static WORK_ITEM *WorkItemDequeue(VOID)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_QueueLock, &irql);
    /* Find a queued item that's due. */
    for (ULONG i = 0; i < WORKITEM_QUEUE_MAX; i++) {
        if (!g_Queue[i].InUse) continue;
        /* CriticalWorkItem is always ready; DelayedWorkItem checks DueTime. */
        if (g_Queue[i].Flags & 0x01 /* DelayedWorkItem */) {
            LARGE_INTEGER now;
            KeQuerySystemTime(&now);
            if (now.QuadPart < g_Queue[i].DueTime.QuadPart) continue;
        }
        WORK_ITEM *item = &g_Queue[i];
        KeReleaseSpinLock(&g_QueueLock, irql);
        return item;
    }
    KeReleaseSpinLock(&g_QueueLock, irql);
    return NULL;
}

static VOID WorkItemRun(VOID *Arg)
{
    ULONG id = (ULONG)(ULONG_PTR)Arg;
    DbgPrint("WORKITEM: worker %u started\n", id);
    for (;;) {
        KeWaitForSingleObject(&g_WorkAvailable, 0, FALSE, FALSE, NULL);
        for (;;) {
            WORK_ITEM *item = WorkItemDequeue();
            if (!item) break;
            if (item->Routine) {
                item->Routine(item->Context);
            }
            WorkItemFree(item);
        }
    }
}

NTSTATUS NTAPI ExInitializeWorkerFactory(VOID)
{
    if (g_Init) return STATUS_SUCCESS;
    KeInitializeSemaphore(&g_WorkAvailable, 0, 0x7FFFFFFF);
    KeInitializeSemaphore(&g_WorkDone, 0, 0x7FFFFFFF);
    KeInitializeSpinLock(&g_QueueLock);

    for (ULONG i = 0; i < WORKITEM_MAX_THREADS; i++) {
        NTSTATUS s = PsCreateSystemThread(PsInitialSystemProcess,
                                          WorkItemRun,
                                          (PVOID)(ULONG_PTR)i,
                                          &g_WorkerThreads[i]);
        if (NT_SUCCESS(s)) g_WorkerStarted[i] = TRUE;
    }
    g_Init = TRUE;
    DbgPrint("WORKITEM: %u worker threads started\n", WORKITEM_MAX_THREADS);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ExQueueWorkItem(PWORK_ITEM Item, WORK_QUEUE_TYPE QueueType)
{
    if (!Item || !Item->Routine) return STATUS_INVALID_PARAMETER;
    KIRQL irql;
    KeAcquireSpinLock(&g_QueueLock, &irql);
    WORK_ITEM *slot = WorkItemAlloc();
    if (!slot) {
        KeReleaseSpinLock(&g_QueueLock, irql);
        return STATUS_NO_MEMORY;
    }
    slot->Routine = Item->Routine;
    slot->Context = Item->Context;
    slot->Flags = (QueueType == DelayedWorkItem) ? 0x01 : 0x00;
    if (QueueType == DelayedWorkItem) {
        KeQuerySystemTime(&slot->DueTime);
        slot->DueTime.QuadPart += Item->DelayMs * 10000LL; /* ms -> 100ns */
    }
    KeReleaseSpinLock(&g_QueueLock, irql);
    KeReleaseSemaphore(&g_WorkAvailable, 0, 1, FALSE);
    return STATUS_SUCCESS;
}

/* Convenience: allocate-and-queue a work item in one call. */
NTSTATUS NTAPI ExInitializeWorkItem(PWORK_ITEM Item, PWORKER_THREAD_ROUTINE Routine, PVOID Context)
{
    if (!Item || !Routine) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Item, sizeof(*Item));
    Item->Routine = Routine;
    Item->Context = Context;
    return STATUS_SUCCESS;
}

ULONG NTAPI ExGetWorkerCount(VOID)
{
    return g_Init ? WORKITEM_MAX_THREADS : 0;
}

ULONG NTAPI ExGetQueueDepth(VOID)
{
    ULONG depth = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_QueueLock, &irql);
    for (ULONG i = 0; i < WORKITEM_QUEUE_MAX; i++) {
        if (g_Queue[i].InUse) depth++;
    }
    KeReleaseSpinLock(&g_QueueLock, irql);
    return depth;
}
