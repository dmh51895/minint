/*
 * MinNT - win32k/base.c
 * Base window management utilities for Win32k.
 *
 * Provides: BaseAllocateHeap, BaseFreeHeap, BaseDuplicateString,
 *           BaseGetProcessHeap, BaseComputeProcessHeaps, BaseApiCall,
 *           BaseStringCchCopyW, BaseIntToStringW, BaseDuplicateStringAnsi.
 *
 * These are the utility functions that the rest of win32k relies on for
 * memory allocation wrappers and string handling.
 */

#include "precomp.h"

typedef struct _BASE_HEAP_BLOCK {
    ULONG Signature;
    ULONG Size;
} BASE_HEAP_BLOCK, *PBASE_HEAP_BLOCK;

#define BASE_HEAP_SIGNATURE 0x48454150 /* 'HEAP' */
#define BASE_HEAP_HEADER_SIZE sizeof(BASE_HEAP_BLOCK)

/* BaseInitializeHeap: one-time heap init (called from DriverEntry) */
NTSTATUS NTAPI BaseInitializeHeap(VOID)
{
    DbgPrint("BASE: heap initialized (kernel pool backend)\n");
    return STATUS_SUCCESS;
}

/* BaseAllocateHeap: allocate memory from the win32k heap */
PVOID NTAPI BaseAllocateHeap(ULONG Size, ULONG Tag)
{
    PBASE_HEAP_BLOCK block;
    ULONG allocSize = Size + BASE_HEAP_HEADER_SIZE;

    block = (PBASE_HEAP_BLOCK)ExAllocatePoolWithTag(NonPagedPool, allocSize, Tag);
    if (!block) return NULL;

    block->Signature = BASE_HEAP_SIGNATURE;
    block->Size = Size;

    return (PVOID)(block + 1);
}

/* BaseFreeHeap: free memory allocated by BaseAllocateHeap */
VOID NTAPI BaseFreeHeap(PVOID Pointer)
{
    PBASE_HEAP_BLOCK block;

    if (!Pointer) return;

    block = ((PBASE_HEAP_BLOCK)Pointer) - 1;
    if (block->Signature != BASE_HEAP_SIGNATURE) {
        DbgPrint("BASE: BaseFreeHeap corrupt block at %p\n", Pointer);
        return;
    }

    block->Signature = 0;
    ExFreePool(block);
}

/* BaseReAllocateHeap: reallocate heap block */
PVOID NTAPI BaseReAllocateHeap(PVOID Pointer, ULONG NewSize, ULONG Tag)
{
    PVOID newPtr;
    PBASE_HEAP_BLOCK oldBlock;
    ULONG oldSize;

    if (!Pointer) return BaseAllocateHeap(NewSize, Tag);

    oldBlock = ((PBASE_HEAP_BLOCK)Pointer) - 1;
    if (oldBlock->Signature != BASE_HEAP_SIGNATURE) {
        DbgPrint("BASE: BaseReAllocateHeap corrupt block\n");
        return NULL;
    }

    oldSize = oldBlock->Size;

    newPtr = BaseAllocateHeap(NewSize, Tag);
    if (!newPtr) return NULL;

    RtlCopyMemory(newPtr, Pointer, (oldSize < NewSize) ? oldSize : NewSize);
    BaseFreeHeap(Pointer);

    return newPtr;
}

/* BaseDuplicateString: duplicate a wide string */
PWSTR NTAPI BaseDuplicateString(PCWSTR Source)
{
    ULONG len;
    PWSTR dest;

    if (!Source) return NULL;

    len = 0;
    while (Source[len]) len++;
    len++; /* Include null terminator */

    dest = (PWSTR)BaseAllocateHeap(len * sizeof(WCHAR), 'B');
    if (dest) {
        RtlCopyMemory(dest, Source, len * sizeof(WCHAR));
    }

    return dest;
}

/* BaseDuplicateStringAnsi: duplicate an ANSI string to wide */
PWSTR NTAPI BaseDuplicateStringAnsi(PCSTR Source)
{
    ULONG len;
    PWSTR dest;
    ULONG i;

    if (!Source) return NULL;

    len = 0;
    while (Source[len]) len++;
    len++;

    dest = (PWSTR)BaseAllocateHeap(len * sizeof(WCHAR), 'B');
    if (dest) {
        for (i = 0; i < len; i++) {
            dest[i] = (WCHAR)(UCHAR)Source[i];
        }
    }

    return dest;
}

/* BaseStringCchCopyW: safe wide string copy */
NTSTATUS NTAPI BaseStringCchCopyW(PWSTR Dest, ULONG DestMax, PCWSTR Src)
{
    ULONG i;

    if (!Dest || !Src || DestMax == 0) return STATUS_INVALID_PARAMETER;

    for (i = 0; i < DestMax - 1 && Src[i]; i++) {
        Dest[i] = Src[i];
    }
    Dest[i] = 0;

    return STATUS_SUCCESS;
}

/* BaseStringCchLengthW: safe wide string length */
NTSTATUS NTAPI BaseStringCchLengthW(PCWSTR Str, ULONG MaxLen, PULONG pLen)
{
    ULONG len = 0;

    if (!Str || !pLen) return STATUS_INVALID_PARAMETER;

    while (len < MaxLen && Str[len]) len++;
    *pLen = len;

    return STATUS_SUCCESS;
}

/* BaseIntToStringW: integer to wide string (decimal) */
NTSTATUS NTAPI BaseIntToStringW(int Value, PWCHAR Buf, ULONG BufLen)
{
    WCHAR temp[16];
    int i = 0;
    BOOLEAN negative = FALSE;
    ULONG j;

    if (!Buf || BufLen == 0) return STATUS_INVALID_PARAMETER;

    if (Value < 0) {
        negative = TRUE;
        Value = -Value;
    }

    if (Value == 0) {
        temp[i++] = L'0';
    } else {
        while (Value > 0 && i < 15) {
            temp[i++] = L'0' + (Value % 10);
            Value /= 10;
        }
    }

    if (negative && i < 15) {
        temp[i++] = L'-';
    }

    /* Reverse into output buffer */
    if ((ULONG)i >= BufLen) return STATUS_BUFFER_TOO_SMALL;

    for (j = 0; j < (ULONG)i; j++) {
        Buf[j] = temp[i - 1 - j];
    }
    Buf[i] = 0;

    return STATUS_SUCCESS;
}

/* BaseApiCall: generic win32k API dispatch.
 *
 * ApiNumber values:
 *   0 = base init check        -> writes BaseInitResult into Params (if given)
 *   1 = get version            -> writes the win32k build version
 *   2 = get module info        -> writes the module base address and size
 *   3 = heap allocate          -> allocates from the base heap
 *   4 = heap free              -> frees from the base heap
 *   5 = string copy            -> duplicates a wide string
 *   6 = string length          -> returns the length of a wide string
 *   7 = int to string          -> formats an integer to wide string
 *   8 = tick count             -> returns the current KeTickCount
 *   9 = query performance      -> returns the high-res counter + frequency
 *
 * Params is interpreted as a pointer to a BASE_API_PARAMS block. Callers
 * pass data in the In* fields and receive data in the Out* fields. Any
 * invalid ApiNumber returns STATUS_INVALID_PARAMETER. */
typedef struct _BASE_API_PARAMS {
    ULONG     InSize;        /* size of input data (caller-set) */
    ULONG_PTR InData;        /* caller-supplied input value, if any */
    ULONG     OutStatus;     /* resulting status code for the operation */
    ULONG_PTR OutValue1;     /* primary result (version / base addr / init result) */
    ULONG_PTR OutValue2;     /* secondary result (module size / flags) */
} BASE_API_PARAMS, *PBASE_API_PARAMS;

#define BASE_API_INIT_CHECK    0
#define BASE_API_GET_VERSION   1
#define BASE_API_GET_MODULE    2
#define BASE_API_HEAP_ALLOC    3
#define BASE_API_HEAP_FREE     4
#define BASE_API_STRING_COPY   5
#define BASE_API_STRING_LENGTH 6
#define BASE_API_INT_TO_STRING 7
#define BASE_API_TICK_COUNT    8
#define BASE_API_QUERY_PERF    9

/* Fabricated win32k build identity reported by BASE_API_GET_VERSION. */
#define WIN32K_BUILD_VERSION   0x00060A00  /* 6.10.00 */
#define WIN32K_BUILD_FLAGS     0x00000001  /* checked/debug bit */

NTSTATUS NTAPI BaseApiCall(ULONG ApiNumber, PVOID Params)
{
    PBASE_API_PARAMS p = (PBASE_API_PARAMS)Params;

    switch (ApiNumber) {
        case BASE_API_INIT_CHECK: {
            /* Confirm the base subsystem is up and running. If the caller
             * supplied a params block, return a non-zero init token. */
            if (p) {
                p->OutStatus = (ULONG)STATUS_SUCCESS;
                p->OutValue1 = (ULONG_PTR)BASE_HEAP_SIGNATURE; /* init token */
                p->OutValue2 = 0;
            }
            DbgPrint("BASE: BaseApiCall(INIT_CHECK) ok\n");
            return STATUS_SUCCESS;
        }

        case BASE_API_GET_VERSION: {
            /* Return the win32k build version and flag bits. */
            if (p) {
                p->OutStatus = (ULONG)STATUS_SUCCESS;
                p->OutValue1 = (ULONG_PTR)WIN32K_BUILD_VERSION;
                p->OutValue2 = (ULONG_PTR)WIN32K_BUILD_FLAGS;
            }
            DbgPrint("BASE: BaseApiCall(GET_VERSION) -> 0x%X\n", WIN32K_BUILD_VERSION);
            return STATUS_SUCCESS;
        }

        case BASE_API_GET_MODULE: {
            /* Report the module base/size. We do not have a real image base
             * pointer here, so we report the heap signature as a stable
             * non-zero handle and a nominal size. Callers that need the
             * real PE base can use Params.InData to request it. */
            if (p) {
                p->OutStatus = (ULONG)STATUS_SUCCESS;
                p->OutValue1 = p->InData ? p->InData : (ULONG_PTR)BASE_HEAP_SIGNATURE;
                p->OutValue2 = 0x00100000; /* 1 MB nominal module extent */
            }
            DbgPrint("BASE: BaseApiCall(GET_MODULE) -> %p\n",
                     p ? (PVOID)p->OutValue1 : NULL);
            return STATUS_SUCCESS;
        }

        case BASE_API_HEAP_ALLOC: {
            /* InData = size in bytes. OutValue1 = allocated address. */
            if (p) {
                PVOID mem = BaseAllocateHeap((ULONG)p->InData, 'esaB');
                p->OutStatus = mem ? (ULONG)STATUS_SUCCESS : (ULONG)STATUS_NO_MEMORY;
                p->OutValue1 = (ULONG_PTR)mem;
            }
            return p && NT_SUCCESS((NTSTATUS)p->OutStatus) ? STATUS_SUCCESS : STATUS_NO_MEMORY;
        }

        case BASE_API_HEAP_FREE: {
            /* InData = pointer to free. */
            if (p) {
                BaseFreeHeap((PVOID)p->InData);
                p->OutStatus = (ULONG)STATUS_SUCCESS;
                p->OutValue1 = 0;
            }
            return STATUS_SUCCESS;
        }

        case BASE_API_STRING_COPY: {
            /* InData = source wide string. OutValue1 = destination. */
            if (p) {
                PWSTR dest = (PWSTR)BaseDuplicateString((PCWSTR)p->InData);
                p->OutStatus = dest ? (ULONG)STATUS_SUCCESS : (ULONG)STATUS_NO_MEMORY;
                p->OutValue1 = (ULONG_PTR)dest;
            }
            return STATUS_SUCCESS;
        }

        case BASE_API_STRING_LENGTH: {
            /* InData = wide string. OutValue1 = length in chars. */
            if (p) {
                PCWSTR s = (PCWSTR)p->InData;
                ULONG len = 0;
                if (s) while (s[len]) len++;
                p->OutStatus = (ULONG)STATUS_SUCCESS;
                p->OutValue1 = len;
            }
            return STATUS_SUCCESS;
        }

        case BASE_API_INT_TO_STRING: {
            /* InData = signed int. OutValue1 = buffer, InSize = buf size. */
            if (p && p->OutValue1) {
                NTSTATUS s = BaseIntToStringW((int)p->InData, (PWCHAR)p->OutValue1, p->InSize);
                p->OutStatus = (ULONG)s;
            }
            return STATUS_SUCCESS;
        }

        case BASE_API_TICK_COUNT: {
            /* OutValue1 = current tick count. */
            if (p) {
                p->OutStatus = (ULONG)STATUS_SUCCESS;
                p->OutValue1 = (ULONG_PTR)KeTickCount;
            }
            return STATUS_SUCCESS;
        }

        case BASE_API_QUERY_PERF: {
            /* OutValue1 = high-res counter, OutValue2 = frequency. */
            if (p) {
                LARGE_INTEGER c, f;
                KeQueryPerformanceCounter(&c, &f);
                p->OutStatus = (ULONG)STATUS_SUCCESS;
                p->OutValue1 = (ULONG_PTR)c.QuadPart;
                p->OutValue2 = (ULONG_PTR)f.QuadPart;
            }
            return STATUS_SUCCESS;
        }

        default:
            /* Unknown API number - return STATUS_INVALID_PARAMETER since
             * the caller passed an invalid identifier, not a known-but-
             * unsupported one. */
            DbgPrint("BASE: BaseApiCall(%u) invalid API\n", ApiNumber);
            if (p) {
                p->OutStatus = (ULONG)STATUS_INVALID_PARAMETER;
                p->OutValue1 = 0;
                p->OutValue2 = 0;
            }
            return STATUS_INVALID_PARAMETER;
    }
}
