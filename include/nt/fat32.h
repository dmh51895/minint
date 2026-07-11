/*
 * MinNT - nt/fat32.h
 * FAT32 filesystem driver API.
 */

#ifndef _FAT32_H_
#define _FAT32_H_

#include <nt/ntdef.h>

NTSTATUS NTAPI Fat32MountPartition(ULONG64 partition_start_lba);
BOOLEAN  NTAPI Fat32IsMounted(VOID);
NTSTATUS NTAPI Fat32ReadFile(const CHAR *name, PVOID buffer, ULONG bufsize, PULONG bytes_read);
NTSTATUS NTAPI Fat32ListDirectory(ULONG dir_cluster, PVOID entries, ULONG max_entries, PULONG out_count);
NTSTATUS NTAPI Fat32ListDirectory(ULONG dir_cluster, PVOID entries, ULONG max_entries, PULONG out_count);
ULONG    NTAPI Fat32GetRootCluster(VOID);
NTSTATUS NTAPI Fat32WriteFile(const CHAR *name, const VOID *buffer, ULONG bufsize, PULONG bytes_written);

#endif /* _FAT32_H_ */
