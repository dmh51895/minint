/*
 * MinNT - ndk/ndk_shim.c
 * Compatibility shim layer: maps NT/NDK API calls to MinNT internal APIs.
 * This allows real ReactOS subsystem code (SMSS, CSRSS, Winlogon) to compile
 * and run on MinNT's kernel thread architecture.
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/ps.h>
#include <nt/rtl.h>
#include <nt/cm.h>
#include <nt/se.h>
#include <nt/lpc.h>
#include <nt/hal.h>
#include <nt/dispatcher.h>
#include <ndk/setypes.h>
#include <ndk/rtlfuncs.h>
#include <ndk/cmfuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/kefuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/exfuncs.h>

/* ==== OBJECT DIRECTORY SHIMS ============================================= */

static POBJECT_TYPE DirectoryObjectType = NULL;
static POBJECT_TYPE SymbolicLinkObjectType = NULL;

NTSTATUS NTAPI NdkInitObjectTypes(VOID)
{
    NTSTATUS status;
    UNICODE_STRING dirTypeName, linkTypeName;

    RtlInitUnicodeString(&dirTypeName, L"DirectoryObject");
    status = ObCreateObjectType(&dirTypeName, 'DirT', NULL, &DirectoryObjectType);
    if (!NT_SUCCESS(status)) return status;

    RtlInitUnicodeString(&linkTypeName, L"SymbolicLinkObject");
    status = ObCreateObjectType(&linkTypeName, 'SymT', NULL, &SymbolicLinkObjectType);
    if (!NT_SUCCESS(status)) return status;

    DbgPrint("NDK_SHIM: directory and symlink object types initialized\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtCreateDirectoryObject(
    PHANDLE DirectoryHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes
)
{
    PVOID body;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DesiredAccess);

    if (!DirectoryObjectType) {
        NdkInitObjectTypes();
    }

    status = ObCreateObject(DirectoryObjectType, 256,
                            ObjectAttributes ? ObjectAttributes->ObjectName : NULL,
                            &body);
    if (NT_SUCCESS(status)) {
        status = ObInsertHandle(body, DirectoryHandle);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(body);
        }
    }

    return status;
}

NTSTATUS NTAPI NtOpenDirectoryObject(
    PHANDLE DirectoryHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes
)
{
    PVOID body;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DesiredAccess);

    status = ObLookupObjectByName(ObjectAttributes->ObjectName, &body);
    if (NT_SUCCESS(status)) {
        status = ObInsertHandle(body, DirectoryHandle);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(body);
        }
    }

    return status;
}

NTSTATUS NTAPI NtQueryDirectoryObject(
    HANDLE DirectoryHandle,
    PVOID Buffer,
    ULONG BufferLength,
    BOOLEAN ReturnSingleEntry,
    BOOLEAN RestartScan,
    PULONG Context,
    PULONG ReturnLength
)
{
    UNREFERENCED_PARAMETER(DirectoryHandle);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(BufferLength);
    UNREFERENCED_PARAMETER(ReturnSingleEntry);
    UNREFERENCED_PARAMETER(RestartScan);
    if (Context) *Context = 0;
    if (ReturnLength) *ReturnLength = 0;
    return STATUS_NO_MORE_ENTRIES;
}

/* ==== SYMBOLIC LINK SHIMS =============================================== */

NTSTATUS NTAPI NtCreateSymbolicLinkObject(
    PHANDLE LinkHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PUNICODE_STRING TargetName
)
{
    PVOID body;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(TargetName);

    if (!SymbolicLinkObjectType) {
        NdkInitObjectTypes();
    }

    status = ObCreateObject(SymbolicLinkObjectType, 128,
                            ObjectAttributes ? ObjectAttributes->ObjectName : NULL,
                            &body);
    if (NT_SUCCESS(status)) {
        status = ObInsertHandle(body, LinkHandle);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(body);
        }
    }

    return status;
}

NTSTATUS NTAPI NtOpenSymbolicLinkObject(
    PHANDLE LinkHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes
)
{
    PVOID body;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DesiredAccess);

    status = ObLookupObjectByName(ObjectAttributes->ObjectName, &body);
    if (NT_SUCCESS(status)) {
        status = ObInsertHandle(body, LinkHandle);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(body);
        }
    }

    return status;
}

NTSTATUS NTAPI NtQuerySymbolicLinkObject(
    HANDLE LinkHandle,
    PUNICODE_STRING TargetName,
    PULONG DataLength
)
{
    UNREFERENCED_PARAMETER(LinkHandle);
    if (TargetName) TargetName->Length = 0;
    if (DataLength) *DataLength = 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtMakeTemporaryObject(HANDLE Handle)
{
    return ObCloseHandle(Handle);
}

/* ==== REGISTRY SHIMS ==================================================== */

NTSTATUS NTAPI NtOpenKey(
    PHANDLE KeyHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes
)
{
    PCM_KEY_NODE keyNode;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DesiredAccess);

    status = CmOpenKey(ObjectAttributes->ObjectName, KEY_READ, &keyNode);
    if (NT_SUCCESS(status)) {
        *KeyHandle = (HANDLE)keyNode;
    }

    return status;
}

NTSTATUS NTAPI NtQueryValueKey(
    HANDLE KeyHandle,
    PUNICODE_STRING ValueName,
    ULONG KeyValueInformationClass,
    PVOID KeyValueInformation,
    ULONG Length,
    PULONG ResultLength
)
{
    ULONG dataType;
    UCHAR dataBuffer[256];
    ULONG actualLength;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(KeyValueInformationClass);

    status = CmQueryValue((PCM_KEY_NODE)KeyHandle, ValueName,
                          &dataType, dataBuffer, sizeof(dataBuffer), &actualLength);
    if (NT_SUCCESS(status)) {
        PKEY_VALUE_PARTIAL_INFORMATION info = (PKEY_VALUE_PARTIAL_INFORMATION)KeyValueInformation;

        if (Length < sizeof(KEY_VALUE_PARTIAL_INFORMATION) + actualLength) {
            *ResultLength = sizeof(KEY_VALUE_PARTIAL_INFORMATION) + actualLength;
            return STATUS_BUFFER_TOO_SMALL;
        }

        info->TitleIndex = 0;
        info->Type = dataType;
        info->DataLength = actualLength;
        RtlCopyMemory(info->Data, dataBuffer, actualLength);
        *ResultLength = sizeof(KEY_VALUE_PARTIAL_INFORMATION) + actualLength;
    }

    return status;
}

NTSTATUS NTAPI NtSetValueKey(
    HANDLE KeyHandle,
    PUNICODE_STRING ValueName,
    ULONG TitleIndex,
    ULONG Type,
    PVOID Data,
    ULONG DataSize
)
{
    UNREFERENCED_PARAMETER(TitleIndex);
    return CmSetValue((PCM_KEY_NODE)KeyHandle, ValueName, Type, Data, DataSize);
}

NTSTATUS NTAPI NtEnumerateKey(
    HANDLE KeyHandle,
    ULONG Index,
    ULONG KeyInformationClass,
    PVOID KeyInformation,
    ULONG Length,
    PULONG ResultLength
)
{
    UNREFERENCED_PARAMETER(KeyHandle);
    UNREFERENCED_PARAMETER(Index);
    UNREFERENCED_PARAMETER(KeyInformationClass);
    UNREFERENCED_PARAMETER(KeyInformation);
    UNREFERENCED_PARAMETER(Length);
    if (ResultLength) *ResultLength = 0;
    return STATUS_NO_MORE_ENTRIES;
}

/* ==== SECTION SHIMS ===================================================== */

/* Internal section object — mirrors real NT SECTION but uses our APIs */
typedef struct _SECTION {
    SIZE_T      MaximumSize;
    SIZE_T      CurrentSize;
    PVOID       BackingStore;
    ULONG       SectionAttributes;
    ULONG       ProtectionAttributes;
} SECTION, *PSECTION;

#define OBJ_TYPE_SECTION  'Sec '

static POBJECT_TYPE SectionObjectType = NULL;

static VOID NtSectionDeleteProcedure(PVOID Body)
{
    PSECTION section = (PSECTION)Body;
    if (section && section->BackingStore) {
        ExFreePoolWithTag(section->BackingStore, TAG_SECT);
    }
}

static NTSTATUS NtEnsureSectionTypeInitialized(VOID)
{
    if (!SectionObjectType) {
        UNICODE_STRING name;
        RtlInitUnicodeString(&name, L"SectionObject");
        return ObCreateObjectType(&name, OBJ_TYPE_SECTION,
                                  NtSectionDeleteProcedure, &SectionObjectType);
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtCreateSection(
    PHANDLE SectionHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PLARGE_INTEGER MaximumSize,
    ULONG SectionAttributes,
    ULONG ProtectionAttributes,
    HANDLE FileHandle
)
{
    PSECTION section;
    SIZE_T size;
    NTSTATUS status;
    PVOID body;

    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(ObjectAttributes);
    UNREFERENCED_PARAMETER(FileHandle);

    if (!SectionHandle) return STATUS_INVALID_PARAMETER;

    /* Get size from MaximumSize or default to 64KB */
    if (MaximumSize) {
        size = MaximumSize->LowPart;
        if (size == 0) size = MaximumSize->HighPart;
    } else {
        size = 0x10000; /* 64KB default */
    }
    if (size == 0) size = 0x10000;

    /* Ensure section object type exists */
    status = NtEnsureSectionTypeInitialized();
    if (!NT_SUCCESS(status)) return status;

    /* Create section object via ObCreateObject */
    status = ObCreateObject(SectionObjectType, sizeof(SECTION),
                            ObjectAttributes ? ObjectAttributes->ObjectName : NULL,
                            &body);
    if (!NT_SUCCESS(status)) return status;

    section = (PSECTION)body;
    RtlZeroMemory(section, sizeof(SECTION));
    section->MaximumSize = size;
    section->CurrentSize = 0;
    section->SectionAttributes = SectionAttributes;
    section->ProtectionAttributes = ProtectionAttributes;

    /* Allocate backing store */
    section->BackingStore = ExAllocatePoolWithTag(NonPagedPool, size, TAG_SECT);
    if (!section->BackingStore) {
        ObDereferenceObject(body);
        *SectionHandle = NULL;
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(section->BackingStore, size);

    /* Create handle */
    status = ObInsertHandle(body, SectionHandle);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(section->BackingStore, TAG_SECT);
        ObDereferenceObject(body);
        *SectionHandle = NULL;
        return status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtOpenSection(
    PHANDLE SectionHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes
)
{
    NTSTATUS status;
    PVOID body;

    UNREFERENCED_PARAMETER(DesiredAccess);

    if (!SectionHandle || !ObjectAttributes || !ObjectAttributes->ObjectName)
        return STATUS_INVALID_PARAMETER;

    /* Ensure section object type exists */
    status = NtEnsureSectionTypeInitialized();
    if (!NT_SUCCESS(status)) return status;

    /* Look up section by name */
    status = ObLookupObjectByName(ObjectAttributes->ObjectName, &body);
    if (!NT_SUCCESS(status)) {
        *SectionHandle = NULL;
        return status;
    }

    /* Verify it's a section object */
    if (BODY_TO_HEADER(body)->Type != SectionObjectType) {
        ObDereferenceObject(body);
        *SectionHandle = NULL;
        return STATUS_ACCESS_DENIED;
    }

    /* Create handle to existing section */
    status = ObInsertHandle(body, SectionHandle);
    ObDereferenceObject(body);

    if (!NT_SUCCESS(status)) {
        *SectionHandle = NULL;
    }

    return status;
}

NTSTATUS NTAPI NtMapViewOfSection(
    HANDLE SectionHandle,
    HANDLE ProcessHandle,
    PVOID *BaseAddress,
    ULONG_PTR ZeroBits,
    SIZE_T CommitSize,
    PLARGE_INTEGER SectionOffset,
    PSIZE_T ViewSize,
    DWORD InheritDisposition,
    ULONG AllocationType,
    ULONG Win32Protect
)
{
    PSECTION section;
    NTSTATUS status;
    PVOID mappedAddr;

    UNREFERENCED_PARAMETER(ProcessHandle);
    UNREFERENCED_PARAMETER(ZeroBits);
    UNREFERENCED_PARAMETER(InheritDisposition);
    UNREFERENCED_PARAMETER(AllocationType);
    UNREFERENCED_PARAMETER(Win32Protect);

    if (!SectionHandle || !BaseAddress)
        return STATUS_INVALID_PARAMETER;

    /* Ensure section object type exists */
    status = NtEnsureSectionTypeInitialized();
    if (!NT_SUCCESS(status)) {
        *BaseAddress = NULL;
        return status;
    }

    /* Get section object by handle */
    status = ObReferenceObjectByHandle(SectionHandle, SectionObjectType, (PVOID *)&section);

    if (!NT_SUCCESS(status)) {
        *BaseAddress = NULL;
        return status;
    }

    /* Determine view size */
    if (ViewSize && *ViewSize > 0) {
        CommitSize = *ViewSize;
    } else {
        CommitSize = section->CurrentSize;
        if (CommitSize == 0) CommitSize = 0x1000; /* at least one page */
    }

    /* Allocate VA for the mapping */
    mappedAddr = MmAllocateVirtualMemory(
        NULL,           /* process = current */
        BaseAddress,    /* accept any address */
        0,              /* zero bits */
        CommitSize,
        MM_VAD_COMMIT,
        PAGE_READWRITE
    );

    if (!mappedAddr) {
        ObDereferenceObject(section);
        *BaseAddress = NULL;
        return STATUS_NO_MEMORY;
    }

    /* Copy section data into mapped VA */
    if (section->BackingStore && section->CurrentSize > 0) {
        SIZE_T copySize = section->CurrentSize;
        if (copySize > CommitSize) copySize = CommitSize;
        RtlCopyMemory(mappedAddr, section->BackingStore, copySize);
    }

    /* Update offset */
    if (SectionOffset) {
        SectionOffset->LowPart = 0;
        SectionOffset->HighPart = 0;
    }

    if (ViewSize)
        *ViewSize = CommitSize;

    ObDereferenceObject(section);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtUnmapViewOfSection(
    HANDLE ProcessHandle,
    PVOID BaseAddress
)
{
    UNREFERENCED_PARAMETER(ProcessHandle);
    UNREFERENCED_PARAMETER(BaseAddress);
    return STATUS_SUCCESS;
}

/* ==== PROCESS SHIMS ===================================================== */

NTSTATUS NTAPI NtCreateProcess(
    PHANDLE ProcessHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    HANDLE InheritedProcessHandle,
    BOOLEAN InheritObjectTable,
    HANDLE SectionHandle OPTIONAL,
    HANDLE DebugPort OPTIONAL,
    HANDLE ExceptionPort OPTIONAL
)
{
    PEPROCESS process;
    NTSTATUS status;
    const CHAR *name = "user";

    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(ObjectAttributes);
    UNREFERENCED_PARAMETER(InheritedProcessHandle);
    UNREFERENCED_PARAMETER(InheritObjectTable);
    UNREFERENCED_PARAMETER(SectionHandle);
    UNREFERENCED_PARAMETER(DebugPort);
    UNREFERENCED_PARAMETER(ExceptionPort);

    status = PsCreateSystemProcess(name, &process);
    if (NT_SUCCESS(status)) {
        *ProcessHandle = (HANDLE)process;
    }

    return status;
}

NTSTATUS NTAPI NtResumeThread(
    HANDLE ThreadHandle,
    PULONG PreviousSuspendCount
)
{
    PETHREAD thread;
    NTSTATUS status;

    status = ObReferenceObjectByHandle(ThreadHandle, PsThreadType, (PVOID *)&thread);
    if (NT_SUCCESS(status)) {
        KiReadyThread(&thread->Tcb);
        ObDereferenceObject(thread);
    }

    if (PreviousSuspendCount) *PreviousSuspendCount = 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtTerminateProcess(
    HANDLE ProcessHandle,
    NTSTATUS ExitStatus
)
{
    PEPROCESS process;
    NTSTATUS status;

    if (ProcessHandle == NtCurrentProcess()) {
        process = PsGetCurrentProcess();
    } else {
        status = ObReferenceObjectByHandle(ProcessHandle, PsProcessType, (PVOID *)&process);
        if (!NT_SUCCESS(status)) return status;
    }

    PLIST_ENTRY entry = process->ThreadListHead.Flink;
    while (entry != &process->ThreadListHead) {
        PETHREAD thread = CONTAINING_RECORD(entry, ETHREAD, ThreadListEntry);
        entry = entry->Flink;
        thread->Tcb.State = Terminated;
    }

    if (ProcessHandle != NtCurrentProcess()) {
        ObDereferenceObject(process);
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtTerminateThread(
    HANDLE ThreadHandle,
    NTSTATUS ExitStatus
)
{
    PETHREAD thread;
    NTSTATUS status;

    if (ThreadHandle == NtCurrentThread()) {
        thread = (PETHREAD)KeGetCurrentThread();
    } else {
        status = ObReferenceObjectByHandle(ThreadHandle, PsThreadType, (PVOID *)&thread);
        if (!NT_SUCCESS(status)) return status;
    }

    thread->Tcb.State = Terminated;

    if (ThreadHandle != NtCurrentThread()) {
        ObDereferenceObject(thread);
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtWaitForSingleObject(
    HANDLE Handle,
    BOOLEAN Alertable,
    PLARGE_INTEGER Timeout
)
{
    PVOID object;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Alertable);

    status = ObReferenceObjectByHandle(Handle, NULL, &object);
    if (NT_SUCCESS(status)) {
        KeWaitForSingleObject(object, Executive, KernelMode, FALSE, Timeout);
        ObDereferenceObject(object);
    }

    return status;
}

NTSTATUS NTAPI NtWaitForMultipleObjects(
    ULONG Count,
    HANDLE *Handles,
    WAIT_TYPE WaitType,
    BOOLEAN Alertable,
    PLARGE_INTEGER Timeout
)
{
    if (Count > 0) {
        return NtWaitForSingleObject(Handles[0], Alertable, Timeout);
    }
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS NTAPI NtDuplicateObject(
    HANDLE SourceProcess,
    HANDLE SourceHandle,
    HANDLE TargetProcess,
    PHANDLE TargetHandle,
    ACCESS_MASK DesiredAccess,
    ULONG HandleAttributes,
    ULONG Options
)
{
    PVOID object;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(SourceProcess);
    UNREFERENCED_PARAMETER(TargetProcess);
    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(HandleAttributes);
    UNREFERENCED_PARAMETER(Options);

    status = ObReferenceObjectByHandle(SourceHandle, NULL, &object);
    if (NT_SUCCESS(status)) {
        status = ObInsertHandle(object, TargetHandle);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(object);
        }
    }

    return status;
}

NTSTATUS NTAPI NtQueryInformationProcess(
    HANDLE ProcessHandle,
    ULONG ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength
)
{
    PEPROCESS process;
    NTSTATUS status;

    if (ProcessHandle == NtCurrentProcess()) {
        process = PsGetCurrentProcess();
    } else {
        status = ObReferenceObjectByHandle(ProcessHandle, PsProcessType, (PVOID *)&process);
        if (!NT_SUCCESS(status)) return status;
    }

    switch (ProcessInformationClass) {
    case ProcessBasicInformation: {
        PPROCESS_BASIC_INFORMATION pbi = (PPROCESS_BASIC_INFORMATION)ProcessInformation;
        if (ProcessInformationLength >= sizeof(PROCESS_BASIC_INFORMATION)) {
            pbi->ExitStatus = STATUS_SUCCESS;
            pbi->UniqueProcessId = (ULONG_PTR)process->UniqueProcessId;
            pbi->InheritedFromUniqueProcessId = 0;
            pbi->BasePriority = process->Pcb.BasePriority;
        }
        if (ReturnLength) *ReturnLength = sizeof(PROCESS_BASIC_INFORMATION);
        break;
    }
    case ProcessSessionInformation: {
        if (ProcessInformationLength >= sizeof(ULONG)) {
            *(PULONG)ProcessInformation = 0;
        }
        if (ReturnLength) *ReturnLength = sizeof(ULONG);
        break;
    }
    default:
        status = STATUS_INVALID_PARAMETER;
        break;
    }

    if (ProcessHandle != NtCurrentProcess()) {
        ObDereferenceObject(process);
    }

    return status;
}

NTSTATUS NTAPI NtSetInformationProcess(
    HANDLE ProcessHandle,
    ULONG ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength
)
{
    PEPROCESS process;
    NTSTATUS status;

    if (ProcessHandle == NtCurrentProcess()) {
        process = PsGetCurrentProcess();
    } else {
        status = ObReferenceObjectByHandle(ProcessHandle, PsProcessType, (PVOID *)&process);
        if (!NT_SUCCESS(status)) return status;
    }

    switch (ProcessInformationClass) {
    case ProcessBasePriority: {
        if (ProcessInformationLength >= sizeof(KPRIORITY)) {
            process->Pcb.BasePriority = *(KPRIORITY *)ProcessInformation;
        }
        break;
    }
    case ProcessSessionInformation: {
        break;
    }
    default:
        break;
    }

    if (ProcessHandle != NtCurrentProcess()) {
        ObDereferenceObject(process);
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtOpenProcessToken(
    HANDLE ProcessHandle,
    ACCESS_MASK DesiredAccess,
    PHANDLE TokenHandle
)
{
    UNREFERENCED_PARAMETER(ProcessHandle);
    UNREFERENCED_PARAMETER(DesiredAccess);
    *TokenHandle = (HANDLE)0x1000;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtQueryInformationToken(
    HANDLE TokenHandle,
    ULONG TokenInformationClass,
    PVOID TokenInformation,
    ULONG TokenInformationLength,
    PULONG ReturnLength
)
{
    UNREFERENCED_PARAMETER(TokenHandle);

    if (ReturnLength) *ReturnLength = 0;

    if (TokenInformationClass == 1) { /* TokenUser */
        PTOKEN_USER user = (PTOKEN_USER)TokenInformation;
        if (TokenInformationLength >= sizeof(TOKEN_USER)) {
            PSID sid;
            SID_IDENTIFIER_AUTHORITY ntAuth = {0, 0, 0, 0, 0, 5}; /* NT Authority */
            RtlAllocateAndInitializeSid(&ntAuth, 1,
                                        0x00000012, /* SECURITY_LOCAL_SYSTEM_RID */
                                        0, 0, 0, 0, 0, 0, 0, &sid);
            user->User.Sid = sid;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtCreateThread(
    PHANDLE ThreadHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    HANDLE ProcessHandle,
    PCLIENT_ID ClientId,
    PVOID StartRoutine,
    PVOID StartArgument,
    BOOLEAN CreateSuspended,
    ULONG StackZeroBits,
    SIZE_T StackCommit,
    SIZE_T StackReserve
)
{
    PEPROCESS process;
    PETHREAD thread;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(ObjectAttributes);
    UNREFERENCED_PARAMETER(StackZeroBits);
    UNREFERENCED_PARAMETER(StackCommit);
    UNREFERENCED_PARAMETER(StackReserve);

    if (ProcessHandle == NtCurrentProcess()) {
        process = PsGetCurrentProcess();
    } else {
        status = ObReferenceObjectByHandle(ProcessHandle, PsProcessType, (PVOID *)&process);
        if (!NT_SUCCESS(status)) return status;
    }

    status = PsCreateSystemThread(process, (PVOID)StartRoutine, StartArgument, &thread);
    if (NT_SUCCESS(status)) {
        *ThreadHandle = (HANDLE)thread;
        if (ClientId) {
            ClientId->UniqueProcess = process->UniqueProcessId;
            ClientId->UniqueThread = thread->UniqueThreadId;
        }
    }

    if (ProcessHandle != NtCurrentProcess()) {
        ObDereferenceObject(process);
    }

    return status;
}

/* ==== KERNEL INFO SHIMS ================================================= */

NTSTATUS NTAPI NtQuerySystemInformation(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
)
{
    switch (SystemInformationClass) {
    case SystemBasicInformation: {
        PSYSTEM_BASIC_INFORMATION sbi = (PSYSTEM_BASIC_INFORMATION)SystemInformation;
        if (SystemInformationLength >= sizeof(SYSTEM_BASIC_INFORMATION)) {
            RtlZeroMemory(sbi, sizeof(SYSTEM_BASIC_INFORMATION));
            sbi->AllocationGranularity = 0x10000;
            sbi->MinimumUserModeAddress = 0x10000;
            sbi->MaximumUserModeAddress = 0x7FFFFFFEFFFFULL;
            sbi->NumberOfProcessors = 1;
        }
        if (ReturnLength) *ReturnLength = sizeof(SYSTEM_BASIC_INFORMATION);
        return STATUS_SUCCESS;
    }
    case SystemKernelDebuggerInformation: {
        PSYSTEM_KERNEL_DEBUGGER_INFORMATION skdi = (PSYSTEM_KERNEL_DEBUGGER_INFORMATION)SystemInformation;
        if (SystemInformationLength >= sizeof(SYSTEM_KERNEL_DEBUGGER_INFORMATION)) {
            skdi->DebuggerEnabled = FALSE;
            skdi->DebuggerNotPresent = TRUE;
        }
        if (ReturnLength) *ReturnLength = sizeof(SYSTEM_KERNEL_DEBUGGER_INFORMATION);
        return STATUS_SUCCESS;
    }
    default:
        return STATUS_INVALID_PARAMETER;
    }
}

/* ==== DISPLAY / HARD ERROR SHIMS ======================================== */

NTSTATUS NTAPI NtDisplayString(PUNICODE_STRING String)
{
    if (String && String->Buffer) {
        DbgPrint("%.*ls", String->Length / sizeof(WCHAR), String->Buffer);
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtRaiseHardError(
    NTSTATUS ErrorStatus,
    ULONG NumberOfParameters,
    ULONG UnicodeStringParameterMask,
    PULONG_PTR Parameters,
    ULONG ValidResponseOptions,
    PULONG Response
)
{
    DbgPrint("HARD ERROR: Status=0x%08lx Params=%lu Option=%lu\n",
             (ULONG)ErrorStatus, NumberOfParameters, ValidResponseOptions);

    if (Response) *Response = 0;
    KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, ErrorStatus, 0, 0, 0);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtSetDefaultHardErrorPort(HANDLE PortHandle)
{
    UNREFERENCED_PARAMETER(PortHandle);
    return STATUS_SUCCESS;
}

/* ==== SM API SHIMS ====================================================== */

NTSTATUS NTAPI SmConnectToSm(
    PUNICODE_STRING PortName,
    HANDLE SmApiPort,
    ULONG SubSystemType,
    PHANDLE ConnectionPort
)
{
    UNREFERENCED_PARAMETER(PortName);
    UNREFERENCED_PARAMETER(SmApiPort);
    UNREFERENCED_PARAMETER(SubSystemType);
    *ConnectionPort = (HANDLE)0x2000;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI SmExecPgm(
    HANDLE SmApiPort,
    PRTL_USER_PROCESS_INFORMATION ProcessInformation,
    BOOLEAN DebugSection
)
{
    UNREFERENCED_PARAMETER(SmApiPort);
    UNREFERENCED_PARAMETER(ProcessInformation);
    UNREFERENCED_PARAMETER(DebugSection);
    return STATUS_SUCCESS;
}

/* ==== RTL FUNCTION SHIMS ================================================ */

void NTAPI RtlInitEmptyUnicodeString(PUNICODE_STRING Dst, PWSTR Buffer, USHORT MaxLen)
{
    Dst->Length = 0;
    Dst->MaximumLength = MaxLen;
    Dst->Buffer = Buffer;
}

NTSTATUS NTAPI RtlUnicodeStringToAnsiString(PANSI_STRING Dst, PCUNICODE_STRING Src, BOOLEAN Alloc)
{
    UNREFERENCED_PARAMETER(Alloc);

    if (!Src || !Src->Buffer) {
        Dst->Length = 0;
        Dst->Buffer = NULL;
        return STATUS_SUCCESS;
    }

    USHORT len = Src->Length / sizeof(WCHAR);
    if (len >= Dst->MaximumLength) len = Dst->MaximumLength - 1;

    for (USHORT i = 0; i < len; i++) {
        Dst->Buffer[i] = (CHAR)Src->Buffer[i];
    }
    Dst->Buffer[len] = 0;
    Dst->Length = len;

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlAnsiStringToUnicodeString(PUNICODE_STRING Dst, PCANSI_STRING Src, BOOLEAN Alloc)
{
    UNREFERENCED_PARAMETER(Alloc);

    if (!Src || !Src->Buffer) {
        Dst->Length = 0;
        Dst->Buffer = NULL;
        return STATUS_SUCCESS;
    }

    USHORT len = Src->Length;
    if (len >= Dst->MaximumLength) len = Dst->MaximumLength - sizeof(WCHAR);

    for (USHORT i = 0; i < len; i++) {
        Dst->Buffer[i] = (WCHAR)Src->Buffer[i];
    }
    Dst->Buffer[len / sizeof(WCHAR)] = 0;
    Dst->Length = len;

    return STATUS_SUCCESS;
}

void NTAPI RtlFreeUnicodeString(PUNICODE_STRING Str)
{
    UNREFERENCED_PARAMETER(Str);
}

void NTAPI RtlFreeAnsiString(PANSI_STRING Str)
{
    UNREFERENCED_PARAMETER(Str);
}

LONG NTAPI RtlCompareUnicodeString(PCUNICODE_STRING S1, PCUNICODE_STRING S2, BOOLEAN CaseInsensitive)
{
    if (!S1 || !S2) return S1 == S2 ? 0 : -1;
    if (CaseInsensitive) {
        USHORT len = S1->Length < S2->Length ? S1->Length : S2->Length;
        for (USHORT i = 0; i < len / sizeof(WCHAR); i++) {
            WCHAR c1 = S1->Buffer[i];
            WCHAR c2 = S2->Buffer[i];
            if (c1 >= L'A' && c1 <= L'Z') c1 += 32;
            if (c2 >= L'A' && c2 <= L'Z') c2 += 32;
            if (c1 != c2) return c1 - c2;
        }
        return S1->Length - S2->Length;
    }
    if (S1->Length != S2->Length) return S1->Length - S2->Length;
    return S1->Length == S2->Length && memcmp(S1->Buffer, S2->Buffer, S1->Length) == 0 ? 0 : -1;
}

NTSTATUS NTAPI RtlAppendUnicodeStringToString(PUNICODE_STRING Dst, PCUNICODE_STRING Src)
{
    if (Dst->Length + Src->Length >= Dst->MaximumLength) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlCopyMemory(Dst->Buffer + Dst->Length / sizeof(WCHAR),
                   Src->Buffer,
                   Src->Length);
    Dst->Length += Src->Length;

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlAppendUnicodeToString(PUNICODE_STRING Dst, PCWSTR Src)
{
    UNICODE_STRING srcStr;
    RtlInitUnicodeString(&srcStr, Src);
    return RtlAppendUnicodeStringToString(Dst, &srcStr);
}

/* ==== HEAP SHIMS ======================================================== */

static PVOID NdkHeapBase = NULL;
static SIZE_T NdkHeapSize = 0;
static SIZE_T NdkHeapUsed = 0;

PVOID NTAPI RtlGetProcessHeap(VOID)
{
    if (!NdkHeapBase) {
        NdkHeapSize = 1024 * 1024;
        NdkHeapBase = ExAllocatePoolWithTag(NonPagedPool, NdkHeapSize, 'HlpN');
        NdkHeapUsed = 0;
    }
    return NdkHeapBase;
}

PVOID NTAPI RtlCreateHeap(ULONG Flags, PVOID Base, SIZE_T Reserve, SIZE_T Commit, PVOID Lock, PVOID Params)
{
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Reserve);
    UNREFERENCED_PARAMETER(Commit);
    UNREFERENCED_PARAMETER(Lock);
    UNREFERENCED_PARAMETER(Params);

    if (Base) return Base;
    return RtlGetProcessHeap();
}

PVOID NTAPI RtlAllocateHeap(PVOID Heap, ULONG Flags, SIZE_T Size)
{
    UNREFERENCED_PARAMETER(Heap);
    UNREFERENCED_PARAMETER(Flags);

    PVOID base = RtlGetProcessHeap();
    PVOID ptr = (PCHAR)base + NdkHeapUsed;

    SIZE_T alignedSize = (Size + 15) & ~15;
    NdkHeapUsed += alignedSize;

    if (NdkHeapUsed > NdkHeapSize) {
        return NULL;
    }

    return ptr;
}

BOOLEAN NTAPI RtlFreeHeap(PVOID Heap, ULONG Flags, PVOID Base)
{
    UNREFERENCED_PARAMETER(Heap);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Base);
    return TRUE;
}

SIZE_T NTAPI RtlSizeHeap(PVOID Heap, ULONG Flags, PVOID Base)
{
    UNREFERENCED_PARAMETER(Heap);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Base);
    return 0;
}

/* ==== PRIVILEGE SHIMS =================================================== */

NTSTATUS NTAPI RtlAdjustPrivilege(ULONG Privilege, BOOLEAN Enable, BOOLEAN CurrentThread, PBOOLEAN WasEnabled)
{
    UNREFERENCED_PARAMETER(Privilege);
    UNREFERENCED_PARAMETER(Enable);
    UNREFERENCED_PARAMETER(CurrentThread);
    if (WasEnabled) *WasEnabled = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlSetProcessIsCritical(BOOLEAN NewValue, PBOOLEAN OldValue, BOOLEAN NeedScb)
{
    UNREFERENCED_PARAMETER(NewValue);
    UNREFERENCED_PARAMETER(NeedScb);
    if (OldValue) *OldValue = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlSetThreadIsCritical(BOOLEAN NewValue, PBOOLEAN OldValue, BOOLEAN NeedScb)
{
    UNREFERENCED_PARAMETER(NewValue);
    UNREFERENCED_PARAMETER(NeedScb);
    if (OldValue) *OldValue = FALSE;
    return STATUS_SUCCESS;
}

/* ==== REGISTRY QUERY SHIM =============================================== */

NTSTATUS NTAPI RtlQueryRegistryValues(
    ULONG RelativeTo,
    PCWSTR Path,
    PRTL_QUERY_REGISTRY_TABLE QueryTable,
    PVOID Context,
    PVOID Environment
)
{
    UNREFERENCED_PARAMETER(RelativeTo);
    UNREFERENCED_PARAMETER(Path);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Environment);

    if (QueryTable) {
        for (ULONG i = 0; QueryTable[i].QueryRoutine || QueryTable[i].Flags; i++) {
            if (QueryTable[i].QueryRoutine && QueryTable[i].Default) {
                QueryTable[i].QueryRoutine(
                    QueryTable[i].Name,
                    QueryTable[i].Type,
                    QueryTable[i].Default,
                    QueryTable[i].DefaultLength,
                    QueryTable[i].EntryContext,
                    Context
                );
            }
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlCharToInteger(PCSTR Str, ULONG Base, PULONG Value)
{
    ULONG result = 0;
    ULONG sign = 1;

    if (!Str || !Value) return STATUS_INVALID_PARAMETER;

    while (*Str == ' ') Str++;

    if (*Str == '-') { sign = (ULONG)-1; Str++; }
    else if (*Str == '+') Str++;

    if (Base == 0) {
        if (*Str == '0' && (Str[1] == 'x' || Str[1] == 'X')) {
            Base = 16; Str += 2;
        } else if (*Str == '0') {
            Base = 8; Str++;
        } else {
            Base = 10;
        }
    }

    while (*Str) {
        ULONG digit;
        if (*Str >= '0' && *Str <= '9') digit = *Str - '0';
        else if (*Str >= 'a' && *Str <= 'f') digit = *Str - 'a' + 10;
        else if (*Str >= 'A' && *Str <= 'F') digit = *Str - 'A' + 10;
        else break;

        if (digit >= Base) break;
        result = result * Base + digit;
        Str++;
    }

    *Value = result * sign;
    return STATUS_SUCCESS;
}

/* ==== ENVIRONMENT SHIM ================================================== */

NTSTATUS NTAPI RtlSetEnvironmentVariable(
    PVOID *Environment,
    PUNICODE_STRING Name,
    PUNICODE_STRING Value
)
{
    UNREFERENCED_PARAMETER(Environment);
    UNREFERENCED_PARAMETER(Name);
    UNREFERENCED_PARAMETER(Value);
    return STATUS_SUCCESS;
}

/* ==== PATH CONVERSION SHIM ============================================== */

BOOLEAN NTAPI RtlDosPathNameToNtPathName_U(
    PCWSTR DosName,
    PUNICODE_STRING NtName,
    PWSTR *FilePart,
    PVOID RelativeName
)
{
    UNREFERENCED_PARAMETER(FilePart);
    UNREFERENCED_PARAMETER(RelativeName);

    if (!DosName || !NtName) return FALSE;

    RtlInitUnicodeString(NtName, DosName);
    return TRUE;
}

/* ==== STRING FORMATTING SHIMS =========================================== */

NTSTATUS NTAPI RtlStringCbPrintfA(PSTR Dest, SIZE_T DestSize, PCSTR Format, ...)
{
    UNREFERENCED_PARAMETER(Format);
    if (Dest && DestSize > 0) {
        Dest[0] = 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlStringCbLengthW(PCWSTR Str, SIZE_T MaxLen, SIZE_T *Length)
{
    SIZE_T len = 0;
    while (len < MaxLen / sizeof(WCHAR) && Str[len]) len++;
    if (Length) *Length = len * sizeof(WCHAR);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlStringCbCopyNW(PWSTR Dest, SIZE_T DestSize, PCWSTR Src, SIZE_T Count)
{
    SIZE_T i;
    SIZE_T maxChars = (DestSize / sizeof(WCHAR)) - 1;
    if (Count < maxChars) maxChars = Count;

    for (i = 0; i < maxChars && Src[i]; i++) {
        Dest[i] = Src[i];
    }
    Dest[i] = 0;

    return STATUS_SUCCESS;
}

/* ==== SECURITY SHIM IMPLEMENTATIONS ===================================== */

NTSTATUS NTAPI
RtlAllocateAndInitializeSid(
    PSID_IDENTIFIER_AUTHORITY IdentifierAuthority,
    UCHAR SubAuthorityCount,
    ULONG SubAuthority0,
    ULONG SubAuthority1,
    ULONG SubAuthority2,
    ULONG SubAuthority3,
    ULONG SubAuthority4,
    ULONG SubAuthority5,
    ULONG SubAuthority6,
    ULONG SubAuthority7,
    PSID *Sid
)
{
    PSID sid;
    ULONG size = sizeof(SID) + (SubAuthorityCount - 1) * sizeof(ULONG);

    sid = (PSID)ExAllocatePoolWithTag(NonPagedPool, size, 'SidA');
    if (!sid) return STATUS_NO_MEMORY;

    sid->Revision = 1;
    sid->SubAuthorityCount = SubAuthorityCount;
    RtlCopyMemory(&sid->IdentifierAuthority, IdentifierAuthority, sizeof(SID_IDENTIFIER_AUTHORITY));

    if (SubAuthorityCount > 0) sid->SubAuthority[0] = SubAuthority0;
    if (SubAuthorityCount > 1) sid->SubAuthority[1] = SubAuthority1;
    if (SubAuthorityCount > 2) sid->SubAuthority[2] = SubAuthority2;
    if (SubAuthorityCount > 3) sid->SubAuthority[3] = SubAuthority3;
    if (SubAuthorityCount > 4) sid->SubAuthority[4] = SubAuthority4;
    if (SubAuthorityCount > 5) sid->SubAuthority[5] = SubAuthority5;
    if (SubAuthorityCount > 6) sid->SubAuthority[6] = SubAuthority6;
    if (SubAuthorityCount > 7) sid->SubAuthority[7] = SubAuthority7;

    *Sid = sid;
    return STATUS_SUCCESS;
}

ULONG NTAPI RtlLengthSid(PSID Sid)
{
    if (!Sid) return 0;
    return sizeof(SID) + (Sid->SubAuthorityCount - 1) * sizeof(ULONG);
}

VOID NTAPI RtlFreeSid(PSID Sid)
{
    if (Sid) ExFreePool(Sid);
}

NTSTATUS NTAPI
RtlCreateSecurityDescriptor(
    PSECURITY_DESCRIPTOR SecurityDescriptor,
    ULONG Revision
)
{
    RtlZeroMemory(SecurityDescriptor, sizeof(SECURITY_DESCRIPTOR));
    SecurityDescriptor->Revision = (UCHAR)Revision;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
RtlSetDaclSecurityDescriptor(
    PSECURITY_DESCRIPTOR SecurityDescriptor,
    BOOLEAN DaclPresent,
    PACL Dacl,
    BOOLEAN DaclDefaulted
)
{
    SecurityDescriptor->Dacl = Dacl;
    if (DaclPresent) {
        SecurityDescriptor->Control |= SE_DACL_PRESENT;
    }
    if (DaclDefaulted) {
        SecurityDescriptor->Control |= SE_DACL_DEFAULTED;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
RtlGetDaclSecurityDescriptor(
    PSECURITY_DESCRIPTOR SecurityDescriptor,
    PBOOLEAN DaclPresent,
    PACL *Dacl,
    PBOOLEAN DaclDefaulted
)
{
    if (DaclPresent) *DaclPresent = (SecurityDescriptor->Control & SE_DACL_PRESENT) != 0;
    if (Dacl) *Dacl = SecurityDescriptor->Dacl;
    if (DaclDefaulted) *DaclDefaulted = (SecurityDescriptor->Control & SE_DACL_DEFAULTED) != 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
RtlCreateAcl(
    PACL Acl,
    ULONG AclLength,
    ULONG AclRevision
)
{
    Acl->AclRevision = (UCHAR)AclRevision;
    Acl->Sbz1 = 0;
    Acl->AclSize = (USHORT)AclLength;
    Acl->AceCount = 0;
    Acl->Sbz2 = 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
RtlAddAccessAllowedAce(
    PACL Acl,
    ULONG AceRevision,
    ACCESS_MASK AccessMask,
    PSID Sid
)
{
    ULONG aceSize = sizeof(ACCESS_ALLOWED_ACE) + RtlLengthSid(Sid) - sizeof(ULONG);

    if (Acl->AclSize < aceSize) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    PACCESS_ALLOWED_ACE ace = (PACCESS_ALLOWED_ACE)((PCHAR)Acl + sizeof(ACL) +
                              Acl->AceCount * sizeof(ACCESS_ALLOWED_ACE));

    ace->Header.AceType = 0;
    ace->Header.AceFlags = 0;
    ace->Header.AceSize = (USHORT)aceSize;
    ace->Mask = AccessMask;
    RtlCopyMemory(&ace->SidStart, Sid, RtlLengthSid(Sid));

    Acl->AceCount++;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
RtlGetAce(
    PACL Acl,
    ULONG AceIndex,
    PVOID *Ace
)
{
    if (AceIndex >= Acl->AceCount) {
        return STATUS_INVALID_PARAMETER;
    }

    PACCESS_ALLOWED_ACE ace = (PACCESS_ALLOWED_ACE)((PCHAR)Acl + sizeof(ACL));
    for (ULONG i = 0; i < AceIndex; i++) {
        ace = (PACCESS_ALLOWED_ACE)((PCHAR)ace + ace->Header.AceSize);
    }

    *Ace = ace;
    return STATUS_SUCCESS;
}

/* ==== RTL USER THREAD SHIM ============================================= */

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
)
{
    PEPROCESS process;
    PETHREAD thread;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(SecurityDescriptor);
    UNREFERENCED_PARAMETER(CreateSuspended);
    UNREFERENCED_PARAMETER(ZeroBits);
    UNREFERENCED_PARAMETER(MaximumStackSize);
    UNREFERENCED_PARAMETER(CommittedStackSize);

    if (ProcessHandle == NtCurrentProcess()) {
        process = PsGetCurrentProcess();
    } else {
        status = ObReferenceObjectByHandle(ProcessHandle, PsProcessType, (PVOID *)&process);
        if (!NT_SUCCESS(status)) return status;
    }

    status = PsCreateSystemThread(process, StartAddress, StartParameter, &thread);
    if (NT_SUCCESS(status)) {
        *ThreadHandle = (HANDLE)thread;
        if (ClientId) {
            ClientId->UniqueProcess = process->UniqueProcessId;
            ClientId->UniqueThread = thread->UniqueThreadId;
        }
    }

    if (ProcessHandle != NtCurrentProcess()) {
        ObDereferenceObject(process);
    }

    return status;
}

/* ==== PROCESS PARAMETER SHIMS =========================================== */

NTSTATUS NTAPI RtlCreateProcessParameters(
    PRTL_USER_PROCESS_PARAMETERS *ProcessParameters,
    PUNICODE_STRING ImagePathName,
    PUNICODE_STRING DllPath,
    PUNICODE_STRING CurrentDirectory,
    PUNICODE_STRING CommandLine,
    PVOID Environment,
    PUNICODE_STRING WindowTitle,
    PUNICODE_STRING DesktopInfo,
    PUNICODE_STRING ShellInfo,
    PUNICODE_STRING RuntimeData)
{
    PRTL_USER_PROCESS_PARAMETERS params;
    ULONG size = sizeof(RTL_USER_PROCESS_PARAMETERS) + 4096;
    ULONG Flags = 0;

    params = (PRTL_USER_PROCESS_PARAMETERS)ExAllocatePoolWithTag(NonPagedPool, size, 'Proc');
    if (!params) return STATUS_NO_MEMORY;

    RtlZeroMemory(params, size);
    params->MaximumLength = size;
    params->Length = size;
    params->Flags = Flags;

    if (ImagePathName) {
        params->ImagePathName = *ImagePathName;
    }
    if (CommandLine) {
        params->CommandLine = *CommandLine;
    }

    *ProcessParameters = params;
    return STATUS_SUCCESS;
}

VOID NTAPI RtlDestroyProcessParameters(PRTL_USER_PROCESS_PARAMETERS ProcessParameters)
{
    if (ProcessParameters) {
        ExFreePool(ProcessParameters);
    }
}

NTSTATUS NTAPI RtlCreateUserProcess(
    PUNICODE_STRING ImagePathName,
    ULONG Attributes,
    PRTL_USER_PROCESS_PARAMETERS ProcessParameters,
    PVOID ProcessSecurityDescriptor,
    PVOID ThreadSecurityDescriptor,
    HANDLE ParentProcess,
    BOOLEAN InheritHandles,
    HANDLE DebugPort,
    HANDLE ExceptionPort,
    PRTL_USER_PROCESS_INFORMATION ProcessInformation
)
{
    PEPROCESS process;
    PETHREAD thread;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Attributes);
    UNREFERENCED_PARAMETER(ProcessSecurityDescriptor);
    UNREFERENCED_PARAMETER(ThreadSecurityDescriptor);
    UNREFERENCED_PARAMETER(ParentProcess);
    UNREFERENCED_PARAMETER(InheritHandles);
    UNREFERENCED_PARAMETER(DebugPort);
    UNREFERENCED_PARAMETER(ExceptionPort);

    status = PsCreateSystemProcess("user", &process);
    if (!NT_SUCCESS(status)) return status;

    status = PsCreateSystemThread(process, NULL, NULL, &thread);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(process);
        return status;
    }

    ProcessInformation->Size = sizeof(RTL_USER_PROCESS_INFORMATION);
    ProcessInformation->ProcessHandle = (HANDLE)process;
    ProcessInformation->ThreadHandle = (HANDLE)thread;
    ProcessInformation->ClientId.UniqueProcess = process->UniqueProcessId;
    ProcessInformation->ClientId.UniqueThread = thread->UniqueThreadId;

    return STATUS_SUCCESS;
}

/* ==== WIDE CHAR COMPARE ================================================== */


