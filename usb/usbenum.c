/*
 * MinNT - usb/usbenum.c
 * USB device enumeration — detects RTL8821CU and other devices.
 * Implements USB spec Chapter 9 enumeration:
 *   1. Port reset + detect
 *   2. SET_ADDRESS
 *   3. GET_DESCRIPTOR (device)
 *   4. GET_DESCRIPTOR (configuration)
 *   5. SET_CONFIGURATION
 *
 * REQUIRES: UsbSubmitUrb with real device address routing (usbclass.c)
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/hal.h>
#include <nt/usb.h>
#include <nt/usbhcd.h>
#include <nt/xhci.h>
#include <nt/rtl.h>

#define TAG_USBENUM 0x554e4245  /* 'USBENUM' */

/* ---- UHCI context (from usb/uhci.c) --------------------------------- */

extern UHCI_CONTEXT UhciContext;

/* ---- USB descriptor types -------------------------------------------- */

#define USB_DESC_TYPE_DEVICE          0x01
#define USB_DESC_TYPE_CONFIGURATION   0x02
#define USB_DESC_TYPE_STRING          0x03
#define USB_DESC_TYPE_INTERFACE       0x04
#define USB_DESC_TYPE_ENDPOINT        0x05

/* ---- USB standard request codes ------------------------------------ */

#define USB_REQUEST_GET_DESCRIPTOR    0x06
#define USB_REQUEST_SET_ADDRESS       0x05
#define USB_REQUEST_SET_CONFIGURATION 0x09

#define USB_CLASS_MASS_STORAGE       0x08

/* ---- USB descriptor structures --------------------------------------- */

typedef struct _USB_DEVICE_DESCRIPTOR {
    UCHAR   bLength;
    UCHAR   bDescriptorType;
    USHORT  bcdUSB;
    UCHAR   bDeviceClass;
    UCHAR   bDeviceSubClass;
    UCHAR   bDeviceProtocol;
    UCHAR   bMaxPacketSize0;
    USHORT  idVendor;
    USHORT  idProduct;
    USHORT  bcdDevice;
    UCHAR   iManufacturer;
    UCHAR   iProduct;
    UCHAR   iSerialNumber;
    UCHAR   bNumConfigurations;
} USB_DEVICE_DESCRIPTOR, *PUSB_DEVICE_DESCRIPTOR;

typedef struct _USB_CONFIGURATION_DESCRIPTOR {
    UCHAR   bLength;
    UCHAR   bDescriptorType;
    USHORT  wTotalLength;
    UCHAR   bNumInterfaces;
    UCHAR   bConfigurationValue;
    UCHAR   iConfiguration;
    UCHAR   bmAttributes;
    UCHAR   bMaxPower;
} USB_CONFIGURATION_DESCRIPTOR, *PUSB_CONFIGURATION_DESCRIPTOR;

typedef struct _USB_INTERFACE_DESCRIPTOR {
    UCHAR   bLength;
    UCHAR   bDescriptorType;
    UCHAR   bInterfaceNumber;
    UCHAR   bAlternateSetting;
    UCHAR   bNumEndpoints;
    UCHAR   bInterfaceClass;
    UCHAR   bInterfaceSubClass;
    UCHAR   bInterfaceProtocol;
    UCHAR   iInterface;
} USB_INTERFACE_DESCRIPTOR, *PUSB_INTERFACE_DESCRIPTOR;

typedef struct _USB_ENDPOINT_DESCRIPTOR {
    UCHAR   bLength;
    UCHAR   bDescriptorType;
    UCHAR   bEndpointAddress;
    UCHAR   bmAttributes;
    USHORT  wMaxPacketSize;
    UCHAR   bInterval;
} USB_ENDPOINT_DESCRIPTOR, *PUSB_ENDPOINT_DESCRIPTOR;

/* ---- Known device list ---------------------------------------------- */

typedef struct _KNOWN_USB_DEVICE {
    USHORT  VendorId;
    USHORT  ProductId;
    PCWSTR  Name;
} KNOWN_USB_DEVICE, *PKNOWN_USB_DEVICE;

static const KNOWN_USB_DEVICE KnownDevices[] = {
    { 0x0BDA, 0xC820, L"Realtek RTL8821CU WiFi" },
    { 0x0BDA, 0x8822, L"Realtek RTL8822BU WiFi" },
    { 0x0BDA, 0x8812, L"Realtek RTL8812AU WiFi" },
};

/* ---- Internal helpers ----------------------------------------------- */

/*
 * UsbReadPortStatus - Read port status register.
 */
static USHORT NTAPI UsbReadPortStatus(ULONG PortIndex)
{
    if (UhciContext.IsIoSpace)
        return READ_PORT_USHORT((USHORT)(UhciContext.IoBase + 0x10 + PortIndex * 2));
    else
        return READ_REGISTER_USHORT(&UhciContext.Regs->PortControl[PortIndex]);
}

/*
 * UsbWritePortStatus - Write port control register.
 */
static VOID NTAPI UsbWritePortStatus(ULONG PortIndex, USHORT Value)
{
    if (UhciContext.IsIoSpace)
        WRITE_PORT_USHORT((USHORT)(UhciContext.IoBase + 0x10 + PortIndex * 2), Value);
    else
        WRITE_REGISTER_USHORT(&UhciContext.Regs->PortControl[PortIndex], Value);
}

/*
 * UsbResetPort - Perform USB port reset (SE0 for 10ms).
 *
 * According to USB spec: se0 for >= 10ms, then release and wait 10ms.
 * Sets PORT_RESET bit, waits, clears it, waits for device to respond.
 */
static NTSTATUS NTAPI UsbResetPort(ULONG PortIndex)
{
    USHORT PortStatus;
    ULONG WaitCount;

    DbgPrint("USBENUM: resetting port %lu\n", (unsigned long)PortIndex);

    /* Set port reset */
    PortStatus = UsbReadPortStatus(PortIndex);
    UsbWritePortStatus(PortIndex, PortStatus | UHCI_PORT_RESET);

    /* Wait 10ms for reset to complete */
    KeStallExecutionProcessor(10000);

    /* Clear port reset */
    UsbWritePortStatus(PortIndex, PortStatus & ~UHCI_PORT_RESET);

    /* Wait for device to come out of reset (bHeath bit or port enable) */
    for (WaitCount = 0; WaitCount < 100; WaitCount++)
    {
        KeStallExecutionProcessor(1000);
        PortStatus = UsbReadPortStatus(PortIndex);

        if (PortStatus & UHCI_PORT_ENABLE)
        {
            DbgPrint("USBENUM: port %lu reset complete, status=0x%04x\n",
                     (unsigned long)PortIndex, (unsigned)PortStatus);
            return STATUS_SUCCESS;
        }
    }

    DbgPrint("USBENUM: port %lu reset timed out, status=0x%04x\n",
             (unsigned long)PortIndex, (unsigned)PortStatus);
    return STATUS_UNSUCCESSFUL;
}

/*
 * UsbControlRequest - Send a USB control request.
 *
 * Builds a URB_CONTROL_VENDOR_OR_CLASS_REQUEST and submits via UsbSubmitUrb.
 * Assumes device is at the given address (0 for default).
 *
 * @DeviceAddress: USB address (0 for default address before SET_ADDRESS)
 * @RequestType: bmRequestType (direction, type, recipient)
 * @Request: bRequest
 * @Value: wValue
 * @Index: wIndex
 * @Data: transfer buffer (or NULL for no data stage)
 * @Length: bytes to transfer
 * @IsRead: TRUE for IN transfer, FALSE for OUT
 * @return: NTSTATUS from UsbSubmitUrb
 */
static NTSTATUS NTAPI UsbControlRequest(UCHAR DeviceAddress,
                                       UCHAR RequestType, UCHAR Request,
                                       USHORT Value, USHORT Index,
                                       PVOID Data, USHORT Length,
                                       BOOLEAN IsRead)
{
    URB_CONTROL_VENDOR_OR_CLASS_REQUEST Urb;
    USB_DEVICE_HANDLE FakeHandle;
    NTSTATUS Status;

    RtlZeroMemory(&FakeHandle, sizeof(FakeHandle));
    FakeHandle.DeviceAddress = DeviceAddress;
    FakeHandle.BulkInPipe = (USBD_PIPE_HANDLE)1;
    FakeHandle.BulkOutPipe = (USBD_PIPE_HANDLE)2;
    FakeHandle.InterruptPipe = (USBD_PIPE_HANDLE)3;

    RtlZeroMemory(&Urb, sizeof(Urb));
    Urb.Header.Length = sizeof(URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
    Urb.Header.Function = URB_FUNCTION_VENDOR_DEVICE;
    Urb.RequestTypeReservedBits = RequestType;
    Urb.Request = Request;
    Urb.Value = Value;
    Urb.Index = Index;
    Urb.TransferBufferLength = Length;
    Urb.TransferBuffer = Data;
    Urb.TransferFlags = IsRead ? USBD_TRANSFER_DIRECTION_IN : 0;

    Status = UsbSubmitUrb(&FakeHandle, (PURB)&Urb);
    return Status;
}

/*
 * UsbSetAddress - Assign a USB address to a device.
 *
 * Issues SET_ADDRESS with the device at default address 0.
 * After this, device uses the new address for all subsequent requests.
 *
 * @DeviceAddress: New address to assign (1-127)
 * @return: STATUS_SUCCESS if device acknowledged
 */
static NTSTATUS NTAPI UsbSetAddress(UCHAR DeviceAddress)
{
    NTSTATUS Status;

    DbgPrint("USBENUM: SET_ADDRESS %u\n", (unsigned)DeviceAddress);

    /* SET_ADDRESS is a control write with no data stage */
    Status = UsbControlRequest(0, 0x00, USB_REQUEST_SET_ADDRESS,
                               DeviceAddress, 0, NULL, 0, FALSE);

    if (!NT_SUCCESS(Status))
    {
        DbgPrint("USBENUM: SET_ADDRESS %u failed: 0x%08lx\n",
                 (unsigned)DeviceAddress, (unsigned long)Status);
        return Status;
    }

    /* USB spec requires 2ms recovery after SET_ADDRESS */
    KeStallExecutionProcessor(2000);

    return STATUS_SUCCESS;
}

/*
 * UsbGetDeviceDescriptor - Read device descriptor (18 bytes).
 *
 * Issues GET_DESCRIPTOR (device) to the given address.
 *
 * @DeviceAddress: USB device address
 * @Buffer: Output buffer (must be >= 18 bytes)
 * @return: STATUS_SUCCESS if descriptor read
 */
static NTSTATUS NTAPI UsbGetDeviceDescriptor(UCHAR DeviceAddress,
                                              PVOID Buffer)
{
    NTSTATUS Status;

    Status = UsbControlRequest(DeviceAddress,
                               0x80, /* IN, standard, device */
                               USB_REQUEST_GET_DESCRIPTOR,
                               (USB_DESC_TYPE_DEVICE << 8), 0, /* descriptor index 0 */
                               Buffer, 18, TRUE);

    if (!NT_SUCCESS(Status))
    {
        DbgPrint("USBENUM: GET_DESCRIPTOR (device) failed: 0x%08lx\n",
                 (unsigned long)Status);
        return Status;
    }

    return STATUS_SUCCESS;
}

/*
 * UsbGetConfigurationDescriptor - Read configuration descriptor.
 *
 * First reads the 9-byte partial descriptor to get wTotalLength,
 * then reads the full configuration data.
 *
 * @DeviceAddress: USB device address
 * @Buffer: Output buffer (must be >= 512 bytes)
 * @pTotalLength: Output for actual bytes read
 * @return: STATUS_SUCCESS if descriptor read
 */
static NTSTATUS NTAPI UsbGetConfigurationDescriptor(UCHAR DeviceAddress,
                                                    PVOID Buffer,
                                                    PULONG pTotalLength)
{
    USB_CONFIGURATION_DESCRIPTOR PartialDesc;
    NTSTATUS Status;
    USHORT TotalLength;

    /* First: read just the config descriptor header (9 bytes) */
    Status = UsbControlRequest(DeviceAddress,
                               0x80, /* IN, standard, device */
                               USB_REQUEST_GET_DESCRIPTOR,
                               (USB_DESC_TYPE_CONFIGURATION << 8), 0,
                               &PartialDesc, 9, TRUE);

    if (!NT_SUCCESS(Status))
    {
        DbgPrint("USBENUM: GET_DESCRIPTOR (config header) failed: 0x%08lx\n",
                 (unsigned long)Status);
        return Status;
    }

    TotalLength = PartialDesc.wTotalLength;
    DbgPrint("USBENUM: config descriptor wTotalLength=%u\n",
             (unsigned)TotalLength);

    if (TotalLength < 9 || TotalLength > 512)
    {
        DbgPrint("USBENUM: invalid wTotalLength %u\n", (unsigned)TotalLength);
        return STATUS_UNSUCCESSFUL;
    }

    /* Second: read the full configuration descriptor */
    Status = UsbControlRequest(DeviceAddress,
                               0x80, /* IN, standard, device */
                               USB_REQUEST_GET_DESCRIPTOR,
                               (USB_DESC_TYPE_CONFIGURATION << 8), 0,
                               Buffer, TotalLength, TRUE);

    if (!NT_SUCCESS(Status))
    {
        DbgPrint("USBENUM: GET_DESCRIPTOR (config full) failed: 0x%08lx\n",
                 (unsigned long)Status);
        return Status;
    }

    *pTotalLength = TotalLength;
    return STATUS_SUCCESS;
}

/*
 * UsbSetConfiguration - Set the active configuration.
 *
 * @DeviceAddress: USB device address
 * @ConfigurationValue: bConfigurationValue from config descriptor
 * @return: STATUS_SUCCESS if configuration set
 */
static NTSTATUS NTAPI UsbSetConfiguration(UCHAR DeviceAddress,
                                          UCHAR ConfigurationValue)
{
    NTSTATUS Status;

    DbgPrint("USBENUM: SET_CONFIGURATION %u\n", (unsigned)ConfigurationValue);

    Status = UsbControlRequest(DeviceAddress,
                               0x00, /* OUT, standard, device */
                               USB_REQUEST_SET_CONFIGURATION,
                               ConfigurationValue, 0,
                               NULL, 0, FALSE);

    if (!NT_SUCCESS(Status))
    {
        DbgPrint("USBENUM: SET_CONFIGURATION %u failed: 0x%08lx\n",
                 (unsigned)ConfigurationValue, (unsigned long)Status);
    }

    return Status;
}

/*
 * UsbParseConfiguration - Parse config descriptor for endpoints.
 *
 * Walks the config descriptor chain looking for:
 * - First interface (bInterfaceNumber = 0, bAlternateSetting = 0)
 * - Bulk IN endpoint (bEndpointAddress & 0x80 == 0x80)
 * - Bulk OUT endpoint (bEndpointAddress & 0x80 == 0x00)
 * - Interrupt endpoint (bmAttributes & 0x03 == 0x03)
 *
 * @ConfigBuffer: Full configuration descriptor buffer
 * @Length: Bytes in buffer (from wTotalLength)
 * @BulkInPipe: Output for bulk IN endpoint address
 * @BulkOutPipe: Output for bulk OUT endpoint address
 * @InterruptPipe: Output for interrupt endpoint address
 * @MaxPacketSize: Output for max packet size (from bulk IN)
 */
static VOID NTAPI UsbParseConfiguration(PVOID ConfigBuffer,
                                         ULONG Length,
                                         PUCHAR BulkInPipe,
                                         PUCHAR BulkOutPipe,
                                         PUCHAR InterruptPipe,
                                         PUSHORT MaxPacketSize)
{
    PUCHAR Walker = (PUCHAR)ConfigBuffer;
    PUCHAR End = Walker + Length;
    BOOLEAN FoundInterface = FALSE;
    UCHAR ifcnum = 0;

    *BulkInPipe = 0;
    *BulkOutPipe = 0;
    *InterruptPipe = 0;
    *MaxPacketSize = 64;

    while (Walker < End)
    {
        UCHAR bLength = Walker[0];
        UCHAR bDescriptorType = Walker[1];

        if (bLength == 0 || Walker + bLength > End)
            break;

        switch (bDescriptorType)
        {
            case USB_DESC_TYPE_INTERFACE:
            {
                USB_INTERFACE_DESCRIPTOR *Ifc = (PVOID)Walker;
                if (!FoundInterface && Ifc->bAlternateSetting == 0)
                {
                    ifcnum = Ifc->bInterfaceNumber;
                    FoundInterface = TRUE;
                    DbgPrint("USBENUM: interface %u (%u endpoints)\n",
                             (unsigned)Ifc->bInterfaceNumber,
                             (unsigned)Ifc->bNumEndpoints);
                }
                break;
            }

            case USB_DESC_TYPE_ENDPOINT:
            {
                USB_ENDPOINT_DESCRIPTOR *Ep = (PVOID)Walker;
                if (FoundInterface)
                {
                    UCHAR Attr = Ep->bmAttributes & 0x03;
                    UCHAR EpAddr = Ep->bEndpointAddress;

                    if (Attr == 0x02)  /* Bulk */
                    {
                        if ((EpAddr & 0x80) && *BulkInPipe == 0)
                        {
                            *BulkInPipe = (UCHAR)(EpAddr & 0x0F);
                            *MaxPacketSize = Ep->wMaxPacketSize;
                            DbgPrint("USBENUM: bulk IN  endpoint 0x%02x, maxpkt=%u\n",
                                     (unsigned)EpAddr, (unsigned)Ep->wMaxPacketSize);
                        }
                        else if (!(EpAddr & 0x80) && *BulkOutPipe == 0)
                        {
                            *BulkOutPipe = (UCHAR)(EpAddr & 0x0F);
                            DbgPrint("USBENUM: bulk OUT endpoint 0x%02x, maxpkt=%u\n",
                                     (unsigned)EpAddr, (unsigned)Ep->wMaxPacketSize);
                        }
                    }
                    else if (Attr == 0x03)  /* Interrupt */
                    {
                        if (*InterruptPipe == 0)
                        {
                            *InterruptPipe = (UCHAR)(EpAddr & 0x0F);
                            DbgPrint("USBENUM: interrupt endpoint 0x%02x\n",
                                     (unsigned)EpAddr);
                        }
                    }
                }
                break;
            }
        }

        Walker += bLength;
    }

    /* Fallback: if no endpoints found, use RTL8821CU defaults */
    if (*BulkInPipe == 0)
        *BulkInPipe = 1;
    if (*BulkOutPipe == 0)
        *BulkOutPipe = 2;
    if (*InterruptPipe == 0)
        *InterruptPipe = 3;
}

/* ---- Enumeration of a single port ---------------------------------- */

/*
 * UsbEnumeratePort - Enumerate a device on a UHCI port.
 *
 * Full USB enumeration sequence:
 *   1. Check port for CONNECTED
 *   2. Reset port
 *   3. SET_ADDRESS (assign address 1)
 *   4. GET_DESCRIPTOR (device) — verify VID/PID
 *   5. GET_DESCRIPTOR (configuration) — find endpoints
 *   6. SET_CONFIGURATION — activate
 *
 * @PortIndex: UHCI port index (0-based)
 * @Context: Output device context (filled on success)
 * @return: STATUS_SUCCESS if device found and enumerated
 */
static NTSTATUS NTAPI UsbEnumeratePort(ULONG PortIndex,
                                         PUSBENUM_DEVICE_CONTEXT Context)
{
    NTSTATUS Status;
    USB_DEVICE_DESCRIPTOR DevDesc;
    UCHAR ConfigBuffer[512];
    ULONG ConfigLength;
    UCHAR BulkIn, BulkOut, Interrupt;
    USHORT MaxPkt;
    UCHAR NextAddress = 1;  /* First enumerated device gets address 1 */
    USHORT PortStatus;

    if (!Context)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Context, sizeof(USBENUM_DEVICE_CONTEXT));

    /* Step 1: Check port for CONNECTED */
    PortStatus = UsbReadPortStatus(PortIndex);
    if (!(PortStatus & UHCI_PORT_CONNECT))
        return STATUS_UNSUCCESSFUL;

    DbgPrint("USBENUM: port %lu connected, status=0x%04x\n",
             (unsigned long)PortIndex, (unsigned)PortStatus);

    /* Step 2: Reset port */
    Status = UsbResetPort(PortIndex);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Step 3: SET_ADDRESS */
    Status = UsbSetAddress(NextAddress);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("USBENUM: SET_ADDRESS failed, trying address 0 anyway\n");
        /* Continue with address 0 as fallback for simple devices */
        NextAddress = 0;
    }

    Context->DeviceAddress = NextAddress;

    /* Step 4: GET_DESCRIPTOR (device) */
    Status = UsbGetDeviceDescriptor(NextAddress, &DevDesc);
    if (!NT_SUCCESS(Status))
    {
        /* Try at address 0 if SET_ADDRESS didn't work */
        Status = UsbGetDeviceDescriptor(0, &DevDesc);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("USBENUM: GET_DESCRIPTOR (device) failed at both addr 0 and %u\n",
                     (unsigned)NextAddress);
            return Status;
        }
        Context->DeviceAddress = 0;
    }

    Context->VendorId = DevDesc.idVendor;
    Context->ProductId = DevDesc.idProduct;
    Context->MaxPacketSize = DevDesc.bMaxPacketSize0;

    DbgPrint("USBENUM: device %04x:%04x (class %02x:%02x), maxpktsize=%u\n",
             (unsigned)DevDesc.idVendor, (unsigned)DevDesc.idProduct,
             (unsigned)DevDesc.bDeviceClass, (unsigned)DevDesc.bDeviceSubClass,
             (unsigned)DevDesc.bMaxPacketSize0);

    /* Step 5: GET_DESCRIPTOR (configuration) */
    Status = UsbGetConfigurationDescriptor(Context->DeviceAddress,
                                           ConfigBuffer, &ConfigLength);
    if (!NT_SUCCESS(Status))
    {
        /* Fallback: use hardcoded config for known devices */
        DbgPrint("USBENUM: config descriptor read failed, using defaults\n");
        ConfigBuffer[1] = USB_DESC_TYPE_CONFIGURATION;
        /* Skip parsing, use defaults */
    }

    /* Parse configuration for endpoints */
    UsbParseConfiguration(ConfigBuffer, ConfigLength,
                          &BulkIn, &BulkOut, &Interrupt, &MaxPkt);

    Context->BulkInPipe = (USBD_PIPE_HANDLE)BulkIn;
    Context->BulkOutPipe = (USBD_PIPE_HANDLE)BulkOut;
    Context->InterruptPipe = (USBD_PIPE_HANDLE)Interrupt;
    Context->ConfigValue = 1;  /* First configuration */
    Context->InterfaceNumber = 0;
    Context->Present = TRUE;

    /* Step 6: SET_CONFIGURATION */
    Status = UsbSetConfiguration(Context->DeviceAddress, Context->ConfigValue);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("USBENUM: SET_CONFIGURATION failed (non-fatal)\n");
        /* Continue anyway — device may still work */
    }

    DbgPrint("USBENUM: enumerated device at address %u, bulk IN=%u OUT=%u INT=%u\n",
             (unsigned)Context->DeviceAddress,
             (unsigned)BulkIn, (unsigned)BulkOut, (unsigned)Interrupt);

    return STATUS_SUCCESS;
}

/*
 * UsbFindKnownDevice - Match detected VID/PID against known device list.
 */
static USHORT NTAPI UsbFindKnownDevice(USHORT VendorId, USHORT ProductId)
{
    ULONG i;

    for (i = 0; i < sizeof(KnownDevices) / sizeof(KnownDevices[0]); i++)
    {
        if (VendorId == KnownDevices[i].VendorId &&
            ProductId == KnownDevices[i].ProductId)
        {
            return (USHORT)i;
        }
    }

    return 0xFFFF;
}

/* ---- xHCI enumeration via HCD abstraction ---------------------------- */

static NTSTATUS XhciGetDesc(UCHAR DevAddr, UCHAR Speed,
                            UCHAR DescType, USHORT Index,
                            PVOID Buf, USHORT Len, USHORT *Actual)
{
    UCHAR setup[8];
    setup[0] = 0x80;
    setup[1] = USB_REQUEST_GET_DESCRIPTOR;
    setup[2] = (UCHAR)(Index & 0xFF);
    setup[3] = DescType;
    setup[4] = 0; setup[5] = 0;
    setup[6] = (UCHAR)(Len & 0xFF);
    setup[7] = (UCHAR)(Len >> 8);
    return USB_HCD_CONTROL_TRANSFER(DevAddr, 0, Speed,
                                    setup, Buf, Len, TRUE, Actual);
}

static ULONG UsbEnumerateXhciDevices(PUSBENUM_DEVICE_CONTEXT DeviceList,
                                     ULONG MaxDevices)
{
    PXHCI_CONTEXT Xhci = (PXHCI_CONTEXT)UsbActiveHcd.Context;
    ULONG Count = 0;
    if (!Xhci) return 0;

    for (ULONG Slot = 1; Slot <= Xhci->MaxSlots && Count < MaxDevices; Slot++) {
        PXHCI_DEVICE Dev = &Xhci->Devices[Slot];
        if (!Dev->Enabled || Dev->Address == 0) continue;

        UCHAR Addr = Dev->Address;
        UCHAR Speed = Dev->Speed;
        UCHAR devDesc[18];
        USHORT actual = 0;
        NTSTATUS s = XhciGetDesc(Addr, Speed, USB_DESC_TYPE_DEVICE, 0,
                                 devDesc, 18, &actual);
        if (!NT_SUCCESS(s) || actual < 18) continue;

        USHORT vid = devDesc[8] | (devDesc[9] << 8);
        USHORT pid = devDesc[10] | (devDesc[11] << 8);

        UCHAR bulkIn = 0, bulkOut = 0, ifClass = 0;
        UCHAR cfgDesc[256];
        actual = 0;
        s = XhciGetDesc(Addr, Speed, USB_DESC_TYPE_CONFIGURATION, 0,
                        cfgDesc, sizeof(cfgDesc), &actual);
        if (NT_SUCCESS(s) && actual >= 9) {
            for (USHORT off = 0; off + 1 < actual; ) {
                UCHAR bLen = cfgDesc[off];
                UCHAR bType = cfgDesc[off + 1];
                if (bLen == 0) break;
                if (bType == 0x04 && bLen >= 9)
                    ifClass = cfgDesc[off + 5];
                else if (bType == 0x05 && bLen >= 7) {
                    UCHAR epAddr = cfgDesc[off + 2];
                    UCHAR epAttr = cfgDesc[off + 3];
                    if ((epAttr & 0x03) == 2) {
                        if (epAddr & 0x80) bulkIn = epAddr & 0x0F;
                        else               bulkOut = epAddr & 0x0F;
                    }
                }
                off += bLen;
            }
        }

        if (ifClass != USB_CLASS_MASS_STORAGE) continue;
        if (!bulkIn && !bulkOut) continue;

        PUSBENUM_DEVICE_CONTEXT ctx = &DeviceList[Count];
        RtlZeroMemory(ctx, sizeof(*ctx));
        ctx->Present = TRUE;
        ctx->DeviceAddress = Addr;
        ctx->VendorId = vid;
        ctx->ProductId = pid;
        ctx->BulkInPipe = (USBD_PIPE_HANDLE)(ULONG_PTR)bulkIn;
        ctx->BulkOutPipe = (USBD_PIPE_HANDLE)(ULONG_PTR)bulkOut;
        ctx->MaxPacketSize = 512;

        DbgPrint("USBENUM-xHCI: slot %lu addr %u class %02x bulk_in=%u bulk_out=%u\n",
                 Slot, Addr, ifClass, bulkIn, bulkOut);
        Count++;
    }
    return Count;
}

/* ---- Public API ----------------------------------------------------- */

/*
 * UsbEnumerateDevices - Enumerate all USB devices on all ports.
 *
 * Called by RtwUsbInit() to find and enumerate WiFi devices.
 * Fills in DeviceList with found devices and their handles.
 *
 * @DeviceList: Array of device contexts to fill
 * @MaxDevices: Number of slots in DeviceList
 * @return: Number of devices found
 */
ULONG NTAPI UsbEnumerateDevices(PUSBENUM_DEVICE_CONTEXT DeviceList,
                                 ULONG MaxDevices)
{
    ULONG PortIndex;
    ULONG DeviceCount = 0;
    NTSTATUS Status;
    USHORT KnownIdx;

    if (!DeviceList || MaxDevices == 0)
        return 0;

    /* xHCI path: enumerate via HCD abstraction. */
    if (UsbActiveHcd.Initialized && !UhciContext.Initialized)
    {
        return UsbEnumerateXhciDevices(DeviceList, MaxDevices);
    }

    if (!UhciContext.Initialized)
    {
        DbgPrint("USBENUM: no USB HCD initialized\n");
        return 0;
    }

    for (PortIndex = 0; PortIndex < UhciContext.NumPorts; PortIndex++)
    {
        if (DeviceCount >= MaxDevices)
            break;

        Status = UsbEnumeratePort(PortIndex, &DeviceList[DeviceCount]);
        if (NT_SUCCESS(Status))
        {
            KnownIdx = UsbFindKnownDevice(DeviceList[DeviceCount].VendorId,
                                          DeviceList[DeviceCount].ProductId);

            if (KnownIdx != 0xFFFF)
            {
                DbgPrint("USBENUM: found %ws (VID=%04x PID=%04x) on port %lu\n",
                         KnownDevices[KnownIdx].Name,
                         (unsigned)DeviceList[DeviceCount].VendorId,
                         (unsigned)DeviceList[DeviceCount].ProductId,
                         (unsigned long)PortIndex);
            }
            else
            {
                DbgPrint("USBENUM: found unknown device %04x:%04x on port %lu\n",
                         (unsigned)DeviceList[DeviceCount].VendorId,
                         (unsigned)DeviceList[DeviceCount].ProductId,
                         (unsigned long)PortIndex);
            }

            DeviceCount++;
        }
    }

    DbgPrint("USBENUM: found %lu device(s)\n", (unsigned long)DeviceCount);
    return DeviceCount;
}