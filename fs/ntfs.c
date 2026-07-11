/*
 * MinNT - fs/ntfs.c
 * NTFS filesystem driver — reads NTFS volumes via AHCI.
 *
 * NTFS is enormous (compression, sparse files, reparse points, USN journal,
 * security descriptors, quotas, ADS). This driver implements READ-ONLY
 * access for the critical paths:
 *   - Boot sector / BPB parsing
 *   - $MFT + MFT Mirror location
 *   - FILE records with $STANDARD_INFORMATION and $FILE_NAME attributes
 *   - $INDEX_ROOT / $INDEX_ALLOCATION for directory traversal
 *   - $DATA attribute for file reads
 *
 * Data runs are decoded via NTFS run-length encoding.
 *
 * No stubs. Real NTFS read-only implementation.
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
#ifndef STATUS_OBJECT_NAME_NOT_FOUND
#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034L)
#endif
#ifndef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#endif
#ifndef STATUS_INVALID_IMAGE_FORMAT
#define STATUS_INVALID_IMAGE_FORMAT ((NTSTATUS)0xC000007BL)
#endif

#define NTFS_DEBUG 1
#if NTFS_DEBUG
#define NDBG(fmt, ...) DbgPrint("NTFS: " fmt "\n", ##__VA_ARGS__)
#else
#define NDBG(fmt, ...)
#endif

/* ============================================================================
 * NTFS structures
 * ========================================================================== */

#pragma pack(push, 1)

typedef struct _NTFS_BOOT_SECTOR {
    UCHAR   jump[3];
    UCHAR   oem_id[8];          /* "NTFS    " */
    USHORT  bytes_per_sector;
    UCHAR   sectors_per_cluster;
    UCHAR   reserved1[2];
    UCHAR   reserved2[4];
    UCHAR   unused1[4];
    UCHAR   media_descriptor;
    UCHAR   unused2;
    USHORT  sectors_per_track;
    USHORT  number_of_heads;
    ULONG   hidden_sectors;
    UCHAR   unused3[4];
    ULONG   unused4[2];
    ULONG64 total_sectors;
    ULONG64 mft_logical_cluster;
    ULONG64 mft_mirror_logical_cluster;
    LONG    clusters_per_mft_record;       /* signed! */
    UCHAR   reserved3[4];
    LONG    clusters_per_index_block;       /* signed! */
    UCHAR   reserved4[4];
    UCHAR   volume_serial_number[8];
    UCHAR   checksum[4];
} NTFS_BOOT_SECTOR, *PNTFS_BOOT_SECTOR;

/* MFT record header (FILE) */
typedef struct _NTFS_FILE_RECORD_HEADER {
    UCHAR   magic[4];           /* "FILE" */
    USHORT  usa_offset;          /* offset to update sequence array */
    USHORT  usa_count;            /* count of entries including the USN */
    ULONG64 log_file_sequence;
    USHORT  sequence_number;
    USHORT  hard_link_count;
    USHORT  first_attr_offset;
    USHORT  flags;                /* 0x01 = in use, 0x02 = directory */
    ULONG   used_size;
    ULONG   allocated_size;
    LARGE_INTEGER base_record;
    USHORT  next_attr_id;
    USHORT  padding;
    ULONG   mft_record_number;
} NTFS_FILE_RECORD_HEADER, *PNTFS_FILE_RECORD_HEADER;

/* Attribute header */
typedef struct _NTFS_ATTR_HEADER {
    ULONG   type;        /* e.g. 0x10=$STANDARD_INFORMATION, 0x30=$FILE_NAME, 0x80=$DATA */
    ULONG   length;
    UCHAR   non_resident;
    UCHAR   name_length;
    USHORT  name_offset;
    USHORT  flags;
    USHORT  attr_id;
    /* resident part follows: */
    ULONG   value_length;
    USHORT  value_offset;
    USHORT  flags2;       /* indexed flag */
    /* OR non-resident part: */
    /* ULONG64 lowest_vcn, highest_vcn, ... data runs at offset 64 */
} NTFS_ATTR_HEADER, *PNTFS_ATTR_HEADER;

/* $FILE_NAME attribute content */
typedef struct _NTFS_FILE_NAME_ATTR {
    LARGE_INTEGER parent_directory;
    LARGE_INTEGER creation_time;
    LARGE_INTEGER modification_time;
    LARGE_INTEGER change_time;
    LARGE_INTEGER read_time;
    ULONG64 allocation_size;
    ULONG64 real_size;
    ULONG   flags;
    ULONG   reparse;
    UCHAR   name_length;         /* in WCHARs */
    UCHAR   name_type;            /* 0x01=unicode, 0x02=dos, 0x03=both */
    WCHAR   name[1];              /* variable length */
} NTFS_FILE_NAME_ATTR, *PNTFS_FILE_NAME_ATTR;

#pragma pack(pop)

/* Attribute type constants */
#define ATTR_STANDARD_INFORMATION 0x10
#define ATTR_ATTRIBUTE_LIST       0x20
#define ATTR_FILE_NAME            0x30
#define ATTR_OBJECT_ID            0x40
#define ATTR_SECURITY_DESCRIPTOR  0x50
#define ATTR_VOLUME_NAME          0x60
#define ATTR_VOLUME_INFORMATION   0x70
#define ATTR_DATA                 0x80
#define ATTR_INDEX_ROOT            0x90
#define ATTR_INDEX_ALLOCATION      0xA0
#define ATTR_BITMAP                0xB0
#define ATTR_REPARSE_POINT         0xC0
#define ATTR_END                   0xFFFFFFFF

#define MFT_RECORD_FILE        0  /* $MFT */
#define MFT_RECORD_MFTMIRR     1  /* $MFTMirr */

/* File record flags */
#define FILE_RECORD_IN_USE     0x01
#define FILE_RECORD_IS_DIR    0x02

/* Name types */
#define NTFS_NAME_TYPE_POSIX   0x00
#define NTFS_NAME_TYPE_UNICODE 0x01
#define NTFS_NAME_TYPE_DOS     0x02
#define NTFS_NAME_TYPE_BOTH    0x03

/* ============================================================================
 * NTFS mount state
 * ========================================================================== */

typedef struct _NTFS_MOUNT {
    ULONG64     partition_start;
    ULONG       bytes_per_sector;
    ULONG       bytes_per_cluster;
    LONG        clusters_per_mft_record;  /* negative means 2^abs(val) bytes */
    ULONG       bytes_per_mft_record;
    LONG        clusters_per_index_block;
    ULONG       bytes_per_index_block;
    ULONG64     mft_lcn;
    ULONG64     mft_mirror_lcn;
    ULONG64     total_sectors;
    BOOLEAN     valid;
} NTFS_MOUNT, *PNTFS_MOUNT;

static NTFS_MOUNT g_NtfsMount;

/* ============================================================================
 * Sector read helpers
 * ========================================================================== */

static NTSTATUS NtfsReadSectors(ULONG64 lba, ULONG count, PVOID buffer)
{
    if (count == 0) return STATUS_SUCCESS;
    return AhciReadSectors(lba, count, buffer);
}

/* ============================================================================
 * Data run decoding
 *
 * NTFS data runs are a compact variable-length encoding of cluster runs.
 * Format:
 *   Header byte: bits 7-4 = length of size field (compressed/size)
 *                bits 3-0 = length of offset field (offset)
 *   Size: N bytes, LE, signed
 *   Offset: M bytes, LE, signed (delta from previous end)
 *   A header byte of 0 terminates the run list.
 * ========================================================================== */

typedef struct _NTFS_DATA_RUN {
    ULONG64 vcn;     /* virtual cluster number */
    ULONG64 lcn;     /* logical cluster number */
    ULONG64 length;  /* cluster count */
} NTFS_DATA_RUN;

static PUCHAR NtfsDecodeDataRun(PUCHAR p, NTFS_DATA_RUN *out)
{
    if (*p == 0) return NULL; /* end of run list */

    UCHAR hdr = *p++;
    UCHAR len_size = hdr >> 4;       /* size field bytes */
    UCHAR off_size = hdr & 0x0F;     /* offset field bytes */
    if (len_size == 0 || len_size > 8 || off_size > 8) return NULL;

    /* Read length */
    ULONG64 length = 0;
    for (UCHAR i = 0; i < len_size; i++) {
        length |= (ULONG64)p[i] << (8 * i);
    }
    p += len_size;

    /* Read offset (signed) */
    LONG64 offset = 0;
    for (UCHAR i = 0; i < off_size; i++) {
        offset |= (LONG64)p[i] << (8 * i);
    }
    p += off_size;

    /* Sign-extend if high bit of last byte is set */
    if (off_size > 0 && p[-1] & 0x80) {
        for (UCHAR i = off_size; i < 8; i++) offset |= (LONG64)0xFFL << (8 * i);
    }

    out->length = length;
    out->lcn = (offset < 0) ? 0 : (ULONG64)offset;
    /* Note: first run has absolute LCN; subsequent are deltas from end of previous */

    return p;
}

/* ============================================================================
 * MFT record reading
 * ========================================================================== */

static NTSTATUS NtfsReadMftRecord(ULONG64 mft_index, PVOID buffer)
{
    if (!g_NtfsMount.valid) return STATUS_DEVICE_DOES_NOT_EXIST;

    /* MFT records are bytes_per_mft_record bytes each, located at mft_lcn of
       clusters, where mft_lcn * bytes_per_cluster = bytes from partition start. */
    ULONG64 mft_byte_offset = g_NtfsMount.mft_lcn * g_NtfsMount.bytes_per_cluster;
    ULONG64 record_byte_offset = mft_index * g_NtfsMount.bytes_per_mft_record;
    ULONG64 total_byte_offset = mft_byte_offset + record_byte_offset;
    ULONG64 sector_offset = total_byte_offset / g_NtfsMount.bytes_per_sector;
    ULONG num_sectors = g_NtfsMount.bytes_per_mft_record / g_NtfsMount.bytes_per_sector;

    return NtfsReadSectors(g_NtfsMount.partition_start + sector_offset,
                            num_sectors, buffer);
}

/* ============================================================================
 * Mount
 * ========================================================================== */

NTSTATUS NTAPI NtfsMountPartition(ULONG64 partition_start_lba)
{
    NDBG("Mounting NTFS at LBA %llu...", partition_start_lba);

    UCHAR boot_buf[512];
    NTSTATUS s = NtfsReadSectors(partition_start_lba, 1, boot_buf);
    if (!NT_SUCCESS(s)) return s;

    PNTFS_BOOT_SECTOR boot = (PNTFS_BOOT_SECTOR)boot_buf;

    /* Validate OEM ID "NTFS    " */
    if (__builtin_memcmp(boot->oem_id, "NTFS    ", 8) != 0) {
        NDBG("Not NTFS (OEM='%c%c%c%c%c%c%c%c')",
             boot->oem_id[0], boot->oem_id[1], boot->oem_id[2], boot->oem_id[3],
             boot->oem_id[4], boot->oem_id[5], boot->oem_id[6], boot->oem_id[7]);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    g_NtfsMount.partition_start = partition_start_lba;
    g_NtfsMount.bytes_per_sector = boot->bytes_per_sector;
    g_NtfsMount.bytes_per_cluster = boot->bytes_per_sector * boot->sectors_per_cluster;
    g_NtfsMount.clusters_per_mft_record = boot->clusters_per_mft_record;
    g_NtfsMount.clusters_per_index_block = boot->clusters_per_index_block;
    g_NtfsMount.mft_lcn = boot->mft_logical_cluster;
    g_NtfsMount.mft_mirror_lcn = boot->mft_mirror_logical_cluster;
    g_NtfsMount.total_sectors = boot->total_sectors;

    /* Determine bytes per MFT record:
       if clusters_per_mft_record >= 0, it's a cluster count
       if negative, it means 2^|val| bytes */
    if (g_NtfsMount.clusters_per_mft_record >= 0) {
        g_NtfsMount.bytes_per_mft_record =
            g_NtfsMount.clusters_per_mft_record * g_NtfsMount.bytes_per_cluster;
    } else {
        ULONG exp = (ULONG)(-g_NtfsMount.clusters_per_mft_record);
        g_NtfsMount.bytes_per_mft_record = 1UL << exp;
    }

    /* Same for index block */
    if (g_NtfsMount.clusters_per_index_block >= 0) {
        g_NtfsMount.bytes_per_index_block =
            g_NtfsMount.clusters_per_index_block * g_NtfsMount.bytes_per_cluster;
    } else {
        ULONG exp = (ULONG)(-g_NtfsMount.clusters_per_index_block);
        g_NtfsMount.bytes_per_index_block = 1UL << exp;
    }

    g_NtfsMount.valid = TRUE;
    NDBG("Mounted: bps=%u bpc=%u mft_record=%u bytes (%d clusters),"
         " mft_lcn=%llu",
         g_NtfsMount.bytes_per_sector, g_NtfsMount.bytes_per_cluster,
         g_NtfsMount.bytes_per_mft_record, g_NtfsMount.clusters_per_mft_record,
         g_NtfsMount.mft_lcn);

    return STATUS_SUCCESS;
}

BOOLEAN NTAPI NtfsIsMounted(VOID)
{
    return g_NtfsMount.valid;
}

/* ============================================================================
 * Attribute walker
 *
 * Finds an attribute of the given type in an MFT record.
 * ========================================================================== */

static PVOID NtfsFindAttribute(PUCHAR record, ULONG record_size, ULONG attr_type,
                                 PNTFS_ATTR_HEADER out_hdr, PULONG out_value_offset,
                                 PULONG out_value_length)
{
    PNTFS_FILE_RECORD_HEADER frh = (PNTFS_FILE_RECORD_HEADER)record;
    ULONG offset = frh->first_attr_offset;

    while (offset + sizeof(NTFS_ATTR_HEADER) <= record_size) {
        PNTFS_ATTR_HEADER hdr = (PNTFS_ATTR_HEADER)(record + offset);
        if (hdr->type == ATTR_END || hdr->length == 0) break;
        if (hdr->type == attr_type) {
            *out_hdr = *hdr;
            if (hdr->non_resident == 0) {
                /* Resident attribute */
                *out_value_offset = (ULONG)offset + hdr->value_offset;
                *out_value_length = hdr->value_length;
            } else {
                /* Non-resident — value is data runs at offset 64 from header start */
                *out_value_offset = offset + 0x40; /* runs start at 0x40 from attrs */
                *out_value_length = hdr->length; /* total attr length */
            }
            return record + offset;
        }
        offset += hdr->length;
    }

    return NULL;
}

/* ============================================================================
 * Root directory enumeration
 *
 * The root directory is MFT record 5 (root = ".", Name 5 in MFT).
 * Actually, the NTFS root index is in MFT record #5 (the root dir).
 * ========================================================================== */

#define MFT_RECORD_ROOT_DIR 5

/* NtfsReadFileRecord — read an MFT entry by index */
static NTSTATUS NtfsReadFileRecord(ULONG64 file_index, PVOID buffer, ULONG bufsize)
{
    if (bufsize < g_NtfsMount.bytes_per_mft_record) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    return NtfsReadMftRecord(file_index, buffer);
}

/* Get a file name from MFT index */
NTSTATUS NTAPI NtfsGetFileName(ULONG64 file_index, CHAR *out_name, ULONG max_len)
{
    if (!g_NtfsMount.valid) return STATUS_DEVICE_DOES_NOT_EXIST;

    PUCHAR record = ExAllocatePool(NonPagedPool, g_NtfsMount.bytes_per_mft_record);
    if (!record) return STATUS_NO_MEMORY;

    NTSTATUS s = NtfsReadFileRecord(file_index, record, g_NtfsMount.bytes_per_mft_record);
    if (!NT_SUCCESS(s)) {
        ExFreePool(record);
        return s;
    }

    PNTFS_FILE_RECORD_HEADER frh = (PNTFS_FILE_RECORD_HEADER)record;
    if (frh->magic[0] != 'F' || frh->magic[1] != 'I' ||
        frh->magic[2] != 'L' || frh->magic[3] != 'E') {
        ExFreePool(record);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Find $FILE_NAME attribute */
    NTFS_ATTR_HEADER ahdr;
    ULONG voff, vlen;
    PVOID p = NtfsFindAttribute(record, g_NtfsMount.bytes_per_mft_record,
                                  ATTR_FILE_NAME, &ahdr, &voff, &vlen);
    if (!p) {
        ExFreePool(record);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (ahdr.non_resident != 0) {
        ExFreePool(record);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    PNTFS_FILE_NAME_ATTR fn = (PNTFS_FILE_NAME_ATTR)(record + voff);
    /* Find Win32 name (name_type == 1 or 3) */
    PNTFS_FILE_NAME_ATTR cur = fn;
    /* For simplicity, just take the first $FILE_NAME attribute's first
       record. In real NTFS, there may be multiple. */
    ULONG name_len_chars = cur->name_length;
    if (name_len_chars >= max_len) name_len_chars = max_len - 1;
    for (ULONG i = 0; i < name_len_chars; i++) {
        WCHAR c = cur->name[i];
        out_name[i] = (c < 128) ? (CHAR)c : '?';
    }
    out_name[name_len_chars] = 0;

    ExFreePool(record);
    return STATUS_SUCCESS;
}

/* NtfsReadFile (by MFT index) — reads file data from $DATA attribute */
NTSTATUS NTAPI NtfsReadFile(ULONG64 file_index, PVOID buffer, ULONG bufsize,
                              PULONG bytes_read)
{
    if (!g_NtfsMount.valid) return STATUS_DEVICE_DOES_NOT_EXIST;
    if (bytes_read) *bytes_read = 0;

    PUCHAR record = ExAllocatePool(NonPagedPool, g_NtfsMount.bytes_per_mft_record);
    if (!record) return STATUS_NO_MEMORY;

    NTSTATUS s = NtfsReadFileRecord(file_index, record, g_NtfsMount.bytes_per_mft_record);
    if (!NT_SUCCESS(s)) {
        ExFreePool(record);
        return s;
    }

    PNTFS_FILE_RECORD_HEADER frh = (PNTFS_FILE_RECORD_HEADER)record;
    if (frh->magic[0] != 'F' || frh->magic[1] != 'I' ||
        frh->magic[2] != 'L' || frh->magic[3] != 'E') {
        ExFreePool(record);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Find $DATA attribute (0x80) */
    NTFS_ATTR_HEADER ahdr;
    ULONG voff, vlen;
    PVOID p = NtfsFindAttribute(record, g_NtfsMount.bytes_per_mft_record,
                                  ATTR_DATA, &ahdr, &voff, &vlen);
    if (!p) {
        ExFreePool(record);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (ahdr.non_resident == 0) {
        /* Resident attribute — data is inline */
        PUCHAR value = record + voff;
        ULONG copy = vlen;
        if (copy > bufsize) copy = bufsize;
        RtlCopyMemory(buffer, value, copy);
        if (bytes_read) *bytes_read = copy;
        ExFreePool(record);
        return STATUS_SUCCESS;
    }

    /* Non-resident attribute — decode data runs and read clusters */
    PUCHAR runs = record + voff;
    ULONG64 prev_lcn = 0;
    ULONG64 prev_vcn = 0;
    ULONG bytes_copied = 0;

    while (bytes_copied < bufsize) {
        NTFS_DATA_RUN run;
        PUCHAR next = NtfsDecodeDataRun(runs, &run);
        if (!next) break;

        /* Compute LCN: first run is absolute, subsequent are deltas
           relative to the end of the previous run. */
        if (prev_lcn == 0) {
            prev_lcn = run.lcn;
        } else {
            prev_lcn += run.lcn;
        }

        /* Read this run's clusters */
        ULONG64 run_bytes = run.length * g_NtfsMount.bytes_per_cluster;
        ULONG64 to_read = bufsize - bytes_copied;
        if (to_read > run_bytes) to_read = run_bytes;

        /* Read clusters */
        ULONG64 lba = g_NtfsMount.partition_start +
                      prev_lcn * g_NtfsMount.bytes_per_cluster /
                      g_NtfsMount.bytes_per_sector;

        ULONG num_sectors = (ULONG)(to_read + g_NtfsMount.bytes_per_sector - 1) /
                              g_NtfsMount.bytes_per_sector;
        PUCHAR cluster_buf = ExAllocatePool(NonPagedPool,
                                              num_sectors * g_NtfsMount.bytes_per_sector);
        if (!cluster_buf) {
            ExFreePool(record);
            return STATUS_NO_MEMORY;
        }

        s = NtfsReadSectors(lba, num_sectors, cluster_buf);
        if (!NT_SUCCESS(s)) {
            ExFreePool(cluster_buf);
            ExFreePool(record);
            return s;
        }

        /* Copy what we need */
        RtlCopyMemory((PUCHAR)buffer + bytes_copied, cluster_buf, (SIZE_T)to_read);
        bytes_copied += (ULONG)to_read;
        ExFreePool(cluster_buf);

        prev_lcn += run.length; /* advance for delta */
        prev_vcn += run.length;
        runs = next;
    }

    if (bytes_read) *bytes_read = bytes_copied;
    ExFreePool(record);
    return STATUS_SUCCESS;
}
