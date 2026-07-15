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
#include <nt/recycle.h>
#include <nt/netconn.h>
#include <nt/comreg.h>
#include <nt/framework.h>
#include <nt/lpc.h>
#include <nt/dispatcher.h>
#include <nt/apic.h>
#include <nt/smp.h>
#include "../boot/chain/chain.h"
#include <rtw/rtw_usb.h>
#include "../win32k/d3d12/d3d12.h"
#include <nt/exe.h>
#include <nt/ahci.h>
#include <nt/fat32.h>
#include <nt/partition.h>
#include <nt/cache.h>
#include <nt/gpu.h>

extern NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);

PVOID KiMultibootInfo;  /* exposed for MmInitSystemWrap */

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
static KEVENT KiDemoEventA;  /* signaled by B when B is done */
static KEVENT KiDemoEventB;  /* signaled by A when A is done */

static VOID NTAPI KiDemoThreadA(PVOID Ctx)
{
    UNREFERENCED_PARAMETER(Ctx);
    while (KiDemoCounterA < 5) {
        DbgPrint("  [thread A] tick=%llu counter=%u\n",
                 KeTickCount, ++KiDemoCounterA);
        KeStallExecutionProcessor(200000);

        /* Signal thread B: A has completed an iteration */
        KeSetEvent(&KiDemoEventB, 0, FALSE);

        /* Wait for thread B to complete its iteration */
        KeWaitForSingleObject(&KiDemoEventA, Executive, KernelMode, FALSE, NULL);
    }
}

static VOID NTAPI KiDemoThreadB(PVOID Ctx)
{
    UNREFERENCED_PARAMETER(Ctx);
    while (KiDemoCounterB < 5) {
        /* Wait for thread A to complete an iteration */
        KeWaitForSingleObject(&KiDemoEventB, Executive, KernelMode, FALSE, NULL);

        DbgPrint("  [thread B] tick=%llu counter=%u\n",
                 KeTickCount, ++KiDemoCounterB);
        KeStallExecutionProcessor(200000);

        /* Signal thread A: B has completed */
        KeSetEvent(&KiDemoEventA, 0, FALSE);
    }
}

/* ---- Phase 1 -------------------------------------------------------------------- */

static VOID KiInitializePhase1(VOID)
{
    PETHREAD ta, tb;
    NTSTATUS status;

    /* Initialize two auto-reset events for proper ping-pong synchronization */
    KeInitializeEvent(&KiDemoEventA, SynchronizationEvent, FALSE);
    KeInitializeEvent(&KiDemoEventB, SynchronizationEvent, FALSE);

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

    KiMultibootInfo = BootInfo;

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

    /* Parse multiboot2 command line for boot flags (/install, /safemode, etc.) */
    BootArgsParse(BootInfo);
    if (BootArgsIsInstall()) {
        DbgPrint("BOOT: /install flag detected - booting into installer mode\n");
    } else if (BootArgsIsSafeMode()) {
        DbgPrint("BOOT: /safemode flag detected - booting into safe mode\n");
    } else if (BootArgsIsTerminal()) {
        DbgPrint("BOOT: /terminal flag detected - booting into terminal-only mode\n");
    } else if (BootArgsIsRecovery()) {
        DbgPrint("BOOT: /recovery flag detected - booting into recovery console\n");
    }

    /* Resolve the active boot profile based on the boot flags. The
     * profile decides which subsystems we initialize below; subsystems
     * that aren't in the profile are skipped entirely, so the
     * installer boot path doesn't pay for win32k, network, shell, etc. */
    BootProfileInit();
    BootProfileResolve();
    if (BootProfileGetActive() == BootProfileInstall) {
        DbgPrint("BOOT: install profile active - skipping non-essential subsystems\n");
    }

    /* Ke: descriptor tables */
    KeInitializeGdt();
    KeInitializeIdt();
    DbgPrint("KE: GDT/IDT loaded, 256 vectors wired\n");

    /* Initialize Local APIC for this processor (BSP) - REAL HARDWARE */
    {
        extern VOID NTAPI KeInitializeLapic(VOID);
        extern BOOLEAN NTAPI KeDetectLapic(VOID);
        extern ULONG NTAPI KeGetLapicVersion(VOID);
        extern VOID NTAPI KeInitializeLapicTimer(VOID);
        
        if (KeDetectLapic()) {
            KeInitializeLapic();
            DbgPrint("KE: Local APIC initialized, version=%x\n", KeGetLapicVersion());
            /* Initialize LAPIC timer (replaces PIT when APIC is active) */
            KeInitializeLapicTimer();
        } else {
            DbgPrint("KE: No Local APIC detected (legacy mode)\n");
        }
    }

    /* Phase 0 proper runs at PASSIVE so spinlocks can raise/lower legally.
       The PIC is fully masked, so enabling IF here delivers nothing. */
    KfLowerIrql(PASSIVE_LEVEL);

    /* Phase 0 proper runs at PASSIVE so spinlocks can raise/lower legally.
       The PIC is fully masked, so enabling IF here delivers nothing. */
    KfLowerIrql(PASSIVE_LEVEL);

    /* Subsystem registry-driven init. Each subsystem declares its
     * name, init function, dependencies, and phase. The dispatcher
     * topologically sorts by dependencies, gates by the active boot
     * profile, and runs each subsystem in order. Adding a new
     * subsystem no longer requires editing this function - just call
     * BootRegisterSubsystemEx() somewhere in module init. */
    status = BootRegistryInit();
    if (!NT_SUCCESS(status))
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, status, 1, 0, 0);

    /* Framebuffer + Keyboard - must be ready before any profile check
     * branches off, so the installer TUI can render. These are HAL
     * primitives, not profile-gated subsystems. */
    if (BootProfileAllowsSubsystem("Framebuffer")) {
        MB2_FRAMEBUFFER_INFO FbInfo;
        status = HalpParseMb2Framebuffer(BootInfo, &FbInfo);
        if (NT_SUCCESS(status) && FbInfo.Valid) {
            HalpFbInit(FbInfo.Address, FbInfo.Width, FbInfo.Height, FbInfo.Pitch, FbInfo.Bpp);
            extern VOID NTAPI HalpFbConsoleInit(ULONG Width, ULONG Height);
            HalpFbConsoleInit(FbInfo.Width, FbInfo.Height);
        } else {
            DbgPrint("FB: no framebuffer available (falling back to VGA text mode)\n");
            HalpVgaInit();
        }
    } else {
        HalpVgaInit();
    }
    if (BootProfileAllowsSubsystem("Keyboard")) HalpKbdInit();
    if (BootProfileAllowsSubsystem("Mouse")) HalpMouseInit();

    /* Ke: timer/DPC subsystem. PsInitSystem already ran from the
     * registry above so we can set up the boot thread. */
    KeTimerInitSystem();

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

    /* Clock + drop to PASSIVE: the machine is alive
     * NOTE: LAPIC timer initialized in APIC setup above, not PIT */
    KfLowerIrql(PASSIVE_LEVEL);
    DbgPrint("KE: clock interrupt at 100Hz (LAPIC timer), IRQL PASSIVE_LEVEL\n\n");

    /* Initialize SMP - detect and start AP processors on multi-core systems */
    {
        extern NTSTATUS NTAPI KeSmpInitSystem(VOID);
        extern ULONG KeNumberProcessors;
        extern KAFFINITY KeActiveProcessors;
        
        status = KeSmpInitSystem();
        if (NT_SUCCESS(status)) {
            DbgPrint("SMP: %u processor(s) detected, active mask=0x%llx\n", 
                     KeNumberProcessors, KeActiveProcessors);
        } else {
            DbgPrint("SMP: running in single-core mode\n");
        }
    }

    /* RTW88 WiFi, OS Installer, ScmAutoStart, win32k, etc. are all
     * initialized by the subsystem registry. No profile checks
     * sprinkled here. */

    /* Initialize syscall MSRs for user mode (only if user-mode will run) */
    if (BootProfileAllowsSubsystem("BootChain")) {
        KiInitializeSyscall();
        ExeInitRegistry();
        DbgPrint("INIT: DLL export registry ready\n");
    }

    /* Branch on boot profile. The Install profile runs the installer
     * TUI instead of the boot chain; the Recovery profile drops into
     * a recovery console; everything else launches the normal boot
     * chain (SMSS -> CSRSS -> Winlogon -> Explorer). */
    if (BootProfileGetActive() == BootProfileInstall) {
        DbgPrint("INIT: launching installer TUI\n");
        OsInstallRunTUI();
        /* If we get here, the installer completed. Reboot and let
         * the user boot the installed system normally. */
        DbgPrint("INIT: installer done, halting\n");
        for (;;) { __asm__ __volatile__("hlt"); }
    } else if (BootProfileGetActive() == BootProfileRecovery) {
        DbgPrint("INIT: launching recovery console\n");
        for (;;) { __asm__ __volatile__("hlt"); }
    } else if (BootProfileGetActive() == BootProfileTerminal) {
        DbgPrint("INIT: terminal mode - skipping GUI\n");
        for (;;) { __asm__ __volatile__("hlt"); }
    }

    /* The boot chain (SMSS → CSRSS → Winlogon → Explorer) was launched
     * by the subsystem registry. Wait for its threads to settle. */

    /* Phase 1: system threads (demo threads for smoke test) */
    KiInitializePhase1();

    DbgPrint("PS: dispatching...\n");

    /* Run boot chain threads until they settle (SMSS->CSRSS->Winlogon->Explorer) */
    /* This gives each thread a chance to run and initialize */
    for (ULONG i = 0; i < 5000; i++) {
        KiDispatchNextThread();
    }

    DbgPrint("PS: dispatch loop complete. tick=%llu\n", KeTickCount);

    HalpVgaSetColor(0x0B);
    DbgPrint("\nMinNT: kernel idle. %llu MB free, tick=%llu.\n",
             (MmGetFreePages() * 4096) >> 20, KeTickCount);
    DbgPrint("Boot thread idling. See ARCHITECTURE.md for info.\n");
    HalpVgaSetColor(0x07);

    /* Idle loop: this becomes the idle thread when preemption lands */
    for (;;) {
        KiDispatchNextThread();
        __asm__ __volatile__("hlt");
    }
}
