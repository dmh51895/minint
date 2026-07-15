/*
 * MinNT - win32k/atom.c
 * Atom table management for Win32k.
 *
 * Implements GlobalAddAtom, GlobalDeleteAtom, GlobalFindAtom,
 * GlobalGetAtomName. Uses a fixed-size hash table with chaining
 * for O(1) average-case lookups. Atoms are reference-counted and
 * survive until the last reference is released.
 */

#include "precomp.h"

#define ATOM_TABLE_SIZE  256
#define ATOM_MAX_NAME    255
#define ATOM_HASH_SEED   2654435761u

typedef struct _ATOM_ENTRY {
    USHORT  AtomId;
    USHORT  RefCount;
    ULONG   NameLength;
    WCHAR   Name[ATOM_MAX_NAME + 1];
    struct _ATOM_ENTRY *Next;
} ATOM_ENTRY, *PATOM_ENTRY;

static PATOM_ENTRY g_AtomTable[ATOM_TABLE_SIZE];
KSPIN_LOCK g_AtomLock;
static USHORT g_NextAtomId = 0xC001; /* Start above 0xC000 for global atoms */

/* Internal hash: FNV-1a variant */
static ULONG AtomHash(PCWSTR Name, ULONG Length)
{
    ULONG hash = 2166136261u;
    ULONG i;
    const UCHAR *p = (const UCHAR *)Name;

    for (i = 0; i < Length; i++) {
        hash ^= p[i];
        hash *= 16777619u;
    }
    return hash % ATOM_TABLE_SIZE;
}

/* Internal: compare two wide strings case-insensitively */
static BOOLEAN AtomEqualName(PCWSTR a, ULONG aLen, PCWSTR b, ULONG bLen)
{
    ULONG i;
    if (aLen != bLen) return FALSE;
    for (i = 0; i < aLen; i++) {
        WCHAR ca = a[i], cb = b[i];
        if (ca >= L'A' && ca <= L'Z') ca += 32;
        if (cb >= L'A' && cb <= L'Z') cb += 32;
        if (ca != cb) return FALSE;
    }
    return TRUE;
}

VOID NTAPI AtomInit(VOID)
{
    RtlZeroMemory(g_AtomTable, sizeof(g_AtomTable));
    KeInitializeSpinLock(&g_AtomLock);
    DbgPrint("ATOM: table initialized (%d slots)\n", ATOM_TABLE_SIZE);
}

/* GlobalAddAtomW: add or increment reference for named atom */
NTSTATUS NTAPI GlobalAddAtomW(PCWSTR lpString, PUSHORT lpAtom)
{
    KIRQL Irql;
    ULONG hash;
    PATOM_ENTRY entry;
    ULONG nameLen;

    if (!lpString || !lpAtom) return STATUS_INVALID_PARAMETER;

    nameLen = 0;
    while (nameLen < ATOM_MAX_NAME && lpString[nameLen]) nameLen++;

    hash = AtomHash(lpString, nameLen);

    W32K_LOCK_SPIN(g_AtomLock, Irql);

    /* Search for existing atom with this name */
    entry = g_AtomTable[hash];
    while (entry) {
        if (AtomEqualName(entry->Name, entry->NameLength, lpString, nameLen)) {
            entry->RefCount++;
            *lpAtom = entry->AtomId;
            W32K_UNLOCK_SPIN(g_AtomLock, Irql);
            DbgPrint("ATOM: GlobalAddAtomW '%ws' -> 0x%04X (ref=%d)\n",
                     lpString, entry->AtomId, entry->RefCount);
            return STATUS_SUCCESS;
        }
        entry = entry->Next;
    }

    /* Allocate new entry */
    entry = (PATOM_ENTRY)ExAllocatePool(NonPagedPool, sizeof(ATOM_ENTRY));
    if (!entry) {
        W32K_UNLOCK_SPIN(g_AtomLock, Irql);
        return STATUS_NO_MEMORY;
    }

    entry->AtomId = g_NextAtomId++;
    entry->RefCount = 1;
    entry->NameLength = nameLen;
    RtlCopyMemory(entry->Name, lpString, nameLen * sizeof(WCHAR));
    entry->Name[nameLen] = 0;

    /* Insert at head of chain */
    entry->Next = g_AtomTable[hash];
    g_AtomTable[hash] = entry;

    *lpAtom = entry->AtomId;
    W32K_UNLOCK_SPIN(g_AtomLock, Irql);

    DbgPrint("ATOM: GlobalAddAtomW '%ws' -> 0x%04X (new)\n",
             lpString, entry->AtomId);
    return STATUS_SUCCESS;
}

/* GlobalDeleteAtom: decrement reference, free if zero */
NTSTATUS NTAPI GlobalDeleteAtom(USHORT nAtom)
{
    KIRQL Irql;
    ULONG hash;
    PATOM_ENTRY entry, *pprev;

    if (nAtom < 0xC000) return STATUS_INVALID_PARAMETER;

    W32K_LOCK_SPIN(g_AtomLock, Irql);

    for (hash = 0; hash < ATOM_TABLE_SIZE; hash++) {
        pprev = &g_AtomTable[hash];
        entry = *pprev;
        while (entry) {
            if (entry->AtomId == nAtom) {
                entry->RefCount--;
                if (entry->RefCount == 0) {
                    *pprev = entry->Next;
                    ExFreePool(entry);
                }
                W32K_UNLOCK_SPIN(g_AtomLock, Irql);
                DbgPrint("ATOM: GlobalDeleteAtom 0x%04X -> ref=%d\n",
                         nAtom, (entry->RefCount > 0) ? entry->RefCount : 0);
                return STATUS_SUCCESS;
            }
            pprev = &entry->Next;
            entry = entry->Next;
        }
    }

    W32K_UNLOCK_SPIN(g_AtomLock, Irql);
    return STATUS_NOT_FOUND;
}

/* GlobalFindAtomW: look up atom by name */
NTSTATUS NTAPI GlobalFindAtomW(PCWSTR lpString, PUSHORT lpAtom)
{
    KIRQL Irql;
    ULONG hash, nameLen;
    PATOM_ENTRY entry;

    if (!lpString || !lpAtom) return STATUS_INVALID_PARAMETER;

    nameLen = 0;
    while (nameLen < ATOM_MAX_NAME && lpString[nameLen]) nameLen++;

    hash = AtomHash(lpString, nameLen);

    W32K_LOCK_SPIN(g_AtomLock, Irql);

    entry = g_AtomTable[hash];
    while (entry) {
        if (AtomEqualName(entry->Name, entry->NameLength, lpString, nameLen)) {
            *lpAtom = entry->AtomId;
            W32K_UNLOCK_SPIN(g_AtomLock, Irql);
            return STATUS_SUCCESS;
        }
        entry = entry->Next;
    }

    W32K_UNLOCK_SPIN(g_AtomLock, Irql);
    return STATUS_NOT_FOUND;
}

/* GlobalGetAtomNameW: retrieve name for atom */
NTSTATUS NTAPI GlobalGetAtomNameW(USHORT nAtom, PWCHAR lpBuffer, int nMaxCount)
{
    KIRQL Irql;
    ULONG hash;
    PATOM_ENTRY entry;
    ULONG copyLen;

    if (!lpBuffer || nMaxCount <= 0) return STATUS_INVALID_PARAMETER;
    if (nAtom < 0xC000) return STATUS_INVALID_PARAMETER;

    W32K_LOCK_SPIN(g_AtomLock, Irql);

    for (hash = 0; hash < ATOM_TABLE_SIZE; hash++) {
        entry = g_AtomTable[hash];
        while (entry) {
            if (entry->AtomId == nAtom) {
                copyLen = entry->NameLength;
                if (copyLen >= (ULONG)nMaxCount)
                    copyLen = (ULONG)nMaxCount - 1;
                RtlCopyMemory(lpBuffer, entry->Name, copyLen * sizeof(WCHAR));
                lpBuffer[copyLen] = 0;
                W32K_UNLOCK_SPIN(g_AtomLock, Irql);
                return STATUS_SUCCESS;
            }
            entry = entry->Next;
        }
    }

    W32K_UNLOCK_SPIN(g_AtomLock, Irql);
    lpBuffer[0] = 0;
    return STATUS_NOT_FOUND;
}
