/*
 * MinNT - fs/vss.c
 * Volume Shadow Copy Service (VSS).
 *
 * VSS provides point-in-time snapshots of a live volume so that
 * "previous versions" and system-restore features have something to
 * restore from. Real VSS requires a coordinate-aware filesystem; in
 * MinNT we model snapshots as immutable differential records: the
 * volume's current state is captured, then each subsequent write is
 * recorded as a "diff" entry that can be replayed (or un-replayed) to
 * reconstruct the snapshot.
 *
 * Exposed interfaces:
 *   VssCreateSnapshot  - capture a snapshot of a volume
 *   VssQuerySnapshot   - retrieve a snapshot's metadata + diff list
 *   VssRestoreSnapshot - delete all writes recorded after the snapshot
 *   VssDeleteSnapshot  - free the snapshot
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define VSS_MAX_SNAPSHOTS     32
#define VSS_MAX_DIFFS         4096
#define VSS_MAX_VOLUME        32

typedef struct _VSS_DIFF {
    ULONG FileId;
    ULONG Offset;
    ULONG Length;
    UCHAR OldData[256];
    BOOLEAN InUse;
} VSS_DIFF, *PVSS_DIFF;

typedef struct _VSS_SNAPSHOT {
    ULONG Id;
    CHAR Volume[VSS_MAX_VOLUME];
    LARGE_INTEGER Timestamp;
    ULONG DiffStart;
    ULONG DiffCount;
    BOOLEAN InUse;
} VSS_SNAPSHOT, *PVSS_SNAPSHOT;

static VSS_SNAPSHOT g_Snapshots[VSS_MAX_SNAPSHOTS];
static VSS_DIFF g_Diffs[VSS_MAX_DIFFS];
static ULONG g_NextSnapshotId = 1;

NTSTATUS NTAPI VssCreateSnapshot(const CHAR *Volume, PULONG OutSnapshotId)
{
    if (!Volume || !OutSnapshotId) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < VSS_MAX_SNAPSHOTS; i++) {
        if (!g_Snapshots[i].InUse) {
            g_Snapshots[i].InUse = TRUE;
            g_Snapshots[i].Id = g_NextSnapshotId++;
            LARGE_INTEGER ts;
            KeQueryPerformanceCounter(&ts, NULL);
            g_Snapshots[i].Timestamp = ts;
            for (ULONG k = 0; k < VSS_MAX_VOLUME; k++) {
                g_Snapshots[i].Volume[k] = Volume[k];
                if (Volume[k] == 0) break;
            }
            /* Find the first free diff slot. */
            for (ULONG j = 0; j < VSS_MAX_DIFFS; j++) {
                if (!g_Diffs[j].InUse) {
                    g_Snapshots[i].DiffStart = j;
                    g_Snapshots[i].DiffCount = 0;
                    break;
                }
            }
            *OutSnapshotId = g_Snapshots[i].Id;
            DbgPrint("VSS: created snapshot %u for volume %s\n",
                     g_Snapshots[i].Id, Volume);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI VssQuerySnapshot(ULONG SnapshotId, PVSS_SNAPSHOT OutInfo,
                                PVOID OutDiffBuffer, ULONG *OutDiffLength)
{
    PVSS_SNAPSHOT snap = NULL;
    for (ULONG i = 0; i < VSS_MAX_SNAPSHOTS; i++) {
        if (g_Snapshots[i].InUse && g_Snapshots[i].Id == SnapshotId) {
            snap = &g_Snapshots[i];
            break;
        }
    }
    if (!snap) return STATUS_NOT_FOUND;
    if (OutInfo) RtlCopyMemory(OutInfo, snap, sizeof(VSS_SNAPSHOT));
    if (OutDiffBuffer && OutDiffLength) {
        ULONG avail = *OutDiffLength;
        ULONG copied = 0;
        for (ULONG k = 0; k < snap->DiffCount && copied + sizeof(VSS_DIFF) <= avail; k++) {
            RtlCopyMemory((PUCHAR)OutDiffBuffer + copied,
                          &g_Diffs[snap->DiffStart + k], sizeof(VSS_DIFF));
            copied += sizeof(VSS_DIFF);
        }
        *OutDiffLength = copied;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI VssRecordDiff(ULONG SnapshotId, ULONG FileId, ULONG Offset,
                            PVOID OldData, ULONG Length)
{
    if (Length > sizeof(VSS_DIFF)) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < VSS_MAX_SNAPSHOTS; i++) {
        if (g_Snapshots[i].InUse && g_Snapshots[i].Id == SnapshotId) {
            ULONG slot = g_Snapshots[i].DiffStart + g_Snapshots[i].DiffCount;
            if (slot >= VSS_MAX_DIFFS) return STATUS_NO_MEMORY;
            g_Diffs[slot].InUse = TRUE;
            g_Diffs[slot].FileId = FileId;
            g_Diffs[slot].Offset = Offset;
            g_Diffs[slot].Length = Length;
            if (OldData) RtlCopyMemory(g_Diffs[slot].OldData, OldData, Length);
            g_Snapshots[i].DiffCount++;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI VssRestoreSnapshot(ULONG SnapshotId)
{
    /* A real implementation would iterate the diffs in reverse order
     * and write the OldData back to the live volume, then delete the
     * diffs. We model that here. */
    for (ULONG i = 0; i < VSS_MAX_SNAPSHOTS; i++) {
        if (g_Snapshots[i].InUse && g_Snapshots[i].Id == SnapshotId) {
            ULONG end = g_Snapshots[i].DiffStart + g_Snapshots[i].DiffCount;
            for (ULONG k = g_Snapshots[i].DiffStart; k < end; k++) {
                if (g_Diffs[k].InUse) {
                    /* In a real VSS, the FS would replay the old data. */
                    g_Diffs[k].InUse = FALSE;
                }
            }
            g_Snapshots[i].DiffCount = 0;
            DbgPrint("VSS: snapshot %u restored\n", SnapshotId);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI VssDeleteSnapshot(ULONG SnapshotId)
{
    for (ULONG i = 0; i < VSS_MAX_SNAPSHOTS; i++) {
        if (g_Snapshots[i].InUse && g_Snapshots[i].Id == SnapshotId) {
            ULONG end = g_Snapshots[i].DiffStart + g_Snapshots[i].DiffCount;
            for (ULONG k = g_Snapshots[i].DiffStart; k < end; k++) {
                if (g_Diffs[k].InUse) g_Diffs[k].InUse = FALSE;
            }
            RtlZeroMemory(&g_Snapshots[i], sizeof(VSS_SNAPSHOT));
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI VssInit(VOID)
{
    RtlZeroMemory(g_Snapshots, sizeof(g_Snapshots));
    RtlZeroMemory(g_Diffs, sizeof(g_Diffs));
    DbgPrint("VSS: volume shadow copy service initialized\n");
    return STATUS_SUCCESS;
}
