/*
 * MinNT - ntdef.h
 * Core NT type system. Everything in the kernel speaks these types.
 * Targets NT 6.x ABI conventions on x86-64 (LP64 here since we build
 * with GCC/ELF; David patches to LLP64/PE when moving to MSVC/mingw).
 */

#ifndef _NTDEF_H_
#define _NTDEF_H_

#include <stdint.h>
#include <stddef.h>

/* ---- Fundamental scalar types (wdm.h style) ---------------------------- */

typedef void            VOID, *PVOID;
typedef uint8_t         UCHAR,  *PUCHAR;
typedef char            CHAR,   *PCHAR;
typedef uint16_t        USHORT, *PUSHORT;
typedef int16_t         CSHORT;
typedef int16_t         SHORT,  *PSHORT;
typedef uint32_t        ULONG,  *PULONG;
typedef int32_t         LONG,   *PLONG;
typedef int             INT,    *PINT;
typedef uint32_t        ULONG32, *PULONG32;
typedef uint64_t        ULONGLONG, ULONG64, *PULONG64;
typedef int64_t         LONGLONG,  LONG64,  *PLONG64;
typedef uint64_t        ULONG_PTR, *PULONG_PTR;
typedef int64_t         LONG_PTR;
typedef ULONG_PTR       SIZE_T, *PSIZE_T;
typedef uint8_t         BOOLEAN, *PBOOLEAN;
typedef uint16_t        WCHAR,  *PWCHAR, *PWSTR;
typedef const WCHAR    *PCWSTR;
typedef PVOID           HANDLE, *PHANDLE;
typedef LONG            NTSTATUS;
typedef UCHAR           KIRQL, *PKIRQL;
typedef UCHAR           CCHAR;
typedef ULONG_PTR       KSPIN_LOCK, *PKSPIN_LOCK;
typedef LONG            KPRIORITY;
typedef ULONG64         PHYSICAL_ADDRESS;

#define TRUE   1
#define FALSE  0
#ifndef NULL
#define NULL ((PVOID)0)
#endif

#define IN
#define OUT
#define OPTIONAL
#define NTAPI                       /* MS ABI shim point for the MSVC port */
#define DECLSPEC_NORETURN  __attribute__((noreturn))
#define FORCEINLINE        static inline __attribute__((always_inline))
#define UNREFERENCED_PARAMETER(P) ((void)(P))

/* ---- NTSTATUS --------------------------------------------------------- */

#define NT_SUCCESS(Status)      (((NTSTATUS)(Status)) >= 0)
#define STATUS_SUCCESS                  ((NTSTATUS)0x00000000L)
#define STATUS_NOT_FOUND                ((NTSTATUS)0xC0000227L)
#define STATUS_UNSUCCESSFUL             ((NTSTATUS)0xC0000001L)
#define STATUS_NOT_IMPLEMENTED          ((NTSTATUS)0xC0000002L)
#define STATUS_NOT_SUPPORTED            ((NTSTATUS)0xC00000BBL)
#define STATUS_INVALID_PARAMETER        ((NTSTATUS)0xC000000DL)
#define STATUS_NO_MEMORY                ((NTSTATUS)0xC0000017L)
#define STATUS_ACCESS_DENIED            ((NTSTATUS)0xC0000022L)
#define STATUS_OBJECT_NAME_NOT_FOUND    ((NTSTATUS)0xC0000034L)
#define STATUS_NO_SUCH_FILE             ((NTSTATUS)0xC000000FL)
#define STATUS_INVALID_IMAGE_FORMAT     ((NTSTATUS)0xC000007BL)
#define STATUS_DEVICE_DATA_ERROR        ((NTSTATUS)0xC000009CL)
#define STATUS_IO_TIMEOUT               ((NTSTATUS)0xC00000B5L)
#define STATUS_INSUFFICIENT_RESOURCES   ((NTSTATUS)0xC000009AL)
#define STATUS_INVALID_HANDLE           ((NTSTATUS)0xC0000008L)
#define STATUS_INVALID_API_VERSION      ((NTSTATUS)0xC0000021L)
#define STATUS_ACCESS_VIOLATION         ((NTSTATUS)0xC0000005L)
#define STATUS_END_OF_FILE              ((NTSTATUS)0xC0000011L)
#define STATUS_MORE_ENTRIES             ((NTSTATUS)0x00000105L)
#define STATUS_BUFFER_OVERFLOW          ((NTSTATUS)0x80000005L)
#define STATUS_BUFFER_TOO_SMALL         ((NTSTATUS)0xC0000023L)
#define STATUS_OBJECT_NAME_INVALID      ((NTSTATUS)0xC0000033L)
#define STATUS_OBJECT_PATH_INVALID      ((NTSTATUS)0xC0000039L)
#define STATUS_OBJECT_PATH_NOT_FOUND    ((NTSTATUS)0xC000003AL)
#define STATUS_OBJECT_NAME_COLLISION    ((NTSTATUS)0xC0000035L)
#define STATUS_OBJECT_NAME_EXISTS       ((NTSTATUS)0x00000000L)
#define STATUS_OBJECT_NAME_DOES_NOT_EXIST ((NTSTATUS)0xC0000034L)
#define STATUS_DEVICE_BUSY              ((NTSTATUS)0xC00000B7L)
#define STATUS_DEVICE_DOES_NOT_EXIST    ((NTSTATUS)0xC00000C0L)
#define STATUS_NO_MORE_ENTRIES          ((NTSTATUS)0x8000001AL)
#define STATUS_FILE_IS_A_DIRECTORY      ((NTSTATUS)0xC00000BACL)
#define STATUS_NOT_A_DIRECTORY          ((NTSTATUS)0xC0000103L)
#define STATUS_SHARING_VIOLATION        ((NTSTATUS)0xC0000043L)
#define STATUS_FILE_LOCKED              ((NTSTATUS)0xC0000054L)
#define STATUS_FILE_NOT_AVAILABLE       ((NTSTATUS)0xC00000D3L)
#define STATUS_WRONG_PASSWORD           ((NTSTATUS)0xC000006AL)
#define STATUS_REQUEST_ABORTED          ((NTSTATUS)0xC0000123L)
#define STATUS_IO_DEVICE_ERROR          ((NTSTATUS)0xC0000185L)
#define STATUS_INVALID_DEVICE_REQUEST   ((NTSTATUS)0xC0000010L)
#define STATUS_NOT_MAPPED_VIEW          ((NTSTATUS)0xC000001EL)
#define STATUS_CONFLICTING_ADDRESSES    ((NTSTATUS)0xC000001EL)
#define STATUS_NO_MORE_ENTRIES          ((NTSTATUS)0x8000001AL)
#define STATUS_NO_TRANSLATION           ((NTSTATUS)0xC0000101L)
#define STATUS_BUFFER_TOO_SMALL         ((NTSTATUS)0xC0000023L)

#define OBJ_CASE_INSENSITIVE    0x00000040
#define OBJ_KERNEL_HANDLE       0x00000200
#define OBJ_OPENIF              0x00000080
#define OBJ_PERMANENT           0x00000010
#define OBJ_EXCLUSIVE           0x00000020
#define OBJ_INHERIT             0x00000002

/* ---- Security SID authorities ------------------------------------------- */

#define SECURITY_WORLD_SID_AUTHORITY {{0,0,0,0,0,1}}
#define SECURITY_NT_AUTHORITY {0,0,0,0,0,5}
#define SECURITY_CREATOR_SID_AUTHORITY {0,0,0,0,0,3}
#define SECURITY_LOCAL_SID_AUTHORITY {0,0,0,0,0,2}

/* ---- Security RIDs ------------------------------------------------------ */

#define SECURITY_WORLD_RID 0
#define SECURITY_LOCAL_SYSTEM_RID 0x00000012
#define SECURITY_CREATOR_OWNER_RID 0
#define SECURITY_CREATOR_GROUP_RID 1
#define SECURITY_BUILTIN_DOMAIN_RID 0x00000020
#define DOMAIN_ALIAS_RID_ADMINS 0x00000220
#define SECURITY_RESTRICTED_CODE_RID 0x00000021

/* ---- ANSI_STRING -------------------------------------------------------- */

typedef struct _ANSI_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PCHAR Buffer;
} ANSI_STRING, *PANSI_STRING;
typedef const ANSI_STRING *PCANSI_STRING;

/* ---- Null characters ---------------------------------------------------- */

#define ANSI_NULL ((CHAR)0)
#define UNICODE_NULL ((WCHAR)0)

/* ---- Max path ----------------------------------------------------------- */

#define MAX_PATH 260

/* ---- LIST_ENTRY: the load-bearing struct of all of NT ------------------ */

typedef struct _LIST_ENTRY {
    struct _LIST_ENTRY *Flink;
    struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;

FORCEINLINE VOID InitializeListHead(PLIST_ENTRY Head)
{
    Head->Flink = Head->Blink = Head;
}

FORCEINLINE BOOLEAN IsListEmpty(const LIST_ENTRY *Head)
{
    return (BOOLEAN)(Head->Flink == Head);
}

FORCEINLINE VOID InsertTailList(PLIST_ENTRY Head, PLIST_ENTRY Entry)
{
    PLIST_ENTRY Blink = Head->Blink;
    Entry->Flink = Head;
    Entry->Blink = Blink;
    Blink->Flink = Entry;
    Head->Blink  = Entry;
}

FORCEINLINE VOID InsertHeadList(PLIST_ENTRY Head, PLIST_ENTRY Entry)
{
    PLIST_ENTRY Flink = Head->Flink;
    Entry->Flink = Flink;
    Entry->Blink = Head;
    Flink->Blink = Entry;
    Head->Flink  = Entry;
}

FORCEINLINE BOOLEAN RemoveEntryList(PLIST_ENTRY Entry)
{
    PLIST_ENTRY Flink = Entry->Flink;
    PLIST_ENTRY Blink = Entry->Blink;
    Blink->Flink = Flink;
    Flink->Blink = Blink;
    return (BOOLEAN)(Flink == Blink);
}

FORCEINLINE PLIST_ENTRY RemoveHeadList(PLIST_ENTRY Head)
{
    PLIST_ENTRY Entry = Head->Flink;
    RemoveEntryList(Entry);
    return Entry;
}

#define CONTAINING_RECORD(Address, Type, Field) \
    ((Type *)((PCHAR)(Address) - offsetof(Type, Field)))

/* ---- UNICODE_STRING ---------------------------------------------------- */

typedef struct _UNICODE_STRING {
    USHORT Length;          /* bytes, not chars */
    USHORT MaximumLength;   /* bytes */
    PWSTR  Buffer;
} UNICODE_STRING, *PUNICODE_STRING;
typedef const UNICODE_STRING *PCUNICODE_STRING;

/* Static UTF-16 literal helper (GCC: u"..." is char16_t == our WCHAR) */
#define RTL_CONSTANT_STRING(s) \
    { sizeof(s) - sizeof((s)[0]), sizeof(s), (PWSTR)(s) }

/* ---- IRQL levels (amd64) ------------------------------------------------ */

#define PASSIVE_LEVEL    0
#define APC_LEVEL        1
#define DISPATCH_LEVEL   2
#define CMCI_LEVEL       5
#define CLOCK_LEVEL     13
#define IPI_LEVEL       14
#define POWER_LEVEL     14
#define PROFILE_LEVEL   15
#define HIGH_LEVEL      15

/* ---- Bugcheck codes (the real ones) ------------------------------------ */

#define IRQL_NOT_LESS_OR_EQUAL           0x0000000A
#define KMODE_EXCEPTION_NOT_HANDLED      0x0000001E
#define PAGE_FAULT_IN_NONPAGED_AREA      0x00000050
#define PHASE0_INITIALIZATION_FAILED     0x00000031
#define PHASE1_INITIALIZATION_FAILED     0x00000032
#define UNEXPECTED_KERNEL_MODE_TRAP      0x0000007F
#define SYSTEM_THREAD_EXCEPTION_NOT_HANDLED 0x0000007E
#define BAD_POOL_HEADER                  0x00000019
#define MANUALLY_INITIATED_CRASH         0x000000E2

/* ---- Wait types -------------------------------------------------------- */

typedef enum _KWAIT_REASON {
    Executive = 0,
    UserRequest = 2,
} KWAIT_REASON;

typedef enum _KPROCESSOR_MODE {
    KernelMode = 0,
    UserMode = 1,
} KPROCESSOR_MODE;

typedef enum _WAIT_TYPE {
    WaitAll = 0,
    WaitAny = 1,
} WAIT_TYPE;

typedef struct _LARGE_INTEGER {
    union {
        struct { ULONG LowPart; LONG HighPart; };
        LONG64 QuadPart;
    };
} LARGE_INTEGER, *PLARGE_INTEGER;

/* ---- Processor architecture ---------------------------------------------- */


/* ---- Image ---------------------------------------------------------------- */

#define IMAGE_FILE_DLL 0x2000
#define SEC_IMAGE 0x1000000
#define STATUS_INVALID_IMPORT_OF_NON_DLL ((NTSTATUS)0xC000007BL)

/* ---- File information classes -------------------------------------------- */

typedef enum _FILE_INFORMATION_CLASS {
    FileDirectoryInformation = 1,
    FileFullDirectoryInformation = 2,
    FileBothDirectoryInformation = 3,
    FileBasicInformation = 4,
    FileStandardInformation = 5,
    FileInternalInformation = 6,
    FileEaInformation = 7,
    FileAccessInformation = 8,
    FileNameInformation = 9,
    FileRenameInformation = 10,
    FileLinkInformation = 11,
    FileDispositionInformation = 13,
    FilePositionInformation = 14,
} FILE_INFORMATION_CLASS;

typedef enum _FILE_FS_INFORMATION_CLASS {
    FileFsVolumeInformation = 1,
    FileFsLabelInformation = 2,
    FileFsSizeInformation = 3,
    FileFsDeviceInformation = 4,
    FileFsAttributeInformation = 5,
    FileFsControlInformation = 6,
    FileFsFullSizeInformation = 7,
    FileFsObjectIdInformation = 8,
    FileFsDriverPathInformation = 9,
    FileFsVolumeFlagsInformation = 10,
    FileFsSectorSizeInformation = 11,
    FileFsDataCopyInformation = 12,
    FileFsMetadataSizeInformation = 13,
    FileFsFullSizeInformationEx = 14,
    FileFsMaximumInformation = 15,
} FILE_FS_INFORMATION_CLASS;

typedef struct _FILE_BASIC_INFORMATION {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    ULONG FileAttributes;
} FILE_BASIC_INFORMATION, *PFILE_BASIC_INFORMATION;

typedef struct _FILE_DISPOSITION_INFORMATION {
    BOOLEAN DeleteFile;
} FILE_DISPOSITION_INFORMATION, *PFILE_DISPOSITION_INFORMATION;



/* ---- System basic info --------------------------------------------------- */

typedef struct _SYSTEM_BASIC_INFORMATION {
    ULONG Reserved;
    ULONG TimerResolution;
    ULONG PageSize;
    ULONG NumberOfPhysicalPages;
    ULONG LowestPhysicalPageNumber;
    ULONG HighestPhysicalPageNumber;
    ULONG AllocationGranularity;
    ULONG_PTR MinimumUserModeAddress;
    ULONG_PTR MaximumUserModeAddress;
    ULONG_PTR ActiveProcessorsAffinityMask;
    CCHAR NumberOfProcessors;
} SYSTEM_BASIC_INFORMATION, *PSYSTEM_BASIC_INFORMATION;

typedef struct _SYSTEM_PROCESSOR_INFORMATION {
    USHORT ProcessorArchitecture;
    USHORT ProcessorLevel;
    USHORT ProcessorRevision;
    USHORT Reserved;
    ULONG ProcessorFeatureBits;
} SYSTEM_PROCESSOR_INFORMATION, *PSYSTEM_PROCESSOR_INFORMATION;

#define SystemBasicInformation 0
#define SystemProcessorInformation 1

/* ---- Privilege ----------------------------------------------------------- */

#define SE_BACKUP_PRIVILEGE     14
#define SE_RESTORE_PRIVILEGE    15
#define SE_DEBUG_PRIVILEGE      20

/* ---- Access mask --------------------------------------------------------- */

#define DELETE                  0x00010000L
#define READ_CONTROL            0x00020000L
#define WRITE_DAC               0x00040000L
#define WRITE_OWNER             0x00080000L
#define SYNCHRONIZE             0x00100000L
#define GENERIC_ALL             0x10000000L
#define GENERIC_EXECUTE         0x20000000L
#define GENERIC_WRITE           0x40000000L
#define GENERIC_READ            0x80000000L

/* ---- File rename --------------------------------------------------------- */

typedef struct _FILE_RENAME_INFORMATION {
    BOOLEAN ReplaceIfExists;
    HANDLE RootDirectory;
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_RENAME_INFORMATION, *PFILE_RENAME_INFORMATION;

typedef struct _FILE_RENAME_INFORMATION_EX {
    ULONG Flags;
    HANDLE RootDirectory;
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_RENAME_INFORMATION_EX, *PFILE_RENAME_INFORMATION_EX;

/* ---- Token information --------------------------------------------------- */

typedef enum _TOKEN_INFORMATION_CLASS {
    TokenUser = 1,
    TokenGroups,
    TokenPrivileges,
    TokenOwner,
    TokenPrimaryGroup,
    TokenDefaultDacl,
    TokenSource,
    TokenType,
    TokenImpersonationLevel,
    TokenStatistics,
    TokenRestrictedSids,
    TokenSessionId,
    TokenGroupsAndPrivileges,
    TokenSessionReference,
    TokenSandBoxInert,
    TokenAuditPolicy,
    TokenOrigin,
    TokenElevationType,
    TokenLinkedToken,
    TokenElevation,
    TokenHasRestrictions,
    TokenAccessInformation,
    TokenVirtualizationAllowed,
    TokenVirtualizationEnabled,
    TokenIntegrityLevel,
    TokenUIAccess,
    TokenMandatoryPolicy,
    TokenLogonSid,
    MaxTokenInfoClass
} TOKEN_INFORMATION_CLASS;

/* ---- System -------------------------------------------------------------- */

#define PROCESSOR_ARCHITECTURE_INTEL   0
#define PROCESSOR_ARCHITECTURE_AMD64   9
#define PROCESSOR_ARCHITECTURE_IA64    6

#define SE_SHUTDOWN_PRIVILEGE      19
#define STATUS_NO_TOKEN            ((NTSTATUS)0xC000007CL)
#define STATUS_PORT_DISCONNECTED            ((NTSTATUS)0xC000013BL)
#define STATUS_SYSTEM_PROCESS_TERMINATED ((NTSTATUS)0xC000014AL)
#define IMAGE_SUBSYSTEM_NATIVE     1

/* ---- Exception handling -------------------------------------------------- */

/* Forward declarations for exception handling */
typedef struct _EXCEPTION_POINTERS EXCEPTION_POINTERS, *PEXCEPTION_POINTERS;

typedef LONG (*PEXCEPTION_FILTER)(PEXCEPTION_POINTERS);
typedef void (*PEXCEPTION_HANDLER)(PEXCEPTION_POINTERS);


/* ---- Token / LUID / Privileges ------------------------------------------ */
typedef struct _LUID {
    ULONG LowPart;
    LONG HighPart;
} LUID, *PLUID;

typedef struct _LUID_AND_ATTRIBUTES {
    LUID Luid;
    ULONG Attributes;
} LUID_AND_ATTRIBUTES, *PLUID_AND_ATTRIBUTES;

typedef struct _TOKEN_PRIVILEGES {
    ULONG PrivilegeCount;
    LUID_AND_ATTRIBUTES Privileges[1];
} TOKEN_PRIVILEGES, *PTOKEN_PRIVILEGES;

#define SE_PRIVILEGE_ENABLED  0x00000002L
#define SE_PRIVILEGE_ENABLED_BY_DEFAULT 0x00000001L

#define STATUS_NOT_ALL_ASSIGNED      ((NTSTATUS)0xC000010CL)
#define STATUS_PRIVILEGE_NOT_HELD    ((NTSTATUS)0xC0000061L)
#define STATUS_REPARSE               ((NTSTATUS)0xC0000500L)
#define STATUS_CANCELLED             ((NTSTATUS)0xC0000120L)
#define STATUS_BUFFER_TOO_SMALL      ((NTSTATUS)0xC0000023L)
#define STATUS_REMOTE_NOT_LISTENING  ((NTSTATUS)0xC00000BCL)
#define STATUS_NO_SUCH_DEVICE        ((NTSTATUS)0xC00000E0L)
#define STATUS_OBJECT_NAME_COLLISION ((NTSTATUS)0xC0000035L)
#define STATUS_DEVICE_NOT_CONNECTED ((NTSTATUS)0xC000009DL)
#define STATUS_DISK_QUOTA_EXCEEDED  ((NTSTATUS)0xC0000042L)
#define STATUS_ACCESS_VIOLATION     ((NTSTATUS)0xC0000005L)

#define SharedUserData ((PVOID)0x7FFE0000)

typedef ULONG SECURITY_INFORMATION;
typedef struct _KUSER_SHARED_DATA { WCHAR NtSystemRoot[260]; ULONG TickCountMultiplier; } KUSER_SHARED_DATA, *PKUSER_SHARED_DATA;
typedef int BOOL;
typedef PVOID HINSTANCE;
typedef PVOID LPVOID;
/* SAL annotations */
#define _In_
#define _Out_
#define _In_opt_
#define _Out_opt_
#define _Inout_
#define _Success(x)
#define _When(x,y)
typedef ULONG SECURITY_IMPERSONATION_LEVEL;

/* ---- File FS information types ------------------------------------------ */
typedef struct _FILE_FS_DEVICE_INFORMATION {
    ULONG DeviceType;
    ULONG Characteristics;
} FILE_FS_DEVICE_INFORMATION, *PFILE_FS_DEVICE_INFORMATION;

typedef struct _FILE_STANDARD_INFORMATION {
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG NumberOfLinks;
    BOOLEAN DeletePending;
    BOOLEAN Directory;
} FILE_STANDARD_INFORMATION, *PFILE_STANDARD_INFORMATION;

typedef struct _FILE_FS_SIZE_INFORMATION {
    LARGE_INTEGER TotalAllocationUnits;
    LARGE_INTEGER AvailableAllocationUnits;
    ULONG SectorsPerAllocationUnit;
    ULONG BytesPerSector;
} FILE_FS_SIZE_INFORMATION, *PFILE_FS_SIZE_INFORMATION;

typedef struct _FILE_FS_VOLUME_INFORMATION {
    LARGE_INTEGER VolumeCreationTime;
    ULONG VolumeSerialNumber;
    ULONG VolumeLabelLength;
    BOOLEAN SupportsObjects;
    WCHAR VolumeLabel[1];
} FILE_FS_VOLUME_INFORMATION, *PFILE_FS_VOLUME_INFORMATION;

typedef struct _FILE_POSITION_INFORMATION {
    LARGE_INTEGER CurrentByteOffset;
} FILE_POSITION_INFORMATION, *PFILE_POSITION_INFORMATION;

#define STATUS_TOO_MANY_PAGING_FILES ((NTSTATUS)0xC000017EL)

#define STATUS_DISK_FULL ((NTSTATUS)0xC000017FL)

#define FILE_CREATED                     0x00000001
#define FILE_OPENED                      0x00000000
#define FILE_OVERWRITTEN                 0x00000002
#define FILE_SUPERSEDED                  0x00000003

typedef struct _PROCESS_DEVICEMAP_INFORMATION {
    union {
        struct {
            HANDLE DirectoryHandle;
        } Set;
        struct {
            ULONG DriveMap;
            UNICODE_STRING DriveName[32];
        } Query;
    };
} PROCESS_DEVICEMAP_INFORMATION, *PPROCESS_DEVICEMAP_INFORMATION;

#define IsNEC_98 0

#define FILE_FLOPPY_DISKETTE 0x00000004
#define FILE_READ_ONLY_DEVICE 0x00000002
#define FILE_REMOTE_DEVICE 0x00000010
#define FILE_REMOVABLE_MEDIA 0x00000001
#define STATUS_UNEXPECTED_IO_ERROR ((NTSTATUS)0xC00000E9L)
#define STATUS_NO_SUCH_PROCESS ((NTSTATUS)0xC000010EL)

#define FILE_DEVICE_DISK 0x00000007

typedef const char* PCSZ;
#endif /* _NTDEF_H_ */