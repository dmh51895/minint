/*
 * MinNT - nt/ahci.h
 * AHCI SATA disk driver API.
 */

#ifndef _AHCI_NT_H_
#define _AHCI_NT_H_

#include <nt/ntdef.h>

NTSTATUS NTAPI AhciInitSystem(VOID);
NTSTATUS NTAPI AhciReadSectors(ULONG64 lba, ULONG count, PVOID buffer);
NTSTATUS NTAPI AhciWriteSectors(ULONG64 lba, ULONG count, const void *buffer);
ULONG    NTAPI AhciGetTotalSectors(VOID);
BOOLEAN  NTAPI AhciIsPresent(VOID);

#endif /* _AHCI_NT_H_ */
