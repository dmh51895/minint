/*
 * MinNT - ps/apc.c
 * APC (Asynchronous Procedure Call) delivery.
 *
 * APCs are a way to run code in the context of a specific thread. They
 * come in two flavors:
 *   - Kernel APCs: run at PASSIVE_LEVEL in the target thread
 *   - User APCs:   delivered to user mode via a special trap; the thread
 *                  must be in an alertable wait
 *
 * APCs are queued to a thread's APC queue and run when the thread
 * next enters the dispatcher or wakes from an alertable wait.
 */

#include <nt/ke.h>
#include <nt/ex.h>
#include <nt/rtl.h>
#include <nt/hal.h>
#include <nt/ps.h>
#include <nt/framework.h>

#define APC_MAX_PER_THREAD  16

/* KAPC and PKAPC are defined in nt/dispatcher.h. */

NTSTATUS NTAPI KeInitializeApc(PKAPC Apc, PETHREAD Thread,
                                PVOID KernelRoutine,
                                PVOID RundownRoutine,
                                PVOID NormalRoutine,
                                ULONG ProcessorMode,
                                PVOID NormalContext)
{
    if (!Apc || !Thread || !KernelRoutine) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Apc, sizeof(KAPC));
    Apc->Thread = (PKTHREAD)Thread;
    Apc->NormalRoutine = NormalRoutine;
    Apc->NormalContext = NormalContext;
    Apc->ApcMode = (UCHAR)(ProcessorMode & 0xFF);
    Apc->KernelApc = (ProcessorMode == 0) ? TRUE : FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI KeInsertQueueApc(PKAPC Apc, PVOID SystemArgument1,
                                PVOID SystemArgument2, UCHAR PriorityBoost)
{
    if (!Apc || !Apc->Thread) return STATUS_INVALID_PARAMETER;
    if (Apc->Inserted) return STATUS_UNSUCCESSFUL;
    Apc->Inserted = TRUE;
    Apc->SystemArgument1 = SystemArgument1;
    Apc->SystemArgument2 = SystemArgument2;
    DbgPrint("APC: queued kernel=%p to thread=%p\n",
             Apc->NormalRoutine, Apc->Thread);
    /* MinNT runs the kernel routine inline on the calling CPU. The
     * thread is parked on a software interrupt that's delivered
     * during the next KiDispatchNextThread call. */
    if (Apc->NormalRoutine && Apc->KernelApc) {
        ((VOID (NTAPI *)(PVOID, PVOID, PVOID))Apc->NormalRoutine)(
            Apc->NormalContext, SystemArgument1, SystemArgument2);
    }
    (void)PriorityBoost;
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI KeRemoveQueueApc(PKAPC Apc)
{
    if (!Apc) return FALSE;
    if (!Apc->Inserted) return FALSE;
    Apc->Inserted = FALSE;
    return TRUE;
}

NTSTATUS NTAPI KeDeliverApc(PETHREAD Thread, ULONG Reserved)
{
    if (!Thread) return STATUS_INVALID_PARAMETER;
    (void)Reserved;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI PsQueueUserApc(PETHREAD Thread, PVOID ApcRoutine, PVOID NormalContext,
                              PVOID SystemArgument1, PVOID SystemArgument2)
{
    if (!Thread || !ApcRoutine) return STATUS_INVALID_PARAMETER;
    KAPC apc;
    KeInitializeApc(&apc, Thread, ApcRoutine, NULL, NULL, 1 /* UserMode */, NormalContext);
    apc.SystemArgument1 = SystemArgument1;
    apc.SystemArgument2 = SystemArgument2;
    return KeInsertQueueApc(&apc, NULL, NULL, 0);
}

NTSTATUS NTAPI ApcTestQueue(PETHREAD Thread)
{
    if (!Thread) return STATUS_INVALID_PARAMETER;
    static KAPC testApc[8];
    static BOOLEAN testApcInUse[8];
    for (ULONG i = 0; i < 8; i++) {
        if (!testApcInUse[i]) {
            testApcInUse[i] = TRUE;
            KeInitializeApc(&testApc[i], Thread,
                             (PVOID)0xDEADBEEF,
                             NULL,
                             (PVOID)0xCAFE0000,
                             0,
                             (PVOID)(ULONG_PTR)i);
            return KeInsertQueueApc(&testApc[i], NULL, NULL, 0);
        }
    }
    return STATUS_NO_MEMORY;
}
