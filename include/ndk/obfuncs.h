/*
 * MinNT - ndk/obfuncs.h
 * Object manager functions: directories, symbolic links, sections.
 * Uses MinNT's existing object types from nt/ob.h.
 */

#ifndef _NDK_OBFUNCS_H_
#define _NDK_OBFUNCS_H_

#include <nt/ntdef.h>
#include <nt/ob.h>
#include <nt/io.h>
#include <ndk/setypes.h>

/* ---- DWORD (not in MinNT base) ----------------------------------------- */

#ifndef _DWORD_DEFINED
#define _DWORD_DEFINED
typedef unsigned long DWORD;
#endif

/* ---- Object attributes -------------------------------------------------- */

#ifndef _OBJECT_ATTRIBUTES_DEFINED
#define _OBJECT_ATTRIBUTES_DEFINED
typedef struct _OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;
#endif

#ifndef InitializeObjectAttributes
#define InitializeObjectAttributes(p, n, a, r, s) { \
    (p)->Length = sizeof(OBJECT_ATTRIBUTES);        \
    (p)->RootDirectory = (r);                       \
    (p)->ObjectName = (n);                          \
    (p)->Attributes = (a);                          \
    (p)->SecurityDescriptor = (s);                  \
    (p)->SecurityQualityOfService = NULL;           \
}
#endif

/* ---- Object directory --------------------------------------------------- */

NTSTATUS NTAPI NtCreateDirectoryObject(
    PHANDLE DirectoryHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes
);

NTSTATUS NTAPI NtOpenDirectoryObject(
    PHANDLE DirectoryHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes
);

typedef struct _OBJECT_DIRECTORY_INFORMATION {
    UNICODE_STRING Name;
    UNICODE_STRING TypeName;
} OBJECT_DIRECTORY_INFORMATION, *POBJECT_DIRECTORY_INFORMATION;

NTSTATUS NTAPI NtQueryDirectoryObject(
    HANDLE DirectoryHandle,
    PVOID Buffer,
    ULONG BufferLength,
    BOOLEAN ReturnSingleEntry,
    BOOLEAN RestartScan,
    PULONG Context,
    PULONG ReturnLength
);

/* ---- Symbolic link ------------------------------------------------------ */

NTSTATUS NTAPI NtCreateSymbolicLinkObject(
    PHANDLE LinkHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PUNICODE_STRING TargetName
);

NTSTATUS NTAPI NtOpenSymbolicLinkObject(
    PHANDLE LinkHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes
);

NTSTATUS NTAPI NtQuerySymbolicLinkObject(
    HANDLE LinkHandle,
    PUNICODE_STRING TargetName,
    PULONG DataLength
);

NTSTATUS NTAPI NtMakeTemporaryObject(HANDLE Handle);

/* ---- Section ------------------------------------------------------------ */

NTSTATUS NTAPI NtCreateSection(
    PHANDLE SectionHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PLARGE_INTEGER MaximumSize,
    ULONG SectionAttributes,
    ULONG ProtectionAttributes,
    HANDLE FileHandle
);

NTSTATUS NTAPI NtOpenSection(
    PHANDLE SectionHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes
);

#define SEC_BASED            0x00200000
#define SEC_RESERVE          0x00400000
#define SEC_COMMIT           0x00800000
#define SEC_NO_CHANGE        0x01000000

#define PAGE_NOACCESS        0x01
#define PAGE_READONLY        0x02
#define PAGE_READWRITE       0x04
#define PAGE_EXECUTE         0x10
#define PAGE_EXECUTE_READ    0x20
#define PAGE_EXECUTE_READWRITE 0x40

#define ViewShare            1
#define ViewUnmap            2

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
);

NTSTATUS NTAPI NtUnmapViewOfSection(
    HANDLE ProcessHandle,
    PVOID BaseAddress
);

/* ---- Close -------------------------------------------------------------- */

NTSTATUS NTAPI NtClose(HANDLE Handle);

/* ---- File I/O ----------------------------------------------------------- */

NTSTATUS NTAPI NtOpenFile(
    PHANDLE FileHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK IoStatusBlock,
    ULONG ShareAccess,
    ULONG OpenOptions
);

NTSTATUS NTAPI NtSetInformationFile(
    HANDLE FileHandle,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation,
    ULONG Length,
    ULONG FileInformationClass
);

/* ---- Loader ------------------------------------------------------------- */

NTSTATUS NTAPI LdrVerifyImageMatchesChecksum(HANDLE FileHandle, ULONG Length);

#endif /* _NDK_OBFUNCS_H_ */
