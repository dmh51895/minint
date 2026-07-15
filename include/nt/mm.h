/*
 * MinNT - mm.h
 * Memory Manager: physical page (PFN) allocator + long-mode paging.
 */

#ifndef _MM_H_
#define _MM_H_

#include <nt/ntdef.h>

#define PAGE_SIZE   0x1000
#define PAGE_SHIFT  12
#define PAGE_ALIGN(Va)      ((ULONG_PTR)(Va) & ~(PAGE_SIZE - 1))
#define BYTES_TO_PAGES(Sz)  (((Sz) + PAGE_SIZE - 1) >> PAGE_SHIFT)

/* PTE bits (amd64) */
#define PTE_PRESENT     (1ULL << 0)
#define PTE_WRITE       (1ULL << 1)
#define PTE_USER        (1ULL << 2)
#define PTE_PWT         (1ULL << 3)    /* Page Write-Through */
#define PTE_PCD         (1ULL << 4)    /* Page Cache Disable */
#define PTE_PAT         (1ULL << 7)    /* PAT index (with PCD,PWT selects mem type) */
#define PTE_LARGE       (1ULL << 7)
#define PTE_GLOBAL      (1ULL << 8)
#define PTE_NX          (1ULL << 63)

/* PAT index for MmMapPage when caller wants UC (UnCached) for MMIO */
/* PAT index bits with PCD=1, PWT=1, PAT=1 => index 7 (UC-) for 4KB pages */
/* For 2MB large pages, PAT index = PCD + PWT*2 + PAT (bits 3,4,5) */
/* We use PAT index 7 (UC-) for MMIO regions - use PTE_PCD|PTE_PWT|PTE_PAT */
#define MM_MEMTYPE_UC   (PTE_PCD | PTE_PWT | PTE_PAT)

/* PFN database entry — miniature MMPFN */
typedef enum _MM_PAGE_STATE {
    PfnFree = 0,
    PfnActive,
    PfnReservedHw,
    PfnKernelImage,
} MM_PAGE_STATE;

typedef struct _MMPFN {
    UCHAR State;            /* MM_PAGE_STATE */
    UCHAR Flags;
    USHORT RefCount;
} MMPFN, *PMMPFN;

/* Init: consumes the multiboot2 memory map */
NTSTATUS NTAPI MmInitSystem(PVOID MultibootInfo);

/* Physical allocator */
PHYSICAL_ADDRESS NTAPI MmAllocatePhysicalPage(VOID);
VOID             NTAPI MmFreePhysicalPage(PHYSICAL_ADDRESS Pa);
ULONG64          NTAPI MmGetTotalPages(VOID);
ULONG64          NTAPI MmGetFreePages(VOID);

/* Virtual mapping into the current address space */
NTSTATUS NTAPI MmMapPage(ULONG_PTR Va, PHYSICAL_ADDRESS Pa, ULONG64 Flags);

/* ---- MMIO mapping (kernel space, uncached) ------------------------------- */

#define MMIO_MAPPING_MAX  128

typedef struct _MMIO_MAPPING {
    ULONG_PTR   VirtualAddress;
    ULONG_PTR   Length;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG       ReferenceCount;
    ULONG       Active;
} MMIO_MAPPING, *PMMIO_MAPPING;

/* Map physical MMIO region to virtual address (always uncached) */
PVOID NTAPI MmMapIoSpace(PHYSICAL_ADDRESS PhysicalAddress, ULONG Length);
VOID  NTAPI MmUnmapIoSpace(PVOID VirtualAddress);

/* Extended version with cache type control */
#define MM_IO_CACHE_UC      0   /* Uncached (default for MMIO) */
#define MM_IO_CACHE_WB      1   /* Write-back (for framebuffer etc) */
#define MM_IO_CACHE_WC      2   /* Write-combining */

PVOID NTAPI MmMapIoSpaceEx(PHYSICAL_ADDRESS PhysicalAddress, ULONG Length, ULONG CacheType);

extern MMIO_MAPPING MmIoMappingTable[MMIO_MAPPING_MAX];
extern ULONG        MmIoMappingCount;

/* ---- Contiguous memory allocation (for DMA) ----------------------------- */

/* 
 * MmAllocateContiguousMemory - Allocate physically contiguous memory
 * 
 * Required for xHCI/EHCI DMA structures that must not cross page boundaries:
 *   - Device Context Base Address Array (DCBAA)
 *   - Command/Event rings
 *   - Transfer rings
 *   - Device contexts
 * 
 * All allocations are 64-byte aligned and come from below 4GB
 * (required for 32-bit DMA hardware).
 */
PVOID NTAPI MmAllocateContiguousMemory(
    SIZE_T NumberOfBytes,
    PHYSICAL_ADDRESS HighestAcceptableAddress
);

VOID NTAPI MmFreeContiguousMemory(PVOID BaseAddress);

/* Get physical address of a virtual address (for DMA descriptors) */
PHYSICAL_ADDRESS NTAPI MmGetPhysicalAddress(PVOID BaseAddress);

/* 
 * Contiguous memory pool for xHCI/EHCI structures
 * Carved from low physical memory (<1GB) at boot time
 */
#define MM_CONTIG_POOL_SIZE    (8 * 1024 * 1024)    /* 8MB contiguous pool for framebuffers */
#define MM_CONTIG_ALIGNMENT    64               /* 64-byte alignment for xHCI */

extern PVOID MmContiguousPoolBase;
extern ULONG MmContiguousPoolUsed;

/* ---- User VA support (per-process address spaces) ------------------------ */

#define MM_USER_BASE        0x10000ULL           /* user mode start */
#define MM_USER_MAX         0x7FFFFFFFE000ULL    /* user mode end (canonical lower) */
#define MM_SHARED_BASE      0x7E0000000000ULL    /* shared user page */

typedef struct _MM_VAD {
    ULONG_PTR   StartingVa;
    ULONG_PTR   EndingVa;
    ULONG       Flags;
    PVOID       Section;
    struct _MM_VAD *Next;
} MM_VAD, *PMM_VAD;

/* Per-process address space */
typedef struct _MM_ADDRESS_SPACE {
    ULONG64     Pml4;           /* physical PML4 base */
    PMM_VAD     VadRoot;        /* VAD tree root */
    ULONG_PTR   NextAvailableVa;
    ULONG_PTR   HighestVa;
} MM_ADDRESS_SPACE, *PMM_ADDRESS_SPACE;

/* VAD flags */
#define MM_VAD_COMMIT    0x01
#define MM_VAD_RESERVE   0x02
#define MM_VAD_FREE      0x04

NTSTATUS NTAPI MmCreateAddressSpace(PMM_ADDRESS_SPACE OutSpace);
NTSTATUS NTAPI MmDestroyAddressSpace(PMM_ADDRESS_SPACE Space);
NTSTATUS NTAPI MmAllocateVirtualMemory(PMM_ADDRESS_SPACE Space,
                                        PULONG_PTR BaseAddress,
                                        ULONG_PTR ZeroBits,
                                        SIZE_T RegionSize,
                                        ULONG AllocationType,
                                        ULONG Protect);
NTSTATUS NTAPI MmFreeVirtualMemory(PMM_ADDRESS_SPACE Space,
                                    PULONG_PTR BaseAddress);
NTSTATUS NTAPI MmMapViewOfSection(PMM_ADDRESS_SPACE Space,
                                   PVOID Section,
                                   PULONG_PTR BaseAddress,
                                   SIZE_T ViewSize,
                                   ULONG Protect);
NTSTATUS NTAPI MmUnmapViewOfSection(PMM_ADDRESS_SPACE Space,
                                     PVOID BaseAddress);
NTSTATUS NTAPI MmCopyVirtualMemory(PVOID SourceAddress,
                                    PVOID TargetAddress,
                                    SIZE_T BufferSize);

/* Page fault handler (called from trap.S vector 14) */
NTSTATUS NTAPI MmAccessFault(ULONG_PTR FaultAddress, ULONG ErrorCode);

/* Switch address space */
VOID NTAPI MmSwitchAddressSpace(PMM_ADDRESS_SPACE Space);

/* User-mode address validation */
BOOLEAN NTAPI MmIsAddressValid(PVOID Va);

/* No-arg wrapper for BootProfile dispatcher. */
NTSTATUS NTAPI MmInitSystemWrap(VOID);

#endif /* _MM_H_ */
