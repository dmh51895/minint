/*
 * MinNT - ndk/setypes.h
 * Security types for NDK compatibility.
 * Uses MinNT's existing types from nt/se.h, adds NDK-specific extras.
 */

#ifndef _NDK_SETYPES_H_
#define _NDK_SETYPES_H_

#include <nt/ntdef.h>
#include <nt/se.h>

/* ---- NDK-specific security constants ----------------------------------- */

#ifndef PROCESS_VM_READ
#define PROCESS_VM_READ                0x0010
#define PROCESS_VM_WRITE               0x0020
#define PROCESS_VM_OPERATION           0x0008
#define PROCESS_DUP_HANDLE             0x0040
#define PROCESS_TERMINATE              0x0001
#define PROCESS_SUSPEND_RESUME         0x0800
#define PROCESS_QUERY_INFORMATION      0x0400
#define PROCESS_ALL_ACCESS             0x001FFFFF
#endif

#ifndef SECTION_ALL_ACCESS
#define SECTION_ALL_ACCESS             0x001F0FFF
#endif

#ifndef DIRECTORY_ALL_ACCESS
#define DIRECTORY_ALL_ACCESS           0x000F001F
#endif

#ifndef SYMBOLIC_LINK_ALL_ACCESS
#define SYMBOLIC_LINK_ALL_ACCESS       0x001F0001
#endif

#ifndef PORT_ALL_ACCESS
#define PORT_ALL_ACCESS                0x001F0000
#endif

#ifndef TOKEN_QUERY
#define TOKEN_QUERY                    0x0008
#endif

#ifndef KEY_READ
#define KEY_READ                       0x00020019
#endif

#ifndef KEY_ALL_ACCESS
#define KEY_ALL_ACCESS                 0x000F003F
#endif

#define ACL_REVISION                   2
#define ACL_REVISION2                  2

#define OBJECT_INHERIT_ACE             0x01
#define CONTAINER_INHERIT_ACE          0x02
#define INHERIT_ONLY_ACE               0x08

#define SE_DACL_DEFAULTED              0x0004
#define SE_DACL_PRESENT                0x0004
#define SE_SECURITY_DESCRIPTOR         0x0100

/* ---- Object attributes flags -------------------------------------------- */

#ifndef OBJ_CASE_INSENSITIVE
#define OBJ_CASE_INSENSITIVE           0x00000040
#endif
#ifndef OBJ_OPENIF
#define OBJ_OPENIF                     0x00000020
#endif
#ifndef OBJ_PERMANENT
#define OBJ_PERMANENT                  0x00000010
#endif

/* ---- Security RIDs ------------------------------------------------------ */

#ifndef SECURITY_WORLD_SID_AUTHORITY
#define SECURITY_WORLD_SID_AUTHORITY   {0,0,0,0,0,1}
#endif
#ifndef SECURITY_WORLD_RID
#define SECURITY_WORLD_RID             0x00000000
#endif

#ifndef SECURITY_NT_AUTHORITY
#define SECURITY_NT_AUTHORITY          {0,0,0,0,0,5}
#endif
#ifndef SECURITY_LOCAL_SYSTEM_RID
#define SECURITY_LOCAL_SYSTEM_RID      0x00000012
#endif

#ifndef SECURITY_DESCRIPTOR_REVISION
#define SECURITY_DESCRIPTOR_REVISION   1
#endif

#ifndef SECURITY_DESCRIPTOR_MIN_LENGTH
#define SECURITY_DESCRIPTOR_MIN_LENGTH 40
#endif

#ifndef HEAP_ZERO_MEMORY
#define HEAP_ZERO_MEMORY               0x00000008
#endif

#ifndef REG_SZ
#define REG_SZ                         1
#endif
#ifndef REG_EXPAND_SZ
#define REG_EXPAND_SZ                  2
#endif
#ifndef REG_BINARY
#define REG_BINARY                     3
#endif
#ifndef REG_DWORD
#define REG_DWORD                      4
#endif
#ifndef REG_MULTI_SZ
#define REG_MULTI_SZ                   7
#endif

/* ---- Read control ------------------------------------------------------- */

#ifndef READ_CONTROL
#define READ_CONTROL                   0x00020000
#endif

/* ---- SECURITY_DESCRIPTOR_CONTROL ---------------------------------------- */

#ifndef _SECURITY_DESCRIPTOR_CONTROL_DEFINED
#define _SECURITY_DESCRIPTOR_CONTROL_DEFINED
typedef USHORT SECURITY_DESCRIPTOR_CONTROL;
typedef SECURITY_DESCRIPTOR_CONTROL *PSECURITY_DESCRIPTOR_CONTROL;
#endif

/* ---- PSECURITY_DESCRIPTOR (alias for PISECURITY_DESCRIPTOR) ------------ */

#ifndef _PSECURITY_DESCRIPTOR_DEFINED
#define _PSECURITY_DESCRIPTOR_DEFINED
typedef PISECURITY_DESCRIPTOR PSECURITY_DESCRIPTOR;
#endif

/* ---- Token -------------------------------------------------------------- */

typedef struct _TOKEN_USER {
    struct {
        PSID Sid;
    } User;
} TOKEN_USER, *PTOKEN_USER;

/* ---- Hard error / option ------------------------------------------------ */

#define OptionShutdownSystem          0x00000001
#define OptionOk                      0x00000000

/* ---- RTL SID helpers ---------------------------------------------------- */

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
);

ULONG NTAPI RtlLengthSid(PSID Sid);
VOID NTAPI RtlFreeSid(PSID Sid);

NTSTATUS NTAPI
RtlCreateSecurityDescriptor(
    PSECURITY_DESCRIPTOR SecurityDescriptor,
    ULONG Revision
);

NTSTATUS NTAPI
RtlSetDaclSecurityDescriptor(
    PSECURITY_DESCRIPTOR SecurityDescriptor,
    BOOLEAN DaclPresent,
    PACL Dacl,
    BOOLEAN DaclDefaulted
);

NTSTATUS NTAPI
RtlGetDaclSecurityDescriptor(
    PSECURITY_DESCRIPTOR SecurityDescriptor,
    PBOOLEAN DaclPresent,
    PACL *Dacl,
    PBOOLEAN DaclDefaulted
);

NTSTATUS NTAPI
RtlCreateAcl(
    PACL Acl,
    ULONG AclLength,
    ULONG AclRevision
);

NTSTATUS NTAPI
RtlAddAccessAllowedAce(
    PACL Acl,
    ULONG AceRevision,
    ACCESS_MASK AccessMask,
    PSID Sid
);

NTSTATUS NTAPI
RtlGetAce(
    PACL Acl,
    ULONG AceIndex,
    PVOID *Ace
);

#endif /* _NDK_SETYPES_H_ */
