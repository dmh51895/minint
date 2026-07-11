/*
 * MinNT - ex.h
 * Executive support: pool allocator with real pool tags, so BAD_POOL_HEADER
 * means something and !pool-style debugging is possible later.
 */

#ifndef _EX_H_
#define _EX_H_

#include <nt/ntdef.h>

typedef enum _POOL_TYPE {
    NonPagedPool = 0,
    PagedPool    = 1,     /* accepted, treated as NonPaged (no pageout yet) */
} POOL_TYPE;

/* Classic tags read backwards in memory: 'Proc' = 0x636F7250 */
#define TAG_PROC  0x636F7250  /* 'Proc' */
#define TAG_THRD  0x64726854  /* 'Thrd' */
#define TAG_OBJT  0x744A624F  /* 'ObJt' */
#define TAG_OBHD  0x64484F62  /* 'bOHd' */
#define TAG_HTAB  0x62617448  /* 'Htab' */
#define TAG_NAME  0x656D614E  /* 'Name' */
#define TAG_SECT  0x74636553  /* 'Sect' */

NTSTATUS NTAPI ExInitializePoolManager(VOID);
PVOID    NTAPI ExAllocatePoolWithTag(POOL_TYPE PoolType, SIZE_T NumberOfBytes, ULONG Tag);
VOID     NTAPI ExFreePoolWithTag(PVOID P, ULONG Tag);
#define  ExFreePool(P) ExFreePoolWithTag((P), 0)
#define  ExAllocatePool(PoolType, Size) ExAllocatePoolWithTag((PoolType), (Size), 0)

#endif /* _EX_H_ */
