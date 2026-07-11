/*
 * MinNT - se/se.c
 * Security subsystem: tokens, SIDs, ACLs, access check.
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/se.h>
#include <nt/rtl.h>

static ULONG SeNextTokenId = 1;
static TOKEN SeNullToken;

/* ---- Init --------------------------------------------------------------- */

NTSTATUS NTAPI SeInitSystem(VOID)
{
    RtlZeroMemory(&SeNullToken, sizeof(TOKEN));
    SeNullToken.TokenId = SeNextTokenId++;
    SeNullToken.Elevated = TRUE;
    SeNullToken.AuthenticationId = 0;
    SeNullToken.UserSid = SeCreateWorldSid();
    SeNullToken.GroupSid = SeCreateWorldSid();
    SeNullToken.Privileges = 0;

    DbgPrint("SE: security subsystem initialized\n");
    return STATUS_SUCCESS;
}

/* ---- SID comparison helper ----------------------------------------------- */

static BOOLEAN SeSidEqual(PSID Sid1, PSID Sid2)
{
    if (Sid1 == NULL && Sid2 == NULL) return TRUE;
    if (Sid1 == NULL || Sid2 == NULL) return FALSE;

    if (Sid1->Revision != Sid2->Revision) return FALSE;
    if (Sid1->SubAuthorityCount != Sid2->SubAuthorityCount) return FALSE;

    for (int j = 0; j < 6; j++) {
        if (Sid1->IdentifierAuthority.Value[j] != Sid2->IdentifierAuthority.Value[j])
            return FALSE;
    }

    for (UCHAR i = 0; i < Sid1->SubAuthorityCount; i++) {
        if (Sid1->SubAuthority[i] != Sid2->SubAuthority[i]) return FALSE;
    }
    return TRUE;
}

/* ---- Access check -------------------------------------------------------- */

BOOLEAN NTAPI SeAccessCheck(PISECURITY_DESCRIPTOR SecurityDescriptor,
                             PSID SubjectSecurityContext,
                             BOOLEAN SubjectContextLocked,
                             ACCESS_MASK DesiredAccess,
                             ACCESS_MASK PreviouslyGranted,
                             PACCESS_MASK AccessStatus,
                             PGENERIC_MAPPING GenericMapping)
{
    UNREFERENCED_PARAMETER(SubjectContextLocked);
    UNREFERENCED_PARAMETER(GenericMapping);

    if (SecurityDescriptor == NULL) {
        if (AccessStatus)
            *AccessStatus = DesiredAccess | PreviouslyGranted;
        return TRUE;
    }

    PACL Dacl = (PACL)SecurityDescriptor->Dacl;
    if (Dacl == NULL || !SecurityDescriptor->Dacl) {
        if (AccessStatus)
            *AccessStatus = DesiredAccess | PreviouslyGranted;
        return TRUE;
    }

    ACCESS_MASK granted = 0;
    BOOLEAN foundAllow = FALSE;

    unsigned char *pAce = (unsigned char *)Dacl + sizeof(ACL);
    for (USHORT i = 0; i < Dacl->AceCount; i++) {
        if (pAce + sizeof(ACE_HEADER) > (unsigned char *)Dacl + Dacl->AclSize) break;
        PACE_HEADER ace = (PACE_HEADER)pAce;

        if (ace->AceType == 0x00) {
            PACCESS_ALLOWED_ACE allowedAce = (PACCESS_ALLOWED_ACE)ace;
            PSID aceSid = (PSID)&allowedAce->SidStart;

            if (SeSidEqual(SubjectSecurityContext, aceSid)) {
                ACCESS_MASK mask = allowedAce->Mask;
                if (GenericMapping) {
                    if (DesiredAccess & GENERIC_READ)
                        mask &= ~GenericMapping->GenericRead;
                    if (DesiredAccess & GENERIC_WRITE)
                        mask &= ~GenericMapping->GenericWrite;
                    if (DesiredAccess & GENERIC_EXECUTE)
                        mask &= ~GenericMapping->GenericExecute;
                    if (DesiredAccess & GENERIC_ALL)
                        mask &= ~GenericMapping->GenericAll;
                }
                if ((DesiredAccess & mask) == DesiredAccess) {
                    granted |= mask;
                    foundAllow = TRUE;
                }
            }
        } else if (ace->AceType == 0x01) {
            PACCESS_ALLOWED_ACE deniedAce = (PACCESS_ALLOWED_ACE)ace;
            PSID aceSid = (PSID)&deniedAce->SidStart;
            if (SeSidEqual(SubjectSecurityContext, aceSid)) {
                ACCESS_MASK mask = deniedAce->Mask;
                if ((DesiredAccess & mask) == DesiredAccess) {
                    if (AccessStatus)
                        *AccessStatus = 0;
                    return FALSE;
                }
            }
        }
        pAce += ace->AceSize;
    }

    if (foundAllow) {
        if (AccessStatus)
            *AccessStatus = granted | PreviouslyGranted;
        return TRUE;
    }

    if (AccessStatus)
        *AccessStatus = 0;
    return FALSE;
}

/* ---- Create security descriptor ----------------------------------------- */

NTSTATUS NTAPI SeCreateSecurityDescriptor(PISECURITY_DESCRIPTOR *OutSecurityDescriptor)
{
    PISECURITY_DESCRIPTOR sd;

    sd = ExAllocatePoolWithTag(NonPagedPool,
                               SECURITY_DESCRIPTOR_MIN_LENGTH, TAG_PROC);
    if (!sd) return STATUS_NO_MEMORY;

    RtlZeroMemory(sd, SECURITY_DESCRIPTOR_MIN_LENGTH);
    sd->Revision = 1;
    sd->Sbz1 = 0;
    sd->Control = 0x8000;   /* SE_SELF_RELATIVE */

    *OutSecurityDescriptor = sd;
    return STATUS_SUCCESS;
}

/* ---- Create token ------------------------------------------------------- */

NTSTATUS NTAPI SeCreateToken(PTOKEN *OutToken)
{
    PTOKEN token;

    token = ExAllocatePoolWithTag(NonPagedPool, sizeof(TOKEN), TAG_PROC);
    if (!token) return STATUS_NO_MEMORY;

    RtlZeroMemory(token, sizeof(TOKEN));
    token->TokenId = SeNextTokenId++;
    token->AuthenticationId = 0;
    token->UserSid = SeCreateWorldSid();
    token->GroupSid = SeCreateWorldSid();
    token->Privileges = 0xFFFF;    /* all privileges for now */
    token->Elevated = TRUE;

    *OutToken = token;
    return STATUS_SUCCESS;
}

/* ---- Create SIDs -------------------------------------------------------- */

static PSID SeAllocateSid(ULONG SubAuthorityCount)
{
    PSID sid;
    ULONG size = sizeof(SID) - sizeof(ULONG) +
                 SubAuthorityCount * sizeof(ULONG);

    sid = ExAllocatePoolWithTag(NonPagedPool, size, TAG_PROC);
    if (!sid) return NULL;

    RtlZeroMemory(sid, size);
    sid->Revision = SID_REVISION;
    sid->SubAuthorityCount = (UCHAR)SubAuthorityCount;
    return sid;
}

PSID NTAPI SeCreateWorldSid(VOID)
{
    PSID sid = SeAllocateSid(1);
    if (!sid) return NULL;

    sid->IdentifierAuthority.Value[5] = 1; /* SECURITY_WORLD_SID_AUTHORITY */
    sid->SubAuthority[0] = 0;              /* SECURITY_WORLD_RID */
    return sid;
}

PSID NTAPI SeCreateLocalSid(VOID)
{
    PSID sid = SeAllocateSid(1);
    if (!sid) return NULL;

    sid->IdentifierAuthority.Value[5] = 2; /* SECURITY_LOCAL_SID_AUTHORITY */
    sid->SubAuthority[0] = 0;              /* SECURITY_LOCAL_RID */
    return sid;
}
