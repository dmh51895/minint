/* MinNT - nt/cmpers.h — Registry persistence API */
#ifndef _CMPERS_H_
#define _CMPERS_H_
#include <nt/ntdef.h>
NTSTATUS NTAPI CmSaveHive(VOID);
NTSTATUS NTAPI CmLoadHive(VOID);
#endif
