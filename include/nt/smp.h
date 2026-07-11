/*
 * MinNT - smp.h
 * Symmetric Multi-Processing (SMP) support definitions.
 * NT 6.1 kernel style SMP layer for x86-64.
 */

#ifndef _SMP_H_
#define _SMP_H_

#include <nt/ntdef.h>
#include <nt/ke.h>

/* ---- Maximum supported CPUs ------------------------------------------------ */

#define MAXIMUM_PROCESSORS      64
#define MAXIMUM_PROCESSORS_BITS 64

/* ---- CPU Bitmap type (cpumask) --------------------------------------------- */

typedef struct _KAFFINITY_MASK {
    ULONG64 Mask;
} KAFFINITY_MASK, *PKAFFINITY_MASK;

typedef ULONG64 KAFFINITY;

/* ---- APIC Constants --------------------------------------------------------- */

#define APIC_BASE_MSR           0x0000001B
#define APIC_BASE_MSR_ENABLE    0x00000800
#define APIC_BASE_MSR_ADDR_MASK 0xFFFFF000

/* Local APIC register offsets */
#define APIC_ID                 0x20
#define APIC_VER                0x30
#define APIC_TPR                0x80
#define APIC_EOI                0xB0
#define APIC_SVR                0xF0
#define APIC_ESR                0x280
#define APIC_ICR_LOW            0x300
#define APIC_ICR_HIGH           0x310
#define APIC_LVT_TIMER          0x320
#define APIC_LVT_THERMAL        0x330
#define APIC_LVT_PERF           0x340
#define APIC_LVT_LINT0          0x350
#define APIC_LVT_LINT1          0x360
#define APIC_LVT_ERROR          0x370
#define APIC_TIMER_INITCNT      0x380
#define APIC_TIMER_CURRCNT      0x390
#define APIC_TIMER_DIV          0x3E0

/* ICR delivery modes */
#define APIC_DM_FIXED           0x000
#define APIC_DM_LOWEST          0x100
#define APIC_DM_SMI             0x200
#define APIC_DM_NMI             0x400
#define APIC_DM_INIT            0x500
#define APIC_DM_STARTUP         0x600
#define APIC_DM_EXTINT          0x700

/* ICR delivery status */
#define APIC_INT_ASSERT         0x4000
#define APIC_INT_LEVELTRIG      0x8000
#define APIC_ICR_BUSY           0x1000

/* IPI Vectors */
#define IPI_VECTOR_RESCHEDULE   0xF0
#define IPI_VECTOR_CALL_FUNCTION  0xF1
#define IPI_VECTOR_TLB_FLUSH      0xF2
#define IPI_VECTOR_STOP           0xF3
#define IPI_VECTOR_APIC_ERROR     0xFE
#define APIC_SPURIOUS_VECTOR    0xFF

/* IPI Request Flags */
#define IPI_FLAG_WAIT           0x0001
#define IPI_FLAG_ASYNC          0x0002

/* ---- MP Configuration Table ------------------------------------------------ */

#define MPFP_SIGNATURE          0x5F504D5F  /* "_MP_" */
#define MPC_SIGNATURE           0x504D4350  /* "PCMP" */

/* MP CPU entry types */
#define MP_PROCESSOR            0
#define MP_BUS                  1
#define MP_IOAPIC               2
#define MP_IOINTERRUPT          3
#define MP_LOCAL_INTERRUPT      4

/* CPU status flags */
#define CPU_ENABLED             1
#define CPU_BOOTPROCESSOR       2

/* ---- ACPI MADT ------------------------------------------------------------ */

#define ACPI_MADT_SIGNATURE     "APIC"
#define ACPI_MADT_TYPE_LAPIC    0
#define ACPI_MADT_TYPE_IOAPIC   1
#define ACPI_MADT_TYPE_LAPIC_OVERRIDE 2

/* ---- CPU States ------------------------------------------------------------ */

typedef enum _KCPU_STATE {
    CpuStateOffline = 0,
    CpuStateStarting,
    CpuStateOnline,
    CpuStateStopping,
    CpuStateStopped
} KCPU_STATE;

/* ---- Per-CPU IPI Request --------------------------------------------------- */

typedef struct _KIPI_REQUEST {
    LIST_ENTRY ListEntry;
    VOID (NTAPI *Function)(PVOID Argument);
    PVOID Argument;
    ULONG Flags;
    volatile LONG *CompletionFlag;
} KIPI_REQUEST, *PKIPI_REQUEST;

/* ---- Extended KPCR for SMP ------------------------------------------------- */

typedef struct _KIPCR {
    /* Standard KPCR fields (must match KPCR layout) */
    struct _KIPCR *Self;
    struct _KPRCB *Prcb;
    KIRQL Irql;
    ULONG64 UserRsp;
    ULONG64 KernelStackTop;
    ULONG MajorVersion;
    ULONG MinorVersion;
    struct _KPRCB PrcbData;

    /* SMP-specific fields */
    ULONG CpuNumber;
    ULONG ApicId;
    KCPU_STATE CpuState;
    volatile LONG IpiPending;
    LIST_ENTRY IpiQueue;
    KSPIN_LOCK IpiLock;
    PVOID IdleStack;
    PVOID InterruptStack;
    PVOID DpcStack;
    volatile ULONG TlbShootdownPending;
} KIPCR, *PKIPCR;

/* ---- AP Startup Context ---------------------------------------------------- */

typedef struct _AP_STARTUP_CONTEXT {
    ULONG Pml4Base;
    ULONG64 StackBase;
    ULONG64 KpcrBase;
    ULONG CpuNumber;
} AP_STARTUP_CONTEXT, *PAP_STARTUP_CONTEXT;

/* ---- Function Types -------------------------------------------------------- */

typedef VOID (*KIPI_FUNCTION)(PVOID Argument1, PVOID Argument2, PVOID Argument3);
typedef VOID (*KCPU_STARTUP_FUNCTION)(ULONG CpuNumber);

/* ---- MP Table Structures --------------------------------------------------- */

/* MP Floating Pointer Structure */
typedef struct _MP_FLOATING_POINTER {
    ULONG Signature;          /* "_MP_" */
    ULONG MpConfigTable;
    UCHAR Length;
    UCHAR SpecRev;
    UCHAR Checksum;
    UCHAR MpFeatures[5];
} MP_FLOATING_POINTER, *PMP_FLOATING_POINTER;

/* MP Configuration Table Header */
typedef struct _MP_CONFIGURATION_TABLE {
    ULONG Signature;          /* "PCMP" */
    USHORT BaseTableLength;
    UCHAR SpecRev;
    UCHAR Checksum;
    UCHAR OemId[8];
    UCHAR ProductId[12];
    ULONG OemTablePtr;
    USHORT OemTableSize;
    USHORT EntryCount;
    ULONG LapicBase;
    USHORT ExtendedTableLength;
    UCHAR ExtendedTableChecksum;
    UCHAR Reserved;
} MP_CONFIGURATION_TABLE, *PMP_CONFIGURATION_TABLE;

/* MP Processor Entry */
typedef struct _MP_PROCESSOR_ENTRY {
    UCHAR EntryType;        /* 0 = Processor */
    UCHAR LocalApicId;
    UCHAR LocalApicVer;
    UCHAR CpuFlags;         /* bit 0 = enabled, bit 1 = BSP */
    ULONG CpuSignature;
    ULONG FeatureFlags;
    UCHAR Reserved[8];
} MP_PROCESSOR_ENTRY, *PMP_PROCESSOR_ENTRY;

/* ---- External Data --------------------------------------------------------- */

extern ULONG KeNumberProcessors;
extern KAFFINITY KeActiveProcessors;
extern KAFFINITY KeOnlineProcessors;
extern KAFFINITY KeIdleProcessors;

extern PKIPCR KePcrArray[MAXIMUM_PROCESSORS];
extern ULONG KeProcessorApicId[MAXIMUM_PROCESSORS];

/* Physical address of AP trampoline */
extern ULONG KeApStartupPhysical;
extern ULONG KeApStartupSize;

/* BSP (Bootstrap Processor) indicator */
extern BOOLEAN KeIsBootstrapProcessor;

/* ---- CPU Iteration Macros -------------------------------------------------- */

#define FOR_EACH_PROCESSOR(Cpu) \
    for ((Cpu) = 0; (Cpu) < KeNumberProcessors; (Cpu)++)

#define FOR_EACH_ONLINE_PROCESSOR(Cpu) \
    for ((Cpu) = 0; (Cpu) < KeNumberProcessors; (Cpu)++) \
        if (KeIsProcessorOnline(Cpu))

#define KeSetProcessorAffinity(Affinity, Cpu) \
    ((Affinity) |= (1ULL << (Cpu)))

#define KeClearProcessorAffinity(Affinity, Cpu) \
    ((Affinity) &= ~(1ULL << (Cpu)))

#define KeTestProcessorAffinity(Affinity, Cpu) \
    (((Affinity) & (1ULL << (Cpu))) != 0)

/* ---- Error codes ------------------------------------------------------------ */

#define STATUS_SMP_NO_MP_TABLE      ((NTSTATUS)0xC0000200L)
#define STATUS_SMP_NO_ACPI_MADT     ((NTSTATUS)0xC0000201L)
#define STATUS_SMP_AP_START_FAILED  ((NTSTATUS)0xC0000202L)
#define STATUS_SMP_AP_TIMEOUT       ((NTSTATUS)0xC0000203L)
#define STATUS_SMP_TOO_MANY_CPUS    ((NTSTATUS)0xC0000204L)
#define STATUS_SMP_INVALID_CPU      ((NTSTATUS)0xC0000205L)

/* ---- Function Prototypes -------------------------------------------------- */

/* Initialization */
NTSTATUS NTAPI KeSmpInitSystem(VOID);
NTSTATUS NTAPI KeSmpInitializeAp(ULONG CpuNumber, ULONG ApicId);

/* CPU Detection */
NTSTATUS NTAPI KeDetectMpConfiguration(VOID);
NTSTATUS NTAPI KeParseMpTable(PMP_FLOATING_POINTER MpFp);

/* AP Startup */
NTSTATUS NTAPI KeAllocateApTrampoline(VOID);
VOID NTAPI KeSetupApTrampolineContext(ULONG CpuNumber, ULONG ApicId, PVOID Stack, PVOID Kpcr);
NTSTATUS NTAPI KeStartProcessor(ULONG CpuNumber);

/* IPI Functions */
VOID NTAPI KeIpiGenericCall(VOID (NTAPI *Function)(PVOID Argument),
                           PVOID Argument,
                           KAFFINITY TargetProcessors);
VOID NTAPI KeIpiGenericCallAllButSelf(VOID (NTAPI *Function)(PVOID Argument),
                                     PVOID Argument);
VOID NTAPI KeIpiInterruptHandler(PKTRAP_FRAME TrapFrame);

/* TLB Management */
VOID NTAPI KeFlushTbSingle(ULONG_PTR VirtualAddress);
VOID NTAPI KeFlushTbAll(VOID);
VOID NTAPI KeFlushTbMultiple(PVOID *VirtualAddresses, ULONG Count, KAFFINITY TargetProcessors);
VOID NTAPI KeRequestTlbShootdown(PVOID VirtualAddress, KAFFINITY TargetProcessors);

/* CPU State */
BOOLEAN NTAPI KeIsProcessorPresent(ULONG CpuNumber);
BOOLEAN NTAPI KeIsProcessorOnline(ULONG CpuNumber);
VOID NTAPI KeStopProcessor(ULONG CpuNumber);

/* SMP Barrier */
VOID NTAPI KeSmpBarrier(VOID);
VOID NTAPI KeSmpMemoryBarrier(VOID);

/* ---- AP Startup States --------------------------------------------------- */
#define AP_STATE_INIT       0
#define AP_STATE_STARTING   1
#define AP_STATE_READY      2
#define AP_STATE_ONLINE     3
#define AP_STATE_ERROR      4

#endif /* _SMP_H_ */
