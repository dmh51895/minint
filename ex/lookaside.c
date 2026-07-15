/*
 * MinNT - ex/lookaside.c
 * Lookaside lists (NPAGED_LOOKASIDE_LIST).
 *
 * A lookaside list is a per-CPU free list of fixed-size allocations.
 * It avoids the overhead of ExAllocatePool for small, frequent
 * allocations (typical examples: IRPs, MDLs, FILE_OBJECTs, etc.).
 *
 * The NT implementation keeps a per-CPU single-linked list of free
 * blocks. Allocate pops from the front; free pushes to the front.
 * The depth is bounded; when empty or full, allocate/free fall
 * through to the pool.
 */

#include <nt/ke.h>
#include <nt/ex.h>
#include <nt/rtl.h>
#include <nt/hal.h>
#include <nt/framework.h>

#define LOOKASIDE_MAX_PER_LIST  256
#define LOOKASIDE_MAX_LISTS     32

static NPAGED_LOOKASIDE_LIST g_Lists[LOOKASIDE_MAX_LISTS];

static NPAGED_LOOKASIDE_LIST g_Lists[LOOKASIDE_MAX_LISTS];

NTSTATUS NTAPI ExInitializeNPagedLookasideList(PNPAGED_LOOKASIDE_LIST Lookaside,
                                                ULONG Tag)
{
    if (!Lookaside) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Lookaside, sizeof(*Lookaside));
    Lookaside->Tag = Tag;
    Lookaside->MaximumDepth = LOOKASIDE_MAX_PER_LIST;
    KeInitializeSpinLock(&Lookaside->Lock);
    Lookaside->ListHead.Next = NULL;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ExDeleteNPagedLookasideList(PNPAGED_LOOKASIDE_LIST Lookaside)
{
    if (!Lookaside) return STATUS_INVALID_PARAMETER;
    /* Drain the free list back to the pool. */
    KIRQL irql;
    KeAcquireSpinLock(&Lookaside->Lock, &irql);
    PNPAGED_LOOKASIDE_ENTRY entry = Lookaside->ListHead.Next;
    Lookaside->ListHead.Next = NULL;
    Lookaside->Depth = 0;
    KeReleaseSpinLock(&Lookaside->Lock, irql);
    while (entry) {
        PNPAGED_LOOKASIDE_ENTRY next = entry->Next;
        ExFreePoolWithTag(entry, Lookaside->Tag);
        entry = next;
    }
    return STATUS_SUCCESS;
}

PVOID NTAPI ExAllocateFromNPagedLookasideList(PNPAGED_LOOKASIDE_LIST Lookaside)
{
    if (!Lookaside) return NULL;
    KIRQL irql;
    KeAcquireSpinLock(&Lookaside->Lock, &irql);
    PNPAGED_LOOKASIDE_ENTRY entry = Lookaside->ListHead.Next;
    if (entry) {
        Lookaside->ListHead.Next = entry->Next;
        Lookaside->Depth--;
        Lookaside->AllocateHits++;
    } else {
        Lookaside->AllocateMisses++;
    }
    KeReleaseSpinLock(&Lookaside->Lock, irql);
    if (entry) return (PVOID)entry;
    /* Real implementation would use the Tag's size. MinNT uses 64. */
    return ExAllocatePoolWithTag(NonPagedPool, 64, Lookaside->Tag);
}

VOID NTAPI ExFreeToNPagedLookasideList(PNPAGED_LOOKASIDE_LIST Lookaside, PVOID Entry)
{
    if (!Lookaside || !Entry) return;
    KIRQL irql;
    KeAcquireSpinLock(&Lookaside->Lock, &irql);
    if (Lookaside->Depth >= Lookaside->MaximumDepth) {
        Lookaside->FreeMisses++;
        KeReleaseSpinLock(&Lookaside->Lock, irql);
        ExFreePoolWithTag(Entry, Lookaside->Tag);
        return;
    }
    PNPAGED_LOOKASIDE_ENTRY entry = (PNPAGED_LOOKASIDE_ENTRY)Entry;
    entry->Next = Lookaside->ListHead.Next;
    Lookaside->ListHead.Next = entry;
    Lookaside->Depth++;
    Lookaside->FreeHits++;
    KeReleaseSpinLock(&Lookaside->Lock, irql);
}

NTSTATUS NTAPI ExRegisterLookaside(const CHAR *Name, ULONG Tag, PNPAGED_LOOKASIDE_LIST *OutList)
{
    for (ULONG i = 0; i < LOOKASIDE_MAX_LISTS; i++) {
        if (!g_Lists[i].InUse) {
            RtlZeroMemory(&g_Lists[i], sizeof(NPAGED_LOOKASIDE_LIST));
            for (ULONG k = 0; k < 63 && Name[k]; k++) g_Lists[i].Name[k] = Name[k];
            g_Lists[i].InUse = TRUE;
            g_Lists[i].Tag = Tag;
            g_Lists[i].MaximumDepth = LOOKASIDE_MAX_PER_LIST;
            KeInitializeSpinLock(&g_Lists[i].Lock);
            if (OutList) *OutList = &g_Lists[i];
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

ULONG NTAPI ExLookasideGetDepth(PNPAGED_LOOKASIDE_LIST Lookaside)
{
    return Lookaside ? Lookaside->Depth : 0;
}

ULONG NTAPI ExLookasideGetAllocateHits(PNPAGED_LOOKASIDE_LIST Lookaside)
{
    return Lookaside ? Lookaside->AllocateHits : 0;
}
