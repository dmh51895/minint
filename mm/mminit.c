/*
 * MinNT - mm/mminit.c
 * Memory Manager phase 0: parse the multiboot2 memory map, build a PFN
 * database (one MMPFN per 4K page), expose a physical page allocator,
 * and provide MmMapPage for growing the identity-mapped address space.
 *
 * The boot stub identity-maps the first 1GB with 2MB pages, so the PFN
 * database itself (placed right after the kernel image) is addressable.
 */

#include <nt/mm.h>
#include <nt/ke.h>
#include <nt/ps.h>
#include <nt/hal.h>
#include <nt/rtl.h>

/* Multiboot2 info structures (just the parts we consume) */
typedef struct { ULONG TotalSize; ULONG Reserved; } MB2_INFO;
typedef struct { ULONG Type; ULONG Size; } MB2_TAG;
typedef struct {
    ULONG Type; ULONG Size;
    ULONG EntrySize; ULONG EntryVersion;
    /* entries follow */
} MB2_TAG_MMAP;
typedef struct {
    ULONG64 BaseAddr;
    ULONG64 Length;
    ULONG   Type;              /* 1 = available */
    ULONG   Reserved;
} MB2_MMAP_ENTRY;

#define MB2_TAG_TYPE_END   0
#define MB2_TAG_TYPE_MMAP  6

extern CHAR __kernel_start[];   /* from linker.ld */
extern CHAR __kernel_end[];

static PMMPFN  MmPfnDatabase;
static ULONG64 MmHighestPfn;
static ULONG64 MmTotalPages;
static ULONG64 MmFreePages;
static ULONG64 MmHintPfn;
static KSPIN_LOCK MmPfnLock;

/* Boot stub now maps up to 4GB with 2MB pages */
#define MM_BOOT_MAPPED_LIMIT (4ULL << 30)
#define MM_MAX_MMAP_ENTRIES  64

NTSTATUS NTAPI MmInitSystem(PVOID MultibootInfo)
{
    MB2_INFO *info = (MB2_INFO *)MultibootInfo;
    MB2_TAG  *tag;
    MB2_TAG_MMAP *mmap = NULL;
    ULONG_PTR cursor, end;
    ULONG64 pfn, dbBytes;
    ULONG_PTR dbStart, dbEnd, reserveEnd;
    /* Snapshot: GRUB's boot info lives in free RAM right after the kernel
       image, exactly where we want the PFN database. Copy the map out
       BEFORE we start scribbling, or we parse our own graffiti. (Found
       the hard way: BootInfo landed at 0x128740, DB at 0x115000.) */
    MB2_MMAP_ENTRY mapCopy[MM_MAX_MMAP_ENTRIES];
    ULONG mapCount = 0, mi;

    KeInitializeSpinLock(&MmPfnLock);

    /* Walk tags, find the memory map */
    cursor = (ULONG_PTR)info + 8;
    end    = (ULONG_PTR)info + info->TotalSize;
    while (cursor < end) {
        tag = (MB2_TAG *)cursor;
        if (tag->Type == MB2_TAG_TYPE_END) break;
        if (tag->Type == MB2_TAG_TYPE_MMAP) mmap = (MB2_TAG_MMAP *)tag;
        cursor += (tag->Size + 7) & ~7UL;
    }
    if (!mmap) return STATUS_UNSUCCESSFUL;

    /* Snapshot entries to the stack */
    {
        ULONG_PTR e = (ULONG_PTR)mmap + mmap->Size;
        ULONG_PTR c = (ULONG_PTR)(mmap + 1);
        for (; c + mmap->EntrySize <= e && mapCount < MM_MAX_MMAP_ENTRIES;
               c += mmap->EntrySize)
            mapCopy[mapCount++] = *(MB2_MMAP_ENTRY *)c;
    }

    /* Pass 1: highest usable PFN inside our mapped window */
    for (mi = 0; mi < mapCount; mi++) {
        ULONG64 top = mapCopy[mi].BaseAddr + mapCopy[mi].Length;
        if (mapCopy[mi].Type != 1) continue;
        if (top > MM_BOOT_MAPPED_LIMIT) top = MM_BOOT_MAPPED_LIMIT;
        if (top >> PAGE_SHIFT > MmHighestPfn)
            MmHighestPfn = top >> PAGE_SHIFT;
    }
    if (!MmHighestPfn) return STATUS_UNSUCCESSFUL;

    /* Place PFN database past BOTH the kernel image and the boot info */
    reserveEnd = (ULONG_PTR)__kernel_end;
    if ((ULONG_PTR)info + info->TotalSize > reserveEnd)
        reserveEnd = (ULONG_PTR)info + info->TotalSize;

    dbBytes = MmHighestPfn * sizeof(MMPFN);
    dbStart = (reserveEnd + PAGE_SIZE - 1) & ~(ULONG_PTR)(PAGE_SIZE - 1);
    dbEnd   = dbStart + dbBytes;
    MmPfnDatabase = (PMMPFN)dbStart;

    /* Everything starts reserved */
    for (pfn = 0; pfn < MmHighestPfn; pfn++) {
        MmPfnDatabase[pfn].State = PfnReservedHw;
        MmPfnDatabase[pfn].RefCount = 0;
    }

    /* Pass 2: mark available ranges free (from the snapshot) */
    for (mi = 0; mi < mapCount; mi++) {
        ULONG64 first, last;
        if (mapCopy[mi].Type != 1) continue;
        first = (mapCopy[mi].BaseAddr + PAGE_SIZE - 1) >> PAGE_SHIFT;
        last  = (mapCopy[mi].BaseAddr + mapCopy[mi].Length) >> PAGE_SHIFT;
        for (pfn = first; pfn < last && pfn < MmHighestPfn; pfn++) {
            MmPfnDatabase[pfn].State = PfnFree;
            MmTotalPages++;
        }
    }

    /* Carve out: page 0, then kernel image + boot info + PFN DB as one
       contiguous reserved span */
    MmPfnDatabase[0].State = PfnReservedHw;
    for (pfn = (ULONG_PTR)__kernel_start >> PAGE_SHIFT;
         pfn <= ((dbEnd - 1) >> PAGE_SHIFT) && pfn < MmHighestPfn; pfn++) {
        if (MmPfnDatabase[pfn].State == PfnFree) MmTotalPages--;
        MmPfnDatabase[pfn].State = PfnKernelImage;
    }

    /* Recount free */
    MmFreePages = 0;
    for (pfn = 0; pfn < MmHighestPfn; pfn++)
        if (MmPfnDatabase[pfn].State == PfnFree) MmFreePages++;

    MmHintPfn = 0x100;   /* start hunting above 1MB */

    DbgPrint("MM: PFN database at %p, %llu pages managed, %llu free (%llu MB)\n",
             (PVOID)MmPfnDatabase, MmHighestPfn, MmFreePages,
             (MmFreePages << PAGE_SHIFT) >> 20);
    return STATUS_SUCCESS;
}

PHYSICAL_ADDRESS NTAPI MmAllocatePhysicalPage(VOID)
{
    KIRQL irql;
    ULONG64 pfn;
    PHYSICAL_ADDRESS pa = 0;

    KeAcquireSpinLock(&MmPfnLock, &irql);
    for (pfn = MmHintPfn; pfn < MmHighestPfn; pfn++) {
        if (MmPfnDatabase[pfn].State == PfnFree) {
            MmPfnDatabase[pfn].State = PfnActive;
            MmPfnDatabase[pfn].RefCount = 1;
            MmFreePages--;
            MmHintPfn = pfn + 1;
            pa = pfn << PAGE_SHIFT;
            break;
        }
    }
    KeReleaseSpinLock(&MmPfnLock, irql);

    if (pa) RtlZeroMemory((PVOID)pa, PAGE_SIZE);   /* identity mapped */
    return pa;
}

VOID NTAPI MmFreePhysicalPage(PHYSICAL_ADDRESS Pa)
{
    KIRQL irql;
    ULONG64 pfn = Pa >> PAGE_SHIFT;

    ASSERT(pfn < MmHighestPfn);
    KeAcquireSpinLock(&MmPfnLock, &irql);
    ASSERT(MmPfnDatabase[pfn].State == PfnActive);
    MmPfnDatabase[pfn].State = PfnFree;
    MmPfnDatabase[pfn].RefCount = 0;
    MmFreePages++;
    if (pfn < MmHintPfn) MmHintPfn = pfn;
    KeReleaseSpinLock(&MmPfnLock, irql);
}

ULONG64 NTAPI MmGetTotalPages(VOID) { return MmTotalPages; }
ULONG64 NTAPI MmGetFreePages(VOID)  { return MmFreePages; }

/* ---- MMIO mapping ------------------------------------------------------ */

MMIO_MAPPING MmIoMappingTable[MMIO_MAPPING_MAX];
ULONG        MmIoMappingCount = 0;

/* Kernel virtual address cursor for MMIO — starts at 0xFFFFF80000000000 and grows down */
static ULONG_PTR MmIoVaCursor = 0xFFFFF80000000000ULL;
static KSPIN_LOCK MmIoLock;

/* Walk the 4-level page table and return the page table entry pointer.
 * Creates intermediate tables (PML4, PDPT, PD) on demand.
 * Returns NULL on failure. */
static ULONG64 *MmpWalkToPte(ULONG_PTR Va, ULONG64 Pml4Base)
{
    ULONG64 *pml4, *pdpt, *pd, *pt;

    pml4 = (ULONG64 *)(Pml4Base & 0x000FFFFFFFFFF000ULL);

    /* PML4 index (bits 39-47) */
    if (!(pml4[(Va >> 39) & 0x1FF] & PTE_PRESENT)) {
        PHYSICAL_ADDRESS pa = MmAllocatePhysicalPage();
        if (!pa) return NULL;
        pml4[(Va >> 39) & 0x1FF] = pa | PTE_PRESENT | PTE_WRITE;
    }
    pdpt = (ULONG64 *)(pml4[(Va >> 39) & 0x1FF] & 0x000FFFFFFFFFF000ULL);
    if (pdpt == NULL) return NULL;

    /* PDPT index (bits 30-38) */
    if (!(pdpt[(Va >> 30) & 0x1FF] & PTE_PRESENT)) {
        PHYSICAL_ADDRESS pa = MmAllocatePhysicalPage();
        if (!pa) return NULL;
        pdpt[(Va >> 30) & 0x1FF] = pa | PTE_PRESENT | PTE_WRITE;
    }
    pd = (ULONG64 *)(pdpt[(Va >> 30) & 0x1FF] & 0x000FFFFFFFFFF000ULL);
    if (pd == NULL) return NULL;

    /* PD index (bits 21-29) */
    if (!(pd[(Va >> 21) & 0x1FF] & PTE_PRESENT)) {
        PHYSICAL_ADDRESS pa = MmAllocatePhysicalPage();
        if (!pa) return NULL;
        pd[(Va >> 21) & 0x1FF] = pa | PTE_PRESENT | PTE_WRITE;
    }
    pt = (ULONG64 *)(pd[(Va >> 21) & 0x1FF] & 0x000FFFFFFFFFF000ULL);
    if (pt == NULL) return NULL;

    return &pt[(Va >> 12) & 0x1FF];
}

/* Get the current CR3 (kernel PML4 physical address) */
static ULONG64 MmpGetKernelCr3(VOID)
{
    ULONG64 cr3;
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/* Internal rollback: clear PTEs for a partially mapped region (caller must hold MmIoLock) */
static VOID MmpRollbackMapping(ULONG_PTR startVa, ULONG numPages)
{
    ULONG64 cr3;
    ULONG64 *pte;

    cr3 = MmpGetKernelCr3();

    for (ULONG i = 0; i < numPages; i++) {
        ULONG_PTR va = startVa + (i * PAGE_SIZE);
        pte = MmpWalkToPte(va, cr3);
        if (pte) {
            *pte = 0;
            __asm__ __volatile__("invlpg (%0)" :: "r"(va) : "memory");
        }
    }
}

PVOID NTAPI MmMapIoSpace(PHYSICAL_ADDRESS PhysicalAddress, ULONG Length)
{
    KIRQL irql;
    ULONG_PTR startPa;
    ULONG_PTR endPa;
    ULONG_PTR startVa;
    ULONG numPages;
    ULONG i;
    ULONG64 *pte;
    ULONG64 cr3;
    ULONG entryIndex;

    if (Length == 0)
        return NULL;

    KeAcquireSpinLock(&MmIoLock, &irql);

    /* Check if we have room in the mapping table */
    if (MmIoMappingCount >= MMIO_MAPPING_MAX) {
        KeReleaseSpinLock(&MmIoLock, irql);
        DbgPrint("MM: MmMapIoSpace failed — mapping table full (%d entries)\n", MMIO_MAPPING_MAX);
        return NULL;
    }

    /* Align physical address down to page boundary */
    startPa = PhysicalAddress & ~(ULONG_PTR)(PAGE_SIZE - 1);
    /* Round length up to page boundary */
    endPa = (startPa + (ULONG_PTR)Length + PAGE_SIZE - 1) & ~(ULONG_PTR)(PAGE_SIZE - 1);
    numPages = (ULONG)((endPa - startPa) >> PAGE_SHIFT);

    /* Find a free slot in the mapping table */
    entryIndex = MmIoMappingCount;
    MmIoMappingCount++;

    /* Allocate kernel virtual address range (grow downward from high kernel space) */
    /* Align cursor down to page boundary */
    MmIoVaCursor &= ~(ULONG_PTR)(PAGE_SIZE - 1);
    startVa = MmIoVaCursor - (numPages * PAGE_SIZE);
    /* Ensure we don't go below a safe boundary (leave room for other kernel mappings) */
    if (startVa < 0xFFFFF70000000000ULL) {
        DbgPrint("MM: MmMapIoSpace failed — kernel VA space exhausted\n");
        MmIoMappingCount--;
        KeReleaseSpinLock(&MmIoLock, irql);
        return NULL;
    }
    MmIoVaCursor = startVa;

    /* Record the mapping */
    MmIoMappingTable[entryIndex].VirtualAddress = startVa;
    MmIoMappingTable[entryIndex].Length = (ULONG_PTR)Length;
    MmIoMappingTable[entryIndex].PhysicalAddress = PhysicalAddress;
    MmIoMappingTable[entryIndex].ReferenceCount = 1;
    MmIoMappingTable[entryIndex].Active = 1;

    /* Walk page tables and create PTEs with UC (uncached) attributes
     * For 4KB pages: PAT index 7 = UC- (PWT=1, PCD=1, PAT=1) */
    cr3 = MmpGetKernelCr3();

    for (i = 0; i < numPages; i++) {
        ULONG_PTR va = startVa + (i * PAGE_SIZE);
        ULONG_PTR pa = startPa + (i * PAGE_SIZE);

        pte = MmpWalkToPte(va, cr3);
        if (!pte) {
            /* Roll back on failure — don't call MmUnmapIoSpace (would deadlock) */
            DbgPrint("MM: MmMapIoSpace failed at page %u/%u\n", i, numPages);
            MmpRollbackMapping(startVa, i);
            MmIoMappingTable[entryIndex].Active = 0;
            MmIoMappingTable[entryIndex].ReferenceCount = 0;
            MmIoMappingCount--;
            KeReleaseSpinLock(&MmIoLock, irql);
            return NULL;
        }

        *pte = (pa & ~0xFFFULL) | PTE_PRESENT | PTE_WRITE | PTE_PCD | PTE_PWT | PTE_PAT;
        __asm__ __volatile__("invlpg (%0)" :: "r"(va) : "memory");
    }

    KeReleaseSpinLock(&MmIoLock, irql);

    DbgPrint("MM: MmMapIoSpace phys=%llx len=%u -> va=%p (%u pages)\n",
             (unsigned long long)PhysicalAddress, (unsigned)Length,
             (PVOID)startVa, numPages);

    return (PVOID)startVa;
}

VOID NTAPI MmUnmapIoSpace(PVOID VirtualAddress)
{
    KIRQL irql;
    ULONG i;
    ULONG_PTR va;
    ULONG numPages;
    ULONG64 *pte;
    ULONG64 cr3;

    if (!VirtualAddress)
        return;

    KeAcquireSpinLock(&MmIoLock, &irql);

    /* Find the mapping entry */
    for (i = 0; i < MmIoMappingCount; i++) {
        if (MmIoMappingTable[i].Active &&
            MmIoMappingTable[i].VirtualAddress == (ULONG_PTR)VirtualAddress) {

            MmIoMappingTable[i].ReferenceCount--;

            if (MmIoMappingTable[i].ReferenceCount > 0) {
                KeReleaseSpinLock(&MmIoLock, irql);
                return;
            }

            va = MmIoMappingTable[i].VirtualAddress;
            numPages = (ULONG)((MmIoMappingTable[i].Length + PAGE_SIZE - 1) >> PAGE_SHIFT);

            /* Clear PTEs and invalidate TLB */
            cr3 = MmpGetKernelCr3();

            for (ULONG j = 0; j < numPages; j++) {
                ULONG_PTR curVa = va + (j * PAGE_SIZE);
                pte = MmpWalkToPte(curVa, cr3);
                if (pte) {
                    *pte = 0;
                    __asm__ __volatile__("invlpg (%0)" :: "r"(curVa) : "memory");
                }
            }

            /* Mark mapping as inactive */
            MmIoMappingTable[i].Active = 0;
            MmIoMappingTable[i].ReferenceCount = 0;

            DbgPrint("MM: MmUnmapIoSpace va=%p (%u pages unmapped)\n",
                     (PVOID)va, numPages);
            break;
        }
    }

    KeReleaseSpinLock(&MmIoLock, irql);
}

/* ---- User-mode page mapping ---------------------------------------------- */

NTSTATUS NTAPI MmMapPage(ULONG_PTR Va, PHYSICAL_ADDRESS Pa, ULONG64 Flags)
{
    ULONG64 *pte;
    ULONG64 cr3;

    cr3 = MmpGetKernelCr3();

    pte = MmpWalkToPte(Va, cr3);
    if (!pte)
        return STATUS_NO_MEMORY;

    *pte = ((ULONG64)Pa & ~0xFFFULL) | Flags;
    __asm__ __volatile__("invlpg (%0)" :: "r"(Va) : "memory");

    return STATUS_SUCCESS;
}
