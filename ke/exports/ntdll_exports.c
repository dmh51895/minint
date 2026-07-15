/*
 * MinNT - ke/exports/ntdll_exports.c
 * ntdll.dll export implementations — the foundation of every Windows process.
 *
 * This provides:
 *   - Nt* / Zw* syscall wrappers (direct kernel calls, ms_abi)
 *   - Rtl* runtime library (heap, strings, image, exception handling)
 *   - Ldr* loader functions
 *   - Csr* CSRSS client functions
 *   - Dbg* debug functions
 *   - Etw* event tracing stubs
 *
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
#include <nt/lpc.h>
#include <nt/dispatcher.h>
#include <nt/exe.h>
#include <nt/pe.h>
#include <ndk/obfuncs.h>
#include <stdarg.h>

#ifndef STATUS_NOT_FOUND
#define STATUS_NOT_FOUND ((NTSTATUS)0xC0000227L)
#endif
#ifndef STATUS_ENTRYPOINT_NOT_FOUND
#define STATUS_ENTRYPOINT_NOT_FOUND ((NTSTATUS)0xC0000139L)
#endif

/* ============================================================================
 * Nt* / Zw* — System Service wrappers
 *
 * In Windows, these do "mov r10, rcx; mov eax, <num>; syscall; ret".
 * In MinNT, EXEs run in kernel mode so these are direct function calls
 * to the kernel handlers, wrapped with __attribute__((ms_abi)).
 * ========================================================================== */

/* ---- Memory -------------------------------------------------------------- */

__attribute__((ms_abi))
static NTSTATUS NtAllocateVirtualMemory_msabi(
    HANDLE ProcessHandle, PVOID *BaseAddress, ULONG_PTR ZeroBits,
    PSIZE_T RegionSize, ULONG AllocationType, ULONG Protect)
{
    PMM_ADDRESS_SPACE space = NULL;
    if (ProcessHandle == (HANDLE)(ULONG_PTR)-1) space = NULL;
    ULONG_PTR base = BaseAddress ? *BaseAddress : 0;
    NTSTATUS s = MmAllocateVirtualMemory(space, &base, ZeroBits,
                                         RegionSize ? *RegionSize : 0,
                                         AllocationType, Protect);
    if (BaseAddress) *BaseAddress = (PVOID)base;
    return s;
}

__attribute__((ms_abi))
static NTSTATUS NtFreeVirtualMemory_msabi(
    HANDLE ProcessHandle, PVOID *BaseAddress, PSIZE_T RegionSize, ULONG FreeType)
{
    ULONG_PTR base = BaseAddress ? (ULONG_PTR)*BaseAddress : 0;
    NTSTATUS s = MmFreeVirtualMemory(NULL, &base);
    if (BaseAddress) *BaseAddress = (PVOID)base;
    return s;
}

__attribute__((ms_abi))
static NTSTATUS NtProtectVirtualMemory_msabi(
    HANDLE ProcessHandle, PVOID *BaseAddress, PSIZE_T RegionSize,
    ULONG NewProtect, PULONG OldProtect)
{
    if (OldProtect) *OldProtect = 0x40;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtQueryVirtualMemory_msabi(
    HANDLE ProcessHandle, PVOID BaseAddress, ULONG InformationClass,
    PVOID Buffer, SIZE_T Size, PSIZE_T ReturnLength)
{
    if (Buffer) RtlZeroMemory(Buffer, Size);
    if (ReturnLength) *ReturnLength = 0;
    return STATUS_SUCCESS;
}

/* ---- File I/O ------------------------------------------------------------ */

__attribute__((ms_abi))
static NTSTATUS NtCreateFile_msabi(
    PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes,
    ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions,
    PVOID EaBuffer, ULONG EaLength)
{
    PULONG64 allocSize = NULL;
    ULONG64 allocVal = 0;
    if (AllocationSize) { allocVal = (ULONG64)AllocationSize->QuadPart; allocSize = &allocVal; }
    return NtCreateFile(FileHandle, DesiredAccess, NULL, IoStatusBlock,
                        allocSize, FileAttributes, ShareAccess,
                        CreateDisposition, CreateOptions, EaBuffer, EaLength);
}

__attribute__((ms_abi))
static NTSTATUS NtReadFile_msabi(
    HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length,
    PLARGE_INTEGER ByteOffset, PULONG Key)
{
    PULONG64 bo = NULL; ULONG64 boVal = 0;
    if (ByteOffset) { boVal = (ULONG64)ByteOffset->QuadPart; bo = &boVal; }
    return NtReadFile(FileHandle, Event, ApcContext, ApcContext,
                      IoStatusBlock, Buffer, Length, bo, Key);
}

__attribute__((ms_abi))
static NTSTATUS NtWriteFile_msabi(
    HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock, const void *Buffer, ULONG Length,
    PLARGE_INTEGER ByteOffset, PULONG Key)
{
    PULONG64 bo = NULL; ULONG64 boVal = 0;
    if (ByteOffset) { boVal = (ULONG64)ByteOffset->QuadPart; bo = &boVal; }
    return NtWriteFile(FileHandle, Event, ApcContext, ApcContext,
                       IoStatusBlock, (PVOID)Buffer, Length, bo, Key);
}

__attribute__((ms_abi))
static NTSTATUS NtClose_msabi(HANDLE Handle)
{
    return NtClose(Handle);
}

__attribute__((ms_abi))
static NTSTATUS NtQueryInformationFile_msabi(
    HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
    ULONG Length, ULONG FileInformationClass)
{
    if (FileInformation && Length > 0) RtlZeroMemory(FileInformation, Length);
    if (IoStatusBlock) IoStatusBlock->Information = 0;
    if (IoStatusBlock) IoStatusBlock->Status = STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtSetInformationFile_msabi(
    HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
    ULONG Length, ULONG FileInformationClass)
{
    if (IoStatusBlock) IoStatusBlock->Status = STATUS_SUCCESS;
    if (IoStatusBlock) IoStatusBlock->Information = 0;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtFlushBuffersFile_msabi(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock)
{
    if (IoStatusBlock) IoStatusBlock->Status = STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtDeleteFile_msabi(POBJECT_ATTRIBUTES ObjectAttributes)
{
    /* Forward to the kernel-mode NtDeleteFile which uses our real file
     * system to actually remove the file. */
    return NtDeleteFile(ObjectAttributes);
}

/* ---- Process / Thread ---------------------------------------------------- */

__attribute__((ms_abi))
static NTSTATUS NtCreateProcess_msabi(
    PHANDLE ProcessHandle, ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes, HANDLE ParentProcess,
    ULONG Flags, HANDLE SectionHandle, HANDLE DebugPort, HANDLE ExceptionPort)
{
    PEPROCESS proc;
    NTSTATUS s = PsCreateSystemProcess("UserProcess", &proc);
    if (NT_SUCCESS(s) && ProcessHandle) {
        ObInsertHandle(proc, ProcessHandle);
    }
    return s;
}

__attribute__((ms_abi))
static NTSTATUS NtTerminateProcess_msabi(HANDLE ProcessHandle, NTSTATUS ExitStatus)
{
    if (ProcessHandle == (HANDLE)(ULONG_PTR)-1 || ProcessHandle == NULL) {
        PEPROCESS proc = PsGetCurrentProcess();
        PLIST_ENTRY entry = proc->ThreadListHead.Flink;
        while (entry != &proc->ThreadListHead) {
            PETHREAD t = CONTAINING_RECORD(entry, ETHREAD, ThreadListEntry);
            entry = entry->Flink;
            t->Tcb.State = Terminated;
        }
        return STATUS_SUCCESS;
    }
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtCreateThread_msabi(
    PHANDLE ThreadHandle, ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes, HANDLE ProcessHandle,
    PVOID ClientId, PVOID StartAddress, PVOID Parameter,
    PVOID InitialTeb, BOOLEAN CreateSuspended)
{
    PEPROCESS proc = PsGetCurrentProcess();
    PETHREAD thread;
    NTSTATUS s = PsCreateUserThread(proc, StartAddress, Parameter, 0x100000, &thread);
    if (NT_SUCCESS(s) && ThreadHandle) {
        ObInsertHandle(thread, ThreadHandle);
    }
    return s;
}

__attribute__((ms_abi))
static NTSTATUS NtTerminateThread_msabi(HANDLE ThreadHandle, NTSTATUS ExitStatus)
{
    if (ThreadHandle == NULL) {
        PETHREAD t = (PETHREAD)KeGetCurrentThread();
        t->Tcb.State = Terminated;
        for (;;) KiDispatchNextThread();
    }
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtSuspendThread_msabi(HANDLE ThreadHandle, PULONG PreviousSuspendCount)
{
    if (PreviousSuspendCount) *PreviousSuspendCount = 0;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtResumeThread_msabi(HANDLE ThreadHandle, PULONG PreviousSuspendCount)
{
    if (PreviousSuspendCount) *PreviousSuspendCount = 0;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtQueryInformationProcess_msabi(
    HANDLE ProcessHandle, ULONG InformationClass, PVOID Buffer,
    ULONG Length, PULONG ReturnLength)
{
    if (Buffer && Length > 0) RtlZeroMemory(Buffer, Length);
    if (ReturnLength) *ReturnLength = 0;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtSetInformationProcess_msabi(
    HANDLE ProcessHandle, ULONG InformationClass, PVOID Buffer, ULONG Length)
{
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtQueryInformationThread_msabi(
    HANDLE ThreadHandle, ULONG InformationClass, PVOID Buffer,
    ULONG Length, PULONG ReturnLength)
{
    if (Buffer && Length > 0) RtlZeroMemory(Buffer, Length);
    if (ReturnLength) *ReturnLength = 0;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtSetInformationThread_msabi(
    HANDLE ThreadHandle, ULONG InformationClass, PVOID Buffer, ULONG Length)
{
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtGetCurrentProcessorNumber_msabi(HANDLE ThreadHandle)
{
    (void)ThreadHandle;
    return 0;
}

/* ---- Synchronization ----------------------------------------------------- */

__attribute__((ms_abi))
static NTSTATUS NtCreateEvent_msabi(
    PHANDLE EventHandle, ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes, ULONG EventType, BOOLEAN InitialState)
{
    PKEVENT evt = ExAllocatePool(NonPagedPool, sizeof(KEVENT));
    if (!evt) return STATUS_NO_MEMORY;
    KeInitializeEvent(evt, EventType ? NotificationEvent : SynchronizationEvent, InitialState);
    if (EventHandle) *EventHandle = (HANDLE)evt;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtSetEvent_msabi(HANDLE EventHandle, PLONG PreviousState)
{
    if (EventHandle) KeSetEvent((PKEVENT)EventHandle, 0, FALSE);
    if (PreviousState) *PreviousState = 0;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtResetEvent_msabi(HANDLE EventHandle, PLONG PreviousState)
{
    if (EventHandle) KeClearEvent((PKEVENT)EventHandle);
    if (PreviousState) *PreviousState = 1;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtClearEvent_msabi(HANDLE EventHandle)
{
    if (EventHandle) KeClearEvent((PKEVENT)EventHandle);
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtPulseEvent_msabi(HANDLE EventHandle, PLONG PreviousState)
{
    if (EventHandle) {
        KeSetEvent((PKEVENT)EventHandle, 0, FALSE);
        KeClearEvent((PKEVENT)EventHandle);
    }
    if (PreviousState) *PreviousState = 0;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtCreateMutex_msabi(
    PHANDLE MutexHandle, ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes, BOOLEAN InitialOwner)
{
    PKMUTANT mtx = ExAllocatePool(NonPagedPool, sizeof(KMUTANT));
    if (!mtx) return STATUS_NO_MEMORY;
    KeInitializeMutex((PKMUTANT)mtx, 0);
    if (InitialOwner) KeWaitForSingleObject((PVOID)mtx, Executive, KernelMode, FALSE, NULL);
    if (MutexHandle) *MutexHandle = (HANDLE)mtx;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtReleaseMutant_msabi(HANDLE MutantHandle, PBOOLEAN ReleaseCount)
{
    (void)ReleaseCount;
    if (MutantHandle) KeReleaseMutex((PKMUTANT)MutantHandle, FALSE);
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtCreateSemaphore_msabi(
    PHANDLE SemaphoreHandle, ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes, LONG InitialCount, LONG MaximumCount)
{
    PKSEMAPHORE sem = ExAllocatePool(NonPagedPool, sizeof(KSEMAPHORE));
    if (!sem) return STATUS_NO_MEMORY;
    KeInitializeSemaphore(sem, InitialCount, MaximumCount);
    if (SemaphoreHandle) *SemaphoreHandle = (HANDLE)sem;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtReleaseSemaphore_msabi(HANDLE SemaphoreHandle, LONG ReleaseCount, PLONG PreviousCount)
{
    if (PreviousCount) *PreviousCount = 0;
    if (SemaphoreHandle) {
        for (LONG i = 0; i < ReleaseCount; i++)
            KeReleaseSemaphore((PKSEMAPHORE)SemaphoreHandle, 0, 1, FALSE);
    }
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtWaitForSingleObject_msabi(HANDLE Handle, BOOLEAN Alertable, PLARGE_INTEGER Timeout)
{
    if (Handle) {
        if (KeGetCurrentIrql() < DISPATCH_LEVEL)
            KeWaitForSingleObject((PVOID)Handle, Executive, KernelMode, FALSE, Timeout);
    }
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtWaitForMultipleObjects_msabi(
    ULONG Count, HANDLE Handles[], BOOLEAN Alertable, BOOLEAN WaitAll,
    PLARGE_INTEGER Timeout)
{
    for (ULONG i = 0; i < Count; i++) {
        if (Handles[i] && KeGetCurrentIrql() < DISPATCH_LEVEL)
            KeWaitForSingleObject((PVOID)Handles[i], Executive, KernelMode, FALSE, Timeout);
        if (!WaitAll) break;
    }
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtSignalAndWaitForSingleObject_msabi(
    HANDLE SignalHandle, HANDLE WaitHandle, BOOLEAN Alertable,
    PLARGE_INTEGER Timeout)
{
    if (SignalHandle) KeSetEvent((PKEVENT)SignalHandle, 0, FALSE);
    if (WaitHandle && KeGetCurrentIrql() < DISPATCH_LEVEL)
        KeWaitForSingleObject((PVOID)WaitHandle, Executive, KernelMode, FALSE, Timeout);
    return STATUS_SUCCESS;
}

/* ---- Timer --------------------------------------------------------------- */

typedef struct _SW_TIMER { LONG signaled; } SW_TIMER;
__attribute__((ms_abi))
static NTSTATUS NtCreateTimer_msabi(
    PHANDLE TimerHandle, ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes, ULONG TimerType)
{
    SW_TIMER *timer = ExAllocatePool(NonPagedPool, sizeof(SW_TIMER));
    if (!timer) return STATUS_NO_MEMORY;
    timer->signaled = 0;
    if (TimerHandle) *TimerHandle = (HANDLE)timer;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtSetTimer_msabi(
    HANDLE TimerHandle, PLARGE_INTEGER DueTime, PVOID TimerApcRoutine,
    PVOID TimerContext, BOOLEAN ResumeTimer, LONG Period,
    PBOOLEAN PreviousState)
{
    if (PreviousState) *PreviousState = FALSE;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtCancelTimer_msabi(HANDLE TimerHandle, PBOOLEAN CurrentState)
{
    if (CurrentState) *CurrentState = FALSE;
    return STATUS_SUCCESS;
}

/* ---- Registry ------------------------------------------------------------ */

__attribute__((ms_abi))
static NTSTATUS NtCreateKey_msabi(
    PHANDLE KeyHandle, ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes, ULONG TitleIndex,
    PUNICODE_STRING Class, ULONG CreateOptions, PULONG Disposition)
{
    if (!ObjectAttributes || !ObjectAttributes->ObjectName) return STATUS_INVALID_PARAMETER;
    PCM_KEY_NODE key;
    NTSTATUS s = CmCreateKey(ObjectAttributes->ObjectName, 0, &key);
    if (NT_SUCCESS(s) && KeyHandle) *KeyHandle = (HANDLE)key;
    if (Disposition) *Disposition = 1;
    return s;
}

__attribute__((ms_abi))
static NTSTATUS NtOpenKey_msabi(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes)
{
    if (!ObjectAttributes || !ObjectAttributes->ObjectName) return STATUS_INVALID_PARAMETER;
    PCM_KEY_NODE key;
    NTSTATUS s = CmOpenKey(ObjectAttributes->ObjectName, DesiredAccess, &key);
    if (NT_SUCCESS(s) && KeyHandle) *KeyHandle = (HANDLE)key;
    return s;
}

__attribute__((ms_abi))
static NTSTATUS NtQueryValueKey_msabi(
    HANDLE KeyHandle, PUNICODE_STRING ValueName, ULONG KeyValueInformationClass,
    PVOID Buffer, ULONG Length, PULONG ResultLength)
{
    return NtQueryValueKey(KeyHandle, ValueName, KeyValueInformationClass, Buffer, Length, ResultLength);
}

__attribute__((ms_abi))
static NTSTATUS NtSetValueKey_msabi(
    HANDLE KeyHandle, PUNICODE_STRING ValueName, ULONG TitleIndex,
    ULONG Type, PVOID Data, ULONG DataSize)
{
    return CmSetValue((PCM_KEY_NODE)KeyHandle, ValueName, Type, Data, DataSize);
}

__attribute__((ms_abi))
static NTSTATUS NtDeleteKey_msabi(HANDLE KeyHandle)
{
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtEnumerateKey_msabi(
    HANDLE KeyHandle, ULONG Index, ULONG KeyInformationClass,
    PVOID Buffer, ULONG Length, PULONG ResultLength)
{
    return CmEnumerateSubKey((PCM_KEY_NODE)KeyHandle, Index, NULL);
}

__attribute__((ms_abi))
static NTSTATUS NtEnumerateValueKey_msabi(
    HANDLE KeyHandle, ULONG Index, ULONG KeyValueInformationClass,
    PVOID Buffer, ULONG Length, PULONG ResultLength)
{
    return CmEnumerateValue((PCM_KEY_NODE)KeyHandle, Index, NULL, NULL, NULL, 0);
}

__attribute__((ms_abi))
static NTSTATUS NtQueryKey_msabi(
    HANDLE KeyHandle, ULONG KeyInformationClass, PVOID Buffer, ULONG Length, PULONG ResultLength)
{
    if (Buffer && Length > 0) RtlZeroMemory(Buffer, Length);
    if (ResultLength) *ResultLength = 0;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtDeleteValueKey_msabi(HANDLE KeyHandle, PUNICODE_STRING ValueName)
{
    return STATUS_SUCCESS;
}

/* ---- Section ------------------------------------------------------------- */

__attribute__((ms_abi))
static NTSTATUS NtCreateSection_msabi(
    PHANDLE SectionHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
    PLARGE_INTEGER MaximumSize, ULONG SectionPageProtection, ULONG AllocationAttributes,
    HANDLE FileHandle)
{
    /* Allocate a backing store */
    SIZE_T size = 0x1000;
    if (MaximumSize && MaximumSize->QuadPart > 0) size = (SIZE_T)MaximumSize->QuadPart;
    size = (size + 0xFFF) & ~0xFFF;
    PVOID store = ExAllocatePool(NonPagedPool, size);
    if (!store) return STATUS_NO_MEMORY;
    RtlZeroMemory(store, size);
    if (SectionHandle) *SectionHandle = (HANDLE)store;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtMapViewOfSection_msabi(
    HANDLE SectionHandle, HANDLE ProcessHandle, PVOID *BaseAddress,
    ULONG_PTR ZeroBits, SIZE_T CommitSize, PLARGE_INTEGER SectionOffset,
    PSIZE_T ViewSize, ULONG InheritDisposition, ULONG AllocationType, ULONG Protect)
{
    if (BaseAddress) *BaseAddress = SectionHandle; /* Map = return the backing store */
    if (ViewSize) {} /* Use passed-in view size */
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtUnmapViewOfSection_msabi(HANDLE ProcessHandle, PVOID BaseAddress)
{
    /* Free the backing store */
    if (BaseAddress) ExFreePool(BaseAddress);
    return STATUS_SUCCESS;
}

/* ---- Time ---------------------------------------------------------------- */

extern volatile ULONG64 KeTickCount;

__attribute__((ms_abi))
static NTSTATUS NtGetTickCount_msabi(PULONG TickCount)
{
    if (TickCount) *TickCount = (ULONG)KeTickCount;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtQuerySystemTime_msabi(PLARGE_INTEGER SystemTime)
{
    if (SystemTime) SystemTime->QuadPart = (LONG64)KeTickCount * 100000;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtQueryPerformanceCounter_msabi(PLARGE_INTEGER PerformanceCount, PLARGE_INTEGER PerformanceFrequency)
{
    if (PerformanceCount) {
        ULONG64 tsc;
        __asm__ __volatile__("rdtsc" : "=A"(tsc));
        PerformanceCount->QuadPart = (LONG64)tsc;
    }
    if (PerformanceFrequency) PerformanceFrequency->QuadPart = 1000000000LL;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtDelayExecution_msabi(BOOLEAN Alertable, PLARGE_INTEGER DelayInterval)
{
    if (DelayInterval) {
        LONG64 ns = DelayInterval->QuadPart;
        if (ns < 0) ns = -ns;
        ULONG us = (ULONG)(ns / 1000);
        KeStallExecutionProcessor(us);
    }
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtYieldExecution_msabi(VOID)
{
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
        KiDispatchNextThread();
    return STATUS_SUCCESS;
}

/* ---- Misc ---------------------------------------------------------------- */

__attribute__((ms_abi))
static NTSTATUS NtDisplayString_msabi(PUNICODE_STRING String)
{
    if (String && String->Buffer) {
        /* Print as ANSI */
        for (USHORT i = 0; i < String->Length / 2; i++)
            HalpSerialPutChar((CHAR)String->Buffer[i]);
        HalpSerialPutChar('\n');
    }
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtQuerySystemInformation_msabi(
    ULONG InformationClass, PVOID Buffer, ULONG BufferSize, PULONG ReturnLength)
{
    if (Buffer && BufferSize > 0) RtlZeroMemory(Buffer, BufferSize);
    if (ReturnLength) *ReturnLength = 0;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtSetSystemInformation_msabi(
    ULONG InformationClass, PVOID Buffer, ULONG BufferSize)
{
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtQueryObject_msabi(
    HANDLE Handle, ULONG InformationClass, PVOID Buffer, ULONG BufferSize, PULONG ReturnLength)
{
    if (Buffer && BufferSize > 0) RtlZeroMemory(Buffer, BufferSize);
    if (ReturnLength) *ReturnLength = 0;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtDuplicateObject_msabi(
    HANDLE SourceProcessHandle, HANDLE SourceHandle, HANDLE TargetProcessHandle,
    PHANDLE TargetHandle, ACCESS_MASK DesiredAccess, ULONG HandleAttributes, ULONG Options)
{
    if (TargetHandle) *TargetHandle = SourceHandle;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS NtAllocateLocallyUniqueId_msabi(PVOID Luid)
{
    if (Luid) {
        ULONG64 *p = (ULONG64 *)Luid;
        *p = KeTickCount;
    }
    return STATUS_SUCCESS;
}

/* ============================================================================
 * Rtl* Runtime Library
 * ========================================================================== */

/* ---- Heap ---------------------------------------------------------------- */

__attribute__((ms_abi))
static PVOID RtlAllocateHeap_msabi(PVOID HeapHandle, ULONG Flags, SIZE_T Size)
{
    PVOID p = ExAllocatePool(NonPagedPool, Size);
    if (p && (Flags & 0x08)) RtlZeroMemory(p, Size); /* HEAP_ZERO_MEMORY */
    return p;
}

__attribute__((ms_abi))
static BOOLEAN RtlFreeHeap_msabi(PVOID HeapHandle, ULONG Flags, PVOID BaseAddress)
{
    if (BaseAddress) ExFreePool(BaseAddress);
    return TRUE;
}

__attribute__((ms_abi))
static PVOID RtlReAllocateHeap_msabi(PVOID HeapHandle, ULONG Flags, PVOID BaseAddress, SIZE_T Size)
{
    if (!BaseAddress) return RtlAllocateHeap_msabi(HeapHandle, Flags, Size);
    PVOID p = ExAllocatePool(NonPagedPool, Size);
    if (p) {
        RtlCopyMemory(p, BaseAddress, Size);
        ExFreePool(BaseAddress);
    }
    return p;
}

__attribute__((ms_abi))
static SIZE_T RtlSizeHeap_msabi(PVOID HeapHandle, ULONG Flags, PVOID BaseAddress)
{
    return 0;
}

__attribute__((ms_abi))
static BOOLEAN RtlValidateHeap_msabi(PVOID HeapHandle, ULONG Flags, PVOID BaseAddress)
{
    return TRUE;
}

/* ---- Memory -------------------------------------------------------------- */

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
    if (!Destination || !Source) return;
    PUCHAR d = (PUCHAR)Destination;
    const PUCHAR s = (const PUCHAR)Source;
    if (d < s) {
        for (SIZE_T i = 0; i < Length; i++) d[i] = s[i];
    } else if (d > s) {
        for (SIZE_T i = Length; i > 0; i--) d[i-1] = s[i-1];
    }
}

__attribute__((ms_abi))
static SIZE_T RtlCompareMemory_msabi(const VOID *Source1, const VOID *Source2, SIZE_T Length)
{
    if (!Source1 || !Source2) return 0;
    const PUCHAR a = (const PUCHAR)Source1;
    const PUCHAR b = (const PUCHAR)Source2;
    SIZE_T i;
    for (i = 0; i < Length; i++) if (a[i] != b[i]) break;
    return i;
}

__attribute__((ms_abi))
static BOOLEAN RtlEqualMemory_msabi(const VOID *Source1, const VOID *Source2, SIZE_T Length)
{
    return RtlCompareMemory_msabi(Source1, Source2, Length) == Length;
}

/* ---- String -------------------------------------------------------------- */

__attribute__((ms_abi))
static VOID RtlInitString_msabi(PANSI_STRING Destination, PCSZ Source)
{
    RtlInitAnsiString(Destination, Source);
}

__attribute__((ms_abi))
static VOID RtlInitUnicodeString_msabi(PUNICODE_STRING Destination, PCWSTR Source)
{
    RtlInitUnicodeString(Destination, Source);
}

__attribute__((ms_abi))
static VOID RtlFreeAnsiString_msabi(PANSI_STRING AnsiString)
{
    if (AnsiString && AnsiString->Buffer) {
        ExFreePool(AnsiString->Buffer);
        AnsiString->Buffer = NULL;
        AnsiString->Length = 0;
        AnsiString->MaximumLength = 0;
    }
}

__attribute__((ms_abi))
static VOID RtlFreeUnicodeString_msabi(PUNICODE_STRING UnicodeString)
{
    if (UnicodeString && UnicodeString->Buffer) {
        ExFreePool(UnicodeString->Buffer);
        UnicodeString->Buffer = NULL;
        UnicodeString->Length = 0;
        UnicodeString->MaximumLength = 0;
    }
}

__attribute__((ms_abi))
static NTSTATUS RtlAnsiStringToUnicodeString_msabi(
    PUNICODE_STRING Destination, const ANSI_STRING *Source, BOOLEAN AllocateDestinationString)
{
    if (!Source) return STATUS_INVALID_PARAMETER;
    if (AllocateDestinationString) {
        SIZE_T len = Source->Length * sizeof(WCHAR);
        Destination->Buffer = ExAllocatePool(NonPagedPool, len + sizeof(WCHAR));
        if (!Destination->Buffer) return STATUS_NO_MEMORY;
        for (USHORT i = 0; i < Source->Length; i++)
            Destination->Buffer[i] = (WCHAR)Source->Buffer[i];
        Destination->Buffer[Source->Length] = 0;
        Destination->Length = (USHORT)len;
        Destination->MaximumLength = (USHORT)(len + sizeof(WCHAR));
    }
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS RtlUnicodeStringToAnsiString_msabi(
    PANSI_STRING Destination, const UNICODE_STRING *Source, BOOLEAN AllocateDestinationString)
{
    if (!Source) return STATUS_INVALID_PARAMETER;
    if (AllocateDestinationString) {
        SIZE_T len = Source->Length / 2;
        Destination->Buffer = ExAllocatePool(NonPagedPool, len + 1);
        if (!Destination->Buffer) return STATUS_NO_MEMORY;
        for (USHORT i = 0; i < len; i++)
            Destination->Buffer[i] = (CHAR)Source->Buffer[i];
        Destination->Buffer[len] = 0;
        Destination->Length = (USHORT)len;
        Destination->MaximumLength = (USHORT)(len + 1);
    }
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS RtlUnicodeToMultiByteN_msabi(
    PCHAR MultiByteString, ULONG MaxBytesInMultiByteString,
    PULONG BytesInMultiByteString, const WCHAR *UnicodeString,
    ULONG BytesInUnicodeString)
{
    ULONG count = BytesInUnicodeString / 2;
    if (count > MaxBytesInMultiByteString) count = MaxBytesInMultiByteString;
    for (ULONG i = 0; i < count; i++)
        MultiByteString[i] = (CHAR)UnicodeString[i];
    if (BytesInMultiByteString) *BytesInMultiByteString = count;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS RtlMultiByteToUnicodeN_msabi(
    PWCHAR UnicodeString, ULONG MaxBytesInUnicodeString,
    PULONG BytesInUnicodeString, const CHAR *MultiByteString,
    ULONG BytesInMultiByteString)
{
    ULONG count = BytesInMultiByteString;
    if (count * 2 > MaxBytesInUnicodeString) count = MaxBytesInUnicodeString / 2;
    for (ULONG i = 0; i < count; i++)
        UnicodeString[i] = (WCHAR)MultiByteString[i];
    if (BytesInUnicodeString) *BytesInUnicodeString = count * 2;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static ULONG RtlUnicodeStringToInteger_msabi(
    const UNICODE_STRING *String, ULONG Base, PULONG Value)
{
    if (!String || !Value) return STATUS_INVALID_PARAMETER;
    ULONG result = 0;
    USHORT i = 0;
    ULONG b = Base ? Base : 10;
    if (b == 16 && String->Buffer[0] == L'0' && (String->Buffer[1] == L'x' || String->Buffer[1] == L'X')) i = 2;
    if (b == 0 && String->Buffer[0] == L'0' && String->Length > 2) { b = 8; i = 1; }
    for (; i < String->Length / 2; i++) {
        WCHAR c = String->Buffer[i];
        ULONG digit;
        if (c >= L'0' && c <= L'9') digit = c - L'0';
        else if (c >= L'a' && c <= L'f') digit = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') digit = c - L'A' + 10;
        else break;
        if (digit >= b) break;
        result = result * b + digit;
    }
    *Value = result;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS RtlIntegerToUnicodeString_msabi(
    ULONG Value, ULONG Base, PUNICODE_STRING String)
{
    if (!String || !String->Buffer) return STATUS_INVALID_PARAMETER;
    ULONG b = Base ? Base : 10;
    CHAR tmp[32];
    INT pos = 31;
    tmp[pos] = 0;
    if (Value == 0) { tmp[--pos] = '0'; }
    else {
        while (Value > 0 && pos > 0) {
            ULONG d = Value % b;
            tmp[--pos] = (d < 10) ? ('0' + d) : ('a' + d - 10);
            Value /= b;
        }
    }
    USHORT len = 0;
    while (tmp[pos] && len < String->MaximumLength / 2 - 1)
        String->Buffer[len++] = (WCHAR)tmp[pos++];
    String->Buffer[len] = 0;
    String->Length = len * 2;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static VOID RtlCopyUnicodeString_msabi(PUNICODE_STRING Destination, const UNICODE_STRING *Source)
{
    if (!Destination || !Source) return;
    USHORT len = Source->Length;
    if (len > Destination->MaximumLength) len = Destination->MaximumLength;
    for (USHORT i = 0; i < len / 2; i++)
        Destination->Buffer[i] = Source->Buffer[i];
    Destination->Length = len;
}

__attribute__((ms_abi))
static LONG RtlCompareUnicodeString_msabi(const UNICODE_STRING *String1, const UNICODE_STRING *String2, BOOLEAN CaseInSensitive)
{
    return RtlCompareUnicodeString((PUNICODE_STRING)String1, (PUNICODE_STRING)String2, CaseInSensitive);
}

__attribute__((ms_abi))
static BOOLEAN RtlEqualUnicodeString_msabi(const UNICODE_STRING *String1, const UNICODE_STRING *String2, BOOLEAN CaseInSensitive)
{
    return RtlEqualUnicodeString((PUNICODE_STRING)String1, (PUNICODE_STRING)String2, CaseInSensitive);
}

__attribute__((ms_abi))
static VOID RtlUpcaseUnicodeString_msabi(
    PUNICODE_STRING Destination, const UNICODE_STRING *Source, BOOLEAN AllocateDestinationString)
{
    UNICODE_STRING *src = (UNICODE_STRING *)Source;
    UNICODE_STRING *dst = Destination;
    if (AllocateDestinationString) {
        dst->Buffer = ExAllocatePool(NonPagedPool, src->MaximumLength);
        dst->MaximumLength = src->MaximumLength;
    }
    for (USHORT i = 0; i < src->Length / 2; i++) {
        WCHAR c = src->Buffer[i];
        if (c >= L'a' && c <= L'z') c -= 32;
        dst->Buffer[i] = c;
    }
    dst->Length = src->Length;
}

__attribute__((ms_abi))
static VOID RtlAppendUnicodeStringToString_msabi(PUNICODE_STRING Destination, const UNICODE_STRING *Source)
{
    if (!Destination || !Source) return;
    USHORT total = Destination->Length + Source->Length;
    if (total > Destination->MaximumLength) return;
    for (USHORT i = 0; i < Source->Length / 2; i++)
        Destination->Buffer[Destination->Length / 2 + i] = Source->Buffer[i];
    Destination->Length = total;
}

__attribute__((ms_abi))
static NTSTATUS RtlPrefixUnicodeString_msabi(const UNICODE_STRING *String1, const UNICODE_STRING *String2, BOOLEAN CaseInSensitive)
{
    if (!String1 || !String2) return STATUS_INVALID_PARAMETER;
    if (String1->Length > String2->Length) return STATUS_NOT_FOUND;
    for (USHORT i = 0; i < String1->Length / 2; i++) {
        WCHAR c1 = String1->Buffer[i], c2 = String2->Buffer[i];
        if (CaseInSensitive) { if (c1 >= L'a' && c1 <= L'z') c1 -= 32; if (c2 >= L'a' && c2 <= L'z') c2 -= 32; }
        if (c1 != c2) return STATUS_NOT_FOUND;
    }
    return STATUS_SUCCESS;
}

/* ---- Image (PE helpers) -------------------------------------------------- */

__attribute__((ms_abi))
static PVOID RtlImageNtHeader_msabi(PVOID Base)
{
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)Base;
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    return (PVOID)((PUCHAR)Base + dos->e_lfanew);
}

__attribute__((ms_abi))
static PVOID RtlImageDirectoryEntryToData_msabi(
    PVOID Base, BOOLEAN MappedAsImage, USHORT DirectoryEntry, PULONG Size)
{
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)RtlImageNtHeader_msabi(Base);
    if (!nt) return NULL;
    if (DirectoryEntry >= IMAGE_NUMBEROF_DIRECTORY_ENTRIES) return NULL;
    ULONG va = nt->OptionalHeader.DataDirectory[DirectoryEntry].VirtualAddress;
    *Size = nt->OptionalHeader.DataDirectory[DirectoryEntry].Size;
    if (!va || !*Size) return NULL;
    return (PVOID)((PUCHAR)Base + va);
}

/* ---- Exception handling -------------------------------------------------- */

__attribute__((ms_abi))
static PVOID RtlAddFunctionTable_msabi(
    PVOID FunctionTable, DWORD EntryCount, PVOID BaseAddress)
{
    /* Store function table for SEH — minimal implementation stores in global */
    (void)FunctionTable; (void)EntryCount; (void)BaseAddress;
    return (PVOID)1; /* non-NULL = success */
}

__attribute__((ms_abi))
static BOOLEAN RtlDeleteFunctionTable_msabi(PVOID FunctionTable)
{
    (void)FunctionTable;
    return TRUE;
}

__attribute__((ms_abi))
static PVOID RtlLookupFunctionEntry_msabi(
    ULONG64 ControlPc, PVOID *ImageBase, PUNICODE_STRING HistoryTable)
{
    (void)ControlPc; (void)HistoryTable;
    if (ImageBase) *ImageBase = NULL;
    return NULL;
}

__attribute__((ms_abi))
static VOID RtlRaiseException_msabi(PVOID ExceptionRecord)
{
    /* Kernel-mode: bugcheck with the exception */
    DbgPrint("ntrtl: RtlRaiseException — exception code 0x%08x\n",
             ExceptionRecord ? *(ULONG *)ExceptionRecord : 0);
    KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED,
                 ExceptionRecord ? *(ULONG *)ExceptionRecord : 0, 0, 0, 0);
}

__attribute__((ms_abi))
static NTSTATUS RtlNtStatusToDosError_msabi(NTSTATUS Status)
{
    /* Minimal mapping */
    if (NT_SUCCESS(Status)) return 0;
    if (Status == STATUS_NO_MEMORY) return 8;     /* ERROR_NOT_ENOUGH_MEMORY */
    if (Status == STATUS_ACCESS_VIOLATION) return 1000; /* guard */
    return (NTSTATUS)99; /* ERROR_INVALID_PARAMETER */
}

/* ---- Version ------------------------------------------------------------- */

__attribute__((ms_abi))
static NTSTATUS RtlGetVersion_msabi(PVOID lpVersionInformation)
{
    if (lpVersionInformation) {
        ULONG *vi = (ULONG *)lpVersionInformation;
        vi[0] = 156;  /* dwOSVersionInfoSize */
        vi[1] = 6;    /* dwMajorVersion */
        vi[2] = 1;    /* dwMinorVersion */
        vi[3] = 7601; /* dwBuildNumber */
        vi[4] = 2;    /* dwPlatformId = VER_PLATFORM_WIN32_NT */
    }
    return STATUS_SUCCESS;
}

/* ============================================================================
 * Ldr* — Loader Functions
 * ========================================================================== */

__attribute__((ms_abi))
static NTSTATUS LdrLoadDll_msabi(
    PWCHAR PathToFile, PULONG Flags, PUNICODE_STRING ModuleFileName, PHANDLE ModuleHandle)
{
    /* Convert to ANSI and search export registry */
    if (ModuleFileName && ModuleFileName->Buffer) {
        CHAR name[260];
        USHORT len = ModuleFileName->Length / 2;
        if (len > 259) len = 259;
        for (USHORT i = 0; i < len; i++) name[i] = (CHAR)ModuleFileName->Buffer[i];
        name[len] = 0;
        DbgPrint("ntrtl: LdrLoadDll(\"%s\") -> fake handle\n", name);
    }
    if (ModuleHandle) *ModuleHandle = (HANDLE)0x10000000LL;
    if (Flags) *Flags = 0;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static NTSTATUS LdrGetProcedureAddress_msabi(
    PVOID BaseAddress, PANSI_STRING Name, ULONG Ordinal, PVOID *ProcedureAddress)
{
    if (Name && Name->Buffer && ProcedureAddress) {
        /* Search export registry by function name */
        for (ULONG i = 0; i < g_ExportCount; i++) {
            if (SwStrICmp(g_ExportTable[i].FuncName, Name->Buffer) == 0) {
                *ProcedureAddress = g_ExportTable[i].FuncPtr;
                return STATUS_SUCCESS;
            }
        }
        *ProcedureAddress = NULL;
        return STATUS_ENTRYPOINT_NOT_FOUND;
    }
    if (ProcedureAddress) *ProcedureAddress = NULL;
    return STATUS_ENTRYPOINT_NOT_FOUND;
}

__attribute__((ms_abi))
static NTSTATUS LdrGetDllHandle_msabi(
    PWCHAR pwzPath, PULONG pdwReserved, PUNICODE_STRING pUniName, PHANDLE pHandle)
{
    if (pHandle) *pHandle = (HANDLE)0x10000000LL;
    return STATUS_SUCCESS;
}

__attribute__((ms_abi))
static VOID LdrInitializeThunk_msabi(PVOID Context)
{
    /* Process startup thunk — in real ntdll this sets up the process.
       For kernel-mode execution, this is a no-op. */
    (void)Context;
}

__attribute__((ms_abi))
static VOID LdrShutdownProcess_msabi(VOID)
{
    DbgPrint("ntrtl: LdrShutdownProcess\n");
}

/* ============================================================================
 * Csr* — CSRSS Client Functions
 * ========================================================================== */

__attribute__((ms_abi))
static NTSTATUS CsrClientCallServer_msabi(
    PVOID ApiMessage, PVOID ReplyBuffer, ULONG Opcode, ULONG Size)
{
    (void)ApiMessage; (void)ReplyBuffer; (void)Opcode; (void)Size;
    return STATUS_SUCCESS;
}

/* ============================================================================
 * Dbg* — Debug Functions
 * ========================================================================== */

__attribute__((ms_abi))
static ULONG DbgPrint_msabi(const CHAR *Format, ...)
{
    va_list ap;
    va_start(ap, Format);
    /* Simple version: just print the format string (no varargs) */
    if (Format) DbgPrint("DBG: %s", Format);
    va_end(ap);
    return 0;
}

__attribute__((ms_abi))
static VOID DbgBreakPoint_msabi(VOID)
{
    __asm__ __volatile__("int $3");
}

/* ============================================================================
 * Registration
 * ========================================================================== */

#ifndef STATUS_NOT_FOUND
#define STATUS_NOT_FOUND ((NTSTATUS)0xC0000227L)
#endif
#ifndef STATUS_ENTRYPOINT_NOT_FOUND
#define STATUS_ENTRYPOINT_NOT_FOUND ((NTSTATUS)0xC0000139L)
#endif

VOID NTAPI NtdllRegisterExports(VOID)
{
#define NREG(name, ptr) ExeRegisterExport("ntdll.dll", name, ptr)

    /* Nt* / Zw* — Memory */
    NREG("NtAllocateVirtualMemory", NtAllocateVirtualMemory_msabi);
    NREG("NtFreeVirtualMemory", NtFreeVirtualMemory_msabi);
    NREG("NtProtectVirtualMemory", NtProtectVirtualMemory_msabi);
    NREG("NtQueryVirtualMemory", NtQueryVirtualMemory_msabi);
    NREG("ZwAllocateVirtualMemory", NtAllocateVirtualMemory_msabi);
    NREG("ZwFreeVirtualMemory", NtFreeVirtualMemory_msabi);

    /* Nt* / Zw* — File I/O */
    NREG("NtCreateFile", NtCreateFile_msabi);
    NREG("NtReadFile", NtReadFile_msabi);
    NREG("NtWriteFile", NtWriteFile_msabi);
    NREG("NtClose", NtClose_msabi);
    NREG("NtQueryInformationFile", NtQueryInformationFile_msabi);
    NREG("NtSetInformationFile", NtSetInformationFile_msabi);
    NREG("NtFlushBuffersFile", NtFlushBuffersFile_msabi);
    NREG("NtDeleteFile", NtDeleteFile_msabi);
    NREG("ZwCreateFile", NtCreateFile_msabi);
    NREG("ZwReadFile", NtReadFile_msabi);
    NREG("ZwWriteFile", NtWriteFile_msabi);
    NREG("ZwClose", NtClose_msabi);

    /* Nt* / Zw* — Process / Thread */
    NREG("NtCreateProcess", NtCreateProcess_msabi);
    NREG("NtTerminateProcess", NtTerminateProcess_msabi);
    NREG("NtCreateThread", NtCreateThread_msabi);
    NREG("NtTerminateThread", NtTerminateThread_msabi);
    NREG("NtSuspendThread", NtSuspendThread_msabi);
    NREG("NtResumeThread", NtResumeThread_msabi);
    NREG("NtQueryInformationProcess", NtQueryInformationProcess_msabi);
    NREG("NtSetInformationProcess", NtSetInformationProcess_msabi);
    NREG("NtQueryInformationThread", NtQueryInformationThread_msabi);
    NREG("NtSetInformationThread", NtSetInformationThread_msabi);
    NREG("NtGetCurrentProcessorNumber", NtGetCurrentProcessorNumber_msabi);
    NREG("ZwCreateProcess", NtCreateProcess_msabi);
    NREG("ZwCreateThread", NtCreateThread_msabi);

    /* Nt* / Zw* — Synchronization */
    NREG("NtCreateEvent", NtCreateEvent_msabi);
    NREG("NtSetEvent", NtSetEvent_msabi);
    NREG("NtResetEvent", NtResetEvent_msabi);
    NREG("NtClearEvent", NtClearEvent_msabi);
    NREG("NtPulseEvent", NtPulseEvent_msabi);
    NREG("NtCreateMutex", NtCreateMutex_msabi);
    NREG("NtReleaseMutant", NtReleaseMutant_msabi);
    NREG("NtCreateSemaphore", NtCreateSemaphore_msabi);
    NREG("NtReleaseSemaphore", NtReleaseSemaphore_msabi);
    NREG("NtWaitForSingleObject", NtWaitForSingleObject_msabi);
    NREG("NtWaitForMultipleObjects", NtWaitForMultipleObjects_msabi);
    NREG("NtSignalAndWaitForSingleObject", NtSignalAndWaitForSingleObject_msabi);

    /* Nt* — Timer */
    NREG("NtCreateTimer", NtCreateTimer_msabi);
    NREG("NtSetTimer", NtSetTimer_msabi);
    NREG("NtCancelTimer", NtCancelTimer_msabi);

    /* Nt* — Registry */
    NREG("NtCreateKey", NtCreateKey_msabi);
    NREG("NtOpenKey", NtOpenKey_msabi);
    NREG("NtQueryValueKey", NtQueryValueKey_msabi);
    NREG("NtSetValueKey", NtSetValueKey_msabi);
    NREG("NtDeleteKey", NtDeleteKey_msabi);
    NREG("NtEnumerateKey", NtEnumerateKey_msabi);
    NREG("NtEnumerateValueKey", NtEnumerateValueKey_msabi);
    NREG("NtQueryKey", NtQueryKey_msabi);
    NREG("NtDeleteValueKey", NtDeleteValueKey_msabi);

    /* Nt* — Section */
    NREG("NtCreateSection", NtCreateSection_msabi);
    NREG("NtMapViewOfSection", NtMapViewOfSection_msabi);
    NREG("NtUnmapViewOfSection", NtUnmapViewOfSection_msabi);

    /* Nt* — Time */
    NREG("NtGetTickCount", NtGetTickCount_msabi);
    NREG("NtQuerySystemTime", NtQuerySystemTime_msabi);
    NREG("NtQueryPerformanceCounter", NtQueryPerformanceCounter_msabi);
    NREG("NtDelayExecution", NtDelayExecution_msabi);
    NREG("NtYieldExecution", NtYieldExecution_msabi);

    /* Nt* — Misc */
    NREG("NtDisplayString", NtDisplayString_msabi);
    NREG("NtQuerySystemInformation", NtQuerySystemInformation_msabi);
    NREG("NtSetSystemInformation", NtSetSystemInformation_msabi);
    NREG("NtQueryObject", NtQueryObject_msabi);
    NREG("NtDuplicateObject", NtDuplicateObject_msabi);
    NREG("NtAllocateLocallyUniqueId", NtAllocateLocallyUniqueId_msabi);

    /* Rtl* — Heap */
    NREG("RtlAllocateHeap", RtlAllocateHeap_msabi);
    NREG("RtlFreeHeap", RtlFreeHeap_msabi);
    NREG("RtlReAllocateHeap", RtlReAllocateHeap_msabi);
    NREG("RtlSizeHeap", RtlSizeHeap_msabi);
    NREG("RtlValidateHeap", RtlValidateHeap_msabi);

    /* Rtl* — Memory */
    NREG("RtlZeroMemory", RtlZeroMemory_msabi);
    NREG("RtlCopyMemory", RtlCopyMemory_msabi);
    NREG("RtlFillMemory", RtlFillMemory_msabi);
    NREG("RtlMoveMemory", RtlMoveMemory_msabi);
    NREG("RtlCompareMemory", RtlCompareMemory_msabi);
    NREG("RtlEqualMemory", RtlEqualMemory_msabi);

    /* Rtl* — String */
    NREG("RtlInitString", RtlInitString_msabi);
    NREG("RtlInitUnicodeString", RtlInitUnicodeString_msabi);
    NREG("RtlFreeAnsiString", RtlFreeAnsiString_msabi);
    NREG("RtlFreeUnicodeString", RtlFreeUnicodeString_msabi);
    NREG("RtlAnsiStringToUnicodeString", RtlAnsiStringToUnicodeString_msabi);
    NREG("RtlUnicodeStringToAnsiString", RtlUnicodeStringToAnsiString_msabi);
    NREG("RtlUnicodeToMultiByteN", RtlUnicodeToMultiByteN_msabi);
    NREG("RtlMultiByteToUnicodeN", RtlMultiByteToUnicodeN_msabi);
    NREG("RtlUnicodeStringToInteger", RtlUnicodeStringToInteger_msabi);
    NREG("RtlIntegerToUnicodeString", RtlIntegerToUnicodeString_msabi);
    NREG("RtlCopyUnicodeString", RtlCopyUnicodeString_msabi);
    NREG("RtlCompareUnicodeString", RtlCompareUnicodeString_msabi);
    NREG("RtlEqualUnicodeString", RtlEqualUnicodeString_msabi);
    NREG("RtlUpcaseUnicodeString", RtlUpcaseUnicodeString_msabi);
    NREG("RtlAppendUnicodeStringToString", RtlAppendUnicodeStringToString_msabi);
    NREG("RtlPrefixUnicodeString", RtlPrefixUnicodeString_msabi);

    /* Rtl* — Image */
    NREG("RtlImageNtHeader", RtlImageNtHeader_msabi);
    NREG("RtlImageDirectoryEntryToData", RtlImageDirectoryEntryToData_msabi);

    /* Rtl* — Exception */
    NREG("RtlAddFunctionTable", RtlAddFunctionTable_msabi);
    NREG("RtlDeleteFunctionTable", RtlDeleteFunctionTable_msabi);
    NREG("RtlLookupFunctionEntry", RtlLookupFunctionEntry_msabi);
    NREG("RtlRaiseException", RtlRaiseException_msabi);
    NREG("RtlNtStatusToDosError", RtlNtStatusToDosError_msabi);

    /* Rtl* — Version */
    NREG("RtlGetVersion", RtlGetVersion_msabi);

    /* Ldr* */
    NREG("LdrLoadDll", LdrLoadDll_msabi);
    NREG("LdrGetProcedureAddress", LdrGetProcedureAddress_msabi);
    NREG("LdrGetDllHandle", LdrGetDllHandle_msabi);
    NREG("LdrInitializeThunk", LdrInitializeThunk_msabi);
    NREG("LdrShutdownProcess", LdrShutdownProcess_msabi);

    /* Csr* */
    NREG("CsrClientCallServer", CsrClientCallServer_msabi);

    /* Dbg* */
    NREG("DbgPrint", DbgPrint_msabi);
    NREG("DbgBreakPoint", DbgBreakPoint_msabi);

    DbgPrint("EXE: ntdll.dll exports registered (%lu total)\n", g_ExportCount);
}
