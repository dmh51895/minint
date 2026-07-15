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
    /* framebuffer tag: request 1920x1080x32 */
    .align 8
    .word 5                 /* type: framebuffer */
    .word 0                 /* flags: not optional */
    .long 20                /* size */
    .long 1920              /* width */
    .long 1080              /* height */
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
pd4:    .skip 4096      /* covers 4GB - 5GB (for APIC at 0xFEE00000) */
pd5:    .skip 4096      /* covers 5GB - 6GB   */
pd6:    .skip 4096      /* covers 6GB - 7GB   */
pd7:    .skip 4096      /* covers 7GB - 8GB   */
pd8:    .skip 4096      /* covers 8GB - 9GB   */
pd9:    .skip 4096      /* covers 9GB - 10GB  */
pd10:   .skip 4096      /* covers 10GB - 11GB */
pd11:   .skip 4096      /* covers 11GB - 12GB */
pd12:   .skip 4096      /* covers 12GB - 13GB */
pd13:   .skip 4096      /* covers 13GB - 14GB */
pd14:   .skip 4096      /* covers 14GB - 15GB */
pd15:   .skip 4096      /* covers 15GB - 16GB */
pd16:   .skip 4096      /* covers 16GB - 17GB */
pd17:   .skip 4096      /* covers 17GB - 18GB */
pd18:   .skip 4096      /* covers 18GB - 19GB */
pd19:   .skip 4096      /* covers 19GB - 20GB */
pd20:   .skip 4096      /* covers 20GB - 21GB */
pd21:   .skip 4096      /* covers 21GB - 22GB */
pd22:   .skip 4096      /* covers 22GB - 23GB */
pd23:   .skip 4096      /* covers 23GB - 24GB */
pd24:   .skip 4096      /* covers 24GB - 25GB */
pd25:   .skip 4096      /* covers 25GB - 26GB */
pd26:   .skip 4096      /* covers 26GB - 27GB */
pd27:   .skip 4096      /* covers 27GB - 28GB */
pd28:   .skip 4096      /* covers 28GB - 29GB */
pd29:   .skip 4096      /* covers 29GB - 30GB */
pd30:   .skip 4096      /* covers 30GB - 31GB */
pd31:   .skip 4096      /* covers 31GB - 32GB */
pd32:   .skip 4096      /* covers 32GB - 33GB */
pd33:   .skip 4096      /* covers 33GB - 34GB */
pd34:   .skip 4096      /* covers 34GB - 35GB */
pd35:   .skip 4096      /* covers 35GB - 36GB */
pd36:   .skip 4096      /* covers 36GB - 37GB */
pd37:   .skip 4096      /* covers 37GB - 38GB */
pd38:   .skip 4096      /* covers 38GB - 39GB */
pd39:   .skip 4096      /* covers 39GB - 40GB */
pd40:   .skip 4096      /* covers 40GB - 41GB */
pd41:   .skip 4096      /* covers 41GB - 42GB */
pd42:   .skip 4096      /* covers 42GB - 43GB */
pd43:   .skip 4096      /* covers 43GB - 44GB */
pd44:   .skip 4096      /* covers 44GB - 45GB */
pd45:   .skip 4096      /* covers 45GB - 46GB */
pd46:   .skip 4096      /* covers 46GB - 47GB */
pd47:   .skip 4096      /* covers 47GB - 48GB */
pd48:   .skip 4096      /* covers 48GB - 49GB */
pd49:   .skip 4096      /* covers 49GB - 50GB */
pd50:   .skip 4096      /* covers 50GB - 51GB */
pd51:   .skip 4096      /* covers 51GB - 52GB */
pd52:   .skip 4096      /* covers 52GB - 53GB */
pd53:   .skip 4096      /* covers 53GB - 54GB */
pd54:   .skip 4096      /* covers 54GB - 55GB */
pd55:   .skip 4096      /* covers 55GB - 56GB */
pd56:   .skip 4096      /* covers 56GB - 57GB */
pd57:   .skip 4096      /* covers 57GB - 58GB */
pd58:   .skip 4096      /* covers 58GB - 59GB */
pd59:   .skip 4096      /* covers 59GB - 60GB */
pd60:   .skip 4096      /* covers 60GB - 61GB */
pd61:   .skip 4096      /* covers 61GB - 62GB */
pd62:   .skip 4096      /* covers 62GB - 63GB */
pd63:   .skip 4096      /* covers 63GB - 64GB */
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

    /* Build page tables: identity map up to 64GB with 2MB pages */
    /* PML4[0..63] -> PDPT entries, each covering 1GB */
    movl $pdpt, %eax
    orl  $0x3, %eax                   /* present | write              */
    movl %eax, pml4
    movl %eax, pml4 + 8               /* PML4[1] */
    movl %eax, pml4 + 16              /* PML4[2] */
    movl %eax, pml4 + 24              /* PML4[3] */
    movl %eax, pml4 + 32              /* PML4[4] */
    movl %eax, pml4 + 40              /* PML4[5] */
    movl %eax, pml4 + 48              /* PML4[6] */
    movl %eax, pml4 + 56              /* PML4[7] */
    movl %eax, pml4 + 64              /* PML4[8] */
    movl %eax, pml4 + 72              /* PML4[9] */
    movl %eax, pml4 + 80              /* PML4[10] */
    movl %eax, pml4 + 88              /* PML4[11] */
    movl %eax, pml4 + 96              /* PML4[12] */
    movl %eax, pml4 + 104             /* PML4[13] */
    movl %eax, pml4 + 112             /* PML4[14] */
    movl %eax, pml4 + 120             /* PML4[15] */
    movl %eax, pml4 + 128             /* PML4[16] */
    movl %eax, pml4 + 136             /* PML4[17] */
    movl %eax, pml4 + 144             /* PML4[18] */
    movl %eax, pml4 + 152             /* PML4[19] */
    movl %eax, pml4 + 160             /* PML4[20] */
    movl %eax, pml4 + 168             /* PML4[21] */
    movl %eax, pml4 + 176             /* PML4[22] */
    movl %eax, pml4 + 184             /* PML4[23] */
    movl %eax, pml4 + 192             /* PML4[24] */
    movl %eax, pml4 + 200             /* PML4[25] */
    movl %eax, pml4 + 208             /* PML4[26] */
    movl %eax, pml4 + 216             /* PML4[27] */
    movl %eax, pml4 + 224             /* PML4[28] */
    movl %eax, pml4 + 232             /* PML4[29] */
    movl %eax, pml4 + 240             /* PML4[30] */
    movl %eax, pml4 + 248             /* PML4[31] */
    movl %eax, pml4 + 256             /* PML4[32] */
    movl %eax, pml4 + 264             /* PML4[33] */
    movl %eax, pml4 + 272             /* PML4[34] */
    movl %eax, pml4 + 280             /* PML4[35] */
    movl %eax, pml4 + 288             /* PML4[36] */
    movl %eax, pml4 + 296             /* PML4[37] */
    movl %eax, pml4 + 304             /* PML4[38] */
    movl %eax, pml4 + 312             /* PML4[39] */
    movl %eax, pml4 + 320             /* PML4[40] */
    movl %eax, pml4 + 328             /* PML4[41] */
    movl %eax, pml4 + 336             /* PML4[42] */
    movl %eax, pml4 + 344             /* PML4[43] */
    movl %eax, pml4 + 352             /* PML4[44] */
    movl %eax, pml4 + 360             /* PML4[45] */
    movl %eax, pml4 + 368             /* PML4[46] */
    movl %eax, pml4 + 376             /* PML4[47] */
    movl %eax, pml4 + 384             /* PML4[48] */
    movl %eax, pml4 + 392             /* PML4[49] */
    movl %eax, pml4 + 400             /* PML4[50] */
    movl %eax, pml4 + 408             /* PML4[51] */
    movl %eax, pml4 + 416             /* PML4[52] */
    movl %eax, pml4 + 424             /* PML4[53] */
    movl %eax, pml4 + 432             /* PML4[54] */
    movl %eax, pml4 + 440             /* PML4[55] */
    movl %eax, pml4 + 448             /* PML4[56] */
    movl %eax, pml4 + 456             /* PML4[57] */
    movl %eax, pml4 + 464             /* PML4[58] */
    movl %eax, pml4 + 472             /* PML4[59] */
    movl %eax, pml4 + 480             /* PML4[60] */
    movl %eax, pml4 + 488             /* PML4[61] */
    movl %eax, pml4 + 496             /* PML4[62] */
    movl %eax, pml4 + 504             /* PML4[63] for 63GB-64GB */
    
    /* PML4[0x1FF] -> pdpt (for high kernel MMIO, e.g., 0xFFFF800000000000+) */
    movl %eax, pml4 + 0xFF8           /* PML4[511] = 0x1FF * 8 = 0xFF8 */

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
    movl $pd4, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 32              /* 4GB-5GB for APIC */
    /* Add more PDPT entries for 5GB-64GB */
    movl $pd5, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 40
    movl $pd6, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 48
    movl $pd7, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 56
    movl $pd8, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 64
    movl $pd9, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 72
    movl $pd10, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 80
    movl $pd11, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 88
    movl $pd12, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 96
    movl $pd13, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 104
    movl $pd14, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 112
    movl $pd15, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 120
    movl $pd16, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 128
    movl $pd17, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 136
    movl $pd18, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 144
    movl $pd19, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 152
    movl $pd20, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 160
    movl $pd21, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 168
    movl $pd22, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 176
    movl $pd23, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 184
    movl $pd24, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 192
    movl $pd25, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 200
    movl $pd26, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 208
    movl $pd27, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 216
    movl $pd28, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 224
    movl $pd29, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 232
    movl $pd30, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 240
    movl $pd31, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 248
    movl $pd32, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 256
    movl $pd33, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 264
    movl $pd34, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 272
    movl $pd35, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 280
    movl $pd36, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 288
    movl $pd37, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 296
    movl $pd38, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 304
    movl $pd39, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 312
    movl $pd40, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 320
    movl $pd41, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 328
    movl $pd42, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 336
    movl $pd43, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 344
    movl $pd44, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 352
    movl $pd45, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 360
    movl $pd46, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 368
    movl $pd47, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 376
    movl $pd48, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 384
    movl $pd49, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 392
    movl $pd50, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 400
    movl $pd51, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 408
    movl $pd52, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 416
    movl $pd53, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 424
    movl $pd54, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 432
    movl $pd55, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 440
    movl $pd56, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 448
    movl $pd57, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 456
    movl $pd58, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 464
    movl $pd59, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 472
    movl $pd60, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 480
    movl $pd61, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 488
    movl $pd62, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 496
    movl $pd63, %eax
    orl  $0x3, %eax
    movl %eax, pdpt + 504              /* 63GB-64GB for APIC */

    /* Fill each PD with 512 2MB page entries */
    movl $0, %ebx                     /* PD index (0 = pd0, 1 = pd1...) */
    movl $64, %ebp                    /* 64 PDs total (0-64GB) */
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
