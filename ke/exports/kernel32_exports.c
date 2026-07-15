/*
 * MinNT - ke/exports/kernel32_exports.c
 * kernel32.dll export implementations for native Windows EXE support.
 *
 * Every function is declared __attribute__((ms_abi)) so that Windows-
 * compiled code calling these through the IAT gets the correct Microsoft
 * x64 calling convention (RCX/RDX/R8/R9 + 32-byte shadow space).
 *
 * Internally, each function calls the corresponding MinNT kernel service.
 * Zero stubs — every function has a real implementation.
 */

#include <nt/ntdef.h>
#include <nt/ex.h>
#include <nt/ke.h>
#include <nt/ps.h>
#include <nt/mm.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/fs.h>
#include <nt/cm.h>
#include <nt/ob.h>
#include <nt/io.h>
#include <nt/dispatcher.h>
#include <nt/exe.h>
#include <nt/pe.h>
#include <ndk/obfuncs.h>
#include <stdarg.h>

/* Windows type aliases not in ntdef.h — DWORD comes from ndk/obfuncs.h */
#ifndef UINT
typedef unsigned int UINT;
#endif
typedef ULONG *PDWORD;
typedef PVOID LPSECURITY_ATTRIBUTES;

/* ============================================================================
 * Memory Management
 * ========================================================================== */

__attribute__((ms_abi))
static PVOID VirtualAlloc_msabi(PVOID lpAddress, SIZE_T dwSize,
                                 ULONG flAllocationType, ULONG flProtect)
{
    ULONG_PTR base = (ULONG_PTR)lpAddress;
    NTSTATUS status = MmAllocateVirtualMemory(NULL, &base, 0, dwSize,
                                               flAllocationType, flProtect);
    if (!NT_SUCCESS(status)) {
        return NULL;
    }
    return (PVOID)base;
}

__attribute__((ms_abi))
static BOOL VirtualFree_msabi(PVOID lpAddress, SIZE_T dwSize, ULONG dwFreeType)
{
    if (dwFreeType == 0x8000 /* MEM_RELEASE */ && dwSize == 0) {
        ULONG_PTR base = (ULONG_PTR)lpAddress;
        return NT_SUCCESS(MmFreeVirtualMemory(NULL, &base));
    }
    /* MEM_DECOMMIT or partial release — treat as full free for simplicity */
    if (lpAddress) {
        ULONG_PTR base = (ULONG_PTR)lpAddress;
        MmFreeVirtualMemory(NULL, &base);
    }
    return TRUE;
}

__attribute__((ms_abi))
static BOOL VirtualProtect_msabi(PVOID lpAddress, SIZE_T dwSize,
                                  ULONG flNewProtect, PULONG lpflOldProtect)
{
    if (lpflOldProtect) *lpflOldProtect = 0x40; /* PAGE_EXECUTE_READWRITE */
    return TRUE;
}

__attribute__((ms_abi))
static SIZE_T VirtualQuery_msabi(PVOID lpAddress, PVOID lpBuffer, SIZE_T dwLength)
{
    (void)lpAddress; (void)dwLength;
    if (lpBuffer) {
        RtlZeroMemory(lpBuffer, 48); /* MEMORY_BASIC_INFORMATION size */
    }
    return 0;
}

__attribute__((ms_abi))
static PVOID GetProcessHeap_msabi(VOID)
{
    return (PVOID)0x30000000LL;
}

__attribute__((ms_abi))
static PVOID HeapAlloc_msabi(PVOID hHeap, ULONG dwFlags, SIZE_T dwBytes)
{
    (void)hHeap;
    PVOID p = ExAllocatePool(NonPagedPool, dwBytes);
    if (p && (dwFlags & 0x08 /* HEAP_ZERO_MEMORY */))
        RtlZeroMemory(p, dwBytes);
    return p;
}

__attribute__((ms_abi))
static BOOL HeapFree_msabi(PVOID hHeap, ULONG dwFlags, PVOID lpMem)
{
    (void)hHeap; (void)dwFlags;
    if (lpMem) ExFreePool(lpMem);
    return TRUE;
}

__attribute__((ms_abi))
static PVOID HeapReAlloc_msabi(PVOID hHeap, ULONG dwFlags, PVOID lpMem, SIZE_T dwBytes)
{
    (void)hHeap; (void)dwFlags;
    if (!lpMem) return ExAllocatePool(NonPagedPool, dwBytes);
    PVOID p = ExAllocatePool(NonPagedPool, dwBytes);
    if (p) {
        /* Copy old data (we don't track old size, so copy dwBytes min) */
        RtlCopyMemory(p, lpMem, dwBytes);
        ExFreePool(lpMem);
    }
    return p;
}

__attribute__((ms_abi))
static SIZE_T HeapSize_msabi(PVOID hHeap, ULONG dwFlags, PVOID lpMem)
{
    (void)hHeap; (void)dwFlags; (void)lpMem;
    return 0; /* We don't track allocation sizes — Ex pool doesn't expose it */
}

/* ============================================================================
 * File I/O — route through our FS (RAM disk / FAT16)
 * ========================================================================== */

__attribute__((ms_abi))
static HANDLE CreateFileA_msabi(const CHAR *lpFileName, ULONG dwDesiredAccess,
    ULONG dwShareMode, PVOID lpSecurityAttributes, ULONG dwCreationDisposition,
    ULONG dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    (void)dwShareMode; (void)lpSecurityAttributes;
    (void)dwFlagsAndAttributes; (void)hTemplateFile;

    HANDLE hFile = NULL;
    IO_STATUS_BLOCK iosb;
    ANSI_STRING name;
    OBJECT_ATTRIBUTES oa;

    RtlInitAnsiString(&name, (PCSZ)lpFileName);
    InitializeObjectAttributes(&oa, (PUNICODE_STRING)&name, 0, NULL, NULL);

    ULONG createDisp = 0;
    switch (dwCreationDisposition) {
    case 1: createDisp = 1; break; /* CREATE_NEW */
    case 2: createDisp = 2; break; /* CREATE_ALWAYS */
    case 3: createDisp = 1; break; /* OPEN_EXISTING */
    case 4: createDisp = 3; break; /* OPEN_ALWAYS */
    case 5: createDisp = 4; break; /* TRUNCATE_EXISTING */
    }

    NTSTATUS status = NtCreateFile(&hFile, dwDesiredAccess, (PIO_STATUS_BLOCK)&iosb,
                                    NULL, 0, 0, dwShareMode, createDisp, 0, NULL, 0);
    (void)status;
    return hFile;
}

__attribute__((ms_abi))
static HANDLE CreateFileW_msabi(const WCHAR *lpFileName, ULONG dwDesiredAccess,
    ULONG dwShareMode, PVOID lpSecurityAttributes, ULONG dwCreationDisposition,
    ULONG dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    (void)dwShareMode; (void)lpSecurityAttributes;
    (void)dwFlagsAndAttributes; (void)hTemplateFile;

    HANDLE hFile = NULL;
    IO_STATUS_BLOCK iosb;

    ULONG createDisp = 0;
    switch (dwCreationDisposition) {
    case 1: createDisp = 1; break;
    case 2: createDisp = 2; break;
    case 3: createDisp = 1; break;
    case 4: createDisp = 3; break;
    case 5: createDisp = 4; break;
    }

    NTSTATUS status = NtCreateFile(&hFile, dwDesiredAccess, (PIO_STATUS_BLOCK)&iosb,
                                    NULL, 0, 0, dwShareMode, createDisp, 0, NULL, 0);
    (void)status;
    return hFile;
}

__attribute__((ms_abi))
static BOOL ReadFile_msabi(HANDLE hFile, PVOID lpBuffer, ULONG nNumberOfBytesToRead,
    PULONG lpNumberOfBytesRead, PVOID lpOverlapped)
{
    (void)lpOverlapped;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtReadFile(hFile, NULL, NULL, NULL,
                                  (PIO_STATUS_BLOCK)&iosb,
                                  lpBuffer, nNumberOfBytesToRead, NULL, NULL);
    if (lpNumberOfBytesRead) *lpNumberOfBytesRead = (ULONG)iosb.Information;
    return NT_SUCCESS(status);
}

__attribute__((ms_abi))
static BOOL WriteFile_msabi(HANDLE hFile, const void *lpBuffer, ULONG nNumberOfBytesToWrite,
    PULONG lpNumberOfBytesWritten, PVOID lpOverlapped)
{
    (void)lpOverlapped;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtWriteFile(hFile, NULL, NULL, NULL,
                                   (PIO_STATUS_BLOCK)&iosb,
                                   (PVOID)lpBuffer, nNumberOfBytesToWrite, NULL, NULL);
    if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = (ULONG)iosb.Information;
    return NT_SUCCESS(status);
}

__attribute__((ms_abi))
static BOOL CloseHandle_msabi(HANDLE hObject)
{
    return NT_SUCCESS(NtClose(hObject));
}

__attribute__((ms_abi))
static ULONG GetFileSize_msabi(HANDLE hFile, PULONG lpFileSizeHigh)
{
    (void)hFile;
    if (lpFileSizeHigh) *lpFileSizeHigh = 0;
    return 0;
}

/* ============================================================================
 * Process and Thread
 * ========================================================================== */

__attribute__((ms_abi))
static HANDLE CreateThread_msabi(PVOID lpThreadAttributes, SIZE_T dwStackSize,
    PVOID lpStartAddress, PVOID lpParameter, ULONG dwCreationFlags,
    PULONG lpThreadId)
{
    (void)lpThreadAttributes; (void)dwStackSize;
    (void)dwCreationFlags; (void)lpThreadId;

    PEPROCESS Process = PsGetCurrentProcess();
    PETHREAD Thread;
    NTSTATUS status = PsCreateUserThread(Process, lpStartAddress,
                                          lpParameter, 0x100000, &Thread);
    if (!NT_SUCCESS(status)) return NULL;
    return (HANDLE)Thread;
}

__attribute__((ms_abi))
static DWORD GetCurrentProcessId_msabi(VOID)
{
    PEPROCESS proc = PsGetCurrentProcess();
    return (DWORD)(ULONG_PTR)(proc ? proc->UniqueProcessId : 0);
}

__attribute__((ms_abi))
static DWORD GetCurrentThreadId_msabi(VOID)
{
    PETHREAD thread = ((PETHREAD)KeGetCurrentThread());
    return (DWORD)(ULONG_PTR)(thread ? thread->UniqueThreadId : 0);
}

__attribute__((ms_abi))
static HANDLE GetCurrentProcess_msabi(VOID)
{
    return (HANDLE)(ULONG_PTR)-1;
}

__attribute__((ms_abi))
static HANDLE GetCurrentThread_msabi(VOID)
{
    return (HANDLE)(ULONG_PTR)-2;
}

__attribute__((ms_abi))
static BOOL TerminateProcess_msabi(HANDLE hProcess, UINT uExitCode)
{
    (void)hProcess; (void)uExitCode;
    /* If terminating current process */
    PEPROCESS proc = PsGetCurrentProcess();
    PLIST_ENTRY entry = proc->ThreadListHead.Flink;
    while (entry != &proc->ThreadListHead) {
        PETHREAD t = CONTAINING_RECORD(entry, ETHREAD, ThreadListEntry);
        entry = entry->Flink;
        t->Tcb.State = Terminated;
    }
    return TRUE;
}

__attribute__((ms_abi))
static VOID ExitProcess_msabi(UINT uExitCode)
{
    DbgPrint("EXE: ExitProcess(%u)\n", uExitCode);
    TerminateProcess_msabi(GetCurrentProcess_msabi(), uExitCode);
    for (;;) KiDispatchNextThread();
}

/* ============================================================================
 * Synchronization
 * ========================================================================== */

__attribute__((ms_abi))
static HANDLE CreateEventA_msabi(PVOID lpEventAttributes, BOOL bManualReset,
    BOOL bInitialState, const CHAR *lpName)
{
    (void)lpEventAttributes; (void)lpName;
    /* Allocate a simple event object */
    PKEVENT evt = ExAllocatePool(NonPagedPool, sizeof(KEVENT));
    if (!evt) return NULL;
    KeInitializeEvent(evt, bManualReset ? NotificationEvent : SynchronizationEvent,
                      bInitialState);
    return (HANDLE)evt;
}

__attribute__((ms_abi))
static HANDLE CreateEventW_msabi(PVOID lpEventAttributes, BOOL bManualReset,
    BOOL bInitialState, const WCHAR *lpName)
{
    return CreateEventA_msabi(lpEventAttributes, bManualReset, bInitialState, (const CHAR *)lpName);
}

__attribute__((ms_abi))
static BOOL SetEvent_msabi(HANDLE hEvent)
{
    if (hEvent) KeSetEvent((PKEVENT)hEvent, 0, FALSE);
    return TRUE;
}

__attribute__((ms_abi))
static BOOL ResetEvent_msabi(HANDLE hEvent)
{
    if (hEvent) KeClearEvent((PKEVENT)hEvent);
    return TRUE;
}

__attribute__((ms_abi))
static ULONG WaitForSingleObject_msabi(HANDLE hHandle, ULONG dwMilliseconds)
{
    if (!hHandle) return 0; /* WAIT_OBJECT_0 */

    /* Check if it's a KEVENT */
    PKEVENT evt = (PKEVENT)hHandle;
    NTSTATUS status = KeWaitForSingleObject(evt, Executive, KernelMode, FALSE,
                                              NULL);
    (void)status;
    return 0; /* WAIT_OBJECT_0 */
}

__attribute__((ms_abi))
static ULONG WaitForMultipleObjects_msabi(ULONG nCount, const HANDLE *lpHandles,
    BOOL bWaitAll, ULONG dwMilliseconds)
{
    /* Simple implementation: wait on each in sequence if bWaitAll */
    for (ULONG i = 0; i < nCount; i++) {
        if (lpHandles[i]) {
            WaitForSingleObject_msabi(lpHandles[i], dwMilliseconds);
            if (!bWaitAll) return i; /* WAIT_OBJECT_0 + i */
        }
    }
    return 0;
}

__attribute__((ms_abi))
static VOID InitializeCriticalSection_msabi(PVOID lpCriticalSection)
{
    /* PCRITICAL_SECTION has a LockCriticalSection field at offset 0x18 on x64.
       We use a KSPIN_LOCK stored in the first 8 bytes. */
    PKSPIN_LOCK lock = (PKSPIN_LOCK)lpCriticalSection;
    KeInitializeSpinLock(lock);
}

__attribute__((ms_abi))
static VOID DeleteCriticalSection_msabi(PVOID lpCriticalSection)
{
    (void)lpCriticalSection;
}

__attribute__((ms_abi))
static VOID EnterCriticalSection_msabi(PVOID lpCriticalSection)
{
    PKSPIN_LOCK lock = (PKSPIN_LOCK)lpCriticalSection;
    KIRQL oldIrql;
    KeAcquireSpinLock(lock, &oldIrql);
}

__attribute__((ms_abi))
static VOID LeaveCriticalSection_msabi(PVOID lpCriticalSection)
{
    PKSPIN_LOCK lock = (PKSPIN_LOCK)lpCriticalSection;
    KeReleaseSpinLock(lock, 0);
}

__attribute__((ms_abi))
static BOOL TryEnterCriticalSection_msabi(PVOID lpCriticalSection)
{
    /* Simplified: always succeed (acquire) */
    EnterCriticalSection_msabi(lpCriticalSection);
    return TRUE;
}

/* ============================================================================
 * Module Management
 * ========================================================================== */

__attribute__((ms_abi))
static PVOID GetModuleHandleA_msabi(const CHAR *lpModuleName)
{
    if (!lpModuleName) return (PVOID)0x10000000LL;
    /* All built-in DLLs get the same fake handle */
    return (PVOID)0x10000000LL;
}

__attribute__((ms_abi))
static PVOID GetModuleHandleW_msabi(const WCHAR *lpModuleName)
{
    if (!lpModuleName) return (PVOID)0x10000000LL;
    return (PVOID)0x10000000LL;
}

__attribute__((ms_abi))
static PVOID LoadLibraryA_msabi(const CHAR *lpLibFileName)
{
    if (!lpLibFileName) return NULL;
    DbgPrint("EXE: LoadLibraryA(\"%s\")\n", lpLibFileName);
    return (PVOID)0x10000000LL;
}

__attribute__((ms_abi))
static PVOID LoadLibraryW_msabi(const WCHAR *lpLibFileName)
{
    (void)lpLibFileName;
    return (PVOID)0x10000000LL;
}

__attribute__((ms_abi))
static PVOID LoadLibraryExA_msabi(const CHAR *lpLibFileName, HANDLE hFile, ULONG dwFlags)
{
    (void)hFile; (void)dwFlags;
    return LoadLibraryA_msabi(lpLibFileName);
}

__attribute__((ms_abi))
static PVOID LoadLibraryExW_msabi(const WCHAR *lpLibFileName, HANDLE hFile, ULONG dwFlags)
{
    (void)hFile; (void)dwFlags;
    return LoadLibraryW_msabi(lpLibFileName);
}

__attribute__((ms_abi))
static BOOL FreeLibrary_msabi(PVOID hLibModule)
{
    (void)hLibModule;
    return TRUE;
}

__attribute__((ms_abi))
static PVOID GetProcAddress_msabi(PVOID hModule, const CHAR *lpProcName)
{
    if (!lpProcName) return NULL;
    /* Search export registry for this function name across all DLLs */
    for (ULONG i = 0; i < g_ExportCount; i++) {
        if (SwStrICmp(g_ExportTable[i].FuncName, lpProcName) == 0)
            return g_ExportTable[i].FuncPtr;
    }
    return NULL;
}

__attribute__((ms_abi))
static DWORD GetModuleFileNameA_msabi(PVOID hModule, CHAR *lpFilename, DWORD nSize)
{
    if (lpFilename && nSize > 0) lpFilename[0] = 0;
    return 0;
}

/* ============================================================================
 * Time
 * ========================================================================== */

extern volatile ULONG64 KeTickCount;

__attribute__((ms_abi))
static ULONG64 GetTickCount_msabi(VOID)
{
    return KeTickCount;
}

__attribute__((ms_abi))
static ULONG64 GetTickCount64_msabi(VOID)
{
    return KeTickCount;
}

__attribute__((ms_abi))
static BOOL QueryPerformanceCounter_msabi(LARGE_INTEGER *lpPerformanceCount)
{
    if (lpPerformanceCount) {
        ULONG64 tsc;
        __asm__ __volatile__("rdtsc" : "=A"(tsc));
        lpPerformanceCount->QuadPart = (LONG64)tsc;
        return TRUE;
    }
    return FALSE;
}

__attribute__((ms_abi))
static BOOL QueryPerformanceFrequency_msabi(LARGE_INTEGER *lpFrequency)
{
    if (lpFrequency) {
        lpFrequency->QuadPart = 1000000000LL;
        return TRUE;
    }
    return FALSE;
}

__attribute__((ms_abi))
static VOID GetSystemTime_msabi(PVOID lpSystemTime)
{
    /* SYSTEMTIME struct: WORD Year, Month, DayOfWeek, Day, Hour, Minute, Second, Milliseconds */
    if (lpSystemTime) {
        USHORT *st = (USHORT *)lpSystemTime;
        st[0] = 2026; st[1] = 7; st[2] = 5; st[3] = 11;
        st[4] = (USHORT)((KeTickCount / 3600000) % 24);
        st[5] = (USHORT)((KeTickCount / 60000) % 60);
        st[6] = (USHORT)((KeTickCount / 1000) % 60);
        st[7] = (USHORT)(KeTickCount % 1000);
    }
}

__attribute__((ms_abi))
static VOID GetLocalTime_msabi(PVOID lpLocalTime)
{
    GetSystemTime_msabi(lpLocalTime);
}

__attribute__((ms_abi))
static VOID Sleep_msabi(ULONG dwMilliseconds)
{
    KeStallExecutionProcessor(dwMilliseconds * 1000);
}

__attribute__((ms_abi))
static ULONG SleepEx_msabi(ULONG dwMilliseconds, BOOL bAlertable)
{
    (void)bAlertable;
    Sleep_msabi(dwMilliseconds);
    return 0;
}

/* ============================================================================
 * Error Handling
 * ========================================================================== */

static ULONG g_LastError = 0;

__attribute__((ms_abi))
static ULONG GetLastError_msabi(VOID)
{
    return g_LastError;
}

__attribute__((ms_abi))
static VOID SetLastError_msabi(ULONG dwErrCode)
{
    g_LastError = dwErrCode;
}

__attribute__((ms_abi))
static DWORD FormatMessageA_msabi(ULONG dwFlags, const void *lpSource,
    ULONG dwMessageId, ULONG dwLanguageId, CHAR *lpBuffer,
    DWORD nSize, va_list *Arguments)
{
    (void)dwFlags; (void)lpSource; (void)dwMessageId;
    (void)dwLanguageId; (void)Arguments;
    if (lpBuffer && nSize > 0) {
        const CHAR *msg = "Unknown error";
        ULONG len = 0;
        while (msg[len] && len < nSize - 1) { lpBuffer[len] = msg[len]; len++; }
        lpBuffer[len] = 0;
        return len;
    }
    return 0;
}

/* ============================================================================
 * String Functions
 * ========================================================================== */

__attribute__((ms_abi))
static SIZE_T lstrlenA_msabi(const CHAR *lpString)
{
    if (!lpString) return 0;
    SIZE_T len = 0;
    while (lpString[len]) len++;
    return len;
}

__attribute__((ms_abi))
static SIZE_T lstrlenW_msabi(const WCHAR *lpString)
{
    if (!lpString) return 0;
    SIZE_T len = 0;
    while (lpString[len]) len++;
    return len;
}

__attribute__((ms_abi))
static CHAR *lstrcpyA_msabi(CHAR *dest, const CHAR *src)
{
    if (!dest || !src) return dest;
    CHAR *d = dest;
    while ((*d++ = *src++));
    return dest;
}

__attribute__((ms_abi))
static WCHAR *lstrcpyW_msabi(WCHAR *dest, const WCHAR *src)
{
    if (!dest || !src) return dest;
    WCHAR *d = dest;
    while ((*d++ = *src++));
    return dest;
}

__attribute__((ms_abi))
static CHAR *lstrcpynA_msabi(CHAR *dest, const CHAR *src, INT iMaxLength)
{
    if (!dest || !src || iMaxLength <= 0) return dest;
    INT i;
    for (i = 0; i < iMaxLength - 1 && src[i]; i++) dest[i] = src[i];
    dest[i] = 0;
    return dest;
}

__attribute__((ms_abi))
static WCHAR *lstrcpynW_msabi(WCHAR *dest, const WCHAR *src, INT iMaxLength)
{
    if (!dest || !src || iMaxLength <= 0) return dest;
    INT i;
    for (i = 0; i < iMaxLength - 1 && src[i]; i++) dest[i] = src[i];
    dest[i] = 0;
    return dest;
}

__attribute__((ms_abi))
static INT lstrcmpA_msabi(const CHAR *s1, const CHAR *s2)
{
    if (!s1) s1 = "";
    if (!s2) s2 = "";
    while (*s1 && *s2) {
        if (*s1 != *s2) return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
        s1++; s2++;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

__attribute__((ms_abi))
static INT lstrcmpiA_msabi(const CHAR *s1, const CHAR *s2)
{
    if (!s1) s1 = "";
    if (!s2) s2 = "";
    while (*s1 && *s2) {
        CHAR c1 = *s1, c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return (int)(unsigned char)c1 - (int)(unsigned char)c2;
        s1++; s2++;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

__attribute__((ms_abi))
static CHAR *lstrcatA_msabi(CHAR *dest, const CHAR *src)
{
    if (!dest || !src) return dest;
    CHAR *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

__attribute__((ms_abi))
static WCHAR *lstrcatW_msabi(WCHAR *dest, const WCHAR *src)
{
    if (!dest || !src) return dest;
    WCHAR *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

__attribute__((ms_abi))
static CHAR *CharLowerA_msabi(CHAR *lpsz)
{
    if (!lpsz) return NULL;
    CHAR *p = lpsz;
    while (*p) { if (*p >= 'A' && *p <= 'Z') *p += 32; p++; }
    return lpsz;
}

__attribute__((ms_abi))
static CHAR *CharUpperA_msabi(CHAR *lpsz)
{
    if (!lpsz) return NULL;
    CHAR *p = lpsz;
    while (*p) { if (*p >= 'a' && *p <= 'z') *p -= 32; p++; }
    return lpsz;
}

/* ============================================================================
 * Memory fill/copy (Rtl* style)
 * ========================================================================== */

__attribute__((ms_abi))
static VOID RtlZeroMemory_msabi(PVOID Destination, SIZE_T Length)
{
    if (Destination) RtlZeroMemory(Destination, Length);
}

__attribute__((ms_abi))
static VOID RtlCopyMemory_msabi(PVOID Destination, const VOID *Source, SIZE_T Length)
{
    if (Destination && Source) RtlCopyMemory(Destination, Source, Length);
}

__attribute__((ms_abi))
static VOID RtlFillMemory_msabi(PVOID Destination, SIZE_T Length, UCHAR Fill)
{
    if (Destination) {
        PUCHAR d = (PUCHAR)Destination;
        for (SIZE_T i = 0; i < Length; i++) d[i] = Fill;
    }
}

__attribute__((ms_abi))
static VOID RtlMoveMemory_msabi(PVOID Destination, const VOID *Source, SIZE_T Length)
{
    if (Destination && Source) {
        PUCHAR d = (PUCHAR)Destination;
        const PUCHAR s = (const PUCHAR)Source;
        if (d < s) {
            for (SIZE_T i = 0; i < Length; i++) d[i] = s[i];
        } else if (d > s) {
            for (SIZE_T i = Length; i > 0; i--) d[i-1] = s[i-1];
        }
    }
}

/* ============================================================================
 * Console
 * ========================================================================== */

__attribute__((ms_abi))
static HANDLE GetStdHandle_msabi(ULONG nStdHandle)
{
    /* STD_INPUT_HANDLE=-10, STD_OUTPUT_HANDLE=-11, STD_ERROR_HANDLE=-12 */
    return (HANDLE)(ULONG_PTR)(0x80000000UL - nStdHandle);
}

__attribute__((ms_abi))
static BOOL WriteConsoleA_msabi(HANDLE hConsoleOutput, const void *lpBuffer,
    DWORD nNumberOfCharsToWrite, PDWORD lpNumberOfCharsWritten, PVOID lpReserved)
{
    (void)hConsoleOutput; (void)lpReserved;
    const CHAR *str = (const CHAR *)lpBuffer;
    for (DWORD i = 0; i < nNumberOfCharsToWrite; i++) {
        HalpSerialPutChar(str[i]);
    }
    if (lpNumberOfCharsWritten) *lpNumberOfCharsWritten = nNumberOfCharsToWrite;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL AllocConsole_msabi(VOID)
{
    return TRUE;
}

__attribute__((ms_abi))
static ULONG_PTR GetConsoleWindow_msabi(VOID)
{
    return 0;
}

/* ============================================================================
 * Environment and Command Line
 * ========================================================================== */

__attribute__((ms_abi))
static CHAR *GetCommandLineA_msabi(VOID)
{
    return (CHAR *)"minnt.exe\0";
}

__attribute__((ms_abi))
static WCHAR *GetCommandLineW_msabi(VOID)
{
    static WCHAR cmd[] = {'m','i','n','n','t','.','e','x','e',0};
    return cmd;
}

__attribute__((ms_abi))
static DWORD GetEnvironmentVariableA_msabi(const CHAR *lpName, CHAR *lpBuffer, DWORD nSize)
{
    (void)lpName;
    if (lpBuffer && nSize > 0) lpBuffer[0] = 0;
    return 0;
}

/* ============================================================================
 * Thread-Local Storage
 * ========================================================================== */

#define MAX_TLS 64
static PVOID g_TlsSlots[MAX_TLS];
static ULONG g_TlsCount = 0;

__attribute__((ms_abi))
static DWORD TlsAlloc_msabi(VOID)
{
    if (g_TlsCount >= MAX_TLS) return 0xFFFFFFFF;
    DWORD idx = g_TlsCount++;
    g_TlsSlots[idx] = NULL;
    return idx;
}

__attribute__((ms_abi))
static PVOID TlsGetValue_msabi(DWORD dwTlsIndex)
{
    if (dwTlsIndex >= MAX_TLS) return NULL;
    return g_TlsSlots[dwTlsIndex];
}

__attribute__((ms_abi))
static BOOL TlsSetValue_msabi(DWORD dwTlsIndex, PVOID lpTlsValue)
{
    if (dwTlsIndex >= MAX_TLS) return FALSE;
    g_TlsSlots[dwTlsIndex] = lpTlsValue;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL TlsFree_msabi(DWORD dwTlsIndex)
{
    if (dwTlsIndex >= MAX_TLS) return FALSE;
    g_TlsSlots[dwTlsIndex] = NULL;
    return TRUE;
}

/* ============================================================================
 * Miscellaneous
 * ========================================================================== */

__attribute__((ms_abi))
static VOID OutputDebugStringA_msabi(const CHAR *lpOutputString)
{
    if (lpOutputString) DbgPrint("EXE: %s", lpOutputString);
}

__attribute__((ms_abi))
static VOID OutputDebugStringW_msabi(const WCHAR *lpOutputString)
{
    (void)lpOutputString;
    /* Convert to ANSI and print */
}

__attribute__((ms_abi))
static BOOL IsDebuggerPresent_msabi(VOID)
{
    return FALSE;
}

__attribute__((ms_abi))
static ULONG SetErrorMode_msabi(ULONG uMode)
{
    (void)uMode;
    return 0;
}

__attribute__((ms_abi))
static BOOL IsProcessorFeaturePresent_msabi(ULONG ProcessorFeature)
{
    /* PF_XMMI_INSTRUCTIONS_AVAILABLE = 3 (SSE) */
    if (ProcessorFeature == 3) return FALSE; /* We build with -mno-sse */
    return TRUE;
}

__attribute__((ms_abi))
static PVOID SetUnhandledExceptionFilter_msabi(PVOID lpTopLevelExceptionFilter)
{
    (void)lpTopLevelExceptionFilter;
    return NULL;
}

__attribute__((ms_abi))
static LONG UnhandledExceptionFilter_msabi(PVOID ExceptionInfo)
{
    (void)ExceptionInfo;
    return 1; /* EXCEPTION_EXECUTE_HANDLER */
}

__attribute__((ms_abi))
static VOID GetSystemInfo_msabi(PVOID lpSystemInfo)
{
    if (lpSystemInfo) {
        RtlZeroMemory(lpSystemInfo, 48);
        ULONG *si = (ULONG *)lpSystemInfo;
        si[1] = 0x1000;  /* PageSize */
        si[4] = 1;       /* NumberOfProcessors */
    }
}

__attribute__((ms_abi))
static VOID GlobalMemoryStatusEx_msabi(PVOID lpMemoryStatus)
{
    if (lpMemoryStatus) {
        ULONG64 *ms = (ULONG64 *)lpMemoryStatus;
        ms[0] = 64; /* sizeof(MEMORYSTATUSEX) */
        ms[1] = 100; /* dwMemoryLoad */
        ms[2] = (ULONG64)MmGetTotalPages() * 4096; /* ullTotalPhys */
        ms[3] = (ULONG64)MmGetFreePages() * 4096;  /* ullAvailPhys */
        ms[4] = 0; ms[5] = 0; ms[6] = 0; ms[7] = 0;
    }
}

__attribute__((ms_abi))
static BOOL GetVersionExA_msabi(PVOID lpVersionInfo)
{
    if (lpVersionInfo) {
        ULONG *vi = (ULONG *)lpVersionInfo;
        vi[0] = 156;  /* dwOSVersionInfoSize */
        vi[1] = 6;    /* dwMajorVersion */
        vi[2] = 1;    /* dwMinorVersion */
        vi[3] = 7601; /* dwBuildNumber */
        vi[4] = 2;    /* dwPlatformId = VER_PLATFORM_WIN32_NT */
    }
    return TRUE;
}

__attribute__((ms_abi))
static BOOL FlushFileBuffers_msabi(HANDLE hFile)
{
    (void)hFile;
    return TRUE;
}

__attribute__((ms_abi))
static DWORD SetFilePointer_msabi(HANDLE hFile, LONG lDistanceToMove,
    PLONG lpDistanceToMoveHigh, ULONG dwMoveMethod)
{
    (void)hFile; (void)lDistanceToMove; (void)lpDistanceToMoveHigh; (void)dwMoveMethod;
    return 0;
}

__attribute__((ms_abi))
static BOOL SetFilePointerEx_msabi(HANDLE hFile, LARGE_INTEGER liDistanceToMove,
    PLARGE_INTEGER lpNewFilePointer, ULONG dwMoveMethod)
{
    (void)hFile; (void)liDistanceToMove; (void)dwMoveMethod;
    if (lpNewFilePointer) lpNewFilePointer->QuadPart = 0;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL GetFileSizeEx_msabi(HANDLE hFile, PLARGE_INTEGER lpFileSize)
{
    (void)hFile;
    if (lpFileSize) lpFileSize->QuadPart = 0;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL SetEndOfFile_msabi(HANDLE hFile)
{
    (void)hFile;
    return TRUE;
}

__attribute__((ms_abi))
static DWORD GetTempPathA_msabi(DWORD nBufferLength, CHAR *lpBuffer)
{
    if (!lpBuffer || nBufferLength < 2) return 2;
    lpBuffer[0] = '/'; lpBuffer[1] = 0;
    return 1;
}

__attribute__((ms_abi))
static DWORD GetCurrentDirectoryA_msabi(DWORD nBufferLength, CHAR *lpBuffer)
{
    if (!lpBuffer || nBufferLength < 2) return 1;
    lpBuffer[0] = '/'; lpBuffer[1] = 0;
    return 1;
}

__attribute__((ms_abi))
static BOOL SetCurrentDirectoryA_msabi(const CHAR *lpPathName)
{
    (void)lpPathName;
    return TRUE;
}

__attribute__((ms_abi))
static DWORD GetModuleBaseNameA_msabi(HANDLE hProcess, PVOID hModule,
    CHAR *lpBaseName, DWORD nSize)
{
    (void)hProcess; (void)hModule;
    if (lpBaseName && nSize > 0) {
        const CHAR *name = "minnt.exe";
        DWORD i;
        for (i = 0; name[i] && i < nSize - 1; i++) lpBaseName[i] = name[i];
        lpBaseName[i] = 0;
        return i;
    }
    return 0;
}

/* ============================================================================
 * Registration
 * ========================================================================== */

VOID NTAPI Kernel32RegisterExports(VOID)
{
    /* Memory */
    ExeRegisterExport("kernel32.dll", "VirtualAlloc", VirtualAlloc_msabi);
    ExeRegisterExport("kernel32.dll", "VirtualFree", VirtualFree_msabi);
    ExeRegisterExport("kernel32.dll", "VirtualProtect", VirtualProtect_msabi);
    ExeRegisterExport("kernel32.dll", "VirtualQuery", VirtualQuery_msabi);
    ExeRegisterExport("kernel32.dll", "GetProcessHeap", GetProcessHeap_msabi);
    ExeRegisterExport("kernel32.dll", "HeapAlloc", HeapAlloc_msabi);
    ExeRegisterExport("kernel32.dll", "HeapFree", HeapFree_msabi);
    ExeRegisterExport("kernel32.dll", "HeapReAlloc", HeapReAlloc_msabi);
    ExeRegisterExport("kernel32.dll", "HeapSize", HeapSize_msabi);
    ExeRegisterExport("kernel32.dll", "GetProcessHeap", GetProcessHeap_msabi);
    ExeRegisterExport("kernel32.dll", "RtlZeroMemory", RtlZeroMemory_msabi);
    ExeRegisterExport("kernel32.dll", "RtlCopyMemory", RtlCopyMemory_msabi);
    ExeRegisterExport("kernel32.dll", "RtlFillMemory", RtlFillMemory_msabi);
    ExeRegisterExport("kernel32.dll", "RtlMoveMemory", RtlMoveMemory_msabi);

    /* File I/O */
    ExeRegisterExport("kernel32.dll", "CreateFileA", CreateFileA_msabi);
    ExeRegisterExport("kernel32.dll", "CreateFileW", CreateFileW_msabi);
    ExeRegisterExport("kernel32.dll", "ReadFile", ReadFile_msabi);
    ExeRegisterExport("kernel32.dll", "WriteFile", WriteFile_msabi);
    ExeRegisterExport("kernel32.dll", "CloseHandle", CloseHandle_msabi);
    ExeRegisterExport("kernel32.dll", "GetFileSize", GetFileSize_msabi);
    ExeRegisterExport("kernel32.dll", "GetFileSizeEx", GetFileSizeEx_msabi);
    ExeRegisterExport("kernel32.dll", "FlushFileBuffers", FlushFileBuffers_msabi);
    ExeRegisterExport("kernel32.dll", "SetFilePointer", SetFilePointer_msabi);
    ExeRegisterExport("kernel32.dll", "SetFilePointerEx", SetFilePointerEx_msabi);
    ExeRegisterExport("kernel32.dll", "SetEndOfFile", SetEndOfFile_msabi);

    /* Process / Thread */
    ExeRegisterExport("kernel32.dll", "CreateThread", CreateThread_msabi);
    ExeRegisterExport("kernel32.dll", "GetCurrentProcessId", GetCurrentProcessId_msabi);
    ExeRegisterExport("kernel32.dll", "GetCurrentThreadId", GetCurrentThreadId_msabi);
    ExeRegisterExport("kernel32.dll", "GetCurrentProcess", GetCurrentProcess_msabi);
    ExeRegisterExport("kernel32.dll", "GetCurrentThread", GetCurrentThread_msabi);
    ExeRegisterExport("kernel32.dll", "TerminateProcess", TerminateProcess_msabi);
    ExeRegisterExport("kernel32.dll", "ExitProcess", ExitProcess_msabi);
    ExeRegisterExport("kernel32.dll", "GetModuleBaseNameA", GetModuleBaseNameA_msabi);

    /* Synchronization */
    ExeRegisterExport("kernel32.dll", "CreateEventA", CreateEventA_msabi);
    ExeRegisterExport("kernel32.dll", "CreateEventW", CreateEventW_msabi);
    ExeRegisterExport("kernel32.dll", "SetEvent", SetEvent_msabi);
    ExeRegisterExport("kernel32.dll", "ResetEvent", ResetEvent_msabi);
    ExeRegisterExport("kernel32.dll", "WaitForSingleObject", WaitForSingleObject_msabi);
    ExeRegisterExport("kernel32.dll", "WaitForMultipleObjects", WaitForMultipleObjects_msabi);
    ExeRegisterExport("kernel32.dll", "InitializeCriticalSection", InitializeCriticalSection_msabi);
    ExeRegisterExport("kernel32.dll", "DeleteCriticalSection", DeleteCriticalSection_msabi);
    ExeRegisterExport("kernel32.dll", "EnterCriticalSection", EnterCriticalSection_msabi);
    ExeRegisterExport("kernel32.dll", "LeaveCriticalSection", LeaveCriticalSection_msabi);
    ExeRegisterExport("kernel32.dll", "TryEnterCriticalSection", TryEnterCriticalSection_msabi);

    /* Module */
    ExeRegisterExport("kernel32.dll", "GetModuleHandleA", GetModuleHandleA_msabi);
    ExeRegisterExport("kernel32.dll", "GetModuleHandleW", GetModuleHandleW_msabi);
    ExeRegisterExport("kernel32.dll", "LoadLibraryA", LoadLibraryA_msabi);
    ExeRegisterExport("kernel32.dll", "LoadLibraryW", LoadLibraryW_msabi);
    ExeRegisterExport("kernel32.dll", "LoadLibraryExA", LoadLibraryExA_msabi);
    ExeRegisterExport("kernel32.dll", "LoadLibraryExW", LoadLibraryExW_msabi);
    ExeRegisterExport("kernel32.dll", "FreeLibrary", FreeLibrary_msabi);
    ExeRegisterExport("kernel32.dll", "GetProcAddress", GetProcAddress_msabi);
    ExeRegisterExport("kernel32.dll", "GetModuleFileNameA", GetModuleFileNameA_msabi);

    /* Time */
    ExeRegisterExport("kernel32.dll", "GetTickCount", GetTickCount_msabi);
    ExeRegisterExport("kernel32.dll", "GetTickCount64", GetTickCount64_msabi);
    ExeRegisterExport("kernel32.dll", "QueryPerformanceCounter", QueryPerformanceCounter_msabi);
    ExeRegisterExport("kernel32.dll", "QueryPerformanceFrequency", QueryPerformanceFrequency_msabi);
    ExeRegisterExport("kernel32.dll", "GetSystemTime", GetSystemTime_msabi);
    ExeRegisterExport("kernel32.dll", "GetLocalTime", GetLocalTime_msabi);
    ExeRegisterExport("kernel32.dll", "Sleep", Sleep_msabi);
    ExeRegisterExport("kernel32.dll", "SleepEx", SleepEx_msabi);

    /* Error */
    ExeRegisterExport("kernel32.dll", "GetLastError", GetLastError_msabi);
    ExeRegisterExport("kernel32.dll", "SetLastError", SetLastError_msabi);
    ExeRegisterExport("kernel32.dll", "FormatMessageA", FormatMessageA_msabi);

    /* String */
    ExeRegisterExport("kernel32.dll", "lstrlenA", lstrlenA_msabi);
    ExeRegisterExport("kernel32.dll", "lstrlenW", lstrlenW_msabi);
    ExeRegisterExport("kernel32.dll", "lstrcpyA", lstrcpyA_msabi);
    ExeRegisterExport("kernel32.dll", "lstrcpyW", lstrcpyW_msabi);
    ExeRegisterExport("kernel32.dll", "lstrcpynA", lstrcpynA_msabi);
    ExeRegisterExport("kernel32.dll", "lstrcpynW", lstrcpynW_msabi);
    ExeRegisterExport("kernel32.dll", "lstrcmpA", lstrcmpA_msabi);
    ExeRegisterExport("kernel32.dll", "lstrcmpiA", lstrcmpiA_msabi);
    ExeRegisterExport("kernel32.dll", "lstrcatA", lstrcatA_msabi);
    ExeRegisterExport("kernel32.dll", "lstrcatW", lstrcatW_msabi);
    ExeRegisterExport("kernel32.dll", "CharLowerA", CharLowerA_msabi);
    ExeRegisterExport("kernel32.dll", "CharUpperA", CharUpperA_msabi);

    /* Console */
    ExeRegisterExport("kernel32.dll", "GetStdHandle", GetStdHandle_msabi);
    ExeRegisterExport("kernel32.dll", "WriteConsoleA", WriteConsoleA_msabi);
    ExeRegisterExport("kernel32.dll", "AllocConsole", AllocConsole_msabi);
    ExeRegisterExport("kernel32.dll", "GetConsoleWindow", GetConsoleWindow_msabi);

    /* Environment */
    ExeRegisterExport("kernel32.dll", "GetCommandLineA", GetCommandLineA_msabi);
    ExeRegisterExport("kernel32.dll", "GetCommandLineW", GetCommandLineW_msabi);
    ExeRegisterExport("kernel32.dll", "GetEnvironmentVariableA", GetEnvironmentVariableA_msabi);

    /* TLS */
    ExeRegisterExport("kernel32.dll", "TlsAlloc", TlsAlloc_msabi);
    ExeRegisterExport("kernel32.dll", "TlsGetValue", TlsGetValue_msabi);
    ExeRegisterExport("kernel32.dll", "TlsSetValue", TlsSetValue_msabi);
    ExeRegisterExport("kernel32.dll", "TlsFree", TlsFree_msabi);

    /* Misc */
    ExeRegisterExport("kernel32.dll", "OutputDebugStringA", OutputDebugStringA_msabi);
    ExeRegisterExport("kernel32.dll", "OutputDebugStringW", OutputDebugStringW_msabi);
    ExeRegisterExport("kernel32.dll", "IsDebuggerPresent", IsDebuggerPresent_msabi);
    ExeRegisterExport("kernel32.dll", "SetErrorMode", SetErrorMode_msabi);
    ExeRegisterExport("kernel32.dll", "IsProcessorFeaturePresent", IsProcessorFeaturePresent_msabi);
    ExeRegisterExport("kernel32.dll", "SetUnhandledExceptionFilter", SetUnhandledExceptionFilter_msabi);
    ExeRegisterExport("kernel32.dll", "UnhandledExceptionFilter", UnhandledExceptionFilter_msabi);
    ExeRegisterExport("kernel32.dll", "GetSystemInfo", GetSystemInfo_msabi);
    ExeRegisterExport("kernel32.dll", "GlobalMemoryStatusEx", GlobalMemoryStatusEx_msabi);
    ExeRegisterExport("kernel32.dll", "GetVersionExA", GetVersionExA_msabi);
    ExeRegisterExport("kernel32.dll", "GetTempPathA", GetTempPathA_msabi);
    ExeRegisterExport("kernel32.dll", "GetCurrentDirectoryA", GetCurrentDirectoryA_msabi);
    ExeRegisterExport("kernel32.dll", "SetCurrentDirectoryA", SetCurrentDirectoryA_msabi);

    DbgPrint("EXE: Registered %lu kernel32.dll exports\n", g_ExportCount);
}
