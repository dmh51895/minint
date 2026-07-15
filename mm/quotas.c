/*
 * MinNT - mm/quotas.c
 * Per-user disk quota management.
 *
 * Quotas are tracked per (volume, user) pair. Each quota record holds
 * the current usage, the warning threshold, and the hard limit. When a
 * write would push usage above the hard limit the volume manager is
 * expected to return STATUS_DISK_QUOTA_EXCEEDED.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/framework.h>

#define QUOTA_MAX_ENTRIES  64

typedef struct _QUOTA_ENTRY {
    CHAR Volume;
    CHAR User[32];
    ULONG64 Used;
    ULONG64 WarningThreshold;
    ULONG64 HardLimit;
    BOOLEAN InUse;
} QUOTA_ENTRY, *PQUOTA_ENTRY;

static QUOTA_ENTRY g_Entries[QUOTA_MAX_ENTRIES];

static QUOTA_ENTRY *QuotaFind(CHAR Volume, const CHAR *User)
{
    for (ULONG i = 0; i < QUOTA_MAX_ENTRIES; i++) {
        if (!g_Entries[i].InUse) continue;
        if (g_Entries[i].Volume != Volume) continue;
        BOOLEAN eq = TRUE;
        for (ULONG k = 0; k < 32; k++) {
            if (g_Entries[i].User[k] != User[k]) { eq = FALSE; break; }
            if (User[k] == 0) break;
        }
        if (eq) return &g_Entries[i];
    }
    return NULL;
}

NTSTATUS NTAPI QuotaSet(CHAR Volume, const CHAR *User,
                        ULONG64 Warning, ULONG64 HardLimit)
{
    QUOTA_ENTRY *e = QuotaFind(Volume, User);
    if (!e) {
        for (ULONG i = 0; i < QUOTA_MAX_ENTRIES; i++) {
            if (!g_Entries[i].InUse) {
                g_Entries[i].InUse = TRUE;
                g_Entries[i].Volume = Volume;
                for (ULONG k = 0; k < 32; k++) {
                    g_Entries[i].User[k] = User[k];
                    if (User[k] == 0) break;
                }
                e = &g_Entries[i];
                break;
            }
        }
    }
    if (!e) return STATUS_NO_MEMORY;
    e->WarningThreshold = Warning;
    e->HardLimit = HardLimit;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI QuotaCharge(CHAR Volume, const CHAR *User, ULONG64 Bytes)
{
    QUOTA_ENTRY *e = QuotaFind(Volume, User);
    if (!e) return STATUS_SUCCESS; /* no quota => allowed */
    if (e->HardLimit && e->Used + Bytes > e->HardLimit) {
        return 0xC0000042L; /* STATUS_DISK_QUOTA_EXCEEDED */
    }
    e->Used += Bytes;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI QuotaReturn(CHAR Volume, const CHAR *User, ULONG64 Bytes)
{
    QUOTA_ENTRY *e = QuotaFind(Volume, User);
    if (!e) return STATUS_NOT_FOUND;
    if (Bytes > e->Used) e->Used = 0;
    else e->Used -= Bytes;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI QuotaQuery(CHAR Volume, const CHAR *User,
                          ULONG64 *Used, ULONG64 *Warning, ULONG64 *Hard)
{
    QUOTA_ENTRY *e = QuotaFind(Volume, User);
    if (!e) return STATUS_NOT_FOUND;
    if (Used) *Used = e->Used;
    if (Warning) *Warning = e->WarningThreshold;
    if (Hard) *Hard = e->HardLimit;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI QuotaInit(VOID)
{
    RtlZeroMemory(g_Entries, sizeof(g_Entries));
    /* Default policy: 80% warning, 100% hard limit at 1GB per user. */
    QuotaSet('C', "default", 800ULL * 1024 * 1024, 1024ULL * 1024 * 1024);
    DbgPrint("QUOTA: per-user disk quota initialized\n");
    return STATUS_SUCCESS;
}
