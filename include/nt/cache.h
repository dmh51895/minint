/* MinNT - nt/cache.h — Cache Manager API */
#ifndef _CACHE_H_
#define _CACHE_H_
#include <nt/ntdef.h>
VOID NTAPI CcInitSystem(VOID);
NTSTATUS NTAPI CcReadSector(ULONG64 lba, PVOID buffer,
                              NTSTATUS (*underlying_read)(ULONG64, ULONG, PVOID));
VOID NTAPI CcFlushAll(VOID);
VOID NTAPI CcInvalidate(VOID);
VOID NTAPI CcGetStats(PULONG hits, PULONG misses, PULONG evictions);
#endif
