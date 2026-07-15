/*
 * MinNT - win32k/queue.c
 * Per-thread message queue management for Win32k.
 */

#include "precomp.h"

#define MAX_THREAD_QUEUES 32
#define QUEUE_SIZE 128

/* WakeMask QS_* flags (match Win32 values) */
#define QS_KEY            0x0001
#define QS_MOUSEMOVE      0x0002
#define QS_MOUSEBUTTON    0x0004
#define QS_MOUSE          (QS_MOUSEMOVE | QS_MOUSEBUTTON)
#define QS_POSTMESSAGE    0x0008
#define QS_TIMER          0x0010
#define QS_PAINT          0x0020
#define QS_SENDMESSAGE    0x0040
#define QS_HOTKEY         0x0080
#define QS_ALLINPUT       0x04FF

/* Wait result codes returned through *pResult */
#define WAIT_OBJECT_0     0
#define WAIT_TIMEOUT      0x00000102L
#define WAIT_FAILED       0xFFFFFFFF

/* MsgWaitForMultipleObjects specials */
#define MWMO_INFINITE     0xFFFFFFFF
#define MWMO_POLL_MS      10

typedef struct _THREAD_MSG_QUEUE {
    ULONG     ThreadId;
    W32K_MSG  Messages[QUEUE_SIZE];
    ULONG     Head;
    ULONG     Tail;
    ULONG     Count;
    KSPIN_LOCK Lock;
    KEVENT    Event;
    BOOLEAN   InUse;
} THREAD_MSG_QUEUE;

static THREAD_MSG_QUEUE g_ThreadQueues[MAX_THREAD_QUEUES];

NTSTATUS NTAPI QueueInit(VOID)
{
    ULONG i;
    RtlZeroMemory(g_ThreadQueues, sizeof(g_ThreadQueues));
    for (i = 0; i < MAX_THREAD_QUEUES; i++) {
        KeInitializeSpinLock(&g_ThreadQueues[i].Lock);
        KeInitializeEvent(&g_ThreadQueues[i].Event, SynchronizationEvent, FALSE);
    }
    DbgPrint("QUEUE: initialized (%d thread queues)\n", MAX_THREAD_QUEUES);
    return STATUS_SUCCESS;
}

static THREAD_MSG_QUEUE *QueueFindOrCreate(ULONG ThreadId, BOOLEAN Create)
{
    ULONG i, freeSlot = MAX_THREAD_QUEUES;

    for (i = 0; i < MAX_THREAD_QUEUES; i++) {
        if (g_ThreadQueues[i].InUse && g_ThreadQueues[i].ThreadId == ThreadId)
            return &g_ThreadQueues[i];
        if (!g_ThreadQueues[i].InUse && freeSlot == MAX_THREAD_QUEUES)
            freeSlot = i;
    }

    if (Create && freeSlot < MAX_THREAD_QUEUES) {
        RtlZeroMemory(&g_ThreadQueues[freeSlot].Messages,
                      sizeof(g_ThreadQueues[freeSlot].Messages));
        g_ThreadQueues[freeSlot].Head = 0;
        g_ThreadQueues[freeSlot].Tail = 0;
        g_ThreadQueues[freeSlot].Count = 0;
        g_ThreadQueues[freeSlot].ThreadId = ThreadId;
        g_ThreadQueues[freeSlot].InUse = TRUE;
        return &g_ThreadQueues[freeSlot];
    }
    return NULL;
}

NTSTATUS NTAPI UserPostThreadMessage(ULONG ThreadId, ULONG Msg,
                                      ULONG_PTR wParam, LONG_PTR lParam)
{
    THREAD_MSG_QUEUE *q;
    KIRQL Irql;

    q = QueueFindOrCreate(ThreadId, TRUE);
    if (!q) return STATUS_INSUFFICIENT_RESOURCES;

    KeAcquireSpinLock(&q->Lock, &Irql);
    if (q->Count >= QUEUE_SIZE) {
        KeReleaseSpinLock(&q->Lock, Irql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    q->Messages[q->Tail].hwnd = 0;
    q->Messages[q->Tail].message = Msg;
    q->Messages[q->Tail].wParam = wParam;
    q->Messages[q->Tail].lParam = lParam;
    q->Messages[q->Tail].time = (ULONG)KeTickCount;
    q->Tail = (q->Tail + 1) % QUEUE_SIZE;
    q->Count++;

    KeReleaseSpinLock(&q->Lock, Irql);
    KeSetEvent(&q->Event, 0, FALSE);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetMessageForThread(ULONG ThreadId, PW32K_MSG Msg,
                                        ULONG MsgFilterMin, ULONG MsgFilterMax)
{
    THREAD_MSG_QUEUE *q;
    KIRQL Irql;
    ULONG i, idx;

    if (!Msg) return STATUS_INVALID_PARAMETER;

    q = QueueFindOrCreate(ThreadId, FALSE);
    if (!q) return STATUS_NOT_FOUND;

    for (;;) {
        KeAcquireSpinLock(&q->Lock, &Irql);
        for (i = 0; i < q->Count; i++) {
            idx = (q->Head + i) % QUEUE_SIZE;
            ULONG m = q->Messages[idx].message;
            if (MsgFilterMin == 0 && MsgFilterMax == 0) {
                /* No filter */
            } else if (m < MsgFilterMin || m > MsgFilterMax) {
                continue;
            }
            *Msg = q->Messages[idx];
            /* Remove by shifting */
            { ULONG j, dst, src;
              for (j = i; j > 0; j--) {
                  dst = (q->Head + j) % QUEUE_SIZE;
                  src = (q->Head + j - 1) % QUEUE_SIZE;
                  q->Messages[dst] = q->Messages[src];
              }
            }
            q->Head = (q->Head + 1) % QUEUE_SIZE;
            q->Count--;
            KeReleaseSpinLock(&q->Lock, Irql);
            return STATUS_SUCCESS;
        }
        KeReleaseSpinLock(&q->Lock, Irql);
        KeWaitForSingleObject(&q->Event, Executive, KernelMode, FALSE, NULL);
    }
}

NTSTATUS NTAPI UserDestroyThreadQueue(ULONG ThreadId)
{
    THREAD_MSG_QUEUE *q;
    q = QueueFindOrCreate(ThreadId, FALSE);
    if (q) {
        q->InUse = FALSE;
        q->Count = 0;
        DbgPrint("QUEUE: Destroyed queue for tid=%u\n", ThreadId);
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetQueueStatus(ULONG QSFlags, PULONG pStatus)
{
    ULONG total = 0;
    ULONG_PTR myTid = PsGetCurrentThreadId();
    THREAD_MSG_QUEUE *q;

    if (!pStatus) return STATUS_INVALID_PARAMETER;

    q = QueueFindOrCreate(myTid, FALSE);
    if (q) {
        total = q->Count;
    }
    *pStatus = (total > 0) ? (QSFlags & 0xFFFF) : 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserMsgWaitForMultipleObjects(ULONG Count, PULONG Handles,
                                               BOOL fWaitAll, ULONG Timeout,
                                               ULONG WakeMask, PULONG pResult)
{
    THREAD_MSG_QUEUE *q;
    ULONG_PTR myTid;
    BOOLEAN infinite;
    BOOLEAN wakeOnInput;
    BOOLEAN allSignaled;
    ULONG64 deadline = 0;
    ULONG i, queueCount;
    KIRQL Irql;
    KEVENT pollTimer;
    LARGE_INTEGER zeroTimeout;
    PVOID waitObj;
    NTSTATUS s;

    if (!pResult) return STATUS_INVALID_PARAMETER;
    if (Count > 0 && !Handles) return STATUS_INVALID_PARAMETER;

    myTid = (ULONG_PTR)PsGetCurrentThreadId();
    q = QueueFindOrCreate((ULONG)myTid, TRUE);
    if (!q) {
        *pResult = WAIT_FAILED;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    wakeOnInput = (WakeMask & (QS_POSTMESSAGE | QS_ALLINPUT)) != 0;

    if (Timeout == MWMO_INFINITE) {
        infinite = TRUE;
    } else {
        infinite = FALSE;
        deadline = KeTickCount + (ULONG64)Timeout;
    }

    KeInitializeEvent(&pollTimer, NotificationEvent, FALSE);
    zeroTimeout.QuadPart = 0;

    for (;;) {
        /* (a) Input: does the thread queue hold a posted message we want? */
        KeAcquireSpinLock(&q->Lock, &Irql);
        queueCount = q->Count;
        KeReleaseSpinLock(&q->Lock, Irql);
        if (queueCount > 0 && wakeOnInput) {
            *pResult = WAIT_OBJECT_0 + Count;
            return STATUS_SUCCESS;
        }

        /* (b) Handles: poll each one with a zero (non-blocking) timeout. */
        if (Count > 0) {
            if (fWaitAll) {
                allSignaled = TRUE;
                for (i = 0; i < Count; i++) {
                    s = KeWaitForSingleObject((PVOID)(ULONG_PTR)Handles[i],
                                              Executive, KernelMode,
                                              FALSE, &zeroTimeout);
                    if (s != STATUS_WAIT_0) {
                        allSignaled = FALSE;
                        break;
                    }
                }
                if (allSignaled) {
                    *pResult = WAIT_OBJECT_0;
                    return STATUS_SUCCESS;
                }
            } else {
                for (i = 0; i < Count; i++) {
                    s = KeWaitForSingleObject((PVOID)(ULONG_PTR)Handles[i],
                                              Executive, KernelMode,
                                              FALSE, &zeroTimeout);
                    if (s == STATUS_WAIT_0) {
                        *pResult = WAIT_OBJECT_0 + i;
                        return STATUS_SUCCESS;
                    }
                }
            }
        }

        /* (c) Timeout expired? */
        if (!infinite && (KeTickCount >= deadline)) {
            *pResult = WAIT_TIMEOUT;
            return STATUS_TIMEOUT;
        }

        /* Sleep until input arrives or the (capped) poll interval elapses. */
        {
            LARGE_INTEGER delay;
            LONGLONG pollMs = MWMO_POLL_MS;

            if (!infinite) {
                LONGLONG remaining = (LONGLONG)(deadline - KeTickCount);
                if (remaining <= 0) {
                    *pResult = WAIT_TIMEOUT;
                    return STATUS_TIMEOUT;
                }
                if (remaining < pollMs) pollMs = remaining;
            }
            delay.QuadPart = -(pollMs * 10000LL);

            /* Prefer the thread queue's KEVENT so we wake promptly on input. */
            waitObj = wakeOnInput ? (PVOID)&q->Event : (PVOID)&pollTimer;
            KeWaitForSingleObject(waitObj, Executive, KernelMode, FALSE, &delay);
        }
    }
}
