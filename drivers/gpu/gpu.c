/*
 * MinNT - drivers/gpu/gpu.c
 * GPU Subsystem - Core initialization and device management
 * 
 * Manages GPU device discovery, driver matching, and command submission.
 * Supports AMD, Intel, and software fallback rendering.
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/hal.h>
#include <nt/gpu.h>
#include <nt/rtl.h>

#define TAG_GPU '  GPU'

/* Global GPU state */
GPU_DEVICE GpuDevices[GPU_MAX_DEVICES];
ULONG GpuDeviceCount = 0;
PGPU_DEVICE PrimaryGpu = NULL;

/* Registered GPU drivers */
static PGPU_DRIVER GpuDrivers[8];
static ULONG GpuDriverCount = 0;

/* Forward declarations */
static NTSTATUS GpuInitDevice(PGPU_DEVICE Device);

/* ---- GPU Subsystem Initialization ------------------------------------- */

NTSTATUS NTAPI GpuInitializeSubsystem(VOID)
{
    NTSTATUS Status;
    
    DbgPrint("GPU: Initializing graphics subsystem...\n");
    
    RtlZeroMemory(GpuDevices, sizeof(GpuDevices));
    GpuDeviceCount = 0;
    PrimaryGpu = NULL;
    
    /* Register built-in drivers */
    Status = GpuRegisterDriver(&AmdGpuDriver);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("GPU: Warning - failed to register AMD driver\n");
    }
    
    Status = GpuRegisterDriver(&IntelGpuDriver);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("GPU: Warning - failed to register Intel driver\n");
    }
    
    Status = GpuRegisterDriver(&NvidiaGpuDriver);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("GPU: Warning - failed to register NVIDIA driver\n");
    }

    Status = GpuRegisterDriver(&VirtioGpuDriver);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("GPU: Warning - failed to register VirtIO GPU driver\n");
    }

    Status = GpuRegisterDriver(&SwGpuDriver);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("GPU: Warning - failed to register software driver\n");
    }
    
    /* Probe PCI for GPU devices */
    Status = GpuProbePciDevices();
    if (!NT_SUCCESS(Status)) {
        DbgPrint("GPU: No GPU devices found, using software renderer\n");
    }
    
    DbgPrint("GPU: Subsystem initialized, %u device(s) found\n", GpuDeviceCount);
    
    return STATUS_SUCCESS;
}

/* ---- Driver Registration ---------------------------------------------- */

NTSTATUS NTAPI GpuRegisterDriver(PGPU_DRIVER Driver)
{
    if (!Driver || !Driver->Ops) {
        return STATUS_INVALID_PARAMETER;
    }
    
    if (GpuDriverCount >= 8) {
        DbgPrint("GPU: Driver registration failed - too many drivers\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    GpuDrivers[GpuDriverCount++] = Driver;
    
    DbgPrint("GPU: Registered driver '%s' (%s)\n", Driver->Name, Driver->Description);
    
    return STATUS_SUCCESS;
}

/* ---- PCI Device Probing ------------------------------------------------ */

NTSTATUS NTAPI GpuProbePciDevices(VOID)
{
    ULONG Bus, Dev, Func;
    ULONG VendorDevice;
    USHORT VendorId, DeviceId;
    ULONG ClassCode;
    UCHAR Class, SubClass;
    NTSTATUS Status;
    PGPU_DRIVER MatchedDriver = NULL;
    ULONG i, j;
    ULONG Timeout = 0;
    
    DbgPrint("GPU: Scanning PCI for display devices...\n");
    
    for (Bus = 0; Bus < 256; Bus++) {
        for (Dev = 0; Dev < 32; Dev++) {
            for (Func = 0; Func < 8; Func++) {
                /* Add timeout to prevent hanging on slow hardware */
                if (Timeout++ > 10000) {
                    DbgPrint("GPU: PCI scan timeout, stopping scan\n");
                    goto scan_complete;
                }
                
                VendorDevice = HalPciReadConfig(Bus, Dev, Func, 0x00);
                VendorId = (USHORT)(VendorDevice & 0xFFFF);
                DeviceId = (USHORT)(VendorDevice >> 16);
                
                if (VendorId == 0xFFFF) {
                    continue;
                }
                
                /* Check class code */
                ClassCode = HalPciReadConfig(Bus, Dev, Func, 0x08);
                Class = (UCHAR)((ClassCode >> 24) & 0xFF);
                SubClass = (UCHAR)((ClassCode >> 16) & 0xFF);
                
                /* Display controller class */
                if (Class != 0x03) {
                    continue;
                }
                
                DbgPrint("GPU: Found display device %04x:%04x at %02x:%02x.%x\n",
                         VendorId, DeviceId, Bus, Dev, Func);
                
                /* Find matching driver */
                MatchedDriver = NULL;
                for (i = 0; i < GpuDriverCount; i++) {
                    for (j = 0; j < GpuDrivers[i]->VendorCount; j++) {
                        if (GpuDrivers[i]->SupportedVendors[j] == VendorId) {
                            MatchedDriver = GpuDrivers[i];
                            break;
                        }
                    }
                    if (MatchedDriver) break;
                }
                
                if (!MatchedDriver) {
                    DbgPrint("GPU: No driver for device %04x:%04x\n", VendorId, DeviceId);
                    continue;
                }
                
                /* Check if driver supports this specific device */
                if (MatchedDriver->Ops->Probe && 
                    !NT_SUCCESS(MatchedDriver->Ops->Probe(VendorId, DeviceId))) {
                    DbgPrint("GPU: Driver rejected device %04x:%04x\n", VendorId, DeviceId);
                    continue;
                }
                
                /* Create GPU device */
                Status = GpuCreateDevice(VendorId, DeviceId, Bus, Dev, Func, &GpuDevices[GpuDeviceCount]);
                if (!NT_SUCCESS(Status)) {
                    DbgPrint("GPU: Failed to create device for %04x:%04x\n", VendorId, DeviceId);
                    continue;
                }
                
                /* Initialize with driver */
                Status = MatchedDriver->Ops->Initialize(&GpuDevices[GpuDeviceCount]);
                if (!NT_SUCCESS(Status)) {
                    DbgPrint("GPU: Driver failed to initialize device\n");
                    continue;
                }
                
                GpuDevices[GpuDeviceCount].Initialized = TRUE;
                
                /* Set as primary if first GPU */
                if (!PrimaryGpu) {
                    PrimaryGpu = &GpuDevices[GpuDeviceCount];
                    DbgPrint("GPU: Set device %u as primary GPU\n", GpuDeviceCount);
                }
                
                GpuDeviceCount++;
                
                if (GpuDeviceCount >= GPU_MAX_DEVICES) {
                    DbgPrint("GPU: Maximum device count reached\n");
                    goto scan_complete;
                }
            }
        }
    }
    
scan_complete:
    return GpuDeviceCount > 0 ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

/* ---- Device Management ------------------------------------------------ */

NTSTATUS NTAPI GpuCreateDevice(USHORT VendorId, USHORT DeviceId, UCHAR Bus, UCHAR Slot, UCHAR Func, PGPU_DEVICE *OutDevice)
{
    PGPU_DEVICE Device;
    
    if (GpuDeviceCount >= GPU_MAX_DEVICES) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    Device = &GpuDevices[GpuDeviceCount];
    RtlZeroMemory(Device, sizeof(GPU_DEVICE));
    
    Device->VendorId = VendorId;
    Device->DeviceId = DeviceId;
    Device->Bus = Bus;
    Device->Slot = Slot;
    Device->Function = Func;
    
    KeInitializeSpinLock(&Device->Lock);
    KeInitializeSpinLock(&Device->MemoryManager.Lock);
    KeInitializeSpinLock(&Device->GraphicsRing.Lock);
    KeInitializeSpinLock(&Device->ComputeRing.Lock);
    
    if (OutDevice) {
        *OutDevice = Device;
    }
    
    return STATUS_SUCCESS;
}

PGPU_DEVICE NTAPI GpuGetPrimaryDevice(VOID)
{
    return PrimaryGpu;
}

/* ---- Memory Management ------------------------------------------------ */

NTSTATUS NTAPI GpuAllocMemory(PGPU_DEVICE Device, SIZE_T Size, ULONG Type, PHYSICAL_ADDRESS *OutAddr)
{
    if (!Device || !OutAddr) {
        return STATUS_INVALID_PARAMETER;
    }
    
    /* For now, allocate from contiguous pool */
    PVOID Va = MmAllocateContiguousMemory(Size, 0xFFFFFFFFULL);
    if (!Va) {
        return STATUS_NO_MEMORY;
    }
    
    *OutAddr = MmGetPhysicalAddress(Va);
    
    DbgPrint("GPU: Allocated %zu bytes type=%u at PA=%p\n", Size, Type, (PVOID)(ULONG_PTR)*OutAddr);
    
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI GpuFreeMemory(PGPU_DEVICE Device, PHYSICAL_ADDRESS Addr)
{
    /* Simplified - need to track allocations properly */
    DbgPrint("GPU: Free memory at PA=%p\n", (PVOID)(ULONG_PTR)Addr);
    return STATUS_SUCCESS;
}

/* ---- Command Submission ------------------------------------------------ */

NTSTATUS NTAPI GpuSubmitCommands(PGPU_DEVICE Device, PVOID Commands, ULONG Size)
{
    if (!Device || !Commands || Size == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    
    DbgPrint("GPU: Submitting %u bytes of commands\n", Size);
    
    /* Driver-specific implementation */
    if (Device->GraphicsRing.Active) {
        /* Copy to ring buffer and signal GPU */
    }
    
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI GpuWaitForIdle(PGPU_DEVICE Device, ULONG TimeoutMs)
{
    DbgPrint("GPU: Waiting for idle (timeout=%u ms)\n", TimeoutMs);
    
    /* Poll until GPU idle */
    KeStallExecutionProcessor(1000);
    
    return STATUS_SUCCESS;
}

/* ---- Display Management ------------------------------------------------ */

NTSTATUS NTAPI GpuSetDisplayMode(PGPU_DEVICE Device, ULONG Width, ULONG Height, ULONG Bpp)
{
    if (!Device) {
        return STATUS_INVALID_PARAMETER;
    }
    
    DbgPrint("GPU: Set display mode %ux%u@%u\n", Width, Height, Bpp);
    
    /* Allocate framebuffer */
    SIZE_T FbSize = Width * Height * (Bpp / 8);
    PHYSICAL_ADDRESS FbAddr;
    NTSTATUS Status = GpuAllocMemory(Device, FbSize, GPU_MEM_TYPE_VRAM, &FbAddr);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    
    Device->Display.Crtcs[0].Width = Width;
    Device->Display.Crtcs[0].Height = Height;
    Device->Display.Crtcs[0].Active = TRUE;
    Device->Display.PrimaryCrtc = 0;
    Device->DisplayActive = TRUE;
    
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI GpuEnableDisplay(PGPU_DEVICE Device, BOOLEAN Enable)
{
    if (!Device) {
        return STATUS_INVALID_PARAMETER;
    }
    
    DbgPrint("GPU: %s display\n", Enable ? "Enable" : "Disable");
    
    Device->DisplayActive = Enable;
    
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI GpuFlipFramebuffer(PGPU_DEVICE Device, PHYSICAL_ADDRESS NewFbAddr)
{
    DbgPrint("GPU: Flip framebuffer to PA=%p\n", (PVOID)(ULONG_PTR)NewFbAddr);
    return STATUS_SUCCESS;
}
