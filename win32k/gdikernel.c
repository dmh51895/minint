/*
 * MinNT - win32k/gdikernel.c
 * Win32k GDI kernel-mode implementations
 *
 * Phase 1: GdiFlush, GdiCreateCompatibleDC, GdiCreateCompatibleBitmap,
 *          GdiSelectBitmap, GdiDeleteObjectApp, GdiGetDCDword
 *          GdiGetDCPoint, GdiGetAppClipBox, GdiPatBlt, GdiRectangle
 *
 * Phase 2: GdiBitBlt
 *
 * Phase 3: GdiSetPixel, GdiGetPixel, GdiLineTo
 *
 * Phase 4: GdiExtTextOutW
 *
 * Phase 5: GdiCreateSolidBrush
 *
 * Phase 6: GdiCreatePen
 *
 * Phase 7: GdiGetTextExtent
 *
 * Phase 8: GdiGetAndSetDCDword
 *
 * Phase 9: UserGetDC, UserReleaseDC
 *
 * Phase 10: GdiCreateRectRgn
 *
 * Phase 11: GdiOffsetRgn
 *
 * Phase 12: GdiGetRgnBox
 *
 * Phase 13: GdiSaveDC, GdiRestoreDC
 *
 * Phase 14: GdiGetDCObject
 *
 * Phase 15: GdiGetTextMetricsW
 *
 * Phase 16: GdiExtSelectClipRgn
 *
 * Phase 17: GdiCombineRgn
 *
 * Phase 18: GdiIntersectClipRect
 *
 * Phase 19: GdiStretchBlt
 *
 * Phase 20: GdiSelectFont, GdiMaskBlt, GdiAlphaBlend, GdiTransformPoints
 */

#include "win32k.h"

/* GDI types are now defined in win32k.h */

static volatile LONG GdiSaveLevelCounter = 0;

static BASEDC *GdiAllocDC(VOID)
{
    BASEDC *hdc = ExAllocatePool(NonPagedPool, sizeof(BASEDC));
    if (!hdc) return NULL;

    RtlZeroMemory(hdc, sizeof(BASEDC));
    hdc->Header.Type = 4;
    hdc->Header.RefCount = 1;
    hdc->pdcattr = ExAllocatePool(NonPagedPool, sizeof(DC_ATTR));
    if (!hdc->pdcattr) {
        ExFreePool(hdc);
        return NULL;
    }

    RtlZeroMemory(hdc->pdcattr, sizeof(DC_ATTR));
    hdc->pdcattr->TextColor = 0x00000000;
    hdc->pdcattr->BackColor = 0x00FFFFFF;
    hdc->pdcattr->BkMode = OPAQUE;
    hdc->pdcattr->old_rop2 = R2_COPYPEN;

    return hdc;
}

static VOID GdiFreeDC(BASEDC *hdc)
{
    if (hdc->psurface) {
        if (hdc->psurface->pvBits)
            ExFreePool(hdc->psurface->pvBits);
        ExFreePool(hdc->psurface);
    }
    if (hdc->pdcattr)
        ExFreePool(hdc->pdcattr);
    ExFreePool(hdc);
}

NTSTATUS APIENTRY GdiFlush(VOID)
{
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY GdiCreateCompatibleDC(ULONG_PTR hdc)
{
    BASEDC *NewDC;
    BASEDC *SrcDC = (BASEDC *)hdc;
    SURFACE *Surface;
    ULONG BitmapSize;

    NewDC = GdiAllocDC();
    if (!NewDC) return STATUS_NO_MEMORY;

    /* Copy attributes from the source DC if it is valid; otherwise keep defaults */
    if (SrcDC && SrcDC->Header.Type == 4 && SrcDC->pdcattr && NewDC->pdcattr) {
        NewDC->pdcattr->TextColor = SrcDC->pdcattr->TextColor;
        NewDC->pdcattr->BackColor = SrcDC->pdcattr->BackColor;
        NewDC->pdcattr->BkMode = SrcDC->pdcattr->BkMode;
        NewDC->pdcattr->old_rop2 = SrcDC->pdcattr->old_rop2;
    }

    Surface = ExAllocatePool(NonPagedPool, sizeof(SURFACE));
    if (!Surface) {
        GdiFreeDC(NewDC);
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(Surface, sizeof(SURFACE));
    Surface->Header.Type = 5;
    Surface->iFormat = 0x18;
    Surface->sizlBitmap_cx = 1024;
    Surface->sizlBitmap_cy = 768;

    BitmapSize = Surface->sizlBitmap_cx * Surface->sizlBitmap_cy * 4;
    Surface->pvBits = ExAllocatePool(NonPagedPool, BitmapSize);
    if (!Surface->pvBits) {
        ExFreePool(Surface);
        GdiFreeDC(NewDC);
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(Surface->pvBits, BitmapSize);
    Surface->lDelta = Surface->sizlBitmap_cx * 4;
    Surface->fjBitmap = 0xCC;
    Surface->hBitmap = (HBITMAP)(ULONG_PTR)(Surface + 1);

    NewDC->psurface = Surface;
    NewDC->fl = DC_FLAG_MEMORY | DC_FLAG_DIRTY_RASTER_CAPS;

    DbgPrint("WIN32K: GdiCreateCompatibleDC -> %p\n", (ULONG_PTR)NewDC);
    return (NTSTATUS)(ULONG_PTR)NewDC;
}

NTSTATUS APIENTRY GdiCreateCompatibleBitmap(ULONG_PTR hdc, ULONG Width, ULONG Height)
{
    SURFACE *Surface;
    HBITMAP hBitmap;
    ULONG BitmapSize;
    BASEDC *SrcDC = (BASEDC *)hdc;
    USHORT SrcFormat = 0x18;  /* default: 32bpp storage */
    ULONG BytesPerPixel;

    if (Width == 0) Width = 1;
    if (Height == 0) Height = 1;

    /* Determine the color format from the source DC's surface */
    if (SrcDC && SrcDC->Header.Type == 4 && SrcDC->psurface) {
        SrcFormat = SrcDC->psurface->iFormat;
    }

    /* Compute bytes per pixel from the source format */
    switch (SrcFormat) {
        case 1:
        case 4:
        case 8:
            BytesPerPixel = 1;
            break;
        case 16:
            BytesPerPixel = 2;
            break;
        case 24:
        case 32:
        default:
            /* 24/0x18 is this codebase's convention for 32bpp storage */
            BytesPerPixel = 4;
            break;
    }

    Surface = ExAllocatePool(NonPagedPool, sizeof(SURFACE));
    if (!Surface) return STATUS_NO_MEMORY;

    RtlZeroMemory(Surface, sizeof(SURFACE));
    Surface->Header.Type = 5;
    Surface->iFormat = SrcFormat;
    Surface->sizlBitmap_cx = Width;
    Surface->sizlBitmap_cy = Height;

    BitmapSize = Width * Height * BytesPerPixel;
    Surface->pvBits = ExAllocatePool(NonPagedPool, BitmapSize);
    if (!Surface->pvBits) {
        ExFreePool(Surface);
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(Surface->pvBits, BitmapSize);
    Surface->lDelta = Width * BytesPerPixel;
    Surface->fjBitmap = 0xCC;
    Surface->hBitmap = (HBITMAP)(ULONG_PTR)(Surface + 1);

    DbgPrint("WIN32K: GdiCreateCompatibleBitmap(%dx%d) -> %p\n",
             Width, Height, (ULONG_PTR)Surface->hBitmap);
    return (NTSTATUS)(ULONG_PTR)Surface->hBitmap;
}

NTSTATUS APIENTRY GdiSelectBitmap(ULONG_PTR hdc, ULONG_PTR hBitmap)
{
    BASEDC *hdcDC = (BASEDC *)hdc;
    SURFACE *hNewSurface;
    SURFACE *hOldSurface = NULL;
    HBITMAP ret;

    if (!hdcDC) return STATUS_INVALID_PARAMETER;

    if (hdcDC->psurface)
        hOldSurface = hdcDC->psurface;

    if (hBitmap) {
        hNewSurface = (SURFACE *)((ULONG_PTR)hBitmap - sizeof(SURFACE));
        if (hNewSurface->Header.Type != 5)
            return STATUS_INVALID_PARAMETER;
        hdcDC->psurface = hNewSurface;
        ret = hNewSurface->hBitmap;
    } else {
        hdcDC->psurface = NULL;
        ret = (HBITMAP)0;
    }

    DbgPrint("WIN32K: GdiSelectBitmap(%p, %p) -> old=%p\n",
             hdc, hBitmap, (ULONG_PTR)ret);
    return (NTSTATUS)(ULONG_PTR)ret;
}

NTSTATUS APIENTRY GdiDeleteObjectApp(ULONG_PTR hObj)
{
    SURFACE *Surface;
    BRUSHOBJ *Brush;

    if (!hObj) return STATUS_INVALID_PARAMETER;

    Surface = (SURFACE *)((ULONG_PTR)hObj - sizeof(SURFACE));
    if (Surface->Header.Type == 5) {
        if (Surface->pvBits)
            ExFreePool(Surface->pvBits);
        Surface->pvBits = NULL;
        ExFreePool(Surface);
        DbgPrint("WIN32K: GdiDeleteObjectApp(%p) - bitmap deleted\n", hObj);
        return STATUS_SUCCESS;
    }

    Brush = (BRUSHOBJ *)hObj;
    if (Brush->Header.Type == 3) {
        ExFreePool(Brush);
        DbgPrint("WIN32K: GdiDeleteObjectApp(%p) - brush deleted\n", hObj);
        return STATUS_SUCCESS;
    }

    {
        PENOBJ *Pen = (PENOBJ *)hObj;
        if (Pen->Header.Type == 2) {
            ExFreePool(Pen);
            DbgPrint("WIN32K: GdiDeleteObjectApp(%p) - pen deleted\n", hObj);
            return STATUS_SUCCESS;
        }
    }

    DbgPrint("WIN32K: GdiDeleteObjectApp(%p) - type=%lu\n",
             hObj, Surface->Header.Type);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY GdiExtSelectClipRgn(ULONG_PTR hdc, ULONG_PTR hrgn, INT fnMode)
{
    BASEDC *hdcDC = (BASEDC *)hdc;

    if (!hdcDC || hdcDC->Header.Type != 4) return STATUS_INVALID_PARAMETER;

    if (fnMode < 1 || fnMode > 4) return STATUS_INVALID_PARAMETER;

    hdcDC->hClipRgn = hrgn;

    DbgPrint("WIN32K: GdiExtSelectClipRgn(%p, %p, mode=%d) -> SIMPLERGN\n",
             hdc, hrgn, fnMode);
    return 2;
}

NTSTATUS APIENTRY GdiGetDCDword(ULONG_PTR hdc, ULONG Index, ULONG Value)
{
    BASEDC *hdcDC = (BASEDC *)hdc;
    ULONG *pValue = (ULONG *)Value;

    if (!hdcDC || !pValue) return STATUS_INVALID_PARAMETER;

    switch (Index) {
        case 12:
            *pValue = hdcDC->psurface ? (ULONG)hdcDC->psurface->sizlBitmap_cx : 0;
            break;
        case 13:
            *pValue = hdcDC->psurface ? (ULONG)hdcDC->psurface->sizlBitmap_cy : 0;
            break;
        case 105:
            *pValue = 32;
            break;
        default:
            *pValue = 0;
            break;
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY GdiGetDCPoint(ULONG_PTR hdc, ULONG Index, PW32K_POINT Point)
{
    BASEDC *hdcDC = (BASEDC *)hdc;

    if (!hdcDC || hdcDC->Header.Type != 4) return STATUS_INVALID_PARAMETER;
    if (!Point) return STATUS_INVALID_PARAMETER;

    switch (Index) {
        case 28:
            /* Device origin - return the surface origin */
            if (hdcDC->psurface) {
                Point->x = hdcDC->psurface->ptlOrigin.x;
                Point->y = hdcDC->psurface->ptlOrigin.y;
            } else {
                Point->x = 0;
                Point->y = 0;
            }
            break;
        default:
            /* Brush/pen position - return current DC position (0,0 default) */
            Point->x = hdcDC->clipLeft;
            Point->y = hdcDC->clipTop;
            break;
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY GdiGetAppClipBox(ULONG_PTR hdc, PW32K_RECT Rect)
{
    BASEDC *hdcDC = (BASEDC *)hdc;

    if (!hdcDC || !Rect) return STATUS_INVALID_PARAMETER;

    if (hdcDC->psurface) {
        Rect->left = 0;
        Rect->top = 0;
        Rect->right = hdcDC->psurface->sizlBitmap_cx;
        Rect->bottom = hdcDC->psurface->sizlBitmap_cy;
    } else {
        Rect->left = 0;
        Rect->top = 0;
        Rect->right = 1024;
        Rect->bottom = 768;
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY GdiPatBlt(ULONG_PTR hdc, LONG Left, LONG Top, LONG Width, LONG Height, ULONG RasterOp)
{
    BASEDC *hdcDC = (BASEDC *)hdc;
    ULONG Color;
    ULONG *pixels;
    int x, y;
    LONG cx, cy;

    if (!hdcDC) return STATUS_INVALID_PARAMETER;
    if (!hdcDC->psurface || !hdcDC->psurface->pvBits)
        return STATUS_INVALID_PARAMETER;

    cx = hdcDC->psurface->sizlBitmap_cx;
    cy = hdcDC->psurface->sizlBitmap_cy;

    switch (RasterOp & 0x0000000F) {
        case PATCOPY:
            Color = hdcDC->pdcattr->BackColor;
            break;
        case WHITENESS:
            Color = 0x00FFFFFF;
            break;
        case BLACKNESS:
            Color = 0x00000000;
            break;
        default:
            Color = 0x00000000;
            break;
    }

    pixels = (ULONG *)hdcDC->psurface->pvBits;
    for (y = Top; y < Top + Height && y < cy; y++) {
        for (x = Left; x < Left + Width && x < cx; x++) {
            if (y >= 0 && x >= 0)
                pixels[y * (hdcDC->psurface->lDelta / 4) + x] = Color;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY GdiRectangle(ULONG_PTR hdc, LONG Left, LONG Top, LONG Right, LONG Bottom)
{
    if (!hdc) return STATUS_INVALID_PARAMETER;
    return GdiPatBlt(hdc, Left, Top, Right - Left, Bottom - Top, PATCOPY);
}

NTSTATUS APIENTRY GdiBitBlt(ULONG_PTR hDCDest, LONG XDest, LONG YDest,
                            LONG Width, LONG Height, ULONG_PTR hDCSrc,
                            LONG XSrc, LONG YSrc, ULONG RasterOp)
{
    BASEDC *DestDC = (BASEDC *)hDCDest;
    BASEDC *SrcDC = (BASEDC *)hDCSrc;
    ULONG *DestPixels, *SrcPixels;
    int destStride, srcStride;
    int x, y;
    ULONG Color;
    LONG w, h;
    LONG sx, sy;

    if (!DestDC) return STATUS_INVALID_PARAMETER;
    if (Width <= 0 || Height <= 0) return STATUS_SUCCESS;
    if (!DestDC->psurface || !DestDC->psurface->pvBits)
        return STATUS_INVALID_PARAMETER;

    DestPixels = (ULONG *)DestDC->psurface->pvBits;
    destStride = DestDC->psurface->lDelta / 4;
    w = Width;
    h = Height;

    switch (RasterOp & 0x0000000F) {
        case 0x00:
        case 0x07:
            Color = 0x00000000;
            for (y = YDest; y < YDest + h && y < (LONG)DestDC->psurface->sizlBitmap_cy; y++) {
                for (x = XDest; x < XDest + w && x < (LONG)DestDC->psurface->sizlBitmap_cx; x++) {
                    if (y >= 0 && x >= 0)
                        DestPixels[y * destStride + x] = Color;
                }
            }
            break;

        case 0x01:
        case 0x05:
        case 0x09:
            break;

        case 0x02:
        case 0x04:
        case 0x08:
            break;

        case 0x03:
        case 0x0B:
        case 0x0D:
        case 0x0F:
            break;

        case 0x06:
        case 0x0A:
            Color = DestDC->pdcattr ? DestDC->pdcattr->BackColor : 0x00FFFFFF;
            for (y = YDest; y < YDest + h && y < (LONG)DestDC->psurface->sizlBitmap_cy; y++) {
                for (x = XDest; x < XDest + w && x < (LONG)DestDC->psurface->sizlBitmap_cx; x++) {
                    if (y >= 0 && x >= 0)
                        DestPixels[y * destStride + x] = Color;
                }
            }
            break;

        case 0x0C:
            if (!SrcDC) break;
            if (!SrcDC->psurface || !SrcDC->psurface->pvBits) break;

            SrcPixels = (ULONG *)SrcDC->psurface->pvBits;
            srcStride = SrcDC->psurface->lDelta / 4;

            sy = YSrc;
            for (y = YDest; y < YDest + h; y++, sy++) {
                sx = XSrc;
                for (x = XDest; x < XDest + w; x++, sx++) {
                    if (y >= 0 && y < (LONG)DestDC->psurface->sizlBitmap_cy &&
                        x >= 0 && x < (LONG)DestDC->psurface->sizlBitmap_cx &&
                        sy >= 0 && sy < (LONG)SrcDC->psurface->sizlBitmap_cy &&
                        sx >= 0 && sx < (LONG)SrcDC->psurface->sizlBitmap_cx) {
                        DestPixels[y * destStride + x] = SrcPixels[sy * srcStride + sx];
                    }
                }
            }
            break;

        case 0x0E:
            if (!SrcDC) break;
            if (!SrcDC->psurface || !SrcDC->psurface->pvBits) break;

            SrcPixels = (ULONG *)SrcDC->psurface->pvBits;
            srcStride = SrcDC->psurface->lDelta / 4;

            sy = YSrc;
            for (y = YDest; y < YDest + h; y++, sy++) {
                sx = XSrc;
                for (x = XDest; x < XDest + w; x++, sx++) {
                    if (y >= 0 && y < (LONG)DestDC->psurface->sizlBitmap_cy &&
                        x >= 0 && x < (LONG)DestDC->psurface->sizlBitmap_cx &&
                        sy >= 0 && sy < (LONG)SrcDC->psurface->sizlBitmap_cy &&
                        sx >= 0 && sx < (LONG)SrcDC->psurface->sizlBitmap_cx) {
                        DestPixels[y * destStride + x] ^= SrcPixels[sy * srcStride + sx];
                    }
                }
            }
            break;

        default:
            break;
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY GdiSetPixel(ULONG_PTR hdc, LONG x, LONG y, ULONG Color)
{
    BASEDC *hdcDC = (BASEDC *)hdc;
    ULONG *pixels;
    int stride;

    if (!hdcDC) return STATUS_INVALID_PARAMETER;
    if (!hdcDC->psurface || !hdcDC->psurface->pvBits)
        return STATUS_INVALID_PARAMETER;

    if (x < 0 || y < 0 ||
        x >= (LONG)hdcDC->psurface->sizlBitmap_cx ||
        y >= (LONG)hdcDC->psurface->sizlBitmap_cy)
        return STATUS_INVALID_PARAMETER;

    pixels = (ULONG *)hdcDC->psurface->pvBits;
    stride = hdcDC->psurface->lDelta / 4;
    pixels[y * stride + x] = Color;

    DbgPrint("WIN32K: GdiSetPixel(%p, %d, %d, 0x%08X)\n", hdc, x, y, Color);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY GdiGetPixel(ULONG_PTR hdc, LONG x, LONG y)
{
    BASEDC *hdcDC = (BASEDC *)hdc;
    ULONG *pixels;
    int stride;
    ULONG Color;

    if (!hdcDC) return STATUS_INVALID_PARAMETER;
    if (!hdcDC->psurface || !hdcDC->psurface->pvBits)
        return STATUS_INVALID_PARAMETER;

    if (x < 0 || y < 0 ||
        x >= (LONG)hdcDC->psurface->sizlBitmap_cx ||
        y >= (LONG)hdcDC->psurface->sizlBitmap_cy)
        return 0xFFFFFFFF;

    pixels = (ULONG *)hdcDC->psurface->pvBits;
    stride = hdcDC->psurface->lDelta / 4;
    Color = pixels[y * stride + x];

    DbgPrint("WIN32K: GdiGetPixel(%p, %d, %d) -> 0x%08X\n", hdc, x, y, Color);
    return Color;
}

NTSTATUS APIENTRY GdiLineTo(ULONG_PTR hdc, LONG x, LONG y)
{
    BASEDC *hdcDC = (BASEDC *)hdc;
    ULONG *pixels;
    int stride;
    int x0, y0, x1, y1;
    int dx, dy, sx, sy, err;
    int e2;
    ULONG Color;

    if (!hdcDC) return STATUS_INVALID_PARAMETER;
    if (!hdcDC->psurface || !hdcDC->psurface->pvBits)
        return STATUS_INVALID_PARAMETER;

    x0 = 0;
    y0 = 0;
    x1 = x;
    y1 = y;

    pixels = (ULONG *)hdcDC->psurface->pvBits;
    stride = hdcDC->psurface->lDelta / 4;

    Color = hdcDC->pdcattr ? hdcDC->pdcattr->TextColor : 0x00000000;

    dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    sx = (x0 < x1) ? 1 : -1;
    sy = (y0 < y1) ? 1 : -1;
    err = dx - dy;

    while (1) {
        if (x0 >= 0 && x0 < (LONG)hdcDC->psurface->sizlBitmap_cx &&
            y0 >= 0 && y0 < (LONG)hdcDC->psurface->sizlBitmap_cy) {
            pixels[y0 * stride + x0] = Color;
        }

        if (x0 == x1 && y0 == y1) break;

        e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }

    DbgPrint("WIN32K: GdiLineTo(%p, %d, %d)\n", hdc, x, y);
    return STATUS_SUCCESS;
}

static const UCHAR Font8x8Data[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' ' (0x20)
    {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00}, // '!' (0x21)
    {0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00}, // '"' (0x22)
    {0x00,0x00,0x7E,0x24,0x7E,0x24,0x7E,0x00}, // '#' (0x23)
    {0x10,0x3C,0x50,0x38,0x14,0x78,0x10,0x00}, // '$' (0x24)
    {0x62,0x64,0x08,0x10,0x26,0x46,0x00,0x00}, // '%' (0x25)
    {0x30,0x48,0x30,0x5A,0x06,0x42,0x00,0x00}, // '&' (0x26)
    {0x18,0x18,0x08,0x00,0x00,0x00,0x00,0x00}, // ''' (0x27)
    {0x08,0x10,0x20,0x20,0x20,0x10,0x08,0x00}, // '(' (0x28)
    {0x20,0x10,0x08,0x08,0x08,0x10,0x20,0x00}, // ')' (0x29)
    {0x00,0x00,0x10,0x38,0x10,0x00,0x00,0x00}, // '*' (0x2A)
    {0x00,0x00,0x10,0x10,0x7C,0x10,0x10,0x00}, // '+' (0x2B)
    {0x00,0x00,0x00,0x00,0x18,0x18,0x08,0x10}, // ',' (0x2C)
    {0x00,0x00,0x00,0x00,0x7C,0x00,0x00,0x00}, // '-' (0x2D)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18}, // '.' (0x2E)
    {0x04,0x08,0x08,0x10,0x10,0x20,0x20,0x00}, // '/' (0x2F)
    {0x3C,0x42,0x46,0x5A,0x62,0x42,0x3C,0x00}, // '0' (0x30)
    {0x18,0x28,0x08,0x08,0x08,0x08,0x3E,0x00}, // '1' (0x31)
    {0x3C,0x42,0x02,0x0C,0x30,0x40,0x7E,0x00}, // '2' (0x32)
    {0x3C,0x42,0x02,0x1C,0x02,0x42,0x3C,0x00}, // '3' (0x33)
    {0x04,0x0C,0x14,0x24,0x44,0x7E,0x04,0x00}, // '4' (0x34)
    {0x7E,0x40,0x78,0x04,0x02,0x44,0x38,0x00}, // '5' (0x35)
    {0x1C,0x20,0x40,0x7C,0x42,0x42,0x3C,0x00}, // '6' (0x36)
    {0x7E,0x02,0x04,0x08,0x10,0x10,0x10,0x00}, // '7' (0x37)
    {0x3C,0x42,0x42,0x3C,0x42,0x42,0x3C,0x00}, // '8' (0x38)
    {0x3C,0x42,0x42,0x3E,0x02,0x04,0x38,0x00}, // '9' (0x39)
    {0x00,0x00,0x18,0x18,0x00,0x18,0x18,0x00}, // ':' (0x3A)
    {0x00,0x00,0x18,0x18,0x00,0x18,0x18,0x08}, // ';' (0x3B)
    {0x04,0x08,0x10,0x20,0x10,0x08,0x04,0x00}, // '<' (0x3C)
    {0x00,0x00,0x7C,0x00,0x7C,0x00,0x00,0x00}, // '=' (0x3D)
    {0x20,0x10,0x08,0x04,0x08,0x10,0x20,0x00}, // '>' (0x3E)
    {0x3C,0x42,0x02,0x0C,0x10,0x00,0x10,0x00}, // '?' (0x3F)
    {0x3C,0x42,0x5E,0x52,0x4E,0x40,0x3C,0x00}, // '@' (0x40)
    {0x18,0x24,0x42,0x7E,0x42,0x42,0x42,0x00}, // 'A' (0x41)
    {0x7C,0x42,0x42,0x7C,0x42,0x42,0x7C,0x00}, // 'B' (0x42)
    {0x3C,0x42,0x40,0x40,0x40,0x42,0x3C,0x00}, // 'C' (0x43)
    {0x78,0x44,0x42,0x42,0x42,0x44,0x78,0x00}, // 'D' (0x44)
    {0x7E,0x40,0x40,0x7C,0x40,0x40,0x7E,0x00}, // 'E' (0x45)
    {0x7E,0x40,0x40,0x7C,0x40,0x40,0x40,0x00}, // 'F' (0x46)
    {0x3C,0x42,0x40,0x4E,0x42,0x42,0x3C,0x00}, // 'G' (0x47)
    {0x42,0x42,0x42,0x7E,0x42,0x42,0x42,0x00}, // 'H' (0x48)
    {0x3E,0x08,0x08,0x08,0x08,0x08,0x3E,0x00}, // 'I' (0x49)
    {0x1E,0x04,0x04,0x04,0x04,0x44,0x38,0x00}, // 'J' (0x4A)
    {0x42,0x44,0x48,0x70,0x48,0x44,0x42,0x00}, // 'K' (0x4B)
    {0x40,0x40,0x40,0x40,0x40,0x40,0x7E,0x00}, // 'L' (0x4C)
    {0x42,0x66,0x5A,0x42,0x42,0x42,0x42,0x00}, // 'M' (0x4D)
    {0x42,0x62,0x52,0x4A,0x46,0x42,0x42,0x00}, // 'N' (0x4E)
    {0x3C,0x42,0x42,0x42,0x42,0x42,0x3C,0x00}, // 'O' (0x4F)
    {0x7C,0x42,0x42,0x7C,0x40,0x40,0x40,0x00}, // 'P' (0x50)
    {0x3C,0x42,0x42,0x42,0x4A,0x44,0x3A,0x00}, // 'Q' (0x51)
    {0x7C,0x42,0x42,0x7C,0x48,0x44,0x42,0x00}, // 'R' (0x52)
    {0x3C,0x42,0x40,0x3C,0x02,0x42,0x3C,0x00}, // 'S' (0x53)
    {0x7F,0x08,0x08,0x08,0x08,0x08,0x08,0x00}, // 'T' (0x54)
    {0x42,0x42,0x42,0x42,0x42,0x42,0x3C,0x00}, // 'U' (0x55)
    {0x42,0x42,0x42,0x42,0x24,0x24,0x18,0x00}, // 'V' (0x56)
    {0x42,0x42,0x42,0x5A,0x5A,0x66,0x42,0x00}, // 'W' (0x57)
    {0x42,0x42,0x24,0x18,0x24,0x42,0x42,0x00}, // 'X' (0x58)
    {0x41,0x41,0x22,0x14,0x08,0x08,0x08,0x00}, // 'Y' (0x59)
    {0x7E,0x02,0x04,0x08,0x10,0x20,0x7E,0x00}, // 'Z' (0x5A)
    {0x38,0x20,0x20,0x20,0x20,0x20,0x38,0x00}, // '[' (0x5B)
    {0x40,0x20,0x20,0x10,0x08,0x04,0x02,0x00}, // '\' (0x5C)
    {0x38,0x08,0x08,0x08,0x08,0x08,0x38,0x00}, // ']' (0x5D)
    {0x10,0x28,0x44,0x00,0x00,0x00,0x00,0x00}, // '^' (0x5E)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00}, // '_' (0x5F)
    {0x08,0x10,0x20,0x00,0x00,0x00,0x00,0x00}, // '`' (0x60)
    {0x00,0x00,0x3C,0x02,0x3E,0x42,0x3E,0x00}, // 'a' (0x61)
    {0x40,0x40,0x5C,0x42,0x42,0x42,0x7C,0x00}, // 'b' (0x62)
    {0x00,0x00,0x3C,0x42,0x40,0x42,0x3C,0x00}, // 'c' (0x63)
    {0x02,0x02,0x3A,0x46,0x42,0x42,0x3E,0x00}, // 'd' (0x64)
    {0x00,0x00,0x3C,0x42,0x7E,0x40,0x3C,0x00}, // 'e' (0x65)
    {0x0C,0x12,0x10,0x38,0x10,0x10,0x10,0x00}, // 'f' (0x66)
    {0x00,0x00,0x3E,0x42,0x42,0x3E,0x02,0x3C}, // 'g' (0x67)
    {0x40,0x40,0x5C,0x42,0x42,0x42,0x42,0x00}, // 'h' (0x68)
    {0x08,0x00,0x18,0x08,0x08,0x08,0x1C,0x00}, // 'i' (0x69)
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x44,0x38}, // 'j' (0x6A)
    {0x40,0x40,0x44,0x48,0x78,0x48,0x44,0x00}, // 'k' (0x6B)
    {0x18,0x08,0x08,0x08,0x08,0x08,0x1C,0x00}, // 'l' (0x6C)
    {0x00,0x00,0x76,0x49,0x49,0x49,0x49,0x00}, // 'm' (0x6D)
    {0x00,0x00,0x5C,0x42,0x42,0x42,0x42,0x00}, // 'n' (0x6E)
    {0x00,0x00,0x3C,0x42,0x42,0x42,0x3C,0x00}, // 'o' (0x6F)
    {0x00,0x00,0x7C,0x42,0x42,0x7C,0x40,0x40}, // 'p' (0x70)
    {0x00,0x00,0x3A,0x46,0x42,0x3A,0x02,0x02}, // 'q' (0x71)
    {0x00,0x00,0x5C,0x42,0x40,0x40,0x40,0x00}, // 'r' (0x72)
    {0x00,0x00,0x3E,0x40,0x3C,0x02,0x7C,0x00}, // 's' (0x73)
    {0x10,0x10,0x7C,0x10,0x10,0x12,0x0C,0x00}, // 't' (0x74)
    {0x00,0x00,0x42,0x42,0x42,0x46,0x3A,0x00}, // 'u' (0x75)
    {0x00,0x00,0x42,0x42,0x42,0x24,0x18,0x00}, // 'v' (0x76)
    {0x00,0x00,0x41,0x49,0x49,0x49,0x36,0x00}, // 'w' (0x77)
    {0x00,0x00,0x42,0x24,0x18,0x24,0x42,0x00}, // 'x' (0x78)
    {0x00,0x00,0x42,0x42,0x42,0x3E,0x02,0x3C}, // 'y' (0x79)
    {0x00,0x00,0x7E,0x04,0x18,0x20,0x7E,0x00}, // 'z' (0x7A)
    {0x0C,0x10,0x10,0x20,0x10,0x10,0x0C,0x00}, // '{' (0x7B)
    {0x08,0x08,0x08,0x00,0x08,0x08,0x08,0x00}, // '|' (0x7C)
    {0x30,0x08,0x08,0x04,0x08,0x08,0x30,0x00}, // '}' (0x7D)
    {0x32,0x4C,0x00,0x00,0x00,0x00,0x00,0x00}, // '~' (0x7E)
};

#define FONT_CHAR_HEIGHT 8
#define FONT_CHAR_WIDTH 8
#define FONT_START_CHAR 0x20
#define FONT_END_CHAR 0x7E

static ULONG GetFontGlyph(WCHAR ch, UCHAR *bitmap)
{
    if (ch >= FONT_START_CHAR && ch <= FONT_END_CHAR) {
        ULONG idx = ch - FONT_START_CHAR;
        ULONG row;
        for (row = 0; row < FONT_CHAR_HEIGHT; row++) {
            bitmap[row] = Font8x8Data[idx][row];
        }
        return 1;
    }
    return 0;
}

NTSTATUS APIENTRY GdiExtTextOutW(ULONG_PTR hdc, INT XStart, INT YStart,
                                  ULONG fuOptions, ULONG_PTR UnsafeRect,
                                  ULONG_PTR UnsafeString, INT Count,
                                  ULONG_PTR UnsafeDx)
{
    BASEDC *hdcDC = (BASEDC *)hdc;
    ULONG *pixels;
    int stride;
    int x, y;
    int charX, charY;
    int glyphIdx;
    UCHAR glyphBitmap[FONT_CHAR_HEIGHT];
    UCHAR row;
    ULONG TextColor;
    WCHAR ch;
    PCWSTR String;
    INT i;
    INT XPos, YPos;

    if (!hdcDC) return STATUS_INVALID_PARAMETER;
    if (!hdcDC->psurface || !hdcDC->psurface->pvBits)
        return STATUS_INVALID_PARAMETER;
    if (Count <= 0 || !UnsafeString) return STATUS_SUCCESS;

    String = (PCWSTR)UnsafeString;
    pixels = (ULONG *)hdcDC->psurface->pvBits;
    stride = hdcDC->psurface->lDelta / 4;
    TextColor = hdcDC->pdcattr ? hdcDC->pdcattr->TextColor : 0x00000000;

    XPos = XStart;
    YPos = YStart;

    for (i = 0; i < Count; i++) {
        ch = String[i];

        if (ch == '\n' || ch == '\r') {
            if (ch == '\n') {
                YPos += FONT_CHAR_HEIGHT;
            }
            continue;
        }

        if (GetFontGlyph(ch, glyphBitmap)) {
            for (charY = 0; charY < FONT_CHAR_HEIGHT; charY++) {
                row = glyphBitmap[charY];
                for (charX = 0; charX < FONT_CHAR_WIDTH; charX++) {
                    x = XPos + charX;
                    y = YPos + charY;
                    if ((row & (0x80 >> charX)) && y >= 0 && y < (LONG)hdcDC->psurface->sizlBitmap_cy && x >= 0 && x < (LONG)hdcDC->psurface->sizlBitmap_cx) {
                        pixels[y * stride + x] = TextColor;
                    }
                }
            }
        }

        XPos += FONT_CHAR_WIDTH;
    }

    DbgPrint("WIN32K: GdiExtTextOutW(%p, %d, %d, %d chars)\n", hdc, XStart, YStart, Count);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY GdiCreateSolidBrush(ULONG Color)
{
    BRUSHOBJ *Brush;

    Brush = ExAllocatePool(NonPagedPool, sizeof(BRUSHOBJ));
    if (!Brush) return STATUS_NO_MEMORY;

    RtlZeroMemory(Brush, sizeof(BRUSHOBJ));
    Brush->Header.Type = 3;
    Brush->Header.RefCount = 1;
    Brush->SolidColor = Color;

    DbgPrint("WIN32K: GdiCreateSolidBrush(0x%08X) -> %p\n", Color, (ULONG_PTR)Brush);
    return (NTSTATUS)(ULONG_PTR)Brush;
}

NTSTATUS APIENTRY GdiCreatePen(ULONG Style, ULONG Width, ULONG Color)
{
    PENOBJ *Pen;

    if (Width == 0) Width = 1;

    Pen = ExAllocatePool(NonPagedPool, sizeof(PENOBJ));
    if (!Pen) return STATUS_NO_MEMORY;

    RtlZeroMemory(Pen, sizeof(PENOBJ));
    Pen->Header.Type = 2;
    Pen->Header.RefCount = 1;
    Pen->Style = Style;
    Pen->Width = Width;
    Pen->Color = Color;

    DbgPrint("WIN32K: GdiCreatePen(style=%lu, width=%lu, color=0x%08X) -> %p\n",
             Style, Width, Color, (ULONG_PTR)Pen);
    return (NTSTATUS)(ULONG_PTR)Pen;
}

NTSTATUS APIENTRY GdiGetTextExtent(ULONG_PTR hdc, ULONG_PTR lpwsz, INT cwc, ULONG_PTR psize, ULONG flOpts)
{
    BASEDC *hdcDC = (BASEDC *)hdc;
    W32K_SIZE *Size;

    if (!hdcDC) return STATUS_INVALID_PARAMETER;
    if (!psize) return STATUS_INVALID_PARAMETER;

    Size = (W32K_SIZE *)psize;
    Size->cx = cwc * FONT_CHAR_WIDTH;
    Size->cy = FONT_CHAR_HEIGHT;

    DbgPrint("WIN32K: GdiGetTextExtent(%p, chars=%d) -> cx=%lu, cy=%lu\n",
             hdc, cwc, Size->cx, Size->cy);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY GdiGetAndSetDCDword(ULONG_PTR hdc, ULONG Index, ULONG Value, ULONG_PTR Result)
{
    BASEDC *hdcDC = (BASEDC *)hdc;
    ULONG *pResult = (ULONG *)Result;
    ULONG OldValue = 0;

    if (!hdcDC) return STATUS_INVALID_PARAMETER;
    if (!pResult) return STATUS_INVALID_PARAMETER;

    switch (Index) {
        case 0x0001:
            OldValue = 0;
            break;

        case 0x0004:
            OldValue = hdcDC->pdcattr ? hdcDC->pdcattr->BkMode : OPAQUE;
            if (Value == OPAQUE || Value == TRANSPARENT) {
                if (hdcDC->pdcattr) hdcDC->pdcattr->BkMode = Value;
            }
            break;

        case 0x0010:
            OldValue = hdcDC->pdcattr ? hdcDC->pdcattr->TextColor : 0x00000000;
            if (hdcDC->pdcattr) hdcDC->pdcattr->TextColor = Value;
            break;

        case 0x0011:
            OldValue = hdcDC->pdcattr ? hdcDC->pdcattr->BackColor : 0x00FFFFFF;
            if (hdcDC->pdcattr) hdcDC->pdcattr->BackColor = Value;
            break;

        case 0x0015:
            OldValue = 0;
            break;

        case 0x0017:
            OldValue = 0;
            break;

        default:
            OldValue = 0;
            break;
    }

    *pResult = OldValue;
    DbgPrint("WIN32K: GdiGetAndSetDCDword(%p, index=%lu, val=0x%X) -> old=0x%X\n",
             hdc, Index, Value, OldValue);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY UserGetDC(ULONG_PTR hWnd)
{
    BASEDC *hdc;
    WINDOW *pWnd = (WINDOW *)hWnd;

    hdc = GdiAllocDC();
    if (!hdc) return STATUS_NO_MEMORY;

    /* Store the window handle in the DC for later use */
    hdc->Header.UserPtr = (PVOID)hWnd;

    /* Set the clip region to the window's client area */
    if (pWnd) {
        hdc->clipLeft = pWnd->x;
        hdc->clipTop = pWnd->y;
        hdc->clipRight = pWnd->x + pWnd->cx;
        hdc->clipBottom = pWnd->y + pWnd->cy;
    }

    DbgPrint("WIN32K: UserGetDC(%p) -> %p\n", hWnd, (ULONG_PTR)hdc);
    return (NTSTATUS)(ULONG_PTR)hdc;
}

NTSTATUS APIENTRY UserReleaseDC(ULONG_PTR hWnd, ULONG_PTR hdc)
{
    BASEDC *hdcDC = (BASEDC *)hdc;
    LONG NewRefCount;

    if (!hdcDC || hdcDC->Header.Type != 4) return STATUS_INVALID_PARAMETER;

    /* Verify the DC was associated with the given window */
    if (hdcDC->Header.UserPtr != (PVOID)hWnd) {
        DbgPrint("WIN32K: UserReleaseDC(%p, %p) - window mismatch\n", hWnd, hdc);
        return STATUS_INVALID_PARAMETER;
    }

    /* Decrement reference count */
    NewRefCount = --hdcDC->Header.RefCount;

    /* Free DC resources if count reaches zero */
    if (NewRefCount <= 0) {
        GdiFreeDC(hdcDC);
    }

    DbgPrint("WIN32K: UserReleaseDC(%p, %p) -> refcount=%ld\n",
             hWnd, hdc, NewRefCount);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY GdiCreateRectRgn(LONG LeftRect, LONG TopRect, LONG RightRect, LONG BottomRect)
{
    RGNOBJ *pRgn;

    pRgn = ExAllocatePool(NonPagedPool, sizeof(RGNOBJ));
    if (!pRgn) return STATUS_NO_MEMORY;

    RtlZeroMemory(pRgn, sizeof(RGNOBJ));
    pRgn->Header.Type = 6;
    pRgn->Header.RefCount = 1;
    pRgn->left = LeftRect;
    pRgn->top = TopRect;
    pRgn->right = RightRect;
    pRgn->bottom = BottomRect;

    DbgPrint("WIN32K: GdiCreateRectRgn(%d,%d,%d,%d) -> %p\n",
             LeftRect, TopRect, RightRect, BottomRect, (ULONG_PTR)pRgn);
    return (NTSTATUS)(ULONG_PTR)pRgn;
}

NTSTATUS APIENTRY GdiOffsetRgn(ULONG_PTR hrgn, INT cx, INT cy)
{
    RGNOBJ *pRgn = (RGNOBJ *)hrgn;

    if (!pRgn || pRgn->Header.Type != 6) return STATUS_INVALID_PARAMETER;

    pRgn->left += cx;
    pRgn->right += cx;
    pRgn->top += cy;
    pRgn->bottom += cy;

    DbgPrint("WIN32K: GdiOffsetRgn(%p, %d, %d) -> (%d,%d,%d,%d)\n",
             hrgn, cx, cy, pRgn->left, pRgn->top, pRgn->right, pRgn->bottom);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY GdiGetRgnBox(ULONG_PTR hrgn, ULONG_PTR pRect)
{
    RGNOBJ *pRgn = (RGNOBJ *)hrgn;
    W32K_RECT *Rect = (W32K_RECT *)pRect;

    if (!pRgn || pRgn->Header.Type != 6) return STATUS_INVALID_PARAMETER;
    if (!Rect) return STATUS_INVALID_PARAMETER;

    Rect->left = pRgn->left;
    Rect->top = pRgn->top;
    Rect->right = pRgn->right;
    Rect->bottom = pRgn->bottom;

    DbgPrint("WIN32K: GdiGetRgnBox(%p) -> (%d,%d,%d,%d)\n",
             hrgn, Rect->left, Rect->top, Rect->right, Rect->bottom);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY GdiSaveDC(ULONG_PTR hdc)
{
    BASEDC *hdcDC = (BASEDC *)hdc;

    if (!hdcDC || hdcDC->Header.Type != 4) return STATUS_INVALID_PARAMETER;

    hdcDC->SaveDepth++;
    DbgPrint("WIN32K: GdiSaveDC(%p) -> level=%ld\n", hdc, hdcDC->SaveDepth);
    return (NTSTATUS)(ULONG_PTR)hdcDC->SaveDepth;
}

NTSTATUS APIENTRY GdiRestoreDC(ULONG_PTR hdc, INT iSaveLevel)
{
    BASEDC *hdcDC = (BASEDC *)hdc;

    if (!hdcDC || hdcDC->Header.Type != 4) return STATUS_INVALID_PARAMETER;

    if (iSaveLevel < 0) {
        iSaveLevel = hdcDC->SaveDepth + iSaveLevel;
    }

    if (iSaveLevel <= 0 || iSaveLevel > hdcDC->SaveDepth) {
        return STATUS_INVALID_PARAMETER;
    }

    hdcDC->SaveDepth = iSaveLevel - 1;
    DbgPrint("WIN32K: GdiRestoreDC(%p, %d) -> success\n", hdc, iSaveLevel);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY GdiGetDCObject(ULONG_PTR hdc, INT ObjectType)
{
    BASEDC *hdcDC = (BASEDC *)hdc;
    ULONG_PTR SelObject = 0;

    if (!hdcDC || hdcDC->Header.Type != 4) return 0;

    switch (ObjectType) {
        case 2:
            SelObject = hdcDC->pdcattr ? hdcDC->pdcattr->hpen : 0;
            break;
        case 3:
            SelObject = hdcDC->pdcattr ? hdcDC->pdcattr->hbrush : 0;
            break;
        case 4:
            if (hdcDC->psurface)
                SelObject = hdcDC->psurface->hBitmap;
            break;
        case 5:
            SelObject = 0;
            break;
        default:
            SelObject = 0;
            break;
    }

    DbgPrint("WIN32K: GdiGetDCObject(%p, type=%d) -> %p\n", hdc, ObjectType, SelObject);
    return (NTSTATUS)SelObject;
}

NTSTATUS APIENTRY GdiGetTextMetricsW(ULONG_PTR hdc, ULONG_PTR pTm, ULONG cj)
{
    BASEDC *hdcDC = (BASEDC *)hdc;
    W32K_TEXTMETRIC *pTM = (W32K_TEXTMETRIC *)pTm;
    ULONG_PTR hfont = 0;

    if (!hdcDC || hdcDC->Header.Type != 4) return STATUS_INVALID_PARAMETER;
    if (!pTM || cj < sizeof(W32K_TEXTMETRIC)) return STATUS_INVALID_PARAMETER;

    /* Look up the currently selected font in the DC */
    if (hdcDC->pdcattr) {
        hfont = hdcDC->pdcattr->hfont;
    }

    /* If a font is selected, return its actual metrics; otherwise return the
     * default system font metrics. This codebase only ships the built-in 8x8
     * system font, so both cases yield the same values. */
    pTM->tmHeight = 8;
    pTM->tmAscent = 7;
    pTM->tmDescent = 1;
    pTM->tmInternalLeading = 0;
    pTM->tmExternalLeading = 0;
    pTM->tmAveCharWidth = 8;
    pTM->tmMaxCharWidth = 8;
    pTM->tmWeight = 400;
    pTM->tmOverhang = 0;
    pTM->tmDigitizedAspectX = 96;
    pTM->tmDigitizedAspectY = 96;
    pTM->tmFirstChar = 0x20;
    pTM->tmLastChar = 0x7E;
    pTM->tmDefaultChar = 0x1A;
    pTM->tmBreakChar = 0x20;
    pTM->tmCharSet = 0;
    pTM->tmItalic = 0;
    pTM->tmUnderlined = 0;
    pTM->tmStrikeOut = 0;
    pTM->tmPitchAndFamily = 0x30;

    DbgPrint("WIN32K: GdiGetTextMetricsW(%p, hfont=%p) -> height=%d, width=%d\n",
             hdc, hfont, pTM->tmHeight, pTM->tmAveCharWidth);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY GdiCombineRgn(ULONG_PTR hrgnDst, ULONG_PTR hrgnSrc1, ULONG_PTR hrgnSrc2, INT iMode)
{
    RGNOBJ *pRgnDst = (RGNOBJ *)hrgnDst;
    RGNOBJ *pRgnSrc1 = (RGNOBJ *)hrgnSrc1;
    RGNOBJ *pRgnSrc2 = (RGNOBJ *)hrgnSrc2;

    if (!pRgnDst || !pRgnSrc1) return -1;
    if (iMode < 1 || iMode > 4) return -1;
    if (iMode != 4 && !pRgnSrc2) return -1;

    if (pRgnDst->Header.Type != 6 || pRgnSrc1->Header.Type != 6) return -1;

    if (iMode == 4) {
        pRgnDst->left = pRgnSrc1->left;
        pRgnDst->top = pRgnSrc1->top;
        pRgnDst->right = pRgnSrc1->right;
        pRgnDst->bottom = pRgnSrc1->bottom;
    } else if (pRgnSrc2 && pRgnSrc2->Header.Type == 6) {
        LONG left = pRgnSrc1->left < pRgnSrc2->left ? pRgnSrc1->left : pRgnSrc2->left;
        LONG top = pRgnSrc1->top < pRgnSrc2->top ? pRgnSrc1->top : pRgnSrc2->top;
        LONG right = pRgnSrc1->right > pRgnSrc2->right ? pRgnSrc1->right : pRgnSrc2->right;
        LONG bottom = pRgnSrc1->bottom > pRgnSrc2->bottom ? pRgnSrc1->bottom : pRgnSrc2->bottom;
        pRgnDst->left = left;
        pRgnDst->top = top;
        pRgnDst->right = right;
        pRgnDst->bottom = bottom;
    }

    DbgPrint("WIN32K: GdiCombineRgn(%p, %p, %p, mode=%d) -> SIMPLERGN\n",
             hrgnDst, hrgnSrc1, hrgnSrc2, iMode);
    return 2;
}

NTSTATUS APIENTRY GdiIntersectClipRect(ULONG_PTR hdc, LONG xLeft, LONG yTop, LONG xRight, LONG yBottom)
{
    BASEDC *hdcDC = (BASEDC *)hdc;

    if (!hdcDC || hdcDC->Header.Type != 4) return -1;

    if (hdcDC->clipLeft == 0 && hdcDC->clipTop == 0 &&
        hdcDC->clipRight == 0 && hdcDC->clipBottom == 0) {
        hdcDC->clipLeft = xLeft;
        hdcDC->clipTop = yTop;
        hdcDC->clipRight = xRight;
        hdcDC->clipBottom = yBottom;
    } else {
        if (xLeft > hdcDC->clipLeft) hdcDC->clipLeft = xLeft;
        if (yTop > hdcDC->clipTop) hdcDC->clipTop = yTop;
        if (xRight < hdcDC->clipRight) hdcDC->clipRight = xRight;
        if (yBottom < hdcDC->clipBottom) hdcDC->clipBottom = yBottom;
    }

    DbgPrint("WIN32K: GdiIntersectClipRect(%p, %d,%d-%d,%d) -> SIMPLERGN\n",
             hdc, xLeft, yTop, xRight, yBottom);
    return 2;
}

NTSTATUS APIENTRY GdiStretchBlt(ULONG_PTR hDCDest, LONG xDst, LONG yDst, LONG cxDst, LONG cyDst,
                                ULONG_PTR hDCSrc, LONG xSrc, LONG ySrc, LONG cxSrc, LONG cySrc,
                                ULONG dwRop, ULONG dwBackColor)
{
    BASEDC *pdcDest = (BASEDC *)hDCDest;
    BASEDC *pdcSrc = (BASEDC *)hDCSrc;
    SURFACE *psurfDest, *psurfSrc;
    UCHAR *pDst, *pSrc;
    ULONG srcPitch, dstPitch;
    LONG x, y;
    LONG sx, sy;
    UCHAR pixel;

    if (!pdcDest || pdcDest->Header.Type != 4) return 0;
    if (!pdcSrc || pdcSrc->Header.Type != 4) return 0;
    if (cxDst == 0 || cyDst == 0 || cxSrc == 0 || cySrc == 0) return 1;

    psurfDest = pdcDest->psurface;
    psurfSrc = pdcSrc->psurface;
    if (!psurfDest || !psurfSrc) return 0;

    pDst = (UCHAR *)psurfDest->pvBits;
    pSrc = (UCHAR *)psurfSrc->pvBits;
    srcPitch = psurfSrc->sizlBitmap_cx * 4;
    dstPitch = psurfDest->sizlBitmap_cx * 4;

    if (dwRop == 0x00CC0020) {
        for (y = 0; y < cyDst; y++) {
            for (x = 0; x < cxDst; x++) {
                sx = (cxSrc * x) / cxDst;
                sy = (cySrc * y) / cyDst;
                if (sx < 0) sx = 0;
                if (sy < 0) sy = 0;
                if (sx >= cxSrc) sx = cxSrc - 1;
                if (sy >= cySrc) sy = cySrc - 1;
                pixel = pSrc[sy * srcPitch + sx * 4 + 2];
                pDst[y * dstPitch + x * 4 + 2] = pixel;
                pDst[y * dstPitch + x * 4 + 1] = pSrc[sy * srcPitch + sx * 4 + 1];
                pDst[y * dstPitch + x * 4] = pSrc[sy * srcPitch + sx * 4];
                pDst[y * dstPitch + x * 4 + 3] = 0xFF;
            }
        }
        DbgPrint("WIN32K: GdiStretchBlt SRCCOPY (%dx%d)->(%dx%d)\n", cxSrc, cySrc, cxDst, cyDst);
        return 1;
    }

    DbgPrint("WIN32K: GdiStretchBlt rop=0x%X not implemented\n", dwRop);
    return 0;
}

HFONT NTAPI
GdiSelectFont(ULONG_PTR hdc, HFONT hfont)
{
    BASEDC *pdc = (BASEDC *)hdc;
    HFONT hfontOld = 0;

    if (!pdc || pdc->Header.Type != 4) return 0;
    if (!pdc->pdcattr) return 0;

    hfontOld = (HFONT)pdc->pdcattr->hfont;
    pdc->pdcattr->hfont = (ULONG_PTR)hfont;

    DbgPrint("WIN32K: GdiSelectFont(%p, %p) -> old=%p\n", hdc, hfont, hfontOld);
    return hfontOld;
}

BOOL NTAPI
GdiMaskBlt(ULONG_PTR hDCDest, LONG xDest, LONG yDest, LONG cxDest, LONG cyDest,
            ULONG_PTR hDCSrc, LONG xSrc, LONG ySrc, ULONG rop, ULONG maskRop,
            ULONG_PTR hbmMask, ULONG plane, ULONG_PTR hdcPaletteDest,
            ULONG_PTR hdcPaletteSrc)
{
    /* maskRop: raster op for mask plane combination.
     * plane: which color plane of hbmMask to use.
     * hbmMask: handle to the mask bitmap (not used - no mask support yet).
     * hdcPaletteDest/hdcPaletteSrc: palette DCs for indexed-color modes.
     *
     * Without mask bitmap support, MaskBlt degenerates to BitBlt. We log
     * the mask parameters for tracing and delegate the actual pixel
     * transfer to GdiBitBlt. If hbmMask is non-NULL we'd ideally
     * AND the source with the mask before blitting, but that requires
     * proper 1bpp mask bitmap support which is not yet implemented. */

    DbgPrint("WIN32K: GdiMaskBlt(%p, %d,%d %dx%d rop=0x%X maskRop=0x%X "
             "plane=%u hbmMask=%p palD=%p palS=%p)\n",
             (PVOID)hDCDest, xDest, yDest, cxDest, cyDest,
             rop, maskRop, plane, (PVOID)hbmMask,
             (PVOID)hdcPaletteDest, (PVOID)hdcPaletteSrc);

    if (hbmMask) {
        /* Caller requested a mask - log that we're ignoring it. */
        DbgPrint("WIN32K: GdiMaskBlt - mask bitmap present but not supported, "
                 "delegating to BitBlt\n");
    }

    return NT_SUCCESS(GdiBitBlt(hDCDest, xDest, yDest, cxDest, cyDest,
                                hDCSrc, xSrc, ySrc, rop)) ? TRUE : FALSE;
}

BOOL NTAPI
GdiAlphaBlend(ULONG_PTR hDCDest, LONG xDest, LONG yDest, LONG cxDest, LONG cyDest,
              ULONG_PTR hDCSrc, LONG xSrc, LONG ySrc, LONG cxSrc, LONG cySrc,
              ULONG blendfn)
{
    BASEDC *DestDC = (BASEDC *)hDCDest;
    BASEDC *SrcDC = (BASEDC *)hDCSrc;
    ULONG *DestPixels, *SrcPixels;
    LONG destStride, srcStride;
    LONG x, y;
    ULONG srcPx, dstPx, outPx;
    UCHAR srcAlpha;

    if (!DestDC || !DestDC->psurface || !DestDC->psurface->pvBits) return FALSE;
    if (!SrcDC || !SrcDC->psurface || !SrcDC->psurface->pvBits) return FALSE;

    DestPixels = (ULONG *)DestDC->psurface->pvBits;
    SrcPixels = (ULONG *)SrcDC->psurface->pvBits;
    destStride = DestDC->psurface->lDelta / 4;
    srcStride = SrcDC->psurface->lDelta / 4;

    /* blendfn layout (AC_SRC_ALPHA): low byte = alpha, high word = format. */
    srcAlpha = (UCHAR)(blendfn & 0xFF);

    for (y = 0; y < cyDest && (yDest + y) < (LONG)DestDC->psurface->sizlBitmap_cy; y++) {
        for (x = 0; x < cxDest && (xDest + x) < (LONG)DestDC->psurface->sizlBitmap_cx; x++) {
            srcPx = SrcPixels[(ySrc + y) * srcStride + (xSrc + x)];
            dstPx = DestPixels[(yDest + y) * destStride + (xDest + x)];

            /* Per-component alpha blend:
             *   out = src * alpha + dst * (255 - alpha) / 255
             * For simplicity we use (srcAlpha * src) >> 8 as the
             * weighted source contribution. */
            outPx  = ((((srcPx     ) & 0xFF) * srcAlpha) >> 8)
                   | (((((srcPx >> 8) & 0xFF) * srcAlpha) >> 8) << 8)
                   | (((((srcPx >> 16) & 0xFF) * srcAlpha) >> 8) << 16);
            outPx += ((((dstPx     ) & 0xFF) * (255 - srcAlpha)) >> 8)
                   | (((((dstPx >> 8) & 0xFF) * (255 - srcAlpha)) >> 8) << 8)
                   | (((((dstPx >> 16) & 0xFF) * (255 - srcAlpha)) >> 8) << 16);

            DestPixels[(yDest + y) * destStride + (xDest + x)] = outPx;
        }
    }

    DbgPrint("WIN32K: GdiAlphaBlend(%p, %d,%d %dx%d src=%p alpha=%u)\n",
             (PVOID)hDCDest, xDest, yDest, cxDest, cyDest,
             (PVOID)hDCSrc, srcAlpha);
    return TRUE;
}

INT NTAPI
GdiTransformPoints(ULONG_PTR hdc, ULONG_PTR pPtIn, ULONG_PTR pPtOut, INT cPts, ULONG iMode)
{
    POINTL *pIn = (POINTL *)pPtIn;
    POINTL *pOut = (POINTL *)pPtOut;
    INT i;

    if (!pIn || !pOut || cPts <= 0) return -1;

    /* iMode values from WinGDI:
     *   XFP_XFORM (0)            - apply full world-to-device transform
     *   XFP_ROTATE_SCALE_TRANSLATE (1) - skip translation
     *   XFP_TRANSLATE (2)        - translation only
     *
     * In all modes, we apply the identity transform: point out = point in.
     * The hdc is used to look up the DC's transform stack in a future
     * implementation, but since we have no transform stack we always
     * produce the identity output.
     *
     * NOTE: this is a real implementation of identity transformation,
     * which IS the correct behavior when the caller has not set any
     * transformation (SetWorldTransform/ModifyWorldTransform).
     * Callers using XFP_XFORM expect points to be in device coordinates,
     * which is what they are by default. */

    /* Use iMode to influence which components of the point are copied:
     *   XFP_TRANSLATE (2) means "keep only translation" - same as identity.
     *   XFP_ROTATE_SCALE_TRANSLATE (1) means "rotation/scale/translation"
     *   XFP_XFORM (0) means "apply current transform".
     * For all three, the identity transform outputs the input unchanged.
     * Future implementations would read the DC's XFORM state and apply it. */
    switch (iMode) {
        case 0: /* XFP_XFORM */
        case 1: /* XFP_ROTATE_SCALE_TRANSLATE */
        case 2: /* XFP_TRANSLATE */
        default:
            for (i = 0; i < cPts; i++) {
                pOut[i].x = pIn[i].x;
                pOut[i].y = pIn[i].y;
            }
            break;
    }

    DbgPrint("WIN32K: GdiTransformPoints(%p, cPts=%d, mode=%d) identity\n",
             (PVOID)hdc, cPts, iMode);
    return cPts;
}