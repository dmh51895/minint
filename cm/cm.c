/*
 * MinNT - cm/cm.c
 * Configuration Manager: in-memory registry hive.
 * Tree of CM_KEY_NODEs with CM_KEY_VALUEs, case-insensitive lookup,
 * enough for SMSS/CSRSS to read boot configuration keys.
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/cm.h>
#include <nt/rtl.h>

static CM_KEY_NODE CmRootKey;
static KSPIN_LOCK CmLock;

/* ---- Internal helpers ---------------------------------------------------- */

static PCM_KEY_NODE CmAllocateKeyNode(PUNICODE_STRING Name)
{
    PCM_KEY_NODE node;
    PWCHAR nameBuf;

    node = ExAllocatePoolWithTag(NonPagedPool, sizeof(CM_KEY_NODE), TAG_PROC);
    if (!node) return NULL;

    nameBuf = ExAllocatePoolWithTag(NonPagedPool,
                                    Name->Length + sizeof(WCHAR), TAG_NAME);
    if (!nameBuf) {
        ExFreePoolWithTag(node, TAG_PROC);
        return NULL;
    }

    RtlCopyMemory(nameBuf, Name->Buffer, Name->Length);
    nameBuf[Name->Length / sizeof(WCHAR)] = L'\0';

    node->Type = CM_NODE_KEY;
    node->Name.Length = Name->Length;
    node->Name.MaximumLength = Name->Length + sizeof(WCHAR);
    node->Name.Buffer = nameBuf;
    node->Parent = NULL;
    node->ChildHead = NULL;
    node->NextSibling = NULL;
    node->SubKeyCount = 0;
    node->ValueCount = 0;
    InitializeListHead(&node->ValueListHead);

    return node;
}

static PCM_KEY_NODE CmFindChild(PCM_KEY_NODE Parent, PCUNICODE_STRING Name)
{
    PCM_KEY_NODE child = Parent->ChildHead;
    while (child) {
        if (RtlEqualUnicodeString(&child->Name, Name, TRUE))
            return child;
        child = child->NextSibling;
    }
    return NULL;
}

/* ---- Init --------------------------------------------------------------- */

NTSTATUS NTAPI CmInitSystem(VOID)
{
    KeInitializeSpinLock(&CmLock);

    RtlZeroMemory(&CmRootKey, sizeof(CmRootKey));
    CmRootKey.Type = CM_NODE_KEY;
    CmRootKey.Name.Buffer = L"\\";
    CmRootKey.Name.Length = sizeof(L"\\") - sizeof(WCHAR);
    CmRootKey.Name.MaximumLength = sizeof(L"\\");
    CmRootKey.SubKeyCount = 0;
    CmRootKey.ValueCount = 0;
    InitializeListHead(&CmRootKey.ValueListHead);

    DbgPrint("CM: registry root key initialized\n");
    return STATUS_SUCCESS;
}

/* ---- Create key --------------------------------------------------------- */

NTSTATUS NTAPI CmCreateKey(PUNICODE_STRING KeyName,
                           ULONG CreateOptions,
                           PCM_KEY_NODE *OutKey)
{
    PCM_KEY_NODE parent, node;
    UNICODE_STRING remaining, segment;
    USHORT i;

    UNREFERENCED_PARAMETER(CreateOptions);

    KIRQL _irql; KeAcquireSpinLock(&CmLock, &_irql);

    /* Start at root, parse path segments separated by '\\' */
    parent = &CmRootKey;
    remaining = *KeyName;

    /* Skip leading backslash */
    if (remaining.Length > 0 && remaining.Buffer[0] == L'\\') {
        remaining.Buffer++;
        remaining.Length -= sizeof(WCHAR);
    }

    while (remaining.Length > 0) {
        /* Find next backslash or end of string */
        segment.Length = 0;
        segment.Buffer = remaining.Buffer;
        segment.MaximumLength = remaining.MaximumLength;

        for (i = 0; i < remaining.Length / sizeof(WCHAR); i++) {
            if (remaining.Buffer[i] == L'\\') {
                segment.Length = i * sizeof(WCHAR);
                break;
            }
        }
        if (segment.Length == 0)
            segment.Length = remaining.Length;

        /* Find or create child */
        node = CmFindChild(parent, &segment);
        if (!node) {
            node = CmAllocateKeyNode(&segment);
            if (!node) {
                KeReleaseSpinLock(&CmLock, 0);
                return STATUS_NO_MEMORY;
            }
            node->Parent = parent;

            /* Link into parent's child list */
            if (parent->ChildHead) {
                /* Append to end */
                PCM_KEY_NODE last = parent->ChildHead;
                while (last->NextSibling)
                    last = last->NextSibling;
                last->NextSibling = node;
            } else {
                parent->ChildHead = node;
            }
            parent->SubKeyCount++;
        }

        parent = node;

        /* Advance past this segment */
        if (i < remaining.Length / sizeof(WCHAR)) {
            remaining.Buffer += i + 1;
            remaining.Length -= (i + 1) * sizeof(WCHAR);
        } else {
            break;
        }
    }

    KeReleaseSpinLock(&CmLock, 0);

    *OutKey = parent;
    return STATUS_SUCCESS;
}

/* ---- Open key ----------------------------------------------------------- */

NTSTATUS NTAPI CmOpenKey(PUNICODE_STRING KeyName,
                         ULONG DesiredAccess,
                         PCM_KEY_NODE *OutKey)
{
    UNREFERENCED_PARAMETER(DesiredAccess);
    return CmCreateKey(KeyName, 0, OutKey);
}

/* ---- Set value ---------------------------------------------------------- */

NTSTATUS NTAPI CmSetValue(PCM_KEY_NODE Key,
                          PUNICODE_STRING ValueName,
                          ULONG DataType,
                          PVOID Data,
                          ULONG DataLength)
{
    PCM_KEY_VALUE value;
    PVOID dataBuf;

    if (!Key || !ValueName) return STATUS_INVALID_PARAMETER;

    dataBuf = ExAllocatePoolWithTag(NonPagedPool, DataLength, TAG_NAME);
    if (!dataBuf) return STATUS_NO_MEMORY;

    value = ExAllocatePoolWithTag(NonPagedPool, sizeof(CM_KEY_VALUE), TAG_PROC);
    if (!value) {
        ExFreePoolWithTag(dataBuf, TAG_NAME);
        return STATUS_NO_MEMORY;
    }

    RtlCopyMemory(dataBuf, Data, DataLength);

    value->Type = CM_NODE_VALUE;
    value->Name = *ValueName;
    value->DataType = DataType;
    value->DataLength = DataLength;
    value->Data = dataBuf;

    KIRQL _irql; KeAcquireSpinLock(&CmLock, &_irql);
    InsertTailList(&Key->ValueListHead, &value->ValueListEntry);
    Key->ValueCount++;
    KeReleaseSpinLock(&CmLock, 0);

    return STATUS_SUCCESS;
}

/* ---- Query value -------------------------------------------------------- */

NTSTATUS NTAPI CmQueryValue(PCM_KEY_NODE Key,
                            PUNICODE_STRING ValueName,
                            PULONG OutDataType,
                            PVOID OutData,
                            ULONG OutDataLength,
                            PULONG OutActualLength)
{
    PLIST_ENTRY e;
    PCM_KEY_VALUE value;

    if (!Key) return STATUS_INVALID_PARAMETER;

    KIRQL _irql; KeAcquireSpinLock(&CmLock, &_irql);

    for (e = Key->ValueListHead.Flink;
         e != &Key->ValueListHead;
         e = e->Flink) {

        value = CONTAINING_RECORD(e, CM_KEY_VALUE, ValueListEntry);
        if (RtlEqualUnicodeString(&value->Name, ValueName, TRUE)) {
            if (OutDataType) *OutDataType = value->DataType;
            if (OutActualLength) *OutActualLength = value->DataLength;

            if (OutData && OutDataLength >= value->DataLength) {
                RtlCopyMemory(OutData, value->Data, value->DataLength);
            }

            KeReleaseSpinLock(&CmLock, 0);
            return STATUS_SUCCESS;
        }
    }

    KeReleaseSpinLock(&CmLock, 0);
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/* ---- Enumerate subkeys -------------------------------------------------- */

NTSTATUS NTAPI CmEnumerateSubKey(PCM_KEY_NODE Key,
                                 ULONG Index,
                                 PUNICODE_STRING OutName)
{
    PCM_KEY_NODE child;
    ULONG i;

    if (!Key) return STATUS_INVALID_PARAMETER;

    KIRQL _irql; KeAcquireSpinLock(&CmLock, &_irql);

    child = Key->ChildHead;
    for (i = 0; child && i < Index; i++)
        child = child->NextSibling;

    if (!child) {
        KeReleaseSpinLock(&CmLock, 0);
        return STATUS_NO_MORE_ENTRIES;
    }

    *OutName = child->Name;
    KeReleaseSpinLock(&CmLock, 0);
    return STATUS_SUCCESS;
}

/* ---- Enumerate values --------------------------------------------------- */

NTSTATUS NTAPI CmEnumerateValue(PCM_KEY_NODE Key,
                                ULONG Index,
                                PUNICODE_STRING OutName,
                                PULONG OutDataType,
                                PVOID OutData,
                                ULONG OutDataLength)
{
    PLIST_ENTRY e;
    PCM_KEY_VALUE value;
    ULONG i;

    if (!Key) return STATUS_INVALID_PARAMETER;

    KIRQL _irql; KeAcquireSpinLock(&CmLock, &_irql);

    e = Key->ValueListHead.Flink;
    for (i = 0; e != &Key->ValueListHead && i < Index; i++)
        e = e->Flink;

    if (e == &Key->ValueListHead) {
        KeReleaseSpinLock(&CmLock, 0);
        return STATUS_NO_MORE_ENTRIES;
    }

    value = CONTAINING_RECORD(e, CM_KEY_VALUE, ValueListEntry);
    if (OutName) *OutName = value->Name;
    if (OutDataType) *OutDataType = value->DataType;
    if (OutData && OutDataLength >= value->DataLength)
        RtlCopyMemory(OutData, value->Data, value->DataLength);

    KeReleaseSpinLock(&CmLock, 0);
    return STATUS_SUCCESS;
}

/* ---- Get root key ------------------------------------------------------- */

PCM_KEY_NODE NTAPI CmGetRootKey(VOID)
{
    return &CmRootKey;
}
