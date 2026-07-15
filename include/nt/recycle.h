/*
 * MinNT - include/nt/recycle.h
 * Recycle Bin file system service.
 */

#ifndef _RECYCLE_H_
#define _RECYCLE_H_

NTSTATUS NTAPI RecycleBinInit(VOID);
NTSTATUS NTAPI RecycleBinSend(PCWSTR OriginalPath, ULONG64 FileSize);
NTSTATUS NTAPI RecycleBinRestore(ULONG Index);
NTSTATUS NTAPI RecycleBinErase(ULONG Index);
NTSTATUS NTAPI RecycleBinEmpty(VOID);
ULONG    NTAPI RecycleBinEnum(ULONG MaxCount, PCHAR *pOriginalPaths,
                                PULONG64 pDeletionTimes, PULONG64 pFileSizes);
ULONG    NTAPI RecycleBinGetCount(VOID);
ULONG64  NTAPI RecycleBinGetTotalSize(VOID);

#endif /* _RECYCLE_H_ */
