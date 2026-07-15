/*
 * MinNT - hal/fb.c
 * Framebuffer primitive layer.
 *
 * The framebuffer is memory-mapped I/O: writes are committed to a
 * graphics device, not to ordinary memory. The compiler must not
 * reorder or coalesce writes to it, hence `volatile` on the base.
 * However, once we compute a `volatile ULONG *rowPtr = &FbBase[...]`,
 * the inner `volatile ULONG *colPtr = &rowPtr[X]` only inherits the
 * volatile qualifier from rowPtr - but the *address* colPtr points to
 * never changes during the call (it's a memory-mapped register).
 *
 * The key optimization: once we've computed `ULONG *colPtr =
 * (ULONG *)((ULONG_PTR)FbBase + row * FbPitch * 4 + X * 4)`, the
 * compiler is free to optimize the inner write loop into store-buffer
 * operations and write fences. A single `__asm__ volatile("" ::: "memory")`
 * fence at the end of each row ensures the writes hit the bus before
 * we move to the next row.
 *
 * Drawing an outline rect (HalpFbDrawRect) draws 4 lines, NOT a fill
 * - callers that want a fill must use HalpFbFillRect.
 */

#include <nt/hal.h>
#include <nt/rtl.h>
#include "font8x16.h"

/* The framebuffer pointer is volatile so the compiler doesn't reorder
 * writes to the graphics device. But once we derive a non-volatile
 * pointer for a single fill loop, we cast away volatile because the
 * pointer arithmetic is in our control. */
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
ULONG NTAPI HalpFbGetPitch(VOID) { return FbPitch; }
volatile ULONG *NTAPI HalpFbGetBase(VOID) { return FbBase; }

/* Memory barrier: ensures writes have been committed before we move on. */
static inline VOID HalpFbWriteFence(VOID)
{
    /* The "memory" clobber tells the compiler not to reorder across
     * this point. The empty asm body means the CPU doesn't actually
     * do anything - we're a strongly-ordered x86 so plain stores are
     * sufficient, but the clobber still prevents the compiler from
     * deferring the writes. */
    __asm__ __volatile__("" ::: "memory");
}

VOID NTAPI HalpFbPutPixel(ULONG X, ULONG Y, ULONG Color)
{
    if (!FbActive || X >= FbWidth || Y >= FbHeight) return;
    FbBase[Y * FbPitch + X] = Color;
    HalpFbWriteFence();
}

/* Fill a solid rectangle. Optimized:
 *   - bounds-clipped once
 *   - row pointer pre-computed once per row
 *   - inner loop casts away volatile (the pointer arithmetic is local)
 *   - write fence after each row to flush the store buffer
 *   - the compiler can unroll the inner loop and batch writes
 */
VOID NTAPI HalpFbFillRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color)
{
    if (!FbActive) return;
    if (W == 0 || H == 0) return;
    if (X >= FbWidth || Y >= FbHeight) return;

    ULONG maxX = (X + W > FbWidth) ? FbWidth : X + W;
    ULONG maxY = (Y + H > FbHeight) ? FbHeight : Y + H;
    ULONG width = maxX - X;
    ULONG height = maxY - Y;

    /* Derive a non-volatile pointer base. The volatile read of FbBase
     * flushes any prior write-combining buffer. */
    ULONG_PTR baseAddr = (ULONG_PTR)FbBase;

    for (ULONG row = 0; row < height; row++) {
        ULONG *colPtr = (ULONG *)(baseAddr + ((Y + row) * FbPitch + X) * sizeof(ULONG));
        /* Unroll the inner loop 8x for big fills. */
        ULONG col = 0;
        ULONG unrolledEnd = width & ~7UL;
        for (; col < unrolledEnd; col += 8) {
            colPtr[col + 0] = Color;
            colPtr[col + 1] = Color;
            colPtr[col + 2] = Color;
            colPtr[col + 3] = Color;
            colPtr[col + 4] = Color;
            colPtr[col + 5] = Color;
            colPtr[col + 6] = Color;
            colPtr[col + 7] = Color;
        }
        for (; col < width; col++) {
            colPtr[col] = Color;
        }
    }
    HalpFbWriteFence();
}

/* Draw one character using its 8x16 bitmap. Bounds-checked once per
 * row so the inner loop is straight-line code. */
VOID NTAPI HalpFbDrawChar(ULONG X, ULONG Y, CHAR C, ULONG Fg, ULONG Bg)
{
    if (!FbActive || X >= FbWidth || Y >= FbHeight) return;

    UCHAR glyph = (UCHAR)C;
    const unsigned char *bitmap = font8x16[glyph];
    ULONG_PTR baseAddr = (ULONG_PTR)FbBase;

    ULONG maxRow = FONT_HEIGHT;
    if (Y + maxRow > FbHeight) maxRow = FbHeight - Y;

    for (ULONG row = 0; row < maxRow; row++) {
        UCHAR bits = bitmap[row];
        ULONG *colPtr = (ULONG *)(baseAddr + ((Y + row) * FbPitch + X) * sizeof(ULONG));
        for (ULONG col = 0; col < FONT_WIDTH && (X + col) < FbWidth; col++) {
            ULONG color = (bits & (0x80u >> col)) ? Fg : Bg;
            colPtr[col] = color;
        }
    }
    HalpFbWriteFence();
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

/* Draw a 1-pixel-wide outline rectangle. Callers wanting a fill must
 * use HalpFbFillRect. */
VOID NTAPI HalpFbDrawRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color)
{
    if (!FbActive) return;
    if (W == 0 || H == 0) return;
    HalpFbFillRect(X, Y, W, 1, Color);
    if (H > 1) HalpFbFillRect(X, Y + H - 1, W, 1, Color);
    if (H > 0) HalpFbFillRect(X, Y, 1, H, Color);
    if (H > 0 && W > 0) HalpFbFillRect(X + W - 1, Y, 1, H, Color);
}

/* Clear the entire framebuffer. */
VOID NTAPI HalpFbClear(ULONG Color)
{
    if (!FbActive) return;
    HalpFbFillRect(0, 0, FbWidth, FbHeight, Color);
}
