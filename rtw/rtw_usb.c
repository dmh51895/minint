/*
 * MinNT - rtw/rtw_usb.c
 * Realtek 8821CU USB WiFi driver — USB transport layer.
 * Ported from ReactOS rtw88. Uses MinNT's I/O Manager + USB stack.
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/hal.h>
#include <nt/usb.h>
#include <nt/ps.h>
#include <rtw/rtw_usb.h>
#include <nt/rtl.h>
#include <ndis.h>
#include <rtw_ndis.h>

#define TAG_RTW 0x575452  /* 'RTW' */

/* ---- Forward declaration ---------------------------------------------- */

static NTSTATUS RtwUsbSubmitBulkUrb(PRTW_ADAPTER Adapter, UCHAR Endpoint,
                                     PVOID Buffer, ULONG Length,
                                     PULONG BytesTransferred, BOOLEAN IsIn);

/* ---- Control request helper ------------------------------------------- */

static NTSTATUS
RtwUsbControlRequest(PRTW_ADAPTER Adapter,
                     UCHAR RequestType, UCHAR Request,
                     USHORT Value, USHORT Index,
                     PVOID Data, USHORT Length, BOOLEAN IsRead)
{
    PURB Urb;

    Urb = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(URB_CONTROL_VENDOR_OR_CLASS_REQUEST),
                                TAG_RTW);
    if (!Urb)
        return STATUS_NO_MEMORY;

    RtlZeroMemory(Urb, sizeof(URB_CONTROL_VENDOR_OR_CLASS_REQUEST));
    Urb->Header.Length = sizeof(URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
    Urb->Header.Function = URB_FUNCTION_VENDOR_DEVICE;
    Urb->ControlVendorClassRequest.TransferBufferLength = Length;
    Urb->ControlVendorClassRequest.TransferBuffer = Data;
    Urb->ControlVendorClassRequest.RequestTypeReservedBits = RequestType;
    Urb->ControlVendorClassRequest.Request = Request;
    Urb->ControlVendorClassRequest.Value = Value;
    Urb->ControlVendorClassRequest.Index = Index;
    Urb->ControlVendorClassRequest.TransferFlags =
        IsRead ? USBD_TRANSFER_DIRECTION_IN : 0;

    NTSTATUS Status = UsbSubmitUrb(Adapter->UsbDevice, Urb);
    ExFreePoolWithTag(Urb, TAG_RTW);
    return Status;
}

/* ---- Register read/write ---------------------------------------------- */

NTSTATUS NTAPI RtwUsbRead8(PRTW_ADAPTER Adapter, ULONG Offset, PUCHAR Value)
{
    return RtwUsbControlRequest(Adapter, RTW_USB_VENDOR_REQUEST,
                                RTW_USB_REG_READ,
                                (USHORT)(Offset & 0xFFFF),
                                (USHORT)(Offset >> 16),
                                Value, sizeof(UCHAR), TRUE);
}

NTSTATUS NTAPI RtwUsbRead16(PRTW_ADAPTER Adapter, ULONG Offset, PUSHORT Value)
{
    return RtwUsbControlRequest(Adapter, RTW_USB_VENDOR_REQUEST,
                                RTW_USB_REG_READ,
                                (USHORT)(Offset & 0xFFFF),
                                (USHORT)(Offset >> 16),
                                Value, sizeof(USHORT), TRUE);
}

NTSTATUS NTAPI RtwUsbRead32(PRTW_ADAPTER Adapter, ULONG Offset, PULONG Value)
{
    return RtwUsbControlRequest(Adapter, RTW_USB_VENDOR_REQUEST,
                                RTW_USB_REG_READ,
                                (USHORT)(Offset & 0xFFFF),
                                (USHORT)(Offset >> 16),
                                Value, sizeof(ULONG), TRUE);
}

NTSTATUS NTAPI RtwUsbWrite8(PRTW_ADAPTER Adapter, ULONG Offset, UCHAR Value)
{
    return RtwUsbControlRequest(Adapter, RTW_USB_VENDOR_REQUEST,
                                RTW_USB_REG_WRITE,
                                (USHORT)(Offset & 0xFFFF),
                                (USHORT)(Offset >> 16),
                                &Value, sizeof(UCHAR), FALSE);
}

NTSTATUS NTAPI RtwUsbWrite16(PRTW_ADAPTER Adapter, ULONG Offset, USHORT Value)
{
    return RtwUsbControlRequest(Adapter, RTW_USB_VENDOR_REQUEST,
                                RTW_USB_REG_WRITE,
                                (USHORT)(Offset & 0xFFFF),
                                (USHORT)(Offset >> 16),
                                &Value, sizeof(USHORT), FALSE);
}

NTSTATUS NTAPI RtwUsbWrite32(PRTW_ADAPTER Adapter, ULONG Offset, ULONG Value)
{
    return RtwUsbControlRequest(Adapter, RTW_USB_VENDOR_REQUEST,
                                RTW_USB_REG_WRITE,
                                (USHORT)(Offset & 0xFFFF),
                                (USHORT)(Offset >> 16),
                                &Value, sizeof(ULONG), FALSE);
}

/* ---- Bulk transfer helper --------------------------------------------- */

static NTSTATUS
RtwUsbSubmitBulkUrb(PRTW_ADAPTER Adapter, UCHAR Endpoint,
                     PVOID Buffer, ULONG Length,
                     PULONG BytesTransferred, BOOLEAN IsIn)
{
    PURB Urb;

    if (BytesTransferred)
        *BytesTransferred = 0;

    Urb = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(URB_BULK_OR_INTERRUPT_TRANSFER),
                                TAG_RTW);
    if (!Urb)
        return STATUS_NO_MEMORY;

    RtlZeroMemory(Urb, sizeof(URB_BULK_OR_INTERRUPT_TRANSFER));
    Urb->Header.Length = sizeof(URB_BULK_OR_INTERRUPT_TRANSFER);
    Urb->Header.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
    Urb->BulkOrInterruptTransfer.TransferFlags =
        IsIn ? (USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK) : 0;
    Urb->BulkOrInterruptTransfer.TransferBufferLength = Length;
    Urb->BulkOrInterruptTransfer.TransferBuffer = Buffer;

    /* Map endpoint address to pipe handle */
    if (Endpoint == RTW_USB_BULK_IN_PIPE)
        Urb->BulkOrInterruptTransfer.PipeHandle = Adapter->UsbDevice->BulkInPipe;
    else if (Endpoint == RTW_USB_BULK_OUT_PIPE)
        Urb->BulkOrInterruptTransfer.PipeHandle = Adapter->UsbDevice->BulkOutPipe;
    else
        Urb->BulkOrInterruptTransfer.PipeHandle = Adapter->UsbDevice->InterruptPipe;

    NTSTATUS Status = UsbSubmitUrb(Adapter->UsbDevice, Urb);

    if (NT_SUCCESS(Status) && BytesTransferred)
        *BytesTransferred = Urb->BulkOrInterruptTransfer.TransferBufferLength;

    ExFreePoolWithTag(Urb, TAG_RTW);
    return Status;
}

/* ---- Bulk IN/OUT ------------------------------------------------------ */

NTSTATUS NTAPI RtwUsbBulkIn(PRTW_ADAPTER Adapter, PVOID Buffer,
                             ULONG Length, PULONG BytesRead)
{
    return RtwUsbSubmitBulkUrb(Adapter, RTW_USB_BULK_IN_PIPE,
                                Buffer, Length, BytesRead, TRUE);
}

NTSTATUS NTAPI RtwUsbBulkOut(PRTW_ADAPTER Adapter, PVOID Buffer, ULONG Length)
{
    UNREFERENCED_PARAMETER(Adapter);
    return RtwUsbSubmitBulkUrb(Adapter, RTW_USB_BULK_OUT_PIPE,
                                Buffer, Length, NULL, FALSE);
}

/* ---- Interrupt pipe (polling — async notification) --------------------- */

static UCHAR InterruptBuffer[RTW_USB_MAX_BULK_SIZE];

NTSTATUS NTAPI RtwUsbStartInterruptPipe(PRTW_ADAPTER Adapter)
{
    PURB Urb;
    NTSTATUS Status;

    if (!Adapter || !Adapter->UsbDevice)
        return STATUS_INVALID_PARAMETER;

    Urb = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(URB_BULK_OR_INTERRUPT_TRANSFER),
                                TAG_RTW);
    if (!Urb)
        return STATUS_NO_MEMORY;

    RtlZeroMemory(Urb, sizeof(URB_BULK_OR_INTERRUPT_TRANSFER));
    Urb->Header.Length = sizeof(URB_BULK_OR_INTERRUPT_TRANSFER);
    Urb->Header.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
    Urb->BulkOrInterruptTransfer.TransferFlags =
        USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK;
    Urb->BulkOrInterruptTransfer.TransferBufferLength = sizeof(InterruptBuffer);
    Urb->BulkOrInterruptTransfer.TransferBuffer = InterruptBuffer;
    Urb->BulkOrInterruptTransfer.PipeHandle = Adapter->UsbDevice->InterruptPipe;

    Status = UsbSubmitUrb(Adapter->UsbDevice, Urb);

    ExFreePoolWithTag(Urb, TAG_RTW);

    if (NT_SUCCESS(Status))
        DbgPrint("RTW: interrupt pipe started\n");
    else
        DbgPrint("RTW: interrupt pipe start failed 0x%08lx\n",
                 (unsigned long)Status);

    return Status;
}

VOID NTAPI RtwUsbStopInterruptPipe(PRTW_ADAPTER Adapter)
{
    if (!Adapter || !Adapter->UsbDevice)
        return;

    PURB_PIPE_REQUEST AbortUrb;

    AbortUrb = ExAllocatePoolWithTag(NonPagedPool,
                                     sizeof(URB_PIPE_REQUEST),
                                     TAG_RTW);
    if (!AbortUrb)
        return;

    RtlZeroMemory(AbortUrb, sizeof(URB_PIPE_REQUEST));
    AbortUrb->Header.Length = sizeof(URB_PIPE_REQUEST);
    AbortUrb->Header.Function = URB_FUNCTION_ABORT_PIPE;
    AbortUrb->PipeHandle = Adapter->UsbDevice->InterruptPipe;

    UsbSubmitUrb(Adapter->UsbDevice, (PURB)AbortUrb);
    ExFreePoolWithTag(AbortUrb, TAG_RTW);

    DbgPrint("RTW: interrupt pipe stopped\n");
}

/* ---- Firmware loading ------------------------------------------------- */

/* ---- Firmware loading (BUG-005: Option A, embedded blob) --------------- */

/*
 * The firmware blob (linux-firmware rtw88/rtw8821c_fw.bin) is embedded in
 * kernel .rodata via firmware/rtw_fw_blob.S (.incbin). When MinNT grows a
 * FAT driver, RtwUsbReadFirmware keeps this exact signature and reads
 * FirmwareName from \SystemRoot\system32\drivers instead — nothing above
 * this layer changes.
 */
extern const UCHAR RtwFwBlobStart[];
extern const UCHAR RtwFwBlobEnd[];

/* rtw88 firmware header layout (first 32 bytes of the blob) */
#define RTW_FW_HDR_SIZE        32
#define RTW_FW_HDR_SIGNATURE   0x8821   /* le16 @ 0x00 — matches chip ID  */
#define RTW_FW_HDR_VERSION_OFF 0x04     /* le16 firmware version          */
#define RTW_FW_HDR_SUBVER_OFF  0x06     /* u8 subversion                  */

/* Download protocol registers (from the ReactOS rtw88 port) */
#define RTW_FW_REG_CTRL        0x80     /* start address / run command    */
#define RTW_FW_REG_SIZE        0x84     /* firmware payload size          */
#define RTW_FW_REG_CSUM        0x88     /* one-byte additive checksum     */
#define RTW_FW_REG_SIG         0x8C     /* signature readback             */
#define RTW_FW_START_ADDR      0x1000   /* download window base           */
#define RTW_FW_RUN_CMD         0x1001   /* start firmware execution       */
#define RTW_FW_SIGNATURE       0x2300   /* expected readback after load   */
#define RTW_FW_CHUNK_SIZE      256      /* per vendor-control write       */
#define RTW_FW_SIG_RETRIES     20       /* x 10ms = 200ms budget          */

NTSTATUS NTAPI RtwUsbReadFirmware(PRTW_ADAPTER Adapter, PCWSTR FirmwareName)
{
    ULONG Size = (ULONG)(RtwFwBlobEnd - RtwFwBlobStart);
    USHORT Signature;
    USHORT Version;
    UCHAR SubVersion;

    UNREFERENCED_PARAMETER(FirmwareName); /* used once the FAT driver lands */

    if (Size == 0)
    {
        DbgPrint("RTW: no firmware embedded (empty blob) — "
                 "run firmware/fetch-firmware.sh and rebuild\n");
        return STATUS_NO_SUCH_FILE;
    }

    if (Size < RTW_FW_HDR_SIZE)
    {
        DbgPrint("RTW: embedded firmware too small (%lu bytes)\n",
                 (unsigned long)Size);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    Signature  = (USHORT)(RtwFwBlobStart[0] | (RtwFwBlobStart[1] << 8));
    Version    = (USHORT)(RtwFwBlobStart[RTW_FW_HDR_VERSION_OFF] |
                          (RtwFwBlobStart[RTW_FW_HDR_VERSION_OFF + 1] << 8));
    SubVersion = RtwFwBlobStart[RTW_FW_HDR_SUBVER_OFF];

    if (Signature != RTW_FW_HDR_SIGNATURE)
    {
        DbgPrint("RTW: bad firmware signature 0x%04x (expect 0x8821)\n",
                 (unsigned)Signature);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    Adapter->FirmwareData = (PVOID)RtwFwBlobStart; /* .rodata, no copy */
    Adapter->FirmwareSize = Size;

    DbgPrint("RTW: embedded firmware v%u.%u, %lu bytes, sig 0x%04x OK\n",
             (unsigned)Version, (unsigned)SubVersion,
             (unsigned long)Size, (unsigned)Signature);
    return STATUS_SUCCESS;
}

/*
 * Write a buffer to chip register space via one vendor control transfer.
 * The control pipe caps at RTW_USB_MAX_CTRL_SIZE per request; the
 * download loop below stays well under that with 256-byte chunks.
 */
static NTSTATUS
RtwUsbWriteBuffer(PRTW_ADAPTER Adapter, ULONG Offset,
                  const VOID *Data, USHORT Length)
{
    return RtwUsbControlRequest(Adapter, RTW_USB_VENDOR_REQUEST,
                                RTW_USB_REG_WRITE,
                                (USHORT)(Offset & 0xFFFF),
                                (USHORT)(Offset >> 16),
                                (PVOID)Data, Length, FALSE);
}

NTSTATUS NTAPI RtwUsbDownloadFirmware(PRTW_ADAPTER Adapter)
{
    NTSTATUS Status;
    const UCHAR *Fw;
    ULONG Size, Offset;
    UCHAR Checksum = 0;
    USHORT SigReadback = 0;
    ULONG Retry;

    if (!Adapter->FirmwareData || Adapter->FirmwareSize == 0)
        return STATUS_NO_SUCH_FILE;

    Fw   = (const UCHAR *)Adapter->FirmwareData;
    Size = Adapter->FirmwareSize;

    DbgPrint("RTW: downloading firmware (%lu bytes)...\n",
             (unsigned long)Size);

    /* Step 1: program download window base and payload size */
    Status = RtwUsbWrite32(Adapter, RTW_FW_REG_CTRL, RTW_FW_START_ADDR);
    if (!NT_SUCCESS(Status)) return Status;

    Status = RtwUsbWrite32(Adapter, RTW_FW_REG_SIZE, Size);
    if (!NT_SUCCESS(Status)) return Status;

    /* Step 2: stream the payload in 256-byte chunks through the
       vendor control pipe, computing the checksum as we go */
    for (Offset = 0; Offset < Size; Offset += RTW_FW_CHUNK_SIZE)
    {
        USHORT Chunk = (USHORT)((Size - Offset) < RTW_FW_CHUNK_SIZE
                                ? (Size - Offset) : RTW_FW_CHUNK_SIZE);
        ULONG i;

        Status = RtwUsbWriteBuffer(Adapter, RTW_FW_START_ADDR + Offset,
                                   Fw + Offset, Chunk);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("RTW: firmware chunk @%lu failed (0x%08lx)\n",
                     (unsigned long)Offset, (unsigned long)Status);
            return Status;
        }

        for (i = 0; i < Chunk; i++)
            Checksum = (UCHAR)(Checksum + Fw[Offset + i]);

        if ((Offset & 0x7FFF) == 0 && Offset != 0)
            DbgPrint("RTW:   %lu / %lu bytes\n",
                     (unsigned long)Offset, (unsigned long)Size);
    }

    /* Step 3: write the one-byte additive checksum */
    Status = RtwUsbWrite8(Adapter, RTW_FW_REG_CSUM, Checksum);
    if (!NT_SUCCESS(Status)) return Status;

    /* Step 4: poll the signature readback — chip verifies the image */
    for (Retry = 0; Retry < RTW_FW_SIG_RETRIES; Retry++)
    {
        Status = RtwUsbRead16(Adapter, RTW_FW_REG_SIG, &SigReadback);
        if (NT_SUCCESS(Status) && SigReadback == RTW_FW_SIGNATURE)
            break;
        KeStallExecutionProcessor(10000); /* 10ms */
    }

    if (SigReadback != RTW_FW_SIGNATURE)
    {
        DbgPrint("RTW: firmware signature readback 0x%04x "
                 "(expect 0x%04x) — download rejected\n",
                 (unsigned)SigReadback, (unsigned)RTW_FW_SIGNATURE);
        return STATUS_DEVICE_DATA_ERROR;
    }

    /* Step 5: kick the firmware */
    Status = RtwUsbWrite32(Adapter, RTW_FW_REG_CTRL, RTW_FW_RUN_CMD);
    if (!NT_SUCCESS(Status)) return Status;

    DbgPrint("RTW: firmware running (checksum 0x%02x, sig 0x%04x)\n",
             (unsigned)Checksum, (unsigned)SigReadback);
    return STATUS_SUCCESS;
}

/* ---- Chip initialization ---------------------------------------------- */

static NTSTATUS Rtw8821cuInit(PRTW_ADAPTER Adapter)
{
    NTSTATUS Status;
    UCHAR ChipVersion;
    USHORT ChipId;
    ULONG Value;

    Status = RtwUsbRead8(Adapter, 0x04, &ChipVersion);
    if (!NT_SUCCESS(Status)) return Status;

    Status = RtwUsbRead16(Adapter, 0xFE, &ChipId);
    if (!NT_SUCCESS(Status)) return Status;

    DbgPrint("RTW: chip ver=0x%02x id=0x%04x\n",
             (unsigned)ChipVersion, (unsigned)ChipId);

    /* Power on */
    Status = RtwUsbWrite8(Adapter, 0x04, 0x01);
    if (!NT_SUCCESS(Status)) return Status;
    KeStallExecutionProcessor(10000);

    /* Enable WiFi */
    Status = RtwUsbWrite32(Adapter, 0x24, 0x00000001);
    if (!NT_SUCCESS(Status)) return Status;

    /* BUG-005: load + download firmware. Without it the chip enumerates
       and answers control reads, but the radio will not TX/RX. */
    {
        static const WCHAR FwName[] =
            {'r','t','l','8','8','2','1','c','u','.','b','i','n',0};
        Status = RtwUsbReadFirmware(Adapter, FwName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = RtwUsbDownloadFirmware(Adapter);
        if (!NT_SUCCESS(Status))
            DbgPrint("RTW: firmware download failed (0x%08x) — "
                     "radio disabled, control path still up\n",
                     (unsigned)Status);
    }
    else
    {
        DbgPrint("RTW: continuing without firmware — radio disabled\n");
    }

    /* Read MAC from efuse */
    Status = RtwUsbRead32(Adapter, 0x0150, &Value);
    if (NT_SUCCESS(Status))
    {
        Adapter->PermanentMacAddress[0] = (UCHAR)(Value & 0xFF);
        Adapter->PermanentMacAddress[1] = (UCHAR)((Value >> 8) & 0xFF);
        Adapter->PermanentMacAddress[2] = (UCHAR)((Value >> 16) & 0xFF);
        Adapter->PermanentMacAddress[3] = (UCHAR)((Value >> 24) & 0xFF);

        Status = RtwUsbRead32(Adapter, 0x0154, &Value);
        if (NT_SUCCESS(Status))
        {
            Adapter->PermanentMacAddress[4] = (UCHAR)(Value & 0xFF);
            Adapter->PermanentMacAddress[5] = (UCHAR)((Value >> 8) & 0xFF);
        }
    }

    RtlCopyMemory(Adapter->CurrentMacAddress, Adapter->PermanentMacAddress, 6);

    DbgPrint("RTW: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
             Adapter->PermanentMacAddress[0],
             Adapter->PermanentMacAddress[1],
             Adapter->PermanentMacAddress[2],
             Adapter->PermanentMacAddress[3],
             Adapter->PermanentMacAddress[4],
             Adapter->PermanentMacAddress[5]);

    return STATUS_SUCCESS;
}

/* ---- USB init/deinit -------------------------------------------------- */

NTSTATUS NTAPI RtwUsbInit(PRTW_ADAPTER Adapter)
{
    NTSTATUS Status;
    USBENUM_DEVICE_CONTEXT EnumCtx;

    DbgPrint("RTW: USB init\n");

    /* Try real USB enumeration first */
    RtlZeroMemory(&EnumCtx, sizeof(EnumCtx));
    if (UsbEnumerateDevices(&EnumCtx, 1) > 0)
    {
        /* Found a device via real enumeration */
        Adapter->UsbDevice = ExAllocatePoolWithTag(NonPagedPool,
                                                   sizeof(USB_DEVICE_HANDLE),
                                                   TAG_RTW);
        if (!Adapter->UsbDevice)
            return STATUS_NO_MEMORY;

        RtlZeroMemory(Adapter->UsbDevice, sizeof(USB_DEVICE_HANDLE));
        Adapter->UsbDevice->VendorId = EnumCtx.VendorId;
        Adapter->UsbDevice->ProductId = EnumCtx.ProductId;
        Adapter->UsbDevice->DeviceAddress = EnumCtx.DeviceAddress;
        Adapter->UsbDevice->BulkInPipe = EnumCtx.BulkInPipe;
        Adapter->UsbDevice->BulkOutPipe = EnumCtx.BulkOutPipe;
        Adapter->UsbDevice->InterruptPipe = EnumCtx.InterruptPipe;
        Adapter->UsbDevice->MaxBulkInSize = RTW_USB_MAX_BULK_SIZE;
        Adapter->UsbDevice->MaxBulkOutSize = RTW_USB_MAX_BULK_SIZE;

        DbgPrint("RTW: enumerated device via USB stack\n");
    }
    else
    {
        /* Fallback: create a fake USB device handle for testing.
           This allows testing without real hardware. */
        DbgPrint("RTW: no device found via enumeration, using fallback handle\n");
        Adapter->UsbDevice = ExAllocatePoolWithTag(NonPagedPool,
                                                   sizeof(USB_DEVICE_HANDLE),
                                                   TAG_RTW);
        if (!Adapter->UsbDevice)
            return STATUS_NO_MEMORY;

        RtlZeroMemory(Adapter->UsbDevice, sizeof(USB_DEVICE_HANDLE));
        Adapter->UsbDevice->VendorId = RTW_USB_VENDOR_ID_REALTEK;
        Adapter->UsbDevice->ProductId = RTW_USB_PRODUCT_8821CU;
        Adapter->UsbDevice->DeviceAddress = 1; /* Default address */
        Adapter->UsbDevice->BulkInPipe = (USBD_PIPE_HANDLE)1;
        Adapter->UsbDevice->BulkOutPipe = (USBD_PIPE_HANDLE)2;
        Adapter->UsbDevice->InterruptPipe = (USBD_PIPE_HANDLE)3;
        Adapter->UsbDevice->MaxBulkInSize = RTW_USB_MAX_BULK_SIZE;
        Adapter->UsbDevice->MaxBulkOutSize = RTW_USB_MAX_BULK_SIZE;
    }

    /* Initialize the chip via USB control transfers */
    Status = Rtw8821cuInit(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("RTW: chip init failed: 0x%08lx\n", (unsigned long)Status);
        ExFreePoolWithTag(Adapter->UsbDevice, TAG_RTW);
        Adapter->UsbDevice = NULL;
        return Status;
    }

    Adapter->Initialized = TRUE;
    RtwStartRxThread(Adapter);
    DbgPrint("RTW: ready\n");
    return STATUS_SUCCESS;
}

VOID NTAPI RtwUsbDeinit(PRTW_ADAPTER Adapter)
{
    RtwUsbStopInterruptPipe(Adapter);

    if (Adapter->UsbDevice)
    {
        ExFreePoolWithTag(Adapter->UsbDevice, TAG_RTW);
        Adapter->UsbDevice = NULL;
    }

    Adapter->Initialized = FALSE;
    DbgPrint("RTW: deinit\n");
}

/* ---- Top-level init --------------------------------------------------- */

RTW_ADAPTER RtwAdapter;

/* ---- RX delivery callback (set by lwip_port.c) ------------------------ */

static PVOID RtwRxCallback = NULL;

VOID NTAPI RtwRegisterRxCallback(PVOID Callback)
{
    RtwRxCallback = Callback;
    DbgPrint("RTW: RX callback registered %p\n", Callback);
}

/* ---- RX polling thread ------------------------------------------------- */

static volatile BOOLEAN RtwRxRunning = FALSE;
static PKTHREAD RtwRxThreadHandle = NULL;

static UCHAR RxBuffer[RTW_USB_MAX_BULK_SIZE];

static VOID NTAPI RtwRxThread(PVOID Context)
{
    PRTW_ADAPTER Adapter = Context;
    ULONG BytesRead;

    DbgPrint("RTW: RX thread started\n");

    while (RtwRxRunning)
    {
        NTSTATUS Status = RtwUsbBulkIn(Adapter, RxBuffer,
                                        RTW_USB_MAX_BULK_SIZE, &BytesRead);
        if (NT_SUCCESS(Status) && BytesRead > 14)
        {
            if (RtwRxCallback)
            {
                ((VOID (*)(PVOID, ULONG))RtwRxCallback)(RxBuffer, BytesRead);
            }
        }

        KeStallExecutionProcessor(1000);
    }

    DbgPrint("RTW: RX thread exiting\n");
}

NTSTATUS NTAPI RtwStartRxThread(PRTW_ADAPTER Adapter)
{
    NTSTATUS Status;
    RtwRxRunning = TRUE;
    Status = PsCreateSystemThread(PsInitialSystemProcess,
                                   RtwRxThread, Adapter,
                                   &RtwRxThreadHandle);
    if (!NT_SUCCESS(Status))
    {
        RtwRxRunning = FALSE;
        DbgPrint("RTW: failed to start RX thread: 0x%08lx\n", (unsigned long)Status);
        return Status;
    }
    DbgPrint("RTW: RX thread created\n");
    return STATUS_SUCCESS;
}

extern NTSTATUS NTAPI RtwNdisInit(PRTW_NDIS_ADAPTER UsbAdapter);
extern NTSTATUS NTAPI RtwTcpipInit(VOID);

NTSTATUS NTAPI RtwInitSystem(VOID)
{
    NTSTATUS Status;
    ULONG FwSize = (ULONG)(RtwFwBlobEnd - RtwFwBlobStart);

    DbgPrint("RTW: probing for 8821CU... (embedded firmware: %lu bytes)\n",
             (unsigned long)FwSize);

    RtlZeroMemory(&RtwAdapter, sizeof(RtwAdapter));

    Status = RtwUsbInit(&RtwAdapter);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("RTW: no 8821CU found (non-fatal)\n");
        return Status;
    }

    DbgPrint("RTW: 8821CU USB ready\n");

    Status = RtwNdisInit(&RtwAdapter);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("RTW: NDIS init failed (non-fatal)\n");
        return Status;
    }
    DbgPrint("RTW: NDIS miniport ready\n");

    Status = RtwTcpipInit();
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("RTW: TCP/IP init failed (non-fatal)\n");
        return Status;
    }
    DbgPrint("RTW: lwIP netif registered\n");

    return STATUS_SUCCESS;
}
