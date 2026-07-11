/*
 * MinNT - ndk/rtlfuncs.h
 * RTL utility functions: strings, heap, registry query, environment, etc.
 * Uses MinNT base types from ntdef.h and ndk/setypes.h.
 */

#ifndef _NDK_RTLFUNCS_H_
#define _NDK_RTLFUNCS_H_

#include <nt/ntdef.h>
#include <nt/se.h>
#include <ndk/setypes.h>

/* ---- PSTR/PCSTR (not in MinNT base) ------------------------------------ */

#ifndef _PSTR_DEFINED
#define _PSTR_DEFINED
typedef char *PSTR;
typedef const char *PCSTR;
#endif

/* ---- String helpers ----------------------------------------------------- */

void NTAPI RtlInitEmptyUnicodeString(PUNICODE_STRING Dst, PWSTR Buffer, USHORT MaxLen);

NTSTATUS NTAPI RtlUnicodeStringToAnsiString(PANSI_STRING Dst, PCUNICODE_STRING Src, BOOLEAN Alloc);
NTSTATUS NTAPI RtlAnsiStringToUnicodeString(PUNICODE_STRING Dst, PCANSI_STRING Src, BOOLEAN Alloc);
void NTAPI RtlFreeUnicodeString(PUNICODE_STRING Str);
void NTAPI RtlFreeAnsiString(PANSI_STRING Str);

LONG NTAPI RtlCompareUnicodeString(PCUNICODE_STRING S1, PCUNICODE_STRING S2, BOOLEAN CaseInsensitive);
NTSTATUS NTAPI RtlAppendUnicodeStringToString(PUNICODE_STRING Dst, PCUNICODE_STRING Src);
NTSTATUS NTAPI RtlAppendUnicodeToString(PUNICODE_STRING Dst, PCWSTR Src);

/* ---- Heap --------------------------------------------------------------- */

PVOID NTAPI RtlGetProcessHeap(VOID);
PVOID NTAPI RtlCreateHeap(ULONG Flags, PVOID Base, SIZE_T Reserve, SIZE_T Commit, PVOID Lock, PVOID Params);
PVOID NTAPI RtlAllocateHeap(PVOID Heap, ULONG Flags, SIZE_T Size);
BOOLEAN NTAPI RtlFreeHeap(PVOID Heap, ULONG Flags, PVOID Base);
SIZE_T NTAPI RtlSizeHeap(PVOID Heap, ULONG Flags, PVOID Base);

/* ---- Registry query ----------------------------------------------------- */

#define RTL_QUERY_REGISTRY_DELETE         0x00000008
#define RTL_QUERY_REGISTRY_SUBKEY        0x00000001
#define RTL_QUERY_REGISTRY_TOPKEY        0x00000000
#define RTL_QUERY_REGISTRY_NOEXPAND      0x00000004

typedef NTSTATUS (NTAPI *PRTL_QUERY_REGISTRY_ROUTINE)(
    IN PWSTR ValueName,
    IN ULONG ValueType,
    IN PVOID ValueData,
    IN ULONG ValueLength,
    IN PVOID Context,
    IN PVOID EntryContext
);

typedef struct _RTL_QUERY_REGISTRY_TABLE {
    PRTL_QUERY_REGISTRY_ROUTINE QueryRoutine;
    ULONG Flags;
    PWSTR Name;
    PVOID EntryContext;
    ULONG Type;
    PVOID Default;
    ULONG DefaultLength;
} RTL_QUERY_REGISTRY_TABLE, *PRTL_QUERY_REGISTRY_TABLE;

NTSTATUS NTAPI RtlQueryRegistryValues(
    ULONG RelativeTo,
    PCWSTR Path,
    PRTL_QUERY_REGISTRY_TABLE QueryTable,
    PVOID Context,
    PVOID Environment
);

NTSTATUS NTAPI RtlCharToInteger(PCSTR Str, ULONG Base, PULONG Value);

/* ---- Environment -------------------------------------------------------- */

NTSTATUS NTAPI RtlSetEnvironmentVariable(
    PVOID *Environment,
    PUNICODE_STRING Name,
    PUNICODE_STRING Value
);

/* ---- Path conversion ---------------------------------------------------- */

BOOLEAN NTAPI RtlDosPathNameToNtPathName_U(
    PCWSTR DosName,
    PUNICODE_STRING NtName,
    PWSTR *FilePart,
    PVOID RelativeName
);

/* ---- String formatting -------------------------------------------------- */

NTSTATUS NTAPI RtlStringCbPrintfA(PSTR Dest, SIZE_T DestSize, PCSTR Format, ...);
NTSTATUS NTAPI RtlStringCbLengthW(PCWSTR Str, SIZE_T MaxLen, SIZE_T *Length);
NTSTATUS NTAPI RtlStringCbCopyNW(PWSTR Dest, SIZE_T DestSize, PCWSTR Src, SIZE_T Count);

/* ---- Privilege ---------------------------------------------------------- */

NTSTATUS NTAPI RtlAdjustPrivilege(ULONG Privilege, BOOLEAN Enable, BOOLEAN CurrentThread, PBOOLEAN WasEnabled);

/* ---- Process/thread critical -------------------------------------------- */

NTSTATUS NTAPI RtlSetProcessIsCritical(BOOLEAN NewValue, PBOOLEAN OldValue, BOOLEAN NeedScb);
NTSTATUS NTAPI RtlSetThreadIsCritical(BOOLEAN NewValue, PBOOLEAN OldValue, BOOLEAN NeedScb);

/* ---- SEH ---------------------------------------------------------------- */

#ifndef EXCEPTION_EXECUTE_HANDLER
#define EXCEPTION_EXECUTE_HANDLER    1
#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define EXCEPTION_CONTINUE_SEARCH    0
#endif

typedef struct _EXCEPTION_RECORD {
    NTSTATUS ExceptionCode;
    ULONG ExceptionFlags;
    PVOID ExceptionRecord;
    PVOID ExceptionAddress;
    ULONG NumberParameters;
    ULONG_PTR ExceptionInformation[15];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;

typedef struct _CONTEXT {
    ULONG64 P1Home;
    ULONG64 P2Home;
    ULONG64 P3Home;
    ULONG64 P4Home;
    ULONG64 P5Home;
    ULONG64 P6Home;
    ULONG32 ContextFlags;
    ULONG32 MxCsr;
    ULONG64 Rip;
    ULONG64 Rsp;
    ULONG64 Rbp;
    ULONG64 Rax;
    ULONG64 Rbx;
    ULONG64 Rcx;
    ULONG64 Rdx;
    ULONG64 Rsi;
    ULONG64 Rdi;
    ULONG64 R8;
    ULONG64 R9;
    ULONG64 R10;
    ULONG64 R11;
    ULONG64 R12;
    ULONG64 R13;
    ULONG64 R14;
    ULONG64 R15;
} CONTEXT, *PCONTEXT;

typedef struct _EXCEPTION_POINTERS {
    PEXCEPTION_RECORD ExceptionRecord;
    PCONTEXT ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS;

/* SEH2 macros are in include/pseh/pseh2.h - do not redefine here */

/* ---- Wide char compare -------------------------------------------------- */

int _wcsicmp(const unsigned short *s1, const unsigned short *s2);

/* ---- Wide string search ------------------------------------------------- */

unsigned short *wcsrchr(const unsigned short *s, unsigned short c);

#define RTL_NUMBER_OF(A) (sizeof(A)/sizeof((A)[0]))
#endif /* _NDK_RTLFUNCS_H_ */
