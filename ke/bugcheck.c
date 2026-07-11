/*
 * MinNT - ke/bugcheck.c
 * The blue screen. White on blue (0x1F), stop code + four parameters,
 * mirrored to COM1 so headless QEMU (-serial stdio) still shows it.
 */

#include <nt/ke.h>
#include <nt/hal.h>

static const CHAR *KiBugCheckName(ULONG Code)
{
    switch (Code) {
    case IRQL_NOT_LESS_OR_EQUAL:        return "IRQL_NOT_LESS_OR_EQUAL";
    case KMODE_EXCEPTION_NOT_HANDLED:   return "KMODE_EXCEPTION_NOT_HANDLED";
    case PAGE_FAULT_IN_NONPAGED_AREA:   return "PAGE_FAULT_IN_NONPAGED_AREA";
    case PHASE0_INITIALIZATION_FAILED:  return "PHASE0_INITIALIZATION_FAILED";
    case PHASE1_INITIALIZATION_FAILED:  return "PHASE1_INITIALIZATION_FAILED";
    case UNEXPECTED_KERNEL_MODE_TRAP:   return "UNEXPECTED_KERNEL_MODE_TRAP";
    case BAD_POOL_HEADER:               return "BAD_POOL_HEADER";
    case MANUALLY_INITIATED_CRASH:      return "MANUALLY_INITIATED_CRASH";
    default:                            return "UNKNOWN_STOP_CODE";
    }
}

DECLSPEC_NORETURN VOID NTAPI
KeBugCheckEx(ULONG BugCheckCode,
             ULONG_PTR P1, ULONG_PTR P2, ULONG_PTR P3, ULONG_PTR P4)
{
    KeDisableInterrupts();

    HalpVgaSetColor(0x1F);              /* white on blue, the classic */
    HalpVgaInit();

    DbgPrint("\n");
    DbgPrint("A problem has been detected and MinNT has been shut down\n");
    DbgPrint("to prevent damage to your imaginary hardware.\n\n");
    DbgPrint("%s\n\n", KiBugCheckName(BugCheckCode));
    DbgPrint("*** STOP: 0x%x (%p, %p, %p, %p)\n\n",
             BugCheckCode, (PVOID)P1, (PVOID)P2, (PVOID)P3, (PVOID)P4);
    DbgPrint("If this is the first time you've seen this Stop error screen,\n");
    DbgPrint("restart your computer. If this screen appears again, blame\n");
    DbgPrint("Curtis. Tick count: %llu\n", KeTickCount);

    for (;;) {
        KeDisableInterrupts();
        KeHaltProcessor();
    }
}
