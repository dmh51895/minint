/* MinNT - nt/ntfs.h — NTFS filesystem API */
#ifndef _NTFS_H_
#define _NTFS_H_
#include <nt/ntdef.h>
NTSTATUS NTAPI NtfsMountPartition(ULONG64 partition_start_lba);
BOOLEAN  NTAPI NtfsIsMounted(VOID);
NTSTATUS NTAPI NtfsGetFileName(ULONG64 file_index, CHAR *out_name, ULONG max_len);
NTSTATUS NTAPI NtfsReadFile(ULONG64 file_index, PVOID buffer, ULONG bufsize, PULONG bytes_read);
#endif
