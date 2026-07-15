/*
 * MinNT - fs/partition.c
 * MBR partition table reader — scans disk via AHCI for partitions.
 *
 * Reads the MBR (sector 0) of the AHCI-attached disk, parses the legacy
 * MBR partition table (4 entries at offset 0x1BE), and mounts FAT32 or
 * reports NTFS partitions.
 *
 * No stubs. Real MBR/GPT support.
 */

#include <nt/ntdef.h>
#include <nt/ke.h>
#include <nt/ex.h>
#include <nt/rtl.h>
#include <nt/mm.h>
#include <nt/ahci.h>
#include <nt/fat32.h>
#include <nt/ntfs.h>
#include <nt/partition.h>
#ifndef STATUS_DEVICE_DOES_NOT_EXIST
#define STATUS_DEVICE_DOES_NOT_EXIST ((NTSTATUS)0xC000003EL)
#endif
#ifndef STATUS_OBJECT_NAME_NOT_FOUND
#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034L)
#endif

#define PART_DEBUG 1
#if PART_DEBUG
#define PDBG(fmt, ...) DbgPrint("PART: " fmt "\n", ##__VA_ARGS__)
#else
#define PDBG(fmt, ...)
#endif

/* ============================================================================
 * MBR structures
 * ========================================================================== */

#pragma pack(push, 1)
typedef struct _PARTITION_TABLE_ENTRY {
    UCHAR  boot_flag;          /* 0x80 = active, 0x00 = inactive */
    UCHAR  start_head;
    UCHAR  start_sector;       /* bits 0-5 */
    UCHAR  start_cylinder;
    UCHAR  system_id;           /* partition type */
    UCHAR  end_head;
    UCHAR  end_sector;
    UCHAR  end_cylinder;
    ULONG  start_lba;
    ULONG  num_sectors;
} PARTITION_TABLE_ENTRY, *PPARTITION_TABLE_ENTRY;

typedef struct _MBR {
    UCHAR              boot_code[440];
    ULONG              disk_signature;
    USHORT              reserved;
    PARTITION_TABLE_ENTRY  partitions[4];
    USHORT             signature;        /* 0xAA55 */
} MBR, *PMBR;
#pragma pack(pop)

#define MBR_SIGNATURE       0xAA55
#define PART_TYPE_EMPTY    0x00
#define PART_TYPE_FAT32     0x0B
#define PART_TYPE_FAT32_LBA 0x0C
#define PART_TYPE_NTFS      0x07
#define PART_TYPE_EFI       0xEE  /* GPT protective partition */
#define PART_TYPE_EXTENDED  0x05  /* MBR extended partition */
#define PART_TYPE_EXT_LBA   0x0F

/* ============================================================================
 * GPT support
 * ========================================================================== */

#pragma pack(push, 1)
typedef struct _GPT_HEADER {
    UCHAR   signature[8];     /* "EFI PART" */
    ULONG   revision;
    ULONG   header_size;
    ULONG   header_crc32;
    ULONG   reserved;
    ULONG64 my_lba;
    ULONG64 alternate_lba;
    ULONG64 first_usable_lba;
    ULONG64 last_usable_lba;
    UCHAR   disk_guid[16];
    ULONG64 partition_entry_lba;
    ULONG   num_partition_entries;
    ULONG   partition_entry_size;
    ULONG   partition_entries_crc32;
} GPT_HEADER, *PGPT_HEADER;

typedef struct _GPT_ENTRY {
    UCHAR   type_guid[16];
    UCHAR   unique_guid[16];
    ULONG64 starting_lba;
    ULONG64 ending_lba;
    UCHAR   attributes[8];
    WCHAR   partition_name[36];
} GPT_ENTRY, *PGPT_ENTRY;
#pragma pack(pop)

static const UCHAR GPT_SIGNATURE[8] = { 'E','F','I',' ','P','A','R','T' };

/* Common GPT partition type GUIDs */
static const UCHAR GPT_TYPE_MICROSOFT_BASIC[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

/* ============================================================================
 * Public API
 *
 * PartitionScan() — scans the AHCI disk for partitions, mounts FAT32
 *                   for any FAT32 type partitions found.
 * ========================================================================== */

typedef struct _PARTITION_INFO {
    ULONG   index;
    UCHAR   system_id;
    ULONG64 start_lba;
    ULONG64 num_sectors;
    BOOLEAN mounted;
    CHAR    fstype[16];    /* "FAT32", "NTFS", "UNKNOWN" */
} PARTITION_INFO, *PPARTITION_INFO;

#define MAX_PARTITIONS 32
static PARTITION_INFO g_Partitions[MAX_PARTITIONS];
static ULONG g_NumPartitions = 0;

ULONG NTAPI PartitionGetCount(VOID)
{
    return g_NumPartitions;
}

PPARTITION_INFO PartitionGetInfo(ULONG index)
{
    if (index >= g_NumPartitions) return NULL;
    return &g_Partitions[index];
}

NTSTATUS NTAPI PartitionScan(VOID)
{
    PDBG("Scanning partitions...");
    RtlZeroMemory(g_Partitions, sizeof(g_Partitions));
    g_NumPartitions = 0;

    if (!AhciIsPresent()) {
        PDBG("No AHCI disk present");
        return STATUS_DEVICE_DOES_NOT_EXIST;
    }

    /* Read MBR (sector 0) */
    UCHAR mbr_buf[512];
    NTSTATUS s = AhciReadSectors(0, 1, mbr_buf);
    if (!NT_SUCCESS(s)) {
        PDBG("Failed to read MBR: 0x%x", (ULONG)s);
        return s;
    }

    PMBR mbr = (PMBR)mbr_buf;
    if (mbr->signature != MBR_SIGNATURE) {
        PDBG("Invalid MBR signature: 0x%04x (expected 0xAA55)", mbr->signature);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Check for GPT protective partition (0xEE) */
    BOOLEAN is_gpt = FALSE;
    for (ULONG i = 0; i < 4; i++) {
        if (mbr->partitions[i].system_id == PART_TYPE_EFI) {
            is_gpt = TRUE;
            break;
        }
    }

    if (is_gpt) {
        PDBG("GPT protective partition found — parsing GPT");
        /* Read GPT header from sector 1 */
        UCHAR gpt_buf[512];
        s = AhciReadSectors(1, 1, gpt_buf);
        if (!NT_SUCCESS(s)) return s;

        PGPT_HEADER gpt = (PGPT_HEADER)gpt_buf;
        if (RtlCompareMemory(gpt->signature, GPT_SIGNATURE, 8) != 8) {
            PDBG("Invalid GPT signature");
            return STATUS_INVALID_IMAGE_FORMAT;
        }

        /* Read partition entries */
        ULONG num_entries = gpt->num_partition_entries;
        ULONG entry_size = gpt->partition_entry_size;
        if (num_entries > 128 || entry_size == 0 || entry_size > 256) {
            PDBG("GPT: too many entries or bad entry size");
            return STATUS_INVALID_IMAGE_FORMAT;
        }
        if (entry_size < sizeof(GPT_ENTRY)) entry_size = sizeof(GPT_ENTRY);

        ULONG entries_sectors = (num_entries * entry_size + 511) / 512;
        PGPT_ENTRY entries = ExAllocatePool(NonPagedPool, num_entries * entry_size);
        if (!entries) return STATUS_NO_MEMORY;

        s = AhciReadSectors(gpt->partition_entry_lba, entries_sectors, entries);
        if (!NT_SUCCESS(s)) {
            ExFreePool(entries);
            return s;
        }

        for (ULONG i = 0; i < num_entries; i++) {
            PGPT_ENTRY e = (PGPT_ENTRY)((PUCHAR)entries + i * entry_size);
            /* Empty partition entries have type GUID = all zeros */
            BOOLEAN is_empty = TRUE;
            for (int j = 0; j < 16; j++) {
                if (e->type_guid[j] != 0) { is_empty = FALSE; break; }
            }
            if (is_empty) continue;

            /* Found a partition */
            PARTITION_INFO *p = &g_Partitions[g_NumPartitions++];
            p->index = i;
            p->start_lba = e->starting_lba;
            p->num_sectors = e->ending_lba - e->starting_lba + 1;
            p->system_id = 0;

            /* Identify type */
            if (RtlCompareMemory(e->type_guid, GPT_TYPE_MICROSOFT_BASIC, 16) == 16) {
                /* Read boot sector to determine if FAT32 or NTFS */
                UCHAR bs[512];
                s = AhciReadSectors(p->start_lba, 1, bs);
                if (NT_SUCCESS(s)) {
                    /* Quick check: FAT32 has "FAT32   " at offset 0x52 (or 0x36) */
                    if (RtlCompareMemory(bs + 0x52, "FAT32", 5) == 5 ||
                        RtlCompareMemory(bs + 0x36, "FAT32", 5) == 5) {
                        p->fstype[0] = 'F'; p->fstype[1] = 'A'; p->fstype[2] = 'T';
                        p->fstype[3] = '3'; p->fstype[4] = '2'; p->fstype[5] = 0;
                        /* Mount victim */
                        NTSTATUS ms = Fat32MountPartition(p->start_lba);
                        p->mounted = NT_SUCCESS(ms);
                        PDBG("GPT partition %u: FAT32 at LBA %llu (%s)",
                             i, p->start_lba, p->mounted ? "mounted" : "mount failed");
                    } else if (RtlCompareMemory(bs + 0x03, "NTFS", 4) == 4) {
                        p->fstype[0] = 'N'; p->fstype[1] = 'T'; p->fstype[2] = 'F';
                        p->fstype[3] = 'S'; p->fstype[4] = 0;
                        NTSTATUS ms = NtfsMountPartition(p->start_lba);
                        p->mounted = NT_SUCCESS(ms);
                        PDBG("GPT partition %u: NTFS at LBA %llu (%s)",
                             i, p->start_lba, p->mounted ? "mounted" : "mount failed");
                    } else {
                        p->fstype[0] = '?'; p->fstype[1] = 0;
                        PDBG("GPT partition %u: unknown FS at LBA %llu", i, p->start_lba);
                    }
                }
            } else {
                p->fstype[0] = 'U'; p->fstype[1] = 0;
                PDBG("GPT partition %u: type GUID (not Microsoft basic)", i);
            }

            if (g_NumPartitions >= MAX_PARTITIONS) break;
        }

        ExFreePool(entries);
    } else {
        /* Legacy MBR partition table */
        PDBG("Legacy MBR:");
        for (ULONG i = 0; i < 4; i++) {
            PARTITION_TABLE_ENTRY *e = &mbr->partitions[i];
            if (e->system_id == PART_TYPE_EMPTY) continue;

            PARTITION_INFO *p = &g_Partitions[g_NumPartitions++];
            p->index = i;
            p->system_id = e->system_id;
            p->start_lba = e->start_lba;
            p->num_sectors = e->num_sectors;
            p->mounted = FALSE;
            RtlZeroMemory(p->fstype, 16);

            switch (e->system_id) {
            case PART_TYPE_FAT32:
            case PART_TYPE_FAT32_LBA:
                __builtin_memcpy(p->fstype, "FAT32", 6);
                PDBG("  [%u] FAT32 at LBA %u size %u sectors",
                     i, e->start_lba, e->num_sectors);
                {
                    NTSTATUS ms = Fat32MountPartition(e->start_lba);
                    p->mounted = NT_SUCCESS(ms);
                    if (!p->mounted) {
                        PDBG("  FAT32 mount failed: 0x%x", (ULONG)ms);
                    }
                }
                break;
            case PART_TYPE_NTFS:
                __builtin_memcpy(p->fstype, "NTFS", 5);
                PDBG("  [%u] NTFS at LBA %u, trying mount...", i, e->start_lba);
                {
                    NTSTATUS ms = NtfsMountPartition(e->start_lba);
                    p->mounted = NT_SUCCESS(ms);
                    if (!p->mounted) {
                        PDBG("  NTFS mount failed: 0x%x", (ULONG)ms);
                    }
                }
                break;
            case PART_TYPE_EXTENDED:
            case PART_TYPE_EXT_LBA:
                __builtin_memcpy(p->fstype, "EXTENDED", 9);
                PDBG("  [%u] Extended at LBA %u (not parsed)", i, e->start_lba);
                break;
            default:
                __builtin_memcpy(p->fstype, "UNKNOWN", 8);
                PDBG("  [%u] type 0x%02x at LBA %u size %u sectors",
                     i, e->system_id, e->start_lba, e->num_sectors);
                break;
            }

            if (g_NumPartitions >= MAX_PARTITIONS) break;
        }
    }

    if (g_NumPartitions == 0) {
        PDBG("No partitions found");
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    PDBG("Scan complete: %u partition(s) found", g_NumPartitions);
    return STATUS_SUCCESS;
}
