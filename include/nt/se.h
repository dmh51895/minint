/*
 * MinNT - se.h
 * Security subsystem: stub tokens, SIDs, ACLs, access check.
 * Minimal NT 6.x security — enough for Winlogon/CSRSS to create
 * security descriptors without crashing.
 */

#ifndef _SE_H_
#define _SE_H_

#include <nt/ntdef.h>

/* ---- Access mask --------------------------------------------------------- */

typedef ULONG ACCESS_MASK;
typedef ACCESS_MASK *PACCESS_MASK;

#define GENERIC_READ     0x80000000
#define GENERIC_WRITE    0x40000000
#define GENERIC_EXECUTE  0x20000000
#define GENERIC_ALL      0x10000000

typedef struct _GENERIC_MAPPING {
    ACCESS_MASK GenericRead;
    ACCESS_MASK GenericWrite;
    ACCESS_MASK GenericExecute;
    ACCESS_MASK GenericAll;
} GENERIC_MAPPING, *PGENERIC_MAPPING;

/* ---- SID ----------------------------------------------------------------- */

#define SID_REVISION                    1
#define SID_MAX_SUB_AUTHORITIES         15
#define SECURITY_WORLD_SID_AUTHORITY    {0,0,0,0,0,1}
#define SECURITY_LOCAL_SID_AUTHORITY    {0,0,0,0,0,2}
#define SECURITY_NULL_SID_AUTHORITY     {0,0,0,0,0,0}

typedef struct _SID_IDENTIFIER_AUTHORITY {
    UCHAR Value[6];
} SID_IDENTIFIER_AUTHORITY, *PSID_IDENTIFIER_AUTHORITY;

typedef struct _SID {
    UCHAR Revision;
    UCHAR SubAuthorityCount;
    SID_IDENTIFIER_AUTHORITY IdentifierAuthority;
    ULONG SubAuthority[SID_MAX_SUB_AUTHORITIES];
} SID, *PSID;

/* ---- Token --------------------------------------------------------------- */

typedef struct _TOKEN {
    ULONG           TokenId;
    ULONG           AuthenticationId;
    PSID            UserSid;
    PSID            GroupSid;
    ULONG           Privileges;
    BOOLEAN         Elevated;
} TOKEN, *PTOKEN;

/* ---- Security Descriptor ------------------------------------------------- */

#define SECURITY_DESCRIPTOR_MIN_LENGTH  40

typedef struct _SECURITY_DESCRIPTOR {
    UCHAR           Revision;
    UCHAR           Sbz1;
    USHORT          Control;
    PVOID           Owner;
    PVOID           Group;
    PVOID           Sacl;
    PVOID           Dacl;
} SECURITY_DESCRIPTOR, *PISECURITY_DESCRIPTOR;

/* ---- ACL ----------------------------------------------------------------- */

typedef struct _ACL {
    UCHAR AclRevision;
    UCHAR Sbz1;
    USHORT AclSize;
    USHORT AceCount;
    USHORT Sbz2;
} ACL, *PACL;

/* ---- ACE ----------------------------------------------------------------- */

typedef struct _ACE_HEADER {
    UCHAR AceType;
    UCHAR AceFlags;
    USHORT AceSize;
} ACE_HEADER, *PACE_HEADER;

typedef struct _ACCESS_ALLOWED_ACE {
    ACE_HEADER Header;
    ACCESS_MASK Mask;
    ULONG SidStart;
} ACCESS_ALLOWED_ACE, *PACCESS_ALLOWED_ACE;

#define FILE_GENERIC_READ    0x00120089
#define FILE_GENERIC_WRITE   0x00120116
#define FILE_GENERIC_EXECUTE 0x001200A0
#define FILE_ALL_ACCESS      0x001F01FF

/* ---- API ----------------------------------------------------------------- */

NTSTATUS NTAPI SeInitSystem(VOID);

BOOLEAN NTAPI SeAccessCheck(PISECURITY_DESCRIPTOR SecurityDescriptor,
                            PSID SubjectSecurityContext,
                            BOOLEAN SubjectContextLocked,
                            ACCESS_MASK DesiredAccess,
                            ACCESS_MASK PreviouslyGranted,
                            PACCESS_MASK AccessStatus,
                            PGENERIC_MAPPING GenericMapping);

NTSTATUS NTAPI SeCreateSecurityDescriptor(PISECURITY_DESCRIPTOR *OutSecurityDescriptor);

NTSTATUS NTAPI SeCreateToken(PTOKEN *OutToken);

PSID    NTAPI SeCreateWorldSid(VOID);
PSID    NTAPI SeCreateLocalSid(VOID);

#endif /* _SE_H_ */
