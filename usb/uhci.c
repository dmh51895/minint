/*
 * MinNT - usb/uhci.c
 * UHCI USB Host Controller Driver. Supports both I/O port and MMIO BARs.
 * Manages the UHCI registers, frame list, Transfer Descriptors (TDs),
 * and Queue Heads (QHs).
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/hal.h>
#include <nt/usb.h>
#include <nt/rtl.h>

#define TAG_UHCI 0x49434855  /* 'UHCI' */

/* ---- UHCI controller context (from usb.h) --------------------------- */

UHCI_CONTEXT UhciContext;

/* ---- PCI config space offsets ------------------------------------------ */

#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_CLASS_REVISION  0x08
#define PCI_BASE_ADDR_4     0x20
#define PCI_INTERRUPT_LINE  0x3C

#define PCI_CLASS_SERIAL    0x0C
#define PCI_SUBCLASS_UHCI   0x03

/* ---- Read/write UHCI registers (I/O or MMIO) --------------------------- */

FORCEINLINE USHORT UhciReadW(PUHCI_CONTEXT Uhci, ULONG Offset)
{
    if (Uhci->IsIoSpace)
        return READ_PORT_USHORT((USHORT)(Uhci->IoBase + Offset));
    else
        return READ_REGISTER_USHORT((volatile USHORT *)((PUCHAR)Uhci->Regs + Offset));
}

FORCEINLINE VOID UhciWriteW(PUHCI_CONTEXT Uhci, ULONG Offset, USHORT Value)
{
    if (Uhci->IsIoSpace)
        WRITE_PORT_USHORT((USHORT)(Uhci->IoBase + Offset), Value);
    else
        WRITE_REGISTER_USHORT((volatile USHORT *)((PUCHAR)Uhci->Regs + Offset), Value);
}

FORCEINLINE ULONG UhciReadL(PUHCI_CONTEXT Uhci, ULONG Offset)
{
    if (Uhci->IsIoSpace)
        return READ_PORT_ULONG((USHORT)(Uhci->IoBase + Offset));
    else
        return READ_REGISTER_ULONG((volatile ULONG *)((PUCHAR)Uhci->Regs + Offset));
}

FORCEINLINE VOID UhciWriteL(PUHCI_CONTEXT Uhci, ULONG Offset, ULONG Value)
{
    if (Uhci->IsIoSpace)
        WRITE_PORT_ULONG((USHORT)(Uhci->IoBase + Offset), Value);
    else
        WRITE_REGISTER_ULONG((volatile ULONG *)((PUCHAR)Uhci->Regs + Offset), Value);
}

/* ---- Reset the host controller ----------------------------------------- */

static NTSTATUS UhciReset(PUHCI_CONTEXT Uhci)
{
    USHORT Cmd;
    ULONG Timeout;

    UhciWriteW(Uhci, 0, UHCI_CMD_HCRESET);
    for (Timeout = 0; Timeout < 1000; Timeout++)
    {
        Cmd = UhciReadW(Uhci, 0);
        if (!(Cmd & UHCI_CMD_HCRESET))
            return STATUS_SUCCESS;
        KeStallExecutionProcessor(1000);
    }
    return STATUS_UNSUCCESSFUL;
}

/* ---- Initialize the frame list ----------------------------------------- */

static NTSTATUS UhciInitFrameList(PUHCI_CONTEXT Uhci)
{
    ULONG i;

    for (i = 0; i < 1024; i++)
        Uhci->FrameList[i] = 0x00000001; /* Terminate */

    UhciWriteL(Uhci, 0x08, (ULONG)(ULONG_PTR)&Uhci->FrameList[0]);
    return STATUS_SUCCESS;
}

/* ---- Start the host controller ----------------------------------------- */

static NTSTATUS UhciStart(PUHCI_CONTEXT Uhci)
{
    USHORT Cmd;
    ULONG Timeout;

    Cmd = UHCI_CMD_MAXP | UHCI_CMD_RUN;
    UhciWriteW(Uhci, 0, Cmd);

    for (Timeout = 0; Timeout < 1000; Timeout++)
    {
        USHORT Status = UhciReadW(Uhci, 2);
        if (!(Status & UHCI_STS_HALTED))
            return STATUS_SUCCESS;
        KeStallExecutionProcessor(1000);
    }
    return STATUS_UNSUCCESSFUL;
}

/* ---- Interrupt handler ------------------------------------------------- */

static VOID NTAPI UhciInterruptHandler(PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    USHORT Status;

    /* Read interrupt status */
    Status = UhciReadW(&UhciContext, 2);

    if (Status & UHCI_STS_USBINT)
    {
        /* USB interrupt — TD complete */
        /* Acknowledge */
        UhciWriteW(&UhciContext, 2, UHCI_STS_USBINT);
    }

    if (Status & UHCI_STS_ERROR)
    {
        /* Error interrupt */
        UhciWriteW(&UhciContext, 2, UHCI_STS_ERROR);
    }

    if (Status & UHCI_STS_RD)
    {
        /* Resume detect */
        UhciWriteW(&UhciContext, 2, UHCI_STS_RD);
    }

    if (Status & UHCI_STS_HSE)
    {
        /* Host system error */
        DbgPrint("USB: UHCI host system error!\n");
        UhciWriteW(&UhciContext, 2, UHCI_STS_HSE);
    }

    HalEndOfInterrupt(UhciContext.IrqNumber);
}

/* ---- Probe and initialize a UHCI controller ---------------------------- */

NTSTATUS NTAPI UsbInitUhciController(ULONG Bus, ULONG Device, ULONG Function)
{
    ULONG VendorDevice, ClassRev;
    ULONG BarValue;
    UCHAR Irq;
    NTSTATUS Status;

    VendorDevice = HalPciReadConfig(Bus, Device, Function, PCI_VENDOR_ID);
    ClassRev = HalPciReadConfig(Bus, Device, Function, PCI_CLASS_REVISION);

    if (((ClassRev >> 16) & 0xFFFF) != ((PCI_CLASS_SERIAL << 8) | PCI_SUBCLASS_UHCI))
        return STATUS_UNSUCCESSFUL;

    DbgPrint("USB: UHCI found at %02x:%02x.%x VID=0x%04x DID=0x%04x\n",
             (unsigned)Bus, (unsigned)Device, (unsigned)Function,
             (unsigned)(USHORT)(VendorDevice & 0xFFFF),
             (unsigned)(USHORT)(VendorDevice >> 16));

    /* Read BAR4 */
    BarValue = HalPciReadConfig(Bus, Device, Function, PCI_BASE_ADDR_4);
    Irq = (UCHAR)(HalPciReadConfig(Bus, Device, Function, PCI_INTERRUPT_LINE) & 0xFF);

    RtlZeroMemory(&UhciContext, sizeof(UhciContext));

    if (BarValue & 0x01)
    {
        UhciContext.IoBase = BarValue & ~0x03;
        UhciContext.IsIoSpace = TRUE;
        DbgPrint("USB: UHCI I/O base=0x%04lx, IRQ=%u\n",
                 (unsigned long)UhciContext.IoBase, (unsigned)Irq);
    }
    else
    {
        UhciContext.Regs = (PUHCI_REGISTERS)(ULONG_PTR)(BarValue & ~0x0F);
        UhciContext.IsIoSpace = FALSE;
        DbgPrint("USB: UHCI MMIO base=%p, IRQ=%u\n",
                 (void*)UhciContext.Regs, (unsigned)Irq);
    }

    UhciContext.IrqNumber = Irq;
    UhciContext.NumPorts = 2;

    Status = UhciReset(&UhciContext);
    if (!NT_SUCCESS(Status)) { DbgPrint("USB: UHCI reset failed\n"); return Status; }

    Status = UhciInitFrameList(&UhciContext);
    if (!NT_SUCCESS(Status)) { DbgPrint("USB: UHCI frame list failed\n"); return Status; }

    Status = UhciStart(&UhciContext);
    if (!NT_SUCCESS(Status)) { DbgPrint("USB: UHCI start failed\n"); return Status; }

    /* Connect interrupt handler */
    KeConnectInterrupt(PIC_IRQ_BASE + Irq, UhciInterruptHandler);
    HalEnableSystemInterrupt(Irq);

    /* Enable UHCI interrupts (USBINT + ERROR) */
    UhciWriteW(&UhciContext, 4, UHCI_STS_USBINT | UHCI_STS_ERROR);

    UhciContext.Initialized = TRUE;
    DbgPrint("USB: UHCI running (IRQ %u connected)\n", (unsigned)Irq);

    /* Check port status */
    for (UCHAR Port = 0; Port < UhciContext.NumPorts; Port++)
    {
        USHORT PortStat;
        if (UhciContext.IsIoSpace)
            PortStat = READ_PORT_USHORT((USHORT)(UhciContext.IoBase + 0x10 + Port * 2));
        else
            PortStat = READ_REGISTER_USHORT(&UhciContext.Regs->PortControl[Port]);

        DbgPrint("USB: port %u status=0x%04x%s%s%s\n",
                 (unsigned)Port, (unsigned)PortStat,
                 (PortStat & UHCI_PORT_CONNECT) ? " CONNECTED" : "",
                 (PortStat & UHCI_PORT_ENABLE)  ? " ENABLED" : "",
                 (PortStat & UHCI_PORT_LOWSPEED) ? " LOWSPEED" : "");
    }

    return STATUS_SUCCESS;
}

/* ---- Scan PCI bus for UHCI controllers --------------------------------- */

NTSTATUS NTAPI UsbInitSystem(VOID)
{
    ULONG Bus, Device;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    DbgPrint("USB: scanning PCI for UHCI...\n");

    for (Bus = 0; Bus < 1; Bus++)
    {
        for (Device = 0; Device < 32; Device++)
        {
            ULONG VendorDevice = HalPciReadConfig(Bus, Device, 0, PCI_VENDOR_ID);
            if ((USHORT)(VendorDevice & 0xFFFF) == 0xFFFF)
                continue;

            Status = UsbInitUhciController(Bus, Device, 0);
            if (NT_SUCCESS(Status))
                return STATUS_SUCCESS;
        }
    }

    DbgPrint("USB: no UHCI found\n");
    return STATUS_UNSUCCESSFUL;
}
