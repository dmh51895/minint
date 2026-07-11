/*
 * MinNT - rtl.h
 * Run-Time Library: the freestanding memcpy/memset the compiler expects,
 * plus UNICODE_STRING helpers.
 */

#ifndef _RTL_H_
#define _RTL_H_

#include <nt/ntdef.h>

PVOID RtlCopyMemory(PVOID Dst, const VOID *Src, SIZE_T Length);
PVOID RtlZeroMemory(PVOID Dst, SIZE_T Length);
PVOID RtlFillMemory(PVOID Dst, SIZE_T Length, UCHAR Fill);
SIZE_T RtlStringLength(const CHAR *S);
SIZE_T RtlWStringLength(const WCHAR *S);
int _stricmp(const char *s1, const char *s2);

VOID    NTAPI RtlInitUnicodeString(PUNICODE_STRING Dst, PCWSTR Src);
BOOLEAN NTAPI RtlEqualUnicodeString(PCUNICODE_STRING A, PCUNICODE_STRING B,
                                    BOOLEAN CaseInsensitive);

/* Critical section */
typedef struct _RTL_CRITICAL_SECTION {
    PVOID DebugInfo;
    LONG LockCount;
    LONG RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    ULONG_PTR SpinCount;
} RTL_CRITICAL_SECTION, *PRTL_CRITICAL_SECTION;

NTSTATUS NTAPI RtlInitializeCriticalSection(PRTL_CRITICAL_SECTION Section);
NTSTATUS NTAPI RtlEnterCriticalSection(PRTL_CRITICAL_SECTION Section);
NTSTATUS NTAPI RtlLeaveCriticalSection(PRTL_CRITICAL_SECTION Section);
BOOLEAN NTAPI RtlTryEnterCriticalSection(PRTL_CRITICAL_SECTION Section);
NTSTATUS NTAPI RtlDeleteCriticalSection(PRTL_CRITICAL_SECTION Section);

/* GCC may emit calls to these even in freestanding mode */
void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
void *memmove(void *d, const void *s, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

#endif /* _RTL_H_ */
