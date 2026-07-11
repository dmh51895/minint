/*
 * MinNT - ke/exports/gdi32_exports.c
 * gdi32.dll exports — GDI graphics functions.
 * Routes to win32k kernel GDI implementations.
 */

#include <nt/ntdef.h>
#include <nt/ex.h>
#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/exe.h>
#include "../win32k/win32k.h"
#include <ndk/obfuncs.h>

#ifndef UINT
typedef unsigned int UINT;
#endif
typedef ULONG_PTR UINT_PTR;
typedef LONG_PTR LRESULT;
typedef ULONG_PTR HPEN;

/* ============================================================================
 * Device Context
 * ========================================================================== */

__attribute__((ms_abi))
static HDC CreateCompatibleDC_msabi(HDC hdc)
{
    return (HDC)GdiCreateCompatibleDC(hdc);
}

__attribute__((ms_abi))
static BOOL DeleteDC_msabi(HDC hdc)
{
    /* Our GdiDeleteObjectApp handles DCs too */
    return NT_SUCCESS(GdiDeleteObjectApp(hdc));
}

__attribute__((ms_abi))
static INT SaveDC_msabi(HDC hdc)
{
    return NT_SUCCESS(GdiSaveDC(hdc)) ? 1 : 0;
}

__attribute__((ms_abi))
static BOOL RestoreDC_msabi(HDC hdc, INT iSaveLevel)
{
    return NT_SUCCESS(GdiRestoreDC(hdc, iSaveLevel));
}

/* ============================================================================
 * Bitmaps
 * ========================================================================== */

__attribute__((ms_abi))
static HBITMAP CreateCompatibleBitmap_msabi(HDC hdc, INT nWidth, INT nHeight)
{
    return (HBITMAP)GdiCreateCompatibleBitmap(hdc, nWidth, nHeight);
}

__attribute__((ms_abi))
static HBITMAP CreateBitmap_msabi(INT nWidth, INT nHeight, UINT nPlanes,
    UINT nBitCount, const void *lpBits)
{
    (void)nWidth; (void)nHeight; (void)nPlanes; (void)nBitCount; (void)lpBits;
    /* Allocate a simple bitmap backed by kernel memory */
    if (nWidth <= 0 || nHeight <= 0) return 0;
    SIZE_T size = (SIZE_T)nWidth * nHeight * 4;
    PVOID bits = ExAllocatePool(NonPagedPool, size);
    if (!bits) return 0;
    if (lpBits) RtlCopyMemory(bits, lpBits, size);
    else RtlZeroMemory(bits, size);
    return (HBITMAP)bits;
}

__attribute__((ms_abi))
static HBITMAP CreateBitmapIndirect_msabi(PVOID lpbm)
{
    /* BITMAP struct: bmType, bmWidth, bmHeight, bmWidthBytes, bmPlanes, bmBits */
    LONG *bm = (LONG *)lpbm;
    if (!bm) return 0;
    INT w = (INT)bm[1], h = (INT)bm[2];
    return CreateBitmap_msabi(w, h, 1, 32, NULL);
}

/* ============================================================================
 * Object Selection and Deletion
 * ========================================================================== */

__attribute__((ms_abi))
static HBITMAP SelectObject_msabi(HDC hdc, ULONG_PTR hgdiobj)
{
    /* For bitmaps, call GdiSelectBitmap; for others, just return the object */
    if (hdc && hgdiobj) {
        /* Try as bitmap first */
        GdiSelectBitmap(hdc, hgdiobj);
    }
    return (HBITMAP)hgdiobj;
}

__attribute__((ms_abi))
static BOOL DeleteObject_msabi(ULONG_PTR hObject)
{
    return NT_SUCCESS(GdiDeleteObjectApp(hObject));
}

__attribute__((ms_abi))
static ULONG_PTR GetCurrentObject_msabi(HDC hdc, INT ObjectType)
{
    return (ULONG_PTR)GdiGetDCObject(hdc, ObjectType);
}

/* ============================================================================
 * Pens and Brushes
 * ========================================================================== */

__attribute__((ms_abi))
static HPEN CreatePen_msabi(INT fnPenStyle, INT nWidth, ULONG crColor)
{
    return (HPEN)GdiCreatePen(fnPenStyle, nWidth, crColor);
}

__attribute__((ms_abi))
static HBRUSH CreateSolidBrush_msabi(ULONG crColor)
{
    return (HBRUSH)GdiCreateSolidBrush(crColor);
}

__attribute__((ms_abi))
static HBRUSH CreateBrushIndirect_msabi(PVOID lplb)
{
    /* LOGBRUSH: lbStyle(ULONG), lbColor(ULONG), lbHatch(ULONG_PTR) */
    ULONG *lb = (ULONG *)lplb;
    if (!lb) return 0;
    return (HBRUSH)GdiCreateSolidBrush(lb[1]);
}

__attribute__((ms_abi))
static HBRUSH GetStockObject_msabi(INT i)
{
    /* Return a simple colored brush for common stock objects */
    switch (i) {
    case 0: return (HBRUSH)GdiCreateSolidBrush(0xFFFFFF); /* WHITE_BRUSH */
    case 1: return (HBRUSH)GdiCreateSolidBrush(0x000000); /* LTGRAY_BRUSH */
    case 2: return (HBRUSH)GdiCreateSolidBrush(0x000000); /* GRAY_BRUSH */
    case 3: return (HBRUSH)GdiCreateSolidBrush(0x000000); /* DKGRAY_BRUSH */
    case 4: return (HBRUSH)GdiCreateSolidBrush(0x000000); /* BLACK_BRUSH */
    case 5: return (HBRUSH)GdiCreateSolidBrush(0xC0C0C0); /* NULL_BRUSH */
    case 10: return (HPEN)GdiCreatePen(0, 1, 0x000000);  /* BLACK_PEN */
    case 11: return (HPEN)GdiCreatePen(0, 1, 0xFFFFFF);  /* WHITE_PEN */
    default: return (HBRUSH)GdiCreateSolidBrush(0x000000);
    }
}

/* ============================================================================
 * Drawing
 * ========================================================================== */

__attribute__((ms_abi))
static BOOL BitBlt_msabi(HDC hdcDest, INT xDest, INT yDest, INT nWidth, INT nHeight,
    HDC hdcSrc, INT xSrc, INT ySrc, ULONG dwRop)
{
    return NT_SUCCESS(GdiBitBlt(hdcDest, xDest, yDest, nWidth, nHeight,
                                  hdcSrc, xSrc, ySrc, dwRop));
}

__attribute__((ms_abi))
static BOOL StretchBlt_msabi(HDC hdcDest, INT xDest, INT yDest, INT nDestWidth,
    INT nDestHeight, HDC hdcSrc, INT xSrc, INT ySrc, INT nSrcWidth, INT nSrcHeight,
    ULONG dwRop)
{
    return NT_SUCCESS(GdiStretchBlt(hdcDest, xDest, yDest, nDestWidth, nDestHeight,
                                       hdcSrc, xSrc, ySrc, nSrcWidth, nSrcHeight,
                                       dwRop, 0));
}

__attribute__((ms_abi))
static BOOL PatBlt_msabi(HDC hdc, INT x, INT y, INT nWidth, INT nHeight, ULONG dwRop)
{
    return NT_SUCCESS(GdiPatBlt(hdc, x, y, nWidth, nHeight, dwRop));
}

__attribute__((ms_abi))
static BOOL Rectangle_msabi(HDC hdc, INT left, INT top, INT right, INT bottom)
{
    return NT_SUCCESS(GdiRectangle(hdc, left, top, right, bottom));
}

__attribute__((ms_abi))
static BOOL LineTo_msabi(HDC hdc, INT x, INT y)
{
    return NT_SUCCESS(GdiLineTo(hdc, x, y));
}

__attribute__((ms_abi))
static BOOL MoveToEx_msabi(HDC hdc, INT x, INT y, PVOID lppt)
{
    /* Get current position, set new position */
    W32K_POINT pt;
    NTSTATUS s = GdiGetDCPoint(hdc, 1, &pt); /* 1 = current position */
    if (lppt) {
        LONG *ppt = (LONG *)lppt;
        ppt[0] = pt.x; ppt[1] = pt.y;
    }
    if (x != -1 || y != -1)
        GdiGetAndSetDCDword(hdc, 0, (ULONG)x, NULL); /* set x */
    return TRUE;
}

__attribute__((ms_abi))
static ULONG SetPixel_msabi(HDC hdc, INT x, INT y, ULONG crColor)
{
    return (ULONG)GdiSetPixel(hdc, x, y, crColor);
}

__attribute__((ms_abi))
static ULONG GetPixel_msabi(HDC hdc, INT x, INT y)
{
    return (ULONG)GdiGetPixel(hdc, x, y);
}

__attribute__((ms_abi))
static BOOL FillRect_msabi(HDC hdc, PVOID lprc, HBRUSH hbr)
{
    W32K_RECT *rc = (W32K_RECT *)lprc;
    if (!rc) return FALSE;
    select_brush:
    /* Select brush into DC, fill, restore */
    GdiSelectBitmap(hdc, hbr); /* reuse as generic select */
    return NT_SUCCESS(GdiPatBlt(hdc, rc->left, rc->top,
                                   rc->right - rc->left, rc->bottom - rc->top,
                                   PATCOPY));
}

/* ============================================================================
 * Text
 * ========================================================================== */

__attribute__((ms_abi))
static BOOL TextOutA_msabi(HDC hdc, INT x, INT y, const CHAR *lpString, INT c)
{
    /* Convert to Unicode */
    static WCHAR wbuf[1024];
    if (c > 1023) c = 1023;
    int i;
    for (i = 0; i < c; i++) wbuf[i] = (WCHAR)lpString[i];
    return NT_SUCCESS(GdiExtTextOutW(hdc, x, y, 0, 0, (ULONG_PTR)wbuf, c, 0));
}

__attribute__((ms_abi))
static BOOL TextOutW_msabi(HDC hdc, INT x, INT y, const WCHAR *lpString, INT c)
{
    return NT_SUCCESS(GdiExtTextOutW(hdc, x, y, 0, 0, (ULONG_PTR)lpString, c, 0));
}

__attribute__((ms_abi))
static BOOL ExtTextOutA_msabi(HDC hdc, INT x, INT y, ULONG fuOptions,
    PVOID lprc, const CHAR *lpString, UINT cbCount, PVOID lpDx)
{
    static WCHAR wbuf[1024];
    if (cbCount > 1023) cbCount = 1023;
    UINT i;
    for (i = 0; i < cbCount; i++) wbuf[i] = (WCHAR)lpString[i];
    return NT_SUCCESS(GdiExtTextOutW(hdc, x, y, fuOptions, (ULONG_PTR)lprc,
                                        (ULONG_PTR)wbuf, cbCount, (ULONG_PTR)lpDx));
}

__attribute__((ms_abi))
static BOOL ExtTextOutW_msabi(HDC hdc, INT x, INT y, ULONG fuOptions,
    PVOID lprc, const WCHAR *lpString, UINT cbCount, PVOID lpDx)
{
    return NT_SUCCESS(GdiExtTextOutW(hdc, x, y, fuOptions, (ULONG_PTR)lprc,
                                        (ULONG_PTR)lpString, cbCount, (ULONG_PTR)lpDx));
}

__attribute__((ms_abi))
static BOOL GetTextExtentA_msabi(HDC hdc, const CHAR *lpString, INT c, PVOID lpSize)
{
    static WCHAR wbuf[1024];
    if (c > 1023) c = 1023;
    int i;
    for (i = 0; i < c; i++) wbuf[i] = (WCHAR)lpString[i];
    return NT_SUCCESS(GdiGetTextExtent(hdc, (ULONG_PTR)wbuf, c, (ULONG_PTR)lpSize, 0));
}

__attribute__((ms_abi))
static BOOL GetTextExtentW_msabi(HDC hdc, const WCHAR *lpString, INT c, PVOID lpSize)
{
    return NT_SUCCESS(GdiGetTextExtent(hdc, (ULONG_PTR)lpString, c, (ULONG_PTR)lpSize, 0));
}

__attribute__((ms_abi))
static BOOL GetTextMetricsW_msabi(HDC hdc, PVOID lpMetrics, UINT cb)
{
    return NT_SUCCESS(GdiGetTextMetricsW(hdc, (ULONG_PTR)lpMetrics, cb));
}

__attribute__((ms_abi))
static HFONT CreateFontA_msabi(INT nHeight, INT nWidth, INT nEscapement, INT nOrientation,
    INT fnWeight, ULONG fdwItalic, ULONG fdwUnderline, ULONG fdwStrikeOut,
    ULONG fdwCharSet, ULONG fdwOutputPrecision, ULONG fdwClipPrecision,
    ULONG fdwQuality, ULONG fdwPitchAndFamily, const CHAR *lpszFace)
{
    (void)nHeight; (void)nWidth; (void)nEscapement; (void)nOrientation;
    (void)fnWeight; (void)fdwItalic; (void)fdwUnderline; (void)fdwStrikeOut;
    (void)fdwCharSet; (void)fdwOutputPrecision; (void)fdwClipPrecision;
    (void)fdwQuality; (void)fdwPitchAndFamily; (void)lpszFace;
    return (HFONT)0x20000000LL; /* fake font handle */
}

__attribute__((ms_abi))
static HFONT CreateFontW_msabi(INT nHeight, INT nWidth, INT nEscapement, INT nOrientation,
    INT fnWeight, ULONG fdwItalic, ULONG fdwUnderline, ULONG fdwStrikeOut,
    ULONG fdwCharSet, ULONG fdwOutputPrecision, ULONG fdwClipPrecision,
    ULONG fdwQuality, ULONG fdwPitchAndFamily, const WCHAR *lpszFace)
{
    return CreateFontA_msabi(nHeight, nWidth, nEscapement, nOrientation, fnWeight,
        fdwItalic, fdwUnderline, fdwStrikeOut, fdwCharSet, fdwOutputPrecision,
        fdwClipPrecision, fdwQuality, fdwPitchAndFamily, (const CHAR *)lpszFace);
}

/* ============================================================================
 * Regions
 * ========================================================================== */

__attribute__((ms_abi))
static HRGN CreateRectRgn_msabi(INT x1, INT y1, INT x2, INT y2)
{
    return (HRGN)GdiCreateRectRgn(x1, y1, x2, y2);
}

__attribute__((ms_abi))
static INT SelectClipRgn_msabi(HDC hdc, HRGN hrgn)
{
    return NT_SUCCESS(GdiExtSelectClipRgn(hdc, hrgn, 1));
}

/* ============================================================================
 * Clipping
 * ========================================================================== */

__attribute__((ms_abi))
static INT GetClipBox_msabi(HDC hdc, PVOID lprc)
{
    return NT_SUCCESS(GdiGetAppClipBox(hdc, (PW32K_RECT)lprc));
}

/* ============================================================================
 * Misc
 * ========================================================================== */

__attribute__((ms_abi))
static BOOL GdiFlush_msabi(VOID)
{
    return NT_SUCCESS(GdiFlush());
}

/* ============================================================================
 * Registration
 * ========================================================================== */

VOID NTAPI Gdi32RegisterExports(VOID)
{
#define GREG(name, ptr) ExeRegisterExport("gdi32.dll", name, ptr)

    /* DC */
    GREG("CreateCompatibleDC", CreateCompatibleDC_msabi);
    GREG("DeleteDC", DeleteDC_msabi);
    GREG("SaveDC", SaveDC_msabi);
    GREG("RestoreDC", RestoreDC_msabi);

    /* Bitmaps */
    GREG("CreateCompatibleBitmap", CreateCompatibleBitmap_msabi);
    GREG("CreateBitmap", CreateBitmap_msabi);
    GREG("CreateBitmapIndirect", CreateBitmapIndirect_msabi);

    /* Objects */
    GREG("SelectObject", SelectObject_msabi);
    GREG("DeleteObject", DeleteObject_msabi);
    GREG("GetCurrentObject", GetCurrentObject_msabi);

    /* Pens/Brushes */
    GREG("CreatePen", CreatePen_msabi);
    GREG("CreateSolidBrush", CreateSolidBrush_msabi);
    GREG("CreateBrushIndirect", CreateBrushIndirect_msabi);
    GREG("GetStockObject", GetStockObject_msabi);

    /* Drawing */
    GREG("BitBlt", BitBlt_msabi);
    GREG("StretchBlt", StretchBlt_msabi);
    GREG("PatBlt", PatBlt_msabi);
    GREG("Rectangle", Rectangle_msabi);
    GREG("LineTo", LineTo_msabi);
    GREG("MoveToEx", MoveToEx_msabi);
    GREG("SetPixel", SetPixel_msabi);
    GREG("GetPixel", GetPixel_msabi);
    GREG("FillRect", FillRect_msabi);

    /* Text */
    GREG("TextOutA", TextOutA_msabi);
    GREG("TextOutW", TextOutW_msabi);
    GREG("ExtTextOutA", ExtTextOutA_msabi);
    GREG("ExtTextOutW", ExtTextOutW_msabi);
    GREG("GetTextExtentA", GetTextExtentA_msabi);
    GREG("GetTextExtentW", GetTextExtentW_msabi);
    GREG("GetTextMetricsW", GetTextMetricsW_msabi);
    GREG("CreateFontA", CreateFontA_msabi);
    GREG("CreateFontW", CreateFontW_msabi);

    /* Regions */
    GREG("CreateRectRgn", CreateRectRgn_msabi);
    GREG("SelectClipRgn", SelectClipRgn_msabi);

    /* Clipping */
    GREG("GetClipBox", GetClipBox_msabi);

    /* Misc */
    GREG("GdiFlush", GdiFlush_msabi);

    DbgPrint("EXE: gdi32.dll exports registered (%lu total)\n", g_ExportCount);
}
