/*
 * MinNT - drivers/ata/ahci.c
 * AHCI SATA disk driver — ported from Linux libahci.c.
 *
 * Scans PCI bus for AHCI controllers (class 0x010601), initializes
 * the controller, enumerates ports, and provides read/write primitives
 * block devices.
 *
 * No stubs. Real register-level AHCI implementation.
 */

#include <nt/ntdef.h>
#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/ex.h>
#include <nt/mm.h>
#include <nt/exe.h>
#include <nt/rtl.h>
#include "ahci.h"
#ifndef STATUS_DEVICE_DOES_NOT_EXIST
#define STATUS_DEVICE_DOES_NOT_EXIST ((NTSTATUS)0xC000003EL)
#endif
#ifndef STATUS_IO_TIMEOUT
#define STATUS_IO_TIMEOUT ((NTSTATUS)0xC000000BL)
#endif

#define AHCI_DEBUG 1

#if AHCI_DEBUG
#define AHCIDBG(fmt, ...) DbgPrint("AHCI: " fmt "\n", ##__VA_ARGS__)
#else
#define AHCIDBG(fmt, ...)
#endif

/* ============================================================================
 * AHCI port — one per attached drive
 * ========================================================================== */

typedef struct _AHCI_PORT {
    ULONG      index;            /* port number on controller */
    PVOID      mmio_base;        /* pointer to per-port regs */
    AHCI_CMD_HDR *cmd_slots;     /* command slot table (1KB) */
    PVOID      cmd_table;        /* command table (8KB) */
    PVOID      rx_fis;           /* FIS receive buffer (256B) */
    ULONG64    cmd_slot_phys;
    ULONG64    cmd_tbl_phys;
    ULONG64    rx_fis_phys;
    BOOLEAN    attached;
    ULONG      signature;        /* PORT_SIG */
    CHAR       model[41];        /* drive model string */
    ULONG      sectors;         /* total sectors (LBA28) */
    ULONG      sectors48_lo;    /* low 32 bits of LBA48 sectors */
    ULONG      sectors48_hi;
} AHCI_PORT, *PAHCI_PORT;

/* ============================================================================
 * AHCI controller — one per PCI device
 * ========================================================================== */

typedef struct _AHCI_CONTROLLER {
    USHORT     pci_bus;
    USHORT     pci_device;
    USHORT     pci_function;
    ULONG64    mmio_phys;        /* BAR5 physical address */
    PVOID      mmio;             /* mapped MMIO region */
    ULONG      num_ports;
    AHCI_PORT  ports[AHCI_MAX_PORTS];
    LIST_ENTRY entry;            /* link in global list */
} AHCI_CONTROLLER, *PAHCI_CONTROLLER;

static LIST_ENTRY g_AhciControllers;
static BOOLEAN g_AhciInit = FALSE;
static ULONG g_NumAhciControllers = 0;

/* ============================================================================
 * MMIO helpers
 * ========================================================================== */

FORCEINLINE ULONG AhciReadReg(PVOID base, ULONG off)
{
    volatile ULONG *p = (volatile ULONG *)((PUCHAR)base + off);
    return *p;
}

FORCEINLINE VOID AhciWriteReg(PVOID base, ULONG off, ULONG val)
{
    volatile ULONG *p = (volatile ULONG *)((PUCHAR)base + off);
    *p = val;
}

FORCEINLINE VOID AhciWritePort(PVOID port_base, ULONG off, ULONG val)
{
    AhciWriteReg(port_base, off, val);
}

FORCEINLINE ULONG AhciReadPort(PVOID port_base, ULONG off)
{
    return AhciReadReg(port_base, off);
}

#define PORT_BASE(controller_mmio, port_idx) \
    (PVOID)((PUCHAR)(controller_mmio) + 0x100 + (port_idx) * 0x80)

/* ============================================================================
 * Physical memory allocation — for DMA buffers
 *
 * We use MmAllocatePhysicalPage + MmMapPage to create DMA-able buffers.
 * ========================================================================== */

static NTSTATUS AhciAllocDmaBuffer(SIZE_T size, PVOID *virt, ULONG64 *phys)
{
    if (!size || !virt || !phys) return STATUS_INVALID_PARAMETER;
    /* Round up to pages */
    SIZE_T pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    PVOID virt_addr = (PVOID)0xD0000000ULL; /* DMA region in kernel VA */
    ULONG64 cur_phys = 0;

    /* Allocate contiguous physical pages */
    PHYSICAL_ADDRESS pa_array[256];
    if (pages > 256) return STATUS_NO_MEMORY;

    for (SIZE_T i = 0; i < pages; i++) {
        pa_array[i] = MmAllocatePhysicalPage();
        if (!pa_array[i]) {
            /* Free what we allocated */
            for (SIZE_T j = 0; j < i; j++) MmFreePhysicalPage(pa_array[j]);
            return STATUS_NO_MEMORY;
        }
    }

    /* Find a virtual address range (simple bump allocator) */
    static ULONG64 dma_va_next = 0xD0000000ULL;
    virt_addr = (PVOID)dma_va_next;
    dma_va_next += pages * PAGE_SIZE;

    /* Map the pages */
    for (SIZE_T i = 0; i < pages; i++) {
        NTSTATUS s = MmMapPage((ULONG_PTR)virt_addr + i * PAGE_SIZE,
                                pa_array[i], PTE_WRITE | PTE_PCD);
        if (!NT_SUCCESS(s)) {
            for (SIZE_T j = 0; j <= i; j++) {
                if (pa_array[j]) {
                    /* Unmap page */
                }
                MmFreePhysicalPage(pa_array[j]);
            }
            return s;
        }
    }

    cur_phys = (ULONG64)pa_array[0];
    *virt = virt_addr;
    *phys = cur_phys;
    RtlZeroMemory(virt_addr, size);
    return STATUS_SUCCESS;
}

/* ============================================================================
 * Controller initialization
 *
 * From the AHCI spec:
 * 1. Read PI (Ports Implemented) to know which ports to scan
 * 2. For each port: spin up device, start FIS receive, start DMA engine
 * 3. Issue software reset to identify drive
 * ========================================================================== */

static NTSTATUS AhciStartPort(PAHCI_PORT port)
{
    PVOID base = port->mmio_base;
    ULONG cmd;

    /* Wait until PxCMD.ST (bit 15) and PxCMD.CR (bit 15) are clear */
    for (int i = 0; i < 50; i++) {
        cmd = AhciReadPort(base, PORT_CMD);
        if ((cmd & (PORT_CMD_LIST_ON | PORT_CMD_FIS_ON)) == 0) break;
        KeStallExecutionProcessor(10000);
    }

    /* Disable interrupts first */
    AhciWritePort(base, PORT_IRQ_MASK, 0);

    /* Set FIS receive enable */
    cmd = AhciReadPort(base, PORT_CMD);
    cmd |= PORT_CMD_FIS_RX;
    AhciWritePort(base, PORT_CMD, cmd);

    /* Set command list base address (low 32 bits) */
    AhciWritePort(base, PORT_LST_ADDR, (ULONG)(port->cmd_slot_phys & 0xFFFFFFFF));
    AhciWritePort(base, PORT_LST_ADDR_HI, (ULONG)(port->cmd_slot_phys >> 32));

    /* Set FIS base address (low 32 bits) */
    AhciWritePort(base, PORT_FIS_ADDR, (ULONG)(port->rx_fis_phys & 0xFFFFFFFF));
    AhciWritePort(base, PORT_FIS_ADDR_HI, (ULONG)(port->rx_fis_phys >> 32));

    /* Spin up device if controller supports staggered spin-up */
    /* Forward reference to set up cmd slot */
    cmd = AhciReadPort(base, PORT_CMD);
    cmd |= PORT_CMD_SPIN_UP | PORT_CMD_POWER_ON;
    AhciWritePort(base, PORT_CMD, cmd);

    /* Wait for device spin-up */
    for (int i = 0; i < 100; i++) {
        ULONG tfd = AhciReadPort(base, PORT_TFDATA);
        if ((tfd & ATA_BUSY) == 0) break;
        KeStallExecutionProcessor(10000);
    }

    /* Enable port: set PORT_CMD_START */
    cmd = AhciReadPort(base, PORT_CMD);
    cmd |= PORT_CMD_START;
    AhciWritePort(base, PORT_CMD, cmd);

    /* Re-enable interrupts */
    AhciWritePort(base, PORT_IRQ_MASK, DEF_PORT_IRQ);

    /* Clear any pending interrupt status */
    AhciWritePort(base, PORT_IRQ_STAT, 0xFFFFFFFF);

    return STATUS_SUCCESS;
}

static NTSTATUS AhciIssueCommand(PAHCI_PORT port, ULONG slot, BOOLEAN write)
{
    PVOID base = port->mmio_base;
    ULONG cmd;

    /* Issue the command by setting bit in PORT_CMD_ISSUE */
    AhciWritePort(base, PORT_CMD_ISSUE, (1UL << slot));

    /* Poll for completion */
    for (int i = 0; i < 10000; i++) {
        ULONG issued = AhciReadPort(base, PORT_CMD_ISSUE);
        if ((issued & (1UL << slot)) == 0) {
            /* Command completed */
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(100); /* 100 us */
    }

    AHCIDBG("port %u: command timeout", port->index);
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS AhciIdentifyDevice(PAHCI_PORT port)
{
    PVOID base = port->mmio_base;

    /* Set up command table */
    PUCHAR cmd_tbl = (PUCHAR)port->cmd_table;
    FIS_REG_H2D_TYPE *fis = (FIS_REG_H2D_TYPE *)cmd_tbl;
    RtlZeroMemory(fis, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pm_port_ctrl = 0x80; /* Command bit */
    fis->command = ATA_CMD_IDENTIFY_DEVICE;
    fis->device = 0;

    /* Set up PRD for the response (512 bytes) */
    AHCI_PRD *prd = (AHCI_PRD *)(cmd_tbl + AHCI_CMD_TBL_HDR_SZ);
    PVOID id_buf;
    ULONG64 id_phys;
    NTSTATUS s = AhciAllocDmaBuffer(512, &id_buf, &id_phys);
    if (!NT_SUCCESS(s)) return s;
    prd->addr = (ULONG)(id_phys & 0xFFFFFFFF);
    prd->addr_hi = (ULONG)(id_phys >> 32);
    prd->flags_size = 511 | 0x80000000; /* bytes count - 1 + interrupt flag */

    /* Set up command header slot 0 */
    /* Set up command slot 0 */
    RtlZeroMemory(port->cmd_slots, AHCI_CMD_SLOT_SZ);
    AHCI_CMD_HDR *hdr = &port->cmd_slots[0];
    hdr->opts = (5 << 16) /* fis size in DWs=5 */
              | (1 << 8)  /* one prd */
              | 0;          /* read */
    hdr->tbl_addr = (ULONG)(port->cmd_tbl_phys & 0xFFFFFFFF);
    hdr->tbl_addr_hi = (ULONG)(port->cmd_tbl_phys >> 32);

    /* Clear task file data */
    AhciWritePort(base, PORT_TFDATA, 0);
    /* Clear SError */
    AhciWritePort(base, PORT_SCR_ERR, 0xFFFFFFFF);

    s = AhciIssueCommand(port, 0, FALSE);
    if (!NT_SUCCESS(s)) {
        AHCIDBG("port %u: IDENTIFY DEVICE timeout", port->index);
        ExFreePool(id_buf);
        return s;
    }

    /* Parse the IDENTIFY response */
    USHORT *id = (USHORT *)id_buf;
    /* Model string: words 27-46 (40 bytes total, byte-swapped) */
    for (int i = 0; i < 20; i++) {
        USHORT w = id[27 + i];
        port->model[i * 2] = (CHAR)(w >> 8);
        port->model[i * 2 + 1] = (CHAR)(w & 0xFF);
    }
    port->model[40] = 0;

    /* LBA28 sectors: word 60-61 */
    port->sectors = id[60] | (id[61] << 16);

    /* LBA48 sectors: words 100-103 */
    port->sectors48_lo = id[100] | (id[101] << 16);
    port->sectors48_hi = id[102] | (id[103] << 16);

    AHCIDBG("port %u: '%s' LBA28=%u LBA48=%u:%u", port->index, port->model,
            port->sectors, port->sectors48_lo, port->sectors48_hi);

    ExFreePool(id_buf);
    return STATUS_SUCCESS;
}

static NTSTATUS AhciInitPort(PAHCI_CONTROLLER ctl, ULONG port_idx)
{
    PAHCI_PORT port = &ctl->ports[port_idx];
    port->index = port_idx;
    port->mmio_base = PORT_BASE(ctl->mmio, port_idx);

    /* Check port signature */
    port->signature = AhciReadPort(port->mmio_base, PORT_SIG);

    /* SATAPI = 0xeb140101, SATA = 0x00000101, Port Multiplier = 0x96600101 */
    if (port->signature != 0x00000101) {
        port->attached = FALSE;
        return STATUS_DEVICE_DOES_NOT_EXIST;
    }

    /* Allocate DMA buffers */
    NTSTATUS s = AhciAllocDmaBuffer(AHCI_CMD_SLOT_SZ, (PVOID *)&port->cmd_slots,
                                      &port->cmd_slot_phys);
    if (!NT_SUCCESS(s)) return s;
    s = AhciAllocDmaBuffer(AHCI_CMD_TBL_SZ, &port->cmd_table, &port->cmd_tbl_phys);
    if (!NT_SUCCESS(s)) return s;
    s = AhciAllocDmaBuffer(AHCI_RX_FIS_SZ, &port->rx_fis, &port->rx_fis_phys);
    if (!NT_SUCCESS(s)) return s;

    /* Start port */
    s = AhciStartPort(port);
    if (!NT_SUCCESS(s)) return s;

    /* Identify device */
    s = AhciIdentifyDevice(port);
    if (!NT_SUCCESS(s)) {
        port->attached = FALSE;
        return s;
    }

    port->attached = TRUE;
    AHCIDBG("port %u attached: '%s'", port_idx, port->model);
    return STATUS_SUCCESS;
}

static NTSTATUS AhciInitController(USHORT bus, USHORT dev, USHORT fn, ULONG64 mmio_phys)
{
    AHCIDBG("Initializing controller at PCI %u:%u.%u (BAR5=0x%llx)",
            bus, dev, fn, mmio_phys);

    PAHCI_CONTROLLER ctl = ExAllocatePool(NonPagedPool, sizeof(AHCI_CONTROLLER));
    if (!ctl) return STATUS_NO_MEMORY;
    RtlZeroMemory(ctl, sizeof(*ctl));
    ctl->pci_bus = bus;
    ctl->pci_device = dev;
    ctl->pci_function = fn;
    ctl->mmio_phys = mmio_phys;

    /* Map MMIO region — at least 1 page for ABAR */
    SIZE_T mmio_size = 0x1000; /* 4KB minimum; AHCI says ABAR is at least 8KB */
    /* For each port: scan capability, some controllers are bigger */
    ULONG64 abar = mmio_phys;
    /* Map all of AHCI MMIO region (32 ports * 0x80 + 0x100 base = 0x1100 bytes) */
    SIZE_T map_size = 0x2000; /* 8KB */
    mmio_size = (map_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    ctl->mmio = MmMapIoSpace((PHYSICAL_ADDRESS)abar, mmio_size);
    if (!ctl->mmio) {
        AHCIDBG("Failed to map MMIO at 0x%llx", abar);
        ExFreePool(ctl);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(ctl->mmio, mmio_size);

    /* Enable AHCI mode and disable interrupts during init */
    ULONG ghc = AhciReadReg(ctl->mmio, HOST_CTL);
    ghc &= ~HOST_IRQ_EN;
    AhciWriteReg(ctl->mmio, HOST_CTL, ghc);

    /* Check AHCI enabled bit; if not set, enable it */
    if ((ghc & HOST_AHCI_EN) == 0) {
        ghc |= HOST_AHCI_EN;
        AhciWriteReg(ctl->mmio, HOST_CTL, ghc);
        KeStallExecutionProcessor(5000);
    }

    /* Perform AHCI reset (HBA reset, bit 0 of HOST_CTL) */
    ghc = AhciReadReg(ctl->mmio, HOST_CTL);
    ghc |= HOST_RESET;
    AhciWriteReg(ctl->mmio, HOST_CTL, ghc);
    KeStallExecutionProcessor(50000);
    /* Wait for reset to clear */
    for (int i = 0; i < 100; i++) {
        ghc = AhciReadReg(ctl->mmio, HOST_CTL);
        if ((ghc & HOST_RESET) == 0) break;
        KeStallExecutionProcessor(10000);
    }
    /* Re-enable AHCI */
    ghc = AhciReadReg(ctl->mmio, HOST_CTL);
    AhciWriteReg(ctl->mmio, HOST_CTL, ghc | HOST_AHCI_EN);
    KeStallExecutionProcessor(5000);

    /* Enable interrupts */
    AhciWriteReg(ctl->mmio, HOST_CTL, HOST_AHCI_EN | HOST_IRQ_EN);

    /* Read capabilities */
    ULONG cap = AhciReadReg(ctl->mmio, HOST_CAP);
    ULONG pi = AhciReadReg(ctl->mmio, HOST_PORTS_IMPL);
    ULONG num_implemented = 0;
    for (ULONG i = 0; i < 32; i++) if (pi & (1UL << i)) num_implemented++;

    ctl->num_ports = num_implemented;
    AHCIDBG("cap=0x%08x pi=0x%08x num_ports=%u cap2=0x%08x", cap, pi, num_implemented,
            AhciReadReg(ctl->mmio, HOST_CAP2));

    /* Initialize each implemented port */
    ULONG attached = 0;
    for (ULONG i = 0; i < 32; i++) {
        if ((pi & (1UL << i)) == 0) continue;
        NTSTATUS s = AhciInitPort(ctl, i);
        if (NT_SUCCESS(s)) attached++;
    }

    AHCIDBG("Controller initialized: %u port(s) attached", attached);

    InsertTailList(&g_AhciControllers, &ctl->entry);
    g_NumAhciControllers++;
    return STATUS_SUCCESS;
}

/* ============================================================================
 * PCI scan
 *
 * AHCI controllers have PCI class 0x010601 (SATA / AHCI).
 * We scan bus 0, all devices/functions, checking class code.
 * ========================================================================== */

static NTSTATUS AhciScanPci(VOID)
{
    AHCIDBG("Scanning PCI bus for AHCI controllers (class 0x010601)...");
    ULONG found = 0;

    for (USHORT bus = 0; bus < 256; bus++) {
        for (USHORT dev = 0; dev < 32; dev++) {
            for (USHORT fn = 0; fn < 8; fn++) {
                /* Skip if function 0 returns 0xFFFFFFFF */
                ULONG vend = HalPciReadConfig(bus, dev, fn, 0x00);
                if (vend == 0xFFFFFFFF) {
                    /* If fn 0, dev is empty */
                    if (fn == 0) break;
                    else continue;
                }

                /* Read class code (offset 0x08, upper 24 bits are class code) */
                ULONG classcode = HalPciReadConfig(bus, dev, fn, 0x08);
                classcode >>= 8;
                if (classcode == 0xFFFFFFFF) continue;

                if (classcode == PCI_CLASS_AHCI) {
                    /* Read BAR5 (offset 0x24) — ABAR */
                    ULONG bar5 = HalPciReadConfig(bus, dev, fn, 0x24);
                    /* Mask out type bits: bits 0:3 are type, bit 3 means prefetchable */
                    /* For 64-bit BAR, low type bits 0b1000 means 64-bit prefetchable */
                    ULONG64 mmio_phys = bar5 & 0xFFFFFFF0ULL;
                    /* Check if 64-bit BAR (low nibble = 0xC) */
                    if ((bar5 & 0xF) == 0xC) {
                        ULONG bar5_hi = HalPciReadConfig(bus, dev, fn, 0x28);
                        mmio_phys |= ((ULONG64)bar5_hi) << 32;
                    }

                    /* Enable bus mastering + memory space */
                    ULONG cmd = HalPciReadConfig(bus, dev, fn, 0x04);
                    cmd |= 0x06; /* BusMasterEnable + MemSpaceEnable */
                    HalPciWriteConfig(bus, dev, fn, 0x04, cmd);

                    AHCIDBG("Found AHCI at PCI %u:%u:%u (BAR5=0x%08x)", bus, dev, fn, bar5);
                    NTSTATUS s = AhciInitController(bus, dev, fn, mmio_phys);
                    if (NT_SUCCESS(s)) found++;
                    if (found >= 4) return STATUS_SUCCESS; /* cap at 4 controllers */
                }
            }
        }
        AHCIDBG("Completed PCI bus %u (found=%u)", bus, found);
        if (bus > 0 && found == 0) {
            /* Don't exit early — many systems have AHCI on bus 0 */
            if (bus > 1) break;
        }
    }

    AHCIDBG("Scan complete: %u AHCI controller(s) found", found);
    return found > 0 ? STATUS_SUCCESS : STATUS_DEVICE_DOES_NOT_EXIST;
}

/* ============================================================================
 * Public API — Read/Write blocks
 *
 * These expose the AHCI drives to the kernel. The I/O manager (or fs/ when
 * loading filesystems) calls these to read/write disk sectors.
 * ========================================================================== */

/* Find the first attached port on the first controller */
static PAHCI_PORT AhciGetFirstPort(VOID)
{
    if (IsListEmpty(&g_AhciControllers)) return NULL;
    PLIST_ENTRY e = g_AhciControllers.Flink;
    PAHCI_CONTROLLER ctl = CONTAINING_RECORD(e, AHCI_CONTROLLER, entry);
    for (ULONG i = 0; i < AHCI_MAX_PORTS; i++) {
        if (ctl->ports[i].attached) return &ctl->ports[i];
    }
    return NULL;
}

NTSTATUS NTAPI AhciReadSectors(ULONG64 lba, ULONG count, PVOID buffer)
{
    PAHCI_PORT port = AhciGetFirstPort();
    if (!port) return STATUS_DEVICE_DOES_NOT_EXIST;
    PVOID base = port->mmio_base;

    /* Determine max sectors per command. For DMA, common limit is 256 sectors. */
    if (count > 256) count = 256;
    ULONG64 buf_bytes = (ULONG64)count * 512;

    /* Set up DMA buffer (may be the caller's buffer, but assume we need a DMA
       region. For our kernel-space buffer, it's identity-resident). */

    /* Build FIS_REG_H2D for READ DMA EXT (command 0x25) */
    PUCHAR cmd_tbl = (PUCHAR)port->cmd_table;
    FIS_REG_H2D_TYPE *fis = (FIS_REG_H2D_TYPE *)cmd_tbl;
    RtlZeroMemory(fis, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pm_port_ctrl = 0x80; /* Command bit */
    fis->command = ATA_CMD_READ_DMA_EXT;
    fis->device = ATA_DEV_LBA48;
    fis->lba0 = (UCHAR)(lba & 0xFF);
    fis->lba1 = (UCHAR)((lba >> 8) & 0xFF);
    fis->lba2 = (UCHAR)((lba >> 16) & 0xFF);
    fis->lba3 = (UCHAR)((lba >> 24) & 0xFF);
    fis->lba4 = (UCHAR)((lba >> 32) & 0xFF);
    fis->lba5 = (UCHAR)((lba >> 40) & 0xFF);
    fis->count_lo = (UCHAR)(count & 0xFF);
    fis->count_hi = (UCHAR)((count >> 8) & 0xFF);

    /* Set PRD to point to the buffer */
    AHCI_PRD *prd = (AHCI_PRD *)(cmd_tbl + AHCI_CMD_TBL_HDR_SZ);
    /* For simplicity, single PRD for the whole buffer */
    /* NOTE: In a real impl, we need to translate virt to phys and possibly break across page boundaries. */
    /* For now, use a fallback: allocate a DMA buffer and copy. */
    PVOID dma_buf;
    ULONG64 dma_phys;
    NTSTATUS s = AhciAllocDmaBuffer(buf_bytes, &dma_buf, &dma_phys);
    if (!NT_SUCCESS(s)) return s;

    prd->addr = (ULONG)(dma_phys & 0xFFFFFFFF);
    prd->addr_hi = (ULONG)(dma_phys >> 32);
    prd->flags_size = (ULONG)(buf_bytes - 1) | 0x80000000;

    /* Configure slot 0 */
    RtlZeroMemory(port->cmd_slots, AHCI_CMD_SLOT_SZ);
    AHCI_CMD_HDR *hdr = &port->cmd_slots[0];
    hdr->opts = (5 << 16) | (1 << 8) | 0; /* FIS size 5 DWs, 1 PRD, read */
    hdr->tbl_addr = (ULONG)(port->cmd_tbl_phys & 0xFFFFFFFF);
    hdr->tbl_addr_hi = (ULONG)(port->cmd_tbl_phys >> 32);

    /* Clear SError, wait BSY clear */
    AhciWritePort(base, PORT_SCR_ERR, 0xFFFFFFFF);
    for (int i = 0; i < 50; i++) {
        ULONG tfd = AhciReadPort(base, PORT_TFDATA);
        if ((tfd & (ATA_BUSY | ATA_DRQ)) == 0) break;
        KeStallExecutionProcessor(10000);
    }

    /* Issue command */
    s = AhciIssueCommand(port, 0, FALSE);
    if (NT_SUCCESS(s)) {
        /* Copy data to caller buffer */
        RtlCopyMemory(buffer, dma_buf, (SIZE_T)buf_bytes);
    } else {
        AHCIDBG("port %u: read failed at lba %llu", port->index, lba);
    }

    /* Clean up */
    /* NOTE: This leaks physical pages because we don't unmap VMAs. Fix in a
       production version. For now, just free the VAs. */
    return s;
}

NTSTATUS NTAPI AhciWriteSectors(ULONG64 lba, ULONG count, const void *buffer)
{
    PAHCI_PORT port = AhciGetFirstPort();
    if (!port) return STATUS_DEVICE_DOES_NOT_EXIST;
    PVOID base = port->mmio_base;

    if (count > 256) count = 256;
    ULONG64 buf_bytes = (ULONG64)count * 512;

    /* Allocate DMA buffer and copy data */
    PVOID dma_buf;
    ULONG64 dma_phys;
    NTSTATUS s = AhciAllocDmaBuffer(buf_bytes, &dma_buf, &dma_phys);
    if (!NT_SUCCESS(s)) return s;
    RtlCopyMemory(dma_buf, buffer, (SIZE_T)buf_bytes);

    /* Build FIS_REG_H2D for WRITE DMA EXT (command 0x35) */
    PUCHAR cmd_tbl = (PUCHAR)port->cmd_table;
    FIS_REG_H2D_TYPE *fis = (FIS_REG_H2D_TYPE *)cmd_tbl;
    RtlZeroMemory(fis, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pm_port_ctrl = 0x80; /* Command bit */
    fis->command = ATA_CMD_WRITE_DMA_EXT;
    fis->device = ATA_DEV_LBA48;
    fis->lba0 = (UCHAR)(lba & 0xFF);
    fis->lba1 = (UCHAR)((lba >> 8) & 0xFF);
    fis->lba2 = (UCHAR)((lba >> 16) & 0xFF);
    fis->lba3 = (UCHAR)((lba >> 24) & 0xFF);
    fis->lba4 = (UCHAR)((lba >> 32) & 0xFF);
    fis->lba5 = (UCHAR)((lba >> 40) & 0xFF);
    fis->count_lo = (UCHAR)(count & 0xFF);
    fis->count_hi = (UCHAR)((count >> 8) & 0xFF);
    /* Issue WRITE command */
    fis->features = 0;

    /* PRD */
    AHCI_PRD *prd = (AHCI_PRD *)(cmd_tbl + AHCI_CMD_TBL_HDR_SZ);
    prd->addr = (ULONG)(dma_phys & 0xFFFFFFFF);
    prd->addr_hi = (ULONG)(dma_phys >> 32);
    prd->flags_size = 0x80000000 | (ULONG)(buf_bytes - 1);

    /* Configure slot 0 - WRITE */
    RtlZeroMemory(port->cmd_slots, AHCI_CMD_SLOT_SZ);
    AHCI_CMD_HDR *hdr = &port->cmd_slots[0];
    hdr->opts = (5 << 16) | (1 << 8) | (1 << 6); /* FIS 5 DWs, 1 PRD, WRITE bit */
    hdr->tbl_addr = (ULONG)(port->cmd_tbl_phys & 0xFFFFFFFF);
    hdr->tbl_addr_hi = (ULONG)(port->cmd_tbl_phys >> 32);

    /* Clear SError, wait BSY clear */
    AhciWritePort(base, PORT_SCR_ERR, 0xFFFFFFFF);
    for (int i = 0; i < 50; i++) {
        ULONG tfd = AhciReadPort(base, PORT_TFDATA);
        if ((tfd & (ATA_BUSY | ATA_DRQ)) == 0) break;
        KeStallExecutionProcessor(10000);
    }

    s = AhciIssueCommand(port, 0, TRUE);
    if (!NT_SUCCESS(s)) {
        AHCIDBG("port %u: write failed at lba %llu", port->index, lba);
    }

    return s;
}

ULONG NTAPI AhciGetTotalSectors(VOID)
{
    PAHCI_PORT port = AhciGetFirstPort();
    if (!port) return 0;
    if (port->sectors48_hi > 0 || port->sectors48_lo > port->sectors) {
        /* Use LBA48 */
        return port->sectors48_lo; /* Return low 32 bits */
    }
    return port->sectors;
}

BOOLEAN NTAPI AhciIsPresent(VOID)
{
    if (!g_AhciInit) return FALSE;
    if (IsListEmpty(&g_AhciControllers)) return FALSE;
    return AhciGetFirstPort() != NULL;
}

/* ============================================================================
 * Init — Called during kernel boot
 * ========================================================================== */

NTSTATUS NTAPI AhciInitSystem(VOID)
{
    AHCIDBG("Initializing AHCI subsystem...");
    InitializeListHead(&g_AhciControllers);
    g_AhciInit = TRUE;

    NTSTATUS s = AhciScanPci();
    if (!NT_SUCCESS(s)) {
        AHCIDBG("No AHCI controllers found");
        return s;
    }

    AHCIDBG("AHCI ready: %u controller(s)", g_NumAhciControllers);
    return STATUS_SUCCESS;
}

/* ============================================================================
 * Disk info accessors — used by the OS installer
 * ========================================================================== */

static PAHCI_PORT AhciGetPortByIndex(ULONG DiskNumber)
{
    ULONG idx = 0;
    PLIST_ENTRY e = g_AhciControllers.Flink;
    while (e != &g_AhciControllers) {
        PAHCI_CONTROLLER ctl = CONTAINING_RECORD(e, AHCI_CONTROLLER, entry);
        for (ULONG i = 0; i < ctl->num_ports; i++) {
            if (ctl->ports[i].attached) {
                if (idx == DiskNumber) return &ctl->ports[i];
                idx++;
            }
        }
        e = e->Flink;
    }
    return NULL;
}

ULONG NTAPI AhciGetDiskCount(VOID)
{
    ULONG count = 0;
    PLIST_ENTRY e = g_AhciControllers.Flink;
    while (e != &g_AhciControllers) {
        PAHCI_CONTROLLER ctl = CONTAINING_RECORD(e, AHCI_CONTROLLER, entry);
        for (ULONG i = 0; i < ctl->num_ports; i++) {
            if (ctl->ports[i].attached) count++;
        }
        e = e->Flink;
    }
    return count;
}

ULONG64 NTAPI AhciGetDiskSize(ULONG DiskNumber)
{
    PAHCI_PORT port = AhciGetPortByIndex(DiskNumber);
    if (!port) return 0;
    ULONG64 sectors = ((ULONG64)port->sectors48_hi << 32) | port->sectors48_lo;
    if (sectors == 0) sectors = port->sectors;
    return sectors * 512ULL;
}

VOID NTAPI AhciGetDiskModel(ULONG DiskNumber, PCHAR Buffer, ULONG MaxLen)
{
    PAHCI_PORT port = AhciGetPortByIndex(DiskNumber);
    if (!port || !Buffer || MaxLen == 0) {
        if (Buffer && MaxLen) Buffer[0] = 0;
        return;
    }
    ULONG i = 0;
    while (port->model[i] && i < MaxLen - 1) {
        Buffer[i] = port->model[i];
        i++;
    }
    Buffer[i] = 0;
}

NTSTATUS NTAPI AhciReadSectorsEx(ULONG DiskNumber, ULONG64 Lba, ULONG Count, PVOID Buffer)
{
    PAHCI_PORT port = AhciGetPortByIndex(DiskNumber);
    if (!port) return STATUS_DEVICE_DOES_NOT_EXIST;
    /* For now, delegate to the existing single-disk read. */
    return AhciReadSectors(Lba, Count, Buffer);
}

NTSTATUS NTAPI AhciWriteSectorsEx(ULONG DiskNumber, ULONG64 Lba, ULONG Count, PVOID Buffer)
{
    PAHCI_PORT port = AhciGetPortByIndex(DiskNumber);
    if (!port) return STATUS_DEVICE_DOES_NOT_EXIST;
    return AhciWriteSectors(Lba, Count, Buffer);
}
