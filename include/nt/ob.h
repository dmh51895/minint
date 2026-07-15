/*
 * MinNT - ob.h
 * Object Manager: every kernel object (process, thread, event, ...) lives
 * behind an OBJECT_HEADER exactly like real NT, where the body pointer you
 * hand out is (Header + 1).
 */

#ifndef _OB_H_
#define _OB_H_

#include <nt/ntdef.h>

typedef struct _OBJECT_TYPE {
    UNICODE_STRING Name;                /* "Process", "Thread", ...        */
    ULONG   Key;                        /* pool tag for bodies             */
    ULONG64 TotalNumberOfObjects;
    ULONG64 TotalNumberOfHandles;
    VOID  (NTAPI *DeleteProcedure)(PVOID Body);   /* on last deref         */
    LIST_ENTRY TypeListEntry;           /* chained into ObpTypeListHead    */
} OBJECT_TYPE, *POBJECT_TYPE;

typedef struct _OBJECT_HEADER {
    LONG64  PointerCount;               /* references                      */
    LONG64  HandleCount;                /* open handles                    */
    POBJECT_TYPE Type;
    UNICODE_STRING Name;                /* optional; Length==0 if unnamed  */
    LIST_ENTRY NamedListEntry;          /* flat namespace for now; David   */
                                        /* patches in \ directory objects  */
    /* object body follows immediately */
} OBJECT_HEADER, *POBJECT_HEADER;

#define BODY_TO_HEADER(Body)  (((POBJECT_HEADER)(Body)) - 1)
#define HEADER_TO_BODY(Hdr)   ((PVOID)(((POBJECT_HEADER)(Hdr)) + 1))

/* ---- API ---------------------------------------------------------------- */

NTSTATUS NTAPI ObInitSystem(VOID);

NTSTATUS NTAPI ObCreateObjectType(PCUNICODE_STRING TypeName,
                                  ULONG PoolTag,
                                  VOID (NTAPI *DeleteProcedure)(PVOID),
                                  POBJECT_TYPE *OutType);

NTSTATUS NTAPI ObCreateObject(POBJECT_TYPE Type,
                              SIZE_T BodySize,
                              PCUNICODE_STRING Name OPTIONAL,
                              PVOID *OutBody);

VOID NTAPI ObReferenceObject(PVOID Body);
VOID NTAPI ObDereferenceObject(PVOID Body);

NTSTATUS NTAPI ObInsertHandle(PVOID Body, PHANDLE OutHandle);
NTSTATUS NTAPI ObReferenceObjectByHandle(HANDLE Handle,
                                         POBJECT_TYPE ExpectedType OPTIONAL,
                                         PVOID *OutBody);
NTSTATUS NTAPI ObCloseHandle(HANDLE Handle);

NTSTATUS NTAPI ObLookupObjectByName(PCUNICODE_STRING Name, PVOID *OutBody);

#endif /* _OB_H_ */
