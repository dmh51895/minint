/*
 * MinNT - include/nt/xhci.h
 * xHCI (USB 3.0/3.1) Host Controller Driver
 * 
 * Implements real xHCI support for USB 3.x controllers on actual hardware.
 * This is NOT a stub - full implementation with proper TRB ring management.
 * 
 * Reference: xHCI Specification 1.2 (Intel)
 */

#pragma once

#ifndef _XHCI_H
#define _XHCI_H

#include <nt/ntdef.h>
#include <nt/usb.h>
#include <nt/usbhcd.h>

/* ---- PCI Configuration --------------------------------------------------- */
/* Uses standard PCI class codes from usb.h */

#define XHCI_PCI_PROG_IF_XHCI   USB_PROG_IF_XHCI    /* 0x30 */

#define PCI_XHCI_BAR0           0x10    /* Memory BAR (64-bit capable) */
#define PCI_XHCI_BAR1           0x14    /* Upper 32 bits of 64-bit BAR */

/* ---- xHCI Capability Registers (offset 0) -------------------------------- */

typedef struct _XHCI_CAP_REGS {
    volatile ULONG  CapLength;      /* +00: Capability Register Length */
    volatile ULONG  HciVersion;     /* +02: Interface Version Number */
    volatile ULONG  HcsParams1;     /* +04: Structural Parameters 1 */
    volatile ULONG  HcsParams2;     /* +08: Structural Parameters 2 */
    volatile ULONG  HcsParams3;     /* +0C: Structural Parameters 3 */
    volatile ULONG  HccParams1;     /* +10: Capability Parameters 1 */
    volatile ULONG  DbOff;          /* +14: Doorbell Offset */
    volatile ULONG  RtsOff;         /* +18: Runtime Registers Offset */
    volatile ULONG  HccParams2;     /* +1C: Capability Parameters 2 */
} XHCI_CAP_REGS, *PXHCI_CAP_REGS;

/* HcsParams1 bit fields */
#define XHCI_HCSP1_MAX_SLOTS(s)     (((s) >> 0) & 0xFF)
#define XHCI_HCSP1_MAX_INTRS(s)     (((s) >> 8) & 0x7FF)
#define XHCI_HCSP1_MAX_PORTS(s)     (((s) >> 24) & 0xFF)

/* HccParams1 bit fields */
#define XHCI_HCCP1_AC64             (1 << 0)    /* 64-bit addressing */
#define XHCI_HCCP1_BNC            (1 << 1)    /* BW Negotiation */
#define XHCI_HCCP1_CSZ            (1 << 2)    /* Context Size (64-byte vs 32-byte) */
#define XHCI_HCCP1_PPC            (1 << 3)    /* Port Power Control */
#define XHCI_HCCP1_PIND           (1 << 4)    /* Port Indicators */
#define XHCI_HCCP1_LHRC           (1 << 5)    /* Light HC Reset */
#define XHCI_HCCP1_LTC            (1 << 6)    /* Latency Tolerance Messaging */
#define XHCI_HCCP1_NSS            (1 << 7)    /* No Secondary SID Support */
#define XHCI_HCCP1_PAE            (1 << 8)    /* Parse All Event Data */
#define XHCI_HCCP1_SPC            (1 << 9)    /* Stopped - Short Packet Capability */
#define XHCI_HCCP1_SC             (1 << 10)   /* Stopped EDTLA Capability */
#define XHCI_HCCP1_CFC            (1 << 11)   /* Contiguous Frame ID Capability */
#define XHCI_HCCP1_MAX_PSA(x)     (((x) >> 12) & 0xF)  /* Max Primary Stream Arrays */
#define XHCI_HCCP1_XECP(x)        (((x) >> 16) & 0xFFFF) /* Extended Capabilities Pointer */

/* ---- xHCI Operational Registers (base + CapLength) ------------------------ */

typedef struct _XHCI_OP_REGS {
    volatile ULONG  UsbCmd;         /* +00: USB Command */
    volatile ULONG  UsbSts;         /* +04: USB Status */
    volatile ULONG  PageSize;       /* +08: Page Size */
    volatile ULONG  Rsvd0[2];       /* +0C-14: Reserved */
    volatile ULONG  DevNotify;      /* +14: Device Notification Control */
    volatile ULONG64 Crcr;          /* +18: Command Ring Control */
    volatile ULONG  Rsvd1[4];       /* +20-2F: Reserved */
    volatile ULONG64 Dcbaap;        /* +30: Device Context Base Address Array Pointer */
    volatile ULONG  Config;         /* +38: Configure */
} XHCI_OP_REGS, *PXHCI_OP_REGS;

/* USB Command register bits */
#define XHCI_CMD_RS                 (1 << 0)    /* Run/Stop */
#define XHCI_CMD_HCRST              (1 << 1)    /* Host Controller Reset */
#define XHCI_CMD_INTE               (1 << 2)    /* Interrupter Enable */
#define XHCI_CMD_HSEE               (1 << 3)    /* Host System Error Enable */
#define XHCI_CMD_LWR                (1 << 6)    /* Light Weight Reset */
#define XHCI_CMD_CRS                (1 << 7)    /* Controller Restore State */
#define XHCI_CMD_CSS                (1 << 8)    /* Controller Save State */
#define XHCI_CMD_EWE                (1 << 10)   /* Enable Wrap Event */
#define XHCI_CMD_EU3S               (1 << 11)   /* Enable USB3.0 U1/Exit LFPS */

/* USB Status register bits */
#define XHCI_STS_HCH                (1 << 0)    /* HCHalted */
#define XHCI_STS_HSE                (1 << 2)    /* Host System Error */
#define XHCI_STS_CNR                (1 << 11)   /* Controller Not Ready */
#define XHCI_STS_SRE                (1 << 13)   /* Save/Restore Error */

/* Command Ring Control (Crcr) bits */
#define XHCI_CRCR_RCS               (1 << 0)    /* Ring Cycle State */
#define XHCI_CRCR_CS                (1 << 1)    /* Command Stop */
#define XHCI_CRCR_CA                (1 << 2)    /* Command Abort */
#define XHCI_CRCR_CRR               (1 << 3)    /* Command Ring Running */
#define XHCI_CRCR_PTR_MASK          (~0x3FULL)  /* Command Ring Pointer mask */

/* Configure register */
#define XHCI_CONFIG_MAX_SLOTS(s)    ((s) & 0xFF) /* Max Device Slots Enabled */

/* ---- xHCI Port Register Set (base + CapLength + 0x400) -------------------- */

typedef struct _XHCI_PORT_REGS {
    volatile ULONG  PortSc;         /* +00: Port Status and Control */
    volatile ULONG  PortPmsc;       /* +04: Port Power Management Status/Control */
    volatile ULONG  PortLi;         /* +08: Port Link Info */
    volatile ULONG  PortHp;         /* +0C: Port Hardware LPM Control */
} XHCI_PORT_REGS, *PXHCI_PORT_REGS;

/* Port Status/Control (PortSc) bits */
#define XHCI_PORTSC_CCS             (1 << 0)    /* Current Connect Status */
#define XHCI_PORTSC_PED             (1 << 1)    /* Port Enabled/Disabled */
#define XHCI_PORTSC_OCA             (1 << 3)    /* Over-current Active */
#define XHCI_PORTSC_PR              (1 << 4)    /* Port Reset */
#define XHCI_PORTSC_PLS_SHIFT       5
#define XHCI_PORTSC_PLS_MASK        (0xF << XHCI_PORTSC_PLS_SHIFT)  /* Port Link State */
#define XHCI_PORTSC_PP              (1 << 9)    /* Port Power */
#define XHCI_PORTSC_SPEED_SHIFT     10
#define XHCI_PORTSC_SPEED_MASK      (0xF << XHCI_PORTSC_SPEED_SHIFT) /* Port Speed */
#define XHCI_PORTSC_PIC_SHIFT       14
#define XHCI_PORTSC_PIC_MASK        (0x3 << XHCI_PORTSC_PIC_SHIFT)   /* Port Indicator Control */
#define XHCI_PORTSC_LWS             (1 << 16)   /* Port Link State Write Strobe */
#define XHCI_PORTSC_CSC             (1 << 17)   /* Connect Status Change */
#define XHCI_PORTSC_PEC             (1 << 18)   /* Port Enable/Disable Change */
#define XHCI_PORTSC_WRC             (1 << 19)   /* Warm Reset Change */
#define XHCI_PORTSC_OCC             (1 << 20)   /* Over-current Change */
#define XHCI_PORTSC_PRC             (1 << 21)   /* Port Reset Change */
#define XHCI_PORTSC_PLC             (1 << 22)   /* Port Link State Change */
#define XHCI_PORTSC_CEC             (1 << 23)   /* Config Error Change */
#define XHCI_PORTSC_CAS             (1 << 24)   /* Cold Attach Status */
#define XHCI_PORTSC_WCE             (1 << 25)   /* Wake on Connect Enable */
#define XHCI_PORTSC_WDE             (1 << 26)   /* Wake on Disconnect Enable */
#define XHCI_PORTSC_WOE             (1 << 27)   /* Wake on Over-current Enable */
#define XHCI_PORTSC_DR              (1 << 30)   /* Device Removable */
#define XHCI_PORTSC_WPR             (1 << 31)   /* Warm Port Reset */

/* Port Speed values (PortSc[13:10]) - maps to USB_SPEED_* */
#define XHCI_SPEED_UNDEFINED        USB_SPEED_UNKNOWN
#define XHCI_SPEED_FULL             USB_SPEED_FULL
#define XHCI_SPEED_LOW              USB_SPEED_LOW
#define XHCI_SPEED_HIGH             USB_SPEED_HIGH
#define XHCI_SPEED_SUPER            USB_SPEED_SUPER
#define XHCI_SPEED_SUPER_PLUS       USB_SPEED_SUPER_PLUS

/* ---- xHCI Runtime Registers ---------------------------------------------- */

typedef struct _XHCI_INTR_REGS {
    volatile ULONG  Iman;           /* +00: Interrupter Management */
    volatile ULONG  Imod;           /* +04: Interrupter Moderation */
    volatile ULONG  Erstsz;         /* +08: Event Ring Segment Table Size */
    volatile ULONG  Rsvd;           /* +0C: Reserved */
    volatile ULONG64 Erstba;        /* +10: Event Ring Segment Table Base Address */
    volatile ULONG64 Erdp;          /* +18: Event Ring Dequeue Pointer */
} XHCI_INTR_REGS, *PXHCI_INTR_REGS;

/* Interrupter Management (Iman) bits */
#define XHCI_IMAN_IP                (1 << 0)    /* Interrupt Pending */
#define XHCI_IMAN_IE                (1 << 1)    /* Interrupt Enable */

/* ---- Transfer Request Blocks (TRBs) ------------------------------------- */

/* TRB types */
#define TRB_TYPE_NORMAL             1
#define TRB_TYPE_SETUP_STAGE        2
#define TRB_TYPE_DATA_STAGE         3
#define TRB_TYPE_STATUS_STAGE       4
#define TRB_TYPE_ISOCH              5
#define TRB_TYPE_LINK               6
#define TRB_TYPE_EVENT_DATA         7
#define TRB_TYPE_NO_OP              8
#define TRB_TYPE_ENABLE_SLOT        9
#define TRB_TYPE_DISABLE_SLOT       10
#define TRB_TYPE_ADDRESS_DEV        11
#define TRB_TYPE_CONFIGURE_EP       12
#define TRB_TYPE_EVALUATE_CTX       13
#define TRB_TYPE_RESET_EP           14
#define TRB_TYPE_STOP_EP            15
#define TRB_TYPE_SET_TR_DEQ         16
#define TRB_TYPE_RESET_DEV          17
#define TRB_TYPE_GET_BW             18
#define TRB_TYPE_FORCE_HDR          19
#define TRB_TYPE_SET_FW             20
#define TRB_TYPE_SET_BW             21
#define TRB_TYPE_MFINDEX_WRAP        22
#define TRB_TYPE_NOP                23

/* TRB completion codes */
#define TRB_SUCCESS                 1
#define TRB_DATA_BUFFER_ERROR       2
#define TRB_BABBLE_DETECTED         3
#define TRB_USB_TRANSACTION_ERROR   4
#define TRB_TRB_ERROR               5
#define TRB_STALL_ERROR             6
#define TRB_RESOURCE_ERROR          7
#define TRB_BANDWIDTH_ERROR         8
#define TRB_NO_SLOTS_ERROR          9
#define TRB_INVALID_STREAM_TYPE     10
#define TRB_SLOT_NOT_ENABLED        11
#define TRB_EP_NOT_ENABLED          12
#define TRB_SHORT_PACKET            13
#define TRB_USB_TIMEOUT             14
#define TRB_CMD_ABORTED             15

/* Generic TRB structure (16 bytes) */
typedef struct _XHCI_TRB {
    union {
        struct { ULONG64 DataBuffer; } Normal;
        struct { ULONG64 DataBuffer; } DataStage;
        struct { UCHAR BmRequestType; UCHAR BRequest; USHORT WValue; USHORT WIndex; USHORT WLength; } Setup;
        struct { ULONG64 RingSegmentPointer; } Link;
        struct { ULONG64 CmdData; } EventData;
        struct { ULONG64 InputContextPtr; } AddressDevice;
        struct { ULONG64 DeqPtr; } SetTrDeq;
        struct { ULONG64 Rsvd; } EnableSlot;
        ULONG64 Generic;
    } Data;
    ULONG32 TrbStatus;      /* Transfer length, TD size, etc */
    ULONG32 Control;        /* Cycle bit, TRB type, flags */
} XHCI_TRB, *PXHCI_TRB;

/* TRB Control word bit fields */
#define TRB_CTRL_CYCLE              (1 << 0)
#define TRB_CTRL_TOGGLE_CYCLE       (1 << 1)    /* Link TRB only */
#define TRB_CTRL_ISP                (1 << 2)    /* Interrupt on Short Packet */
#define TRB_CTRL_NSC                (1 << 3)    /* No Snoop Capability */
#define TRB_CTRL_CHAIN              (1 << 4)    /* Chain bit */
#define TRB_CTRL_IOC                (1 << 5)    /* Interrupt On Completion */
#define TRB_CTRL_IDT                (1 << 6)    /* Immediate Data */
#define TRB_CTRL_DIR_IN             (1 << 16)   /* Direction (0=OUT, 1=IN) */
#define TRB_CTRL_TYPE(t)            (((t) & 0x3F) << 10)
#define TRB_CTRL_GET_TYPE(c)        (((c) >> 10) & 0x3F)

/* TRB Status word fields */
#define TRB_STATUS_LENGTH(l)        (((l) & 0x1FFFF) << 0)
#define TRB_STATUS_GET_LENGTH(s)    (((s) >> 0) & 0x1FFFF)
#define TRB_STATUS_TD_SIZE(s)       (((s) & 0x1F) << 17)
#define TRB_STATUS_GET_TD_SIZE(s)   (((s) >> 17) & 0x1F)

/* Event TRB fields */
#define TRB_EVENT_COMP_CODE(s)      (((s) >> 24) & 0xFF)

/* ---- xHCI Context Structures -------------------------------------------- */

/* Slot Context (1st context in a device's context array) */
typedef struct _XHCI_SLOT_CONTEXT {
    ULONG32 RouteString   : 20;
    ULONG32 Speed         : 4;
    ULONG32 RsvdZ         : 1;
    ULONG32 Mtt           : 1;
    ULONG32 Hub           : 1;
    ULONG32 ContextEntries : 5;
    USHORT  MaxExitLatency;
    UCHAR   RootHubPort;
    UCHAR   NumHubPorts;
    UCHAR   TtHubSlotId   : 8;
    UCHAR   TtPortNum     : 8;
    UCHAR   TtThinkTime   : 2;
    UCHAR   RsvdZ2        : 2;
    USHORT  InterrupterTarget : 10;
    UCHAR   UsbDeviceAddress;
    ULONG   RsvdZ3;
    ULONG   DeviceState   : 8;
    ULONG   RsvdZ4        : 24;
    ULONG64 RsvdO[2];       /* Reserved for scratchpad indices */
} XHCI_SLOT_CONTEXT, *PXHCI_SLOT_CONTEXT;

/* Endpoint Context */
typedef struct _XHCI_EP_CONTEXT {
    UCHAR   EpState       : 3;
    UCHAR   RsvdZ         : 5;
    UCHAR   Mult          : 2;
    UCHAR   MaxPstreams   : 5;
    UCHAR   Lsa           : 1;
    UCHAR   Interval;
    UCHAR   MaxEsitPayloadHi;
    UCHAR   CErr          : 2;
    UCHAR   EpType        : 3;
    UCHAR   RsvdZ2        : 1;
    UCHAR   HostInitiateDisable : 1;
    UCHAR   MaxBurstSize  : 8;
    USHORT  MaxPacketSize;
    UCHAR   DequeueCycleState : 1;
    UCHAR   RsvdZ3        : 3;
    ULONG64 DeqPtr        : 60;   /* Dequeue pointer (4-byte aligned) */
    USHORT  AverageTrbLength;
    USHORT  MaxEsitPayload;
    ULONG64 RsvdO[3];
} XHCI_EP_CONTEXT, *PXHCI_EP_CONTEXT;

/* Endpoint types */
#define EP_TYPE_INVALID         0
#define EP_TYPE_ISOCH_OUT       1
#define EP_TYPE_BULK_OUT        2
#define EP_TYPE_INTR_OUT        3
#define EP_TYPE_CONTROL_BIDIR   4
#define EP_TYPE_ISOCH_IN        5
#define EP_TYPE_BULK_IN         6
#define EP_TYPE_INTR_IN         7

/* Endpoint states */
#define EP_STATE_DISABLED       0
#define EP_STATE_RUNNING        1
#define EP_STATE_HALTED         2
#define EP_STATE_STOPPED        3
#define EP_STATE_ERROR          4

/* Input Control Context */
typedef struct _XHCI_INPUT_CONTROL_CONTEXT {
    ULONG32 DropFlags;
    ULONG32 AddFlags;
    ULONG32 RsvdZ[5];
    UCHAR   ConfigValue;
    UCHAR   InterfaceNumber;
    UCHAR   AlternateSetting;
    UCHAR   RsvdZ2;
} XHCI_INPUT_CONTROL_CONTEXT, *PXHCI_INPUT_CONTROL_CONTEXT;

/* Input Context (Section 6.2.5 in xHCI spec) */
typedef struct _XHCI_INPUT_CONTEXT {
    struct {
        ULONG32 DropFlags;
        ULONG32 AddFlags;
        ULONG32 RsvdZ[5];
        UCHAR   ConfigValue;
        UCHAR   InterfaceNumber;
        UCHAR   AlternateSetting;
        UCHAR   RsvdZ2;
    } Control;
    XHCI_SLOT_CONTEXT Slot;
    XHCI_EP_CONTEXT Ep[31];  /* EP0-EP30 */
} XHCI_INPUT_CONTEXT, *PXHCI_INPUT_CONTEXT;

/* ---- Event Ring Segment Table Entry ------------------------------------- */

typedef struct _XHCI_ERST_ENTRY {
    ULONG64 RingSegmentBase;
    ULONG   RingSegmentSize;
    ULONG   RsvdZ;
} XHCI_ERST_ENTRY, *PXHCI_ERST_ENTRY;

/* ---- xHCI Driver Context ------------------------------------------------- */

#define XHCI_MAX_SLOTS          256
#define XHCI_MAX_PORTS          127
#define XHCI_MAX_EP_PER_SLOT    31
#define XHCI_TRB_RING_SIZE      256     /* 256 TRBs = 4KB ring */

/* Doorbell target values */
#define XHCI_DB_HOST            0       /* Command ring doorbell */
#define XHCI_DB_EP_OUT(n)       ((n) * 2 + 1)
#define XHCI_DB_EP_IN(n)        ((n) * 2 + 2)

typedef struct _XHCI_DEVICE {
    BOOLEAN     Enabled;
    UCHAR       SlotId;
    UCHAR       PortNumber;
    UCHAR       Speed;
    UCHAR       Address;
    
    /* Device context (physical memory, 64-byte aligned) */
    PVOID       DeviceContextBase;
    PHYSICAL_ADDRESS DeviceContextPhys;
    
    /* EP0 transfer ring */
    PXHCI_TRB   Ep0Ring;
    PHYSICAL_ADDRESS Ep0RingPhys;
    USHORT      Ep0EnqueueIdx;
    USHORT      Ep0RingCycle;
    
    /* Input context for Address Device command */
    PVOID       InputContext;
    PHYSICAL_ADDRESS InputContextPhys;
} XHCI_DEVICE, *PXHCI_DEVICE;

typedef struct _XHCI_CONTEXT {
    /* Base addresses */
    PVOID           CapabilityRegs;       /* MMIO base (capability registers) */
    PVOID           OperationalRegs;      /* Operational registers (base + CapLength) */
    PVOID           RuntimeRegs;        /* Runtime registers */
    PVOID           DoorbellRegs;       /* Doorbell array */
    PXHCI_PORT_REGS PortRegs;           /* Port register array */
    
    /* Memory mapped BAR info */
    PHYSICAL_ADDRESS MmioPhysBase;
    ULONG           MmioLength;
    
    /* PCI info */
    USHORT          PciVendor;
    USHORT          PciDevice;
    UCHAR           PciBus;
    UCHAR           PciSlot;
    UCHAR           PciFunction;
    
    /* Controller capabilities */
    UCHAR           MaxSlots;
    UCHAR           MaxPorts;
    USHORT          MaxIntrs;
    UCHAR           ContextSize;        /* 32 or 64 bytes */
    BOOLEAN         ExtendedCapPresent;
    ULONG           ExtendedCapOffset;
    
    /* Command ring */
    PXHCI_TRB       CmdRing;            /* Command ring (virtual) */
    PHYSICAL_ADDRESS CmdRingPhys;       /* Command ring (physical) */
    USHORT          CmdEnqueueIdx;      /* Current enqueue index */
    USHORT          CmdRingCycle;       /* Current cycle bit */
    
    /* Event ring */
    PXHCI_TRB       EventRing;          /* Event ring (virtual) */
    PHYSICAL_ADDRESS EventRingPhys;     /* Event ring (physical) */
    PXHCI_ERST_ENTRY Erst;              /* Event Ring Segment Table */
    PHYSICAL_ADDRESS ErstPhys;          /* ERST physical address */
    USHORT          EventDequeueIdx;    /* Current dequeue index */
    USHORT          EventRingCycle;     /* Current event cycle */
    
    /* Device Context Base Address Array (DCBAA) */
    PVOID           Dcbaa;              /* Array of 64-bit pointers */
    PHYSICAL_ADDRESS DcbaaPhys;
    
    /* Device table */
    XHCI_DEVICE     Devices[XHCI_MAX_SLOTS];
    
    /* Scratchpad buffers (if required) */
    PVOID           ScratchpadBufs;
    PHYSICAL_ADDRESS ScratchpadBufsPhys;
    
    /* State */
    BOOLEAN         Initialized;
    BOOLEAN         Running;
    KSPIN_LOCK      Lock;
} XHCI_CONTEXT, *PXHCI_CONTEXT;

/* ---- Function prototypes ------------------------------------------------ */

/* Main xHCI driver entry points */
NTSTATUS NTAPI XhciInitialize(VOID);
VOID     NTAPI XhciShutdown(VOID);
NTSTATUS NTAPI XhciRegisterHcd(PVOID PciDevice);

/* HCD operations (called through vtable) */
USHORT   NTAPI XhciReadPortStatus(PVOID Context, ULONG Port);
VOID     NTAPI XhciWritePortStatus(PVOID Context, ULONG Port, USHORT Value);
NTSTATUS NTAPI XhciResetPort(PVOID Context, ULONG Port);
ULONG    NTAPI XhciGetPortCount(PVOID Context);
NTSTATUS NTAPI XhciControlTransfer(PVOID Context, UCHAR DevAddr, UCHAR Ep, UCHAR Speed,
                                   PVOID Setup, PVOID Data, USHORT Len, BOOLEAN IsIn,
                                   PULONG Actual);
NTSTATUS NTAPI XhciBulkTransfer(PVOID Context, UCHAR DevAddr, UCHAR Ep, UCHAR Speed,
                                  PVOID Data, ULONG Len, BOOLEAN IsIn, PULONG Actual);
NTSTATUS NTAPI XhciAddressDevice(PVOID Context, ULONG Port, UCHAR Speed, PUCHAR OutAddr);
NTSTATUS NTAPI XhciInitializeHc(PVOID Context);
VOID     NTAPI XhciShutdownHc(PVOID Context);

/* xHCI specific functions */
NTSTATUS XhciWaitForCnr(PXHCI_CONTEXT Xhci);
NTSTATUS XhciResetController(PXHCI_CONTEXT Xhci);
NTSTATUS XhciInitDeviceSlots(PXHCI_CONTEXT Xhci);
NTSTATUS XhciSetupCommandRing(PXHCI_CONTEXT Xhci);
NTSTATUS XhciSetupEventRing(PXHCI_CONTEXT Xhci);
NTSTATUS XhciStartController(PXHCI_CONTEXT Xhci);
NTSTATUS XhciStopController(PXHCI_CONTEXT Xhci);
NTSTATUS XhciBiosHandoff(PXHCI_CONTEXT Xhci);

/* Command ring operations */
NTSTATUS XhciSubmitCommand(PXHCI_CONTEXT Xhci, PXHCI_TRB Trb);
NTSTATUS XhciWaitForCommandComplete(PXHCI_CONTEXT Xhci, PXHCI_TRB CompletionTrb,
                                     ULONG TimeoutMs);

/* Event ring operations */
NTSTATUS XhciPollEventRing(PXHCI_CONTEXT Xhci, PXHCI_TRB EventTrb, ULONG TimeoutMs);
NTSTATUS XhciProcessEvent(PXHCI_CONTEXT Xhci, PXHCI_TRB Event);

/* Port operations */
NTSTATUS XhciPortReset(PXHCI_CONTEXT Xhci, ULONG Port);
UCHAR    XhciGetPortSpeed(PXHCI_CONTEXT Xhci, ULONG Port);
BOOLEAN  XhciPortConnected(PXHCI_CONTEXT Xhci, ULONG Port);

/* Device slot operations */
NTSTATUS XhciEnableSlot(PXHCI_CONTEXT Xhci, PUCHAR OutSlotId);
NTSTATUS XhciDisableSlot(PXHCI_CONTEXT Xhci, UCHAR SlotId);
NTSTATUS XhciAddressDeviceCommand(PXHCI_CONTEXT Xhci, UCHAR SlotId, UCHAR Port,
                                   UCHAR Speed, PUCHAR OutDevAddr);
NTSTATUS XhciSetupEp0Context(PXHCI_CONTEXT Xhci, UCHAR SlotId, UCHAR Speed);

/* TRB building helpers */
VOID XhciBuildSetupTrb(PXHCI_TRB Trb, PVOID SetupData, BOOLEAN IsIn);
VOID XhciBuildDataTrb(PXHCI_TRB Trb, PVOID Data, USHORT Len, BOOLEAN IsIn);
VOID XhciBuildStatusTrb(PXHCI_TRB Trb, BOOLEAN IsIn);
VOID XhciBuildLinkTrb(PXHCI_TRB Trb, PHYSICAL_ADDRESS RingBase, BOOLEAN Toggle);
VOID XhciBuildEnableSlotCmd(PXHCI_TRB Trb);
VOID XhciBuildDisableSlotCmd(PXHCI_TRB Trb, UCHAR SlotId);
VOID XhciBuildAddressDeviceCmd(PXHCI_TRB Trb, PHYSICAL_ADDRESS InputCtx, UCHAR SlotId);

/* Extended capabilities */
NTSTATUS XhciParseExtendedCaps(PXHCI_CONTEXT Xhci);
NTSTATUS XhciUsbLegSupHandoff(PXHCI_CONTEXT Xhci);

/* Debug */
VOID XhciPrintInfo(PXHCI_CONTEXT Xhci);

#endif /* _XHCI_H */

/* Port register access */
ULONG XhciReadPort(PXHCI_CONTEXT Xhci, ULONG Port, ULONG Offset);
VOID XhciWritePort(PXHCI_CONTEXT Xhci, ULONG Port, ULONG Offset, ULONG Value);
