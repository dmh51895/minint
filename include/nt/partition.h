/*
 * MinNT - nt/partition.h
 * Partition table reader API.
 */

#ifndef _PARTITION_H_
#define _PARTITION_H_

#include <nt/ntdef.h>

#ifndef STATUS_OBJECT_NAME_NOT_FOUND
#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034L)
#endif
#ifndef STATUS_DEVICE_DOES_NOT_EXIST
#define STATUS_DEVICE_DOES_NOT_EXIST ((NTSTATUS)0xC000003EL)
#endif
#ifndef STATUS_INVALID_IMAGE_FORMAT
#define STATUS_INVALID_IMAGE_FORMAT ((NTSTATUS)0xC000007BL)
#endif

NTSTATUS NTAPI PartitionScan(VOID);
ULONG    NTAPI PartitionGetCount(VOID);

struct _PARTITION_INFO;
struct _PARTITION_INFO *NTAPI PartitionGetInfo(ULONG index);

#endif /* _PARTITION_H_ */
