/*
 * MinNT - ke/irql.c
 * Software IRQL model over a PIC (no LAPIC TPR yet): raising to
 * DISPATCH_LEVEL or above masks interrupts, lowering below restores.
 * Bugchecks IRQL_NOT_LESS_OR_EQUAL on bad transitions like real NT.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/ps.h>

volatile ULONG64 KeTickCount;

/* Default quantum: 3 ticks (~30ms at 100Hz) */
#define DEFAULT_QUANTUM 3

KIRQL NTAPI KfRaiseIrql(KIRQL NewIrql)
{
    PKPCR Pcr = KeGetPcr();
    KIRQL Old = Pcr->Irql;

    if (NewIrql < Old)
        KeBugCheckEx(IRQL_NOT_LESS_OR_EQUAL, NewIrql, Old, 0, 1);

    Pcr->Irql = NewIrql;
    if (NewIrql >= DISPATCH_LEVEL)
        KeDisableInterrupts();
    return Old;
}

VOID NTAPI KfLowerIrql(KIRQL NewIrql)
{
    PKPCR Pcr = KeGetPcr();

    if (NewIrql > Pcr->Irql)
        KeBugCheckEx(IRQL_NOT_LESS_OR_EQUAL, NewIrql, Pcr->Irql, 0, 2);

    Pcr->Irql = NewIrql;
    if (NewIrql < DISPATCH_LEVEL)
        KeEnableInterrupts();
}

/* ---- Spinlocks ---------------------------------------------------------------- */

VOID NTAPI KeInitializeSpinLock(PKSPIN_LOCK SpinLock)
{
    *SpinLock = 0;
}

KIRQL NTAPI KeAcquireSpinLockRaiseToDpc(PKSPIN_LOCK SpinLock)
{
    KIRQL Old = KfRaiseIrql(DISPATCH_LEVEL);
    while (__atomic_test_and_set((volatile char *)SpinLock, __ATOMIC_ACQUIRE))
        __asm__ __volatile__("pause");
    return Old;
}

VOID NTAPI KeReleaseSpinLock(PKSPIN_LOCK SpinLock, KIRQL NewIrql)
{
    __atomic_clear((volatile char *)SpinLock, __ATOMIC_RELEASE);
    KfLowerIrql(NewIrql);
}

/* ---- Clock tick ----------------------------------------------------------------- */

/* Default quantum: 3 ticks (~30ms at 100Hz) */
#define DEFAULT_QUANTUM 3

static VOID NTAPI KiClockInterrupt(PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    PKPRCB Prcb = KeGetCurrentPrcb();

    KeTickCount++;
    Prcb->InterruptCount++;

    /* Decrement current thread's quantum */
    if (Prcb->CurrentThread)
    {
        if (Prcb->CurrentThread->Quantum > 0)
        {
            Prcb->CurrentThread->Quantum--;
        }

        /* Cooperative scheduling only for now; no quantum preemption. */
    }

    HalEndOfInterrupt(0);
}

VOID NTAPI KiInitializeClockInterrupt(VOID)
{
    KeConnectInterrupt(PIC_IRQ_BASE + 0, KiClockInterrupt);
    HalEnableSystemInterrupt(0);
}

/* ---- Crude calibrated-ish stall (busy loop on the PIT tick) ---------------------- */

VOID NTAPI KeStallExecutionProcessor(ULONG Microseconds)
{
    ULONG64 ticks = (Microseconds + 9999) / 10000;   /* 100Hz -> 10ms/tick */
    ULONG64 end = KeTickCount + (ticks ? ticks : 1);
    while (KeTickCount < end)
        __asm__ __volatile__("pause");
}
