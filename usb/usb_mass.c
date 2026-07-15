/*
 * MinNT - usb/usb_mass.c
 * USB Mass Storage Class Driver (Bulk-Only Transport + SCSI)
 *
 * Implements the USB Mass Storage Class Specification using the
 * Bulk-Only Transport (BOT) protocol. SCSI commands (INQUIRY,
 * READ CAPACITY, READ(10), WRITE(10)) are sent via CBW/CSW
 * wrappers over the USB bulk endpoints.
 *
 * This driver exposes a block-device API identical to the AHCI
 * driver so that the OS installer can enumerate and install to
 * USB-attached portable SSDs, flash drives, and HDD enclosures.
 *
 * NT parallel: classpnp.sys + usbstor.sys
 */

#include <nt/ntdef.h>
#include <nt/usb.h>
#include <nt/usbhcd.h>
#include <nt/ex.h>
#include <nt/rtl.h>

/* ---- BOT (Bulk-Only Transport) protocol structures ---------------------- */

#define USB_BOT_CBW_SIGNATURE   0x43425355U   /* "USBC" */
#define USB_BOT_CSW_SIGNATURE   0x53425355U   /* "USBS" */
#define USB_BOT_CBW_SIZE        31
#define USB_BOT_CSW_SIZE        13

#define CBW_FLAGS_IN            0x80          /* Device-to-Host */
#define CBW_FLAGS_OUT           0x00          /* Host-to-Device */

#define CSW_STATUS_PASSED       0
#define CSW_STATUS_FAILED       1
#define CSW_STATUS_PHASE_ERROR  2

#pragma pack(push, 1)
typedef struct _BOT_CBW {
    ULONG  Signature;
    ULONG  Tag;
    ULONG  DataTransferLength;
    UCHAR  Flags;
    UCHAR  LUN;
    UCHAR  CBLength;
    UCHAR  CB[16];
} BOT_CBW, *PBOT_CBW;

typedef struct _BOT_CSW {
    ULONG  Signature;
    ULONG  Tag;
    ULONG  DataResidue;
    UCHAR  Status;
} BOT_CSW, *PBOT_CSW;
#pragma pack(pop)

/* ---- SCSI command structures -------------------------------------------- */

#pragma pack(push, 1)
typedef struct _SCSI_INQUIRY {
    UCHAR  Opcode;        /* 0x12 */
    UCHAR  EVPD_LUN;
    UCHAR  PageCode;
    UCHAR  AllocationLength_hi;
    UCHAR  AllocationLength_lo;
    UCHAR  Control;
} SCSI_INQUIRY;

typedef struct _SCSI_INQUIRY_DATA {
    UCHAR  PeripheralDeviceType;    /* 0x00 = Direct Access Block */
    UCHAR  RMB;                     /* bit 7 = removable */
    UCHAR  Version;
    UCHAR  ResponseDataFormat;
    UCHAR  AdditionalLength;
    UCHAR  Reserved[3];
    UCHAR  VendorId[8];
    UCHAR  ProductId[16];
    UCHAR  ProductRevision[4];
} SCSI_INQUIRY_DATA, *PSCSI_INQUIRY_DATA;

typedef struct _SCSI_READ_CAPACITY {
    UCHAR  Opcode;        /* 0x25 */
    UCHAR  LBA[4];
    UCHAR  Reserved[2];
    UCHAR  PMI;
    UCHAR  Control;
} SCSI_READ_CAPACITY;

typedef struct _SCSI_READ_CAPACITY_DATA {
    ULONG  LastLBA;
    ULONG  BlockSize;
} SCSI_READ_CAPACITY_DATA, *PSCSI_READ_CAPACITY_DATA;

typedef struct _SCSI_READ10 {
    UCHAR  Opcode;        /* 0x28 */
    UCHAR  Flags;
    UCHAR  LBA[4];
    UCHAR  Group;
    UCHAR  TransferLength[2];
    UCHAR  Control;
} SCSI_READ10;

typedef struct _SCSI_WRITE10 {
    UCHAR  Opcode;        /* 0x2A */
    UCHAR  Flags;
    UCHAR  LBA[4];
    UCHAR  Group;
    UCHAR  TransferLength[2];
    UCHAR  Control;
} SCSI_WRITE10;

typedef struct _SCSI_TEST_UNIT_READY {
    UCHAR  Opcode;        /* 0x00 */
    UCHAR  Reserved[4];
    UCHAR  Control;
} SCSI_TEST_UNIT_READY;

typedef struct _SCSI_REQUEST_SENSE {
    UCHAR  Opcode;        /* 0x03 */
    UCHAR  DESC_LUN;
    UCHAR  Reserved[2];
    UCHAR  AllocationLength;
    UCHAR  Control;
} SCSI_REQUEST_SENSE;
#pragma pack(pop)

/* ---- USB Mass Storage device table -------------------------------------- */

#define USB_MASS_MAX_DISKS   8

typedef struct _USB_MASS_DISK {
    BOOLEAN     Present;
    UCHAR       DeviceAddress;
    UCHAR       BulkInEndpoint;
    UCHAR       BulkOutEndpoint;
    UCHAR       Speed;
    ULONG       BlockSize;
    ULONG64     DiskSize;        /* total bytes */
    ULONG       SectorCount;
    CHAR        ModelName[40];
} USB_MASS_DISK;

static USB_MASS_DISK g_UsbMassDisks[USB_MASS_MAX_DISKS];
static ULONG g_UsbMassDiskCount = 0;
static BOOLEAN g_UsbMassInitialized = FALSE;

/* ---- BOT transport primitives ------------------------------------------- */

static ULONG g_BotTag = 0xDEADBEEF;

static NTSTATUS UsbMassBotCommand(
    UCHAR DevAddr, UCHAR Speed,
    UCHAR BulkInEp, UCHAR BulkOutEp,
    PVOID Cb, UCHAR CbLength,
    PVOID DataBuffer, ULONG DataLength,
    BOOLEAN IsDataIn)
{
    BOT_CBW cbw;
    BOT_CSW csw;
    ULONG actual;
    NTSTATUS status;

    if (CbLength > 16) CbLength = 16;
    if (CbLength == 0) return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&cbw, sizeof(cbw));
    cbw.Signature = USB_BOT_CBW_SIGNATURE;
    cbw.Tag = ++g_BotTag;
    cbw.DataTransferLength = DataLength;
    cbw.Flags = IsDataIn ? CBW_FLAGS_IN : CBW_FLAGS_OUT;
    cbw.LUN = 0;
    cbw.CBLength = CbLength;
    RtlCopyMemory(cbw.CB, Cb, CbLength);

    status = USB_HCD_BULK_TRANSFER(DevAddr, BulkOutEp, Speed,
                                   &cbw, USB_BOT_CBW_SIZE,
                                   FALSE, &actual);
    if (!NT_SUCCESS(status) || actual != USB_BOT_CBW_SIZE) {
        DbgPrint("USBMASS: CBW send failed (status=0x%lx actual=%u)\n",
                 status, actual);
        return status;
    }

    if (DataLength > 0 && DataBuffer) {
        UCHAR ep = IsDataIn ? BulkInEp : BulkOutEp;
        status = USB_HCD_BULK_TRANSFER(DevAddr, ep, Speed,
                                       DataBuffer, DataLength,
                                       IsDataIn, &actual);
        if (!NT_SUCCESS(status)) {
            DbgPrint("USBMASS: data transfer failed (status=0x%lx)\n", status);
            return status;
        }
    }

    status = USB_HCD_BULK_TRANSFER(DevAddr, BulkInEp, Speed,
                                   &csw, USB_BOT_CSW_SIZE,
                                   TRUE, &actual);
    if (!NT_SUCCESS(status) || actual != USB_BOT_CSW_SIZE) {
        DbgPrint("USBMASS: CSW recv failed (status=0x%lx actual=%u)\n",
                 status, actual);
        return status;
    }

    if (csw.Signature != USB_BOT_CSW_SIGNATURE) {
        DbgPrint("USBMASS: bad CSW signature 0x%08x\n", csw.Signature);
        return STATUS_IO_DEVICE_ERROR;
    }
    if (csw.Tag != cbw.Tag) {
        DbgPrint("USBMASS: CSW tag mismatch (got %u expected %u)\n",
                 csw.Tag, cbw.Tag);
        return STATUS_IO_DEVICE_ERROR;
    }
    if (csw.Status != CSW_STATUS_PASSED) {
        DbgPrint("USBMASS: CSW status %u (residue=%u)\n",
                 csw.Status, csw.DataResidue);
        return STATUS_IO_DEVICE_ERROR;
    }

    return STATUS_SUCCESS;
}

/* ---- SCSI command wrappers ---------------------------------------------- */

static NTSTATUS UsbMassTestUnitReady(USB_MASS_DISK *Disk)
{
    SCSI_TEST_UNIT_READY cmd;
    RtlZeroMemory(&cmd, sizeof(cmd));
    cmd.Opcode = 0x00;
    return UsbMassBotCommand(Disk->DeviceAddress, Disk->Speed,
                             Disk->BulkInEndpoint, Disk->BulkOutEndpoint,
                             &cmd, 6, NULL, 0, FALSE);
}

static NTSTATUS UsbMassInquiry(USB_MASS_DISK *Disk,
                                PSCSI_INQUIRY_DATA Info)
{
    SCSI_INQUIRY cmd;
    RtlZeroMemory(&cmd, sizeof(cmd));
    cmd.Opcode = 0x12;
    cmd.AllocationLength_lo = sizeof(SCSI_INQUIRY_DATA);
    return UsbMassBotCommand(Disk->DeviceAddress, Disk->Speed,
                             Disk->BulkInEndpoint, Disk->BulkOutEndpoint,
                             &cmd, 6, Info, sizeof(SCSI_INQUIRY_DATA), TRUE);
}

static NTSTATUS UsbMassReadCapacity(USB_MASS_DISK *Disk,
                                     PSCSI_READ_CAPACITY_DATA Cap)
{
    SCSI_READ_CAPACITY cmd;
    RtlZeroMemory(&cmd, sizeof(cmd));
    cmd.Opcode = 0x25;
    return UsbMassBotCommand(Disk->DeviceAddress, Disk->Speed,
                             Disk->BulkInEndpoint, Disk->BulkOutEndpoint,
                             &cmd, 10, Cap, sizeof(SCSI_READ_CAPACITY_DATA),
                             TRUE);
}

/* ---- Public API --------------------------------------------------------- */

NTSTATUS NTAPI UsbMassInit(VOID)
{
    USBENUM_DEVICE_CONTEXT enumList[16];
    ULONG found, i;

    if (g_UsbMassInitialized) return STATUS_SUCCESS;
    g_UsbMassInitialized = TRUE;
    g_UsbMassDiskCount = 0;

    RtlZeroMemory(g_UsbMassDisks, sizeof(g_UsbMassDisks));

    if (!UsbActiveHcd.Initialized) {
        DbgPrint("USBMASS: no active USB HCD, skipping\n");
        return STATUS_DEVICE_DOES_NOT_EXIST;
    }

    found = UsbEnumerateDevices(enumList, 16);
    DbgPrint("USBMASS: %lu USB device(s) enumerated\n", found);

    for (i = 0; i < found && g_UsbMassDiskCount < USB_MASS_MAX_DISKS; i++) {
        if (!enumList[i].Present) continue;
        if (!enumList[i].BulkInPipe || !enumList[i].BulkOutPipe) continue;

        USB_MASS_DISK *d = &g_UsbMassDisks[g_UsbMassDiskCount];
        RtlZeroMemory(d, sizeof(*d));
        d->Present = TRUE;
        d->DeviceAddress = enumList[i].DeviceAddress;
        d->BulkInEndpoint = (UCHAR)(ULONG_PTR)enumList[i].BulkInPipe & 0xFF;
        d->BulkOutEndpoint = (UCHAR)(ULONG_PTR)enumList[i].BulkOutPipe & 0xFF;
        d->Speed = 0;
        d->BlockSize = 512;

        NTSTATUS s = UsbMassTestUnitReady(d);
        if (!NT_SUCCESS(s)) {
            DbgPrint("USBMASS: device %u not ready (0x%lx)\n", i, s);
            continue;
        }

        SCSI_INQUIRY_DATA inquiry;
        s = UsbMassInquiry(d, &inquiry);
        if (NT_SUCCESS(s)) {
            ULONG k = 0;
            for (ULONG j = 0; j < 16 && k < 38; j++) {
                if (inquiry.ProductId[j] == 0 || inquiry.ProductId[j] == ' ')
                    continue;
                d->ModelName[k++] = inquiry.ProductId[j];
            }
            d->ModelName[k] = 0;
            if (k == 0) {
                RtlCopyMemory(d->ModelName, "USB Disk", 9);
            }
            DbgPrint("USBMASS: device %u: %s (type=0x%02x)\n",
                     i, d->ModelName, inquiry.PeripheralDeviceType);
        } else {
            RtlCopyMemory(d->ModelName, "USB Disk", 9);
        }

        SCSI_READ_CAPACITY_DATA cap;
        s = UsbMassReadCapacity(d, &cap);
        if (NT_SUCCESS(s)) {
            d->SectorCount = (cap.LastLBA + 1);
            d->DiskSize = (ULONG64)(cap.LastLBA + 1) * cap.BlockSize;
            d->BlockSize = cap.BlockSize;
            DbgPrint("USBMASS: disk %u: %lu sectors, %lu bytes/sector, %llu bytes total\n",
                     g_UsbMassDiskCount, d->SectorCount, d->BlockSize, d->DiskSize);
        } else {
            DbgPrint("USBMASS: READ CAPACITY failed (0x%lx), assuming 512-byte sectors\n", s);
            d->DiskSize = 0;
            d->SectorCount = 0;
        }

        g_UsbMassDiskCount++;
    }

    DbgPrint("USBMASS: %u mass storage disk(s) initialized\n",
             g_UsbMassDiskCount);
    return STATUS_SUCCESS;
}

ULONG NTAPI UsbMassGetDiskCount(VOID)
{
    return g_UsbMassDiskCount;
}

ULONG64 NTAPI UsbMassGetDiskSize(ULONG DiskNumber)
{
    if (DiskNumber >= g_UsbMassDiskCount) return 0;
    return g_UsbMassDisks[DiskNumber].DiskSize;
}

VOID NTAPI UsbMassGetDiskModel(ULONG DiskNumber, PCHAR Buffer, ULONG MaxLen)
{
    if (DiskNumber >= g_UsbMassDiskCount || !Buffer || MaxLen == 0) return;
    ULONG k = 0;
    while (k < MaxLen - 1 && g_UsbMassDisks[DiskNumber].ModelName[k]) {
        Buffer[k] = g_UsbMassDisks[DiskNumber].ModelName[k];
        k++;
    }
    Buffer[k] = 0;
}

NTSTATUS NTAPI UsbMassReadSectors(ULONG DiskNumber, ULONG64 Lba,
                                   ULONG Count, PVOID Buffer)
{
    if (DiskNumber >= g_UsbMassDiskCount) return STATUS_DEVICE_DOES_NOT_EXIST;
    USB_MASS_DISK *d = &g_UsbMassDisks[DiskNumber];

    SCSI_READ10 cmd;
    RtlZeroMemory(&cmd, sizeof(cmd));
    cmd.Opcode = 0x28;
    cmd.LBA[0] = (UCHAR)(Lba >> 24);
    cmd.LBA[1] = (UCHAR)(Lba >> 16);
    cmd.LBA[2] = (UCHAR)(Lba >> 8);
    cmd.LBA[3] = (UCHAR)(Lba);
    cmd.TransferLength[0] = (UCHAR)(Count >> 8);
    cmd.TransferLength[1] = (UCHAR)(Count);

    ULONG totalBytes = Count * d->BlockSize;
    return UsbMassBotCommand(d->DeviceAddress, d->Speed,
                              d->BulkInEndpoint, d->BulkOutEndpoint,
                              &cmd, 10, Buffer, totalBytes, TRUE);
}

NTSTATUS NTAPI UsbMassWriteSectors(ULONG DiskNumber, ULONG64 Lba,
                                    ULONG Count, PVOID Buffer)
{
    if (DiskNumber >= g_UsbMassDiskCount) return STATUS_DEVICE_DOES_NOT_EXIST;
    USB_MASS_DISK *d = &g_UsbMassDisks[DiskNumber];

    SCSI_WRITE10 cmd;
    RtlZeroMemory(&cmd, sizeof(cmd));
    cmd.Opcode = 0x2A;
    cmd.LBA[0] = (UCHAR)(Lba >> 24);
    cmd.LBA[1] = (UCHAR)(Lba >> 16);
    cmd.LBA[2] = (UCHAR)(Lba >> 8);
    cmd.LBA[3] = (UCHAR)(Lba);
    cmd.TransferLength[0] = (UCHAR)(Count >> 8);
    cmd.TransferLength[1] = (UCHAR)(Count);

    ULONG totalBytes = Count * d->BlockSize;
    return UsbMassBotCommand(d->DeviceAddress, d->Speed,
                              d->BulkInEndpoint, d->BulkOutEndpoint,
                              &cmd, 10, Buffer, totalBytes, FALSE);
}
