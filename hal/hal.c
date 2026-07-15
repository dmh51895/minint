/*
 * MinNT - hal/hal.c
 * COM1 serial + VGA 80x25 text console + framebuffer console + printf-lite DbgPrint,
 * 8259 PIC remap, 8254 PIT at 100Hz.
 */

#include <nt/hal.h>
#include <nt/rtl.h>

/* VGA cursor position tracking */
static ULONG HalpVgaCursorX = 0;
static ULONG HalpVgaCursorY = 0;

VOID NTAPI HalpVgaSetCursor(ULONG X, ULONG Y)
{
    if (X >= 80) X = 79;
    if (Y >= 25) Y = 24;
    
    HalpVgaCursorX = X;
    HalpVgaCursorY = Y;
    
    /* Calculate cursor position (row * 80 + col) */
    USHORT cursorPos = (USHORT)(Y * 80 + X);
    
    /* Set cursor position - VGA CRT Controller registers */
    WRITE_PORT_UCHAR(0x3D4, 0x0F); /* Low cursor position register */
    WRITE_PORT_UCHAR(0x3D5, (UCHAR)(cursorPos & 0xFF));
    WRITE_PORT_UCHAR(0x3D4, 0x0E); /* High cursor position register */
    WRITE_PORT_UCHAR(0x3D5, (UCHAR)((cursorPos >> 8) & 0xFF));
}

VOID NTAPI HalpVgaInit(VOID)
{
    /* Initialize VGA text mode - ensure we can output text even without framebuffer */
    HalpVgaSetColor(0x07); /* Light gray on black */
    DbgPrint("VGA: Initializing VGA text mode fallback\n");
    
    /* Clear screen */
    for (int i = 0; i < 80 * 25; i++) {
        ((volatile USHORT*)0xB8000)[i] = (0x07 << 8) | ' ';
    }
    
    /* Set cursor to top-left */
    HalpVgaSetCursor(0, 0);
    
    /* Test output */
    HalpVgaPutChar('V');
    HalpVgaPutChar('G');
    HalpVgaPutChar('A');
    HalpVgaPutChar(' ');
    HalpVgaPutChar('O');
    HalpVgaPutChar('K');
    
    DbgPrint("VGA: VGA text mode initialized successfully\n");
}
#include <stdarg.h>

/* Forward declarations for framebuffer functions in fb.c */
extern NTSTATUS NTAPI HalpFbInit(PVOID FramebufferAddr, ULONG Width, ULONG Height, ULONG Pitch, ULONG Bpp);
extern BOOLEAN NTAPI HalpFbIsActive(VOID);
extern VOID NTAPI HalpFbPutPixel(ULONG X, ULONG Y, ULONG Color);
extern VOID NTAPI HalpFbFillRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color);
extern VOID NTAPI HalpFbDrawChar(ULONG X, ULONG Y, CHAR C, ULONG Fg, ULONG Bg);
extern VOID NTAPI HalpFbClear(ULONG Color);

/* ---- COM1 ----------------------------------------------------------------- */

#define COM1 0x3F8

VOID NTAPI HalpSerialInit(VOID)
{
    WRITE_PORT_UCHAR(COM1 + 1, 0x00);   /* IRQs off             */
    WRITE_PORT_UCHAR(COM1 + 3, 0x80);   /* DLAB on              */
    WRITE_PORT_UCHAR(COM1 + 0, 0x01);   /* 115200 baud          */
    WRITE_PORT_UCHAR(COM1 + 1, 0x00);
    WRITE_PORT_UCHAR(COM1 + 3, 0x03);   /* 8N1                  */
    WRITE_PORT_UCHAR(COM1 + 2, 0xC7);   /* FIFO on, clear       */
    WRITE_PORT_UCHAR(COM1 + 4, 0x0B);   /* RTS/DSR              */
}

VOID NTAPI HalpSerialPutChar(CHAR C)
{
    /* Bounded wait for COM1 transmit-ready. If the port isn't
     * responding (e.g. minimal boot environment without proper
     * init), bail out after ~10000 iterations so the kernel can
     * continue rather than hanging forever. */
    int wait = 10000;
    while (wait-- > 0) {
        if (READ_PORT_UCHAR(COM1 + 5) & 0x20) break;
    }
    WRITE_PORT_UCHAR(COM1, (UCHAR)C);
}

/* ---- VGA text -------------------------------------------------------------- */

#define VGA_BASE  ((volatile USHORT *)0xB8000)
#define VGA_COLS  80
#define VGA_ROWS  25

static ULONG HalpVgaRow, HalpVgaCol;
static UCHAR HalpVgaAttr = 0x07;

VOID NTAPI HalpVgaSetColor(UCHAR Attr) { HalpVgaAttr = Attr; }

static VOID HalpVgaScroll(VOID)
{
    ULONG i;
    for (i = 0; i < (VGA_ROWS - 1) * VGA_COLS; i++)
        VGA_BASE[i] = VGA_BASE[i + VGA_COLS];
    for (i = (VGA_ROWS - 1) * VGA_COLS; i < VGA_ROWS * VGA_COLS; i++)
        VGA_BASE[i] = (USHORT)((HalpVgaAttr << 8) | ' ');
    HalpVgaRow = VGA_ROWS - 1;
}

VOID NTAPI HalpVgaPutChar(CHAR C)
{
    if (C == '\n') {
        HalpVgaCol = 0;
        if (++HalpVgaRow >= VGA_ROWS) HalpVgaScroll();
        return;
    }
    if (C == '\r') { HalpVgaCol = 0; return; }
    VGA_BASE[HalpVgaRow * VGA_COLS + HalpVgaCol] =
        (USHORT)((HalpVgaAttr << 8) | (UCHAR)C);
    if (++HalpVgaCol >= VGA_COLS) {
        HalpVgaCol = 0;
        if (++HalpVgaRow >= VGA_ROWS) HalpVgaScroll();
    }
}

/* ---- Framebuffer console --------------------------------------------------- */

#define FB_FONT_WIDTH 8
#define FB_FONT_HEIGHT 16
#define FB_CONSOLE_FG  0x00FFFFFF   /* White */
#define FB_CONSOLE_BG  0x00000000   /* Black */

static ULONG HalpFbCursorX = 0;
static ULONG HalpFbCursorY = 0;
static ULONG HalpFbCols = 0;
static ULONG HalpFbRows = 0;
static BOOLEAN HalpFbConsoleReady = FALSE;

/* Called by kiinit.c after HalpFbInit to set up console dimensions */
VOID NTAPI HalpFbConsoleInit(ULONG Width, ULONG Height)
{
    HalpFbCols = Width / FB_FONT_WIDTH;
    HalpFbRows = Height / FB_FONT_HEIGHT;
    HalpFbCursorX = 0;
    HalpFbCursorY = 0;
    HalpFbConsoleReady = TRUE;
    
    /* Clear screen to black */
    HalpFbClear(FB_CONSOLE_BG);
    
    /* Draw welcome banner */
    DbgPrint("FB: Console %ux%u chars on %ux%u screen\n",
             HalpFbCols, HalpFbRows, Width, Height);
}

static VOID HalpFbScroll(VOID)
{
    if (!HalpFbConsoleReady) return;
    
    extern ULONG NTAPI HalpFbGetWidth(VOID);
    extern ULONG NTAPI HalpFbGetHeight(VOID);
    extern ULONG NTAPI HalpFbGetPitch(VOID);
    extern volatile ULONG *NTAPI HalpFbGetBase(VOID);
    
    ULONG FbWidth = HalpFbGetWidth();
    ULONG FbHeight = HalpFbGetHeight();
    ULONG FbPitch = HalpFbGetPitch();
    volatile ULONG *Fb = HalpFbGetBase();
    
    /* Copy pixel rows up by one character height */
    for (ULONG y = 0; y < FbHeight - FB_FONT_HEIGHT; y++) {
        for (ULONG x = 0; x < FbWidth; x++) {
            Fb[y * FbPitch + x] = Fb[(y + FB_FONT_HEIGHT) * FbPitch + x];
        }
    }
    
    /* Clear bottom line */
    HalpFbFillRect(0, FbHeight - FB_FONT_HEIGHT, FbWidth, FB_FONT_HEIGHT, FB_CONSOLE_BG);
}

static VOID HalpFbConsolePutChar(CHAR C)
{
    if (!HalpFbConsoleReady || !HalpFbIsActive()) return;
    
    if (C == '\n') {
        HalpFbCursorX = 0;
        HalpFbCursorY++;
        if (HalpFbCursorY >= HalpFbRows) {
            HalpFbCursorY = HalpFbRows - 1;
            HalpFbScroll();
        }
        return;
    }
    if (C == '\r') {
        HalpFbCursorX = 0;
        return;
    }
    if (C == '\t') {
        HalpFbCursorX = (HalpFbCursorX + 4) & ~3;
        if (HalpFbCursorX >= HalpFbCols) {
            HalpFbCursorX = 0;
            HalpFbCursorY++;
            if (HalpFbCursorY >= HalpFbRows) {
                HalpFbCursorY = HalpFbRows - 1;
                HalpFbScroll();
            }
        }
        return;
    }
    
    /* Draw character at cursor position */
    ULONG PixX = HalpFbCursorX * FB_FONT_WIDTH;
    ULONG PixY = HalpFbCursorY * FB_FONT_HEIGHT;
    HalpFbDrawChar(PixX, PixY, C, FB_CONSOLE_FG, FB_CONSOLE_BG);
    
    HalpFbCursorX++;
    if (HalpFbCursorX >= HalpFbCols) {
        HalpFbCursorX = 0;
        HalpFbCursorY++;
        if (HalpFbCursorY >= HalpFbRows) {
            HalpFbCursorY = HalpFbRows - 1;
            HalpFbScroll();
        }
    }
}

static VOID HalpPutChar(CHAR C)
{
    if (C == '\n') HalpSerialPutChar('\r');
    HalpSerialPutChar(C);
    HalpVgaPutChar(C);
    HalpFbConsolePutChar(C);
}

VOID NTAPI HalDisplayString(const CHAR *String)
{
    while (*String) HalpPutChar(*String++);
}

/* ---- DbgPrint: %s %c %d %u %x %p %ws %wZ %lx %llx ---------------------------- */

static VOID HalpPrintUnsigned(ULONG64 v, ULONG base, ULONG width)
{
    static const CHAR digits[] = "0123456789abcdef";
    CHAR buf[24];
    ULONG i = 0;
    if (v == 0) buf[i++] = '0';
    while (v) { buf[i++] = digits[v % base]; v /= base; }
    while (i < width) buf[i++] = '0';
    while (i--) HalpPutChar(buf[i]);
}

static VOID HalpPutHex(ULONG v, ULONG width)
{
    HalpPrintUnsigned((ULONG64)v, 16, width);
}

static VOID HalpPutDecSigned(LONG v)
{
    if (v < 0) { HalpPutChar('-'); v = -v; }
    HalpPrintUnsigned((ULONG64)v, 10, 0);
}

static VOID HalpPrintWideString(const uint16_t *ws)
{
    if (!ws) { HalDisplayString("(null)"); return; }
    while (*ws) HalpPutChar((CHAR)*ws++);
}

static VOID HalpPrintUnicodeString(const uint16_t *Buffer, USHORT Length)
{
    if (!Buffer) return;
    USHORT i;
    for (i = 0; i < Length / sizeof(uint16_t) && *Buffer; i++, Buffer++)
        HalpPutChar((CHAR)*Buffer);
}

VOID DbgPrint(const CHAR *Format, ...)
{
    va_list ap;
    va_start(ap, Format);
    for (; *Format; Format++) {
        if (*Format != '%') { HalpPutChar(*Format); continue; }
        Format++;

        ULONG Width = 0;
        while (*Format >= '0' && *Format <= '9')
            Width = Width * 10 + (ULONG)(*Format++ - '0');

reswitch:
        switch (*Format) {

        case 's': {
            const CHAR *s = va_arg(ap, const CHAR *);
            HalDisplayString(s ? s : "(null)");
            break;
        }
        case 'c':
            HalpPutChar((CHAR)va_arg(ap, int));
            break;
        case 'd': {
            LONG v = va_arg(ap, LONG);
            HalpPutDecSigned((LONG)v);
            break;
        }
        case 'u': {
            ULONG v = va_arg(ap, ULONG);
            HalpPrintUnsigned((ULONG64)v, 10, Width);
            break;
        }
        case 'x': {
            ULONG v = va_arg(ap, ULONG);
            HalpPrintUnsigned((ULONG64)v, 16, Width);
            break;
        }
        case 'p':
            HalDisplayString("0x");
            HalpPrintUnsigned((ULONG64)va_arg(ap, void *), 16, 16);
            break;
        case 'l':
            Format++;
            if (*Format == 'l') {
                Format++;
                if (*Format == 'x')
                    HalpPrintUnsigned(va_arg(ap, ULONG64), 16, Width);
                else if (*Format == 'd') {
                    LONG64 v = va_arg(ap, LONG64);
                    if (v < 0) { HalpPutChar('-'); v = -v; }
                    HalpPrintUnsigned((ULONG64)v, 10, Width);
                } else if (*Format == 'u')
                    HalpPrintUnsigned(va_arg(ap, ULONG64), 10, Width);
                else
                    goto reswitch;
            } else if (*Format == 'x')
                HalpPrintUnsigned(va_arg(ap, ULONG), 16, Width);
            else if (*Format == 'd') {
                LONG v = va_arg(ap, LONG);
                HalpPutDecSigned(v);
            } else if (*Format == 'u') {
                ULONG v = va_arg(ap, ULONG);
                HalpPrintUnsigned((ULONG64)v, 10, Width);
            } else if (*Format == 's') {
                /* %ls — wide string, same as %ws */
                const uint16_t *ws = va_arg(ap, const uint16_t *);
                HalpPrintWideString(ws);
            } else {
                Format--;
                goto reswitch;
            }
            break;
        case 'w':
            Format++;
            if (*Format == 's') {
                const uint16_t *ws = va_arg(ap, uint16_t *);
                HalpPrintWideString(ws);
            } else if (*Format == 'Z') {
                PUNICODE_STRING us = va_arg(ap, PUNICODE_STRING);
                if (us && us->Buffer)
                    HalpPrintUnicodeString(us->Buffer, us->Length);
            } else {
                HalpPutChar('%');
                HalpPutChar('w');
                Format--;
            }
            break;
        case '%':
            HalpPutChar('%');
            break;
        default:
            HalpPutChar('%');
            HalpPutChar(*Format);
            break;
        }
    }
    va_end(ap);
}

/* ---- 8259 PIC -------------------------------------------------------------------- */

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

VOID NTAPI HalpInitializePic(VOID)
{
    /* Remap IRQ0-15 to vectors 0x20-0x2F, mask everything */
    WRITE_PORT_UCHAR(PIC1_CMD,  0x11);
    WRITE_PORT_UCHAR(PIC2_CMD,  0x11);
    WRITE_PORT_UCHAR(PIC1_DATA, PIC_IRQ_BASE);
    WRITE_PORT_UCHAR(PIC2_DATA, PIC_IRQ_BASE + 8);
    WRITE_PORT_UCHAR(PIC1_DATA, 0x04);
    WRITE_PORT_UCHAR(PIC2_DATA, 0x02);
    WRITE_PORT_UCHAR(PIC1_DATA, 0x01);
    WRITE_PORT_UCHAR(PIC2_DATA, 0x01);
    WRITE_PORT_UCHAR(PIC1_DATA, 0xFF);
    WRITE_PORT_UCHAR(PIC2_DATA, 0xFF);
}

VOID NTAPI HalEnableSystemInterrupt(UCHAR Irq)
{
    USHORT port = (Irq < 8) ? PIC1_DATA : PIC2_DATA;
    UCHAR  bit  = (UCHAR)(Irq & 7);
    UCHAR  mask = READ_PORT_UCHAR(port);
    WRITE_PORT_UCHAR(port, (UCHAR)(mask & ~(1 << bit)));
    if (Irq >= 8) {                           /* cascade */
        mask = READ_PORT_UCHAR(PIC1_DATA);
        WRITE_PORT_UCHAR(PIC1_DATA, (UCHAR)(mask & ~(1 << 2)));
    }
}

VOID NTAPI HalEndOfInterrupt(UCHAR Irq)
{
    if (Irq >= 8) WRITE_PORT_UCHAR(PIC2_CMD, 0x20);
    WRITE_PORT_UCHAR(PIC1_CMD, 0x20);
}

/* ---- 8254 PIT ---------------------------------------------------------------------- */

VOID NTAPI HalpInitializeClock(VOID)
{
    ULONG divisor = 1193182 / HAL_TIMER_HZ;
    WRITE_PORT_UCHAR(0x43, 0x36);                       /* ch0, rate gen */
    WRITE_PORT_UCHAR(0x40, (UCHAR)(divisor & 0xFF));
    WRITE_PORT_UCHAR(0x40, (UCHAR)(divisor >> 8));
}

/* ---- HalInitSystem -------------------------------------------------------------------- */

NTSTATUS NTAPI HalInitSystem(VOID)
{
    HalpSerialInit();
    HalpVgaInit();
    HalpInitializePic();
    HalpInitializeClock();
    return STATUS_SUCCESS;
}

/* ---- Shutdown / Reboot --------------------------------------------------- */

VOID NTAPI HalpShutdownSystem(VOID)
{
    /* Try ACPI shutdown via port 0x605 (QEMU accepts this) */
    WRITE_PORT_UCHAR(0x605, 0x2000);
    /* Fallback: disable interrupts and halt */
    __asm__ __volatile__("cli; hlt");
    for (;;);
}

VOID NTAPI HalpRebootSystem(VOID)
{
    /* Keyboard controller reset command */
    for (int i = 0; i < 0x10000; i++) {
        if (READ_PORT_UCHAR(0x64) & 1) {
            UCHAR out = READ_PORT_UCHAR(0x60);
            if (out == 0xFA) continue;
        }
        if (!(READ_PORT_UCHAR(0x64) & 0x2)) break;
    }
    WRITE_PORT_UCHAR(0x64, 0xFE);
    /* Fallback: triple fault */
    __asm__ __volatile__("lgdt (%0); hlt" :: "r"((ULONG64)0));
    for (;;);
}
