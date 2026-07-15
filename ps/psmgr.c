/*
 * MinNT - ps/psmgr.c
 * Process structure: registers Process/Thread object types with Ob,
 * creates the System process (PID 4, obviously), spins up kernel threads
 * with pre-seeded stacks for KiSwapContext, and runs a cooperative
 * round-robin dispatcher. Preemption is David's patch: hook the clock
 * ISR to set a quantum-end flag and yield on IRQL drop.
 */

#include <nt/ps.h>
#include <nt/ob.h>
#include <nt/ex.h>
#include <nt/mm.h>
#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/hal.h>

LIST_ENTRY   PsActiveProcessHead;
PEPROCESS    PsInitialSystemProcess;
POBJECT_TYPE PsProcessType;
POBJECT_TYPE PsThreadType;

static ULONG_PTR PspNextPid = 4;        /* System is 4, everything after +4 */
static ULONG_PTR PspNextTid = 8;

/* ---- Dispatcher state ---------------------------------------------------- */

LIST_ENTRY KiReadyListHead;
KSPIN_LOCK KiDispatcherLock;

extern VOID KiSwapContext(ULONG_PTR *OldRsp, ULONG_PTR NewRsp);  /* ctxswap.S */

static VOID KiReleaseDispatcherLockNoYield(PKSPIN_LOCK Lock, KIRQL OldIrql)
{
    PKPCR Pcr = KeGetPcr();

    __atomic_clear((volatile char *)Lock, __ATOMIC_RELEASE);
    Pcr->Irql = OldIrql;
    if (OldIrql < DISPATCH_LEVEL)
        KeEnableInterrupts();
}

VOID NTAPI KiInitializeScheduler(VOID)
{
    InitializeListHead(&KiReadyListHead);
    KeInitializeSpinLock(&KiDispatcherLock);
}

VOID NTAPI KiReadyThread(PKTHREAD Thread)
{
    KIRQL irql;
    KeAcquireSpinLock(&KiDispatcherLock, &irql);
    Thread->State = Ready;
    InsertTailList(&KiReadyListHead, &Thread->WaitListEntry);
    KiReleaseDispatcherLockNoYield(&KiDispatcherLock, irql);
}

VOID NTAPI KiDispatchNextThread(VOID)
{
dispatch_loop:
    KIRQL irql;
    PKTHREAD Old, New;
    PKPRCB Prcb = KeGetCurrentPrcb();
    PETHREAD OldThread;
    PETHREAD NewThread;

    KeAcquireSpinLock(&KiDispatcherLock, &irql);
    if (IsListEmpty(&KiReadyListHead)) {
        KeReleaseSpinLock(&KiDispatcherLock, &irql);
        /* Nothing to run - HLT to wait for next interrupt */
        __asm__ __volatile__("hlt");
        /* Loop back to re-acquire and re-check */
        goto dispatch_loop;
    }

    Old = Prcb->CurrentThread;
    New = CONTAINING_RECORD(RemoveHeadList(&KiReadyListHead),
                            KTHREAD, WaitListEntry);
    New->State = Running;
    OldThread = Old ? CONTAINING_RECORD(Old, ETHREAD, Tcb) : NULL;
    NewThread = CONTAINING_RECORD(New, ETHREAD, Tcb);

    if (Old && Old->State == Running) {
        Old->State = Ready;
        InsertTailList(&KiReadyListHead, &Old->WaitListEntry);
    }
    Prcb->CurrentThread = New;
    KeReleaseSpinLock(&KiDispatcherLock, irql);

    /* Update TSS.RSP0 for ring 3→0 transitions (new thread's kernel stack) */
    if (New->KernelStack)
        KiSetTssRsp0((ULONG64)New->KernelStack);

    if (Old)
        KiSwapContext(&Old->StackPointer, New->StackPointer);
    /* first dispatch (no Old) is handled by KiThreadStartup path */
}

/* ---- Thread trampoline ------------------------------------------------------ */

static VOID NTAPI KiThreadStartup(VOID)
{
    PETHREAD Thread = (PETHREAD)KeGetCurrentThread();

    KfLowerIrql(PASSIVE_LEVEL);
    Thread->StartRoutine(Thread->StartContext);

    /* Thread returned: terminate and yield forever */
    Thread->Tcb.State = Terminated;
    DbgPrint("PS: thread %p (TID %llu) terminated\n",
             (PVOID)Thread, (ULONG64)(ULONG_PTR)Thread->UniqueThreadId);
    for (;;) KiDispatchNextThread();
}

/* ---- Object delete procedures ----------------------------------------------- */

static VOID NTAPI PspProcessDelete(PVOID Body)
{
    UNREFERENCED_PARAMETER(Body);
}

static VOID NTAPI PspThreadDelete(PVOID Body)
{
    PETHREAD t = Body;
    if (t->Tcb.KernelStack)
        ExFreePoolWithTag((PUCHAR)t->Tcb.KernelStack - 0x4000, TAG_THRD);
}

/* ---- Init --------------------------------------------------------------------- */

static const WCHAR PspProcessTypeName[] = u"Process";
static const WCHAR PspThreadTypeName[]  = u"Thread";

NTSTATUS NTAPI PsInitSystem(VOID)
{
    static const UNICODE_STRING ProcName = RTL_CONSTANT_STRING(PspProcessTypeName);
    static const UNICODE_STRING ThrdName = RTL_CONSTANT_STRING(PspThreadTypeName);
    NTSTATUS status;

    InitializeListHead(&PsActiveProcessHead);
    KiInitializeScheduler();

    status = ObCreateObjectType(&ProcName, TAG_PROC, PspProcessDelete,
                                &PsProcessType);
    if (!NT_SUCCESS(status)) return status;

    status = ObCreateObjectType(&ThrdName, TAG_THRD, PspThreadDelete,
                                &PsThreadType);
    if (!NT_SUCCESS(status)) return status;

    /* The System process, PID 4 */
    status = PsCreateSystemProcess("System", &PsInitialSystemProcess);
    return status;
}

NTSTATUS NTAPI PsCreateSystemProcess(const CHAR *ImageName, PEPROCESS *OutProcess)
{
    NTSTATUS status;
    PEPROCESS proc;
    SIZE_T i;
    ULONG64 cr3;

    status = ObCreateObject(PsProcessType, sizeof(EPROCESS), NULL,
                            (PVOID *)&proc);
    if (!NT_SUCCESS(status)) return status;

    __asm__ __volatile__("movq %%cr3, %0" : "=r"(cr3));
    proc->Pcb.DirectoryTableBase = cr3;    /* kernel address space for now */
    proc->Pcb.BasePriority = 8;
    proc->Pcb.ActiveThreads = 0;
    InitializeListHead(&proc->Pcb.ThreadListHead);
    InitializeListHead(&proc->ThreadListHead);

    proc->UniqueProcessId = (HANDLE)PspNextPid;
    PspNextPid += 4;

    for (i = 0; i < sizeof(proc->ImageFileName) - 1 && ImageName[i]; i++)
        proc->ImageFileName[i] = ImageName[i];
    proc->ImageFileName[i] = 0;

    InsertTailList(&PsActiveProcessHead, &proc->ActiveProcessLinks);

    DbgPrint("PS: created process '%s' PID %llu\n",
             proc->ImageFileName, (ULONG64)(ULONG_PTR)proc->UniqueProcessId);
    *OutProcess = proc;
    return STATUS_SUCCESS;
}

#define KERNEL_STACK_SIZE 0x4000        /* 16K */

NTSTATUS NTAPI PsCreateSystemThread(PEPROCESS Process,
                                    VOID (NTAPI *StartRoutine)(PVOID),
                                    PVOID StartContext,
                                    PETHREAD *OutThread)
{
    NTSTATUS status;
    PETHREAD thread;
    PUCHAR stackBase;
    ULONG_PTR *sp;

    status = ObCreateObject(PsThreadType, sizeof(ETHREAD), NULL,
                            (PVOID *)&thread);
    if (!NT_SUCCESS(status)) return status;

    stackBase = ExAllocatePoolWithTag(NonPagedPool, KERNEL_STACK_SIZE, TAG_THRD);
    if (!stackBase) {
        ObDereferenceObject(thread);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    thread->Tcb.Process     = &Process->Pcb;
    thread->Tcb.KernelStack = stackBase + KERNEL_STACK_SIZE;
    thread->Tcb.Priority    = Process->Pcb.BasePriority;
    thread->Tcb.State       = Initialized;
    thread->Tcb.Quantum     = 3;  /* ~30ms at 100Hz */
    thread->ThreadProcess   = Process;
    thread->StartRoutine    = StartRoutine;
    thread->StartContext    = StartContext;
    thread->UniqueThreadId  = (HANDLE)PspNextTid;
    PspNextTid += 4;

    /* Seed the stack so KiSwapContext's pop/ret lands in KiThreadStartup:
       layout must mirror ctxswap.S: r15 r14 r13 r12 rbx rbp rflags ret */
    sp = (ULONG_PTR *)((ULONG_PTR)thread->Tcb.KernelStack & ~0xFULL);
    *--sp = (ULONG_PTR)KiThreadStartup;   /* ret target   */
    *--sp = 0x202;                        /* rflags (IF)  */
    *--sp = 0;                            /* rbp          */
    *--sp = 0;                            /* rbx          */
    *--sp = 0;                            /* r12          */
    *--sp = 0;                            /* r13          */
    *--sp = 0;                            /* r14          */
    *--sp = 0;                            /* r15          */
    thread->Tcb.StackPointer = (ULONG_PTR)sp;

    InsertTailList(&Process->Pcb.ThreadListHead, &thread->Tcb.ThreadListEntry);
    InsertTailList(&Process->ThreadListHead, &thread->ThreadListEntry);
    Process->Pcb.ActiveThreads++;

    KiReadyThread(&thread->Tcb);

    DbgPrint("PS: created thread TID %llu in '%s'\n",
             (ULONG64)(ULONG_PTR)thread->UniqueThreadId,
             Process->ImageFileName);
    *OutThread = thread;
    return STATUS_SUCCESS;
}

/* ---- User thread creation ----------------------------------------------- */

extern VOID KiEnterUserMode(VOID);   /* from syscall.S — iretq to ring 3 */

NTSTATUS NTAPI PsCreateUserThread(PEPROCESS Process,
                                   PVOID UserEntryPoint,
                                   PVOID UserStackBase,
                                   ULONG64 UserStackSize,
                                   PETHREAD *OutThread)
{
    NTSTATUS status;
    PETHREAD thread;
    PUCHAR stackBase;
    ULONG_PTR *sp;

    status = ObCreateObject(PsThreadType, sizeof(ETHREAD), NULL,
                            (PVOID *)&thread);
    if (!NT_SUCCESS(status)) return status;

    stackBase = ExAllocatePoolWithTag(NonPagedPool, KERNEL_STACK_SIZE, TAG_THRD);
    if (!stackBase) {
        ObDereferenceObject(thread);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    thread->Tcb.Process      = &Process->Pcb;
    thread->Tcb.KernelStack  = stackBase + KERNEL_STACK_SIZE;
    thread->Tcb.Priority     = Process->Pcb.BasePriority;
    thread->Tcb.State        = Initialized;
    thread->Tcb.Quantum      = 3;
    thread->ThreadProcess    = Process;
    thread->UniqueThreadId   = (HANDLE)PspNextTid;
    PspNextTid += 4;

    /* Seed the kernel stack for KiSwapContext → KiEnterUserMode → IRETQ.
       KiSwapContext pops 7 values (r15-r12, rbx, rbp, rflags) then rets.
       After ret, we land in KiEnterUserMode which pops the return address
       and does IRETQ to ring 3. */
    sp = (ULONG_PTR *)((ULONG_PTR)thread->Tcb.KernelStack & ~0xFULL);

    /* IRETQ frame (popped by IRETQ): SS, RSP, RFLAGS, CS, RIP */
    *--sp = (ULONG_PTR)(0x20 | 3);            /* SS = ring3 data */
    *--sp = (ULONG_PTR)(UserStackBase + UserStackSize - 8); /* user RSP */
    *--sp = 0x202;                             /* RFLAGS (IF set) */
    *--sp = (ULONG_PTR)(0x18 | 3);             /* CS = ring3 code */
    *--sp = (ULONG_PTR)UserEntryPoint;          /* RIP */

    /* Return address for KiSwapContext's ret — lands in KiEnterUserMode */
    *--sp = (ULONG_PTR)KiEnterUserMode;

    /* Callee-saved registers popped by KiSwapContext (in reverse order) */
    *--sp = 0;  /* r15 */
    *--sp = 0;  /* r14 */
    *--sp = 0;  /* r13 */
    *--sp = 0;  /* r12 */
    *--sp = 0;  /* rbx */
    *--sp = 0;  /* rbp */
    *--sp = 0x202;  /* rflags (IF set — popped by popfq) */

    thread->Tcb.StackPointer = (ULONG_PTR)sp;

    InsertTailList(&Process->Pcb.ThreadListHead, &thread->Tcb.ThreadListEntry);
    InsertTailList(&Process->ThreadListHead, &thread->ThreadListEntry);
    Process->Pcb.ActiveThreads++;

    KiReadyThread(&thread->Tcb);

    DbgPrint("PS: created user thread TID %llu entry=%p stack=%p\n",
             (ULONG64)(ULONG_PTR)thread->UniqueThreadId,
             UserEntryPoint, UserStackBase);
    *OutThread = thread;
    return STATUS_SUCCESS;
}

/* ── Current thread/process accessors ───────────────────────────────────── */

PKTHREAD NTAPI KeGetCurrentThread(VOID)
{
    return KeGetCurrentPrcb()->CurrentThread;
}

HANDLE NTAPI PsGetCurrentProcessId(VOID)
{
    return PsGetCurrentProcess()->UniqueProcessId;
}

PETHREAD NTAPI PsGetCurrentThread(VOID)
{
    return (PETHREAD)KeGetCurrentThread();
}