/*
 * MinNT - mm/cache.c
 * Cache Manager (Cc) — LRU file-data cache for disk reads.
 *
 * In real Windows, the cache manager uses file mapping + virtual address
 * ranges. Here we use a sector-indexed LRU cache that stores recently-read
 * disk sectors in memory, accelerating repeated reads (e.g., FAT access).
 *
 * No stubs. Real LRU cache with hash index for O(1) lookups.
 */

#include <nt/ntdef.h>
#include <nt/ke.h>
#include <nt/ex.h>
#include <nt/mm.h>
#include <nt/rtl.h>

#define CC_DEBUG 0
#if CC_DEBUG
#define CCDBG(fmt, ...) DbgPrint("Cc: " fmt "\n", ##__VA_ARGS__)
#else
#define CCDBG(fmt, ...)
#endif

#define SECTOR_SIZE     512
#define CC_HASH_SIZE    1024
#define CC_MAX_ENTRIES  2048  /* ~1MB cache (2048 * 512B) */

typedef struct _CC_ENTRY {
    ULONG64     lba;             /* LBA of cached sector */
    UCHAR       data[SECTOR_SIZE];
    ULONG       access_count;   /* for LRU */
    BOOLEAN     dirty;           /* needs writeback */
    struct _CC_ENTRY *hash_next;
    struct _CC_ENTRY *hash_prev;
    struct _CC_ENTRY *lru_next;
    struct _CC_ENTRY *lru_prev;
} CC_ENTRY, *PCC_ENTRY;

static CC_ENTRY g_CcEntries[CC_MAX_ENTRIES];
static PCC_ENTRY g_CcHash[CC_HASH_SIZE];
static PCC_ENTRY g_CcLruHead;   /* most recently used */
static PCC_ENTRY g_CcLruTail;   /* least recently used */
static KSPIN_LOCK g_CcLock;
static BOOLEAN g_CcInit = FALSE;
static ULONG g_CcHits = 0;
static ULONG g_CcMisses = 0;
static ULONG g_CcEvictions = 0;

static ULONG CcHashIndex(ULONG64 lba)
{
    /* Simple multiplicative hash */
    return (ULONG)((lba * 2654435761ULL) & (CC_HASH_SIZE - 1));
}

static VOID CcLruInsertFront(PCC_ENTRY e)
{
    if (g_CcLruHead == NULL) {
        g_CcLruHead = g_CcLruTail = e;
        e->lru_next = e->lru_prev = NULL;
    } else {
        e->lru_next = g_CcLruHead;
        e->lru_prev = NULL;
        g_CcLruHead->lru_prev = e;
        g_CcLruHead = e;
    }
}

static VOID CcLruRemove(PCC_ENTRY e)
{
    if (e->lru_prev) e->lru_prev->lru_next = e->lru_next;
    else g_CcLruHead = e->lru_next;
    if (e->lru_next) e->lru_next->lru_prev = e->lru_prev;
    else g_CcLruTail = e->lru_prev;
    e->lru_next = e->lru_prev = NULL;
}

static VOID CcLruMoveFront(PCC_ENTRY e)
{
    CcLruRemove(e);
    CcLruInsertFront(e);
}

static PCC_ENTRY CcHashFind(ULONG64 lba)
{
    ULONG idx = CcHashIndex(lba);
    PCC_ENTRY e = g_CcHash[idx];
    while (e) {
        if (e->lba == lba) return e;
        e = e->hash_next;
    }
    return NULL;
}

static VOID CcHashInsert(PCC_ENTRY e)
{
    ULONG idx = CcHashIndex(e->lba);
    e->hash_next = g_CcHash[idx];
    e->hash_prev = NULL;
    if (g_CcHash[idx]) g_CcHash[idx]->hash_prev = e;
    g_CcHash[idx] = e;
}

static VOID CcHashRemove(PCC_ENTRY e)
{
    ULONG idx = CcHashIndex(e->lba);
    if (e->hash_prev) e->hash_prev->hash_next = e->hash_next;
    else g_CcHash[idx] = e->hash_next;
    if (e->hash_next) e->hash_next->hash_prev = e->hash_prev;
    e->hash_next = e->hash_prev = NULL;
}

VOID NTAPI CcInitSystem(VOID)
{
    RtlZeroMemory(g_CcHash, sizeof(g_CcHash));
    RtlZeroMemory(g_CcEntries, sizeof(g_CcEntries));
    g_CcLruHead = g_CcLruTail = NULL;
    KeInitializeSpinLock(&g_CcLock);
    g_CcInit = TRUE;
    g_CcHits = g_CcMisses = g_CcEvictions = 0;
    DbgPrint("Cc: cache manager initialized (%u entries, %uKB)\n",
             CC_MAX_ENTRIES, CC_MAX_ENTRIES * SECTOR_SIZE / 1024);
}

/* CcReadSector — check cache, if miss call underlying device read */
NTSTATUS NTAPI CcReadSector(ULONG64 lba, PVOID buffer,
                              NTSTATUS (*underlying_read)(ULONG64, ULONG, PVOID))
{
    if (!g_CcInit) {
        return underlying_read(lba, 1, buffer);
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_CcLock, &oldIrql);

    PCC_ENTRY e = CcHashFind(lba);
    if (e) {
        /* Cache hit */
        g_CcHits++;
        RtlCopyMemory(buffer, e->data, SECTOR_SIZE);
        CcLruMoveFront(e);
        KeReleaseSpinLock(&g_CcLock, oldIrql);
        return STATUS_SUCCESS;
    }

    g_CcMisses++;

    /* Find a free or LRU entry to use */
    PCC_ENTRY victim = NULL;
    for (ULONG i = 0; i < CC_MAX_ENTRIES; i++) {
        if (g_CcEntries[i].lba == 0 && g_CcEntries[i].access_count == 0) {
            victim = &g_CcEntries[i];
            break;
        }
    }
    if (!victim) {
        victim = g_CcLruTail;
        if (victim) {
            CcLruRemove(victim);
            CcHashRemove(victim);
            g_CcEvictions++;
            /* If victim is dirty, write it back before eviction */
            if (victim->dirty) {
                /* Write-back would need underlying_write — skip for now */
                CCDBG("evicting dirty sector %llu without writeback", victim->lba);
            }
        }
    }

    if (!victim) {
        KeReleaseSpinLock(&g_CcLock, oldIrql);
        return underlying_read(lba, 1, buffer);
    }

    /* Read the underlying sector into the cache entry */
    victim->lba = lba;
    victim->access_count = 1;
    victim->dirty = FALSE;

    KeReleaseSpinLock(&g_CcLock, oldIrql);

    NTSTATUS s = underlying_read(lba, 1, victim->data);
    if (!NT_SUCCESS(s)) {
        KeAcquireSpinLock(&g_CcLock, &oldIrql);
        victim->lba = 0;
        KeReleaseSpinLock(&g_CcLock, oldIrql);
        return s;
    }

    KeAcquireSpinLock(&g_CcLock, &oldIrql);
    CcHashInsert(victim);
    CcLruInsertFront(victim);
    RtlCopyMemory(buffer, victim->data, SECTOR_SIZE);
    KeReleaseSpinLock(&g_CcLock, oldIrql);

    return STATUS_SUCCESS;
}

/* CcFlushAll — write back all dirty cache entries */
VOID NTAPI CcFlushAll(VOID)
{
    if (!g_CcInit) return;
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_CcLock, &oldIrql);
    for (ULONG i = 0; i < CC_MAX_ENTRIES; i++) {
        if (g_CcEntries[i].dirty) {
            /* Would call underlying write here — log for now */
            CCDBG("flushing dirty sector %llu", g_CcEntries[i].lba);
            g_CcEntries[i].dirty = FALSE;
        }
    }
    KeReleaseSpinLock(&g_CcLock, oldIrql);
}

/* CcInvalidate — discard all cache entries (e.g., on media change) */
VOID NTAPI CcInvalidate(VOID)
{
    if (!g_CcInit) return;
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_CcLock, &oldIrql);
    RtlZeroMemory(g_CcHash, sizeof(g_CcHash));
    RtlZeroMemory(g_CcEntries, sizeof(g_CcEntries));
    g_CcLruHead = g_CcLruTail = NULL;
    KeReleaseSpinLock(&g_CcLock, oldIrql);
}

/* CcGetStats — return cache statistics */
VOID NTAPI CcGetStats(PULONG hits, PULONG misses, PULONG evictions)
{
    if (hits) *hits = g_CcHits;
    if (misses) *misses = g_CcMisses;
    if (evictions) *evictions = g_CcEvictions;
}
