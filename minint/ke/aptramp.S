/*
 * MinNT - ke/aptramp.S
 * Application Processor (AP) startup trampoline code.
 *
 * This code is copied to low memory (<1MB) and executed by APs in real mode
 * when they receive the STARTUP IPI from the BSP.
 *
 * Flow:
 *   1. Start in 16-bit real mode
 *   2. Switch to 32-bit protected mode with flat segments
 *   3. Enable PAE, load PML4, enable long mode (EFER.LME)
 *   4. Enable paging to activate long mode
 *   5. Jump to 64-bit code
 *   6. Load GS base with KPCR
 *   7. Call KiApStartup() in C code
 */

    .code16
    .globl KiApTrampoline16
    .type KiApTrampoline16, @function
    .align 16

/*
 * KiApTrampoline16 - 16-bit real mode entry point
 * Physical address: 0x90000 (or other low memory location < 1MB)
 *
 * AP enters here in real mode with:
 *   - CS:IP pointing to this code
 *   - Unknown segment values
 *   - Interrupts disabled
 */
KiApTrampoline16:
    cli                     /* Disable interrupts */
    cld                     /* Clear direction flag */

    /* DEBUG: Write to COM1 serial port to show we're alive */
    /* This executes in real mode at 0x90000 */
    movb    $'A', %al       /* 'A' = AP started */
    movw    $0x3F8, %dx     /* COM1 port */
    outb    %al, %dx        /* Send character */

    /* Setup segments */
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss

    /* Setup temporary stack at 0x9F000 (below this code) */
    movw    $0x9F00, %sp

    /* DEBUG: Segments setup done */
    movb    $'B', %al
    movw    $0x3F8, %dx
    outb    %al, %dx

    /* Load GDT for protected mode */
    lgdtl   %cs:ApGdtDesc - KiApTrampoline16

    /* DEBUG: GDT loaded */
    movb    $'C', %al
    movw    $0x3F8, %dx
    outb    %al, %dx

    /* Enable A20 line (using fast A20) */
    inb     $0x92, %al
    orb     $0x02, %al
    outb    %al, $0x92

    /* DEBUG: A20 enabled */
    movb    $'D', %al
    movw    $0x3F8, %dx
    outb    %al, %dx

    /* Enable protected mode (CR0.PE = 1) */
    movl    %cr0, %eax
    orl     $0x00000001, %eax
    movl    %eax, %cr0

    /* DEBUG: Protected mode enabled */
    movb    $'E', %al
    movw    $0x3F8, %dx
    outb    %al, %dx

    /* Far jump to 32-bit protected mode code using operand-size prefix */
    /* Manually encode: ljmpl $0x08, $(KiApTrampoline32 - KiApTrampoline16) */
    .byte 0x66              /* Operand-size prefix (32-bit) */
    .byte 0xEA              /* JMP ptr16:32 opcode */
    .long (KiApTrampoline32 - KiApTrampoline16)  /* 32-bit offset */
    .word 0x0008            /* 16-bit selector (code segment)

    .size KiApTrampoline16, . - KiApTrampoline16

/* ---- 32-bit Protected Mode Code ------------------------------------------ */

    .code32
    .globl KiApTrampoline32
    .type KiApTrampoline32, @function
    .align 16

/*
 * KiApTrampoline32 - 32-bit protected mode entry
 *
 * Now in 32-bit protected mode with flat segments.
 * Next steps:
 *   - Enable PAE (CR4.PAE = 1)
 *   - Load PML4 into CR3
 *   - Enable long mode (EFER.LME = 1)
 *   - Enable paging (CR0.PG = 1) to activate long mode
 *   - Jump to 64-bit code
 */
KiApTrampoline32:
    /* DEBUG: In 32-bit protected mode */
    movb    $'F', %al
    movw    $0x3F8, %dx
    outb    %al, %dx

    /* Load 32-bit data segments */
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    /* DEBUG: Segments loaded */
    movb    $'G', %al
    movw    $0x3F8, %dx
    outb    %al, %dx

    /* Enable PAE (CR4.PAE = 1) */
    movl    %cr4, %eax
    orl     $0x00000020, %eax
    movl    %eax, %cr4

    /* DEBUG: PAE enabled */
    movb    $'H', %al
    movw    $0x3F8, %dx
    outb    %al, %dx

    /* Load PML4 physical address from startup context */
    /* The context is at ApStartupContext (defined below) */
    movl    ApPml4Base - KiApTrampoline16, %eax
    movl    %eax, %cr3

    /* DEBUG: PML4 loaded */
    movb    $'I', %al
    movw    $0x3F8, %dx
    outb    %al, %dx

    /* Enable long mode (EFER.LME = 1) */
    movl    $0xC0000080, %ecx       /* EFER MSR */
    rdmsr
    orl     $0x00000100, %eax       /* LME bit */
    wrmsr

    /* DEBUG: Long mode enabled in EFER */
    movb    $'J', %al
    movw    $0x3F8, %dx
    outb    %al, %dx

    /* Enable paging to activate long mode (CR0.PG = 1) */
    movl    %cr0, %eax
    orl     $0x80000000, %eax
    movl    %eax, %cr0

    /* DEBUG: Paging enabled, now in compatibility mode */
    movb    $'K', %al
    movw    $0x3F8, %dx
    outb    %al, %dx

    /* Now in compatibility mode, jump to 64-bit code */
    /* Manually encode: ljmpl $0x08, $(KiApTrampoline64 - KiApTrampoline16) */
    .byte 0x48              /* REX.W prefix (64-bit operand) - optional for jumps */
    .byte 0xEA              /* JMP ptr16:32 opcode (in compat mode, this is ptr16:32) */
    .long (KiApTrampoline64 - KiApTrampoline16)  /* 32-bit offset */
    .word 0x0008            /* 16-bit selector */

    .size KiApTrampoline32, . - KiApTrampoline32

/* ---- 64-bit Long Mode Code ------------------------------------------------ */

    .code64
    .globl KiApTrampoline64
    .type KiApTrampoline64, @function
    .align 16

/*
 * KiApTrampoline64 - 64-bit long mode entry
 *
 * Now in full 64-bit mode. We can:
 *   - Use 64-bit registers
 *   - Access GS-relative data (KPCR)
 *   - Call 64-bit C code
 *
 * Setup stack and call KiApStartup
 */
KiApTrampoline64:
    /* DEBUG: In 64-bit long mode - write to COM1 via MMIO */
    /* COM1 is at I/O port 0x3F8, use inline outb */
    movw    $0x3F8, %dx
    movb    $'L', %al
    outb    %al, %dx

    /* Load 64-bit data segments */
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    /* DEBUG: 64-bit segments loaded */
    movw    $0x3F8, %dx
    movb    $'M', %al
    outb    %al, %dx

    /* Load stack from startup context */
    movq    ApStackBase - KiApTrampoline16(%rip), %rsp

    /* DEBUG: Stack loaded */
    movw    $0x3F8, %dx
    movb    $'N', %al
    outb    %al, %dx

    /* Save context registers that might be needed */
    pushq   %rbx
    pushq   %rbp
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15

    /* Load GS base with KPCR pointer */
    movq    ApKpcrBase - KiApTrampoline16(%rip), %rax
    movl    $0xC0000101, %ecx       /* IA32_GS_BASE MSR */
    movl    %eax, %edx
    shrq    $32, %rax
    wrmsr

    /* Call C entry point */
    call    KiApStartup

    /* Should never return, but halt if it does */
ApHalt:    hlt
    jmp     ApHalt

    .size KiApTrampoline64, . - KiApTrampoline64

/* ---- Data Section --------------------------------------------------------- */

    .align 16

/*
 * Global Descriptor Table (GDT) for AP startup
 * Must be located in the same low memory page as the trampoline code
 */
ApGdt:
    /* Null descriptor (0x00) */
    .quad   0x0000000000000000

    /* 32-bit/64-bit Code Segment (0x08) - executable, read-only, present */
    /* Base=0, Limit=4GB, Type=0xA (Execute/Read), S=1, DPL=0, P=1 */
    /* G=1 (4KB granularity), D=1 (32-bit operands), L=1 (64-bit mode for long mode) */
    .quad   0x00AF9A000000FFFF       /* 64-bit code segment */

    /* 32-bit/64-bit Data Segment (0x10) - read/write, present */
    /* Base=0, Limit=4GB, Type=0x2 (Read/Write), S=1, DPL=0, P=1 */
    /* G=1, D/B=1, L=0 (not code) */
    .quad   0x00CF92000000FFFF       /* Data segment */

ApGdtEnd:

/* GDT descriptor for LGDT instruction */
ApGdtDesc:
    .word   ApGdtEnd - ApGdt - 1    /* Limit (size - 1) */
    .long   ApGdt - KiApTrampoline16 /* Base (offset from trampoline start) */

/* ---- Startup Context Data ------------------------------------------------- */

/* These values are filled in by the BSP before starting the AP */

    .align 8

/* PML4 base physical address */
ApPml4Base:
    .long   0x00000000
    .long   0x00000000

/* Initial kernel stack pointer for AP */
ApStackBase:
    .quad   0x0000000000000000

/* KPCR virtual address for GS base */
ApKpcrBase:
    .quad   0x0000000000000000

/* CPU number */
ApCpuNumber:
    .long   0x00000000

/* APIC ID */
ApApicId:
    .long   0x00000000

/* Startup state (used for synchronization) */
ApStartupState:
    .long   0x00000000

/* Error code */
ApErrorCode:
    .long   0x00000000

/* ---- Trampoline Size ------------------------------------------------------ */

/* Ensure trampoline fits in one page (4KB) */
    .org    0x1000
KiApTrampolineEnd:

/* ---- Exported Symbols ------------------------------------------------------- */

    .globl KiApTrampolineEnd
    .globl ApGdt
    .globl ApGdtDesc
    .globl ApPml4Base
    .globl ApStackBase
    .globl ApKpcrBase
    .globl ApCpuNumber
    .globl ApApicId
    .globl ApStartupState
    .globl ApErrorCode