/*
 * MinNT - ndk/psfuncs.h
 * Process and Thread functions.
 */

#ifndef _NDK_PSFUNCS_H_
#define _NDK_PSFUNCS_H_

#include <nt/ntdef.h>
#include <nt/ob.h>
#include <nt/ps.h>
#include <ndk/setypes.h>

/* ---- Client ID ---------------------------------------------------------- */

#ifndef _CLIENT_ID_DEFINED
#define _CLIENT_ID_DEFINED
typedef struct _CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} CLIENT_ID, *PCLIENT_ID;
#endif

/* ---- Process info classes ----------------------------------------------- */

#define ProcessBasicInformation     0
#define ProcessSessionInformation  24
#define ProcessDefaultHardErrorMode 29
#define ProcessBasePriority         3

typedef struct _PROCESS_BASIC_INFORMATION {
    NTSTATUS ExitStatus;
    PVOID PebBaseAddress;
    ULONG_PTR AffinityMask;
    LONG BasePriority;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR InheritedFromUniqueProcessId;
} PROCESS_BASIC_INFORMATION, *PPROCESS_BASIC_INFORMATION;

/* ---- RTL_USER_PROCESS_INFORMATION --------------------------------------- */

typedef struct _SECTION_IMAGE_INFORMATION {
    PVOID ImageBaseAddress;
    ULONG ImageAttributes;
    ULONG ImageSubSystemType;
    union {
        ULONG ImageDllNameOffsetAndLength;
        struct {
            USHORT ImageDllNameOffset;
            USHORT ImageDllNameLength;
        };
    };
    ULONG ImageEntryPoint;
    ULONG SizeOfImage;
    ULONG ImageCheckSum;
    ULONG NumberOfSections;
    ULONG SectionAlignment;
    USHORT DllCharacteristics;
    USHORT Machine;
    ULONG Reserved[3];
} SECTION_IMAGE_INFORMATION, *PSECTION_IMAGE_INFORMATION;

typedef struct _RTL_USER_PROCESS_INFORMATION {
    ULONG Size;
    HANDLE ProcessHandle;
    HANDLE ThreadHandle;
    CLIENT_ID ClientId;
    PVOID ImageBaseAddress;
    SECTION_IMAGE_INFORMATION ImageInformation;
} RTL_USER_PROCESS_INFORMATION, *PRTL_USER_PROCESS_INFORMATION;

/* ---- User process parameters -------------------------------------------- */

typedef struct _RTL_USER_PROCESS_PARAMETERS {
    ULONG MaximumLength;
    ULONG Length;
    ULONG Flags;
    ULONG DebugFlags;
    HANDLE ConsoleHandle;
    ULONG ConsoleFlags;
    HANDLE StandardInput;
    HANDLE StandardOutput;
    HANDLE StandardError;
    UNICODE_STRING DllPath;
    UNICODE_STRING ImagePathName;
    UNICODE_STRING CommandLine;
    PVOID Environment;
    ULONG StartingX;
    ULONG StartingY;
    ULONG CountX;
    ULONG CountY;
    ULONG CountCharsX;
    ULONG CountCharsY;
    ULONG WindowFlags;
    UNICODE_STRING WindowTitle;
    UNICODE_STRING DesktopInfo;
    UNICODE_STRING ShellInfo;
    UNICODE_STRING RuntimeData;
} RTL_USER_PROCESS_PARAMETERS, *PRTL_USER_PROCESS_PARAMETERS;

#define RTL_USER_PROCESS_PARAMETERS_RESERVE_1MB  0x00000200
#define RTL_USER_PROCESS_PARAMETERS_NX           0x00002000

/* ---- Process/thread creation -------------------------------------------- */

NTSTATUS NTAPI NtCreateProcess(
    PHANDLE ProcessHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    HANDLE InheritedProcessHandle,
    BOOLEAN InheritObjectTable,
    HANDLE SectionHandle OPTIONAL,
    HANDLE DebugPort OPTIONAL,
    HANDLE ExceptionPort OPTIONAL
);

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
);

NTSTATUS NTAPI NtResumeThread(
    HANDLE ThreadHandle,
    PULONG PreviousSuspendCount
);

NTSTATUS NTAPI NtTerminateProcess(
    HANDLE ProcessHandle,
    NTSTATUS ExitStatus
);

NTSTATUS NTAPI NtTerminateThread(
    HANDLE ThreadHandle,
    NTSTATUS ExitStatus
);

NTSTATUS NTAPI NtWaitForSingleObject(
    HANDLE Handle,
    BOOLEAN Alertable,
    PLARGE_INTEGER Timeout
);

NTSTATUS NTAPI NtWaitForMultipleObjects(
    ULONG Count,
    HANDLE *Handles,
    WAIT_TYPE WaitType,
    BOOLEAN Alertable,
    PLARGE_INTEGER Timeout
);

NTSTATUS NTAPI NtDuplicateObject(
    HANDLE SourceProcess,
    HANDLE SourceHandle,
    HANDLE TargetProcess,
    PHANDLE TargetHandle,
    ACCESS_MASK DesiredAccess,
    ULONG HandleAttributes,
    ULONG Options
);

NTSTATUS NTAPI NtQueryInformationProcess(
    HANDLE ProcessHandle,
    ULONG ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength
);

NTSTATUS NTAPI NtSetInformationProcess(
    HANDLE ProcessHandle,
    ULONG ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength
);

NTSTATUS NTAPI NtOpenProcessToken(
    HANDLE ProcessHandle,
    ACCESS_MASK DesiredAccess,
    PHANDLE TokenHandle
);

NTSTATUS NTAPI NtQueryInformationToken(
    HANDLE TokenHandle,
    ULONG TokenInformationClass,
    PVOID TokenInformation,
    ULONG TokenInformationLength,
    PULONG ReturnLength
);

/* ---- NtCurrentPeb / NtCurrentProcess / NtCurrentThread ------------------ */

#define NtCurrentProcess()  ((HANDLE)(LONG_PTR)-1)
#define NtCurrentThread()   ((HANDLE)(LONG_PTR)-2)

#ifndef _PEB_DEFINED
#define _PEB_DEFINED
typedef struct _PEB {
    UCHAR Reserved1[2];
    UCHAR BeingDebugged;
    UCHAR Reserved2[1];
    PVOID Reserved3[2];
    struct _PEB_LDR_DATA *Ldr;
    struct _RTL_USER_PROCESS_PARAMETERS *ProcessParameters;
    PVOID Reserved4[3];
    PVOID TlsBitmap;
    ULONG64 TlsBitmapBits[2];
    PVOID Reserved5[26];
    ULONG Reserved6;
    PVOID Reserved7[1];
    ULONG64 SessionId;
} PEB, *PPEB;

typedef struct _PEB_LDR_DATA {
    ULONG Length;
    UCHAR Initialized;
    HANDLE SsHandle;
} PEB_LDR_DATA, *PPEB_LDR_DATA;
#endif

FORCEINLINE PPEB NtCurrentPeb(VOID)
{
    PPEB Peb;
    __asm__ __volatile__("movq %%gs:0x60, %0" : "=r"(Peb));
    return Peb;
}

/* ---- Image subsystem types ---------------------------------------------- */

#define IMAGE_SUBSYSTEM_UNKNOWN         0
#define IMAGE_SUBSYSTEM_NATIVE          1
#define IMAGE_SUBSYSTEM_WINDOWS_CUI     3
#define IMAGE_SUBSYSTEM_WINDOWS_GUI     2
#define IMAGE_SUBSYSTEM_POSIX_CUI       7
#define IMAGE_SUBSYSTEM_OS2_CUI         5

typedef struct _IMAGE_SUBSYSTEM_INFORMATION {
    USHORT SubSystemType;
} IMAGE_SUBSYSTEM_INFORMATION, *PIMAGE_SUBSYSTEM_INFORMATION;

/* ---- RTL_USER_PROCESS_INFORMATION extension ---------------------------- */

typedef struct _EXTENDED_RTL_USER_PROCESS_INFORMATION {
    RTL_USER_PROCESS_INFORMATION Core;
    IMAGE_SUBSYSTEM_INFORMATION ImageInformation;
} EXTENDED_RTL_USER_PROCESS_INFORMATION, *PEXTENDED_RTL_USER_PROCESS_INFORMATION;

/* ---- RtlCreateUserProcess ----------------------------------------------- */

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
);

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
    PUNICODE_STRING RuntimeData
);

VOID NTAPI RtlDestroyProcessParameters(PRTL_USER_PROCESS_PARAMETERS ProcessParameters);

#endif /* _NDK_PSFUNCS_H_ */
