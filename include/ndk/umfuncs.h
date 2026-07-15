/*
 * MinNT - ndk/umfuncs.h
 * User-mode helper functions.
 */

#ifndef _NDK_UMFUNCS_H_
#define _NDK_UMFUNCS_H_

#include <nt/ntdef.h>

/* ---- RtlCreateUserThread ------------------------------------------------ */

NTSTATUS NTAPI RtlCreateUserThread(
    HANDLE ProcessHandle,
    PVOID SecurityDescriptor,
    BOOLEAN CreateSuspended,
    ULONG ZeroBits,
    SIZE_T MaximumStackSize,
    SIZE_T CommittedStackSize,
    PVOID StartAddress,
    PVOID StartParameter,
    PHANDLE ThreadHandle,
    PCLIENT_ID ClientId
);

/* ---- RtlUserThreadStart ------------------------------------------------- */

VOID NTAPI RtlUserThreadStart(
    PVOID StartRoutine,
    PVOID StartArgument
);

#endif /* _NDK_UMFUNCS_H_ */
