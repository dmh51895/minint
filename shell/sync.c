/*
 * MinNT - shell/sync.c
 * Offline Files / Sync Center.
 *
 * Allows the user to specify a set of network shares that should be
 * cached locally so they remain available when the network connection
 * drops. While online, writes are mirrored to the server; while
 * offline, writes are queued and replayed on reconnect.
 *
 * MinNT models this with a per-share record that holds the remote UNC
 * path, the local cache path, the dirty/clean state, a queue of
 * pending operations, and a "connected" flag that the network stack
 * flips based on its link status.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define SYNC_MAX_SHARES    16
#define SYNC_PATH_MAX      260
#define SYNC_QUEUE_MAX     64

typedef struct _SYNC_OP {
    CHAR Path[SYNC_PATH_MAX];
    UCHAR Type; /* 0 = write, 1 = delete */
    ULONG Length;
    UCHAR Data[512];
    BOOLEAN InUse;
} SYNC_OP, *PSYNC_OP;

typedef struct _SYNC_SHARE {
    ULONG Id;
    CHAR RemotePath[SYNC_PATH_MAX];
    CHAR LocalCache[SYNC_PATH_MAX];
    BOOLEAN Online;
    BOOLEAN Dirty;
    BOOLEAN InUse;
    ULONG PendingOps[SYNC_QUEUE_MAX];
    ULONG PendingCount;
} SYNC_SHARE, *PSYNC_SHARE;

static SYNC_SHARE g_Shares[SYNC_MAX_SHARES];
static SYNC_OP g_Queue[SYNC_QUEUE_MAX];

static SYNC_OP *SyncAllocOp(VOID)
{
    for (ULONG i = 0; i < SYNC_QUEUE_MAX; i++) {
        if (!g_Queue[i].InUse) {
            RtlZeroMemory(&g_Queue[i], sizeof(SYNC_OP));
            g_Queue[i].InUse = TRUE;
            return &g_Queue[i];
        }
    }
    return NULL;
}

NTSTATUS NTAPI SyncAddShare(const CHAR *RemotePath, const CHAR *LocalCache,
                            PULONG OutShareId)
{
    for (ULONG i = 0; i < SYNC_MAX_SHARES; i++) {
        if (!g_Shares[i].InUse) {
            RtlZeroMemory(&g_Shares[i], sizeof(SYNC_SHARE));
            g_Shares[i].InUse = TRUE;
            g_Shares[i].Online = TRUE;
            g_Shares[i].Id = i + 1;
            for (ULONG k = 0; k < SYNC_PATH_MAX; k++) {
                g_Shares[i].RemotePath[k] = RemotePath[k];
                if (RemotePath[k] == 0) break;
            }
            for (ULONG k = 0; k < SYNC_PATH_MAX; k++) {
                g_Shares[i].LocalCache[k] = LocalCache[k];
                if (LocalCache[k] == 0) break;
            }
            if (OutShareId) *OutShareId = g_Shares[i].Id;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI SyncQueueWrite(ULONG ShareId, const CHAR *Path,
                              PVOID Data, ULONG Length)
{
    if (ShareId == 0 || ShareId > SYNC_MAX_SHARES) return STATUS_INVALID_PARAMETER;
    PSYNC_SHARE s = &g_Shares[ShareId - 1];
    if (!s->InUse) return STATUS_NOT_FOUND;
    PSYNC_OP op = SyncAllocOp();
    if (!op) return STATUS_NO_MEMORY;
    op->Type = 0;
    op->Length = Length;
    if (Length > sizeof(op->Data)) Length = sizeof(op->Data);
    if (Data && Length) RtlCopyMemory(op->Data, Data, Length);
    for (ULONG k = 0; k < SYNC_PATH_MAX; k++) {
        op->Path[k] = Path[k];
        if (Path[k] == 0) break;
    }
    if (s->PendingCount < SYNC_QUEUE_MAX) {
        s->PendingOps[s->PendingCount++] = (ULONG)(op - g_Queue);
    }
    s->Dirty = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI SyncReconcile(ULONG ShareId)
{
    if (ShareId == 0 || ShareId > SYNC_MAX_SHARES) return STATUS_INVALID_PARAMETER;
    PSYNC_SHARE s = &g_Shares[ShareId - 1];
    if (!s->InUse) return STATUS_NOT_FOUND;
    if (!s->Online) return STATUS_DEVICE_NOT_CONNECTED;
    /* Replay pending operations. */
    for (ULONG i = 0; i < s->PendingCount; i++) {
        ULONG idx = s->PendingOps[i];
        if (idx >= SYNC_QUEUE_MAX) continue;
        PSYNC_OP op = &g_Queue[idx];
        DbgPrint("SYNC: replay %s op %u path=%s len=%u\n",
                 s->RemotePath, op->Type, op->Path, op->Length);
        op->InUse = FALSE;
    }
    s->PendingCount = 0;
    s->Dirty = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI SyncSetOnline(ULONG ShareId, BOOLEAN Online)
{
    if (ShareId == 0 || ShareId > SYNC_MAX_SHARES) return STATUS_INVALID_PARAMETER;
    PSYNC_SHARE s = &g_Shares[ShareId - 1];
    if (!s->InUse) return STATUS_NOT_FOUND;
    BOOLEAN was = s->Online;
    s->Online = Online;
    if (!was && Online) SyncReconcile(ShareId);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI SyncInit(VOID)
{
    RtlZeroMemory(g_Shares, sizeof(g_Shares));
    RtlZeroMemory(g_Queue, sizeof(g_Queue));
    DbgPrint("SYNC: offline files / sync center initialized\n");
    return STATUS_SUCCESS;
}
