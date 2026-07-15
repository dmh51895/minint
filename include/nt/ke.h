/*
 * MinNT - ke.h
 * Kernel core (Ke): processor control region, PRCB, IRQL, interrupts,
 * spinlocks, bugcheck. This is the ntoskrnl "ke" layer.
 */

#ifndef _KE_H_
#define _KE_H_

#include <nt/ntdef.h>
#include <nt/dispatcher.h>

/* ---- Trap frame (pushed by KiTrapEntry stubs, see ke/trap.S) ----------- */

typedef struct _KTRAP_FRAME {
    ULONG64 R15, R14, R13, R12, R11, R10, R9, R8;
    ULONG64 Rdi, Rsi, Rbp, Rbx, Rdx, Rcx, Rax;
    ULONG64 Vector;        /* interrupt vector number     */
    ULONG64 ErrorCode;     /* CPU error code or 0         */
    ULONG64 Rip;           /* hardware-pushed frame below */
    ULONG64 SegCs;
    ULONG64 EFlags;
    ULONG64 Rsp;
    ULONG64 SegSs;
} KTRAP_FRAME, *PKTRAP_FRAME;

/* ---- Forward decls ------------------------------------------------------ */

struct _KTHREAD;
struct _KPROCESS;

/* ---- KPRCB: per-processor control block --------------------------------- */

typedef struct _KPRCB {
    ULONG   Number;                     /* processor index               */
    struct _KTHREAD *CurrentThread;
    struct _KTHREAD *IdleThread;
    ULONG64 InterruptCount;
    ULONG64 DpcQueueDepth;
    LIST_ENTRY DpcListHead;
    KSPIN_LOCK DpcLock;
    LONG    QuantumEnd;                 /* non-zero when quantum expired */
} KPRCB, *PKPRCB;

/* ---- KPCR: processor control region, anchored at GS:[0] ----------------- */

typedef struct _KPCR {
    struct _KPCR *Self;                 /* GS:[0x00] self-pointer, NT-style */
    PKPRCB  Prcb;                       /* GS:[0x08]                        */
    KIRQL   Irql;                       /* GS:[0x10] current IRQL           */
    ULONG64 UserRsp;                    /* GS:[0x18] saved user RSP         */
    ULONG64 KernelStackTop;             /* GS:[0x20] kernel stack top       */
    ULONG   MajorVersion;               /* GS:[0x28] 6                      */
    ULONG   MinorVersion;               /* GS:[0x2C] 1                      */
    KPRCB   PrcbData;                   /* embedded PRCB for BSP            */
} KPCR, *PKPCR;

FORCEINLINE PKPCR KeGetPcr(VOID)
{
    PKPCR Pcr;
    __asm__ __volatile__("movq %%gs:0, %0" : "=r"(Pcr));
    return Pcr;
}

FORCEINLINE PKPRCB KeGetCurrentPrcb(VOID)
{
    return KeGetPcr()->Prcb;
}

FORCEINLINE KIRQL KeGetCurrentIrql(VOID)
{
    return KeGetPcr()->Irql;
}

/* ---- IRQL manipulation -------------------------------------------------- */

KIRQL NTAPI KfRaiseIrql(KIRQL NewIrql);
VOID  NTAPI KfLowerIrql(KIRQL NewIrql);

#define KeRaiseIrql(NewIrql, OldIrql) (*(OldIrql) = KfRaiseIrql(NewIrql))
#define KeLowerIrql(NewIrql)          KfLowerIrql(NewIrql)

/* ---- Spinlocks ----------------------------------------------------------
 * Real MP semantics on the lock word; IRQL discipline enforced like NT:
 * KeAcquireSpinLock raises to DISPATCH_LEVEL.
 */
VOID  NTAPI KeInitializeSpinLock(PKSPIN_LOCK SpinLock);
KIRQL NTAPI KeAcquireSpinLockRaiseToDpc(PKSPIN_LOCK SpinLock);
VOID  NTAPI KeReleaseSpinLock(PKSPIN_LOCK SpinLock, KIRQL NewIrql);
#define KeAcquireSpinLock(Lock, OldIrql) \
    (*(OldIrql) = KeAcquireSpinLockRaiseToDpc(Lock))

/* ---- Interrupt plumbing -------------------------------------------------- */

typedef VOID (NTAPI *PKINTERRUPT_ROUTINE)(PKTRAP_FRAME TrapFrame);

VOID NTAPI KeInitializeIdt(VOID);
VOID NTAPI KeInitializeGdt(VOID);
VOID NTAPI KeConnectInterrupt(ULONG Vector, PKINTERRUPT_ROUTINE Routine);
VOID NTAPI KiDispatchInterrupt(PKTRAP_FRAME TrapFrame);   /* from trap.S */

FORCEINLINE VOID KeEnableInterrupts(VOID)  { __asm__ __volatile__("sti"); }
FORCEINLINE VOID KeDisableInterrupts(VOID) { __asm__ __volatile__("cli"); }
FORCEINLINE VOID KeHaltProcessor(VOID)     { __asm__ __volatile__("hlt"); }

/* ---- Bugcheck ------------------------------------------------------------ */

DECLSPEC_NORETURN VOID NTAPI
KeBugCheckEx(ULONG BugCheckCode,
             ULONG_PTR P1, ULONG_PTR P2, ULONG_PTR P3, ULONG_PTR P4);

#define KeBugCheck(Code) KeBugCheckEx((Code), 0, 0, 0, 0)

/* ---- Tick count / timekeeping ------------------------------------------- */

extern volatile ULONG64 KeTickCount;           /* incremented by PIT ISR */
VOID NTAPI KeStallExecutionProcessor(ULONG Microseconds);
VOID NTAPI KeQuerySystemTime(PLARGE_INTEGER SystemTime);
VOID NTAPI KeQueryTickCount(PULONG64 TickCount);
VOID NTAPI KeQueryPerformanceCounter(PLARGE_INTEGER Counter, PLARGE_INTEGER Frequency);

/* ---- Timers and DPCs ---------------------------------------------------- */

/* KDPC is defined in dispatcher.h - use it from there */

typedef struct _KTIMER {
    LIST_ENTRY TimerListEntry;
    ULONG64 DueTime;              /* absolute KeTickCount value when it fires */
    ULONG64 Period;               /* periodic interval (0 = one-shot) */
    PKDPC Dpc;                    /* DPC to invoke on expiration */
    PVOID DpcContext;             /* context passed to DPC */
    BOOLEAN InUse;
    BOOLEAN Cancelled;
} KTIMER, *PKTIMER;

VOID NTAPI KeInitializeTimer(PKTIMER Timer);
VOID NTAPI KeInitializeDpc(PKDPC Dpc, KDPC_Routine Routine, PVOID Context);
BOOLEAN NTAPI KeSetTimer(PKTIMER Timer, LARGE_INTEGER DueTime, PKDPC Dpc);
BOOLEAN NTAPI KeSetTimerEx(PKTIMER Timer, LARGE_INTEGER DueTime,
                            LONG Period, PKDPC Dpc);
BOOLEAN NTAPI KeCancelTimer(PKTIMER Timer);
VOID NTAPI KeInitializeTimerEx(PKTIMER Timer, LONG Type);
VOID NTAPI KeTimerInitSystem(VOID);

/* ---- ASSERT -------------------------------------------------------------- */

#define ASSERT(exp) \
    do { if (!(exp)) KeBugCheckEx(MANUALLY_INITIATED_CRASH, \
        (ULONG_PTR)__FILE__, __LINE__, 0, 0); } while (0)

/* ---- Startup ------------------------------------------------------------- */

DECLSPEC_NORETURN VOID NTAPI KiSystemStartup(PVOID BootInfo);

/* ---- User mode / syscall ------------------------------------------------- */

VOID NTAPI KiInitializeSyscall(VOID);
VOID NTAPI KiSetTssRsp0(ULONG64 Rsp0);
NTSTATUS NTAPI KiSystemServiceHandler(ULONG SyscallNumber, PKTRAP_FRAME TrapFrame);

#endif /* _KE_H_ */
