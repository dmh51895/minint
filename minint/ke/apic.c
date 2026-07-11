/*
 * MinNT - minint/ke/apic.c
 * Local APIC (LAPIC) driver with full initialization and IPI support.
 *
 * This driver implements:
 *   - LAPIC detection via CPUID
 *   - xAPIC mode (MMIO-based, default 0xFEE00000)
 *   - x2APIC mode detection (MSR-based)
 *   - Full IPI (Inter-Processor Interrupt) support
 *   - TLB shootdown, CPU stop, and function call IPIs
 *   - Per-CPU LAPIC ID tracking
 *
 * Reference: Intel SDM Vol. 3A, Chapter 10
 *            Linux arch/x86/kernel/apic/apic.c
 */

#include <nt/ntdef.h>
#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/hal.h>
#include <nt/apic.h>

/* ---- Global State ------------------------------------------------------- */

/* MMIO base address for xAPIC mode (NULL if using x2APIC) */
volatile ULONG *LapicBase = NULL;

/* Current APIC mode: APIC_MODE_DISABLED, APIC_MODE_XAPIC, APIC_MODE_X2APIC */
ULONG LapicMode = APIC_MODE_DISABLED;

/* BSP APIC information */
LAPIC_INFO LapicBootInfo = { 0 };

/* Per-processor APIC ID mapping table */
extern ULONG KeProcessorApicId[];

/* Total number of detected processors */
ULONG KeProcessorCount = 1;  /* At least BSP exists */

/* Spinlock for IPI operations */
static KSPIN_LOCK IpiLock;

/* Spinlock for LAPIC register access (xAPIC mode only) */
static KSPIN_LOCK LapicAccessLock;

/* ---- Forward Declarations ----------------------------------------------- */

static VOID LapicEnableX2Apic(VOID);
static VOID LapicEnableXApic(VOID);
static VOID LapicSetupSvr(VOID);
static VOID LapicSetupLvtEntries(VOID);
static VOID LapicClearErrors(VOID);
static ULONG LapicGetMaxLvtEntries(VOID);
static VOID LapicWaitIcrIdle(VOID);

/* ---- CPUID and MSR Helper Functions ------------------------------------ */

/*
 * KeCpuId - Execute CPUID instruction
 */
FORCEINLINE VOID KeCpuId(ULONG Function, ULONG SubFunction,
                                  PULONG Eax, PULONG Ebx, PULONG Ecx, PULONG Edx)
{
    __asm__ __volatile__(
        "cpuid"
        : "=a"(*Eax), "=b"(*Ebx), "=c"(*Ecx), "=d"(*Edx)
        : "a"(Function), "c"(SubFunction)
        : "memory"
    );
}

/*
 * KeReadMsr - Read a Model-Specific Register
 */
FORCEINLINE ULONG64 KeReadMsr(ULONG Msr)
{
    ULONG Low, High;
    __asm__ __volatile__("rdmsr" : "=a"(Low), "=d"(High) : "c"(Msr));
    return ((ULONG64)High << 32) | Low;
}

/*
 * KeWriteMsr - Write a Model-Specific Register
 */
FORCEINLINE VOID KeWriteMsr(ULONG Msr, ULONG64 Value)
{
    ULONG Low = (ULONG)(Value & 0xFFFFFFFF);
    ULONG High = (ULONG)(Value >> 32);
    __asm__ __volatile__("wrmsr" :: "a"(Low), "d"(High), "c"(Msr));
}

/* ---- LAPIC Detection ---------------------------------------------------- */

/*
 * KeDetectLapic - Check if LAPIC is present via CPUID
 *
 * Returns TRUE if APIC feature bit is set in CPUID.EDX
 */
BOOLEAN NTAPI KeDetectLapic(VOID)
{
    ULONG eax, ebx, ecx, edx;
    
    /* CPUID function 1: Get processor features */
    KeCpuId(1, 0, &eax, &ebx, &ecx, &edx);
    
    /* Check APIC bit (bit 9 of EDX) */
    return (edx & CPUID_FEATURE_APIC) != 0;
}

/*
 * KeDetectX2Apic - Check if x2APIC is supported and enabled
 *
 * x2APIC is indicated by bit 21 of CPUID.ECX (function 1)
 */
BOOLEAN NTAPI KeDetectX2Apic(VOID)
{
    ULONG eax, ebx, ecx, edx;
    ULONG64 apicBase;
    
    /* CPUID function 1: Get processor features */
    KeCpuId(1, 0, &eax, &ebx, &ecx, &edx);
    
    /* Check x2APIC bit (bit 21 of ECX) */
    if ((ecx & CPUID_FEATURE_X2APIC) == 0) {
        return FALSE;
    }
    
    /* Also verify x2APIC is enabled in APIC_BASE MSR */
    apicBase = KeReadMsr(MSR_IA32_APICBASE);
    if (!(apicBase & MSR_IA32_APICBASE_EXTD)) {
        return FALSE;
    }
    
    return TRUE;
}

/*
 * KeIsLapicPresent - Check if LAPIC is initialized and available
 */
BOOLEAN NTAPI KeIsLapicPresent(VOID)
{
    return (LapicMode != APIC_MODE_DISABLED);
}

/* ---- LAPIC Initialization ----------------------------------------------- */

/*
 * LapicEnableXApic - Enable xAPIC mode (traditional MMIO-based APIC)
 *
 * In xAPIC mode, we use MMIO to access APIC registers.
 * The default physical base is 0xFEE00000.
 */
static VOID LapicEnableXApic(VOID)
{
    ULONG64 apicBase;
    
    /* Read current APIC base from MSR */
    apicBase = KeReadMsr(MSR_IA32_APICBASE);
    
    /* If x2APIC is currently enabled, we need to disable it first */
    if (apicBase & MSR_IA32_APICBASE_EXTD) {
        /* Disable x2APIC by clearing the EXTENDED bit, keep enabled bit */
        apicBase &= ~MSR_IA32_APICBASE_EXTD;
        KeWriteMsr(MSR_IA32_APICBASE, apicBase);
    }
    
    /* Ensure APIC is enabled */
    if (!(apicBase & MSR_IA32_APICBASE_ENABLE)) {
        apicBase |= MSR_IA32_APICBASE_ENABLE;
        KeWriteMsr(MSR_IA32_APICBASE, apicBase);
    }
    
    /* Get the base address (masked) */
    apicBase = KeReadMsr(MSR_IA32_APICBASE);
    apicBase &= MSR_IA32_APICBASE_BASE;
    
    /*
     * In a real implementation, we would map this physical address.
     * For MinNT, the boot stub maps the first 4GB identity-mapped,
     * so we can access the LAPIC directly.
     */
    LapicBase = (volatile ULONG *)(ULONG_PTR)apicBase;
    LapicMode = APIC_MODE_XAPIC;
    
    DbgPrint("KE: xAPIC enabled at base %p\n", LapicBase);
}

/*
 * LapicEnableX2Apic - Enable x2APIC mode (MSR-based APIC)
 *
 * In x2APIC mode, we use RDMSR/WRMSR to access APIC registers.
 * This mode is required for systems with more than 255 CPUs.
 */
static VOID LapicEnableX2Apic(VOID)
{
    ULONG64 apicBase;
    
    /* Read current APIC base from MSR */
    apicBase = KeReadMsr(MSR_IA32_APICBASE);
    
    /* Enable x2APIC mode (sets both ENABLE and EXTENDED bits) */
    apicBase |= (MSR_IA32_APICBASE_ENABLE | MSR_IA32_APICBASE_EXTD);
    KeWriteMsr(MSR_IA32_APICBASE, apicBase);
    
    LapicMode = APIC_MODE_X2APIC;
    LapicBase = NULL;  /* Not used in x2APIC mode */
    
    DbgPrint("KE: x2APIC mode enabled\n");
}

/*
 * LapicGetMaxLvtEntries - Get the maximum number of LVT entries
 */
static ULONG LapicGetMaxLvtEntries(VOID)
{
    ULONG version;
    
    if (LapicMode == APIC_MODE_X2APIC) {
        version = (ULONG)(LapicReadMsr(MSR_IA32_X2APIC_VERSION) & 0xFF);
    } else {
        version = LapicRead(LAPIC_VERSION) & APIC_VERSION_MASK;
    }
    
    /* For integrated APICs, read MAXLVT from bits 16-23 */
    if (APIC_INTEGRATED(version)) {
        return ((version >> 16) & 0xFF);
    }
    
    /* 82489DX has fixed 2 LVT entries */
    return 2;
}

/*
 * LapicClearErrors - Clear the Error Status Register
 */
static VOID LapicClearErrors(VOID)
{
    ULONG maxLvt;
    
    maxLvt = LapicGetMaxLvtEntries();
    
    if (LapicMode == APIC_MODE_X2APIC) {
        /* In x2APIC, ESR is accessed via MSR */
        if (maxLvt > 3) {
            LapicWriteMsr(MSR_IA32_X2APIC_ESR, 0);
        }
        /* Read to clear */
        (VOID)LapicReadMsr(MSR_IA32_X2APIC_ESR);
    } else {
        if (maxLvt > 3) {
            /* For integrated APICs, clear by writing then reading */
            LapicWrite(LAPIC_ESR, 0);
        }
        (VOID)LapicRead(LAPIC_ESR);
    }
}

/*
 * LapicSetupSvr - Setup the Spurious Vector Register
 *
 * This register controls the spurious interrupt vector and
 * enables/disables the APIC.
 */
static VOID LapicSetupSvr(VOID)
{
    ULONG svr;
    
    if (LapicMode == APIC_MODE_X2APIC) {
        svr = (ULONG)(LapicReadMsr(MSR_IA32_X2APIC_SVR));
    } else {
        svr = LapicRead(LAPIC_SVR);
    }
    
    /* Set spurious vector and enable APIC */
    svr &= ~APIC_SVR_VECTOR_MASK;
    svr |= SPURIOUS_APIC_VECTOR;
    svr |= APIC_SVR_ENABLE;
    
    /* Disable focus processor checking (helps with certain errata) */
    svr |= APIC_SVR_FOCUS_DISABLE;
    
    if (LapicMode == APIC_MODE_X2APIC) {
        LapicWriteMsr(MSR_IA32_X2APIC_SVR, svr);
    } else {
        LapicWrite(LAPIC_SVR, svr);
    }
}

/*
 * LapicSetupLvtEntries - Configure Local Vector Table entries
 *
 * Sets up timer, LINT0, LINT1, and error LVT entries.
 */
static VOID LapicSetupLvtEntries(VOID)
{
    ULONG lvtValue;
    ULONG maxLvt;
    
    maxLvt = LapicGetMaxLvtEntries();
    
    /*
     * LVT Timer - Initially masked, will be configured later
     * if using APIC timer instead of PIT
     */
    lvtValue = LOCAL_TIMER_VECTOR | APIC_LVT_MASKED;
    if (LapicMode == APIC_MODE_X2APIC) {
        LapicWriteMsr(MSR_IA32_X2APIC_LVT_TIMER, lvtValue);
    } else {
        LapicWrite(LAPIC_LVT_TIMER, lvtValue);
    }
    
    /*
     * LVT LINT0 - Masked for now (would be used for 8259 PIC
     * in Virtual Wire Mode if needed)
     */
    lvtValue = APIC_LVT_MASKED | APIC_DM_EXTINT;
    if (LapicMode == APIC_MODE_X2APIC) {
        LapicWriteMsr(MSR_IA32_X2APIC_LVT_LINT0, lvtValue);
    } else {
        LapicWrite(LAPIC_LVT_LINT0, lvtValue);
    }
    
    /*
     * LVT LINT1 - Masked for now (would be used for NMI
     * in Virtual Wire Mode if needed)
     */
    lvtValue = APIC_LVT_MASKED | APIC_DM_NMI;
    if (!LapicBootInfo.Integrated) {
        /* 82489DX requires level-triggered for LINT1 */
        lvtValue |= APIC_LVT_LEVEL_TRIGGER;
    }
    if (LapicMode == APIC_MODE_X2APIC) {
        LapicWriteMsr(MSR_IA32_X2APIC_LVT_LINT1, lvtValue);
    } else {
        LapicWrite(LAPIC_LVT_LINT1, lvtValue);
    }
    
    /*
     * LVT Error - Configure error reporting
     */
    if (maxLvt >= 3) {
        lvtValue = ERROR_APIC_VECTOR;
        if (LapicMode == APIC_MODE_X2APIC) {
            LapicWriteMsr(MSR_IA32_X2APIC_LVT_ERROR, lvtValue);
        } else {
            LapicWrite(LAPIC_LVT_ERROR, lvtValue);
        }
    }
    
    /*
     * LVT Performance Counter - Mask if present
     */
    if (maxLvt >= 4) {
        lvtValue = APIC_LVT_MASKED;
        if (LapicMode == APIC_MODE_X2APIC) {
            /* x2APIC LVT Performance is at MSR 0x834 */
        } else {
            LapicWrite(LAPIC_PERF, lvtValue);
        }
    }
}

/*
 * KeInitializeLapic - Initialize the Local APIC
 *
 * This is the main entry point for LAPIC initialization.
 * It detects the APIC, sets up the appropriate mode (xAPIC or x2APIC),
 * and configures the basic registers.
 */
VOID NTAPI KeInitializeLapic(VOID)
{
    ULONG eax, ebx, ecx, edx;
    ULONG apicId;
    
    DbgPrint("KE: Initializing Local APIC...\n");
    
    /* Initialize spinlocks */
    KeInitializeSpinLock(&IpiLock);
    KeInitializeSpinLock(&LapicAccessLock);
    
    /* Check if LAPIC is present */
    if (!KeDetectLapic()) {
        DbgPrint("KE: WARNING - LAPIC not detected!\n");
        LapicMode = APIC_MODE_DISABLED;
        return;
    }
    
    /* Check for x2APIC support */
    KeCpuId(1, 0, &eax, &ebx, &ecx, &edx);
    
    if ((ecx & CPUID_FEATURE_X2APIC) && KeDetectX2Apic()) {
        /* x2APIC is already enabled by BIOS */
        LapicMode = APIC_MODE_X2APIC;
        LapicBase = NULL;
    } else {
        /* Use xAPIC mode */
        LapicEnableXApic();
    }
    
    /* Get APIC ID and version */
    if (LapicMode == APIC_MODE_X2APIC) {
        apicId = (ULONG)LapicReadMsr(MSR_IA32_X2APIC_APICID);
        LapicBootInfo.Version = (ULONG)(LapicReadMsr(MSR_IA32_X2APIC_VERSION) & 0xFF);
    } else {
        apicId = (LapicRead(LAPIC_ID) >> APIC_ID_SHIFT) & 0xFF;
        LapicBootInfo.Version = LapicRead(LAPIC_VERSION) & APIC_VERSION_MASK;
    }
    
    LapicBootInfo.ApicId = apicId;
    LapicBootInfo.MaxLvt = LapicGetMaxLvtEntries();
    LapicBootInfo.Integrated = APIC_INTEGRATED(LapicBootInfo.Version) != 0;
    LapicBootInfo.X2Apic = (LapicMode == APIC_MODE_X2APIC);
    
    /* Store BSP's APIC ID */
    KeProcessorApicId[0] = apicId;
    KeProcessorCount = 1;
    
    DbgPrint("KE: APIC ID=%u Version=%u MaxLVT=%u Integrated=%s Mode=%s\n",
             apicId,
             LapicBootInfo.Version,
             LapicBootInfo.MaxLvt,
             LapicBootInfo.Integrated ? "Yes" : "No",
             LapicMode == APIC_MODE_X2APIC ? "x2APIC" : "xAPIC");
    
    /* Clear any pending errors */
    LapicClearErrors();
    
    /* Set Task Priority Register to accept all but NMI/MCA (0-31) */
    if (LapicMode == APIC_MODE_X2APIC) {
        LapicWriteMsr(MSR_IA32_X2APIC_TPR, 0x10);
    } else {
        LapicWrite(LAPIC_TPR, 0x10);
    }
    
    /* Setup LVT entries */
    LapicSetupLvtEntries();
    
    /* Enable APIC via SVR */
    LapicSetupSvr();
    
    DbgPrint("KE: Local APIC initialized successfully\n");
}

/*
 * KeInitializeLapicForProcessor - Initialize LAPIC for a secondary processor
 *
 * Called when bringing up an Application Processor (AP).
 */
VOID NTAPI KeInitializeLapicForProcessor(ULONG ProcessorNumber)
{
    ULONG apicId;
    
    /* Get the APIC ID for this processor */
    if (LapicMode == APIC_MODE_X2APIC) {
        apicId = (ULONG)LapicReadMsr(MSR_IA32_X2APIC_APICID);
    } else {
        apicId = (LapicRead(LAPIC_ID) >> APIC_ID_SHIFT) & 0xFF;
    }
    
    /* Store in the mapping table */
    if (ProcessorNumber < MAX_PROCESSORS) {
        KeProcessorApicId[ProcessorNumber] = apicId;
        if (ProcessorNumber >= KeProcessorCount) {
            KeProcessorCount = ProcessorNumber + 1;
        }
    }
    
    /* Setup for this processor */
    LapicClearErrors();
    
    /* Set Task Priority Register */
    if (LapicMode == APIC_MODE_X2APIC) {
        LapicWriteMsr(MSR_IA32_X2APIC_TPR, 0x10);
    } else {
        LapicWrite(LAPIC_TPR, 0x10);
    }
    
    /* Setup LVT entries */
    LapicSetupLvtEntries();
    
    /* Enable APIC via SVR */
    LapicSetupSvr();
    
    DbgPrint("KE: AP %u initialized (APIC ID=%u)\n", ProcessorNumber, apicId);
}

/* ---- LAPIC Register Access Functions ------------------------------------ */

/*
 * KeGetLapicVersion - Get LAPIC version
 */
ULONG NTAPI KeGetLapicVersion(VOID)
{
    if (LapicMode == APIC_MODE_X2APIC) {
        return (ULONG)(LapicReadMsr(MSR_IA32_X2APIC_VERSION) & 0xFF);
    }
    if (LapicBase) {
        return LapicRead(LAPIC_VERSION) & APIC_VERSION_MASK;
    }
    return 0;
}

/*
 * KeLapicEoi - Send End of Interrupt acknowledgment
 *
 * Must be called at the end of an interrupt handler that
 * received an interrupt from the local APIC.
 */
VOID NTAPI KeLapicEoi(VOID)
{
    if (LapicMode == APIC_MODE_X2APIC) {
        LapicWriteMsr(MSR_IA32_X2APIC_EOI, 0);
    } else if (LapicBase) {
        LapicWrite(LAPIC_EOI, 0);
    }
}

/* ---- IPI (Inter-Processor Interrupt) Support --------------------------- */

/*
 * LapicWaitIcrIdle - Wait for the Interrupt Command Register to become idle
 *
 * The ICR delivery status bit indicates whether an IPI is still pending.
 */
static VOID LapicWaitIcrIdle(VOID)
{
    ULONG icrLow;
    ULONG timeout = 1000000;  /* Prevent infinite loop */
    
    if (LapicMode == APIC_MODE_X2APIC) {
        /* x2APIC has no delivery status bit - writes are immediate */
        return;
    }
    
    /* Wait for Delivery Status bit to clear (0 = idle) */
    do {
        icrLow = LapicRead(LAPIC_ICR_LOW);
        if (!(icrLow & APIC_ICR_DS_PENDING)) {
            break;
        }
        timeout--;
        if (timeout == 0) {
            DbgPrint("KE: WARNING - ICR timeout waiting for idle\n");
            break;
        }
        /* Relaxed spin */
        __asm__ __volatile__("pause" ::: "memory");
    } while (TRUE);
}

/*
 * KeLapicWaitForIcrIdle - Public API to wait for ICR idle
 */
VOID NTAPI KeLapicWaitForIcrIdle(VOID)
{
    LapicWaitIcrIdle();
}

/*
 * KeSendIpiEx - Send an IPI with specific delivery mode
 *
 * ApicId:    Destination APIC ID
 * Vector:    Interrupt vector (0x10-0xFE)
 * DeliveryMode: APIC_ICR_DM_FIXED, APIC_ICR_DM_NMI, etc.
 */
VOID NTAPI KeSendIpiEx(ULONG ApicId, ULONG Vector, ULONG DeliveryMode)
{
    KIRQL oldIrql;
    ULONG icrLow, icrHigh;
    
    if (LapicMode == APIC_MODE_DISABLED) {
        return;
    }
    
    /* Raise IRQL to prevent preemption during IPI send */
    KeRaiseIrql(IPI_LEVEL, &oldIrql);
    KeAcquireSpinLock(&IpiLock, &oldIrql);
    
    if (LapicMode == APIC_MODE_X2APIC) {
        /*
         * x2APIC mode: Single MSR write
         * Destination is in bits 32-63, vector and mode in bits 0-31
         */
        ULONG64 icr = ((ULONG64)ApicId << 32) | 
                      (Vector & APIC_ICR_VECTOR_MASK) |
                      (DeliveryMode & APIC_ICR_DM_MASK);
        
        LapicWriteMsr(MSR_IA32_X2APIC_ICR, icr);
    } else {
        /*
         * xAPIC mode: Two 32-bit writes to ICR_HIGH and ICR_LOW
         * Must write HIGH first, then LOW (which triggers the send)
         */
        
        /* Wait for any previous IPI to complete */
        LapicWaitIcrIdle();
        
        /* Setup destination in ICR_HIGH */
        icrHigh = (ApicId & 0xFF) << APIC_ICR_DEST_SHIFT;
        LapicWrite(LAPIC_ICR_HIGH, icrHigh);
        
        /* Setup ICR_LOW with vector and delivery mode */
        icrLow = (Vector & APIC_ICR_VECTOR_MASK) |
                 (DeliveryMode & APIC_ICR_DM_MASK) |
                 APIC_ICR_LEVEL_ASSERT;  /* Assert level for all IPIs */
        
        /* For INIT and SIPI, need level-triggered mode */
        if (DeliveryMode == APIC_ICR_DM_INIT || 
            DeliveryMode == APIC_ICR_DM_STARTUP) {
            icrLow |= APIC_ICR_TRIGGER_LEVEL;
        }
        
        /* Write ICR_LOW - this sends the IPI */
        LapicWrite(LAPIC_ICR_LOW, icrLow);
    }
    
    KeReleaseSpinLock(&IpiLock, oldIrql);
}

/*
 * KeSendIpi - Send an IPI to a specific processor
 *
 * ApicId: Destination APIC ID
 * Vector: Interrupt vector to deliver
 */
VOID NTAPI KeSendIpi(ULONG ApicId, ULONG Vector)
{
    KeSendIpiEx(ApicId, Vector, APIC_ICR_DM_FIXED);
}

/*
 * KeSendIpiSelf - Send an IPI to self
 */
VOID NTAPI KeSendIpiSelf(ULONG Vector)
{
    KIRQL oldIrql;
    
    if (LapicMode == APIC_MODE_DISABLED) {
        return;
    }
    
    KeRaiseIrql(IPI_LEVEL, &oldIrql);
    KeAcquireSpinLock(&IpiLock, &oldIrql);
    
    if (LapicMode == APIC_MODE_X2APIC) {
        /* x2APIC has dedicated SELF IPI register */
        LapicWriteMsr(MSR_IA32_X2APIC_SELF_IPI, Vector);
    } else {
        /* xAPIC: Use SELF destination shorthand */
        ULONG icrLow = (Vector & APIC_ICR_VECTOR_MASK) |
                       APIC_ICR_DM_FIXED |
                       APIC_ICR_DEST_SELF |
                       APIC_ICR_LEVEL_ASSERT;
        
        LapicWaitIcrIdle();
        LapicWrite(LAPIC_ICR_LOW, icrLow);
    }
    
    KeReleaseSpinLock(&IpiLock, oldIrql);
}

/*
 * KeSendIpiAll - Send an IPI to all processors including self
 *
 * Vector: Interrupt vector to deliver
 */
VOID NTAPI KeSendIpiAll(ULONG Vector)
{
    KIRQL oldIrql;
    
    if (LapicMode == APIC_MODE_DISABLED) {
        return;
    }
    
    KeRaiseIrql(IPI_LEVEL, &oldIrql);
    KeAcquireSpinLock(&IpiLock, &oldIrql);
    
    if (LapicMode == APIC_MODE_X2APIC) {
        /* x2APIC: Use 0xFFFFFFFF as destination for "all" */
        ULONG64 icr = ((ULONG64)0xFFFFFFFFULL << 32) |
                      (Vector & APIC_ICR_VECTOR_MASK) |
                      APIC_ICR_DM_FIXED;
        LapicWriteMsr(MSR_IA32_X2APIC_ICR, icr);
    } else {
        /* xAPIC: Use ALL INCLUDING SELF shorthand */
        ULONG icrLow = (Vector & APIC_ICR_VECTOR_MASK) |
                       APIC_ICR_DM_FIXED |
                       APIC_ICR_DEST_ALLINC |
                       APIC_ICR_LEVEL_ASSERT;
        
        LapicWaitIcrIdle();
        LapicWrite(LAPIC_ICR_LOW, icrLow);
    }
    
    KeReleaseSpinLock(&IpiLock, oldIrql);
}

/*
 * KeSendIpiAllButSelf - Send an IPI to all processors except self
 *
 * This is commonly used for TLB shootdown and other global operations
 * where the initiating processor has already done the work.
 *
 * Vector: Interrupt vector to deliver
 */
VOID NTAPI KeSendIpiAllButSelf(ULONG Vector)
{
    KIRQL oldIrql;
    
    if (LapicMode == APIC_MODE_DISABLED) {
        return;
    }
    
    KeRaiseIrql(IPI_LEVEL, &oldIrql);
    KeAcquireSpinLock(&IpiLock, &oldIrql);
    
    if (LapicMode == APIC_MODE_X2APIC) {
        /* x2APIC: Use ALL EXCEPT SELF encoding in bits 40-43 */
        ULONG64 icr = ((ULONG64)0x00000000ULL << 32) |  /* Destination not used */
                      (Vector & APIC_ICR_VECTOR_MASK) |
                      APIC_ICR_DM_FIXED;
        /* For x2APIC all-but-self, we use the shorthand encoding */
        icr |= ((ULONG64)3 << 40);  /* Destination shorthand = 3 (all but self) */
        LapicWriteMsr(MSR_IA32_X2APIC_ICR, icr);
    } else {
        /* xAPIC: Use ALL BUT SELF shorthand */
        ULONG icrLow = (Vector & APIC_ICR_VECTOR_MASK) |
                       APIC_ICR_DM_FIXED |
                       APIC_ICR_DEST_ALLBUT |
                       APIC_ICR_LEVEL_ASSERT;
        
        LapicWaitIcrIdle();
        LapicWrite(LAPIC_ICR_LOW, icrLow);
    }
    
    KeReleaseSpinLock(&IpiLock, oldIrql);
}

/*
 * KeSendIpiMask - Send IPI to multiple processors based on APIC ID mask
 *
 * This function iterates through the mask and sends individual IPIs.
 * For large systems, a logical destination mode would be more efficient.
 */
VOID NTAPI KeSendIpiMask(PULONG ApicIdMask, ULONG Vector)
{
    ULONG i;
    ULONG mask;
    
    if (LapicMode == APIC_MODE_DISABLED || ApicIdMask == NULL) {
        return;
    }
    
    for (i = 0; i < (MAX_PROCESSORS + 31) / 32; i++) {
        mask = ApicIdMask[i];
        while (mask) {
            ULONG bit = __builtin_ctz(mask);
            ULONG apicId = i * 32 + bit;
            
            KeSendIpi(apicId, Vector);
            
            mask &= ~(1UL << bit);
        }
    }
}

/* ---- IPI Handlers ------------------------------------------------------- */

/*
 * KeIpiInvalidateHandler - TLB shootdown handler
 *
 * Called when another processor has modified page tables and
 * needs this processor to flush its TLB.
 */
VOID NTAPI KeIpiInvalidateHandler(PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    
    /* Flush the entire TLB by reloading CR3 */
    __asm__ __volatile__(
        "movq %%cr3, %%rax\n\t"
        "movq %%rax, %%cr3\n\t"
        ::: "rax", "memory"
    );
    
    /* Send EOI */
    KeLapicEoi();
}

/*
 * KeIpiStopHandler - CPU stop handler
 *
 * Called when the system needs this processor to halt immediately.
 * This is used for panic and emergency situations.
 */
VOID NTAPI KeIpiStopHandler(PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    
    /* Send EOI before halting */
    KeLapicEoi();
    
    /* Disable interrupts and halt forever */
    KeDisableInterrupts();
    
    while (TRUE) {
        __asm__ __volatile__("hlt" ::: "memory");
    }
}

/*
 * KeIpiCallHandler - Function call IPI handler
 *
 * Executes a function on this processor at the request of another.
 * The function pointer and context are passed via a global structure.
 */
static volatile PIPI_CALL_CONTEXT IpiCallContext = NULL;

VOID NTAPI KeIpiCallHandler(PKTRAP_FRAME TrapFrame)
{
    PIPI_CALL_CONTEXT context;
    
    UNREFERENCED_PARAMETER(TrapFrame);
    
    /* Get the function call context */
    context = (PIPI_CALL_CONTEXT)IpiCallContext;
    
    if (context != NULL && context->Function != NULL) {
        /* Acknowledge receipt */
        context->Acknowledged = 1;
        
        /* Execute the function */
        context->Function(context->Context);
        
        /* Mark as completed */
        context->Completed = 1;
    }
    
    /* Send EOI */
    KeLapicEoi();
}

/*
 * KeIpiRescheduleHandler - Reschedule IPI handler
 *
 * Triggers the scheduler on this processor to check for
 * higher priority threads.
 */
VOID NTAPI KeIpiRescheduleHandler(PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    
    /*
     * In a full NT kernel, this would trigger the scheduler.
     * For now, we just acknowledge with EOI.
     */
    
    /* Send EOI */
    KeLapicEoi();
}

/* ---- High-Level IPI Operations ------------------------------------------ */

/*
 * KeInvalidateAllTlbs - Send TLB shootdown IPI to all other processors
 *
 * Called after modifying page tables that may be cached in other
 * processors' TLBs.
 */
VOID NTAPI KeInvalidateAllTlbs(VOID)
{
    /* Send invalidate IPI to all but self */
    KeSendIpiAllButSelf(IPI_VECTOR_INVALIDATE);
}

/*
 * KeStopAllProcessors - Send stop IPI to all other processors
 *
 * Used in panic situations to freeze all processors.
 */
VOID NTAPI KeStopAllProcessors(VOID)
{
    /* Send stop IPI to all but self */
    KeSendIpiAllButSelf(IPI_VECTOR_STOP);
}

/*
 * KeCallFunctionOnProcessor - Execute a function on a specific processor
 *
 * This uses the IPI_CALL mechanism to execute code on another CPU.
 * It waits for completion before returning.
 *
 * ProcessorNumber: Target processor
 * Function:        Function to execute
 * Context:         Context argument for function
 */
VOID NTAPI KeCallFunctionOnProcessor(ULONG ProcessorNumber,
                                       VOID (NTAPI *Function)(PVOID),
                                       PVOID Context)
{
    IPI_CALL_CONTEXT callContext;
    ULONG targetApicId;
    
    if (ProcessorNumber >= KeProcessorCount) {
        return;
    }
    
    /* Get target APIC ID */
    targetApicId = KeProcessorApicId[ProcessorNumber];
    
    /* Setup call context */
    callContext.Function = Function;
    callContext.Context = Context;
    callContext.Completed = 0;
    callContext.Acknowledged = 0;
    
    /* Publish context pointer */
    IpiCallContext = &callContext;
    
    /* Memory barrier to ensure context is visible */
    KeLapicMemoryBarrier();
    
    /* Send IPI to target processor */
    KeSendIpi(targetApicId, IPI_VECTOR_CALL);
    
    /* Wait for completion with timeout */
    {
        ULONG timeout = 10000000;  /* Timeout counter */
        while (!callContext.Completed && timeout > 0) {
            __asm__ __volatile__("pause" ::: "memory");
            timeout--;
        }
        
        if (timeout == 0) {
            DbgPrint("KE: WARNING - IPI call timeout on processor %u\n", 
                     ProcessorNumber);
        }
    }
    
    /* Clear context pointer */
    IpiCallContext = NULL;
    KeLapicMemoryBarrier();
}

/*
 * KeCallFunctionOnAllProcessors - Execute a function on all processors
 *
 * Function: Function to execute
 * Context:   Context argument for function
 * Wait:      TRUE to wait for all to complete
 */
VOID NTAPI KeCallFunctionOnAllProcessors(
    VOID (NTAPI *Function)(PVOID),
    PVOID Context,
    BOOLEAN Wait)
{
    IPI_CALL_CONTEXT callContext;
    ULONG i;
    
    /* Setup call context */
    callContext.Function = Function;
    callContext.Context = Context;
    callContext.Completed = 0;
    callContext.Acknowledged = 0;
    
    /* Publish context */
    IpiCallContext = &callContext;
    KeLapicMemoryBarrier();
    
    /* Send IPI to all but self */
    KeSendIpiAllButSelf(IPI_VECTOR_CALL);
    
    /* Execute locally as well */
    Function(Context);
    
    /* Wait for completion if requested */
    if (Wait) {
        /* Wait for all other processors to complete */
        for (i = 0; i < KeProcessorCount; i++) {
            /* Skip self */
            if (KeProcessorApicId[i] == KeGetLapicId()) {
                continue;
            }
            
            /* Wait for this processor - check via acknowledgment tracking */
            /* In a real implementation, we'd track per-processor completion */
        }
        
        /* Simple spin wait for all to complete */
        {
            ULONG timeout = 10000000;
            while (!callContext.Completed && timeout > 0) {
                __asm__ __volatile__("pause" ::: "memory");
                timeout--;
            }
        }
    }
    
    /* Clear context */
    IpiCallContext = NULL;
    KeLapicMemoryBarrier();
}

/* ---- Utility Functions -------------------------------------------------- */

/*
 * KeLapicCpuNumberToId - Convert processor number to APIC ID
 */
ULONG NTAPI KeLapicCpuNumberToId(ULONG ProcessorNumber)
{
    if (ProcessorNumber < MAX_PROCESSORS) {
        return KeProcessorApicId[ProcessorNumber];
    }
    return 0xFFFFFFFF;
}

/*
 * KeLapicIdToCpuNumber - Convert APIC ID to processor number
 */
ULONG NTAPI KeLapicIdToCpuNumber(ULONG ApicId)
{
    ULONG i;
    
    for (i = 0; i < KeProcessorCount && i < MAX_PROCESSORS; i++) {
        if (KeProcessorApicId[i] == ApicId) {
            return i;
        }
    }
    
    return 0xFFFFFFFF;  /* Not found */
}

/*
 * KeSetLapicId - Set APIC ID for current processor (BSP setup)
 *
 * This is used to set the APIC ID of the boot processor.
 */
VOID NTAPI KeSetLapicId(ULONG ApicId)
{
    if (KeProcessorCount == 0) {
        KeProcessorCount = 1;
    }
    KeProcessorApicId[0] = ApicId;
    LapicBootInfo.ApicId = ApicId;
}
