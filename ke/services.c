/*
 * MinNT - ke/services.c
 * System service table.  Dispatches syscalls from user mode to kernel
 * handlers.  Called from KiSystemCall64 (syscall.S).
 *
 * x86-64 Windows syscall convention:
 *   RCX = arg1, RDX = arg2, R8 = arg3, R9 = arg4, R10 = arg5, R11 = arg6
 * These are saved in the KTRAP_FRAME by KiSystemCall64.
 */

#include <nt/ke.h>
#include <nt/ps.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/cm.h>
#include <nt/se.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/pe.h>
#include <nt/fs.h>
#include <nt/lpc.h>
#include <nt/io.h>
#include <nt/ob.h>
#include <ndk/obfuncs.h>
#include <ndk/psfuncs.h>
#include "../win32k/w32ksvc.h"

#define SYSCALL_MAX 64

/* Per-process address space lookup table (indexed by PID) */
static PMM_ADDRESS_SPACE PspAddressSpaces[256];

/* Kernel CR3 for switching back to kernel address space */
static ULONG64 KiKernelCr3;

/* ---- Syscall handlers — args come from KTRAP_FRAME ---------------------- */

/* 0: NtDbgPrint — print a string from user space */
static NTSTATUS SvcDbgPrint(PKTRAP_FRAME tf)
{
    const CHAR *String = (const CHAR *)tf->R10;
    if (!MmIsAddressValid(String)) return STATUS_ACCESS_VIOLATION;
    DbgPrint("USER: %s\n", String ? String : "(null)");
    return STATUS_SUCCESS;
}

/* 1: NtGetTickCount — return the current tick count */
static NTSTATUS SvcGetTickCount(PKTRAP_FRAME tf)
{
    volatile ULONG64 *OutTick = (volatile ULONG64 *)tf->R10;
    if (OutTick && !MmIsAddressValid(OutTick)) return STATUS_ACCESS_VIOLATION;
    if (OutTick)
        *OutTick = KeTickCount;
    return STATUS_SUCCESS;
}

/* 2: NtYieldExecution — yield the CPU */
static NTSTATUS SvcYieldExecution(PKTRAP_FRAME tf)
{
    UNREFERENCED_PARAMETER(tf);
    KiDispatchNextThread();
    return STATUS_SUCCESS;
}

/* 3: NtTerminateThread — exit the current thread */
static NTSTATUS SvcTerminateThread(PKTRAP_FRAME tf)
{
    UNREFERENCED_PARAMETER(tf);
    DbgPrint("USER: thread terminating\n");
    for (;;)
        KiDispatchNextThread();
    return STATUS_SUCCESS;
}

/* 4: NtBlameCurtis — easter egg */
static NTSTATUS SvcBlameCurtis(PKTRAP_FRAME tf)
{
    UNREFERENCED_PARAMETER(tf);
    DbgPrint("USER: CURTIS DID IT. AGAIN.\n");
    return (NTSTATUS)0xC0FFEE;
}

/* 5: NtAllocateVirtualMemory */
static NTSTATUS SvcAllocateVirtualMemory(PKTRAP_FRAME tf)
{
    PULONG_PTR base = (PULONG_PTR)tf->R10;
    if (!MmIsAddressValid(base)) return STATUS_ACCESS_VIOLATION;
    ULONG_PTR zeroBits = tf->Rdx;
    ULONG_PTR regionSize = tf->R8;
    ULONG allocType = (ULONG)tf->R9;
    ULONG protect = (ULONG)(tf->R10 & 0xFFFFFFFF);

    /* Use kernel address space for now — per-process comes with ntdll */
    return MmAllocateVirtualMemory(NULL, base, zeroBits, regionSize, allocType, protect);
}

/* 6: NtFreeVirtualMemory */
static NTSTATUS SvcFreeVirtualMemory(PKTRAP_FRAME tf)
{
    PULONG_PTR base = (PULONG_PTR)tf->R10;
    if (!MmIsAddressValid(base)) return STATUS_ACCESS_VIOLATION;
    return MmFreeVirtualMemory(NULL, base);
}

/* 7: NtCreateFile */
static NTSTATUS SvcCreateFile(PKTRAP_FRAME tf)
{
    return NtCreateFile((PHANDLE)tf->R10,
                        (ACCESS_MASK)tf->Rdx,
                        (PISECURITY_DESCRIPTOR)tf->R8,
                        (PVOID)tf->R9,
                        (PULONG64)tf->R10,
                        (ULONG)(tf->R11 & 0xFFFFFFFF),
                        0, 0, 0,
                        NULL, 0);
}

/* 8: NtReadFile */
static NTSTATUS SvcReadFile(PKTRAP_FRAME tf)
{
    return NtReadFile((HANDLE)tf->R10, (PVOID)tf->Rdx,
                       (PVOID)tf->R8, (PVOID)tf->R9,
                       (PVOID)tf->R10, (PVOID)tf->R11,
                       0, NULL, NULL);
}

/* 9: NtWriteFile */
static NTSTATUS SvcWriteFile(PKTRAP_FRAME tf)
{
    return NtWriteFile((HANDLE)tf->R10, (PVOID)tf->Rdx,
                        (PVOID)tf->R8, (PVOID)tf->R9,
                        (PVOID)tf->R10, (PVOID)tf->R11,
                        0, NULL, NULL);
}

/* 10: NtClose */
static NTSTATUS SvcClose(PKTRAP_FRAME tf)
{
    return NtClose((HANDLE)tf->R10);
}

/* ---- Section object type ------------------------------------------------ */

typedef struct _SECTION {
    SIZE_T          MaximumSize;
    ULONG           SectionAttributes;
    ULONG           ProtectionAttributes;
    PVOID           BackingStore;
} SECTION, *PSECTION;

static POBJECT_TYPE SectionObjectType = NULL;

static VOID NTAPI SecSectionDelete(PVOID Body)
{
    PSECTION section = Body;
    if (section && section->BackingStore) {
        ExFreePoolWithTag(section->BackingStore, TAG_SECT);
        section->BackingStore = NULL;
    }
}

/* 11: NtCreateSection */
static NTSTATUS SvcCreateSection(PKTRAP_FRAME tf)
{
    PHANDLE SectionHandle = (PHANDLE)tf->R10;
    ACCESS_MASK DesiredAccess = (ACCESS_MASK)tf->Rdx;
    POBJECT_ATTRIBUTES ObjectAttributes = (POBJECT_ATTRIBUTES)tf->R8;
    PLARGE_INTEGER MaximumSize = (PLARGE_INTEGER)tf->R9;
    ULONG SectionAttributes = (ULONG)tf->R10;
    ULONG ProtectionAttributes = (ULONG)tf->R11;

    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(ObjectAttributes);
    UNREFERENCED_PARAMETER(SectionAttributes);
    UNREFERENCED_PARAMETER(ProtectionAttributes);

    SIZE_T size = 0x1000;
    if (MaximumSize && MaximumSize->QuadPart > 0) {
        size = (SIZE_T)MaximumSize->QuadPart;
        if (size == 0) size = 0x1000;
    }
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (!SectionObjectType) {
        static const UNICODE_STRING SecName = RTL_CONSTANT_STRING(L"Section");
        ObCreateObjectType(&SecName, TAG_SECT, SecSectionDelete, &SectionObjectType);
    }

    PSECTION section;
    NTSTATUS status = ObCreateObject(SectionObjectType, sizeof(SECTION), NULL, (PVOID *)&section);
    if (!NT_SUCCESS(status)) return status;

    section->MaximumSize = size;
    section->SectionAttributes = SectionAttributes;
    section->ProtectionAttributes = ProtectionAttributes;

    section->BackingStore = ExAllocatePoolWithTag(NonPagedPool, size, TAG_SECT);
    if (!section->BackingStore) {
        ObDereferenceObject(section);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(section->BackingStore, size);

    status = ObInsertHandle(section, SectionHandle);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(section->BackingStore, TAG_SECT);
        ObDereferenceObject(section);
        return status;
    }

    DbgPrint("PS: created section handle %p size %llu\n",
              (PVOID)*SectionHandle, (unsigned long long)size);
    return STATUS_SUCCESS;
}

/* 12: NtMapViewOfSection */
static NTSTATUS SvcMapViewOfSection(PKTRAP_FRAME tf)
{
    PULONG_PTR baseAddr = (PULONG_PTR)tf->R8;
    UNREFERENCED_PARAMETER(tf);
    UNREFERENCED_PARAMETER(baseAddr);

    return MmAllocateVirtualMemory(NULL, baseAddr, 0, 0x10000, MM_VAD_COMMIT, 0);
}

/* 13: NtCreateProcess — create a user-mode process with its own address space */
static NTSTATUS SvcCreateProcess(PKTRAP_FRAME tf)
{
    PHANDLE ProcessHandle = (PHANDLE)tf->R10;
    ACCESS_MASK DesiredAccess = (ACCESS_MASK)tf->Rdx;
    POBJECT_ATTRIBUTES ObjectAttributes = (POBJECT_ATTRIBUTES)tf->R8;
    HANDLE ParentProcess = (HANDLE)tf->R9;
    ULONG Flags = (ULONG)tf->R10;
    HANDLE SectionHandle = (HANDLE)tf->R11;

    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(ObjectAttributes);
    UNREFERENCED_PARAMETER(ParentProcess);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(SectionHandle);

    PEPROCESS NewProcess;
    NTSTATUS status;
    PMM_ADDRESS_SPACE space;
    ULONG_PTR base = MM_USER_BASE;
    SIZE_T image_size = 0x100000; /* 1MB default user address space */
    ULONG pid;

    /* Create the EPROCESS object */
    status = PsCreateSystemProcess("User", &NewProcess);
    if (!NT_SUCCESS(status)) return status;

    /* Create a per-process address space */
    space = ExAllocatePoolWithTag(NonPagedPool, sizeof(MM_ADDRESS_SPACE), TAG_PROC);
    if (!space) {
        ObDereferenceObject(NewProcess);
        return STATUS_NO_MEMORY;
    }

    status = MmCreateAddressSpace(space);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(space, TAG_PROC);
        ObDereferenceObject(NewProcess);
        return status;
    }

    /* Set the process CR3 to the new address space PML4 */
    NewProcess->Pcb.DirectoryTableBase = space->Pml4;

    /* Allocate user VA range for the process */
    status = MmAllocateVirtualMemory(space, &base, 0, image_size, MM_VAD_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!NT_SUCCESS(status)) {
        MmDestroyAddressSpace(space);
        ExFreePoolWithTag(space, TAG_PROC);
        ObDereferenceObject(NewProcess);
        return status;
    }

    /* Store address space in per-process lookup table */
    pid = (ULONG)(ULONG_PTR)NewProcess->UniqueProcessId;
    if (pid >= 256) {
        MmDestroyAddressSpace(space);
        ExFreePoolWithTag(space, TAG_PROC);
        ObDereferenceObject(NewProcess);
        return STATUS_NO_MEMORY;
    }
    PspAddressSpaces[pid] = space;

    /* Create a handle for the process */
    status = ObInsertHandle(NewProcess, ProcessHandle);
    if (!NT_SUCCESS(status)) {
        PspAddressSpaces[pid] = NULL;
        MmDestroyAddressSpace(space);
        ExFreePoolWithTag(space, TAG_PROC);
        ObDereferenceObject(NewProcess);
        return status;
    }

    DbgPrint("PS: created user process PID %llu with address space PML4=%p\n",
              (ULONG64)(ULONG_PTR)NewProcess->UniqueProcessId,
              (PVOID)space->Pml4);
    return STATUS_SUCCESS;
}

/* 14: NtCreateThread — create a user-mode thread in a process */
static NTSTATUS SvcCreateThread(PKTRAP_FRAME tf)
{
    PHANDLE ThreadHandle = (PHANDLE)tf->R10;
    ACCESS_MASK DesiredAccess = (ACCESS_MASK)tf->Rdx;
    POBJECT_ATTRIBUTES ObjectAttributes = (POBJECT_ATTRIBUTES)tf->R8;
    HANDLE ProcessHandle = (HANDLE)tf->R9;
    PVOID UserEntryPoint = (PVOID)tf->R10;
    PVOID UserStackBase = (PVOID)tf->R11;

    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(ObjectAttributes);

    PEPROCESS Process;
    PETHREAD Thread;
    NTSTATUS status;
    ULONG64 stack_size = 0x100000; /* 1MB user stack */
    PMM_ADDRESS_SPACE space;
    ULONG pid;

    /* Look up the process from the handle */
    status = ObReferenceObjectByHandle(ProcessHandle, PsProcessType, (PVOID *)&Process);
    if (!NT_SUCCESS(status)) return status;

    /* Find the address space for this process */
    pid = (ULONG)(ULONG_PTR)Process->UniqueProcessId;
    if (pid < 256) {
        space = PspAddressSpaces[pid];
    } else {
        space = NULL;
    }

    /* Switch to the process's address space before creating the thread */
    if (space && space->Pml4) {
        MmSwitchAddressSpace(space);
    }

    /* Create the user thread */
    status = PsCreateUserThread(Process, UserEntryPoint, UserStackBase,
                                 stack_size, &Thread);
    if (!NT_SUCCESS(status)) {
        if (space && space->Pml4)
            MmSwitchAddressSpace(NULL);
        ObDereferenceObject(Process);
        return status;
    }

    /* Create a handle for the thread */
    status = ObInsertHandle(Thread, ThreadHandle);

    /* Switch back to kernel address space */
    if (space && space->Pml4)
        MmSwitchAddressSpace(NULL);

    ObDereferenceObject(Process);
    return status;
}

/* 15: NtTerminateProcess */
static NTSTATUS SvcTerminateProcess(PKTRAP_FRAME tf)
{
    HANDLE ProcessHandle = (HANDLE)tf->R10;
    NTSTATUS ExitStatus = (NTSTATUS)tf->Rdx;

    UNREFERENCED_PARAMETER(ExitStatus);

    PEPROCESS Process;
    NTSTATUS status;

    if (ProcessHandle == NtCurrentProcess()) {
        Process = PsGetCurrentProcess();
    } else {
        status = ObReferenceObjectByHandle(ProcessHandle, PsProcessType, (PVOID *)&Process);
        if (!NT_SUCCESS(status)) return status;
    }

    /* Mark all threads as terminated */
    PLIST_ENTRY entry = Process->ThreadListHead.Flink;
    while (entry != &Process->ThreadListHead) {
        PETHREAD Thread = CONTAINING_RECORD(entry, ETHREAD, ThreadListEntry);
        entry = entry->Flink;
        Thread->Tcb.State = Terminated;
    }

    /* Free the per-process address space */
    ULONG pid = (ULONG)(ULONG_PTR)Process->UniqueProcessId;
    if (pid < 256) {
        PMM_ADDRESS_SPACE space = PspAddressSpaces[pid];
        if (space) {
            PspAddressSpaces[pid] = NULL;
            /* Free all VADs */
            PMM_VAD vad = space->VadRoot;
            while (vad) {
                PMM_VAD next = vad->Next;
                ExFreePoolWithTag(vad, TAG_PROC);
                vad = next;
            }
            /* Free the PML4 page */
            if (space->Pml4) {
                MmFreePhysicalPage((PHYSICAL_ADDRESS)space->Pml4);
            }
            ExFreePoolWithTag(space, TAG_PROC);
        }
    }

    /* Remove from active process list */
    RemoveEntryList(&Process->ActiveProcessLinks);

    /* Dereference the process object (triggers delete procedure if refcount == 0) */
    if (ProcessHandle != NtCurrentProcess()) {
        ObDereferenceObject(Process);
    }

    DbgPrint("PS: terminated process '%s' PID %llu\n",
              Process->ImageFileName, (ULONG64)(ULONG_PTR)Process->UniqueProcessId);

    /* If terminating self, yield forever */
    if (ProcessHandle == NtCurrentProcess()) {
        for (;;)
            KiDispatchNextThread();
    }

    return STATUS_SUCCESS;
}

/* 16: NtOpenKey */
static NTSTATUS SvcOpenKey(PKTRAP_FRAME tf)
{
    PHANDLE KeyHandle = (PHANDLE)tf->R10;
    ACCESS_MASK DesiredAccess = (ACCESS_MASK)tf->Rdx;
    POBJECT_ATTRIBUTES ObjectAttributes = (POBJECT_ATTRIBUTES)tf->R8;

    if (!MmIsAddressValid(KeyHandle)) return STATUS_ACCESS_VIOLATION;
    if (!MmIsAddressValid(ObjectAttributes)) return STATUS_ACCESS_VIOLATION;

    return NtOpenKey(KeyHandle, DesiredAccess, ObjectAttributes);
}

/* 17: NtQueryValueKey */
static NTSTATUS SvcQueryValueKey(PKTRAP_FRAME tf)
{
    HANDLE KeyHandle = (HANDLE)tf->R10;
    PUNICODE_STRING ValueName = (PUNICODE_STRING)tf->Rdx;
    ULONG KeyValueInformationClass = (ULONG)tf->R8;
    PVOID KeyValueInformation = (PVOID)tf->R9;
    ULONG Length = (ULONG)tf->R10;
    PULONG ResultLength = (PULONG)tf->R11;

    if (!MmIsAddressValid(ValueName)) return STATUS_ACCESS_VIOLATION;
    if (!MmIsAddressValid(KeyValueInformation)) return STATUS_ACCESS_VIOLATION;

    return NtQueryValueKey(KeyHandle, ValueName, KeyValueInformationClass,
                           KeyValueInformation, Length, ResultLength);
}

/* 18: NtCreatePort */
static NTSTATUS SvcCreatePort(PKTRAP_FRAME tf)
{
    return NtCreatePort((PHANDLE)tf->R10,
                        (PUNICODE_STRING)tf->Rdx,
                        (ULONG)tf->R8, (ULONG)tf->R9,
                        NULL);
}

/* 19: NtConnectPort */
static NTSTATUS SvcConnectPort(PKTRAP_FRAME tf)
{
    return NtConnectPort((PHANDLE)tf->R10,
                          (PUNICODE_STRING)tf->Rdx,
                          NULL, NULL,
                          NULL, NULL,
                          NULL, NULL);
}

/* 20: NtRequestWaitReplyPort */
static NTSTATUS SvcRequestWaitReplyPort(PKTRAP_FRAME tf)
{
    return NtRequestWaitReplyPort((HANDLE)tf->R10,
                                   (PVOID)tf->Rdx, (PVOID)tf->R8);
}

/* ---- Service table ------------------------------------------------------ */

typedef NTSTATUS (*SYSCALL_HANDLER)(PKTRAP_FRAME);

static const SYSCALL_HANDLER KiServiceTable[SYSCALL_MAX] = {
    [0]  = (SYSCALL_HANDLER)SvcDbgPrint,
    [1]  = (SYSCALL_HANDLER)SvcGetTickCount,
    [2]  = (SYSCALL_HANDLER)SvcYieldExecution,
    [3]  = (SYSCALL_HANDLER)SvcTerminateThread,
    [4]  = (SYSCALL_HANDLER)SvcBlameCurtis,
    [5]  = (SYSCALL_HANDLER)SvcAllocateVirtualMemory,
    [6]  = (SYSCALL_HANDLER)SvcFreeVirtualMemory,
    [7]  = (SYSCALL_HANDLER)SvcCreateFile,
    [8]  = (SYSCALL_HANDLER)SvcReadFile,
    [9]  = (SYSCALL_HANDLER)SvcWriteFile,
    [10] = (SYSCALL_HANDLER)SvcClose,
    [11] = (SYSCALL_HANDLER)SvcCreateSection,
    [12] = (SYSCALL_HANDLER)SvcMapViewOfSection,
    [13] = (SYSCALL_HANDLER)SvcCreateProcess,
    [14] = (SYSCALL_HANDLER)SvcCreateThread,
    [15] = (SYSCALL_HANDLER)SvcTerminateProcess,
    [16] = (SYSCALL_HANDLER)SvcOpenKey,
    [17] = (SYSCALL_HANDLER)SvcQueryValueKey,
    [18] = (SYSCALL_HANDLER)SvcCreatePort,
    [19] = (SYSCALL_HANDLER)SvcConnectPort,
    [20] = (SYSCALL_HANDLER)SvcRequestWaitReplyPort,
};

/* ---- Dispatcher --------------------------------------------------------- */

extern NTSTATUS NTAPI Win32kSyscallDispatcher(ULONG SyscallNumber,
                                               ULONG_PTR Arg1, ULONG_PTR Arg2, ULONG_PTR Arg3,
                                               ULONG_PTR Arg4, ULONG_PTR Arg5, ULONG_PTR Arg6,
                                               ULONG_PTR Arg7, ULONG_PTR Arg8, ULONG_PTR Arg9,
                                               ULONG_PTR Arg10, ULONG_PTR Arg11, ULONG_PTR Arg12,
                                               ULONG_PTR Arg13, ULONG_PTR Arg14, ULONG_PTR Arg15);

NTSTATUS NTAPI KiSystemServiceHandler(ULONG SyscallNumber, PKTRAP_FRAME TrapFrame)
{
    if (SyscallNumber >= 0x1000)
    {
        /* Windows x64 syscall convention:
         *   User code does: mov r10, rcx; mov eax, <num>; syscall
         *   After syscall: R10=arg1, RDX=arg2, R8=arg3, R9=arg4
         *   RCX = return address (destroyed by syscall)
         *   Args 5+ are on the user stack at Rsp+0x28, Rsp+0x30, ...
         *   (0x28 = 8 bytes return addr + 32 bytes shadow space)
         */
        ULONG_PTR UserStack = (ULONG_PTR)TrapFrame->Rsp;

        ULONG_PTR arg1  = TrapFrame->R10;
        ULONG_PTR arg2  = TrapFrame->Rdx;
        ULONG_PTR arg3  = TrapFrame->R8;
        ULONG_PTR arg4  = TrapFrame->R9;
        /* Stack args (validate user pointer before reading) */
        ULONG_PTR arg5  = MmIsAddressValid((PVOID)(UserStack + 0x28)) ? *(ULONG_PTR*)(UserStack + 0x28) : 0;
        ULONG_PTR arg6  = MmIsAddressValid((PVOID)(UserStack + 0x30)) ? *(ULONG_PTR*)(UserStack + 0x30) : 0;
        ULONG_PTR arg7  = MmIsAddressValid((PVOID)(UserStack + 0x38)) ? *(ULONG_PTR*)(UserStack + 0x38) : 0;
        ULONG_PTR arg8  = MmIsAddressValid((PVOID)(UserStack + 0x40)) ? *(ULONG_PTR*)(UserStack + 0x40) : 0;
        ULONG_PTR arg9  = MmIsAddressValid((PVOID)(UserStack + 0x48)) ? *(ULONG_PTR*)(UserStack + 0x48) : 0;
        ULONG_PTR arg10 = MmIsAddressValid((PVOID)(UserStack + 0x50)) ? *(ULONG_PTR*)(UserStack + 0x50) : 0;
        ULONG_PTR arg11 = MmIsAddressValid((PVOID)(UserStack + 0x58)) ? *(ULONG_PTR*)(UserStack + 0x58) : 0;
        ULONG_PTR arg12 = MmIsAddressValid((PVOID)(UserStack + 0x60)) ? *(ULONG_PTR*)(UserStack + 0x60) : 0;
        ULONG_PTR arg13 = MmIsAddressValid((PVOID)(UserStack + 0x68)) ? *(ULONG_PTR*)(UserStack + 0x68) : 0;
        ULONG_PTR arg14 = MmIsAddressValid((PVOID)(UserStack + 0x70)) ? *(ULONG_PTR*)(UserStack + 0x70) : 0;
        ULONG_PTR arg15 = MmIsAddressValid((PVOID)(UserStack + 0x78)) ? *(ULONG_PTR*)(UserStack + 0x78) : 0;

        return Win32kSyscallDispatcher(SyscallNumber,
                                        arg1, arg2, arg3, arg4, arg5, arg6,
                                        arg7, arg8, arg9, arg10, arg11, arg12,
                                        arg13, arg14, arg15);
    }

    if (SyscallNumber >= SYSCALL_MAX || !KiServiceTable[SyscallNumber])
    {
        DbgPrint("KE: unknown syscall %llu\n", (ULONG64)SyscallNumber);
        return STATUS_INVALID_HANDLE;
    }

    /* Call the handler with the trap frame — args are in TrapFrame->Rcx/Rdx/R8-R11 */
    return KiServiceTable[SyscallNumber](TrapFrame);
}
