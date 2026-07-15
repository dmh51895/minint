/*
 * MinNT - drivers/gpu/sw/swgpu.c
 * Software GPU fallback renderer
 * 
 * Provides basic GPU interface without real hardware
 * Used when no GPU detected or as fallback
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/hal.h>
#include <nt/gpu.h>
#include <nt/rtl.h>

#define TAG_SWGPU 'SWGP'

NTSTATUS NTAPI SwGpuInitialize(PGPU_DEVICE Device)
{
    DbgPrint("SWGPU: Software renderer initialized\n");
    
    RtlCopyMemory(Device->Name, "Software Renderer", sizeof("Software Renderer"));
    Device->Architecture = GPU_ARCH_SOFTWARE;
    
    /* No MMIO needed for software */
    Device->MmioBase = NULL;
    Device->MmioPhys = 0;
    Device->MmioSize = 0;
    
    /* Allocate system memory for framebuffer */
    Device->VramSize = 16 * 1024 * 1024;  /* 16MB */
    NTSTATUS Status = GpuAllocMemory(Device, Device->VramSize, GPU_MEM_TYPE_SYSTEM, &Device->VramPhys);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    
    Device->VramVirt = MmMapIoSpace(Device->VramPhys, (ULONG)Device->VramSize);
    
    Device->Initialized = TRUE;
    
    DbgPrint("SWGPU: Ready (16MB framebuffer at PA=%p)\n", (PVOID)(ULONG_PTR)Device->VramPhys);
    
    return STATUS_SUCCESS;
}

const GPU_DRIVER_OPS SwGpuOps = {
    .Initialize = SwGpuInitialize,
};

GPU_DRIVER SwGpuDriver = {
    .Ops = &SwGpuOps,
    .Name = "swgpu",
    .Description = "Software GPU renderer",
    .SupportedVendors = {0},
    .VendorCount = 0,
};
