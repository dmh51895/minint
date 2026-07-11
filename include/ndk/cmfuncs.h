/*
 * MinNT - ndk/cmfuncs.h
 * Configuration Manager (registry) functions.
 */

#ifndef _NDK_CMFUNCS_H_
#define _NDK_CMFUNCS_H_

#include <nt/ntdef.h>
#include <nt/se.h>
#include <ndk/setypes.h>
#include <ndk/obfuncs.h>

#define KEY_READ          0x20019
#define KEY_WRITE         0x20006
#define KEY_ALL_ACCESS    0xF003F

#define REG_NONE                    0
#define REG_SZ                      1
#define REG_EXPAND_SZ               2
#define REG_BINARY                  3
#define REG_DWORD                   4
#define REG_MULTI_SZ                7
#define REG_QWORD                   11

#define KeyValuePartialInformation   2

typedef struct _KEY_VALUE_PARTIAL_INFORMATION {
    ULONG TitleIndex;
    ULONG Type;
    ULONG DataLength;
    UCHAR Data[1];
} KEY_VALUE_PARTIAL_INFORMATION, *PKEY_VALUE_PARTIAL_INFORMATION;

NTSTATUS NTAPI NtOpenKey(
    PHANDLE KeyHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes
);

NTSTATUS NTAPI NtQueryValueKey(
    HANDLE KeyHandle,
    PUNICODE_STRING ValueName,
    ULONG KeyValueInformationClass,
    PVOID KeyValueInformation,
    ULONG Length,
    PULONG ResultLength
);

NTSTATUS NTAPI NtSetValueKey(
    HANDLE KeyHandle,
    PUNICODE_STRING ValueName,
    ULONG TitleIndex,
    ULONG Type,
    PVOID Data,
    ULONG DataSize
);

NTSTATUS NTAPI NtEnumerateKey(
    HANDLE KeyHandle,
    ULONG Index,
    ULONG KeyInformationClass,
    PVOID KeyInformation,
    ULONG Length,
    PULONG ResultLength
);

#endif /* _NDK_CMFUNCS_H_ */
