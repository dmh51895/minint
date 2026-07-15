/*
 * MinNT - user/user_app.S
 * First user-mode program.  Embedded as a flat binary and loaded
 * at 0x1_0000_0000 by PsCreateUserThread.
 *
 * Makes syscalls to demonstrate ring 3 → ring 0 transitions.
 * Syscall convention: RAX = syscall number, args in RDI, RSI, RDX, R10, R8, R9
 */

.section .text
.global _user_start
.type _user_start, @function
_user_start:
    /* Print "Hello from ring 3!" via NtDbgPrint (syscall 0) */
    leaq hello_str(%rip), %rdi
    xorq %rax, %rax
    syscall

    /* Get tick count via NtGetTickCount (syscall 1) */
    leaq tick_buf(%rip), %rdi
    movq $1, %rax
    syscall

    /* Print the tick value */
    leaq tick_msg(%rip), %rdi
    movq $0, %rax
    syscall

    /* Blame Curtis (syscall 4) */
    movq $4, %rax
    syscall

    /* Yield (syscall 2) */
    movq $2, %rax
    syscall

    /* Terminate (syscall 3) */
    xorq %rdi, %rdi
    movq $3, %rax
    syscall

    /* Should never reach here */
    hlt

.section .rodata
hello_str:
    .asciz "Hello from ring 3! 😈🔥🥒"
tick_msg:
    .asciz "Tick count acquired"
tick_buf:
    .quad 0
