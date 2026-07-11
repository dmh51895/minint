/*
 * MinNT - ke/idt.c
 * 64-bit GDT with TSS, full 256-entry IDT, syscall MSR init.
 * Now supports user mode (ring 3) via SYSCALL/SYSRET.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>

/* ---- GDT — includes TSS for user mode ----------------------------------- */

#define GDT_ENTRIES 8
static ULONG64 KiGdt[GDT_ENTRIES] __attribute__((aligned(16))) = {
    0,                                    /* 0x00 null */
    0x00AF9A000000FFFFULL,                /* 0x08 ring0 code64 */
    0x00CF92000000FFFFULL,                /* 0x10 ring0 data   */
    0x00AFFA000000FFFFULL,                /* 0x18 ring3 code64 */
    0x00CFF2000000FFFFULL,                /* 0x20 ring3 data   */
    0, 0,                                 /* 0x28/0x30 TSS (filled at init) */
};

/* 64-bit TSS — 104 bytes, must be filled at runtime.
   MUST be packed: hardware TSS has Rsp0 at offset 4 (after 4-byte Reserved0),
   but an unpacked struct would align Rsp0 to offset 8 due to ULONG64 alignment. */
struct __attribute__((packed)) {
    ULONG Reserved0;
    ULONG64 Rsp0;          /* kernel stack for ring 3→0 traps — offset 4 */
    ULONG64 Rsp1;
    ULONG64 Rsp2;
    ULONG64 Reserved1;
    ULONG64 Ist[7];
    ULONG64 Reserved2;
    USHORT  Reserved3;
    USHORT  IoMapBase;
} KiTss __attribute__((aligned(16)));  /* non-static — referenced by syscall.S */

struct KiDescPtr { USHORT Limit; ULONG64 Base; } __attribute__((packed));

VOID NTAPI KeInitializeGdt(VOID)
{
    ULONG64 tss_base = (ULONG64)&KiTss;
    ULONG64 tss_limit = sizeof(KiTss) - 1;

    /* Fill TSS descriptor at 0x28/0x30.
       64-bit TSS descriptor format:
         Bits 0-15:    Limit 0-15
         Bits 16-39:   Base 0-23
         Bits 40-47:   Type (0x89 = present, 64-bit available TSS)
         Bits 48-55:   Limit 16-19 (0x0, can't exceed 64K)
         Bits 56-63:   Base 24-31
         High 64 bits: Base 32-63 (at GDT+1 slot) */
    KiGdt[5] = (tss_limit & 0xFFFF) |
               ((tss_base & 0xFFFFFF) << 16) |
               (0x89ULL << 40) |
               (((tss_limit >> 16) & 0xF) << 48) |
               (((tss_base >> 24) & 0xFF) << 56);
    KiGdt[6] = (tss_base >> 32);

    /* Zero-initialize TSS */
    for (ULONG i = 0; i < sizeof(KiTss) / sizeof(ULONG); i++)
        ((ULONG*)&KiTss)[i] = 0;

    /* Set initial RSP0 to 0 — KiEnterUserMode will set it for user threads */
    KiTss.Rsp0 = 0;

    struct KiDescPtr gp = { sizeof(KiGdt) - 1, (ULONG64)KiGdt };
    __asm__ __volatile__("lgdt %0" :: "m"(gp));

    /* Load TSS */
    __asm__ __volatile__("ltr %%ax" :: "a"(0x28));

    /* Reload segments */
    __asm__ __volatile__(
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        ::: "rax");
}

/* Update TSS.RSP0 during context switch — called from KiDispatchNextThread */
VOID NTAPI KiSetTssRsp0(ULONG64 Rsp0)
{
    KiTss.Rsp0 = Rsp0;
}

/* ---- IDT ----------------------------------------------------------------- */

typedef struct _KIDTENTRY64 {
    USHORT OffsetLow;
    USHORT Selector;
    UCHAR  IstIndex;
    UCHAR  TypeAttr;
    USHORT OffsetMiddle;
    ULONG  OffsetHigh;
    ULONG  Reserved;
} __attribute__((packed)) KIDTENTRY64;

static KIDTENTRY64 KiIdt[256] __attribute__((aligned(16)));

extern ULONG64 KiTrapTable[256];      /* from trap.S */

static VOID KiSetGate(ULONG Vector, ULONG64 Handler, UCHAR Dpl)
{
    KIDTENTRY64 *e = &KiIdt[Vector];
    e->OffsetLow    = (USHORT)(Handler & 0xFFFF);
    e->Selector     = 0x08;
    e->IstIndex     = 0;
    e->TypeAttr     = (UCHAR)(0x8E | (Dpl << 5));  /* present, interrupt gate, DPL */
    e->OffsetMiddle = (USHORT)((Handler >> 16) & 0xFFFF);
    e->OffsetHigh   = (ULONG)(Handler >> 32);
    e->Reserved     = 0;
}

VOID NTAPI KeInitializeIdt(VOID)
{
    ULONG i;
    struct KiDescPtr ip;

    /* All vectors at DPL0 */
    for (i = 0; i < 256; i++)
        KiSetGate(i, KiTrapTable[i], 0);

    /* Exception vectors 0-31 need to be accessible from ring 3 too */
    for (i = 0; i < 32; i++)
        KiSetGate(i, KiTrapTable[i], 3);

    /* Interrupt vectors 32-255 stay at DPL0 */
    for (i = 32; i < 256; i++)
        KiSetGate(i, KiTrapTable[i], 0);

    ip.Limit = sizeof(KiIdt) - 1;
    ip.Base  = (ULONG64)KiIdt;
    __asm__ __volatile__("lidt %0" :: "m"(ip));
}

/* ---- MSRs — SYSCALL/SYSRET --------------------------------------------- */

#define IA32_STAR  0xC0000081
#define IA32_LSTAR 0xC0000082
#define IA32_FMASK 0xC0000084

extern VOID KiSystemCall64(VOID);   /* from syscall.S */

VOID NTAPI KiInitializeSyscall(VOID)
{
    ULONG64 star = (0x0008ULL << 32) | (0x0018ULL << 48);  /* kernel CS=0x08, user CS=0x18 */
    ULONG64 lstar = (ULONG64)KiSystemCall64;
    ULONG64 fmask = 0x200;  /* mask IF on entry */

    __asm__ __volatile__(
        "wrmsr"
        :: "c"(IA32_STAR), "a"((ULONG)star), "d"((ULONG)(star >> 32))
    );
    __asm__ __volatile__(
        "wrmsr"
        :: "c"(IA32_LSTAR), "a"((ULONG)lstar), "d"((ULONG)(lstar >> 32))
    );
    __asm__ __volatile__(
        "wrmsr"
        :: "c"(IA32_FMASK), "a"((ULONG)fmask), "d"(0)
    );

    /* Enable SCE (SysCall Enable) in EFER */
    ULONG64 efer;
    ULONG efer_low, efer_high;
    __asm__ __volatile__("rdmsr" : "=a"(efer_low), "=d"(efer_high)
        : "c"(0xC0000080));
    efer = (ULONG64)efer_low | ((ULONG64)efer_high << 32);
    efer |= 1;  /* SCE bit */
    efer_low = (ULONG)efer;
    efer_high = (ULONG)(efer >> 32);
    __asm__ __volatile__("wrmsr" :: "c"(0xC0000080),
        "a"(efer_low), "d"(efer_high));

    DbgPrint("KE: syscall MSRs initialized (STAR, LSTAR, FMASK)\n");
}

/* ---- Dispatch ------------------------------------------------------------- */

static PKINTERRUPT_ROUTINE KiInterruptTable[256];

VOID NTAPI KeConnectInterrupt(ULONG Vector, PKINTERRUPT_ROUTINE Routine)
{
    KiInterruptTable[Vector] = Routine;
}

static const CHAR *KiExceptionName(ULONG64 v)
{
    switch (v) {
    case 0:  return "DIVIDE_ERROR";
    case 6:  return "INVALID_OPCODE";
    case 8:  return "DOUBLE_FAULT";
    case 13: return "GENERAL_PROTECTION_FAULT";
    case 14: return "PAGE_FAULT";
    default: return "TRAP";
    }
}

VOID NTAPI KiDispatchInterrupt(PKTRAP_FRAME TrapFrame)
{
    ULONG64 v = TrapFrame->Vector;

    /* Registered handler wins */
    if (KiInterruptTable[v]) {
        KiInterruptTable[v](TrapFrame);
        return;
    }

    if (v < 32) {
        ULONG64 cr2 = 0;
        if (v == 14)
            __asm__ __volatile__("movq %%cr2, %0" : "=r"(cr2));
        DbgPrint("\n*** %s at RIP=%p EFLAGS=%llx ERR=%llx CR2=%llx\n",
                 KiExceptionName(v), (PVOID)TrapFrame->Rip,
                 TrapFrame->EFlags, TrapFrame->ErrorCode, cr2);
        if (v == 14)
            KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                         cr2, TrapFrame->ErrorCode, TrapFrame->Rip, 0);
        if (v == 8)
            KeBugCheckEx(UNEXPECTED_KERNEL_MODE_TRAP,
                         8, 0, 0, 0);
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED,
                     v, TrapFrame->Rip, TrapFrame->ErrorCode, 0);
    }

    /* Spurious hardware interrupt: EOI and move on */
    if (v >= PIC_IRQ_BASE && v < PIC_IRQ_BASE + 16)
        HalEndOfInterrupt((UCHAR)(v - PIC_IRQ_BASE));
}
