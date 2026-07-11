/*
 * MinNT - apic.h
 * Local APIC (LAPIC) driver definitions for SMP support.
 * Implements xAPIC (MMIO-based) with x2APIC detection.
 *
 * Reference: Intel SDM Vol. 3A, Chapter 10 (Local APIC)
 * Compatible with NT 6.1 kernel architecture.
 */

#ifndef _APIC_H_
#define _APIC_H_

#include <nt/ntdef.h>

/* ---- MSR Definitions ----------------------------------------------------- */

#define MSR_IA32_APICBASE               0x0000001B
#define MSR_IA32_APICBASE_BSP           (1 << 8)
#define MSR_IA32_APICBASE_ENABLE        (1 << 11)
#define MSR_IA32_APICBASE_EXTD          (1 << 10)   /* x2APIC mode */
#define MSR_IA32_APICBASE_BASE          0xFFFFF000  /* LAPIC base address mask */

/* x2APIC MSRs */
#define MSR_IA32_X2APIC_APICID          0x802
#define MSR_IA32_X2APIC_VERSION         0x803
#define MSR_IA32_X2APIC_TPR             0x808
#define MSR_IA32_X2APIC_EOI             0x80B
#define MSR_IA32_X2APIC_LDR             0x80D
#define MSR_IA32_X2APIC_SVR             0x80F
#define MSR_IA32_X2APIC_ISR0            0x810
#define MSR_IA32_X2APIC_TMR0            0x818
#define MSR_IA32_X2APIC_IRR0            0x820
#define MSR_IA32_X2APIC_ESR             0x828
#define MSR_IA32_X2APIC_LVT_CMCI        0x82F
#define MSR_IA32_X2APIC_ICR             0x830
#define MSR_IA32_X2APIC_LVT_TIMER       0x832
#define MSR_IA32_X2APIC_LVT_THERMAL     0x833
#define MSR_IA32_X2APIC_LVT_PMI         0x834
#define MSR_IA32_X2APIC_LVT_LINT0       0x835
#define MSR_IA32_X2APIC_LVT_LINT1       0x836
#define MSR_IA32_X2APIC_LVT_ERROR       0x837
#define MSR_IA32_X2APIC_INIT_COUNT      0x838
#define MSR_IA32_X2APIC_CUR_COUNT       0x839
#define MSR_IA32_X2APIC_DIV_CONF        0x83E
#define MSR_IA32_X2APIC_SELF_IPI        0x83F

/* ---- LAPIC Register Offsets (xAPIC MMIO) -------------------------------- */

#define LAPIC_ID                        0x020   /* Local APIC ID Register */
#define LAPIC_VERSION                   0x030   /* Local APIC Version Register */
#define LAPIC_TPR                       0x080   /* Task Priority Register */
#define LAPIC_APR                       0x090   /* Arbitration Priority Register */
#define LAPIC_PPR                       0x0A0   /* Processor Priority Register */
#define LAPIC_EOI                       0x0B0   /* EOI Register */
#define LAPIC_RRD                       0x0C0   /* Remote Read Register */
#define LAPIC_LDR                       0x0D0   /* Logical Destination Register */
#define LAPIC_DFR                       0x0E0   /* Destination Format Register */
#define LAPIC_SVR                       0x0F0   /* Spurious Interrupt Vector Register */
#define LAPIC_ISR0                      0x100   /* In-Service Register (bits 31:0) */
#define LAPIC_ISR1                      0x110   /* In-Service Register (bits 63:32) */
#define LAPIC_ISR2                      0x120   /* In-Service Register (bits 95:64) */
#define LAPIC_ISR3                      0x130   /* In-Service Register (bits 127:96) */
#define LAPIC_ISR4                      0x140   /* In-Service Register (bits 159:128) */
#define LAPIC_ISR5                      0x150   /* In-Service Register (bits 191:160) */
#define LAPIC_ISR6                      0x160   /* In-Service Register (bits 223:192) */
#define LAPIC_ISR7                      0x170   /* In-Service Register (bits 255:224) */
#define LAPIC_TMR0                      0x180   /* Trigger Mode Register */
#define LAPIC_IRR0                      0x200   /* Interrupt Request Register */
#define LAPIC_ESR                       0x280   /* Error Status Register */
#define LAPIC_CMCI                      0x2F0   /* LVT CMCI Register */
#define LAPIC_ICR_LOW                   0x300   /* Interrupt Command Register (low) */
#define LAPIC_ICR_HIGH                  0x310   /* Interrupt Command Register (high) */
#define LAPIC_TIMER                     0x320   /* LVT Timer Register */
#define LAPIC_LVT_TIMER                 0x320   /* LVT Timer Register (alias) */
#define LAPIC_THERMAL                   0x330   /* LVT Thermal Sensor Register */
#define LAPIC_PERF                      0x340   /* LVT Performance Counter Register */
#define LAPIC_LVT_LINT0                0x350   /* LVT LINT0 Register */
#define LAPIC_LVT_LINT1                0x360   /* LVT LINT1 Register */
#define LAPIC_LVT_ERROR                0x370   /* LVT Error Register */
#define LAPIC_TIMER_INITCNT            0x380   /* Initial Count Register */
#define LAPIC_TIMER_CURRCNT            0x390   /* Current Count Register */
#define LAPIC_TIMER_DIV                0x3E0   /* Divide Configuration Register */

/* ---- LAPIC ID Register (0x020) ------------------------------------------ */

#define APIC_ID_MASK                    0xFF000000
#define APIC_ID_SHIFT                   24
#define APIC_ID_MASK_8BIT               0xFFFFFFFF
#define APIC_ID_SHIFT_8BIT              0

/* ---- LAPIC Version Register (0x030) ------------------------------------- */

#define APIC_VERSION_MASK               0x000000FF
#define APIC_VERSION_GET(x)             ((x) & APIC_VERSION_MASK)
#define APIC_MAXLVT_MASK                0x0000FF00
#define APIC_MAXLVT_SHIFT               16
#define APIC_MAXLVT_GET(x)              (((x) >> APIC_MAXLVT_SHIFT) & 0xFF)

/* Check if integrated APIC (as opposed to 82489DX external APIC) */
#define APIC_INTEGRATED(x)              ((x) & 0xF0)

/* ---- Task Priority Register (0x080) ------------------------------------- */

#define APIC_TPRI_MASK                  0x000000FF
#define APIC_TPRI_CLASS_MASK            0xF0
#define APIC_TPRI_SUBCLASS_MASK         0x0F

/* ---- Logical Destination Register (0x0D0) ------------------------------- */

#define APIC_LDR_MASK                   0xFF000000

/* ---- Destination Format Register (0x0E0) -------------------------------- */

#define APIC_DFR_MODEL_MASK             0xF0000000
#define APIC_DFR_CLUSTER                0x00000000
#define APIC_DFR_FLAT                   0xF0000000

/* ---- Spurious Interrupt Vector Register (0x0F0) ------------------------- */

#define APIC_SVR_VECTOR_MASK            0x000000FF
#define APIC_SVR_ENABLE                 0x00000100   /* APIC Software Enable */
#define APIC_SVR_FOCUS_DISABLE          0x00000200   /* Focus Processor Checking Disable */
#define APIC_SVR_EOI_SUPPRESS           0x00001000   /* EOI Broadcast Suppression (x2APIC) */

/* ---- LVT Timer Register (0x320) ---------------------------------------- */

#define APIC_LVT_VECTOR_MASK            0x000000FF
#define APIC_LVT_DM_MASK                0x00000700
#define APIC_LVT_DM_FIXED               0x00000000
#define APIC_LVT_DM_SMI                 0x00000200
#define APIC_LVT_DM_NMI                 0x00000400
#define APIC_LVT_DM_EXTINT              0x00000700
#define APIC_LVT_DM_INIT                0x00000500   /* Reserved for LVT */
#define APIC_LVT_DS_PENDING             0x00001000
#define APIC_LVT_POLARITY               0x00002000
#define APIC_LVT_REMOTE_IRR             0x00004000
#define APIC_LVT_LEVEL_TRIGGER          0x00008000
#define APIC_LVT_MASKED                 0x00010000
#define APIC_LVT_TIMER_MODE_MASK        0x00020000
#define APIC_LVT_TIMER_ONESHOT          0x00000000
#define APIC_LVT_TIMER_PERIODIC         0x00020000
#define APIC_LVT_TIMER_TSCDEADLINE      0x00040000

/* ---- LVT LINT0/LINT1 Registers (0x350, 0x360) --------------------------- */

#define APIC_LVT_PIN_POLARITY_LOW       0x00002000   /* Active low */
#define APIC_LVT_PIN_POLARITY_HIGH      0x00000000   /* Active high */

/* ---- Error Status Register (0x280) -------------------------------------- */

#define APIC_ESR_SEND_CS_ERROR          0x00000001
#define APIC_ESR_RECEIVE_CS_ERROR       0x00000002
#define APIC_ESR_SEND_ACCEPT_ERROR      0x00000004
#define APIC_ESR_RECEIVE_ACCEPT_ERROR   0x00000008
#define APIC_ESR_SEND_ILLEGAL_VECTOR    0x00000020
#define APIC_ESR_RECEIVE_ILLEGAL_VECTOR 0x00000040
#define APIC_ESR_ILLEGAL_ADDRESS        0x00000080

/* ---- Interrupt Command Register (0x300-0x310) -------------------------- */

/* ICR Low Bits */
#define APIC_ICR_VECTOR_MASK            0x000000FF
#define APIC_ICR_DM_MASK                0x00000700
#define APIC_ICR_DM_FIXED               0x00000000
#define APIC_ICR_DM_LOWEST              0x00000100
#define APIC_ICR_DM_SMI                 0x00000200
#define APIC_ICR_DM_NMI                 0x00000400
#define APIC_ICR_DM_INIT                0x00000500
#define APIC_ICR_DM_STARTUP             0x00000600
#define APIC_ICR_DM_EXTINT              0x00000700
/* Compatibility aliases */
#define APIC_DM_FIXED                   APIC_ICR_DM_FIXED
#define APIC_DM_LOWEST                  APIC_ICR_DM_LOWEST
#define APIC_DM_SMI                     APIC_ICR_DM_SMI
#define APIC_DM_NMI                     APIC_ICR_DM_NMI
#define APIC_DM_INIT                    APIC_ICR_DM_INIT
#define APIC_DM_STARTUP                 APIC_ICR_DM_STARTUP
#define APIC_DM_EXTINT                  APIC_ICR_DM_EXTINT
#define APIC_ICR_DS_PENDING             0x00001000
#define APIC_ICR_LEVEL_ASSERT           0x00004000
#define APIC_ICR_LEVEL_DEASSERT         0x00000000
#define APIC_ICR_TRIGGER_LEVEL          0x00008000
#define APIC_ICR_TRIGGER_EDGE            0x00000000
#define APIC_ICR_RR_MASK                0x00030000
#define APIC_ICR_DEST_SELF              0x00040000
#define APIC_ICR_DEST_ALLINC            0x00080000
#define APIC_ICR_DEST_ALLBUT            0x000C0000

/* ICR High Bits */
#define APIC_ICR_DEST_MASK              0xFF000000
#define APIC_ICR_DEST_SHIFT             24

/* ICR Delivery Status */
#define APIC_ICR_IDLE                   0
#define APIC_ICR_SEND_PENDING           1

/* ---- Timer Divide Configuration Register (0x3E0) ------------------------ */

#define APIC_TDR_DIV_1                  0x0000000B
#define APIC_TDR_DIV_2                  0x00000000
#define APIC_TDR_DIV_4                  0x00000001
#define APIC_TDR_DIV_8                  0x00000002
#define APIC_TDR_DIV_16                 0x00000003
#define APIC_TDR_DIV_32                 0x00000008
#define APIC_TDR_DIV_64                 0x00000009
#define APIC_TDR_DIV_128                0x0000000A
#define APIC_TDR_DIV_TMBASE             0x00000004

/* ---- Default Physical Base Address -------------------------------------- */

#define APIC_DEFAULT_PHYS_BASE          0xFEE00000

/* ---- IPI Vectors -------------------------------------------------------- */

/* Fixed vectors used for IPIs (must be >= 0x10, preferably in reserved range) */
#define IPI_VECTOR_BASE                 0xF0
#define IPI_VECTOR_INVALIDATE           0xF0   /* TLB shootdown */
#define IPI_VECTOR_STOP                 0xF1   /* Stop CPU */
#define IPI_VECTOR_CALL                 0xF2   /* Function call */
#define IPI_VECTOR_RESCHEDULE           0xF3   /* Reschedule */
#define IPI_VECTOR_INVALIDATE_END       0xF4   /* TLB shootdown completion */

/* Spurious vector */
#define SPURIOUS_APIC_VECTOR            0xFF

/* Local timer vector */
#define LOCAL_TIMER_VECTOR              0xEF

/* Error vector */
#define ERROR_APIC_VECTOR               0xFE

/* ---- CPUID Features ------------------------------------------------------ */

#define CPUID_FEATURE_APIC              (1 << 9)   /* EDX bit 9 */
#define CPUID_FEATURE_X2APIC            (1 << 21)  /* ECX bit 21 */

/* ---- Status and Configuration ------------------------------------------- */

/* APIC modes */
#define APIC_MODE_DISABLED            0
#define APIC_MODE_XAPIC                 1
#define APIC_MODE_X2APIC                2

/* ---- Data Structures ---------------------------------------------------- */

/* IPI function call structure */
typedef struct _IPI_CALL_CONTEXT {
    VOID (NTAPI *Function)(PVOID Context);
    PVOID Context;
    volatile LONG Completed;
    volatile LONG Acknowledged;
} IPI_CALL_CONTEXT, *PIPI_CALL_CONTEXT;

/* Per-processor APIC info */
typedef struct _LAPIC_INFO {
    ULONG ApicId;                    /* Physical APIC ID */
    ULONG LogicalId;                 /* Logical APIC ID */
    ULONG Version;                   /* APIC version */
    ULONG MaxLvt;                    /* Maximum LVT entries */
    BOOLEAN Integrated;              /* TRUE if integrated APIC */
    BOOLEAN X2Apic;                /* TRUE if in x2APIC mode */
} LAPIC_INFO, *PLAPIC_INFO;

/* ---- Global Variables ---------------------------------------------------- */

/* Initialized during KeInitializeLapic() */
extern volatile ULONG *LapicBase;     /* MMIO base (NULL if x2APIC) */
extern ULONG LapicMode;               /* Current APIC mode */
extern LAPIC_INFO LapicBootInfo;      /* BSP APIC info */

/* Per-processor APIC IDs (indexed by processor number) */
#define MAX_PROCESSORS                  64
extern ULONG KeProcessorApicId[MAX_PROCESSORS];
extern ULONG KeProcessorCount;

/* ---- Function Prototypes ----------------------------------------------- */

/* Initialization */
VOID NTAPI KeInitializeLapic(VOID);
VOID NTAPI KeInitializeLapicForProcessor(ULONG ProcessorNumber);
BOOLEAN NTAPI KeDetectLapic(VOID);
BOOLEAN NTAPI KeDetectX2Apic(VOID);
VOID NTAPI KeSetLapicId(ULONG ApicId);
ULONG NTAPI KeGetLapicVersion(VOID);

/* Register access (xAPIC MMIO) */
FORCEINLINE ULONG LapicRead(ULONG Offset);
FORCEINLINE VOID LapicWrite(ULONG Offset, ULONG Value);

/* MSR-based x2APIC access */
FORCEINLINE ULONG64 LapicReadMsr(ULONG Msr);
FORCEINLINE VOID LapicWriteMsr(ULONG Msr, ULONG64 Value);

/* EOI - End of Interrupt */
VOID NTAPI KeLapicEoi(VOID);

/* IPI - Inter-Processor Interrupts */
VOID NTAPI KeSendIpi(ULONG ApicId, ULONG Vector);
VOID NTAPI KeSendIpiAll(ULONG Vector);
VOID NTAPI KeSendIpiAllButSelf(ULONG Vector);
VOID NTAPI KeSendIpiSelf(ULONG Vector);
VOID NTAPI KeSendIpiMask(PULONG ApicIdMask, ULONG Vector);

/* IPI with delivery mode */
VOID NTAPI KeSendIpiEx(ULONG ApicId, ULONG Vector, ULONG DeliveryMode);

/* IPI handlers */
VOID NTAPI KeIpiInvalidateHandler(PKTRAP_FRAME TrapFrame);
VOID NTAPI KeIpiStopHandler(PKTRAP_FRAME TrapFrame);
VOID NTAPI KeIpiCallHandler(PKTRAP_FRAME TrapFrame);
VOID NTAPI KeIpiRescheduleHandler(PKTRAP_FRAME TrapFrame);

/* IPI operations */
VOID NTAPI KeInvalidateAllTlbs(VOID);
VOID NTAPI KeStopAllProcessors(VOID);
VOID NTAPI KeCallFunctionOnProcessor(ULONG ProcessorNumber, 
                                       VOID (NTAPI *Function)(PVOID),
                                       PVOID Context);
VOID NTAPI KeCallFunctionOnAllProcessors(
    VOID (NTAPI *Function)(PVOID),
    PVOID Context,
    BOOLEAN Wait);

/* Utility functions */
ULONG NTAPI KeLapicCpuNumberToId(ULONG ProcessorNumber);
ULONG NTAPI KeLapicIdToCpuNumber(ULONG ApicId);
BOOLEAN NTAPI KeIsLapicPresent(VOID);
VOID NTAPI KeLapicWaitForIcrIdle(VOID);

/* ---- Inline Helpers ---------------------------------------------------- */

/* Memory barrier for APIC ordering */
FORCEINLINE VOID KeLapicMemoryBarrier(VOID)
{
    __asm__ __volatile__("mfence" ::: "memory");
}

FORCEINLINE VOID KeLapicMemoryBarrierRead(VOID)
{
    __asm__ __volatile__("lfence" ::: "memory");
}

FORCEINLINE VOID KeLapicMemoryBarrierWrite(VOID)
{
    __asm__ __volatile__("sfence" ::: "memory");
}

/* xAPIC MMIO register access */
FORCEINLINE ULONG LapicRead(ULONG Offset)
{
    volatile ULONG *reg = (volatile ULONG *)((PUCHAR)LapicBase + Offset);
    return *reg;
}

FORCEINLINE VOID LapicWrite(ULONG Offset, ULONG Value)
{
    volatile ULONG *reg = (volatile ULONG *)((PUCHAR)LapicBase + Offset);
    *reg = Value;
    KeLapicMemoryBarrier();
}

/* x2APIC MSR access */
FORCEINLINE ULONG64 LapicReadMsr(ULONG Msr)
{
    ULONG Low, High;
    __asm__ __volatile__("rdmsr" : "=a"(Low), "=d"(High) : "c"(Msr));
    return ((ULONG64)High << 32) | Low;
}

FORCEINLINE VOID LapicWriteMsr(ULONG Msr, ULONG64 Value)
{
    ULONG Low = (ULONG)(Value & 0xFFFFFFFF);
    ULONG High = (ULONG)(Value >> 32);
    __asm__ __volatile__("wrmsr" :: "a"(Low), "d"(High), "c"(Msr));
}

/* Get current LAPIC ID */
FORCEINLINE ULONG KeGetLapicId(VOID)
{
    if (LapicMode == APIC_MODE_X2APIC) {
        return (ULONG)LapicReadMsr(MSR_IA32_X2APIC_APICID);
    }
    if (LapicBase) {
        return (LapicRead(LAPIC_ID) >> APIC_ID_SHIFT) & 0xFF;
    }
    return 0;
}

/* Quick EOI (use from interrupt handlers) */
FORCEINLINE VOID KeLapicEoiQuick(VOID)
{
    if (LapicMode == APIC_MODE_X2APIC) {
        LapicWriteMsr(MSR_IA32_X2APIC_EOI, 0);
    } else if (LapicBase) {
        LapicWrite(LAPIC_EOI, 0);
    }
}

#endif /* _APIC_H_ */
