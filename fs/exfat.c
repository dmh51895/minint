/*
 * MinNT - fs/exfat.c
 * exFAT (Extended File Allocation Table) filesystem.
 *
 * exFAT is Microsoft's filesystem for SDXC cards and flash drives.
 * Differences from FAT32:
 *   - 64-bit cluster addressing (up to 128PB volumes)
 *   - No 4GB file size limit
 *   - No short filename limitation
 *   - UTC timestamps instead of local time
 *
 * The directory entry is 32 bytes (vs 32 for FAT32's standard entry).
 * The boot sector and up-case table live at fixed offsets.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define EXFAT_MAX_ENTRIES   128

typedef struct _EXFAT_SUPERBLOCK {
    ULONG JumpBoot[3];
    CHAR FileSystemName[8];   /* "EXFAT   " */
    UCHAR MustBeZero[53];
    ULONG64 PartitionOffset;
    ULONG64 VolumeLength;
    ULONG FatOffset;
    ULONG FatLength;
    ULONG ClusterHeapOffset;
    ULONG ClusterCount;
    ULONG RootDirectoryCluster;
    ULONG SerialNumber;
    USHORT FileSystemRevision;
    USHORT VolumeFlags;
    UCHAR BytesPerSectorShift;
    UCHAR SectorsPerClusterShift;
    UCHAR NumberOfFats;
    UCHAR DriveSelect;
    UCHAR PercentInUse;
} EXFAT_SUPERBLOCK;

typedef struct _EXFAT_ENTRY {
    CHAR Name[256];
    ULONG StartCluster;
    ULONG64 Size;
    BOOLEAN InUse;
} EXFAT_ENTRY;

static EXFAT_ENTRY g_ExFatEntries[EXFAT_MAX_ENTRIES];

NTSTATUS NTAPI ExFatInit(VOID)
{
    RtlZeroMemory(g_ExFatEntries, sizeof(g_ExFatEntries));
    DbgPrint("EXFAT: filesystem driver initialized\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ExFatMount(const CHAR *DevicePath)
{
    if (!DevicePath) return STATUS_INVALID_PARAMETER;
    DbgPrint("EXFAT: mount attempt on %s\n", DevicePath);
    /* A real implementation would read the boot sector at offset 0
     * and verify the "EXFAT   " signature. For MinNT we just register
     * the filesystem as available. */
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ExFatCreateEntry(const CHAR *Path, ULONG64 Size)
{
    if (!Path) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < EXFAT_MAX_ENTRIES; i++) {
        if (!g_ExFatEntries[i].InUse) {
            RtlZeroMemory(&g_ExFatEntries[i], sizeof(EXFAT_ENTRY));
            g_ExFatEntries[i].InUse = TRUE;
            for (ULONG k = 0; k < 255 && Path[k]; k++) g_ExFatEntries[i].Name[k] = Path[k];
            g_ExFatEntries[i].Size = Size;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

ULONG NTAPI ExFatGetEntryCount(VOID)
{
    ULONG n = 0;
    for (ULONG i = 0; i < EXFAT_MAX_ENTRIES; i++) if (g_ExFatEntries[i].InUse) n++;
    return n;
}
