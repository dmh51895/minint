/*
 * MinNT - drivers/ata/ahci.h
 * AHCI SATA definitions — ported from Linux drivers/ata/ahci.h.
 * PCI class 0x010601 = SATA AHCI controller.
 */

#ifndef _MINNT_AHCI_H_
#define _MINNT_AHCI_H_

#include <nt/ntdef.h>
#include <nt/hal.h>

/* ---- Global controller registers ---- */
#define HOST_CAP            0x00
#define HOST_CTL           0x04
#define HOST_IRQ_STAT      0x08
#define HOST_PORTS_IMPL    0x0c
#define HOST_VERSION       0x10
#define HOST_EM_LOC        0x1c
#define HOST_EM_CTL        0x20
#define HOST_CAP2          0x24

#define HOST_RESET         0x00000001
#define HOST_IRQ_EN        0x00000002
#define HOST_AHCI_EN       0x80000000

#define HOST_CAP_SSS       0x08000000
#define HOST_CAP_NCQ       0x40000000
#define HOST_CAP_64        0x80000000

/* ---- Per-port registers ---- */
#define PORT_LST_ADDR      0x00
#define PORT_LST_ADDR_HI   0x04
#define PORT_FIS_ADDR      0x08
#define PORT_FIS_ADDR_HI   0x0c
#define PORT_IRQ_STAT      0x10
#define PORT_IRQ_MASK      0x14
#define PORT_CMD           0x18
#define PORT_TFDATA        0x20
#define PORT_SIG           0x24
#define PORT_SCR_STAT      0x28
#define PORT_SCR_CTL       0x2c
#define PORT_SCR_ERR       0x30
#define PORT_SCR_ACT       0x34
#define PORT_CMD_ISSUE     0x38

#define PORT_CMD_START     0x00000001
#define PORT_CMD_FIS_RX    0x00000010
#define PORT_CMD_CLO       0x00000008
#define PORT_CMD_POWER_ON  0x00000004
#define PORT_CMD_SPIN_UP   0x00000002
#define PORT_CMD_LIST_ON   0x00008000
#define PORT_CMD_FIS_ON    0x00004000
#define PORT_CMD_ICC_MASK  0xf0000000
#define PORT_CMD_ICC_SLUMBER 0x60000000

#define PORT_IRQ_D2H_REG_FIS 0x00000001
#define PORT_IRQ_PIOS_FIS     0x00000002
#define PORT_IRQ_SDB_FIS      0x00000008
#define PORT_IRQ_SG_DONE      0x00000020
#define PORT_IRQ_CONNECT      0x00000040
#define PORT_IRQ_PHYRDY       0x00400000

#define DEF_PORT_IRQ         (PORT_IRQ_SG_DONE | PORT_IRQ_SDB_FIS | \
                              PORT_IRQ_D2H_REG_FIS | PORT_IRQ_PIOS_FIS)

/* ---- Hardware structures ---- */
#define AHCI_MAX_PORTS        32
#define AHCI_MAX_CMDS         32
#define AHCI_CMD_SZ           32
#define AHCI_CMD_SLOT_SZ      (AHCI_MAX_CMDS * AHCI_CMD_SZ)
#define AHCI_RX_FIS_SZ        256
#define AHCI_MAX_SG           168
#define AHCI_CMD_TBL_HDR_SZ   0x80
#define AHCI_CMD_TBL_SZ       (AHCI_CMD_TBL_HDR_SZ + (AHCI_MAX_SG * 16))

/* Command header (32 bytes) */
typedef struct _AHCI_CMD_HDR {
    ULONG opts;
    ULONG status;
    ULONG tbl_addr;
    ULONG tbl_addr_hi;
    ULONG reserved[4];
} AHCI_CMD_HDR;

/* PRD (scatter-gather) entry (16 bytes) */
typedef struct _AHCI_PRD {
    ULONG addr;
    ULONG addr_hi;
    ULONG reserved;
    ULONG flags_size;
} AHCI_PRD;

/* FIS structures — Host to Device (H2D) */
#define FIS_TYPE_REG_H2D    0x27
typedef struct _FIS_REG_H2D {
    UCHAR fis_type;
    UCHAR pm_port_ctrl; /* pm port<<4 | ctrl bit (0=control, 1=data) */
    UCHAR command;
    UCHAR features;
    UCHAR lba0;
    UCHAR lba1;
    UCHAR lba2;
    UCHAR device;
    UCHAR lba3;
    UCHAR lba4;
    UCHAR lba5;
    UCHAR features_hi;
    UCHAR count_lo;
    UCHAR count_hi;
    UCHAR reserved[6];
} FIS_REG_H2D_TYPE;
#define sizeof_fis_reg_h2d 20

/* Device to Host Register FIS — offset 0x40 in RX FIS area */
typedef struct _FIS_REG_D2H {
    UCHAR fis_type;
    UCHAR pm_port_intr;
    UCHAR status;
    UCHAR error;
    UCHAR lba0;
    UCHAR lba1;
    UCHAR lba2;
    UCHAR device;
    UCHAR lba3;
    UCHAR lba4;
    UCHAR lba5;
    UCHAR reserved;
    UCHAR count_lo;
    UCHAR count_hi;
    UCHAR reserved2[2];
    UCHAR reserved3[4];
} FIS_REG_D2H;

/* ---- ATA commands ---- */
#define ATA_CMD_READ_DMA_EXT      0x25
#define ATA_CMD_WRITE_DMA_EXT     0x35
#define ATA_CMD_READ_SECTORS_EXT  0x24
#define ATA_CMD_WRITE_SECTORS_EXT 0x34
#define ATA_CMD_IDENTIFY_DEVICE   0xec
#define ATA_CMD_FLUSH_CACHE_EXT   0xea
#define ATA_CMD_READ_FPDMA_QUEUED 0x60
#define ATA_CMD_WRITE_FPDMA_QUEUED 0x61

#define ATA_DRQ     0x08
#define ATA_BUSY    0x80
#define ATA_ERR     0x01

#define ATA_DEV_LBA 0xe0
#define ATA_DEV_LBA48 (ATA_DEV_LBA | 0x40)

/* PCI class for AHCI: 0x01 0x06 0x01 (SATA / AHCI) */
#define PCI_CLASS_AHCI 0x010601

#endif /* _MINNT_AHCI_H_ */
