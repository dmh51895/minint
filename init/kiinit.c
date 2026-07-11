/*
 * MinNT - init/kiinit.c
 * KiSystemStartup -> KiInitializeKernel, phase 0 and phase 1, in the same
 * order ntoskrnl does it: Hal, Ke(GDT/IDT/PCR), Mm, Ex(pool), Ob, Ps.
 * Then adopts the boot context as the System process's first thread and
 * starts two demo kernel threads round-robining, which is the smoke test
 * that Ob + Ex + Ps + KiSwapContext all actually work together.
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/ps.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/io.h>
#include <nt/usb.h>
#include <nt/cm.h>
#include <nt/se.h>
#include <nt/pe.h>
#include <nt/fs.h>
#include <nt/lpc.h>
#include <nt/dispatcher.h>
#include "../boot/chain/chain.h"
#include <rtw/rtw_usb.h>
#include "../win32k/d3d12/d3d12.h"

extern NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);

/* The BSP's PCR lives in .data; real NT allocates per-CPU, patch point */
static KPCR KiInitialPcr;
static ETHREAD KiBootThread;            /* boot context adoption shell */

#define IA32_GS_BASE 0xC0000101U

static VOID KiSetGsBase(ULONG64 Base)
{
    __asm__ __volatile__("wrmsr" ::
        "c"(IA32_GS_BASE),
        "a"((ULONG)(Base & 0xFFFFFFFF)),
        "d"((ULONG)(Base >> 32)));
}

VOID NTAPI KiInitializeClockInterrupt(VOID);    /* ke/irql.c */

/* ---- Demo payload: proof of dispatcher objects ------------------------- */

static volatile ULONG KiDemoCounterA, KiDemoCounterB;
static KEVENT KiDemoEvent;

static VOID NTAPI KiDemoThreadA(PVOID Ctx)
{
    UNREFERENCED_PARAMETER(Ctx);
    while (KiDemoCounterA < 5) {
        DbgPrint("  [thread A] tick=%llu counter=%u\n",
                 KeTickCount, ++KiDemoCounterA);
        KeStallExecutionProcessor(200000);

        /* Signal thread B to run */
        KeSetEvent(&KiDemoEvent, 0, FALSE);

        /* Wait for thread B to signal back */
        KeWaitForSingleObject(&KiDemoEvent, Executive, KernelMode, FALSE, NULL);
    }
}

static VOID NTAPI KiDemoThreadB(PVOID Ctx)
{
    UNREFERENCED_PARAMETER(Ctx);
    while (KiDemoCounterB < 5) {
        /* Wait for thread A to signal us */
        KeWaitForSingleObject(&KiDemoEvent, Executive, KernelMode, FALSE, NULL);

        DbgPrint("  [thread B] tick=%llu counter=%u\n",
                 KeTickCount, ++KiDemoCounterB);
        KeStallExecutionProcessor(200000);

        /* Signal thread A to continue */
        KeSetEvent(&KiDemoEvent, 0, FALSE);
    }
}

/* ---- Phase 1 -------------------------------------------------------------------- */

static VOID KiInitializePhase1(VOID)
{
    PETHREAD ta, tb;
    NTSTATUS status;

    /* Initialize the demo event as auto-reset (SynchronizationEvent) */
    KeInitializeEvent(&KiDemoEvent, SynchronizationEvent, FALSE);

    status = PsCreateSystemThread(PsInitialSystemProcess,
                                  KiDemoThreadA, NULL, &ta);
    if (!NT_SUCCESS(status))
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, status, 1, 0, 0);

    status = PsCreateSystemThread(PsInitialSystemProcess,
                                  KiDemoThreadB, NULL, &tb);
    if (!NT_SUCCESS(status))
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, status, 2, 0, 0);
}

/* ---- Phase 0 + entry --------------------------------------------------------------- */

DECLSPEC_NORETURN VOID NTAPI KiSystemStartup(PVOID BootInfo)
{
    NTSTATUS status;

    /* PCR before anything can bugcheck (bugcheck reads nothing from PCR,
       but IRQL code does) */
    KiInitialPcr.Self = &KiInitialPcr;
    KiInitialPcr.Prcb = &KiInitialPcr.PrcbData;
    KiInitialPcr.Irql = HIGH_LEVEL;
    KiInitialPcr.MajorVersion = 6;
    KiInitialPcr.MinorVersion = 1;
    KiInitialPcr.PrcbData.Number = 0;
    InitializeListHead(&KiInitialPcr.PrcbData.DpcListHead);
    KiSetGsBase((ULONG64)&KiInitialPcr);

    /* HAL first so DbgPrint works for everything after */
    HalInitSystem();

    HalpVgaSetColor(0x0A);
    DbgPrint("MinNT Kernel [Version 6.1.0001] (build: barebones)\n");
    HalpVgaSetColor(0x07);
    DbgPrint("KiSystemStartup: BootInfo=%p, CPU 0\n\n", BootInfo);

    /* Ke: descriptor tables */
    KeInitializeGdt();
    KeInitializeIdt();
    DbgPrint("KE: GDT/IDT loaded, 256 vectors wired\n");

    /* Phase 0 proper runs at PASSIVE so spinlocks can raise/lower legally.
       The PIC is fully masked, so enabling IF here delivers nothing. */
    KfLowerIrql(PASSIVE_LEVEL);

    /* Mm phase 0 */
    status = MmInitSystem(BootInfo);
    if (!NT_SUCCESS(status))
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, status, 1, 0, 0);

    /* Ex: pool */
    status = ExInitializePoolManager();
    if (!NT_SUCCESS(status))
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, status, 2, 0, 0);
    DbgPrint("EX: NonPaged pool online\n");

    /* Ob */
    status = ObInitSystem();
    if (!NT_SUCCESS(status))
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, status, 3, 0, 0);
    DbgPrint("OB: object manager online\n");

    /* Io */
    status = IoInitSystem();
    if (!NT_SUCCESS(status))
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, status, 5, 0, 0);

    /* Null driver */
    status = IoInitNullDriver();
    if (!NT_SUCCESS(status))
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, status, 6, 0, 0);

    /* Cm: registry (in-memory hive) */
    status = CmInitSystem();
    if (!NT_SUCCESS(status))
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, status, 7, 0, 0);

    /* Se: security subsystem */
    status = SeInitSystem();
    if (!NT_SUCCESS(status))
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, status, 8, 0, 0);

    /* Fs: file system (RAM disk) */
    status = FsInitSystem();
    if (!NT_SUCCESS(status))
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, status, 9, 0, 0);

    /* Create RAM disk (16MB) and mount FAT16 */
    status = FsCreateRamDisk(16);
    if (NT_SUCCESS(status))
        FsMountFat16();

    /* Lpc: local procedure call */
    status = LpcInitSystem();
    if (!NT_SUCCESS(status))
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, status, 10, 0, 0);

    /* USB */
    status = UsbInitSystem();
    if (NT_SUCCESS(status))
        DbgPrint("USB: subsystem ready\n");
    else
        DbgPrint("USB: no controller found (non-fatal)\n");

    /* Framebuffer + Keyboard */
    MB2_FRAMEBUFFER_INFO FbInfo;
    status = HalpParseMb2Framebuffer(BootInfo, &FbInfo);
    if (NT_SUCCESS(status) && FbInfo.Valid) {
        HalpFbInit(FbInfo.Address, FbInfo.Width, FbInfo.Height, FbInfo.Pitch, FbInfo.Bpp);
    } else {
        DbgPrint("FB: no framebuffer available (text mode only)\n");
    }
    HalpKbdInit();
    HalpMouseInit();

    /* Ps: types + System process */
    status = PsInitSystem();
    if (!NT_SUCCESS(status))
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, status, 4, 0, 0);

    /* Adopt the boot context as the System process's thread 0 so the
       dispatcher has a CurrentThread to swap away from */
    RtlZeroMemory(&KiBootThread, sizeof(KiBootThread));
    KiBootThread.Tcb.Process     = &PsInitialSystemProcess->Pcb;
    KiBootThread.Tcb.State       = Running;
    KiBootThread.Tcb.KernelStack = NULL;      /* static stack, never freed */
    KiBootThread.Tcb.Quantum     = 3;
    KiBootThread.ThreadProcess   = PsInitialSystemProcess;
    KiBootThread.UniqueThreadId  = (HANDLE)4;
    KeGetCurrentPrcb()->CurrentThread = &KiBootThread.Tcb;

    /* Clock + drop to PASSIVE: the machine is alive */
    KiInitializeClockInterrupt();
    KfLowerIrql(PASSIVE_LEVEL);
    DbgPrint("KE: clock interrupt at 100Hz, IRQL PASSIVE_LEVEL\n\n");

    /* RTW88 WiFi — after clock so KeStallExecutionProcessor works */
    status = RtwInitSystem();

    /* win32k — after framebuffer so GUI stack is available */
    status = DriverEntry(NULL, NULL);
    if (!NT_SUCCESS(status))
        DbgPrint("INIT: win32k DriverEntry failed: 0x%08lx\n", (ULONG)status);

    /* Initialize syscall MSRs for user mode */
    KiInitializeSyscall();

    /* Launch boot chain: SMSS → CSRSS → Winlogon → Explorer */
    status = BootChainInit();
    if (!NT_SUCCESS(status))
        DbgPrint("INIT: boot chain failed to start: 0x%08lx\n", (ULONG)status);

    /* Phase 1: system threads (demo threads for smoke test) */
    KiInitializePhase1();

    DbgPrint("PS: dispatching...\n");
    while (KiDemoCounterA < 5 || KiDemoCounterB < 5)
        KiDispatchNextThread();

    HalpVgaSetColor(0x0B);
    DbgPrint("\nMinNT: phase 1 complete. %llu MB free, tick=%llu.\n",
             (MmGetFreePages() * 4096) >> 20, KeTickCount);
    DbgPrint("Boot thread idling. Patch targets: see ARCHITECTURE.md\n");
    HalpVgaSetColor(0x07);

    /* Idle loop: this becomes the idle thread when preemption lands */
    for (;;) {
        KiDispatchNextThread();
        __asm__ __volatile__("hlt");
    }
}
