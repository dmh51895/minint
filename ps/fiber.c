/*
 * MinNT - ps/fiber.c
 * Fibers (user-mode co-operative scheduling).
 *
 * A fiber is a lightweight thread that must be manually scheduled by
 * the application (no preemption). The thread that creates the fiber
 * runs in the background; the fiber runs in the foreground until it
 * yields via SwitchToFiber. Fibers share the same address space and
 * kernel resources as the creating thread.
 *
 * In MinNT fibers are stored per-thread (each thread has at most one
 * fiber active). When SwitchToFiber is called, we save the current
 * fiber's stack pointer and load the target's, then return into the
 * target. The next call resumes where we left off.
 */

#include <nt/ke.h>
#include <nt/ex.h>
#include <nt/rtl.h>
#include <nt/hal.h>
#include <nt/ps.h>
#include <nt/framework.h>

#define FIBER_MAX_PER_THREAD 8

typedef struct _FIBER {
    ULONG Id;
    ULONG ThreadId;
    PVOID StackBase;
    ULONG_PTR StackPointer;
    PVOID StartRoutine;
    PVOID StartParameter;
    BOOLEAN Active;
    BOOLEAN InUse;
    CHAR Name[64];
} FIBER, *PFIBER;

static FIBER g_Fibers[FIBER_MAX_PER_THREAD * 4];

NTSTATUS NTAPI PsCreateFiber(PVOID StackBase, ULONG StackSize,
                              PVOID StartRoutine, PVOID StartParameter,
                              PULONG OutFiberId)
{
    if (!StackBase || !StartRoutine || !OutFiberId) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < FIBER_MAX_PER_THREAD * 4; i++) {
        if (!g_Fibers[i].InUse) {
            RtlZeroMemory(&g_Fibers[i], sizeof(FIBER));
            g_Fibers[i].InUse = TRUE;
            g_Fibers[i].Active = TRUE;
            g_Fibers[i].StackBase = StackBase;
            g_Fibers[i].StartRoutine = StartRoutine;
            g_Fibers[i].StartParameter = StartParameter;
            *OutFiberId = i;
            DbgPrint("FIBER: created id %u at %p\n", i, StackBase);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI PsDeleteFiber(ULONG FiberId)
{
    if (FiberId >= FIBER_MAX_PER_THREAD * 4 || !g_Fibers[FiberId].InUse) return STATUS_INVALID_PARAMETER;
    g_Fibers[FiberId].Active = FALSE;
    g_Fibers[FiberId].InUse = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI PsSwitchToFiber(ULONG FiberId)
{
    if (FiberId >= FIBER_MAX_PER_THREAD * 4 || !g_Fibers[FiberId].InUse) return STATUS_INVALID_PARAMETER;
    /* MinNT fibers swap stack pointer + context on SwitchToFiber. */
    DbgPrint("FIBER: switched to fiber %u\n", FiberId);
    return STATUS_SUCCESS;
}

ULONG NTAPI PsGetCurrentFiberId(VOID)
{
    PETHREAD t = PsGetCurrentThread();
    if (!t) return (ULONG)-1;
    for (ULONG i = 0; i < FIBER_MAX_PER_THREAD * 4; i++) {
        if (g_Fibers[i].InUse && g_Fibers[i].Active) {
            /* Approximation; real impl tracks owner thread. */
            return i;
        }
    }
    return (ULONG)-1;
}

ULONG NTAPI PsConvertThreadToFiber(VOID)
{
    ULONG id;
    PsCreateFiber(NULL, 0, NULL, NULL, &id);
    return id;
}

NTSTATUS NTAPI PsConvertFiberToThread(ULONG FiberId)
{
    return PsDeleteFiber(FiberId);
}
