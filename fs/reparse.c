/*
 * MinNT - fs/reparse.c
 * NTFS reparse points (junctions, symbolic links) and hard links.
 *
 * A reparse point is a directory/file with a special attribute that
 * contains a reparse tag and a reparse data buffer. When the I/O
 * manager encounters one, it invokes the registered handler for that
 * tag. NTFS uses tag IO_REPARSE_TAG_MOUNT_POINT for junctions and
 * IO_REPARSE_TAG_SYMLINK for symbolic links.
 *
 * Hard links are a separate mechanism: multiple directory entries
 * point at the same file record (the link count in the MFT record is
 * incremented and each name is stored in a separate index entry).
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/fs.h>
#include <nt/framework.h>

#define REPARSE_MAX_HANDLERS     16
#define REPARSE_MAX_DATA         4096
#define HLINK_MAX                256

/* Well-known reparse tags (subset of <winnt.h>) */
#define IO_REPARSE_TAG_MOUNT_POINT       0xA0000003
#define IO_REPARSE_TAG_SYMLINK           0xA000000C
#define IO_REPARSE_TAG_DEDUP             0x80000013
#define IO_REPARSE_TAG_APPXSTREAM        0x80000014

typedef struct _REPARSE_HANDLER {
    ULONG Tag;
    PVOID Callback;
    struct _REPARSE_HANDLER *Next;
} REPARSE_HANDLER, *PREPARSE_HANDLER;

typedef struct _REPARSE_POINT {
    ULONG Tag;
    CHAR Path[260];
    UCHAR Data[REPARSE_MAX_DATA];
    ULONG DataLength;
    BOOLEAN InUse;
} REPARSE_POINT, *PREPARSE_POINT;

typedef struct _HARDLINK {
    CHAR Path[260];
    ULONG FileId;
    ULONG LinkCount;
    BOOLEAN InUse;
} HARDLINK, *PHARDLINK;

static REPARSE_HANDLER *g_Handlers = NULL;
static REPARSE_POINT g_ReparseTable[REPARSE_MAX_HANDLERS];
static HARDLINK g_HardLinks[HLINK_MAX];

NTSTATUS NTAPI ReparseRegisterHandler(ULONG Tag, PVOID Callback)
{
    REPARSE_HANDLER *h = (REPARSE_HANDLER *)ExAllocatePool(0, sizeof(REPARSE_HANDLER));
    if (!h) return STATUS_NO_MEMORY;
    h->Tag = Tag;
    h->Callback = Callback;
    h->Next = g_Handlers;
    g_Handlers = h;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ReparseSetPoint(const CHAR *Path, ULONG Tag,
                               PVOID Data, ULONG DataLength)
{
    for (ULONG i = 0; i < REPARSE_MAX_HANDLERS; i++) {
        if (!g_ReparseTable[i].InUse) {
            g_ReparseTable[i].InUse = TRUE;
            g_ReparseTable[i].Tag = Tag;
            g_ReparseTable[i].DataLength = DataLength;
            if (DataLength > REPARSE_MAX_DATA) DataLength = REPARSE_MAX_DATA;
            for (ULONG k = 0; k < 260 && Path[k]; k++) {
                g_ReparseTable[i].Path[k] = Path[k];
                if (Path[k] == 0) break;
            }
            if (Data && DataLength) RtlCopyMemory(g_ReparseTable[i].Data, Data, DataLength);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI ReparseGetPoint(const CHAR *Path, PULONG OutTag,
                               PVOID OutData, PULONG OutDataLength)
{
    for (ULONG i = 0; i < REPARSE_MAX_HANDLERS; i++) {
        if (!g_ReparseTable[i].InUse) continue;
        BOOLEAN eq = TRUE;
        for (ULONG k = 0; k < 260; k++) {
            if (g_ReparseTable[i].Path[k] != Path[k]) { eq = FALSE; break; }
            if (Path[k] == 0) break;
        }
        if (eq) {
            if (OutTag) *OutTag = g_ReparseTable[i].Tag;
            if (OutData && OutDataLength) {
                ULONG got = g_ReparseTable[i].DataLength;
                if (got > *OutDataLength) got = *OutDataLength;
                RtlCopyMemory(OutData, g_ReparseTable[i].Data, got);
                *OutDataLength = got;
            }
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI ReparseRemovePoint(const CHAR *Path)
{
    for (ULONG i = 0; i < REPARSE_MAX_HANDLERS; i++) {
        if (!g_ReparseTable[i].InUse) continue;
        BOOLEAN eq = TRUE;
        for (ULONG k = 0; k < 260; k++) {
            if (g_ReparseTable[i].Path[k] != Path[k]) { eq = FALSE; break; }
            if (Path[k] == 0) break;
        }
        if (eq) {
            RtlZeroMemory(&g_ReparseTable[i], sizeof(REPARSE_POINT));
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

/* The I/O manager calls this when opening a path that hits a reparse
 * point. We dispatch to the registered handler if one is registered,
 * otherwise we return STATUS_REPARSE so the caller can apply the
 * resolution itself. */
NTSTATUS NTAPI ReparseResolve(const CHAR *Path, PCHAR OutTarget, PULONG OutLength)
{
    ULONG tag = 0;
    UCHAR data[REPARSE_MAX_DATA];
    ULONG len = sizeof(data);
    NTSTATUS s = ReparseGetPoint(Path, &tag, data, &len);
    if (!NT_SUCCESS(s)) return s;

    /* Find a registered handler. */
    REPARSE_HANDLER *h = g_Handlers;
    while (h) {
        if (h->Tag == tag && h->Callback) break;
        h = h->Next;
    }
    if (h) {
        /* For simplicity, we don't actually invoke the callback here.
         * Handlers should be invoked by the I/O manager after we hand
         * the data back. We return the target via OutTarget. */
    }

    /* data layout for mount point/symlink:
     *   USHORT SubstituteNameOffset
     *   USHORT SubstituteNameLength
     *   USHORT PrintNameOffset
     *   USHORT PrintNameLength
     *   ULONG  Flags
     *   WCHAR  PathBuffer[1]
     */
    if (len < 12) return STATUS_INVALID_PARAMETER;
    PUSHORT sub_off = (PUSHORT)data;
    PUSHORT sub_len = (PUSHORT)(data + 2);
    PUSHORT prt_off = (PUSHORT)(data + 4);
    PUSHORT prt_len = (PUSHORT)(data + 6);
    (void)prt_off; (void)prt_len;
    PWCHAR sub = (PWCHAR)(data + *sub_off);
    ULONG copy_chars = (*sub_len) / sizeof(WCHAR);
    if (copy_chars >= *OutLength / sizeof(WCHAR)) copy_chars = *OutLength / sizeof(WCHAR);
    for (ULONG i = 0; i < copy_chars; i++) {
        ((PWCHAR)OutTarget)[i] = sub[i];
    }
    ((PWCHAR)OutTarget)[copy_chars] = 0;
    *OutLength = copy_chars * sizeof(WCHAR);
    return STATUS_REPARSE;
}

/* Hard link support: an inode/file record can be referenced by multiple
 * pathnames. The link count in the file record is the reference count. */
NTSTATUS NTAPI HardLinkCreate(const CHAR *ExistingPath, const CHAR *NewPath)
{
    /* Look for an existing record. */
    ULONG existingId = 0;
    BOOLEAN found = FALSE;
    for (ULONG i = 0; i < HLINK_MAX; i++) {
        if (g_HardLinks[i].InUse) {
            BOOLEAN eq = TRUE;
            for (ULONG k = 0; k < 260; k++) {
                if (g_HardLinks[i].Path[k] != ExistingPath[k]) { eq = FALSE; break; }
                if (ExistingPath[k] == 0) break;
            }
            if (eq) { existingId = g_HardLinks[i].FileId; found = TRUE; break; }
        }
    }
    if (!found) {
        /* Allocate a fresh FileId from the time source. */
        LARGE_INTEGER tick; KeQuerySystemTime(&tick);
        existingId = (ULONG)(tick.QuadPart & 0xFFFFFFFF);
    }
    for (ULONG i = 0; i < HLINK_MAX; i++) {
        if (!g_HardLinks[i].InUse) {
            g_HardLinks[i].InUse = TRUE;
            g_HardLinks[i].FileId = existingId;
            g_HardLinks[i].LinkCount = 1;
            for (ULONG k = 0; k < 260; k++) {
                g_HardLinks[i].Path[k] = NewPath[k];
                if (NewPath[k] == 0) break;
            }
            /* Bump the link count of the existing record. */
            for (ULONG j = 0; j < HLINK_MAX; j++) {
                if (g_HardLinks[j].InUse && g_HardLinks[j].FileId == existingId) {
                    g_HardLinks[j].LinkCount++;
                }
            }
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI HardLinkRemove(const CHAR *Path)
{
    for (ULONG i = 0; i < HLINK_MAX; i++) {
        if (!g_HardLinks[i].InUse) continue;
        BOOLEAN eq = TRUE;
        for (ULONG k = 0; k < 260; k++) {
            if (g_HardLinks[i].Path[k] != Path[k]) { eq = FALSE; break; }
            if (Path[k] == 0) break;
        }
        if (eq) {
            ULONG fid = g_HardLinks[i].FileId;
            RtlZeroMemory(&g_HardLinks[i], sizeof(HARDLINK));
            for (ULONG j = 0; j < HLINK_MAX; j++) {
                if (g_HardLinks[j].InUse && g_HardLinks[j].FileId == fid) {
                    if (g_HardLinks[j].LinkCount) g_HardLinks[j].LinkCount--;
                }
            }
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI HardLinkGetCount(const CHAR *Path, PULONG OutCount)
{
    if (!OutCount) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < HLINK_MAX; i++) {
        if (!g_HardLinks[i].InUse) continue;
        BOOLEAN eq = TRUE;
        for (ULONG k = 0; k < 260; k++) {
            if (g_HardLinks[i].Path[k] != Path[k]) { eq = FALSE; break; }
            if (Path[k] == 0) break;
        }
        if (eq) { *OutCount = g_HardLinks[i].LinkCount; return STATUS_SUCCESS; }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI ReparseInit(VOID)
{
    RtlZeroMemory(g_ReparseTable, sizeof(g_ReparseTable));
    RtlZeroMemory(g_HardLinks, sizeof(g_HardLinks));
    DbgPrint("REPARSE: reparse point + hard link support initialized\n");
    return STATUS_SUCCESS;
}
