/*
 * MinNT - hal/mouse.c
 * PS/2 Auxiliary (mouse) device — IRQ12
 * Three-byte packet protocol: [status][X][Y]
 * X and Y are sign-extended deltas; accumulates into absolute position.
 */

#include <nt/hal.h>
#include <nt/ke.h>

#define MOUSE_DATA_PORT    0x60
#define MOUSE_STATUS_PORT  0x64

#define MOUSE_IRQ 12

typedef struct _MOUSE_PACKET {
    UCHAR Status;
    CHAR DeltaX;
    CHAR DeltaY;
} MOUSE_PACKET;

static MOUSE_PACKET MouseBuffer[32];
static volatile UCHAR MouseHead = 0;
static volatile UCHAR MouseTail = 0;

static SHORT MouseX = 0;
static SHORT MouseY = 0;
static volatile BOOLEAN MouseInitialized = FALSE;

static UCHAR MousePacket[3];
static UCHAR PacketIdx = 0;

static VOID NTAPI KiMouseInterrupt(PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);

    UCHAR status = READ_PORT_UCHAR(MOUSE_STATUS_PORT);

    if ((status & 0x20) == 0)
        return;

    UCHAR data = READ_PORT_UCHAR(MOUSE_DATA_PORT);

    if (!MouseInitialized)
        return;

    if (PacketIdx == 0) {
        if ((data & 0x08) == 0)
            return;
        MousePacket[0] = data;
        PacketIdx = 1;
    } else if (PacketIdx == 1) {
        MousePacket[1] = data;
        PacketIdx = 2;
    } else {
        MousePacket[2] = data;
        PacketIdx = 0;

        UCHAR st = MousePacket[0];
        CHAR dx = MousePacket[1];
        CHAR dy = MousePacket[2];

        if ((st & 0x40) || (st & 0x80))
            return;

        MouseX += (SHORT)dx;
        MouseY -= (SHORT)dy;

        if (MouseX < 0) MouseX = 0;
        if (MouseY < 0) MouseY = 0;

        ULONG maxX = HalpFbIsActive() ? (SHORT)HalpFbGetWidth() - 1 : 1023;
        ULONG maxY = HalpFbIsActive() ? (SHORT)HalpFbGetHeight() - 1 : 767;
        if ((ULONG)MouseX > maxX) MouseX = maxX;
        if ((ULONG)MouseY > maxY) MouseY = maxY;

        UCHAR next = (MouseHead + 1) & 0x1F;
        if (next != MouseTail) {
            MouseBuffer[MouseHead].Status = st;
            MouseBuffer[MouseHead].DeltaX = dx;
            MouseBuffer[MouseHead].DeltaY = dy;
            MouseHead = next;
        }
    }

    HalEndOfInterrupt(MOUSE_IRQ);
}

static VOID MouseWaitInput(VOID)
{
    for (volatile ULONG i = 0; i < 100000; i++) {
        if ((READ_PORT_UCHAR(MOUSE_STATUS_PORT) & 0x02) == 0)
            return;
    }
}

static VOID MouseWaitOutput(VOID)
{
    for (volatile ULONG i = 0; i < 100000; i++) {
        if (READ_PORT_UCHAR(MOUSE_STATUS_PORT) & 0x01)
            return;
    }
}

static VOID MouseWriteCommand(UCHAR cmd)
{
    MouseWaitInput();
    WRITE_PORT_UCHAR(MOUSE_STATUS_PORT, 0xD4);
    MouseWaitInput();
    WRITE_PORT_UCHAR(MOUSE_DATA_PORT, cmd);
}

NTSTATUS NTAPI HalpMouseInit(VOID)
{
    MouseX = 512;
    MouseY = 384;
    PacketIdx = 0;
    MouseHead = MouseTail = 0;

    MouseWaitInput();
    WRITE_PORT_UCHAR(MOUSE_STATUS_PORT, 0xA8);
    MouseWaitInput();

    MouseWriteCommand(0xF3);
    MouseWaitOutput();
    READ_PORT_UCHAR(MOUSE_DATA_PORT);

    MouseWriteCommand(0xC8);
    MouseWaitOutput();
    READ_PORT_UCHAR(MOUSE_DATA_PORT);

    MouseWriteCommand(0xF3);
    MouseWaitOutput();
    READ_PORT_UCHAR(MOUSE_DATA_PORT);

    MouseWriteCommand(0x64);
    MouseWaitOutput();
    READ_PORT_UCHAR(MOUSE_DATA_PORT);

    MouseWriteCommand(0xF3);
    MouseWaitOutput();
    READ_PORT_UCHAR(MOUSE_DATA_PORT);

    MouseWriteCommand(0x03);
    MouseWaitOutput();
    READ_PORT_UCHAR(MOUSE_DATA_PORT);

    MouseWriteCommand(0xF4);
    MouseWaitOutput();
    READ_PORT_UCHAR(MOUSE_DATA_PORT);

    MouseInitialized = TRUE;

    KeConnectInterrupt(PIC_IRQ_BASE + MOUSE_IRQ, KiMouseInterrupt);
    HalEnableSystemInterrupt(MOUSE_IRQ);

    DbgPrint("MOUSE: PS/2 mouse initialized (IRQ12), pos=%d,%d\n", MouseX, MouseY);
    return STATUS_SUCCESS;
}

SHORT NTAPI HalpMouseGetX(VOID)
{
    return MouseX;
}

SHORT NTAPI HalpMouseGetY(VOID)
{
    return MouseY;
}

BOOLEAN NTAPI HalpMouseHasEvent(VOID)
{
    return MouseHead != MouseTail;
}

BOOLEAN NTAPI HalpMouseGetEvent(PUCHAR Status, PCHAR DeltaX, PCHAR DeltaY)
{
    if (MouseHead == MouseTail)
        return FALSE;

    *Status = MouseBuffer[MouseTail].Status;
    *DeltaX = MouseBuffer[MouseTail].DeltaX;
    *DeltaY = MouseBuffer[MouseTail].DeltaY;
    MouseTail = (MouseTail + 1) & 0x1F;
    return TRUE;
}