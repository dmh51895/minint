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

/* ---- TSC-based microsecond delay (works with interrupts disabled) -------------- */

/* 
 * Approximate CPU frequency for TSC-based delays.
 * Assume 2.0 GHz as a reasonable default for modern CPUs.
 * TSC runs at CPU clock rate, so 2GHz = 2 cycles per ns = 2000 cycles per us
 */
#define TSC_CYCLES_PER_US 2000ULL

/* 
 * KeStallExecutionProcessor - Busy-wait for specified microseconds
 * Uses RDTSC (Time Stamp Counter) which works regardless of interrupt state
 */
VOID NTAPI KeStallExecutionProcessor(ULONG Microseconds)
{
    ULONG64 start, end;
    ULONG64 cycles = (ULONG64)Microseconds * TSC_CYCLES_PER_US;
    
    /* Read TSC - works even with interrupts disabled */
    __asm__ __volatile__("rdtsc" : "=A" (start));
    
    end = start + cycles;
    
    /* Busy wait using pause hint for power efficiency */
    do {
        __asm__ __volatile__("pause");
        __asm__ __volatile__("rdtsc" : "=A" (start));
    } while (start < end);
}
