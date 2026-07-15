/*
 * MinNT - ke/trap.S
 * 256 vector stubs -> common frame builder -> KiDispatchInterrupt(C).
 * Exceptions 8,10-14,17,21,29,30 push a hardware error code; everyone
 * else gets a fake 0 so the KTRAP_FRAME layout is uniform. This is the
 * KiTrap* family from real ntoskrnl, macro-generated.
 */

.section .text
.code64

.macro TRAP_NOERR vec
.global KiTrap\vec
KiTrap\vec:
    pushq $0                 /* fake error code */
    pushq $\vec              /* vector          */
    jmp   KiTrapCommon
.endm

.macro TRAP_ERR vec
.global KiTrap\vec
KiTrap\vec:
    pushq $\vec              /* CPU already pushed error code */
    jmp   KiTrapCommon
.endm

/* Frame builder: push GP regs to complete KTRAP_FRAME, call C dispatcher */
KiTrapCommon:
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rdi          /* PKTRAP_FRAME */
    cld
    call KiDispatchInterrupt

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    addq $16, %rsp           /* vector + error code */
    iretq

/* CPU exceptions 0-31 */
TRAP_NOERR 0
TRAP_NOERR 1
TRAP_NOERR 2
TRAP_NOERR 3
TRAP_NOERR 4
TRAP_NOERR 5
TRAP_NOERR 6
TRAP_NOERR 7
TRAP_ERR   8
TRAP_NOERR 9
TRAP_ERR   10
TRAP_ERR   11
TRAP_ERR   12
TRAP_ERR   13
TRAP_ERR   14
TRAP_NOERR 15
TRAP_NOERR 16
TRAP_ERR   17
TRAP_NOERR 18
TRAP_NOERR 19
TRAP_NOERR 20
TRAP_ERR   21
TRAP_NOERR 22
TRAP_NOERR 23
TRAP_NOERR 24
TRAP_NOERR 25
TRAP_NOERR 26
TRAP_NOERR 27
TRAP_NOERR 28
TRAP_ERR   29
TRAP_ERR   30
TRAP_NOERR 31

/* Hardware IRQs + software vectors 32-255 via .altmacro loop */
.altmacro
.macro GEN_RANGE from, to
    TRAP_NOERR \from
    .if \to - \from
    GEN_RANGE %(\from + 1), \to
    .endif
.endm

GEN_RANGE 32, 63
GEN_RANGE 64, 95
GEN_RANGE 96, 127
GEN_RANGE 128, 159
GEN_RANGE 160, 191
GEN_RANGE 192, 223
GEN_RANGE 224, 255

/* Vector table consumed by idt.c */
.section .rodata
.align 8
.global KiTrapTable
KiTrapTable:
.altmacro
.macro TBL_RANGE from, to
    .quad KiTrap\from
    .if \to - \from
    TBL_RANGE %(\from + 1), \to
    .endif
.endm
TBL_RANGE 0, 63
TBL_RANGE 64, 127
TBL_RANGE 128, 191
TBL_RANGE 192, 255

.section .note.GNU-stack,"",@progbits
