/*
 * MinNT - win32k/w32ksvc.h
 * Win32k syscall table interface
 *
 * W32pServiceTable is indexed by (syscall_number - 0x1000).
 * USER syscalls: 0x1000-0x10FF
 * GDI syscalls:  0x1008-0x10FF (overlaps with USER)
 */

#ifndef _W32KSVC_H_
#define _W32KSVC_H_

#include <nt/ntdef.h>

#define WIN32K_SYSCALL_BASE 0x1000
#define WIN32K_SYSCALL_MAX  0x1000

typedef NTSTATUS (NTAPI *W32P_SERVICE)(ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                        ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                        ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                        ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                        ULONG_PTR, ULONG_PTR, ULONG_PTR);

extern W32P_SERVICE W32pServiceTable[256];

extern NTSTATUS NTAPI Win32kSyscallDispatcher(ULONG SyscallNumber,
                                              ULONG_PTR Arg1, ULONG_PTR Arg2, ULONG_PTR Arg3,
                                              ULONG_PTR Arg4, ULONG_PTR Arg5, ULONG_PTR Arg6,
                                              ULONG_PTR Arg7, ULONG_PTR Arg8, ULONG_PTR Arg9,
                                              ULONG_PTR Arg10, ULONG_PTR Arg11, ULONG_PTR Arg12,
                                              ULONG_PTR Arg13, ULONG_PTR Arg14, ULONG_PTR Arg15);

#endif /* _W32KSVC_H_ */