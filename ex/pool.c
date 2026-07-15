/*
 * MinNT - ex/pool.c
 * NonPaged pool: first-fit free list over pages pulled from Mm, with
 * real POOL_HEADERs carrying tags. Corrupt a header and you get an
 * authentic BAD_POOL_HEADER 0x19, as God and Dave Cutler intended.
 */

#include <nt/ex.h>
#include <nt/mm.h>
#include <nt/ke.h>
#include <nt/rtl.h>

#define POOL_MAGIC_USED  0x4E504F4FU   /* 'OOPN' */
#define POOL_MAGIC_FREE  0x45455246U   /* 'FREE' */
#define POOL_GRANULARITY 16

typedef struct _POOL_HEADER {
    ULONG  Magic;
    ULONG  Tag;
    SIZE_T Size;                        /* body size                    */
    struct _POOL_HEADER *NextFree;      /* valid only when free         */
} POOL_HEADER, *PPOOL_HEADER;

static PPOOL_HEADER ExpFreeList;
static KSPIN_LOCK   ExpPoolLock;

#define POOL_EXPAND_PAGES 16

static NTSTATUS ExpExpandPool(SIZE_T MinBytes)
{
    SIZE_T pages = BYTES_TO_PAGES(MinBytes + sizeof(POOL_HEADER));
    SIZE_T i;
    PHYSICAL_ADDRESS base, pa;
    PPOOL_HEADER hdr;

    if (pages < POOL_EXPAND_PAGES) pages = POOL_EXPAND_PAGES;

    /* Physical pages are identity mapped; require contiguity by grabbing
       sequential PFNs (allocator is a rising-hint scan so early boot
       allocations are contiguous in practice; a fragmented grab just
       yields a smaller region and we retry). */
    base = MmAllocatePhysicalPage();
    if (!base) return STATUS_NO_MEMORY;
    for (i = 1; i < pages; i++) {
        pa = MmAllocatePhysicalPage();
        if (pa != base + (i << PAGE_SHIFT)) {
            if (pa) MmFreePhysicalPage(pa);
            pages = i;
            break;
        }
    }

    hdr = (PPOOL_HEADER)base;
    hdr->Magic = POOL_MAGIC_FREE;
    hdr->Tag   = 0;
    hdr->Size  = (pages << PAGE_SHIFT) - sizeof(POOL_HEADER);
    hdr->NextFree = ExpFreeList;
    ExpFreeList = hdr;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ExInitializePoolManager(VOID)
{
    KeInitializeSpinLock(&ExpPoolLock);
    ExpFreeList = NULL;
    return ExpExpandPool(POOL_EXPAND_PAGES << PAGE_SHIFT);
}

PVOID NTAPI ExAllocatePoolWithTag(POOL_TYPE PoolType, SIZE_T NumberOfBytes, ULONG Tag)
{
    KIRQL irql;
    PPOOL_HEADER cur, *prev;
    PVOID body = NULL;

    UNREFERENCED_PARAMETER(PoolType);
    if (!NumberOfBytes) return NULL;
    NumberOfBytes = (NumberOfBytes + POOL_GRANULARITY - 1) & ~(SIZE_T)(POOL_GRANULARITY - 1);

    KeAcquireSpinLock(&ExpPoolLock, &irql);

retry:
    prev = &ExpFreeList;
    for (cur = ExpFreeList; cur; prev = &cur->NextFree, cur = cur->NextFree) {
        if (cur->Magic != POOL_MAGIC_FREE)
            KeBugCheckEx(BAD_POOL_HEADER, (ULONG_PTR)cur, cur->Magic, 0, 1);
        if (cur->Size < NumberOfBytes) continue;

        /* Split if the remainder can hold a header + one granule */
        if (cur->Size >= NumberOfBytes + sizeof(POOL_HEADER) + POOL_GRANULARITY) {
            PPOOL_HEADER split = (PPOOL_HEADER)
                ((PUCHAR)(cur + 1) + NumberOfBytes);
            split->Magic = POOL_MAGIC_FREE;
            split->Tag   = 0;
            split->Size  = cur->Size - NumberOfBytes - sizeof(POOL_HEADER);
            split->NextFree = cur->NextFree;
            *prev = split;
            cur->Size = NumberOfBytes;
        } else {
            *prev = cur->NextFree;
        }
        cur->Magic = POOL_MAGIC_USED;
        cur->Tag   = Tag;
        body = (PVOID)(cur + 1);
        break;
    }

    if (!body) {
        if (NT_SUCCESS(ExpExpandPool(NumberOfBytes)))
            goto retry;
    }

    KeReleaseSpinLock(&ExpPoolLock, irql);
    if (body) RtlZeroMemory(body, NumberOfBytes);
    return body;
}

VOID NTAPI ExFreePoolWithTag(PVOID P, ULONG Tag)
{
    KIRQL irql;
    PPOOL_HEADER hdr;

    if (!P) return;
    hdr = ((PPOOL_HEADER)P) - 1;

    if (hdr->Magic != POOL_MAGIC_USED)
        KeBugCheckEx(BAD_POOL_HEADER, (ULONG_PTR)hdr, hdr->Magic, 0, 2);
    if (Tag && hdr->Tag != Tag)
        KeBugCheckEx(BAD_POOL_HEADER, (ULONG_PTR)hdr, hdr->Tag, Tag, 3);

    KeAcquireSpinLock(&ExpPoolLock, &irql);
    hdr->Magic = POOL_MAGIC_FREE;
    hdr->NextFree = ExpFreeList;
    ExpFreeList = hdr;
    KeReleaseSpinLock(&ExpPoolLock, irql);
}
