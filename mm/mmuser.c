/*
 * MinNT - mm/mmuser.c
 * User-mode virtual memory: per-process address spaces (PML4), VAD management,
 * NtAllocateVirtualMemory, demand-zero page faults, section mapping.
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/rtl.h>
#include <nt/hal.h>
#include <nt/ps.h>

/* ---- PML4 allocation for user processes --------------------------------- */

static PHYSICAL_ADDRESS MmpAllocateUserPml4(VOID)
{
    PHYSICAL_ADDRESS pml4Pa;
    ULONG64 *pml4;

    pml4Pa = MmAllocatePhysicalPage();
    if (!pml4Pa) return 0;

    pml4 = (ULONG64 *)pml4Pa;  /* identity mapped */

    /* Copy kernel-high half (PML4 entries 256-511) from current CR3 */
    ULONG64 cr3;
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(cr3));
    ULONG64 *currentPml4 = (ULONG64 *)(cr3 & 0x000FFFFFFFFFF000ULL);

    /* Copy kernel mappings (indices 256-511) */
    for (int i = 256; i < 512; i++)
        pml4[i] = currentPml4[i];

    return pml4Pa;
}

/* ---- Create address space ------------------------------------------------ */

NTSTATUS NTAPI MmCreateAddressSpace(PMM_ADDRESS_SPACE OutSpace)
{
    PHYSICAL_ADDRESS pml4Pa;

    pml4Pa = MmpAllocateUserPml4();
    if (!pml4Pa) return STATUS_NO_MEMORY;

    OutSpace->Pml4 = pml4Pa;
    OutSpace->VadRoot = NULL;
    OutSpace->NextAvailableVa = MM_USER_BASE;
    OutSpace->HighestVa = MM_USER_MAX;

    return STATUS_SUCCESS;
}

/* ---- Destroy address space ----------------------------------------------- */

NTSTATUS NTAPI MmDestroyAddressSpace(PMM_ADDRESS_SPACE Space)
{
    PMM_VAD vad, next;

    if (!Space) return STATUS_INVALID_PARAMETER;

    /* Free all VADs */
    vad = Space->VadRoot;
    while (vad) {
        next = vad->Next;
        ExFreePoolWithTag(vad, TAG_PROC);
        vad = next;
    }

    /* Don't free the PML4 page itself — it's identity mapped and
       freeing it would corrupt the kernel. Just mark it unused. */
    Space->VadRoot = NULL;
    return STATUS_SUCCESS;
}

/* ---- Allocate virtual memory -------------------------------------------- */

NTSTATUS NTAPI MmAllocateVirtualMemory(PMM_ADDRESS_SPACE Space,
                                        PULONG_PTR BaseAddress,
                                        ULONG_PTR ZeroBits,
                                        SIZE_T RegionSize,
                                        ULONG AllocationType,
                                        ULONG Protect)
{
    PMM_VAD vad;
    ULONG_PTR base;
    SIZE_T size;

    UNREFERENCED_PARAMETER(ZeroBits);

    if (!Space || !BaseAddress) return STATUS_INVALID_PARAMETER;

    /* Align size to page boundary */
    size = (RegionSize + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    /* Determine base address */
    if (*BaseAddress) {
        base = PAGE_ALIGN(*BaseAddress);
    } else {
        base = Space->NextAvailableVa;
    }

    /* Check for address collision with existing VADs */
    PMM_VAD existing = Space->VadRoot;
    while (existing) {
        if (base < existing->EndingVa && (base + size) > existing->StartingVa) {
            /* Collision — try above the conflicting VAD */
            base = existing->EndingVa;
            existing = Space->VadRoot;  /* restart check */
            continue;
        }
        existing = existing->Next;
    }

    /* Check bounds */
    if (base + size > Space->HighestVa)
        return STATUS_NO_MEMORY;

    /* Create VAD */
    vad = ExAllocatePoolWithTag(NonPagedPool, sizeof(MM_VAD), TAG_PROC);
    if (!vad) return STATUS_NO_MEMORY;

    vad->StartingVa = base;
    vad->EndingVa = base + size;
    vad->Flags = AllocationType;
    vad->Section = NULL;
    vad->Next = Space->VadRoot;
    Space->VadRoot = vad;

    /* Update next available VA */
    Space->NextAvailableVa = base + size;

    *BaseAddress = base;

    DbgPrint("MM: allocated VA %p-%p (size %llu KB)\n",
             (PVOID)base, (PVOID)(base + size), (unsigned long long)(size >> 10));
    return STATUS_SUCCESS;
}

/* ---- Free virtual memory ------------------------------------------------- */

NTSTATUS NTAPI MmFreeVirtualMemory(PMM_ADDRESS_SPACE Space,
                                    PULONG_PTR BaseAddress)
{
    PMM_VAD *prev, vad;

    if (!Space || !BaseAddress) return STATUS_INVALID_PARAMETER;

    prev = &Space->VadRoot;
    while (*prev) {
        vad = *prev;
        if (vad->StartingVa == PAGE_ALIGN(*BaseAddress)) {
            *prev = vad->Next;
            ExFreePoolWithTag(vad, TAG_PROC);
            return STATUS_SUCCESS;
        }
        prev = &vad->Next;
    }

    return STATUS_INVALID_PARAMETER;
}

/* ---- Map view of section ------------------------------------------------- */

NTSTATUS NTAPI MmMapViewOfSection(PMM_ADDRESS_SPACE Space,
                                   PVOID Section,
                                   PULONG_PTR BaseAddress,
                                   SIZE_T ViewSize,
                                   ULONG Protect)
{
    UNREFERENCED_PARAMETER(Section);
    UNREFERENCED_PARAMETER(Protect);

    /* For now, just allocate virtual memory at the requested address */
    return MmAllocateVirtualMemory(Space, BaseAddress, 0,
                                   ViewSize, MM_VAD_COMMIT, 0);
}

/* ---- Unmap view of section ---------------------------------------------- */

NTSTATUS NTAPI MmUnmapViewOfSection(PMM_ADDRESS_SPACE Space,
                                     PVOID BaseAddress)
{
    ULONG_PTR addr = (ULONG_PTR)BaseAddress;
    return MmFreeVirtualMemory(Space, &addr);
}

/* ---- Copy virtual memory ------------------------------------------------- */

NTSTATUS NTAPI MmCopyVirtualMemory(PVOID SourceAddress,
                                    PVOID TargetAddress,
                                    SIZE_T BufferSize)
{
    /* Identity mapped — direct copy */
    RtlCopyMemory(TargetAddress, SourceAddress, BufferSize);
    return STATUS_SUCCESS;
}

/* ---- Demand-zero page fault handler -------------------------------------- */

NTSTATUS NTAPI MmAccessFault(ULONG_PTR FaultAddress, ULONG ErrorCode)
{
    PHYSICAL_ADDRESS pa;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(ErrorCode);

    /* Only handle demand-zero faults (write to uncommitted user page) */
    if (FaultAddress < MM_USER_BASE || FaultAddress > MM_USER_MAX)
        return STATUS_UNSUCCESSFUL;

    /* For now, handle ALL user-mode faults as demand-zero.
       Real MM would check VAD flags first. */
    pa = MmAllocatePhysicalPage();
    if (!pa) return STATUS_NO_MEMORY;

    status = MmMapPage(PAGE_ALIGN(FaultAddress), pa,
                       PTE_USER | PTE_WRITE);
    if (!NT_SUCCESS(status)) {
        MmFreePhysicalPage(pa);
        return status;
    }

    DbgPrint("MM: demand-zero fault at %p → PA %p\n",
             (PVOID)FaultAddress, (PVOID)pa);
    return STATUS_SUCCESS;
}

/* ---- Switch address space ------------------------------------------------ */

VOID NTAPI MmSwitchAddressSpace(PMM_ADDRESS_SPACE Space)
{
    if (Space && Space->Pml4) {
        __asm__ __volatile__("movq %0, %%cr3" :: "r"(Space->Pml4) : "memory");
    }
}

/* ---- User-mode address validation ---------------------------------------- */

BOOLEAN NTAPI MmIsAddressValid(PVOID Va)
{
    ULONG_PTR addr = (ULONG_PTR)Va;

    if (!Va) return FALSE;

    /* Kernel addresses (high half canonical) are always valid */
    if (addr >= 0xFFFF800000000000ULL) return TRUE;

    /* User-mode addresses must be in the valid range */
    if (addr < MM_USER_BASE || addr > MM_USER_MAX) return FALSE;

    /* For a minimal implementation, accept all addresses in the
       user-mode range. Real NT would walk the PTEs, but our
       demand-zero fault handler will trap on invalid pages. */
    return TRUE;
}
