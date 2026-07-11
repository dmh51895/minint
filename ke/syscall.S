/*
 * MinNT - ke/syscall.S
 * SYSCALL entry point (KiSystemCall64), IRETQ-based return, and
 * KiEnterUserMode trampoline for initial ring 3 transition.
 */

.section .text
.global KiSystemCall64
.type KiSystemCall64, @function
KiSystemCall64:
    swapgs
    movq %rsp, %gs:0x18           /* save user RSP */
    movq %gs:0x20, %rsp            /* load kernel stack */
    pushq $0x23                    /* SS */
    pushq %gs:0x18                 /* user RSP */
    pushq %r11                     /* RFLAGS */
    pushq $0x1B                    /* CS */
    pushq %rcx                     /* RIP */
    pushq $0                       /* error code */
    pushq %rax                     /* vector */
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rdi
    pushq %rsi
    pushq %rbp
    pushq %rbx
    pushq %rdx
    pushq %rcx
    pushq %rax
    sti
    movq %rax, %rdi
    movq %rsp, %rsi
    call KiSystemServiceHandler
    cli
    popq %rax
    popq %rcx
    popq %rdx
    popq %rbx
    popq %rbp
    popq %rsi
    popq %rdi
    popq %r8
    popq %r9
    popq %r10
    popq %r11
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    addq $16, %rsp
    iretq

/*
 * KiEnterUserMode — initial transition to ring 3.
 * Stack has been seeded by PsCreateUserThread with:
 *   [ret to KiEnterUserMode] ← RSP
 *   [SS] [user RSP] [RFLAGS] [CS] [RIP]
 *
 * We set up the kernel stack top, then IRETQ into user mode.
 */
.global KiEnterUserMode
.type KiEnterUserMode, @function
KiEnterUserMode:
    /* Get kernel stack top from current thread's KTHREAD.KernelStack.
       Must read BEFORE swapgs, since swapgs swaps GS to user base.
       KPCR.PrcbData.CurrentThread is at GS:[0x38] (kernel GS).
       KTHREAD.KernelStack at offset 0x08. */
    movq %gs:0x38, %rax          /* CurrentThread (KTHREAD*) */
    movq 0x08(%rax), %rax        /* KernelStack (top of kernel stack) */
    movq %rax, %gs:0x20          /* save for syscalls (KernelStackTop) */

    /* Set TSS.RSP0 = KernelStack (inline, no call) */
    movabsq $KiTss, %rcx
    movq %rax, 4(%rcx)           /* Rsp0 is at offset 4 in KiTss */

    /* NOTE: No popq here! KiSwapContext's ret already landed us
       in KiEnterUserMode with the IRETQ frame (RIP/CS/RFLAGS/RSP/SS)
       correctly positioned on the stack. Popping would shift the
       frame and cause #GP. */
    swapgs                        /* GS back to user (for ring 3) */
    iretq
