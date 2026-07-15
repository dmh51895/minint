/*
 * MinNT - ndk/kefuncs.h
 * Kernel Executive functions: system info, time, etc.
 */

#ifndef _NDK_KEFUNCS_H_
#define _NDK_KEFUNCS_H_

#include <nt/ntdef.h>

/* ---- CCHAR (not in MinNT base) ----------------------------------------- */

/* ---- System information ------------------------------------------------- */

#define SystemBasicInformation         0
#define SystemKernelDebuggerInformation 36

typedef struct _SYSTEM_KERNEL_DEBUGGER_INFORMATION {
    BOOLEAN DebuggerEnabled;
    BOOLEAN DebuggerNotPresent;
} SYSTEM_KERNEL_DEBUGGER_INFORMATION, *PSYSTEM_KERNEL_DEBUGGER_INFORMATION;

NTSTATUS NTAPI NtQuerySystemInformation(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
);

/* ---- Time --------------------------------------------------------------- */

NTSTATUS NTAPI NtQuerySystemTime(PLARGE_INTEGER SystemTime);

/* ---- Display string ----------------------------------------------------- */

NTSTATUS NTAPI NtDisplayString(PUNICODE_STRING String);

/* ---- Hard error --------------------------------------------------------- */

NTSTATUS NTAPI NtRaiseHardError(
    NTSTATUS ErrorStatus,
    ULONG NumberOfParameters,
    ULONG UnicodeStringParameterMask,
    PULONG_PTR Parameters,
    ULONG ValidResponseOptions,
    PULONG Response
);

NTSTATUS NTAPI NtSetDefaultHardErrorPort(HANDLE PortHandle);

#endif /* _NDK_KEFUNCS_H_ */
