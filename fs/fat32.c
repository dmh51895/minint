/*
 * MinNT - fs/fat32.c
 * FAT32 filesystem driver — mounts real disk partitions via AHCI.
 *
 * Reads FAT32 BPB from disk sector 0 (or partition offset), manages
 * the FAT chain, directory traversal, and file read/write operations.
 *
 * For write operations, we update the FAT and directory entries on disk
 * in addition to caching in RAM where possible.
 *
 * No stubs. Real FAT32 implementation.
 */

#include <nt/ntdef.h>
#include <nt/ke.h>
#include <nt/ex.h>
#include <nt/mm.h>
#include <nt/rtl.h>
#include <nt/fs.h>
#include <nt/ahci.h>
#ifndef STATUS_DEVICE_DOES_NOT_EXIST
#define STATUS_DEVICE_DOES_NOT_EXIST ((NTSTATUS)0xC000003EL)
#endif
#ifndef STATUS_DISK_FULL
#define STATUS_DISK_FULL ((NTSTATUS)0xC000007FL)
#endif
#ifndef STATUS_OBJECT_NAME_NOT_FOUND
#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034L)
#endif
#ifndef STATUS_INVALID_IMAGE_FORMAT
#define STATUS_INVALID_IMAGE_FORMAT ((NTSTATUS)0xC000007BL)
#endif

#define FAT32_DEBUG 1
#if FAT32_DEBUG
#define F32DBG(fmt, ...) DbgPrint("FAT32: " fmt "\n", ##__VA_ARGS__)
#else
#define F32DBG(fmt, ...)
#endif

/* ============================================================================
 * FAT32 Structures (from Microsoft FAT spec)
 * ========================================================================== */

#pragma pack(push, 1)
typedef struct _FAT32_BPB {
    UCHAR   JumpBoot[3];
    UCHAR   OEMName[8];
    USHORT  BytesPerSector;
    UCHAR   SectorsPerCluster;
    USHORT  ReservedSectors;
    UCHAR   NumberOfFATs;
    USHORT  RootEntryCount;       /* 0 for FAT32 */
    USHORT  TotalSectors16;       /* 0 for FAT32 */
    UCHAR   Media;
    USHORT  SectorsPerFAT16;      /* 0 for FAT32 */
    USHORT  SectorsPerTrack;
    USHORT  NumberOfHeads;
    ULONG   HiddenSectors;
    ULONG   TotalSectors32;
    /* FAT32 extended BPB */
    ULONG   SectorsPerFAT32;
    USHORT  ExtFlags;
    USHORT  FSVersion;
    ULONG   RootCluster;          /* typically 2 */
    USHORT  FSInfoSector;
    USHORT  BackupBootSector;
    UCHAR   Reserved[12];
    UCHAR   DriveNumber;
    UCHAR   Reserved1;
    UCHAR   BootSignature;        /* 0x29 */
    ULONG   VolumeID;
    UCHAR   VolumeLabel[11];
    UCHAR   FileSystemType[8];    /* "FAT32   " */
} FAT32_BPB, *PFAT32_BPB;

typedef struct _FAT_DIR_ENTRY {
    UCHAR   Name[11];        /* 8.3 format, space padded */
    UCHAR   Attributes;
    UCHAR   ReservedNT;
    UCHAR   CreateTimeTenth;
    USHORT  CreateTime;
    USHORT  CreateDate;
    USHORT  AccessDate;
    USHORT  FirstClusterHigh;
    USHORT  ModifyTime;
    USHORT  ModifyDate;
    USHORT  FirstClusterLow;
    ULONG   FileSize;
} FAT_DIR_ENTRY, *PFAT_DIR_ENTRY;
#pragma pack(pop)

#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LONG_NAME  0x0F

/* ============================================================================
 * FAT32 mount state
 * ========================================================================== */

typedef struct _FAT32_MOUNT {
    ULONG64     partition_start;   /* LBA where partition begins */
    USHORT      bytes_per_sector;
    UCHAR       sectors_per_cluster;
    ULONG       bytes_per_cluster;
    USHORT      reserved_sectors;
    UCHAR       num_fats;
    ULONG       sectors_per_fat;
    ULONG       root_cluster;
    ULONG64     fat_start;          /* LBA of first FAT */
    ULONG64     data_start;         /* LBA of first data cluster */
    ULONG       fat_total_sectors;
    BOOLEAN     valid;
    /* FAT cache — we cache the entire FAT in memory for speed */
    ULONG       *fat_cache;
    ULONG       fat_entries;
    /* Lock */
    KSPIN_LOCK  lock;
} FAT32_MOUNT, *PFAT32_MOUNT;

static FAT32_MOUNT g_Fat32Mount;

/* ============================================================================
 * Sector read — kernel-side AHCI wrapper
 *
 * AHCI requires reading in 512-byte sectors (or larger multiples). For
 * FAT32, all reads happen in sector units.
 * ========================================================================== */

static NTSTATUS Fat32ReadSectors(ULONG64 lba, ULONG count, PVOID buffer)
{
    /* For sub-sector reads, read into a temp buffer */
    if (count == 0) return STATUS_SUCCESS;
    NTSTATUS s = AhciReadSectors(lba, count, buffer);
    if (!NT_SUCCESS(s)) {
        F32DBG("read sectors at LBA %llu count %u failed: 0x%x", lba, count, (ULONG)s);
    }
    return s;
}

static NTSTATUS Fat32WriteSectors(ULONG64 lba, ULONG count, const void *buffer)
{
    if (count == 0) return STATUS_SUCCESS;
    NTSTATUS s = AhciWriteSectors(lba, count, buffer);
    if (!NT_SUCCESS(s)) {
        F32DBG("write sectors at LBA %llu count %u failed: 0x%x", lba, count, (ULONG)s);
    }
    return s;
}

/* ============================================================================
 * Cluster I/O
 * ========================================================================== */

static NTSTATUS Fat32ReadCluster(ULONG cluster, PVOID buffer)
{
    if (cluster < 2) return STATUS_INVALID_PARAMETER;
    ULONG64 lba = g_Fat32Mount.data_start + (ULONG64)(cluster - 2) *
                   g_Fat32Mount.sectors_per_cluster;
    return Fat32ReadSectors(lba, g_Fat32Mount.sectors_per_cluster, buffer);
}

static NTSTATUS Fat32WriteCluster(ULONG cluster, const void *buffer)
{
    if (cluster < 2) return STATUS_INVALID_PARAMETER;
    ULONG64 lba = g_Fat32Mount.data_start + (ULONG64)(cluster - 2) *
                   g_Fat32Mount.sectors_per_cluster;
    return Fat32WriteSectors(lba, g_Fat32Mount.sectors_per_cluster, buffer);
}

/* ============================================================================
 * FAT chain operations
 * ========================================================================== */

static ULONG Fat32GetFatEntry(ULONG cluster)
{
    if (cluster >= g_Fat32Mount.fat_entries) return 0x0FFFFFFF;
    if (!g_Fat32Mount.fat_cache) return 0x0FFFFFFF;
    return g_Fat32Mount.fat_cache[cluster] & 0x0FFFFFFF;
}

static NTSTATUS Fat32SetFatEntry(ULONG cluster, ULONG value)
{
    if (cluster >= g_Fat32Mount.fat_entries) return STATUS_INVALID_PARAMETER;
    if (!g_Fat32Mount.fat_cache) return STATUS_INVALID_PARAMETER;
    g_Fat32Mount.fat_cache[cluster] = value & 0x0FFFFFFF;

    /* Also write through to disk — each FAT32 entry is 4 bytes */
    ULONG64 fat_lba = g_Fat32Mount.fat_start;
    ULONG byte_offset = cluster * 4;
    ULONG sector_offset = byte_offset / g_Fat32Mount.bytes_per_sector;
    ULONG offset_in_sector = byte_offset % g_Fat32Mount.bytes_per_sector;

    UCHAR sector_buf[512];
    NTSTATUS s = Fat32ReadSectors(fat_lba + sector_offset, 1, sector_buf);
    if (!NT_SUCCESS(s)) return s;

    ULONG *fat_in_sector = (ULONG *)(sector_buf + offset_in_sector);
    *fat_in_sector = value & 0x0FFFFFFF;

    s = Fat32WriteSectors(fat_lba + sector_offset, 1, sector_buf);
    return s;
}

static NTSTATUS Fat32FindFreeCluster(ULONG *out_cluster)
{
    for (ULONG i = 2; i < g_Fat32Mount.fat_entries; i++) {
        if (g_Fat32Mount.fat_cache[i] == 0) {
            *out_cluster = i;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_DISK_FULL;
}

static NTSTATUS Fat32AllocateChain(ULONG start_cluster, ULONG count)
{
    ULONG cur = start_cluster;
    for (ULONG i = 0; i < count; i++) {
        ULONG next = 0;
        if (i + 1 < count) {
            NTSTATUS s = Fat32FindFreeCluster(&next);
            if (!NT_SUCCESS(s)) {
                /* Allocation chain ran out */
                Fat32SetFatEntry(cur, 0x0FFFFFFF);
                return s;
            }
            Fat32SetFatEntry(next, 0x0FFFFFFF);
        } else {
            next = 0x0FFFFFFF; /* EOC */
        }
        Fat32SetFatEntry(cur, next);
        cur = next;
    }
    return STATUS_SUCCESS;
}

/* ============================================================================
 * Directory operations
 * ========================================================================== */

static NTSTATUS Fat32ReadDirectory(ULONG dir_cluster, FAT_DIR_ENTRY *entries,
                                     ULONG max_entries, ULONG *out_count)
{
    PVOID cluster_buf = ExAllocatePool(NonPagedPool, g_Fat32Mount.bytes_per_cluster);
    if (!cluster_buf) return STATUS_NO_MEMORY;

    ULONG cur = dir_cluster;
    ULONG count = 0;
    while (cur < 0x0FFFFFF8 && cur >= 2 && count < max_entries) {
        NTSTATUS s = Fat32ReadCluster(cur, cluster_buf);
        if (!NT_SUCCESS(s)) {
            ExFreePool(cluster_buf);
            return s;
        }

        ULONG entries_per_cluster = g_Fat32Mount.bytes_per_cluster / sizeof(FAT_DIR_ENTRY);
        PFAT_DIR_ENTRY dir = (PFAT_DIR_ENTRY)cluster_buf;
        for (ULONG i = 0; i < entries_per_cluster && count < max_entries; i++) {
            if (dir[i].Name[0] == 0) {
                /* End of directory */
                ExFreePool(cluster_buf);
                if (out_count) *out_count = count;
                return STATUS_SUCCESS;
            }
            if (dir[i].Name[0] != 0xE5) { /* not deleted */
                entries[count++] = dir[i];
            }
        }

        /* Follow FAT chain */
        cur = Fat32GetFatEntry(cur);
        if (cur == 0 || cur >= 0x0FFFFFF8) break;
    }

    ExFreePool(cluster_buf);
    if (out_count) *out_count = count;
    return STATUS_SUCCESS;
}

static NTSTATUS Fat32FindEntry(ULONG dir_cluster, const CHAR *name,
                                FAT_DIR_ENTRY *out_entry)
{
    /* Convert name to 8.3 format */
    CHAR fat_name[11];
    RtlZeroMemory(fat_name, 11);
    ULONG si = 0, di = 0;
    /* Copy filename part (up to 8 chars or '.') */
    while (name[si] && name[si] != '.' && di < 8) {
        CHAR c = name[si++];
        if (c >= 'a' && c <= 'z') c -= 32;
        fat_name[di++] = c;
    }
    /* Skip to extension */
    while (name[si] && name[si] != '.') si++;
    if (name[si] == '.') si++;
    /* Copy extension (3 chars) */
    di = 8;
    while (name[si] && di < 11) {
        CHAR c = name[si++];
        if (c >= 'a' && c <= 'z') c -= 32;
        fat_name[di++] = c;
    }

    FAT_DIR_ENTRY entries[64];
    ULONG count = 0;
    NTSTATUS s = Fat32ReadDirectory(dir_cluster, entries, 64, &count);
    if (!NT_SUCCESS(s)) return s;

    for (ULONG i = 0; i < count; i++) {
        if (RtlCompareMemory(entries[i].Name, fat_name, 11) == 11) {
            *out_entry = entries[i];
            return STATUS_SUCCESS;
        }
    }

    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/* ============================================================================
 * Mount
 * ========================================================================== */

NTSTATUS NTAPI Fat32MountPartition(ULONG64 partition_start_lba)
{
    F32DBG("Mounting FAT32 partition at LBA %llu...", partition_start_lba);

    UCHAR boot_sector[512];
    NTSTATUS s = Fat32ReadSectors(partition_start_lba, 1, boot_sector);
    if (!NT_SUCCESS(s)) {
        F32DBG("Failed to read boot sector at LBA %llu", partition_start_lba);
        return s;
    }

    PFAT32_BPB bpb = (PFAT32_BPB)boot_sector;

    /* Validate FAT32 signature */
    if (bpb->RootEntryCount != 0 || bpb->SectorsPerFAT16 != 0 ||
        bpb->BytesPerSector == 0) {
        F32DBG("Not a FAT32 volume (RootEntryCount=%u, FAT16=%u)",
               bpb->RootEntryCount, bpb->SectorsPerFAT16);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Check "FAT32" string in filesystem type */
    /* Some FAT32 volumes don't have this set, so we go by rootEntryCount=0 */

    /* Save mount metadata */
    g_Fat32Mount.partition_start = partition_start_lba;
    g_Fat32Mount.bytes_per_sector = bpb->BytesPerSector;
    g_Fat32Mount.sectors_per_cluster = bpb->SectorsPerCluster;
    g_Fat32Mount.bytes_per_cluster = bpb->BytesPerSector * bpb->SectorsPerCluster;
    g_Fat32Mount.reserved_sectors = bpb->ReservedSectors;
    g_Fat32Mount.num_fats = bpb->NumberOfFATs;
    g_Fat32Mount.sectors_per_fat = bpb->SectorsPerFAT32;
    g_Fat32Mount.root_cluster = bpb->RootCluster;

    /* Calculate key offsets */
    g_Fat32Mount.fat_start = partition_start_lba + bpb->ReservedSectors;
    g_Fat32Mount.fat_total_sectors = bpb->NumberOfFATs * bpb->SectorsPerFAT32;
    g_Fat32Mount.data_start = g_Fat32Mount.fat_start + g_Fat32Mount.fat_total_sectors;

    /* Total data sectors */
    ULONG total_sectors = bpb->TotalSectors32;
    if (total_sectors == 0) total_sectors = bpb->TotalSectors16;

    ULONG data_sectors = total_sectors - (bpb->ReservedSectors + g_Fat32Mount.fat_total_sectors);
    g_Fat32Mount.fat_entries = data_sectors / bpb->SectorsPerCluster;

    /* Allocate FAT cache */
    if (g_Fat32Mount.fat_cache) {
        ExFreePool(g_Fat32Mount.fat_cache);
    }
    g_Fat32Mount.fat_cache = ExAllocatePool(NonPagedPool,
                                              g_Fat32Mount.fat_entries * sizeof(ULONG));
    if (!g_Fat32Mount.fat_cache) {
        F32DBG("Failed to allocate FAT cache (%u entries)", g_Fat32Mount.fat_entries);
        return STATUS_NO_MEMORY;
    }

    /* Read the entire FAT into cache */
    ULONG fat_sectors_to_read = g_Fat32Mount.fat_total_sectors;
    F32DBG("Reading FAT: %u sectors (%u entries)...", fat_sectors_to_read, g_Fat32Mount.fat_entries);

    /* Read in chunks of 64 sectors */
    ULONG offset = 0;
    while (offset < fat_sectors_to_read) {
        ULONG chunk = fat_sectors_to_read - offset;
        if (chunk > 64) chunk = 64;
        s = Fat32ReadSectors(g_Fat32Mount.fat_start + offset, chunk,
                              (PUCHAR)g_Fat32Mount.fat_cache + offset * 512);
        if (!NT_SUCCESS(s)) {
            F32DBG("Failed to read FAT sectors: 0x%x", (ULONG)s);
            return s;
        }
        offset += chunk;
    }

    g_Fat32Mount.valid = TRUE;
    KeInitializeSpinLock(&g_Fat32Mount.lock);

    F32DBG("Mounted: bytesPerSector=%u spc=%u bpc=%u reserved=%u fats=%u",
           g_Fat32Mount.bytes_per_sector, g_Fat32Mount.sectors_per_cluster,
           g_Fat32Mount.bytes_per_cluster, g_Fat32Mount.reserved_sectors,
           g_Fat32Mount.num_fats);
    F32DBG("Root cluster: %u, FAT entries: %u, data start LBA: %llu",
           g_Fat32Mount.root_cluster, g_Fat32Mount.fat_entries,
           g_Fat32Mount.data_start);

    return STATUS_SUCCESS;
}

BOOLEAN NTAPI Fat32IsMounted(VOID)
{
    return g_Fat32Mount.valid;
}

/* ============================================================================
 * File read API — exposed via FsFat32ReadFile
 *
 * Reads a file by name from the root directory.
 * ========================================================================== */

NTSTATUS NTAPI Fat32ReadFile(const CHAR *name, PVOID buffer, ULONG bufsize, PULONG bytes_read)
{
    if (!g_Fat32Mount.valid) return STATUS_DEVICE_DOES_NOT_EXIST;
    if (bytes_read) *bytes_read = 0;

    FAT_DIR_ENTRY entry;
    NTSTATUS s = Fat32FindEntry(g_Fat32Mount.root_cluster, name, &entry);
    if (!NT_SUCCESS(s)) return s;

    if (entry.FileSize == 0) {
        return STATUS_SUCCESS;
    }

    ULONG read_size = entry.FileSize;
    if (read_size > bufsize) read_size = bufsize;

    ULONG start_cluster = (entry.FirstClusterHigh << 16) | entry.FirstClusterLow;
    if (start_cluster < 2) return STATUS_INVALID_PARAMETER;

    /* Allocate cluster buffer */
    PVOID cluster_buf = ExAllocatePool(NonPagedPool, g_Fat32Mount.bytes_per_cluster);
    if (!cluster_buf) return STATUS_NO_MEMORY;

    ULONG cur = start_cluster;
    ULONG bytes_copied = 0;
    while (cur >= 2 && cur < 0x0FFFFFF8 && bytes_copied < read_size) {
        s = Fat32ReadCluster(cur, cluster_buf);
        if (!NT_SUCCESS(s)) {
            ExFreePool(cluster_buf);
            return s;
        }
        ULONG copy = g_Fat32Mount.bytes_per_cluster;
        if (bytes_copied + copy > read_size) copy = read_size - bytes_copied;
        RtlCopyMemory((PUCHAR)buffer + bytes_copied, cluster_buf, copy);
        bytes_copied += copy;
        cur = Fat32GetFatEntry(cur);
    }

    ExFreePool(cluster_buf);
    if (bytes_read) *bytes_read = bytes_copied;
    return STATUS_SUCCESS;
}

/* ============================================================================
 * Directory entry creation/modification
 * ========================================================================== */

static NTSTATUS Fat32WriteDirectoryEntry(ULONG dir_cluster, FAT_DIR_ENTRY *entry, BOOLEAN is_new)
{
    PVOID cluster_buf = ExAllocatePool(NonPagedPool, g_Fat32Mount.bytes_per_cluster);
    if (!cluster_buf) return STATUS_NO_MEMORY;

    ULONG cur = dir_cluster;
    while (cur < 0x0FFFFFF8 && cur >= 2) {
        NTSTATUS s = Fat32ReadCluster(cur, cluster_buf);
        if (!NT_SUCCESS(s)) {
            ExFreePool(cluster_buf);
            return s;
        }

        ULONG entries_per_cluster = g_Fat32Mount.bytes_per_cluster / sizeof(FAT_DIR_ENTRY);
        PFAT_DIR_ENTRY dir = (PFAT_DIR_ENTRY)cluster_buf;
        
        for (ULONG i = 0; i < entries_per_cluster; i++) {
            if (dir[i].Name[0] == 0x00 || dir[i].Name[0] == 0xE5) {
                /* Found free slot */
                RtlCopyMemory(&dir[i], entry, sizeof(FAT_DIR_ENTRY));
                
                /* Write back the cluster */
                NTSTATUS s = Fat32WriteCluster(cur, cluster_buf);
                ExFreePool(cluster_buf);
                return s;
            }
        }

        /* Follow FAT chain */
        cur = Fat32GetFatEntry(cur);
        if (cur == 0 || cur >= 0x0FFFFFF8) break;
    }

    ExFreePool(cluster_buf);
    return STATUS_DISK_FULL;
}

/* ============================================================================
 * File write API
 * ========================================================================== */

/* Write or create a file */
NTSTATUS NTAPI Fat32WriteFile(const CHAR *name, const VOID *buffer, ULONG bufsize, PULONG bytes_written)
{
    if (!g_Fat32Mount.valid) return STATUS_DEVICE_DOES_NOT_EXIST;

    /* Convert name to 8.3 format */
    CHAR fat_name[11];
    RtlZeroMemory(fat_name, 11);
    ULONG si = 0, di = 0;
    while (name[si] && name[si] != '.' && di < 8) {
        CHAR c = name[si++];
        if (c >= 'a' && c <= 'z') c -= 32;
        fat_name[di++] = c;
    }
    while (di < 8) fat_name[di++] = ' ';
    while (name[si] && name[si] != '.') si++;
    if (name[si] == '.') si++;
    di = 8;
    while (name[si] && di < 11) {
        CHAR c = name[si++];
        if (c >= 'a' && c <= 'z') c -= 32;
        fat_name[di++] = c;
    }
    while (di < 11) fat_name[di++] = ' ';

    /* Check if file already exists */
    FAT_DIR_ENTRY entry;
    NTSTATUS s = Fat32FindEntry(g_Fat32Mount.root_cluster, name, &entry);
    ULONG start_cluster;
    BOOLEAN is_new = FALSE;

    if (NT_SUCCESS(s)) {
        /* File exists - get existing cluster chain */
        start_cluster = (entry.FirstClusterHigh << 16) | entry.FirstClusterLow;
        if (start_cluster < 2) return STATUS_INVALID_PARAMETER;
    } else {
        /* Create new file */
        is_new = TRUE;
        ULONG new_cluster;
        NTSTATUS s = Fat32FindFreeCluster(&start_cluster);
        if (!NT_SUCCESS(s)) return s;
        /* Mark as end of chain */
        Fat32SetFatEntry(start_cluster, 0x0FFFFFFF);
        
        /* Create directory entry */
        FAT_DIR_ENTRY new_entry;
        RtlZeroMemory(&new_entry, sizeof(new_entry));
        /* Copy 8.3 name */
        for (int i = 0; i < 11; i++) new_entry.Name[i] = ' ';
        /* Parse name for 8.3 format */
        si = 0; di = 0;
        while (name[si] && name[si] != '.' && di < 8) {
            CHAR c = name[si++];
            if (c >= 'a' && c <= 'z') c -= 32;
            new_entry.Name[di++] = c;
        }
        while (di < 8) new_entry.Name[di++] = ' ';
        while (name[si] && name[si] != '.') si++;
        if (name[si] == '.') si++;
        di = 8;
        while (name[si] && di < 11) {
            CHAR c = name[si++];
            if (c >= 'a' && c <= 'z') c -= 32;
            new_entry.Name[di++] = c;
        }
        while (di < 11) new_entry.Name[di++] = ' ';
        new_entry.Attributes = FAT_ATTR_ARCHIVE;
        new_entry.FirstClusterHigh = (USHORT)(start_cluster >> 16);
        new_entry.FirstClusterLow = (USHORT)start_cluster;
        new_entry.FileSize = 0;
        
        s = Fat32WriteDirectoryEntry(g_Fat32Mount.root_cluster, &new_entry, TRUE);
        if (!NT_SUCCESS(s)) return s;
    }

    /* Calculate clusters needed */
    ULONG bytes_per_cluster = g_Fat32Mount.bytes_per_cluster;
    ULONG clusters_needed = (bufsize + bytes_per_cluster - 1) / bytes_per_cluster;
    if (clusters_needed == 0) clusters_needed = 1;

    /* Allocate/extend cluster chain */
    if (is_new) {
        NTSTATUS s = Fat32AllocateChain(start_cluster, clusters_needed);
        if (!NT_SUCCESS(s)) return s;
    } else {
        /* Extend existing chain if needed */
        ULONG cur = start_cluster;
        ULONG existing_clusters = 0;
        ULONG cur_cluster = start_cluster;
        while (cur_cluster < 0x0FFFFFF8 && cur_cluster >= 2) {
            existing_clusters++;
            cur_cluster = Fat32GetFatEntry(cur_cluster);
        }
        
        if (existing_clusters < clusters_needed) {
            ULONG cur = start_cluster;
            ULONG prev = 0;
            while (cur < 0x0FFFFFF8 && cur >= 2) {
                prev = cur;
                cur = Fat32GetFatEntry(cur);
            }
            /* cur is EOC, prev is last cluster */
            ULONG extra = clusters_needed - existing_clusters;
            s = Fat32AllocateChain(prev, extra);
            if (!NT_SUCCESS(s)) return s;
        }
    }

    /* Write data */
    ULONG bytes_written_local = 0;
    ULONG cur = start_cluster;
    while (bytes_written_local < bufsize && cur >= 2 && cur < 0x0FFFFFF8) {
        ULONG bytes_this_cluster = g_Fat32Mount.bytes_per_cluster;
        ULONG copy = bufsize - bytes_written_local;
        if (copy > g_Fat32Mount.bytes_per_cluster) copy = g_Fat32Mount.bytes_per_cluster;
        
        PVOID cluster_buf = ExAllocatePool(NonPagedPool, g_Fat32Mount.bytes_per_cluster);
        if (!cluster_buf) return STATUS_NO_MEMORY;
        
        if (bytes_written_local + copy > bufsize) copy = bufsize - bytes_written_local;
        if (copy < g_Fat32Mount.bytes_per_cluster) {
            RtlZeroMemory(cluster_buf, g_Fat32Mount.bytes_per_cluster);
        }
        RtlCopyMemory(cluster_buf, (PUCHAR)buffer + bytes_written_local, copy);
        
        NTSTATUS s = Fat32WriteCluster(cur, cluster_buf);
        ExFreePool(cluster_buf);
        if (!NT_SUCCESS(s)) return s;

        bytes_written_local += copy;
        
        ULONG next = Fat32GetFatEntry(cur);
        if (next >= 0x0FFFFFF8 || next == 0) break;
        cur = next;
    }

    if (bytes_written_local) *bytes_written = bytes_written_local;

    /* Update directory entry size if new file */
    if (is_new) {
        entry.FileSize = bytes_written_local;
        s = Fat32WriteDirectoryEntry(g_Fat32Mount.root_cluster, &entry, FALSE);
        if (!NT_SUCCESS(s)) return s;
    }

    return STATUS_SUCCESS;
}

/* Delete a file */
NTSTATUS NTAPI Fat32DeleteFile(const CHAR *name)
{
    if (!g_Fat32Mount.valid) return STATUS_DEVICE_DOES_NOT_EXIST;

    FAT_DIR_ENTRY entry;
    NTSTATUS s = Fat32FindEntry(g_Fat32Mount.root_cluster, name, &entry);
    if (!NT_SUCCESS(s)) return s;

    ULONG start_cluster = (entry.FirstClusterHigh << 16) | entry.FirstClusterLow;
    if (start_cluster >= 2) {
        /* Free the cluster chain */
        ULONG cur = (entry.FirstClusterHigh << 16) | entry.FirstClusterLow;
        while (cur < 0x0FFFFFF8 && cur >= 2) {
            ULONG next = Fat32GetFatEntry(cur);
            Fat32SetFatEntry(cur, 0); /* Free cluster */
            cur = next;
        }
    }

    /* Mark directory entry as deleted (0xE5) */
    FAT_DIR_ENTRY deleted_entry = entry;
    deleted_entry.Name[0] = 0xE5;
    return Fat32WriteDirectoryEntry(g_Fat32Mount.root_cluster, &deleted_entry, FALSE);
}

/* ============================================================================
 * Directory listing API
 * ========================================================================== */

NTSTATUS NTAPI Fat32ListDirectory(ULONG dir_cluster, FAT_DIR_ENTRY *entries,
                                    ULONG max_entries, PULONG out_count)
{
    if (!g_Fat32Mount.valid) return STATUS_DEVICE_DOES_NOT_EXIST;
    return Fat32ReadDirectory(dir_cluster, entries, max_entries, out_count);
}

ULONG NTAPI Fat32GetRootCluster(VOID)
{
    return g_Fat32Mount.root_cluster;
}

/* ============================================================================
 * File write API — exposed via FsFat32WriteFile
 *
 * Writes a file by name to the FAT32 volume.
 * ========================================================================== */

