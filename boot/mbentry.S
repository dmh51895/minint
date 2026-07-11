/*
 * MinNT - boot/mbentry.S
 * Multiboot2 entry. GRUB drops us at _start in 32-bit protected mode,
 * paging off. We build identity page tables for the low 1GB (2MB pages),
 * flip on PAE + LME + PG, load a 64-bit GDT, and long-jump into
 * KiSystemStartup with the multiboot info pointer in RDI (SysV arg0).
 *
 * NT parallel: this file is winload.exe + the very top of ntoskrnl's
 * KiSystemStartup asm thunk compressed into one place.
 */

.set MB2_MAGIC,    0xE85250D6
.set MB2_ARCH,     0                 /* i386 protected mode */
.set MB2_HDRLEN,   (mb2_header_end - mb2_header)
.set MB2_CHECKSUM, -(MB2_MAGIC + MB2_ARCH + MB2_HDRLEN)

.section .multiboot, "a"
.align 8
mb2_header:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long MB2_HDRLEN
    .long MB2_CHECKSUM
    /* framebuffer tag: request 1024x768x32 */
    .align 8
    .word 5                 /* type: framebuffer */
    .word 0                 /* flags: not optional */
    .long 20                /* size */
    .long 1024              /* width */
    .long 768               /* height */
    .long 32                /* bpp */
    /* end tag */
    .align 8
    .word 0
    .word 0
    .long 8
mb2_header_end:

/* ------------------------------------------------------------------ */
.section .bss
.align 4096
pml4:   .skip 4096
pdpt:   .skip 4096
pd0:    .skip 4096      /* covers 0GB - 1GB   */
pd1:    .skip 4096      /* covers 1GB - 2GB   */
pd2:    .skip 4096      /* covers 2GB - 3GB   */
pd3:    .skip 4096      /* covers 3GB - 4GB   */
.align 16
stack_bottom:
    .skip 32768                       /* 32K boot stack */
stack_top:

.section .rodata
.align 16
gdt64:
    .quad 0                            /* null                        */
    .quad 0x00AF9A000000FFFF           /* 0x08: 64-bit ring0 code     */
    .quad 0x00CF92000000FFFF           /* 0x10: ring0 data            */
gdt64_end:
gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .quad gdt64

/* ------------------------------------------------------------------ */
.section .text
.code32
.global _start
.type _start, @function
_start:
    cli
    movl %ebx, mb2_info_ptr           /* stash multiboot2 info (phys) */

    /* Build page tables: identity map up to 4GB with 2MB pages */
    /* PML4[0..3] -> PDPT entries, each covering 1GB */
    movl $pdpt, %eax
    orl  $0x3, %eax                   /* present | write              */
    movl %eax, pml4
    movl %eax, pml4 + 8               /* also at PML4[1] for >1GB     */
    movl %eax, pml4 + 16              /* PML4[2]                      */
    movl %eax, pml4 + 24              /* PML4[3]                      */

    /* PDPT[0] -> pd0, PDPT[1] -> pd1, etc */
    movl $pd0, %eax
    orl  $0x3, %eax
    movl %eax, pdpt
    movl $pd1, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 8
    movl $pd2, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 16
    movl $pd3, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 24

    /* Fill each PD with 512 2MB page entries */
    movl $0, %ebx                     /* PD index (0 = pd0, 1 = pd1...) */
    movl $4, %ebp                     /* 4 PDs total */
1:  movl $0, %ecx                     /* entry index within PD */
    movl %ebx, %eax
    shll $30, %eax                    /* base = pd_idx * 1GB */
    movl %eax, %esi                   /* save base */
    addl $0x83, %eax                  /* present | write | large */
2:  /* Compute address of PD[ebx][ecx] */
    movl %ebx, %edx
    shll $12, %edx                    /* pd_idx * 4096 */
    addl $pd0, %edx
    movl %eax, (%edx,%ecx,8)          /* PD entry */
    movl $0, 4(%edx,%ecx,8)
    addl $0x200000, %eax              /* next 2MB */
    incl %ecx
    cmpl $512, %ecx
    jne  2b
    incl %ebx
    cmpl %ebp, %ebx
    jne  1b

    /* Enable PAE */
    movl %cr4, %eax
    orl  $(1 << 5), %eax
    movl %eax, %cr4

    /* Load CR3 */
    movl $pml4, %eax
    movl %eax, %cr3

    /* Set EFER.LME (and NXE for PTE_NX support) */
    movl $0xC0000080, %ecx
    rdmsr
    orl  $((1 << 8) | (1 << 11)), %eax
    wrmsr

    /* Initialize IA32_PAT MSR for proper cache control
     * PAT entries:
     *   PA0 = WB (Write-Back)       - standard RAM
     *   PA1 = WT (Write-Through)    - not used
     *   PA2 = UC (UnCached)         - MMIO regions
     *   PA3 = UC- (UnCached minus)  - not used
     *   PA4 = WB (Write-Back)       - standard RAM
     *   PA5 = UC (UnCached)         - MMIO via PAT bit
     *   PA6 = WT (Write-Through)    - not used
     *   PA7 = UC (UnCached)         - BIOS/ROM regions
     * Without this, all memory defaults to WB and MMIO regions like
     * VGA framebuffer (0xA0000-0xC0000) and BIOS (0xF0000-0x100000)
     * are cached, causing #UD exceptions on access and coherency bugs.
     */
    movl $0x277, %ecx                /* IA32_PAT MSR */
    /* PAT values per Intel SDM: UC=0, WC=1, Rsvd=2-3, WT=4, WP=5, WB=6, UC-=7
     * Build QWORD: [PA7][PA6][PA5][PA4] in EDX, [PA3][PA2][PA1][PA0] in EAX
     * All 8 entries: PA0=WB, PA1=WT, PA2=UC, PA3=UC-, PA4=WB, PA5=UC, PA6=WT, PA7=UC-
     * Layout: EDX = 0x07060406 (PA7=UC-, PA6=WT, PA5=UC, PA4=WB)
     *         EAX = 0x07060400 (PA3=UC-, PA2=UC, PA1=WT, PA0=WB) */
    movl $0x07060400, %eax           /* [PA3=UC-][PA2=UC][PA1=WT][PA0=WB] */
    movl $0x07060406, %edx           /* [PA7=UC-][PA6=WT][PA5=UC][PA4=WB] */
    wrmsr

    /* Enable paging -> we are now in compatibility mode */
    movl %cr0, %eax
    orl  $(1 << 31), %eax
    movl %eax, %cr0

    /* Load 64-bit GDT and far-jump into long mode */
    lgdt gdt64_ptr
    ljmp $0x08, $long_mode_entry

.code64
long_mode_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    movq $stack_top, %rsp
    xorq %rbp, %rbp

    /* SysV: arg0 in RDI = multiboot2 info pointer */
    movl mb2_info_ptr(%rip), %edi
    call KiSystemStartup

    /* KiSystemStartup never returns; belt and suspenders: */
2:  cli
    hlt
    jmp 2b

.section .data
.align 8
mb2_info_ptr: .long 0

.section .note.GNU-stack,"",@progbits
