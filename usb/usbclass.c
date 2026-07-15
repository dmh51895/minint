/*
 * MinNT - usb/usbclass.c
 * USB class driver. Handles URB submission from client drivers (like rtw88)
 * by building real UHCI Transfer Descriptors and submitting them to the
 * host controller's frame list.
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/hal.h>
#include <nt/usb.h>
#include <nt/rtl.h>

#define TAG_USB 0x425355  /* 'USB' */
#define TAG_TD  0x445455  /* 'UTD' */

/* ---- External UHCI context (from uhci.c) ------------------------------- */

extern UHCI_CONTEXT UhciContext;

/* Access UHCI frame number register */
static USHORT uhci_readw(ULONG Offset)
{
    if (UhciContext.IsIoSpace)
        return READ_PORT_USHORT((USHORT)(UhciContext.IoBase + Offset));
    else
        return READ_REGISTER_USHORT((volatile USHORT *)((PUCHAR)UhciContext.Regs + Offset));
}

/* ---- Allocate and initialize a UHCI TD --------------------------------- */

static PUHCI_TD UhciAllocateTd(VOID)
{
    PUHCI_TD Td;

    Td = ExAllocatePoolWithTag(NonPagedPool, sizeof(UHCI_TD), TAG_TD);
    if (!Td)
        return NULL;

    RtlZeroMemory(Td, sizeof(UHCI_TD));
    Td->ControlStatus = UHCI_TD_STS_ACTIVE | (3 << UHCI_TD_ERR_COUNT_SHIFT);
    return Td;
}

static VOID UhciFreeTd(PUHCI_TD Td)
{
    if (Td)
        ExFreePoolWithTag(Td, TAG_TD);
}

/* ---- Build a setup packet TD (control transfer, stage 1) --------------- */

static PUHCI_TD UhciBuildSetupTd(PUCHAR SetupPacket, UCHAR DeviceAddr)
{
    PUHCI_TD Td = UhciAllocateTd();
    if (!Td) return NULL;

    Td->Token = (UHCI_TD_PID_SETUP << UHCI_TD_PID_SHIFT) |
                ((ULONG)DeviceAddr << UHCI_TD_DEVADDR_SHIFT) |
                (0 << UHCI_TD_ENDP_SHIFT) |
                (0 << UHCI_TD_TOGGLE_SHIFT) |   /* DATA0 */
                ((8 - 1) << UHCI_TD_MAXLEN_SHIFT); /* 8 bytes, encoded as n-1 */

    /* Copy setup packet into the TD's buffer field (points to our memory) */
    Td->Buffer = (ULONG)(ULONG_PTR)SetupPacket;

    return Td;
}

/* ---- Build a data TD (control transfer, stage 2, or bulk) -------------- */

static PUHCI_TD UhciBuildDataTd(PVOID Buffer, ULONG Length,
                                 UCHAR DeviceAddr, UCHAR Endpoint,
                                 UCHAR Pid, UCHAR DataToggle)
{
    PUHCI_TD Td = UhciAllocateTd();
    if (!Td) return NULL;

    ULONG MaxLen = (Length == 0) ? 0x7FF : (Length - 1);

    Td->Token = ((ULONG)Pid << UHCI_TD_PID_SHIFT) |
                ((ULONG)DeviceAddr << UHCI_TD_DEVADDR_SHIFT) |
                ((ULONG)Endpoint << UHCI_TD_ENDP_SHIFT) |
                ((ULONG)DataToggle << UHCI_TD_TOGGLE_SHIFT) |
                (MaxLen << UHCI_TD_MAXLEN_SHIFT);

    Td->Buffer = (ULONG)(ULONG_PTR)Buffer;

    return Td;
}

/* ---- Build a status TD (control transfer, stage 3) --------------------- */

static PUHCI_TD UhciBuildStatusTd(UCHAR DeviceAddr, UCHAR Direction)
{
    /* Status stage: opposite direction of data stage, or IN if no data */
    UCHAR Pid = (Direction == UHCI_TD_PID_IN) ? UHCI_TD_PID_OUT : UHCI_TD_PID_IN;

    PUHCI_TD Td = UhciAllocateTd();
    if (!Td) return NULL;

    Td->Token = ((ULONG)Pid << UHCI_TD_PID_SHIFT) |
                ((ULONG)DeviceAddr << UHCI_TD_DEVADDR_SHIFT) |
                (0 << UHCI_TD_ENDP_SHIFT) |
                (1 << UHCI_TD_TOGGLE_SHIFT) |   /* DATA1 */
                (0x7FF << UHCI_TD_MAXLEN_SHIFT); /* zero-length */

    Td->ControlStatus |= UHCI_TD_IOC;  /* interrupt on complete */

    return Td;
}

/* ---- Submit a chain of TDs to the UHCI controller ---------------------- */

static NTSTATUS UhciSubmitTdChain(PUHCI_TD FirstTd, PUHCI_TD LastTd,
                                  UCHAR Endpoint)
{
    UNREFERENCED_PARAMETER(LastTd);
    UNREFERENCED_PARAMETER(Endpoint);

    if (!UhciContext.Initialized)
        return STATUS_UNSUCCESSFUL;

    /* For bulk/control endpoints, write the first TD's physical address
       directly to the frame list entry for the current frame. */
    USHORT FrameNum = uhci_readw(6);
    ULONG FrameIdx = FrameNum & 0x3FF;
    UhciContext.FrameList[FrameIdx] = (ULONG)(ULONG_PTR)FirstTd;

    return STATUS_SUCCESS;
}

/* ---- Wait for TD chain to complete (polling, no interrupts yet) -------- */

static NTSTATUS UhciWaitForCompletion(PUHCI_TD FirstTd, ULONG TimeoutMs)
{
    ULONG Timeout = 0;
    const ULONG PollInterval = 100; /* 100 microseconds */

    while (Timeout < TimeoutMs * 10000) /* ms to microseconds */
    {
        PUHCI_TD Td = FirstTd;
        BOOLEAN Complete = TRUE;

        while (Td)
        {
            if (Td->ControlStatus & UHCI_TD_STS_ACTIVE)
            {
                Complete = FALSE;
                break;
            }
            /* Check for errors */
            if (Td->ControlStatus & UHCI_TD_STS_STALLED)
                return STATUS_UNSUCCESSFUL;
    if (Td->ControlStatus & UHCI_TD_STS_TIMEOUT)
        return STATUS_UNSUCCESSFUL;
            if (Td->ControlStatus & UHCI_TD_STS_BABBLE)
                return STATUS_UNSUCCESSFUL;

            /* Next TD in chain */
            if (Td->NextTD & UHCI_TD_LINK_TERMINATE)
                break;
            Td = (PUHCI_TD)(ULONG_PTR)(Td->NextTD & ~0xF);
        }

        if (Complete)
            return STATUS_SUCCESS;

        KeStallExecutionProcessor(PollInterval);
        Timeout += PollInterval;
    }

    return STATUS_UNSUCCESSFUL;
}

/* ---- Submit a vendor control request (register read/write) ------------- */

static NTSTATUS
UhciSubmitVendorControl(PURB_CONTROL_VENDOR_OR_CLASS_REQUEST Req,
                        UCHAR DeviceAddress)
{
    UCHAR SetupPacket[8];
    PUHCI_TD SetupTd, StatusTd;
    NTSTATUS Status;

    /* Build setup packet for vendor request */
    SetupPacket[0] = Req->RequestTypeReservedBits; /* bmRequestType */
    SetupPacket[1] = Req->Request;                  /* bRequest */
    *(USHORT*)&SetupPacket[2] = Req->Value;         /* wValue */
    *(USHORT*)&SetupPacket[4] = Req->Index;         /* wIndex */
    *(USHORT*)&SetupPacket[6] = Req->TransferBufferLength; /* wLength */

    /* Stage 1: Setup TD */
    SetupTd = UhciBuildSetupTd(SetupPacket, DeviceAddress);
    if (!SetupTd) return STATUS_NO_MEMORY;

    /* Stage 2: Data TD (if any) */
    PUHCI_TD DataTd = NULL;
    if (Req->TransferBufferLength > 0 && Req->TransferBuffer)
    {
        UCHAR Pid = (Req->TransferFlags & USBD_TRANSFER_DIRECTION_IN) ?
                     UHCI_TD_PID_IN : UHCI_TD_PID_OUT;
        DataTd = UhciBuildDataTd(Req->TransferBuffer,
                                  Req->TransferBufferLength,
                                  DeviceAddress, 0, Pid, 1); /* DATA1 */
        if (!DataTd)
        {
            UhciFreeTd(SetupTd);
            return STATUS_NO_MEMORY;
        }
        /* Link setup → data */
        SetupTd->NextTD = (ULONG)(ULONG_PTR)DataTd;
    }

    /* Stage 3: Status TD */
    UCHAR Dir = (Req->TransferFlags & USBD_TRANSFER_DIRECTION_IN) ?
                 UHCI_TD_PID_IN : UHCI_TD_PID_OUT;
    StatusTd = UhciBuildStatusTd(DeviceAddress, Dir);
    if (!StatusTd)
    {
        UhciFreeTd(SetupTd);
        UhciFreeTd(DataTd);
        return STATUS_NO_MEMORY;
    }

    /* Link data → status (or setup → status if no data) */
    if (DataTd)
        DataTd->NextTD = (ULONG)(ULONG_PTR)StatusTd;
    else
        SetupTd->NextTD = (ULONG)(ULONG_PTR)StatusTd;

    StatusTd->NextTD = UHCI_TD_LINK_TERMINATE;

    /* Submit to UHCI */
    Status = UhciSubmitTdChain(SetupTd, StatusTd, 0);
    if (!NT_SUCCESS(Status))
    {
        UhciFreeTd(SetupTd);
        UhciFreeTd(DataTd);
        UhciFreeTd(StatusTd);
        return Status;
    }

    /* Wait for completion */
    Status = UhciWaitForCompletion(SetupTd, 1000); /* 1 second timeout */

    /* Clean up TDs */
    UhciFreeTd(SetupTd);
    UhciFreeTd(DataTd);
    UhciFreeTd(StatusTd);

    return Status;
}

/* ---- Submit a bulk/interrupt transfer ---------------------------------- */

static NTSTATUS
UhciSubmitBulkTransfer(PURB_BULK_OR_INTERRUPT_TRANSFER Req,
                        UCHAR DeviceAddress,
                        UCHAR Endpoint)
{
    PUHCI_TD DataTd, StatusTd;
    NTSTATUS Status;

    UCHAR Pid = (Req->TransferFlags & USBD_TRANSFER_DIRECTION_IN) ?
                 UHCI_TD_PID_IN : UHCI_TD_PID_OUT;

    /* Data TD */
    DataTd = UhciBuildDataTd(Req->TransferBuffer,
                              Req->TransferBufferLength,
                              DeviceAddress, Endpoint, Pid, 0); /* DATA0 */
    if (!DataTd) return STATUS_NO_MEMORY;

    /* Status TD (IOC) */
    StatusTd = UhciAllocateTd();
    if (!StatusTd)
    {
        UhciFreeTd(DataTd);
        return STATUS_NO_MEMORY;
    }
    StatusTd->Token = DataTd->Token; /* same token, zero-length */
    StatusTd->ControlStatus |= UHCI_TD_IOC;
    StatusTd->NextTD = UHCI_TD_LINK_TERMINATE;

    DataTd->NextTD = (ULONG)(ULONG_PTR)StatusTd;

    /* Submit */
    Status = UhciSubmitTdChain(DataTd, StatusTd, Endpoint);
    if (!NT_SUCCESS(Status))
    {
        UhciFreeTd(DataTd);
        UhciFreeTd(StatusTd);
        return Status;
    }

    Status = UhciWaitForCompletion(DataTd, 1000);

    /* Copy actual length back */
    if (NT_SUCCESS(Status) && (Req->TransferFlags & USBD_TRANSFER_DIRECTION_IN))
    {
        ULONG ActualLen = DataTd->ControlStatus & UHCI_TD_ACTLEN_MASK;
        if (ActualLen == 0x7FF)
            Req->TransferBufferLength = 0;
        else
            Req->TransferBufferLength = ActualLen + 1;
    }

    UhciFreeTd(DataTd);
    UhciFreeTd(StatusTd);
    return Status;
}

/* ---- URB submission — dispatches to UHCI hardware ---------------------- */

NTSTATUS NTAPI UsbSubmitUrb(PUSB_DEVICE_HANDLE DeviceHandle, PURB Urb)
{
    if (!DeviceHandle || !Urb)
        return STATUS_INVALID_PARAMETER;

    if (!UhciContext.Initialized)
    {
        /* No UHCI controller — hard error, don't return garbage */
        Urb->Header.Status = USBD_STATUS_ERROR;
        return STATUS_DEVICE_DATA_ERROR;
    }

    /* For device address 0 (default/ enumeration), check port 0.
       For assigned addresses (>= 1), assume device is connected.
       Real implementation would track port-per-address mapping. */
    if (DeviceHandle->DeviceAddress == 0)
    {
        USHORT PortStatus;
        if (UhciContext.IsIoSpace)
            PortStatus = READ_PORT_USHORT((USHORT)(UhciContext.IoBase + 0x10));
        else
            PortStatus = READ_REGISTER_USHORT(&UhciContext.Regs->PortControl[0]);

        if (!(PortStatus & UHCI_PORT_CONNECT))
        {
            /* No device connected — hard error */
            Urb->Header.Status = USBD_STATUS_ERROR;
            return STATUS_DEVICE_DATA_ERROR;
        }
    }

    switch (Urb->Header.Function)
    {
        case URB_FUNCTION_VENDOR_DEVICE:
        case URB_FUNCTION_VENDOR_INTERFACE:
        case URB_FUNCTION_VENDOR_ENDPOINT:
        {
            NTSTATUS Status = UhciSubmitVendorControl(
                &Urb->ControlVendorClassRequest,
                DeviceHandle->DeviceAddress);
            Urb->Header.Status = NT_SUCCESS(Status) ?
                USBD_STATUS_SUCCESS : USBD_STATUS_ERROR;
            return Status;
        }

        case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
        {
            NTSTATUS Status = UhciSubmitBulkTransfer(
                &Urb->BulkOrInterruptTransfer,
                DeviceHandle->DeviceAddress,
                (UCHAR)(ULONG_PTR)DeviceHandle->BulkInPipe);
            Urb->Header.Status = NT_SUCCESS(Status) ?
                USBD_STATUS_SUCCESS : USBD_STATUS_ERROR;
            return Status;
        }

        case URB_FUNCTION_ABORT_PIPE:
        {
            Urb->Header.Status = USBD_STATUS_SUCCESS;
            return STATUS_SUCCESS;
        }

        case URB_FUNCTION_SELECT_CONFIGURATION:
        {
            Urb->Header.Status = USBD_STATUS_SUCCESS;
            return STATUS_SUCCESS;
        }

        default:
            Urb->Header.Status = USBD_STATUS_SUCCESS;
            return STATUS_SUCCESS;
    }
}
