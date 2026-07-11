#include <nt/hal.h>
#include <nt/rtl.h>
#include "font8x16.h"

static volatile ULONG *FbBase;
static ULONG FbWidth, FbHeight, FbPitch, FbBpp;
static BOOLEAN FbActive;

NTSTATUS NTAPI HalpFbInit(PVOID FramebufferAddr, ULONG Width, ULONG Height, ULONG Pitch, ULONG Bpp)
{
    FbBase = (volatile ULONG *)FramebufferAddr;
    FbWidth = Width;
    FbHeight = Height;
    FbPitch = Pitch / 4;
    FbBpp = Bpp;
    FbActive = TRUE;
    DbgPrint("FB: %ux%u pitch=%u bpp=%u at %p\n", Width, Height, Pitch, Bpp, FramebufferAddr);
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI HalpFbIsActive(VOID) { return FbActive; }
ULONG NTAPI HalpFbGetWidth(VOID) { return FbWidth; }
ULONG NTAPI HalpFbGetHeight(VOID) { return FbHeight; }
volatile ULONG *NTAPI HalpFbGetBase(VOID) { return FbBase; }

VOID NTAPI HalpFbPutPixel(ULONG X, ULONG Y, ULONG Color)
{
    if (!FbActive || X >= FbWidth || Y >= FbHeight) return;
    FbBase[Y * FbPitch + X] = Color;
}

VOID NTAPI HalpFbFillRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color)
{
    if (!FbActive) return;
    for (ULONG row = Y; row < Y + H && row < FbHeight; row++) {
        for (ULONG col = X; col < X + W && col < FbWidth; col++) {
            FbBase[row * FbPitch + col] = Color;
        }
    }
}

VOID NTAPI HalpFbDrawChar(ULONG X, ULONG Y, CHAR C, ULONG Fg, ULONG Bg)
{
    if (!FbActive) return;
    UCHAR glyph = (UCHAR)C;
    const unsigned char *bitmap = font8x16[glyph];
    for (ULONG row = 0; row < FONT_HEIGHT; row++) {
        UCHAR bits = bitmap[row];
        for (ULONG col = 0; col < FONT_WIDTH; col++) {
            ULONG color = (bits & (0x80 >> col)) ? Fg : Bg;
            HalpFbPutPixel(X + col, Y + row, color);
        }
    }
}

VOID NTAPI HalpFbDrawString(ULONG X, ULONG Y, const CHAR *Str, ULONG Fg, ULONG Bg)
{
    if (!FbActive) return;
    ULONG cx = X;
    while (*Str) {
        if (*Str == '\n') {
            Y += FONT_HEIGHT;
            cx = X;
            Str++;
            continue;
        }
        HalpFbDrawChar(cx, Y, *Str, Fg, Bg);
        cx += FONT_WIDTH;
        Str++;
    }
}

VOID NTAPI HalpFbDrawStringCentered(ULONG X, ULONG Y, ULONG W, const CHAR *Str, ULONG Fg, ULONG Bg)
{
    if (!FbActive) return;
    ULONG len = 0;
    const CHAR *p = Str;
    while (*p) { len++; p++; }
    ULONG startX = X + (W - len * FONT_WIDTH) / 2;
    HalpFbDrawString(startX, Y, Str, Fg, Bg);
}

VOID NTAPI HalpFbDrawRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color)
{
    if (!FbActive) return;
    HalpFbFillRect(X, Y, W, 1, Color);
    HalpFbFillRect(X, Y + H - 1, W, 1, Color);
    HalpFbFillRect(X, Y, 1, H, Color);
    HalpFbFillRect(X + W - 1, Y, 1, H, Color);
}

VOID NTAPI HalpFbClear(ULONG Color)
{
    if (!FbActive) return;
    HalpFbFillRect(0, 0, FbWidth, FbHeight, Color);
}
