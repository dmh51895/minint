/*
 * MinNT - include/nt/comreg.h
 * COM registration database - kernel-side class factories.
 */

#ifndef _COMREG_H_
#define _COMREG_H_

#include <nt/ntdef.h>
#include <nt/dispatcher.h>  /* for GUID */

#ifndef S_OK
#define S_OK                 0
#endif
#ifndef E_NOINTERFACE
#define E_NOINTERFACE        0x80004002
#endif
#ifndef CLASS_E_NOAGGREGATION
#define CLASS_E_NOAGGREGATION 0x80040110
#endif
#ifndef CLASS_E_CLASSNOTREG
#define CLASS_E_CLASSNOTREG   0x80040100
#endif
#ifndef REGDB_E_KEYMISSING
#define REGDB_E_KEYMISSING    0x80040116
#endif

#ifndef CLSCTX_INPROC_SERVER
#define CLSCTX_INPROC_SERVER  0x1
#endif
#ifndef CLSCTX_LOCAL_SERVER
#define CLSCTX_LOCAL_SERVER   0x4
#endif

#ifndef COINIT_APARTMENTTHREADED
#define COINIT_APARTMENTTHREADED 0x2
#endif

NTSTATUS NTAPI ComRegisterInit(VOID);
NTSTATUS NTAPI CoRegisterClassObject(GUID *Clsid, PVOID OuterUnknown,
                                       PVOID (*CreateInstance)(PVOID, GUID *),
                                       ULONG Flags, ULONG RegFlags);
NTSTATUS NTAPI CoRevokeClassObject(GUID *Clsid);
NTSTATUS NTAPI CoCreateInstance(GUID *Clsid, PVOID OuterUnknown,
                                  GUID *InterfaceId, PVOID *ppv);
NTSTATUS NTAPI ComProgIdToClsid(const CHAR *ProgId, GUID *OutClsid);
NTSTATUS NTAPI ComRegisterProgId(const CHAR *ProgId, GUID *Clsid);
ULONG    NTAPI ComEnumClasses(ULONG MaxCount, PCHAR *pNames, GUID *pClsids);

#endif /* _COMREG_H_ */
