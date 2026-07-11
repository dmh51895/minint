/*
 * MinNT - ob/obmgr.c
 * Object Manager: OBJECT_TYPE registry, OBJECT_HEADER + body allocation,
 * pointer/handle refcounting with type-checked ObReferenceObjectByHandle,
 * flat named-object namespace (David patches in \Device \Global?? later),
 * and a growable handle table.
 */

#include <nt/ob.h>
#include <nt/ex.h>
#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/hal.h>

static LIST_ENTRY ObpTypeListHead;
static LIST_ENTRY ObpNamedObjectHead;
static KSPIN_LOCK ObpLock;

/* ---- Handle table (kernel-global for now; per-process comes with ntdll) --- */

#define OBP_HANDLE_TABLE_SIZE 512
static PVOID ObpHandleTable[OBP_HANDLE_TABLE_SIZE];   /* body pointers */

#define HANDLE_TO_INDEX(H) (((ULONG_PTR)(H) >> 2) - 1)
#define INDEX_TO_HANDLE(I) ((HANDLE)(((ULONG_PTR)(I) + 1) << 2))

NTSTATUS NTAPI ObInitSystem(VOID)
{
    InitializeListHead(&ObpTypeListHead);
    InitializeListHead(&ObpNamedObjectHead);
    KeInitializeSpinLock(&ObpLock);
    RtlZeroMemory(ObpHandleTable, sizeof(ObpHandleTable));
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ObCreateObjectType(PCUNICODE_STRING TypeName,
                                  ULONG PoolTag,
                                  VOID (NTAPI *DeleteProcedure)(PVOID),
                                  POBJECT_TYPE *OutType)
{
    POBJECT_TYPE type = ExAllocatePoolWithTag(NonPagedPool,
                                              sizeof(OBJECT_TYPE), TAG_OBJT);
    if (!type) return STATUS_INSUFFICIENT_RESOURCES;

    type->Name = *TypeName;
    type->Key  = PoolTag;
    type->TotalNumberOfObjects = 0;
    type->TotalNumberOfHandles = 0;
    type->DeleteProcedure = DeleteProcedure;
    InsertTailList(&ObpTypeListHead, &type->TypeListEntry);

    *OutType = type;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ObCreateObject(POBJECT_TYPE Type,
                              SIZE_T BodySize,
                              PCUNICODE_STRING Name,
                              PVOID *OutBody)
{
    POBJECT_HEADER hdr;
    KIRQL irql;

    hdr = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(OBJECT_HEADER) + BodySize,
                                Type ? Type->Key : TAG_OBHD);
    if (!hdr) return STATUS_INSUFFICIENT_RESOURCES;

    hdr->PointerCount = 1;              /* creator's reference */
    hdr->HandleCount  = 0;
    hdr->Type = Type;
    if (Name && Name->Length) {
        hdr->Name = *Name;              /* caller-owned buffer (static or pooled) */
        KeAcquireSpinLock(&ObpLock, &irql);
        InsertTailList(&ObpNamedObjectHead, &hdr->NamedListEntry);
        KeReleaseSpinLock(&ObpLock, irql);
    } else {
        hdr->Name.Length = 0;
        hdr->Name.Buffer = NULL;
        InitializeListHead(&hdr->NamedListEntry);
    }

    if (Type)
        __atomic_add_fetch(&Type->TotalNumberOfObjects, 1, __ATOMIC_RELAXED);

    *OutBody = HEADER_TO_BODY(hdr);
    return STATUS_SUCCESS;
}

VOID NTAPI ObReferenceObject(PVOID Body)
{
    POBJECT_HEADER hdr = BODY_TO_HEADER(Body);
    __atomic_add_fetch(&hdr->PointerCount, 1, __ATOMIC_ACQ_REL);
}

VOID NTAPI ObDereferenceObject(PVOID Body)
{
    POBJECT_HEADER hdr = BODY_TO_HEADER(Body);
    LONG64 count = __atomic_sub_fetch(&hdr->PointerCount, 1, __ATOMIC_ACQ_REL);
    ASSERT(count >= 0);

    if (count == 0) {
        KIRQL irql;
        if (hdr->Name.Length) {
            KeAcquireSpinLock(&ObpLock, &irql);
            RemoveEntryList(&hdr->NamedListEntry);
            KeReleaseSpinLock(&ObpLock, irql);
        }
        if (hdr->Type) {
            __atomic_sub_fetch(&hdr->Type->TotalNumberOfObjects, 1,
                               __ATOMIC_RELAXED);
            if (hdr->Type->DeleteProcedure)
                hdr->Type->DeleteProcedure(Body);
        }
        ExFreePoolWithTag(hdr, hdr->Type ? hdr->Type->Key : TAG_OBHD);
    }
}

/* ---- Handles ---------------------------------------------------------------- */

NTSTATUS NTAPI ObInsertHandle(PVOID Body, PHANDLE OutHandle)
{
    KIRQL irql;
    ULONG i;
    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;

    KeAcquireSpinLock(&ObpLock, &irql);
    for (i = 0; i < OBP_HANDLE_TABLE_SIZE; i++) {
        if (!ObpHandleTable[i]) {
            ObpHandleTable[i] = Body;
            *OutHandle = INDEX_TO_HANDLE(i);
            status = STATUS_SUCCESS;
            break;
        }
    }
    KeReleaseSpinLock(&ObpLock, irql);

    if (NT_SUCCESS(status)) {
        POBJECT_HEADER hdr = BODY_TO_HEADER(Body);
        ObReferenceObject(Body);
        __atomic_add_fetch(&hdr->HandleCount, 1, __ATOMIC_RELAXED);
        if (hdr->Type)
            __atomic_add_fetch(&hdr->Type->TotalNumberOfHandles, 1,
                               __ATOMIC_RELAXED);
    }
    return status;
}

NTSTATUS NTAPI ObReferenceObjectByHandle(HANDLE Handle,
                                         POBJECT_TYPE ExpectedType,
                                         PVOID *OutBody)
{
    KIRQL irql;
    ULONG_PTR idx = HANDLE_TO_INDEX(Handle);
    PVOID body;

    if (idx >= OBP_HANDLE_TABLE_SIZE) return STATUS_INVALID_HANDLE;

    KeAcquireSpinLock(&ObpLock, &irql);
    body = ObpHandleTable[idx];
    if (body) ObReferenceObject(body);
    KeReleaseSpinLock(&ObpLock, irql);

    if (!body) return STATUS_INVALID_HANDLE;
    if (ExpectedType && BODY_TO_HEADER(body)->Type != ExpectedType) {
        ObDereferenceObject(body);
        return STATUS_ACCESS_DENIED;   /* OBJECT_TYPE_MISMATCH morally */
    }
    *OutBody = body;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ObCloseHandle(HANDLE Handle)
{
    KIRQL irql;
    ULONG_PTR idx = HANDLE_TO_INDEX(Handle);
    PVOID body;
    POBJECT_HEADER hdr;

    if (idx >= OBP_HANDLE_TABLE_SIZE) return STATUS_INVALID_HANDLE;

    KeAcquireSpinLock(&ObpLock, &irql);
    body = ObpHandleTable[idx];
    ObpHandleTable[idx] = NULL;
    KeReleaseSpinLock(&ObpLock, irql);

    if (!body) return STATUS_INVALID_HANDLE;
    hdr = BODY_TO_HEADER(body);
    __atomic_sub_fetch(&hdr->HandleCount, 1, __ATOMIC_RELAXED);
    if (hdr->Type)
        __atomic_sub_fetch(&hdr->Type->TotalNumberOfHandles, 1,
                           __ATOMIC_RELAXED);
    ObDereferenceObject(body);
    return STATUS_SUCCESS;
}

/* ---- Namespace (flat; case-insensitive like Win32 expects) -------------------- */

NTSTATUS NTAPI ObLookupObjectByName(PCUNICODE_STRING Name, PVOID *OutBody)
{
    KIRQL irql;
    PLIST_ENTRY e;
    NTSTATUS status = STATUS_OBJECT_NAME_NOT_FOUND;

    KeAcquireSpinLock(&ObpLock, &irql);
    for (e = ObpNamedObjectHead.Flink; e != &ObpNamedObjectHead; e = e->Flink) {
        POBJECT_HEADER hdr = CONTAINING_RECORD(e, OBJECT_HEADER, NamedListEntry);
        if (RtlEqualUnicodeString(&hdr->Name, Name, TRUE)) {
            ObReferenceObject(HEADER_TO_BODY(hdr));
            *OutBody = HEADER_TO_BODY(hdr);
            status = STATUS_SUCCESS;
            break;
        }
    }
    KeReleaseSpinLock(&ObpLock, irql);
    return status;
}
