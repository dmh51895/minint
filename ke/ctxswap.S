/*
 * MinNT - ke/ctxswap.S
 * void KiSwapContext(ULONG_PTR *OldRsp, ULONG_PTR NewRsp);
 *   rdi = &OldThread->StackPointer (save slot)
 *   rsi = NewThread->StackPointer  (restore value)
 *
 * SysV cooperative switch: save callee-saved regs + rflags on the old
 * stack, stash rsp, load new rsp, pop the mirror image, ret into the
 * new thread. New threads get their stack pre-seeded by KiInitializeThread
 * so this same pop/ret path "returns" into KiThreadStartup.
 *
 * BUG-015: KiDispatchNextThread releases KiDispatcherLock (which re-enables
 * interrupts, since the lock is what's holding IRQL at DISPATCH_LEVEL)
 * before calling this. That leaves RSP's actual transition from the old
 * stack to the new one exposed: if a hardware IRQ (timer, keyboard) lands
 * between "movq %rsi, %rsp" and the pops finishing, it pushes its own trap
 * frame into whatever headroom sits below the new thread's saved
 * registers -- on a small kernel stack that's a real stack-smash, not a
 * hypothetical one, and it only needed a second real IRQ source (the
 * keyboard) to become reachable outside of pure timer-interrupt timing.
 * cli right after pushfq closes that window for the swap itself; popfq
 * still restores whatever IF state this exact thread had when it was
 * switched out, so nothing about the higher-level IRQL/lock protocol has
 * to change to fix this.
 */

.section .text
.code64
.global KiSwapContext
.type KiSwapContext, @function
KiSwapContext:
    pushfq
    cli                      /* BUG-015: close the mid-swap interrupt window */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, (%rdi)        /* save old RSP  */
    movq %rsi, %rsp          /* load new RSP  */

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    popfq                    /* restores this thread's own IF state */
    ret

.section .note.GNU-stack,"",@progbits
