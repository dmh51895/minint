/*
 * MinNT - include/nt/gpu.h
 * GPU (Graphics Processing Unit) Driver Architecture
 * 
 * Provides abstraction layer for GPU hardware support:
 *   - DRM (Direct Rendering Manager) core
 *   - GPU device enumeration and initialization
 *   - Command submission (rings/buffers)
 *   - Memory management (VRAM, GART, etc.)
 *   - Display output (CRTC, encoder, connector)
 * 
 * Supports real GPU hardware: AMD, Intel, NVIDIA (via open source nouveau)
 * Reference: Linux DRM (drivers/gpu/drm/)
 */

#pragma once

#ifndef _GPU_H_
#define _GPU_H_

#include <nt/ntdef.h>
#include <nt/mm.h>

/* ---- GPU Vendor IDs ----------------------------------------------------- */

#define PCI_VENDOR_AMD              0x1002
#define PCI_VENDOR_INTEL            0x8086
#define PCI_VENDOR_NVIDIA           0x10DE
#define PCI_VENDOR_ATI              0x1002

/* ---- GPU Class Codes --------------------------------------------------- */

#define PCI_CLASS_DISPLAY_VGA       0x0300
#define PCI_CLASS_DISPLAY_XGA       0x0301
#define PCI_CLASS_DISPLAY_3D        0x0302
#define PCI_CLASS_DISPLAY_OTHER     0x0380

/* ---- GPU Architecture Types --------------------------------------------- */

#define GPU_ARCH_AMD_GCN            1   /* AMD Graphics Core Next */
#define GPU_ARCH_AMD_RDNA           2   /* AMD RDNA/RDNA2/RDNA3 */
#define GPU_ARCH_INTEL_GEN          3   /* Intel Graphics Gen */
#define GPU_ARCH_NVIDIA_TURING      4   /* NVIDIA Turing/Ampere */
#define GPU_ARCH_SOFTWARE           0   /* Software renderer fallback */

/* ---- GPU Memory Types ------------------------------------------------- */

#define GPU_MEM_TYPE_VRAM           1   /* Video RAM (GDDR/HBM) */
#define GPU_MEM_TYPE_GTT            2   /* Graphics Translation Table */
#define GPU_MEM_TYPE_SYSTEM         3   /* System memory (for iGPU) */

/* ---- GPU Commands ----------------------------------------------------- */

#define GPU_CMD_NOP                 0x00000000
#define GPU_CMD_SURFACE_SYNC        0x00000001
#define GPU_CMD_CONTEXT_CONTROL     0x00000002
#define GPU_CMD_COPY_DATA           0x00000003
#define GPU_CMD_DRAW                0x00000004
#define GPU_CMD_DISPATCH            0x00000005

/* ---- Forward Declarations --------------------------------------------- */

typedef struct _GPU_DEVICE GPU_DEVICE, *PGPU_DEVICE;
typedef struct _GPU_CONTEXT GPU_CONTEXT, *PGPU_CONTEXT;
typedef struct _GPU_RING_BUFFER GPU_RING_BUFFER, *PGPU_RING_BUFFER;
typedef struct _GPU_MEMORY_MANAGER GPU_MEMORY_MANAGER, *PGPU_MEMORY_MANAGER;
typedef struct _GPU_DISPLAY_OUTPUT GPU_DISPLAY_OUTPUT, *PGPU_DISPLAY_OUTPUT;

/* ---- GPU Ring Buffer (Command Submission) ------------------------------ */

#define GPU_RING_SIZE_4KB           0x1000
#define GPU_RING_SIZE_8KB           0x2000
#define GPU_RING_SIZE_16KB          0x4000
#define GPU_RING_SIZE_32KB          0x8000
#define GPU_RING_SIZE_64KB          0x10000

struct _GPU_RING_BUFFER {
    PVOID               CpuBase;        /* CPU accessible address */
    PHYSICAL_ADDRESS    GpuBase;        /* GPU physical address */
    ULONG               Size;           /* Ring buffer size */
    ULONG               WritePtr;       /* Current write position */
    ULONG               ReadPtr;        /* Current read position (from GPU) */
    ULONG               GpuAddr;        /* GPU-relative address */
    KSPIN_LOCK          Lock;
    BOOLEAN             Active;
};

/* ---- GPU Memory Manager ------------------------------------------------ */

#define GPU_MAX_MEMORY_REGIONS      16

typedef struct _GPU_MEMORY_REGION {
    PHYSICAL_ADDRESS    BaseAddress;
    ULONG64             Size;
    ULONG               Type;           /* VRAM, GTT, System */
    BOOLEAN             Allocated;
    PVOID               VirtualMapping;
} GPU_MEMORY_REGION, *PGPU_MEMORY_REGION;

struct _GPU_MEMORY_MANAGER {
    GPU_MEMORY_REGION   Regions[GPU_MAX_MEMORY_REGIONS];
    ULONG               RegionCount;
    ULONG64             TotalVram;
    ULONG64             FreeVram;
    KSPIN_LOCK          Lock;
};

/* ---- GPU Display Output ------------------------------------------------ */

#define GPU_MAX_CRTCS               6
#define GPU_MAX_ENCODERS            8
#define GPU_MAX_CONNECTORS          8

typedef struct _GPU_CRTC {
    ULONG               Id;
    ULONG               Width;
    ULONG               Height;
    ULONG               RefreshRate;
    BOOLEAN             Active;
    PVOID               Framebuffer;    /* Current FB */
} GPU_CRTC, *PGPU_CRTC;

typedef struct _GPU_ENCODER {
    ULONG               Id;
    ULONG               Type;           /* DP, HDMI, DVI, VGA */
    BOOLEAN             Active;
} GPU_ENCODER, *PGPU_ENCODER;

typedef struct _GPU_CONNECTOR {
    ULONG               Id;
    ULONG               Type;           /* DP, HDMI, DVI, VGA */
    BOOLEAN             Connected;
    ULONG               EncoderId;
} GPU_CONNECTOR, *PGPU_CONNECTOR;

struct _GPU_DISPLAY_OUTPUT {
    GPU_CRTC            Crtcs[GPU_MAX_CRTCS];
    GPU_ENCODER         Encoders[GPU_MAX_ENCODERS];
    GPU_CONNECTOR       Connectors[GPU_MAX_CONNECTORS];
    ULONG               CrtcCount;
    ULONG               EncoderCount;
    ULONG               ConnectorCount;
    ULONG               PrimaryCrtc;
};

/* ---- GPU Device Structure ---------------------------------------------- */

struct _GPU_DEVICE {
    /* PCI Info */
    USHORT              VendorId;
    USHORT              DeviceId;
    USHORT              SubsystemVendor;
    USHORT              SubsystemDevice;
    UCHAR               Revision;
    UCHAR               Bus;
    UCHAR               Slot;
    UCHAR               Function;
    
    /* Architecture */
    ULONG               Architecture;
    CHAR                Name[64];
    
    /* MMIO */
    PVOID               MmioBase;
    PHYSICAL_ADDRESS    MmioPhys;
    ULONG               MmioSize;
    
    /* VRAM/Framebuffer */
    PHYSICAL_ADDRESS    VramPhys;
    PVOID               VramVirt;
    ULONG64             VramSize;
    
    /* Doorbells/Interrupts */
    PVOID               DoorbellBase;
    PHYSICAL_ADDRESS    DoorbellPhys;
    
    /* Command Rings */
    GPU_RING_BUFFER     GraphicsRing;
    GPU_RING_BUFFER     ComputeRing;
    GPU_RING_BUFFER     SDMARing;
    
    /* Memory Management */
    GPU_MEMORY_MANAGER  MemoryManager;
    
    /* Display */
    GPU_DISPLAY_OUTPUT  Display;
    
    /* State */
    BOOLEAN             Initialized;
    BOOLEAN             DisplayActive;
    KSPIN_LOCK          Lock;
};

/* ---- GPU Operations Vtable (Driver Interface) ------------------------- */

typedef struct _GPU_DRIVER_OPS {
    /* Device Lifecycle */
    NTSTATUS (NTAPI *Probe)(USHORT VendorId, USHORT DeviceId);
    NTSTATUS (NTAPI *Initialize)(PGPU_DEVICE Device);
    NTSTATUS (NTAPI *Shutdown)(PGPU_DEVICE Device);
    NTSTATUS (NTAPI *Reset)(PGPU_DEVICE Device);
    
    /* Memory Management */
    NTSTATUS (NTAPI *AllocVram)(PGPU_DEVICE Device, SIZE_T Size, ULONG Alignment, PHYSICAL_ADDRESS *OutAddr);
    NTSTATUS (NTAPI *FreeVram)(PGPU_DEVICE Device, PHYSICAL_ADDRESS Addr);
    NTSTATUS (NTAPI *MapVram)(PGPU_DEVICE Device, PHYSICAL_ADDRESS GpuAddr, PVOID *OutCpuAddr);
    NTSTATUS (NTAPI *UnmapVram)(PGPU_DEVICE Device, PVOID CpuAddr);
    
    /* Command Submission */
    NTSTATUS (NTAPI *SubmitCommands)(PGPU_DEVICE Device, PVOID Commands, ULONG Size);
    NTSTATUS (NTAPI *WaitForIdle)(PGPU_DEVICE Device, ULONG TimeoutMs);
    
    /* Display */
    NTSTATUS (NTAPI *SetMode)(PGPU_DEVICE Device, ULONG CrtcId, ULONG Width, ULONG Height, ULONG Bpp);
    NTSTATUS (NTAPI *EnableDisplay)(PGPU_DEVICE Device, BOOLEAN Enable);
    NTSTATUS (NTAPI *FlipFramebuffer)(PGPU_DEVICE Device, PHYSICAL_ADDRESS NewFbAddr);
    
    /* Interrupt Handling */
    VOID (NTAPI *HandleInterrupt)(PGPU_DEVICE Device);
    
} GPU_DRIVER_OPS, *PGPU_DRIVER_OPS;

/* ---- GPU Driver Registration ------------------------------------------ */

typedef struct _GPU_DRIVER {
    const GPU_DRIVER_OPS *Ops;
    const CHAR *Name;
    const CHAR *Description;
    ULONG       SupportedVendors[8];
    ULONG       VendorCount;
} GPU_DRIVER, *PGPU_DRIVER;

/* ---- Global GPU State -------------------------------------------------- */

#define GPU_MAX_DEVICES     4

extern GPU_DEVICE GpuDevices[GPU_MAX_DEVICES];
extern ULONG GpuDeviceCount;
extern PGPU_DEVICE PrimaryGpu;

/* ---- Function Prototypes ---------------------------------------------- */

/* Initialization */
NTSTATUS NTAPI GpuInitializeSubsystem(VOID);
NTSTATUS NTAPI GpuProbePciDevices(VOID);
NTSTATUS NTAPI GpuRegisterDriver(PGPU_DRIVER Driver);

/* Device Management */
NTSTATUS NTAPI GpuCreateDevice(USHORT VendorId, USHORT DeviceId, UCHAR Bus, UCHAR Slot, UCHAR Func, PGPU_DEVICE *OutDevice);
NTSTATUS NTAPI GpuDestroyDevice(PGPU_DEVICE Device);
PGPU_DEVICE NTAPI GpuGetPrimaryDevice(VOID);

/* Memory Management */
NTSTATUS NTAPI GpuAllocMemory(PGPU_DEVICE Device, SIZE_T Size, ULONG Type, PHYSICAL_ADDRESS *OutAddr);
NTSTATUS NTAPI GpuFreeMemory(PGPU_DEVICE Device, PHYSICAL_ADDRESS Addr);
NTSTATUS NTAPI GpuMapMemory(PGPU_DEVICE Device, PHYSICAL_ADDRESS GpuAddr, PVOID *OutCpuAddr);
NTSTATUS NTAPI GpuUnmapMemory(PGPU_DEVICE Device, PVOID CpuAddr);

/* Command Submission */
NTSTATUS NTAPI GpuSubmitCommands(PGPU_DEVICE Device, PVOID Commands, ULONG Size);
NTSTATUS NTAPI GpuWaitForIdle(PGPU_DEVICE Device, ULONG TimeoutMs);

/* Display */
NTSTATUS NTAPI GpuSetDisplayMode(PGPU_DEVICE Device, ULONG Width, ULONG Height, ULONG Bpp);
NTSTATUS NTAPI GpuEnableDisplay(PGPU_DEVICE Device, BOOLEAN Enable);
NTSTATUS NTAPI GpuFlipFramebuffer(PGPU_DEVICE Device, PHYSICAL_ADDRESS NewFbAddr);

/* Interrupt Handling */
VOID NTAPI GpuHandleInterrupt(PGPU_DEVICE Device);

/* ---- AMD GPU Specific ------------------------------------------------- */

NTSTATUS NTAPI AmdGpuInitialize(PGPU_DEVICE Device);
NTSTATUS NTAPI AmdGpuProbe(USHORT VendorId, USHORT DeviceId);
extern const GPU_DRIVER_OPS AmdGpuOps;
extern GPU_DRIVER AmdGpuDriver;

/* ---- Intel GPU Specific ----------------------------------------------- */

NTSTATUS NTAPI IntelGpuInitialize(PGPU_DEVICE Device);
NTSTATUS NTAPI IntelGpuProbe(USHORT VendorId, USHORT DeviceId);
extern const GPU_DRIVER_OPS IntelGpuOps;
extern GPU_DRIVER IntelGpuDriver;

/* ---- NVIDIA GPU Specific ----------------------------------------------- */

NTSTATUS NTAPI NvidiaGpuProbe(USHORT VendorId, USHORT DeviceId);
NTSTATUS NTAPI NvidiaGpuInitialize(PGPU_DEVICE Device);
extern const GPU_DRIVER_OPS NvidiaGpuOps;
extern GPU_DRIVER NvidiaGpuDriver;

/* ---- VirtIO GPU Specific ----------------------------------------------- */

NTSTATUS NTAPI VirtioGpuProbe(USHORT VendorId, USHORT DeviceId);
NTSTATUS NTAPI VirtioGpuInitialize(PGPU_DEVICE Device);
extern const GPU_DRIVER_OPS VirtioGpuOps;
extern GPU_DRIVER VirtioGpuDriver;

/* ---- Software Fallback ------------------------------------------------ */

NTSTATUS NTAPI SwGpuInitialize(PGPU_DEVICE Device);
extern const GPU_DRIVER_OPS SwGpuOps;
extern GPU_DRIVER SwGpuDriver;

#endif /* _GPU_H_ */
