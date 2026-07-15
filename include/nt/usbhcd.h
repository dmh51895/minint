/*
 * MinNT - include/nt/usbhcd.h
 * USB Host Controller Driver (HCD) abstraction layer
 * 
 * This provides a vtable interface that abstracts UHCI, EHCI, and xHCI controllers.
 * All USB stack code uses these operations instead of directly accessing controller
 * registers.
 */

#pragma once

#ifndef _USBHCD_H
#define _USBHCD_H

#include <nt/ntdef.h>

/* HCD kinds */
#define USB_HCD_KIND_INVALID  0
#define USB_HCD_KIND_UHCI     1
#define USB_HCD_KIND_EHCI     2
#define USB_HCD_KIND_XHCI     3

/* Forward declarations */
typedef struct _USB_HCD_OPS USB_HCD_OPS, *PUSB_HCD_OPS;
typedef struct _USB_HCD USB_HCD, *PUSB_HCD;

/*
 * USB_HCD_OPS - Host Controller Driver operations vtable
 * 
 * Each controller type (UHCI, EHCI, xHCI) implements these operations.
 * The USB enumeration and class drivers call through this interface.
 */
typedef struct _USB_HCD_OPS {
    /*
     * Port operations
     */
    USHORT   (NTAPI *ReadPortStatus)(PVOID HcContext, ULONG Port);
    VOID     (NTAPI *WritePortStatus)(PVOID HcContext, ULONG Port, USHORT Value);
    NTSTATUS (NTAPI *ResetPort)(PVOID HcContext, ULONG Port);
    ULONG    (NTAPI *GetPortCount)(PVOID HcContext);
    
    /*
     * Transfer operations
     */
    NTSTATUS (NTAPI *ControlTransfer)(
        PVOID HcContext,
        UCHAR DeviceAddress,
        UCHAR Endpoint,
        UCHAR Speed,
        PVOID SetupPacket,      /* 8-byte USB setup packet */
        PVOID DataBuffer,
        USHORT DataLength,
        BOOLEAN IsInTransfer,
        PULONG ActualTransferred
    );
    
    NTSTATUS (NTAPI *BulkTransfer)(
        PVOID HcContext,
        UCHAR DeviceAddress,
        UCHAR Endpoint,
        UCHAR Speed,
        PVOID DataBuffer,
        ULONG DataLength,
        BOOLEAN IsInTransfer,
        PULONG ActualTransferred
    );
    
    /*
     * Device addressing (xHCI-specific, NULL for UHCI/EHCI)
     * 
     * xHCI handles SET_ADDRESS in hardware via Enable Slot and Address
     * Device commands. UHCI/EHCI use ControlTransfer with SET_ADDRESS request.
     */
    NTSTATUS (NTAPI *AddressDevice)(
        PVOID HcContext,
        ULONG PortNumber,
        UCHAR Speed,
        PUCHAR OutDeviceAddress
    );
    
    /*
     * Controller management
     */
    NTSTATUS (NTAPI *Initialize)(PVOID HcContext);
    VOID     (NTAPI *Shutdown)(PVOID HcContext);
    
} USB_HCD_OPS, *PUSB_HCD_OPS;

/*
 * USB_HCD - Active host controller instance
 * 
 * The USB stack maintains one active HCD. During initialization,
 * the highest-capability controller is selected (xHCI > EHCI > UHCI).
 */
typedef struct _USB_HCD {
    const USB_HCD_OPS *Ops;      /* Operation vtable */
    PVOID              Context;  /* Controller-specific context (UHCI_CONTEXT, XHCI_CONTEXT, etc.) */
    UCHAR              Kind;     /* USB_HCD_KIND_* */
    BOOLEAN            Initialized;
    ULONG              PortCount;
} USB_HCD, *PUSB_HCD;

/* Global active HCD - USB stack uses this */
extern USB_HCD UsbActiveHcd;

/* HCD registration functions (called by controller drivers) */
NTSTATUS NTAPI UsbHcdRegisterUhci(PVOID PciDevice);
NTSTATUS NTAPI UsbHcdRegisterEhci(PVOID PciDevice);
NTSTATUS NTAPI UsbHcdRegisterXhci(PVOID PciDevice);

/* HCD utility functions */
NTSTATUS NTAPI UsbHcdInitialize(VOID);
VOID     NTAPI UsbHcdShutdown(VOID);

/* Port helper macros that call through vtable */
#define USB_HCD_READ_PORT_STATUS(port) \
    (UsbActiveHcd.Ops ? UsbActiveHcd.Ops->ReadPortStatus(UsbActiveHcd.Context, (port)) : 0)

#define USB_HCD_WRITE_PORT_STATUS(port, val) \
    do { if (UsbActiveHcd.Ops && UsbActiveHcd.Ops->WritePortStatus) \
         UsbActiveHcd.Ops->WritePortStatus(UsbActiveHcd.Context, (port), (val)); } while(0)

#define USB_HCD_RESET_PORT(port) \
    (UsbActiveHcd.Ops ? UsbActiveHcd.Ops->ResetPort(UsbActiveHcd.Context, (port)) : STATUS_NOT_SUPPORTED)

#define USB_HCD_GET_PORT_COUNT() \
    (UsbActiveHcd.Ops ? UsbActiveHcd.Ops->GetPortCount(UsbActiveHcd.Context) : 0)

#define USB_HCD_CONTROL_TRANSFER(dev, ep, speed, setup, data, len, in, actual) \
    (UsbActiveHcd.Ops ? UsbActiveHcd.Ops->ControlTransfer(UsbActiveHcd.Context, \
     (dev), (ep), (speed), (setup), (data), (len), (in), (actual)) : STATUS_NOT_SUPPORTED)

#define USB_HCD_BULK_TRANSFER(dev, ep, speed, data, len, in, actual) \
    (UsbActiveHcd.Ops ? UsbActiveHcd.Ops->BulkTransfer(UsbActiveHcd.Context, \
     (dev), (ep), (speed), (data), (len), (in), (actual)) : STATUS_NOT_SUPPORTED)

#define USB_HCD_ADDRESS_DEVICE(port, speed, addr) \
    (UsbActiveHcd.Ops && UsbActiveHcd.Ops->AddressDevice ? \
     UsbActiveHcd.Ops->AddressDevice(UsbActiveHcd.Context, (port), (speed), (addr)) : \
     STATUS_NOT_SUPPORTED)

#endif /* _USBHCD_H */
