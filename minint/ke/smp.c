/*
 * MinNT - ke/smp.c
 * Symmetric Multi-Processing (SMP) support for x86-64.
 * 
 * This module implements:
 *   - MP table / ACPI MADT parsing for CPU detection
 *   - AP (Application Processor) startup via INIT-SIPI-SIPI
 *   - Per-CPU KPCR initialization
 *   - IPI (Inter-Processor Interrupt) coordination
 *   - TLB shootdown
 *   - CPU state tracking
 * 
 * Reference: Linux arch/x86/kernel/smpboot.c, smpboot.c
 */

#include <nt/ke.h>
#include <nt/smp.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ps.h>
#include <nt/rtl.h>
#include <nt/apic.h>

/* Type definitions */
#ifndef CONST
#define CONST const
#endif

/* ---- Forward declarations ---------------------------------------------- */

PKIPCR KeAllocateKpcr(ULONG CpuNumber);
VOID NTAPI DbgPrint(CONST CHAR *Format, ...);

VOID NTAPI KeAcquireSpinLockAtDpcLevel(PKSPIN_LOCK SpinLock);
VOID NTAPI KeReleaseSpinLockFromDpcLevel(PKSPIN_LOCK SpinLock);

/* Inline implementations for missing functions */
FORCEINLINE VOID KeSpinWaitPause(VOID)
{
    __asm__ __volatile__("pause" ::: "memory");
}

FORCEINLINE VOID KeMemoryBarrier(VOID)
{
    __asm__ __volatile__("mfence" ::: "memory");
}

FORCEINLINE LONG InterlockedExchange(volatile LONG *Target, LONG Value)
{
    LONG OldValue;
    __asm__ __volatile__("xchg %0, %1"
                        : "=r"(OldValue), "+m"(*Target)
                        : "0"(Value)
                        : "memory");
    return OldValue;
}

FORCEINLINE LONG InterlockedIncrement(volatile LONG *Value)
{
    LONG Result;
    __asm__ __volatile__("lock; incl %0"
                        : "+m"(*Value), "=a"(Result)
                        :
                        : "memory");
    return Result + 1;
}

FORCEINLINE LONG InterlockedDecrement(volatile LONG *Value)
{
    LONG Result;
    __asm__ __volatile__("lock; decl %0"
                        : "+m"(*Value), "=a"(Result)
                        :
                        : "memory");
    return Result - 1;
}

/* Cast helper for KPCR to KIPCR */
#define KeGetIpicr() ((PKIPCR)KeGetPcr())

/* ---- External Assembly Trampoline Symbols -------------------------------- */

extern VOID KiApTrampoline16(VOID);
extern UCHAR KiApTrampolineEnd[];
extern ULONG ApPml4Base[];
extern ULONG64 ApStackBase[];
extern ULONG64 ApKpcrBase[];
extern ULONG ApCpuNumber[];
extern ULONG ApApicId[];
extern ULONG ApStartupState[];
extern ULONG ApErrorCode[];
extern UCHAR ApGdt[];
extern UCHAR ApGdtDesc[];

/* ---- Global SMP State ------------------------------------------------------ */

/* Processor count and affinity masks */
ULONG KeNumberProcessors = 1;
ULONG KeNumberNodes = 1;
KAFFINITY KeActiveProcessors = { 1 };  /* CPU 0 always active */
KAFFINITY KeOnlineProcessors = { 1 };  /* CPU 0 always online */

/* Per-processor data blocks */
PKIPCR KeProcessorBlock[MAXIMUM_PROCESSORS];
KCPU_STATE KeProcessorState[MAXIMUM_PROCESSORS];
ULONG KeProcessorApicId[MAXIMUM_PROCESSORS];

/* BSP information */
ULONG KeBootProcessorNumber = 0;
ULONG KeBootProcessorApicId = 0;

/* AP startup context (low memory trampoline) */
PAP_STARTUP_CONTEXT KeApStartupContext = NULL;
ULONG KeApStartupPhysical = 0;

/* SMP initialized flag */
static BOOLEAN SmpInitialized = FALSE;
static KSPIN_LOCK SmpLock;

/* AP startup synchronization */
static volatile LONG ApStartupComplete = 0;
static volatile LONG ApStartupError = 0;

/* ---- APIC Access ----------------------------------------------------------- */

/* Local APIC base address */
static PVOID LocalApicBase = NULL;

/*
 * ApicRead - Read from local APIC register
 */
ULONG NTAPI ApicRead(ULONG Register)
{
    if (!LocalApicBase)
        return 0;
    return *(volatile ULONG *)((PUCHAR)LocalApicBase + Register);
}

/*
 * ApicWrite - Write to local APIC register
 */
VOID NTAPI ApicWrite(ULONG Register, ULONG Value)
{
    if (!LocalApicBase)
        return;
    *(volatile ULONG *)((PUCHAR)LocalApicBase + Register) = Value;
}

/*
 * ApicGetId - Get current CPU's APIC ID
 */
ULONG NTAPI ApicGetId(VOID)
{
    if (!LocalApicBase)
        return 0;
    return (ApicRead(APIC_ID) >> 24) & 0xFF;
}

/*
 * ApicSendEoi - Send End-of-Interrupt
 */
VOID NTAPI ApicSendEoi(VOID)
{
    if (LocalApicBase)
        ApicWrite(APIC_EOI, 0);
}

/*
 * ApicWaitForIcr - Wait for ICR to become idle
 */
VOID NTAPI ApicWaitForIcr(VOID)
{
    ULONG Timeout = 10000;
    while ((ApicRead(APIC_ICR_LOW) & APIC_ICR_BUSY) && Timeout--)
        KeSpinWaitPause();
}

/*
 * ApicIsPresent - Check if local APIC is present and enabled
 */
BOOLEAN NTAPI ApicIsPresent(VOID)
{
    /* Check CPUID for APIC support */
    ULONG Eax, Ebx, Ecx, Edx;
    __asm__ __volatile__("cpuid" : "=a"(Eax), "=b"(Ebx), "=c"(Ecx), "=d"(Edx) : "a"(1));
    if (!(Edx & (1 << 9)))
        return FALSE;
    
    /* Check if APIC is enabled via MSR */
    ULONG Low, High;
    __asm__ __volatile__("rdmsr" : "=a"(Low), "=d"(High) : "c"(APIC_BASE_MSR));
    if (!(Low & APIC_BASE_MSR_ENABLE))
        return FALSE;
    
    return TRUE;
}

/*
 * ApicSendIpi - Send IPI to target processor(s)
 */
VOID NTAPI ApicSendIpi(ULONG Vector, ULONG TargetApicId)
{
    if (!LocalApicBase)
        return;
    
    /* Wait for ICR to become idle */
    ApicWaitForIcr();
    
    /* Set destination in high ICR */
    ApicWrite(APIC_ICR_HIGH, TargetApicId << 24);
    
    /* Send the IPI */
    ApicWrite(APIC_ICR_LOW, Vector | APIC_DM_FIXED);
}

/*
 * ApicSendIpiToAll - Broadcast IPI to all processors
 */
VOID NTAPI ApicSendIpiToAll(ULONG Vector, BOOLEAN IncludeSelf)
{
    if (!LocalApicBase)
        return;
    
    /* Wait for ICR to become idle */
    ApicWaitForIcr();
    
    /* Broadcast IPI */
    ULONG DeliveryMode = APIC_DM_FIXED | Vector;
    if (!IncludeSelf)
        DeliveryMode |= (1 << 19);  /* Destination shorthand: all-but-self */
    
    ApicWrite(APIC_ICR_LOW, DeliveryMode);
}

/* ---- MP Configuration Table Detection ------------------------------------ */

#pragma pack(push, 1)

/* MP structures are defined in nt/smp.h */

#pragma pack(pop)

#define EBDA_START          0x00080000
#define EBDA_END            0x0009FFFF
#define BIOS_ROM_START      0x000F0000
#define BIOS_ROM_END        0x000FFFFF

/*
 * KeDetectMpConfiguration - Detect MP configuration via MP table
 * 
 * Scans for Intel MP configuration table to enumerate processors.
 * Returns status indicating success or failure to find MP table.
 */
NTSTATUS NTAPI KeDetectMpConfiguration(VOID)
{
    PMP_FLOATING_POINTER MpFp = NULL;
    ULONG i;
    
    /* Search for MP floating pointer in EBDA and BIOS ROM */
    for (i = EBDA_START; i <= EBDA_END; i += 16) {
        if (*(PULONG)i == MPFP_SIGNATURE) {
            MpFp = (PMP_FLOATING_POINTER)i;
            break;
        }
    }
    
    if (!MpFp) {
        for (i = BIOS_ROM_START; i <= BIOS_ROM_END; i += 16) {
            if (*(PULONG)i == MPFP_SIGNATURE) {
                MpFp = (PMP_FLOATING_POINTER)i;
                break;
            }
        }
    }
    
    if (!MpFp) {
        DbgPrint("SMP: No MP floating pointer found\n");
        return STATUS_SMP_NO_MP_TABLE;
    }
    
    DbgPrint("SMP: MP floating pointer found at %p, spec rev %d\n",
             MpFp, MpFp->SpecRev);
    
    /* Check if default configuration (no config table) */
    if ((MpFp->MpFeatures[0] & 0x80) == 0) {
        /* Default configuration - assume dual processor */
        KeNumberProcessors = 2;
        KeActiveProcessors = 0x3;
        
        /* Set APIC IDs for default configuration */
        KeProcessorApicId[0] = 0;
        KeProcessorApicId[1] = 1;
        
        /* BSP is CPU 0 */
        KeBootProcessorNumber = 0;
        KeBootProcessorApicId = ApicGetId();
        
        DbgPrint("SMP: Using default MP configuration, %u processors\n",
                 KeNumberProcessors);
        return STATUS_SUCCESS;
    }
    
    /* Parse MP configuration table */
    PMP_CONFIGURATION_TABLE MpConfig = (PMP_CONFIGURATION_TABLE)
                                       (ULONG_PTR)MpFp->MpConfigTable;
    
    if (!MpConfig || MpConfig->Signature != MPC_SIGNATURE) {
        DbgPrint("SMP: Invalid MP configuration table\n");
        return STATUS_SMP_NO_MP_TABLE;
    }
    
    DbgPrint("SMP: MP config table found at %p, %u entries\n",
             MpConfig, MpConfig->EntryCount);
    
    /* Initialize processor count */
    KeNumberProcessors = 0;
    KeActiveProcessors = 0;
    
    /* Get BSP APIC ID */
    KeBootProcessorApicId = ApicGetId();
    
    /* Enumerate entries */
    PUCHAR EntryPtr = (PUCHAR)(MpConfig + 1);
    for (i = 0; i < MpConfig->EntryCount; i++) {
        UCHAR EntryType = *EntryPtr;
        
        if (EntryType == MP_PROCESSOR) {
            PMP_PROCESSOR_ENTRY CpuEntry = (PMP_PROCESSOR_ENTRY)EntryPtr;
            
            if (KeNumberProcessors < MAXIMUM_PROCESSORS) {
                KeProcessorApicId[KeNumberProcessors] = CpuEntry->LocalApicId;
                
                /* Check if this is the BSP */
                if (CpuEntry->CpuFlags & CPU_BOOTPROCESSOR) {
                    KeBootProcessorNumber = KeNumberProcessors;
                }
                
                /* Mark as active if enabled */
                if (CpuEntry->CpuFlags & CPU_ENABLED) {
                    KeActiveProcessors |= (1ULL << KeNumberProcessors);
                }
                
                KeNumberProcessors++;
            }
            
            EntryPtr += sizeof(MP_PROCESSOR_ENTRY);
        } else if (EntryType == MP_BUS) {
            EntryPtr += 8;
        } else if (EntryType == MP_IOAPIC) {
            EntryPtr += 8;
        } else if (EntryType == MP_IOINTERRUPT || EntryType == MP_LOCAL_INTERRUPT) {
            EntryPtr += 8;
        }
    }
    
    if (KeNumberProcessors == 0) {
        KeNumberProcessors = 1;
        KeActiveProcessors = 1;
        KeBootProcessorNumber = 0;
    }
    
    DbgPrint("SMP: Detected %u processors, BSP is CPU %u (APIC %u)\n",
             KeNumberProcessors, KeBootProcessorNumber, KeBootProcessorApicId);
    
    return STATUS_SUCCESS;
}

/*
 * KeIsMpSystem - Check if running on an MP system
 */
BOOLEAN NTAPI KeIsMpSystem(VOID)
{
    return (KeNumberProcessors > 1);
}

/*
 * KeGetProcessorCount - Get total number of processors
 */
ULONG NTAPI KeGetProcessorCount(VOID)
{
    return KeNumberProcessors;
}

/*
 * KeGetCurrentProcessorNumber - Get current processor number
 */
ULONG NTAPI KeGetCurrentProcessorNumber(VOID)
{
    PKPCR Pcr = KeGetPcr();
    return Pcr->Prcb->Number;
}

/* ---- AP Startup Trampoline Code ------------------------------------------ */

/*
 * KeAllocateApTrampoline - Allocate and setup AP trampoline in low memory
 */
NTSTATUS KeAllocateApTrampoline(VOID)
{
    PHYSICAL_ADDRESS TrampPhys;
    PVOID TrampVirt;
    ULONG TrampSize;
    ULONG64 BspPml4;
    PKPCR BspPcr;
    
    /* Calculate trampoline size */
    TrampSize = (ULONG)((ULONG_PTR)KiApTrampolineEnd - (ULONG_PTR)KiApTrampoline16);
    if (TrampSize > 0x1000) {
        DbgPrint("SMP: Trampoline too large: %u bytes\n", TrampSize);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    /* Allocate one page below 1MB for trampoline (at 0x90000 typically) */
    /* For now, use a fixed location in low memory */
    TrampPhys = 0x90000;
    
    /* Map it into kernel space - assume identity mapping for low memory */
    /* In MinNT, kernel VAs == PAs for low memory */
    TrampVirt = (PVOID)(ULONG_PTR)TrampPhys;
    
    KeApStartupPhysical = TrampPhys;
    
    DbgPrint("SMP: AP trampoline at PA 0x%lx, VA %p, size %u\n",
             (ULONG)TrampPhys, TrampVirt, TrampSize);
    
    /* Clear trampoline area */
    RtlZeroMemory(TrampVirt, 0x1000);
    
    /* Copy trampoline code to low memory */
    RtlCopyMemory(TrampVirt, KiApTrampoline16, TrampSize);
    
    /* Get BSP's PML4 for AP to use */
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(BspPml4));
    
    /* Get BSP's KPCR */
    BspPcr = KeGetPcr();
    
    /* Calculate offsets within trampoline */
    ULONG_PTR TrampBase = (ULONG_PTR)KiApTrampoline16;
    ULONG_PTR DestBase = (ULONG_PTR)TrampVirt;
    
    /* Update data in the copied trampoline */
    /* PML4 base */
    PULONG Pml4Ptr = (PULONG)(DestBase + ((ULONG_PTR)ApPml4Base - TrampBase));
    *Pml4Ptr = (ULONG)(BspPml4 & 0xFFFFFFFF);
    
    /* GDT descriptor base needs to be updated to physical address */
    /* ApGdtDesc is at a known offset, needs to point to ApGdt in physical memory */
    PUCHAR GdtDescPtr = (PUCHAR)(DestBase + ((ULONG_PTR)ApGdtDesc - TrampBase));
    /* GDT descriptor: limit (2 bytes) + base (4 bytes) */
    *(PULONG)(GdtDescPtr + 2) = (ULONG)(TrampPhys + ((ULONG_PTR)ApGdt - TrampBase));
    
    DbgPrint("SMP: AP trampoline setup complete\n");
    DbgPrint("SMP:   PML4 = 0x%llx\n", BspPml4);
    DbgPrint("SMP:   GDT at physical 0x%lx\n", 
             (ULONG)(TrampPhys + ((ULONG_PTR)ApGdt - TrampBase)));
    
    return STATUS_SUCCESS;
}

/*
 * KeSetupApTrampolineContext - Setup trampoline context for specific AP
 */
VOID KeSetupApTrampolineContext(ULONG CpuNumber, ULONG ApicId, 
                                        PVOID StackBase, PVOID KpcrBase)
{
    ULONG_PTR TrampBase = (ULONG_PTR)KeApStartupPhysical;
    ULONG_PTR TrampVirt = (ULONG_PTR)KeApStartupPhysical;
    ULONG_PTR SourceBase = (ULONG_PTR)KiApTrampoline16;
    
    /* Set CPU number */
    PULONG CpuNumPtr = (PULONG)(TrampVirt + ((ULONG_PTR)ApCpuNumber - SourceBase));
    *CpuNumPtr = CpuNumber;
    
    /* Set APIC ID */
    PULONG ApicIdPtr = (PULONG)(TrampVirt + ((ULONG_PTR)ApApicId - SourceBase));
    *ApicIdPtr = ApicId;
    
    /* Set stack base (top of stack) */
    PULONG64 StackPtr = (PULONG64)(TrampVirt + ((ULONG_PTR)ApStackBase - SourceBase));
    *StackPtr = (ULONG64)StackBase;
    
    /* Set KPCR base */
    PULONG64 KpcrPtr = (PULONG64)(TrampVirt + ((ULONG_PTR)ApKpcrBase - SourceBase));
    *KpcrPtr = (ULONG64)KpcrBase;
    
    /* Reset startup state */
    PULONG StatePtr = (PULONG)(TrampVirt + ((ULONG_PTR)ApStartupState - SourceBase));
    *StatePtr = AP_STATE_STARTING;
    
    /* Clear error code */
    PULONG ErrPtr = (PULONG)(TrampVirt + ((ULONG_PTR)ApErrorCode - SourceBase));
    *ErrPtr = 0;
    
    DbgPrint("SMP: Trampoline context set for CPU %u: stack=%p, kpcr=%p\n",
             CpuNumber, StackBase, KpcrBase);
}

/* ---- AP Startup Sequence ------------------------------------------------- */

/*
 * KeSendInitIpi - Send INIT IPI to target AP
 */
static VOID KeSendInitIpi(ULONG TargetApicId)
{
    if (!LocalApicBase) {
        DbgPrint("SMP: ERROR - No LocalApicBase!\n");
        return;
    }
    
    DbgPrint("SMP: INIT IPI Step 1 - Clear errors\n");
    /* Clear any pending errors */
    ApicWrite(APIC_ESR, 0);
    (VOID)ApicRead(APIC_ESR);
    
    DbgPrint("SMP: INIT IPI Step 2 - Wait for ICR idle\n");
    /* Assert INIT (level-triggered, asserted) */
    ApicWaitForIcr();
    
    DbgPrint("SMP: INIT IPI Step 3 - Write ICR HIGH\n");
    ApicWrite(APIC_ICR_HIGH, TargetApicId << 24);
    
    DbgPrint("SMP: INIT IPI Step 4 - Write ICR LOW (assert INIT)\n");
    ApicWrite(APIC_ICR_LOW, APIC_INT_LEVELTRIG | APIC_INT_ASSERT | APIC_DM_INIT);
    
    DbgPrint("SMP: INIT IPI Step 5 - Delay 10ms\n");
    /* 10ms delay (Pentium requirement) */
    KeStallExecutionProcessor(10000);
    
    DbgPrint("SMP: INIT IPI Step 6 - Wait for ICR idle\n");
    /* Deassert INIT */
    ApicWaitForIcr();
    
    DbgPrint("SMP: INIT IPI Step 7 - Write ICR HIGH\n");
    ApicWrite(APIC_ICR_HIGH, TargetApicId << 24);
    
    DbgPrint("SMP: INIT IPI Step 8 - Write ICR LOW (deassert)\n");
    ApicWrite(APIC_ICR_LOW, APIC_INT_LEVELTRIG | APIC_DM_INIT);
    
    DbgPrint("SMP: INIT IPI Step 9 - Wait for completion\n");
    ApicWaitForIcr();
    
    DbgPrint("SMP: INIT IPI Complete\n");
}

/*
 * KeSendStartupIpi - Send STARTUP IPI to target AP
 */
static VOID KeSendStartupIpi(ULONG TargetApicId, ULONG StartupPage)
{
    if (!LocalApicBase)
        return;
    
    DbgPrint("SMP: StartupIPI: waiting for ICR idle...\n");
    /* Clear any pending errors */
    ApicWrite(APIC_ESR, 0);
    (VOID)ApicRead(APIC_ESR);
    
    /* Send STARTUP IPI (vector is page number, i.e., address >> 12) */
    DbgPrint("SMP: StartupIPI: sending to APIC %u, vector=0x%x\n", TargetApicId, StartupPage);
    ApicWaitForIcr();
    ApicWrite(APIC_ICR_HIGH, TargetApicId << 24);
    DbgPrint("SMP: StartupIPI: wrote ICR_HIGH\n");
    ApicWrite(APIC_ICR_LOW, APIC_DM_STARTUP | StartupPage);
    DbgPrint("SMP: StartupIPI: wrote ICR_LOW\n");
    
    /* Short delay */
    KeStallExecutionProcessor(200);
    
    DbgPrint("SMP: StartupIPI: waiting for completion...\n");
    ApicWaitForIcr();
    DbgPrint("SMP: StartupIPI: complete\n");
}

/*
 * KeStartProcessor - Start a specific AP processor
 */
NTSTATUS NTAPI KeStartProcessor(ULONG CpuNumber)
{
    NTSTATUS Status;
    ULONG ApicId;
    ULONG StartupPage;
    ULONG Timeout;
    PVOID StackBase;
    PKIPCR Kpcr;
    
    /* Validate CPU number */
    if (CpuNumber >= MAXIMUM_PROCESSORS || CpuNumber >= KeNumberProcessors)
        return STATUS_SMP_INVALID_CPU;
    
    /* Can't start the BSP */
    if (CpuNumber == KeBootProcessorNumber)
        return STATUS_SUCCESS;
    
    /* Check if already online */
    if (KeIsProcessorOnline(CpuNumber))
        return STATUS_SUCCESS;
    
    ApicId = KeProcessorApicId[CpuNumber];
    
    DbgPrint("SMP: Starting CPU %u (APIC ID %u)...\n", CpuNumber, ApicId);
    
    /* Allocate KPCR and stack first */
    Kpcr = KeAllocateKpcr(CpuNumber);
    if (!Kpcr) {
        DbgPrint("SMP: Failed to allocate KPCR for CPU %u\n", CpuNumber);
        return STATUS_NO_MEMORY;
    }
    
    StackBase = Kpcr->KernelStackTop;
    
    /* Mark as starting */
    KeProcessorState[CpuNumber] = CpuStateStarting;
    KeProcessorBlock[CpuNumber] = Kpcr;
    
    /* Reset startup synchronization */
    ApStartupComplete = 0;
    ApStartupError = 0;
    
    /* Setup AP startup context */
    if (!KeApStartupContext) {
        Status = KeAllocateApTrampoline();
        if (!NT_SUCCESS(Status)) {
            KeProcessorBlock[CpuNumber] = NULL;
            KeProcessorState[CpuNumber] = CpuStateOffline;
            /* Free KPCR resources */
            if (Kpcr->IdleStack)
                ExFreePoolWithTag(Kpcr->IdleStack, 'kStK');
            if (Kpcr->InterruptStack)
                ExFreePoolWithTag(Kpcr->InterruptStack, 'iStK');
            if (Kpcr->DpcStack)
                ExFreePoolWithTag(Kpcr->DpcStack, 'dStK');
            ExFreePoolWithTag(Kpcr, 'rCpK');
            return Status;
        }
    }
    
    /* Setup trampoline context for this AP */
    KeSetupApTrampolineContext(CpuNumber, ApicId, (PVOID)StackBase, Kpcr);
    
    /* Calculate startup page (address >> 12) */
    StartupPage = (ULONG)(KeApStartupPhysical >> 12);
    
    DbgPrint("SMP: INIT-SIPI-SIPI sequence to APIC ID %u, startup page=0x%x (addr=0x%x)\n",
             ApicId, StartupPage, KeApStartupPhysical);
    
    /* Send INIT IPI */
    DbgPrint("SMP: Sending INIT IPI...\n");
    KeSendInitIpi(ApicId);
    KeStallExecutionProcessor(10000);  /* 10ms after INIT */
    
    /* Memory barrier */
    KeMemoryBarrier();
    
    /* Send STARTUP IPI twice (INIT-SIPI-SIPI sequence) */
    DbgPrint("SMP: Sending STARTUP IPI #1 (vector=0x%x)...\n", StartupPage);
    KeSendStartupIpi(ApicId, StartupPage);
    DbgPrint("SMP: SIPI #1 sent, waiting 200ms...\n");
    KeStallExecutionProcessor(200000);  /* 200ms between SIPIs */
    DbgPrint("SMP: Sending STARTUP IPI #2...\n");
    KeSendStartupIpi(ApicId, StartupPage);
    DbgPrint("SMP: SIPI #2 sent\n");
    DbgPrint("SMP: SIPI sequence complete, waiting for AP...\n");
    
    /* Wait for AP to signal ready */
    Timeout = 10000;  /* 1 second timeout (100us units) */
    while (ApStartupComplete == 0 && ApStartupError == 0 && Timeout--)
        KeStallExecutionProcessor(100);
    
    if (ApStartupError != 0) {
        DbgPrint("SMP: CPU %u startup error %d\n", CpuNumber, ApStartupError);
        KeProcessorState[CpuNumber] = CpuStateOffline;
        return STATUS_SMP_AP_START_FAILED;
    }
    
    if (ApStartupComplete == 0) {
        DbgPrint("SMP: CPU %u startup timeout\n", CpuNumber);
        KeProcessorState[CpuNumber] = CpuStateOffline;
        return STATUS_SMP_AP_TIMEOUT;
    }
    
    /* Mark as online */
    KeProcessorState[CpuNumber] = CpuStateOnline;
    KeOnlineProcessors |= (1ULL << CpuNumber);
    
    DbgPrint("SMP: CPU %u is online\n", CpuNumber);
    
    return STATUS_SUCCESS;
}

/* ---- Per-CPU KPCR Initialization ------------------------------------------- */

/*
 * KeAllocateKpcr - Allocate and initialize a KPCR for a CPU
 */
PKIPCR KeAllocateKpcr(ULONG CpuNumber)
{
    PKIPCR Kpcr;
    PVOID StackBase;
    
    /* Allocate KPCR from non-paged pool */
    Kpcr = (PKIPCR)ExAllocatePoolWithTag(NonPagedPool, sizeof(KIPCR), 'rCpK');
    if (!Kpcr) {
        DbgPrint("SMP: Failed to allocate KPCR for CPU %u\n", CpuNumber);
        return NULL;
    }
    
    /* Clear structure */
    RtlZeroMemory(Kpcr, sizeof(KIPCR));
    
    /* Initialize KPCR fields */
    Kpcr->Self = (PKPCR)Kpcr;
    Kpcr->Prcb = &Kpcr->PrcbData;
    Kpcr->Irql = HIGH_LEVEL;
    Kpcr->MajorVersion = 6;
    Kpcr->MinorVersion = 1;
    
    /* Initialize PRCB */
    Kpcr->PrcbData.Number = CpuNumber;
    InitializeListHead(&Kpcr->PrcbData.DpcListHead);
    KeInitializeSpinLock(&Kpcr->PrcbData.DpcLock);
    
    /* Initialize SMP-specific fields */
    Kpcr->CpuNumber = CpuNumber;
    Kpcr->CpuState = CpuStateOnline;
    Kpcr->ApicId = KeProcessorApicId[CpuNumber];
    KeInitializeSpinLock(&Kpcr->IpiLock);
    InitializeListHead(&Kpcr->IpiQueue);
    
    /* Allocate kernel stack */
    StackBase = ExAllocatePoolWithTag(NonPagedPool, 0x6000, 'kStK'); /* 24KB */
    if (!StackBase) {
        /* Free KPCR on failure */
        ExFreePoolWithTag(Kpcr, 'rCpK');
        return NULL;
    }
    Kpcr->IdleStack = StackBase;
    Kpcr->KernelStackTop = (ULONG64)StackBase + 0x6000;
    
    /* Allocate interrupt stack */
    StackBase = ExAllocatePoolWithTag(NonPagedPool, 0x2000, 'iStK'); /* 8KB */
    if (StackBase)
        Kpcr->InterruptStack = StackBase;
    
    /* Allocate DPC stack */
    StackBase = ExAllocatePoolWithTag(NonPagedPool, 0x2000, 'dStK'); /* 8KB */
    if (StackBase)
        Kpcr->DpcStack = StackBase;
    
    return Kpcr;
}

/*
 * KeSetGsBase - Set GS base MSR for current CPU
 */
static VOID KeSetGsBase(ULONG64 Base)
{
    __asm__ __volatile__(
        "wrmsr"
        :
        : "c"(0xC0000101),    /* IA32_GS_BASE */
          "a"((ULONG)(Base & 0xFFFFFFFF)),
          "d"((ULONG)(Base >> 32))
    );
}

/*
 * KeGetProcessorBlock - Get the KPCR/KIPCR for a specific CPU
 */
PKIPCR NTAPI KeGetProcessorBlock(ULONG CpuNumber)
{
    if (CpuNumber >= MAXIMUM_PROCESSORS)
        return NULL;
    return KeProcessorBlock[CpuNumber];
}

/*
 * KeGetProcessorApicId - Get the APIC ID for a specific CPU
 */
ULONG NTAPI KeGetProcessorApicId(ULONG CpuNumber)
{
    if (CpuNumber >= MAXIMUM_PROCESSORS)
        return (ULONG)-1;
    return KeProcessorApicId[CpuNumber];
}

/* ---- IPI Coordination ------------------------------------------------------ */

/*
 * KeIpiGenericCall - Execute a function on all specified processors
 */
VOID NTAPI KeIpiGenericCall(
    VOID (NTAPI *Function)(PVOID Argument),
    PVOID Argument,
    KAFFINITY TargetProcessors
)
{
    ULONG Cpu;
    volatile LONG CompletionCount = 0;
    KIRQL OldIrql;
    
    if (!Function)
        return;
    
    /* Raise to IPI_LEVEL to prevent reentrancy */
    OldIrql = KfRaiseIrql(IPI_LEVEL);
    
    /* Queue IPI request to each target processor */
    for (Cpu = 0; Cpu < KeNumberProcessors; Cpu++) {
        if (KeTestProcessorAffinity(TargetProcessors, Cpu) &&
            Cpu != KeGetCurrentProcessorNumber()) {
            
            PKIPCR TargetKpcr = KeProcessorBlock[Cpu];
            if (!TargetKpcr)
                continue;
            
            /* Allocate and queue IPI request */
            PKIPI_REQUEST Request = (PKIPI_REQUEST)ExAllocatePoolWithTag(
                NonPagedPool, sizeof(KIPI_REQUEST), 'IpIK');
            if (!Request)
                continue;
            
            Request->Function = Function;
            Request->Argument = Argument;
            Request->CompletionFlag = &CompletionCount;
            Request->Flags = IPI_FLAG_WAIT;
            
            KeAcquireSpinLockAtDpcLevel(&TargetKpcr->IpiLock);
            InsertTailList(&TargetKpcr->IpiQueue, &Request->ListEntry);
            InterlockedIncrement(&TargetKpcr->IpiPending);
            KeReleaseSpinLockFromDpcLevel(&TargetKpcr->IpiLock);
            
            /* Send IPI to target CPU */
            ApicSendIpi(IPI_VECTOR_CALL_FUNCTION, KeProcessorApicId[Cpu]);
            
            InterlockedIncrement(&CompletionCount);
        }
    }
    
    /* Execute on current CPU */
    Function(Argument);
    
    /* Wait for all other CPUs to complete */
    if (CompletionCount > 0) {
        while (CompletionCount > 0)
            KeSpinWaitPause();
    }
    
    KfLowerIrql(OldIrql);
}

/*
 * KeIpiGenericCallAllButSelf - Execute function on all other processors
 */
VOID NTAPI KeIpiGenericCallAllButSelf(
    VOID (NTAPI *Function)(PVOID Argument),
    PVOID Argument
)
{
    KAFFINITY Target;
    Target = KeActiveProcessors & ~(1ULL << KeGetCurrentProcessorNumber());
    KeIpiGenericCall(Function, Argument, Target);
}

/*
 * KeIpiInterruptHandler - Handle incoming IPI
 */
VOID NTAPI KeIpiInterruptHandler(PKTRAP_FRAME TrapFrame)
{
    PKIPCR Kpcr = KeGetIpicr();
    
    UNREFERENCED_PARAMETER(TrapFrame);
    
    /* Send EOI first */
    ApicSendEoi();
    
    if (!Kpcr)
        return;
    
    /* Process IPI queue */
    while (InterlockedExchange(&Kpcr->IpiPending, 0) > 0) {
        KeAcquireSpinLockAtDpcLevel(&Kpcr->IpiLock);
        
        while (!IsListEmpty(&Kpcr->IpiQueue)) {
            PLIST_ENTRY Entry = RemoveHeadList(&Kpcr->IpiQueue);
            PKIPI_REQUEST Request = CONTAINING_RECORD(Entry, KIPI_REQUEST, ListEntry);
            
            KeReleaseSpinLockFromDpcLevel(&Kpcr->IpiLock);
            
            /* Execute the function */
            if (Request->Function)
                Request->Function(Request->Argument);
            
            /* Signal completion */
            if (Request->CompletionFlag)
                InterlockedDecrement(Request->CompletionFlag);
            
            /* Free the request */
            ExFreePoolWithTag(Request, 'IpIK');
            
            KeAcquireSpinLockAtDpcLevel(&Kpcr->IpiLock);
        }
        
        KeReleaseSpinLockFromDpcLevel(&Kpcr->IpiLock);
    }
}

/* ---- TLB Management -------------------------------------------------------- */

/*
 * KeFlushTbAll - Flush TLB on all processors
 */
VOID NTAPI KeFlushTbAll(VOID)
{
    /* Use INVPCID or INVVPID if available, otherwise reload CR3 */
    __asm__ __volatile__("movq %%cr3, %%rax; movq %%rax, %%cr3" ::: "rax", "memory");
    
    /* If SMP, send TLB shootdown IPI to all other CPUs */
    if (KeIsMpSystem()) {
        KeSendIpiAllButSelf(IPI_VECTOR_TLB_FLUSH);  /* Don't include self */
    }
}

/*
 * KeFlushTbSingle - Flush single TLB entry on all processors
 */
VOID NTAPI KeFlushTbSingle(ULONG_PTR VirtualAddress)
{
    /* Use INVLPG instruction */
    __asm__ __volatile__("invlpg (%0)" :: "r"(VirtualAddress) : "memory");
    
    /* If SMP, send TLB shootdown IPI */
    if (KeIsMpSystem()) {
        /* For single page flush, we'd ideally pass the address */
        /* For now, just do full TLB shootdown */
        KeSendIpiAll(IPI_VECTOR_TLB_FLUSH);
    }
}

/*
 * KeFlushTbMultiple - Flush multiple TLB entries on specified processors
 */
VOID NTAPI KeFlushTbMultiple(
    PVOID *VirtualAddresses,
    ULONG Count,
    KAFFINITY TargetProcessors
)
{
    ULONG i;
    
    /* Flush locally first */
    for (i = 0; i < Count; i++) {
        if (VirtualAddresses[i])
            __asm__ __volatile__("invlpg (%0)" :: "r"(VirtualAddresses[i]) : "memory");
    }
    
    /* Send IPI to other targets */
    if (KeIsMpSystem()) {
        ULONG Cpu;
        for (Cpu = 0; Cpu < KeNumberProcessors; Cpu++) {
            if (KeTestProcessorAffinity(TargetProcessors, Cpu) &&
                Cpu != KeGetCurrentProcessorNumber()) {
                KeSendIpi(KeProcessorApicId[Cpu], IPI_VECTOR_TLB_FLUSH);
            }
        }
    }
}

/*
 * KeRequestTlbShootdown - Request TLB shootdown across CPUs
 */
VOID NTAPI KeRequestTlbShootdown(
    PVOID VirtualAddress,
    KAFFINITY TargetProcessors
)
{
    /* Increment shootdown counter for statistics */
    PKIPCR Kpcr = KeGetIpicr();
    if (Kpcr)
        Kpcr->TlbShootdownPending++;
    
    /* Perform the shootdown */
    if (VirtualAddress)
        KeFlushTbSingle((ULONG_PTR)VirtualAddress);
    else
        KeFlushTbAll();
}

/* ---- CPU State Tracking ---------------------------------------------------- */

/*
 * KeIsProcessorPresent - Check if processor exists
 */
BOOLEAN NTAPI KeIsProcessorPresent(ULONG CpuNumber)
{
    if (CpuNumber >= MAXIMUM_PROCESSORS)
        return FALSE;
    return (KeActiveProcessors & (1ULL << CpuNumber)) != 0;
}

/*
 * KeIsProcessorOnline - Check if processor is online
 */
BOOLEAN NTAPI KeIsProcessorOnline(ULONG CpuNumber)
{
    if (CpuNumber >= MAXIMUM_PROCESSORS)
        return FALSE;
    return (KeOnlineProcessors & (1ULL << CpuNumber)) != 0;
}

/*
 * KeStopProcessor - Stop (offline) a specific processor
 */
VOID NTAPI KeStopProcessor(ULONG CpuNumber)
{
    if (CpuNumber == 0 || CpuNumber >= KeNumberProcessors)
        return;
    
    DbgPrint("SMP: Stopping CPU %u...\n", CpuNumber);
    
    /* Mark as stopping */
    KeProcessorState[CpuNumber] = CpuStateStopping;
    
    /* Send stop IPI */
    KeSendIpi(KeProcessorApicId[CpuNumber], IPI_VECTOR_STOP);
    
    /* Wait for CPU to stop */
    /* TODO: Implement proper synchronization */
    KeStallExecutionProcessor(100000);
    
    /* Mark as offline */
    KeProcessorState[CpuNumber] = CpuStateOffline;
    KeClearProcessorAffinity(KeOnlineProcessors, CpuNumber);
    
    DbgPrint("SMP: CPU %u stopped\n", CpuNumber);
}

/* ---- AP C Entry Point ------------------------------------------------------ */

/*
 * KiApStartup - C entry point for AP startup
 * 
 * This is called from the assembly trampoline after the AP has switched
 * to long mode and set up basic state.
 */
VOID NTAPI KiApStartup(VOID)
{
    ULONG CpuNumber;
    PKIPCR Kpcr;
    ULONG64 Cr3Value;
    
    /* Get CPU number from startup context - read from trampoline in low memory */
    ULONG_PTR TrampBase = (ULONG_PTR)KeApStartupPhysical;
    ULONG_PTR SourceBase = (ULONG_PTR)KiApTrampoline16;
    
    CpuNumber = *(PULONG)(TrampBase + ((ULONG_PTR)ApCpuNumber - SourceBase));
    
    DbgPrint("SMP: AP %u entering KiApStartup\n", CpuNumber);
    
    /* Get the KPCR that was pre-allocated by BSP */
    Kpcr = KeProcessorBlock[CpuNumber];
    if (!Kpcr) {
        DbgPrint("SMP: No KPCR for AP %u\n", CpuNumber);
        ApStartupError = 2;
        __asm__ __volatile__("hlt");
        return;
    }
    
    /* Load CR3 from BSP (same page tables) */
    /* Actually AP should already have this from trampoline */
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(Cr3Value));
    
    /* Set GS base to point to KPCR */
    KeSetGsBase((ULONG64)Kpcr);
    
    /* Verify we can access the KPCR via GS */
    PKPCR TestPcr = KeGetPcr();
    if (TestPcr != (PKPCR)Kpcr) {
        DbgPrint("SMP: AP %u KPCR verification failed!\n", CpuNumber);
        ApStartupError = 3;
        __asm__ __volatile__("hlt");
        return;
    }
    
    /* Initialize IDT - use same IDT as BSP */
    /* The IDT was set up by BSP and is shared */
    
    /* Initialize local APIC for this CPU */
    /* Enable APIC via SVR */
    ApicWrite(APIC_SVR, 0x100 | APIC_SPURIOUS_VECTOR);
    
    /* Enable interrupts */
    KeEnableInterrupts();
    
    /* Signal completion */
    ApStartupComplete = 1;
    
    /* Update startup state in trampoline */
    *(PULONG)(TrampBase + ((ULONG_PTR)ApStartupState - SourceBase)) = 0x1;
    
    DbgPrint("SMP: AP %u is ready, entering idle loop\n", CpuNumber);
    
    /* Enter idle loop */
    for (;;) {
        __asm__ __volatile__(
            "hlt\n"
            "pause\n"
        );
    }
}

/* ---- SMP Spinlock Improvements -------------------------------------------- */

/*
 * KeAcquireSpinLockRaiseToDpcSmp - Acquire spinlock with memory barriers
 * 
 * Enhanced version with proper memory ordering for SMP systems.
 */
KIRQL NTAPI KeAcquireSpinLockRaiseToDpcSmp(PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql = KfRaiseIrql(DISPATCH_LEVEL);
    
    /* Test-and-test-and-set with pause for hyperthreading */
    while (__atomic_test_and_set((volatile char *)SpinLock, __ATOMIC_ACQUIRE)) {
        /* Spin with less bus traffic using pause instruction */
        while (*(volatile KSPIN_LOCK *)SpinLock != 0)
            KeSpinWaitPause();
    }
    
    return OldIrql;
}

/*
 * KeReleaseSpinLockSmp - Release spinlock with memory barriers
 */
VOID NTAPI KeReleaseSpinLockSmp(PKSPIN_LOCK SpinLock, KIRQL NewIrql)
{
    /* Memory barrier before releasing lock */
    KeMemoryBarrier();
    
    /* Release the lock */
    __atomic_clear((volatile char *)SpinLock, __ATOMIC_RELEASE);
    
    KfLowerIrql(NewIrql);
}

/*
 * KeAcquireSpinLockAtDpcLevel - Acquire spinlock when already at DPC level
 */
VOID NTAPI KeAcquireSpinLockAtDpcLevel(PKSPIN_LOCK SpinLock)
{
    while (__atomic_test_and_set((volatile char *)SpinLock, __ATOMIC_ACQUIRE)) {
        while (*(volatile KSPIN_LOCK *)SpinLock != 0)
            KeSpinWaitPause();
    }
}

/*
 * KeReleaseSpinLockFromDpcLevel - Release spinlock when at DPC level
 */
VOID NTAPI KeReleaseSpinLockFromDpcLevel(PKSPIN_LOCK SpinLock)
{
    KeMemoryBarrier();
    __atomic_clear((volatile char *)SpinLock, __ATOMIC_RELEASE);
}

/* ---- SMP Initialization ---------------------------------------------------- */

/*
 * KeSmpInitializeAp - Initialize AP CPU after startup
 */
NTSTATUS NTAPI KeSmpInitializeAp(ULONG CpuNumber, ULONG ApicId)
{
    PKIPCR Kpcr;
    
    DbgPrint("SMP: Initializing AP %u (APIC %u)\n", CpuNumber, ApicId);
    
    /* Allocate KPCR for this AP */
    Kpcr = KeAllocateKpcr(CpuNumber);
    if (!Kpcr)
        return STATUS_NO_MEMORY;
    
    Kpcr->ApicId = ApicId;
    KeProcessorBlock[CpuNumber] = Kpcr;
    KeProcessorApicId[CpuNumber] = ApicId;
    
    /* Mark as online */
    KeProcessorState[CpuNumber] = CpuStateOnline;
    KeSetProcessorAffinity(KeOnlineProcessors, CpuNumber);
    
    return STATUS_SUCCESS;
}

/*
 * KeSmpInitSystem - Initialize SMP subsystem
 */
NTSTATUS NTAPI KeSmpInitSystem(VOID)
{
    NTSTATUS Status;
    ULONG i;
    
    DbgPrint("SMP: Initializing SMP subsystem...\n");
    
    /* Initialize spinlock */
    KeInitializeSpinLock(&SmpLock);
    
    /* Clear processor arrays */
    for (i = 0; i < MAXIMUM_PROCESSORS; i++) {
        KeProcessorBlock[i] = NULL;
        KeProcessorState[i] = CpuStateOffline;
        KeProcessorApicId[i] = 0;
    }
    
    /* Setup local APIC base from MSR */
    if (ApicIsPresent()) {
        ULONG Low, High;
        __asm__ __volatile__("rdmsr" : "=a"(Low), "=d"(High) : "c"(APIC_BASE_MSR));
        LocalApicBase = (PVOID)((ULONG_PTR)(Low & APIC_BASE_MSR_ADDR_MASK));
        DbgPrint("SMP: Local APIC at %p\n", LocalApicBase);
        
        /* Map APIC if needed (it's in MMIO space) */
        /* On x86, APIC is typically at physical 0xFEE00000 */
        if (LocalApicBase == (PVOID)0xFEE00000) {
            /* Map APIC registers */
            /* For now, assume it's already accessible */
        }
    } else {
        DbgPrint("SMP: No local APIC detected\n");
    }
    
    /* Detect MP configuration */
    Status = KeDetectMpConfiguration();
    if (!NT_SUCCESS(Status)) {
        /* Single processor fallback */
        KeNumberProcessors = 1;
        KeActiveProcessors = 1;
        KeOnlineProcessors = 1;
        KeProcessorApicId[0] = ApicGetId();
        KeBootProcessorNumber = 0;
        KeBootProcessorApicId = KeProcessorApicId[0];
        DbgPrint("SMP: Running in single-processor mode\n");
    }
    
    /* Setup BSP's KPCR in processor block */
    PKPCR BspPcr = KeGetPcr();
    if (BspPcr) {
        KeProcessorBlock[0] = (PKIPCR)BspPcr;
        KeProcessorState[0] = CpuStateOnline;
        BspPcr->Prcb->Number = 0;
    }
    
    /* If MP system, start APs */
    if (KeNumberProcessors > 1) {
        DbgPrint("SMP: Starting %u application processors...\n",
                 KeNumberProcessors - 1);
        
        for (i = 1; i < KeNumberProcessors; i++) {
            Status = KeStartProcessor(i);
            if (!NT_SUCCESS(Status)) {
                DbgPrint("SMP: Failed to start CPU %u: 0x%08X\n", i, Status);
                /* Continue with other CPUs */
            }
        }
    }
    
    SmpInitialized = TRUE;
    
    DbgPrint("SMP: Initialization complete. %u/%u processors online\n",
             KeGetProcessorCount(), KeNumberProcessors);
    
    return STATUS_SUCCESS;
}

/*
 * KeSmpStartupAp - Entry point called by AP during startup
 * (Called from assembly trampoline)
 */
VOID NTAPI KeSmpStartupAp(VOID)
{
    /* This is called from the trampoline - just call KiApStartup */
    KiApStartup();
}
