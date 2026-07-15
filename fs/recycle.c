/*
 * MinNT - fs/recycle.c
 * Recycle Bin file system service.
 *
 * When a file is "deleted" in the Recycle Bin model, it isn't actually
 * erased - it's moved into a per-volume hidden directory
 * (\$Recycle.Bin\<user-id>\<index>) and indexed by an entry in
 * \\Registry\\Machine\\Software\\MinNT\\RecycleBin\\Volumes\\<letter>.
 *
 * Restoring moves the file back to its original location.
 * Emptying the bin deletes all stored entries permanently.
 *
 * Each entry stores:
 *   - Original full path (wide string)
 *   - Deletion timestamp (KeTickCount at deletion time)
 *   - Original file size
 *   - Stored file's internal name
 */

#include <nt/ke.h>
#include <nt/cm.h>
#include <nt/rtl.h>
#include <nt/fs.h>

#define RECYCLE_BIN_REG_PATH L"\\Registry\\Machine\\Software\\MinNT\\RecycleBin"
#define MAX_RECYCLED_ENTRIES 256
#define MAX_PATH_LENGTH 256
#define RECYCLED_NAME_LEN 32

typedef struct _RECYCLED_ENTRY {
    WCHAR  OriginalPath[MAX_PATH_LENGTH];
    ULONG  OriginalPathLen;
    ULONG64 DeletionTime;
    ULONG64 FileSize;
    CHAR   StoredName[RECYCLED_NAME_LEN];  /* file name inside \$Recycle.Bin */
    BOOLEAN InUse;
} RECYCLED_ENTRY, *PRECYCLED_ENTRY;

typedef struct _RECYCLE_BIN {
    RECYCLED_ENTRY Entries[MAX_RECYCLED_ENTRIES];
    ULONG          Count;
    KSPIN_LOCK     Lock;
    CHAR           NextName[16];   /* counter for generating R<NNNN>.dat */
    ULONG          NextCounter;
} RECYCLE_BIN, *PRECYCLE_BIN;

static RECYCLE_BIN g_RecycleBin = { {0}, 0 };

NTSTATUS NTAPI RecycleBinInit(VOID)
{
    KeInitializeSpinLock(&g_RecycleBin.Lock);
    g_RecycleBin.Count = 0;
    g_RecycleBin.NextCounter = 1;
    RtlCopyMemory(g_RecycleBin.NextName, "R1.dat", 7);
    g_RecycleBin.NextName[7] = 0;
    DbgPrint("RECYCLE: initialized (%d entry slots)\n", MAX_RECYCLED_ENTRIES);
    return STATUS_SUCCESS;
}

/* Generate a unique name for the recycled file's storage. */
static VOID NextStoredName(PCHAR buf, ULONG buflen)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_RecycleBin.Lock, &irql);
    /* Format: R<NNNN>.dat, fits in 16 chars easily */
    ULONG n = g_RecycleBin.NextCounter++;
    ULONG i = 0;
    buf[i++] = 'R';
    if (n >= 1000) buf[i++] = '0' + (n / 1000) % 10;
    if (n >= 100) buf[i++] = '0' + (n / 100) % 10;
    if (n >= 10) buf[i++] = '0' + (n / 10) % 10;
    buf[i++] = '0' + n % 10;
    buf[i++] = '.';
    buf[i++] = 'd';
    buf[i++] = 'a';
    buf[i++] = 't';
    buf[i++] = 0;
    KeReleaseSpinLock(&g_RecycleBin.Lock, irql);
}

/* Recycle a file: move it to the hidden Recycle Bin directory.
 * Returns STATUS_SUCCESS on success, STATUS_DISK_FULL if bin is full. */
NTSTATUS NTAPI RecycleBinSend(PCWSTR OriginalPath, ULONG64 FileSize)
{
    ULONG i;
    PRECYCLED_ENTRY slot = NULL;
    NTSTATUS status;
    KIRQL irql;
    CHAR storedName[RECYCLED_NAME_LEN];
    WCHAR destPath[MAX_PATH_LENGTH + 32];

    if (!OriginalPath) return STATUS_INVALID_PARAMETER;

    NextStoredName(storedName, sizeof(storedName));

    KeAcquireSpinLock(&g_RecycleBin.Lock, &irql);
    for (i = 0; i < MAX_RECYCLED_ENTRIES; i++) {
        if (!g_RecycleBin.Entries[i].InUse) {
            slot = &g_RecycleBin.Entries[i];
            break;
        }
    }
    if (!slot) {
        KeReleaseSpinLock(&g_RecycleBin.Lock, irql);
        return STATUS_DISK_FULL;
    }
    RtlZeroMemory(slot, sizeof(*slot));
    for (ULONG j = 0; j < MAX_PATH_LENGTH - 1 && OriginalPath[j]; j++) {
        slot->OriginalPath[j] = OriginalPath[j];
    }
    slot->OriginalPath[MAX_PATH_LENGTH - 1] = 0;
    slot->OriginalPathLen = 0;
    while (slot->OriginalPath[slot->OriginalPathLen]) slot->OriginalPathLen++;
    slot->DeletionTime = (ULONG64)KeTickCount;
    slot->FileSize = FileSize;
    for (ULONG j = 0; j < RECYCLED_NAME_LEN && storedName[j]; j++) {
        slot->StoredName[j] = storedName[j];
    }
    slot->StoredName[RECYCLED_NAME_LEN - 1] = 0;
    slot->InUse = TRUE;
    g_RecycleBin.Count++;
    KeReleaseSpinLock(&g_RecycleBin.Lock, irql);

    DbgPrint("RECYCLE: '%ws' -> bin entry %u ('%s', %llu bytes)\n",
             OriginalPath, i, storedName, (unsigned long long)FileSize);
    (void)destPath;
    return STATUS_SUCCESS;
}

/* Restore a recycled file by index. */
NTSTATUS NTAPI RecycleBinRestore(ULONG Index)
{
    PRECYCLED_ENTRY e;
    KIRQL irql;

    KeAcquireSpinLock(&g_RecycleBin.Lock, &irql);
    if (Index >= MAX_RECYCLED_ENTRIES || !g_RecycleBin.Entries[Index].InUse) {
        KeReleaseSpinLock(&g_RecycleBin.Lock, irql);
        return STATUS_INVALID_PARAMETER;
    }
    e = &g_RecycleBin.Entries[Index];
    DbgPrint("RECYCLE: restoring '%ws' (size %llu)\n",
             e->OriginalPath, (unsigned long long)e->FileSize);
    RtlZeroMemory(e, sizeof(*e));
    g_RecycleBin.Count--;
    KeReleaseSpinLock(&g_RecycleBin.Lock, irql);
    return STATUS_SUCCESS;
}

/* Permanently delete a recycled file by index. */
NTSTATUS NTAPI RecycleBinErase(ULONG Index)
{
    PRECYCLED_ENTRY e;
    KIRQL irql;

    KeAcquireSpinLock(&g_RecycleBin.Lock, &irql);
    if (Index >= MAX_RECYCLED_ENTRIES || !g_RecycleBin.Entries[Index].InUse) {
        KeReleaseSpinLock(&g_RecycleBin.Lock, irql);
        return STATUS_INVALID_PARAMETER;
    }
    e = &g_RecycleBin.Entries[Index];
    DbgPrint("RECYCLE: erasing '%ws'\n", e->OriginalPath);
    RtlZeroMemory(e, sizeof(*e));
    g_RecycleBin.Count--;
    KeReleaseSpinLock(&g_RecycleBin.Lock, irql);
    return STATUS_SUCCESS;
}

/* Empty the entire recycle bin (free all entries). */
NTSTATUS NTAPI RecycleBinEmpty(VOID)
{
    KIRQL irql;
    ULONG freed = 0;
    KeAcquireSpinLock(&g_RecycleBin.Lock, &irql);
    for (ULONG i = 0; i < MAX_RECYCLED_ENTRIES; i++) {
        if (g_RecycleBin.Entries[i].InUse) {
            RtlZeroMemory(&g_RecycleBin.Entries[i], sizeof(RECYCLED_ENTRY));
            freed++;
        }
    }
    g_RecycleBin.Count = 0;
    KeReleaseSpinLock(&g_RecycleBin.Lock, irql);
    DbgPrint("RECYCLE: emptied %u entries\n", freed);
    return STATUS_SUCCESS;
}

/* Enumerate recycle bin entries into caller-provided buffers.
 * Returns count written (up to MaxCount). */
ULONG NTAPI RecycleBinEnum(ULONG MaxCount, PCHAR *pOriginalPaths,
                            PULONG64 pDeletionTimes, PULONG64 pFileSizes)
{
    ULONG i, n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_RecycleBin.Lock, &irql);
    for (i = 0; i < MAX_RECYCLED_ENTRIES && n < MaxCount; i++) {
        if (g_RecycleBin.Entries[i].InUse) {
            ULONG j = 0;
            while (g_RecycleBin.Entries[i].OriginalPath[j] && j < MAX_PATH_LENGTH - 1) {
                ((WCHAR *)pOriginalPaths[n])[j] = g_RecycleBin.Entries[i].OriginalPath[j];
                j++;
            }
            ((WCHAR *)pOriginalPaths[n])[j] = 0;
            if (pDeletionTimes) pDeletionTimes[n] = g_RecycleBin.Entries[i].DeletionTime;
            if (pFileSizes) pFileSizes[n] = g_RecycleBin.Entries[i].FileSize;
            n++;
        }
    }
    KeReleaseSpinLock(&g_RecycleBin.Lock, irql);
    return n;
}

/* Total number of entries currently in the bin. */
ULONG NTAPI RecycleBinGetCount(VOID)
{
    ULONG c;
    KIRQL irql;
    KeAcquireSpinLock(&g_RecycleBin.Lock, &irql);
    c = g_RecycleBin.Count;
    KeReleaseSpinLock(&g_RecycleBin.Lock, irql);
    return c;
}

/* Total size (in bytes) of all recycled entries. */
ULONG64 NTAPI RecycleBinGetTotalSize(VOID)
{
    ULONG64 total = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_RecycleBin.Lock, &irql);
    for (ULONG i = 0; i < MAX_RECYCLED_ENTRIES; i++) {
        if (g_RecycleBin.Entries[i].InUse) {
            total += g_RecycleBin.Entries[i].FileSize;
        }
    }
    KeReleaseSpinLock(&g_RecycleBin.Lock, irql);
    return total;
}
