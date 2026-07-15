/*
 * MinNT - hal.h
 * Hardware Abstraction Layer: port I/O, 8259 PIC, 8254 PIT, serial (COM1),
 * VGA text console. Everything hardware-shaped goes through here so the
 * kernel proper never touches a port directly — same contract as hal.dll.
 */

#ifndef _HAL_H_
#define _HAL_H_

#include <nt/ntdef.h>

/* ---- Port I/O ------------------------------------------------------------- */

FORCEINLINE VOID WRITE_PORT_UCHAR(USHORT Port, UCHAR Value)
{
    __asm__ __volatile__("outb %0, %1" :: "a"(Value), "Nd"(Port));
}
FORCEINLINE UCHAR READ_PORT_UCHAR(USHORT Port)
{
    UCHAR v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(Port));
    return v;
}
FORCEINLINE VOID WRITE_PORT_USHORT(USHORT Port, USHORT Value)
{
    __asm__ __volatile__("outw %0, %1" :: "a"(Value), "Nd"(Port));
}
FORCEINLINE USHORT READ_PORT_USHORT(USHORT Port)
{
    USHORT v;
    __asm__ __volatile__("inw %1, %0" : "=a"(v) : "Nd"(Port));
    return v;
}
FORCEINLINE VOID WRITE_PORT_ULONG(USHORT Port, ULONG Value)
{
    __asm__ __volatile__("outl %0, %1" :: "a"(Value), "Nd"(Port));
}
FORCEINLINE ULONG READ_PORT_ULONG(USHORT Port)
{
    ULONG v;
    __asm__ __volatile__("inl %1, %0" : "=a"(v) : "Nd"(Port));
    return v;
}

/* ---- PCI config space access (CF8/CFC method) --------------------------- */

FORCEINLINE ULONG HalPciReadConfig(ULONG Bus, ULONG Device,
                                    ULONG Function, ULONG Offset)
{
    ULONG Address = 0x80000000 |
                    ((Bus & 0xFF) << 16) |
                    ((Device & 0x1F) << 11) |
                    ((Function & 0x07) << 8) |
                    (Offset & 0xFC);
    WRITE_PORT_ULONG(0xCF8, Address);
    return READ_PORT_ULONG(0xCFC);
}
FORCEINLINE VOID HalPciWriteConfig(ULONG Bus, ULONG Device,
                                    ULONG Function, ULONG Offset, ULONG Value)
{
    ULONG Address = 0x80000000 |
                    ((Bus & 0xFF) << 16) |
                    ((Device & 0x1F) << 11) |
                    ((Function & 0x07) << 8) |
                    (Offset & 0xFC);
    WRITE_PORT_ULONG(0xCF8, Address);
    WRITE_PORT_ULONG(0xCFC, Value);
}

/* ---- MMIO access (for devices that use memory-mapped registers) -------- */

FORCEINLINE USHORT READ_REGISTER_USHORT(volatile USHORT *Register)
{
    return *Register;
}
FORCEINLINE VOID WRITE_REGISTER_USHORT(volatile USHORT *Register, USHORT Value)
{
    *Register = Value;
}
FORCEINLINE ULONG READ_REGISTER_ULONG(volatile ULONG *Register)
{
    return *Register;
}
FORCEINLINE VOID WRITE_REGISTER_ULONG(volatile ULONG *Register, ULONG Value)
{
    *Register = Value;
}

/* ---- Init ------------------------------------------------------------------ */

NTSTATUS NTAPI HalInitSystem(VOID);        /* PIC remap + PIT + serial + VGA */

/* ---- 8259 PIC ---------------------------------------------------------------- */

#define PIC_IRQ_BASE 0x20                  /* IRQ0 -> vector 0x20            */
VOID NTAPI HalpInitializePic(VOID);
VOID NTAPI HalEnableSystemInterrupt(UCHAR Irq);
VOID NTAPI HalEndOfInterrupt(UCHAR Irq);

/* ---- 8254 PIT ------------------------------------------------------------------ */

#define HAL_TIMER_HZ 100                   /* 10ms tick, NT-ish              */
VOID NTAPI HalpInitializeClock(VOID);

/* ---- Debug/console output -------------------------------------------------------- */

VOID NTAPI HalDisplayString(const CHAR *String);      /* VGA + COM1          */
VOID NTAPI HalpSerialInit(VOID);
VOID NTAPI HalpSerialPutChar(CHAR C);
VOID NTAPI HalpVgaInit(VOID);
VOID NTAPI HalpVgaPutChar(CHAR C);
VOID NTAPI HalpVgaSetColor(UCHAR Attr);

/* printf-lite: %s %c %d %u %x %p %llx */
VOID DbgPrint(const CHAR *Format, ...);

/* ---- Framebuffer graphics (hal/fb.c) ------------------------------------ */

NTSTATUS NTAPI HalpFbInit(PVOID FramebufferAddr, ULONG Width, ULONG Height, ULONG Pitch, ULONG Bpp);
BOOLEAN NTAPI HalpFbIsActive(VOID);
ULONG NTAPI HalpFbGetWidth(VOID);
ULONG NTAPI HalpFbGetHeight(VOID);
volatile ULONG *NTAPI HalpFbGetBase(VOID);
VOID NTAPI HalpFbPutPixel(ULONG X, ULONG Y, ULONG Color);
VOID NTAPI HalpFbFillRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color);
VOID NTAPI HalpFbDrawChar(ULONG X, ULONG Y, CHAR C, ULONG Fg, ULONG Bg);
VOID NTAPI HalpFbDrawString(ULONG X, ULONG Y, const CHAR *Str, ULONG Fg, ULONG Bg);
VOID NTAPI HalpFbDrawStringCentered(ULONG X, ULONG Y, ULONG W, const CHAR *Str, ULONG Fg, ULONG Bg);
VOID NTAPI HalpFbDrawRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color);
VOID NTAPI HalpFbClear(ULONG Color);

/* ---- PS/2 Keyboard (hal/kbd.c) ------------------------------------------ */

NTSTATUS NTAPI HalpKbdInit(VOID);
CHAR NTAPI HalpKbdGetChar(VOID);
BOOLEAN NTAPI HalpKbdHasKey(VOID);
BOOLEAN NTAPI HalpKbdWasSasDetected(VOID);
VOID NTAPI HalpKbdClearSas(VOID);

/* ---- PS/2 Mouse (hal/mouse.c) -------------------------------------------- */

NTSTATUS NTAPI HalpMouseInit(VOID);
SHORT NTAPI HalpMouseGetX(VOID);
SHORT NTAPI HalpMouseGetY(VOID);
BOOLEAN NTAPI HalpMouseHasEvent(VOID);
BOOLEAN NTAPI HalpMouseGetEvent(PUCHAR Status, PCHAR DeltaX, PCHAR DeltaY);

VOID NTAPI HalpRebootSystem(VOID);

/* ---- Shutdown / Reboot --------------------------------------------------- */

VOID NTAPI HalpShutdownSystem(VOID);
VOID NTAPI HalpRebootSystem(VOID);

/* ---- Multiboot2 framebuffer info (parsed in kiinit.c) ------------------- */

typedef struct _MB2_FRAMEBUFFER_INFO {
    PVOID  Address;
    ULONG  Width;
    ULONG  Height;
    ULONG  Pitch;
    ULONG  Bpp;
    BOOLEAN Valid;
} MB2_FRAMEBUFFER_INFO, *PMB2_FRAMEBUFFER_INFO;

NTSTATUS NTAPI HalpParseMb2Framebuffer(PVOID Mb2Info, PMB2_FRAMEBUFFER_INFO Out);

#endif /* _HAL_H_ */
