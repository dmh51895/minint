/*
 * MinNT - usb.h
 * USB data structures: URB, UHCI registers, endpoint descriptors.
 * Matches the ReactOS rtw88 USB transport API for easy porting.
 */

#ifndef _USB_H_
#define _USB_H_

#include <nt/ntdef.h>
#include <nt/io.h>

/* ---- USB device identifiers (RTW 8821CU) ------------------------------- */

#define RTW_USB_VENDOR_ID_REALTEK         0x0BDA
#define RTW_USB_PRODUCT_8821CU            0xC820

/* ---- URB function codes (subset) --------------------------------------- */

#define URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER     0x0009
#define URB_FUNCTION_CONTROL_TRANSFER               0x0008
#define URB_FUNCTION_VENDOR_DEVICE                  0x0012
#define URB_FUNCTION_VENDOR_INTERFACE               0x0013
#define URB_FUNCTION_VENDOR_ENDPOINT                0x0014
#define URB_FUNCTION_ABORT_PIPE                     0x0015
#define URB_FUNCTION_SELECT_CONFIGURATION            0x0000

/* ---- URB flags --------------------------------------------------------- */

#define USBD_TRANSFER_DIRECTION_IN      0x0001
#define USBD_SHORT_TRANSFER_OK          0x0002

/* ---- URB status codes -------------------------------------------------- */

#define USBD_STATUS_SUCCESS             0x00000000
#define USBD_STATUS_PENDING             0x40000000
#define USBD_STATUS_CANCELED            0xC0010000
#define USBD_STATUS_ERROR               0x80000000

/* ---- Pipe handle ------------------------------------------------------- */

typedef PVOID USBD_PIPE_HANDLE;

/* ---- URB header -------------------------------------------------------- */

typedef struct _URB_HEADER {
    USHORT      Length;
    USHORT      Function;
    NTSTATUS    Status;
    PVOID       UsbdDeviceHandle;
    ULONG       UsbdFlags;
} URB_HEADER, *PURB_HEADER;

/* ---- Bulk or interrupt transfer URB ------------------------------------ */

typedef struct _URB_BULK_OR_INTERRUPT_TRANSFER {
    URB_HEADER   Header;
    USBD_PIPE_HANDLE PipeHandle;
    ULONG        TransferFlags;
    ULONG        TransferBufferLength;
    PVOID        TransferBuffer;
    PVOID        TransferBufferMDL;
    struct _URB *UrbLink;
    ULONG        Reserved0;
} URB_BULK_OR_INTERRUPT_TRANSFER, *PURB_BULK_OR_INTERRUPT_TRANSFER;

/* ---- Control transfer URB ---------------------------------------------- */

typedef struct _URB_CONTROL_TRANSFER {
    URB_HEADER   Header;
    USBD_PIPE_HANDLE PipeHandle;
    ULONG        TransferFlags;
    ULONG        TransferBufferLength;
    PVOID        TransferBuffer;
    PVOID        TransferBufferMDL;
    struct _URB *UrbLink;
    UCHAR        SetupPacket[8];
    ULONG        Reserved0;
} URB_CONTROL_TRANSFER, *PURB_CONTROL_TRANSFER;

/* ---- Vendor/class request URB ------------------------------------------ */

typedef struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST {
    URB_HEADER   Header;
    USBD_PIPE_HANDLE PipeHandle;
    ULONG        TransferFlags;
    ULONG        TransferBufferLength;
    PVOID        TransferBuffer;
    PVOID        TransferBufferMDL;
    struct _URB *UrbLink;
    UCHAR        RequestTypeReservedBits;
    UCHAR        Request;
    USHORT       Value;
    USHORT       Index;
    USHORT       Reserved1;
} URB_CONTROL_VENDOR_OR_CLASS_REQUEST, *PURB_CONTROL_VENDOR_OR_CLASS_REQUEST;

/* ---- Pipe request URB (abort, reset) ----------------------------------- */

typedef struct _URB_PIPE_REQUEST {
    URB_HEADER   Header;
    USBD_PIPE_HANDLE PipeHandle;
    ULONG        Reserved0[7];
} URB_PIPE_REQUEST, *PURB_PIPE_REQUEST;

/* ---- Select configuration URB ------------------------------------------ */

typedef struct _URB_SELECT_CONFIGURATION {
    URB_HEADER   Header;
    PVOID        ConfigurationDescriptor;
    USBD_PIPE_HANDLE PipeHandle;
    ULONG        Reserved0[4];
} URB_SELECT_CONFIGURATION, *PURB_SELECT_CONFIGURATION;

/* ---- Unified URB ------------------------------------------------------- */

typedef struct _URB {
    union {
        URB_HEADER                              Header;
        URB_BULK_OR_INTERRUPT_TRANSFER          BulkOrInterruptTransfer;
        URB_CONTROL_TRANSFER                    ControlTransfer;
        URB_CONTROL_VENDOR_OR_CLASS_REQUEST     ControlVendorClassRequest;
        URB_PIPE_REQUEST                        PipeRequest;
        URB_SELECT_CONFIGURATION                SelectConfiguration;
    };
} URB, *PURB;

/* ---- IOCTL codes for USB internal IOCTLs ------------------------------- */

#define IOCTL_INTERNAL_USB_SUBMIT_URB \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0000, METHOD_NEITHER, FILE_ANY_ACCESS)

#define IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0001, METHOD_NEITHER, FILE_ANY_ACCESS)

/* ---- UHCI register layout ---------------------------------------------- */

typedef volatile struct _UHCI_REGISTERS {
    USHORT     HcCommand;           /* 0x00 - USB Command */
    USHORT     HcStatus;            /* 0x02 - USB Status */
    USHORT     HcInterruptEnable;   /* 0x04 - Interrupt Enable */
    USHORT     FrameNumber;         /* 0x06 - Frame Number */
    ULONG      FrameListBaseAddress;/* 0x08 - Frame List Base Address */
    UCHAR      SOF_Modify;          /* 0x0C - Start of Frame Modify */
    UCHAR      Reserved0[3];
    USHORT     PortControl[2];      /* 0x10, 0x12 - Port Status/Control */
} UHCI_REGISTERS, *PUHCI_REGISTERS;

/* ---- UHCI command register bits ---------------------------------------- */

#define UHCI_CMD_RUN                0x0001
#define UHCI_CMD_HCRESET            0x0002
#define UHCI_CMD_GRESET             0x0004
#define UHCI_CMD_GSTOP              0x0008
#define UHCI_CMD_MAXP               0x0040

/* ---- UHCI status register bits ----------------------------------------- */

#define UHCI_STS_USBINT             0x0001
#define UHCI_STS_ERROR              0x0002
#define UHCI_STS_RD                 0x0004
#define UHCI_STS_HSE                0x0008
#define UHCI_STS_HALTED             0x0020

/* ---- USB speed constants (shared across all HCDs) ---------------------- */

#define USB_SPEED_UNKNOWN           0
#define USB_SPEED_LOW               1   /* 1.5 Mbps */
#define USB_SPEED_FULL              2   /* 12 Mbps */
#define USB_SPEED_HIGH              3   /* 480 Mbps */
#define USB_SPEED_SUPER             4   /* 5 Gbps */
#define USB_SPEED_SUPER_PLUS        5   /* 10 Gbps */

/* ---- PCI class codes for USB controllers ----------------------------- */

#define PCI_CLASS_SERIAL_BUS        0x0C
#define PCI_SUBCLASS_USB            0x03

/* USB controller programming interfaces */
#define USB_PROG_IF_UHCI            0x00    /* Universal HCI */
#define USB_PROG_IF_OHCI            0x10    /* Open HCI */
#define USB_PROG_IF_EHCI            0x20    /* Enhanced HCI */
#define USB_PROG_IF_XHCI            0x30    /* eXtensible HCI */

/* ---- UHCI port status bits --------------------------------------------- */

#define UHCI_PORT_CONNECT           0x0001
#define UHCI_PORT_ENABLE            0x0002
#define UHCI_PORT_RESET             0x0200
#define UHCI_PORT_LOWSPEED          0x0100
#define UHCI_PORT_CSC               0x0004  /* Connect Status Change */

/* ---- UHCI Transfer Descriptor (TD) ------------------------------------- */

typedef struct _UHCI_TD {
    ULONG   NextTD;          /* Link Pointer */
    ULONG   ControlStatus;   /* Control and Status */
    ULONG   Token;           /* Token */
    ULONG   Buffer;          /* Data Buffer */
} UHCI_TD, *PUHCI_TD;

/* UHCI TD Control/Status bits */
#define UHCI_TD_STS_ACTIVE          0x80
#define UHCI_TD_STS_ERROR           0x40
#define UHCI_TD_STS_BABBLE          0x20
#define UHCI_TD_STS_NAK             0x10
#define UHCI_TD_STS_TIMEOUT         0x08
#define UHCI_TD_STS_STALLED         0x04
#define UHCI_TD_IOC                 0x01  /* Interrupt on Complete */
#define UHCI_TD_LS                 0x02  /* Low Speed */
#define UHCI_TD_ERR_COUNT_SHIFT    27
#define UHCI_TD_ERR_COUNT_MASK     (3 << 27)
#define UHCI_TD_ACTLEN_SHIFT       0
#define UHCI_TD_ACTLEN_MASK        0x7FF

/* UHCI TD Token bits */
#define UHCI_TD_PID_SETUP          0x00
#define UHCI_TD_PID_IN             0x01
#define UHCI_TD_PID_OUT            0x02
#define UHCI_TD_PID_SHIFT          0
#define UHCI_TD_PID_MASK           (7 << 0)
#define UHCI_TD_DEVADDR_SHIFT      8
#define UHCI_TD_DEVADDR_MASK       (127 << 8)
#define UHCI_TD_ENDP_SHIFT         15
#define UHCI_TD_ENDP_MASK          (15 << 15)
#define UHCI_TD_TOGGLE_SHIFT       19
#define UHCI_TD_MAXLEN_SHIFT       21
#define UHCI_TD_MAXLEN_MASK        (0x7FF << 21)

/* UHCI TD Link Pointer bits */
#define UHCI_TD_LINK_TERMINATE     0x00000001
#define UHCI_TD_LINK_QH            0x00000002
#define UHCI_TD_LINK_DEPTH         0x00000004

/* ---- UHCI Queue Head (QH) ---------------------------------------------- */

typedef struct _UHCI_QH {
    ULONG   NextQH;          /* Horizontal Link */
    ULONG   Element;         /* Vertical Link (first TD) */
} UHCI_QH, *PUHCI_QH;

/* UHCI QH Link Pointer bits */
#define UHCI_QH_LINK_TERMINATE     0x00000001
#define UHCI_QH_LINK_QH            0x00000002

/* ---- UHCI context (shared with uhci.c, usbclass.c) -------------------- */

typedef struct _UHCI_CONTEXT {
    union {
        PUHCI_REGISTERS Regs;
        ULONG           IoBase;
    };
    BOOLEAN             IsIoSpace;
    ULONG               IrqNumber;
    volatile ULONG      FrameList[1024];
    UCHAR               NumPorts;
    BOOLEAN             Initialized;
} UHCI_CONTEXT, *PUHCI_CONTEXT;

/* ---- USB device handle (simplified) ------------------------------------ */

typedef struct _USB_DEVICE_HANDLE {
    USHORT   VendorId;
    USHORT   ProductId;
    UCHAR    DeviceAddress;
    UCHAR    ConfigValue;
    USBD_PIPE_HANDLE BulkInPipe;
    USBD_PIPE_HANDLE BulkOutPipe;
    USBD_PIPE_HANDLE InterruptPipe;
    ULONG    MaxBulkInSize;
    ULONG    MaxBulkOutSize;
} USB_DEVICE_HANDLE, *PUSB_DEVICE_HANDLE;

/* ---- USB class driver API ---------------------------------------------- */

NTSTATUS NTAPI UsbInitSystem(VOID);
NTSTATUS NTAPI UsbInitUhciController(ULONG Bus, ULONG Device, ULONG Function);

/* ---- URB submission (called by drivers like rtw88) --------------------- */

NTSTATUS NTAPI UsbSubmitUrb(PUSB_DEVICE_HANDLE DeviceHandle, PURB Urb);

/* ---- USB device enumeration ------------------------------------------ */

typedef struct _USBENUM_DEVICE_CONTEXT {
    BOOLEAN             Present;
    USHORT              VendorId;
    USHORT              ProductId;
    UCHAR               DeviceAddress;
    UCHAR               ConfigValue;
    UCHAR               InterfaceNumber;
    USBD_PIPE_HANDLE    BulkInPipe;
    USBD_PIPE_HANDLE    BulkOutPipe;
    USBD_PIPE_HANDLE    InterruptPipe;
    ULONG               MaxPacketSize;
} USBENUM_DEVICE_CONTEXT, *PUSBENUM_DEVICE_CONTEXT;

ULONG NTAPI UsbEnumerateDevices(PUSBENUM_DEVICE_CONTEXT DeviceList, ULONG MaxDevices);

/* ---- USB Mass Storage Class (BOT + SCSI) ------------------------------ */

NTSTATUS NTAPI UsbMassInit(VOID);
ULONG    NTAPI UsbMassGetDiskCount(VOID);
ULONG64  NTAPI UsbMassGetDiskSize(ULONG DiskNumber);
VOID     NTAPI UsbMassGetDiskModel(ULONG DiskNumber, PCHAR Buffer, ULONG MaxLen);
NTSTATUS NTAPI UsbMassReadSectors(ULONG DiskNumber, ULONG64 Lba, ULONG Count, PVOID Buffer);
NTSTATUS NTAPI UsbMassWriteSectors(ULONG DiskNumber, ULONG64 Lba, ULONG Count, PVOID Buffer);

#endif /* _USB_H_ */
