/*
 * MinNT - boot/setupldr_entry.S
 * Multiboot2 entry for the setup loader.
 *
 * GRUB drops us in 32-bit protected mode with paging off. We build
 * identity-mapped page tables for the low 2GB (enough for a 1GB
 * kernel), flip on PAE + LME + PG, and long-jump to SetupLoaderMain
 * with the multiboot info pointer in RDI.
 */

.set MB2_MAGIC,    0xE85250D6
.set MB2_ARCH,     0
.set MB2_HDRLEN,   (mb2_header_end - mb2_header)
.set MB2_CHECKSUM, -(MB2_MAGIC + MB2_ARCH + MB2_HDRLEN)

.section .multiboot, "a"
.align 8
mb2_header:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long MB2_HDRLEN
    .long MB2_CHECKSUM
    /* framebuffer tag: request 1920x1080x32 */
    .align 8
    .word 5
    .word 0
    .long 20
    .long 1024
    .long 768
    .long 32
    /* end tag */
    .align 8
    .word 0
    .word 0
    .long 8
mb2_header_end:

.section .text
.code32
.global _start
.type _start, @function
_start:
    cli
    movl %ebx, %edi

    /* PML4[0] -> PDPT (covers low 1GB) */
    movl $setupldr_pdpt, %eax
    orl  $0x3, %eax
    movl %eax, setupldr_pml4
    /* PML4[1] -> PDPT (covers 1GB-2GB, where our kernel lives at 1MB) */
    movl %eax, setupldr_pml4 + 8

    /* PDPT[0] -> PD (covers low 1GB) */
    movl $setupldr_pd, %eax
    orl  $0x3, %eax
    movl %eax, setupldr_pdpt
    /* PDPT[1] -> PD (covers 1GB-2GB) */
    movl %eax, setupldr_pdpt + 8

    /* PD entries: 2MB pages, present+writable */
    movl $0x000083, %eax
    movl %eax, setupldr_pd
    movl $0x200083, %eax
    movl %eax, setupldr_pd + 8
    movl $0x400083, %eax
    movl %eax, setupldr_pd + 16
    movl $0x600083, %eax
    movl %eax, setupldr_pd + 24

    /* Load PML4 address into CR3 */
    movl $setupldr_pml4, %eax
    movl %eax, %cr3

    /* Enable PAE + LME in EFER */
    movl $0xC0000080, %ecx
    rdmsr
    orl  $0x900, %eax
    wrmsr

    /* Enable paging */
    movl %cr0, %eax
    orl  $0x80000000, %eax
    movl %eax, %cr0

    /* Load 64-bit GDT and long-jump into setupldr_long */
    lgdt setupldr_gdt_pointer
    ljmp $0x08, $setupldr_long

.code64
setupldr_long:
    /* Sanity print to COM1 - confirms long mode is up. */
    movq $0x3F8, %rdx
    movb $'S', %al
    outb %al, %dx
    movb $'L', %al
    outb %al, %dx
    movb $'\n', %al
    outb %al, %dx

    /* Set up our own stack. GRUB leaves us with whatever stack
     * it was using, which is small and shared with other GRUB
     * bookkeeping. Give the kernel a dedicated 64KB stack.
     *
     * SysV ABI requires RSP+8 == 0 mod 16 at function entry. The
     * CALL pushes 8 bytes (return address), so we need RSP mod
     * 16 == 8 right before CALL. We subtract 8 to leave room for
     * the implicit CALL push. */
    leaq setupldr_stack_top, %rsp
    subq $8, %rsp

    /* RDI holds the multiboot2 info pointer (passed by GRUB).
     * The BSS zeroing loop below uses RDI as its pointer, so save
     * it in R12 first. */
    movq %rdi, %r12

    /* Zero BSS */
    movq $setupldr_bss_start, %rdi
    movq $setupldr_bss_end, %rcx
    subq %rdi, %rcx
    shrq $3, %rcx
    xorq %rax, %rax
    rep stosq

    /* Restore multiboot info pointer for SetupLoaderMain. */
    movq %r12, %rdi

    call SetupLoaderMain
1:  hlt
    jmp 1b

.section .bss
.align 4096
setupldr_pml4: .skip 4096
setupldr_pdpt: .skip 4096
setupldr_pd:   .skip 8192
setupldr_bss_start:
    .skip 16
    /* Dedicated 64KB stack for the kernel. */
    .skip 65536
setupldr_stack_top:
setupldr_bss_end:

.section .data
.align 16
setupldr_gdt:
    .quad 0                            /* null descriptor              */
    .quad 0x00AF9A000000FFFF          /* 0x08: 64-bit ring0 code       */
    .quad 0x00CF92000000FFFF          /* 0x10: 64-bit ring0 data       */
setupldr_gdt_pointer:
    .word . - setupldr_gdt - 1
    .quad setupldr_gdt
