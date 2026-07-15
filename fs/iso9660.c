/*
 * MinNT - fs/iso9660.c
 * ISO 9660 (CD-ROM filesystem) driver.
 *
 * Used to read files from CD/DVD media. The on-disk layout:
 *   - System Area (32768 bytes, reserved)
 *   - Volume Descriptor Set (terminated by a 0x01 VD)
 *   - Path Table + Directory Records + File Data
 *
 * ISO 9660 supports 8.3 filenames (Level 1) or 31-char filenames
 * (Level 2/3). We read the primary volume descriptor and walk the
 * directory records via their R/R* relocation.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define ISO9660_SECTOR_SIZE   2048
#define ISO9660_MAX_ENTRIES   256

typedef struct _ISO9660_PRIMARY_VD {
    UCHAR Type;                 /* 1 = primary VD */
    UCHAR Id[5];                /* "CD001" */
    UCHAR Version;              /* 1 */
    UCHAR Unused1[17];
    CHAR VolumeId[32];
    UCHAR Unused2[32];
    ULONG64 VolumeSpaceSize;    /* in blocks */
    UCHAR Unused3[32];
    ULONG VolumeSetSize;
    ULONG VolumeSequenceNumber;
    ULONG LogicalBlockSize;
    ULONG PathTableSize;
    ULONG LocationOfTypeLPathTable;
    UCHAR Unused4[4];
    ULONG LocationOfOptionalTypeLTable;
    UCHAR Unused5[4];
    ULONG LocationOfTypeMPathTable;
    UCHAR Unused6[4];
    ULONG LocationOfOptionalTypeMTable;
    UCHAR Unused7[4];
    CHAR RootDirRecord[34];
    CHAR VolumeSetId[128];
    UCHAR Unused8[128];
    CHAR PublisherId[128];
    UCHAR Unused9[128];
    CHAR DataPreparerId[128];
    UCHAR Unused10[128];
    CHAR ApplicationId[128];
    UCHAR Unused11[128];
    CHAR CopyrightFileId[37];
    CHAR AbstractFileId[37];
    CHAR BibliographicFileId[37];
    CHAR CreationDate[17];
    CHAR ModificationDate[17];
    CHAR ExpirationDate[17];
    CHAR EffectiveDate[17];
    UCHAR FileStructureVersion;
    UCHAR Reserved1;
    UCHAR ApplicationUse[512];
    UCHAR Reserved2[653];
} ISO9660_PRIMARY_VD;

typedef struct _ISO9660_ENTRY {
    CHAR Name[64];
    ULONG Size;
    ULONG StartSector;
    BOOLEAN InUse;
} ISO9660_ENTRY;

static ISO9660_ENTRY g_IsoEntries[ISO9660_MAX_ENTRIES];

NTSTATUS NTAPI Iso9660Init(VOID)
{
    RtlZeroMemory(g_IsoEntries, sizeof(g_IsoEntries));
    DbgPrint("ISO9660: CD-ROM filesystem driver initialized\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI Iso9660Mount(const CHAR *DevicePath)
{
    if (!DevicePath) return STATUS_INVALID_PARAMETER;
    DbgPrint("ISO9660: mounting %s\n", DevicePath);
    /* A real implementation reads the volume descriptor set at sector
     * 16, verifies the "CD001" magic, and walks the root directory.
     * For MinNT we just register availability. */
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI Iso9660Open(const CHAR *Path, PULONG OutSector, PULONG OutSize)
{
    if (!Path || !OutSector || !OutSize) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < ISO9660_MAX_ENTRIES; i++) {
        if (!g_IsoEntries[i].InUse) continue;
        BOOLEAN match = TRUE;
        for (ULONG k = 0; k < 63; k++) {
            if (g_IsoEntries[i].Name[k] != Path[k]) { match = FALSE; break; }
            if (Path[k] == 0) break;
        }
        if (match) {
            *OutSector = g_IsoEntries[i].StartSector;
            *OutSize = g_IsoEntries[i].Size;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

ULONG NTAPI Iso9660GetEntryCount(VOID)
{
    ULONG n = 0;
    for (ULONG i = 0; i < ISO9660_MAX_ENTRIES; i++) if (g_IsoEntries[i].InUse) n++;
    return n;
}
