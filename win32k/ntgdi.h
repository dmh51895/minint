/*
 * MinNT - win32k/ntgdi.h
 * NT Graphics Device Interface (GDI) types and declarations
 *
 * These mirror the ReactOS win32ss gdi/ntgdi types.
 */

#ifndef _NTGDI_H_
#define _NTGDI_H_

#include <nt/ntdef.h>

#define APIENTRY __attribute__((stdcall))

typedef ULONG_PTR HDC;
typedef ULONG_PTR HBITMAP;
typedef ULONG_PTR HPEN;
typedef ULONG_PTR HBRUSH;
typedef ULONG_PTR HRGN;
typedef ULONG_PTR HFONT;
typedef ULONG_PTR HPALETTE;
typedef ULONG_PTR HANDLE;
typedef ULONG_PTR HTABLE;
typedef ULONG_PTR HGDIOBJ;
typedef HDC HDC_SURFACE;
typedef LONG COLORREF;

typedef struct _POINT {
    LONG x;
    LONG y;
} POINT, *PPOINT;

typedef struct _POINTL {
    LONG x;
    LONG y;
} POINTL, *PPOINTL;

typedef struct _SIZE {
    LONG cx;
    LONG cy;
} SIZE, *PSIZE;

typedef struct _SIZEL {
    LONG cx;
    LONG cy;
} SIZEL, *PSIZEL;

typedef struct _RECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *PRECT;

typedef struct _RECTL {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECTL, *PRECTL;

typedef struct _BLENDFUNCTION {
    UCHAR BlendOp;
    UCHAR BlendFlags;
    UCHAR SourceConstantAlpha;
    UCHAR AlphaFormat;
} BLENDFUNCTION, *PBLENDFUNCTION;

#define AC_SRC_OVER                 0x00
#define AC_SRC_ALPHA                0x01
#define ULW_ALPHA                   0x02

#define BLACKNESS                   0x00000042
#define Whiteness                   0x000000FF
#define R2_BLACK                    1
#define R2_WHITE                    0
#define R2_COPYPEN                  13
#define R2_PATCOPY                  11

#define DIB_RGB_COLORS              0
#define DIB_PAL_COLORS              1

#define SRCCOPY                    0x00CC0020
#define SRCPAINT                   0x00EE0086
#define SRCAND                     0x008800C6
#define SRCINVERT                  0x00660046
#define SRCERASE                   0x00440328
#define NOTSRCCOPY                 0x00330066
#define NOTSRCERASE                0x001100A9
#define MERGECOPY                  0x00C000C8
#define MERGEPAINT                 0x00BB0227
#define PATCOPY                    0x00F00021
#define PATPAINT                   0x00FB0A09
#define PATINVERT                  0x005A0049
#define DSTINVERT                  0x00550009
#define BLACKONWHITE               0x00010003
#define WHITEONBLACK               0x00020004

#define PS_SOLID                    0
#define PS_DASH                     1
#define PS_DOT                      2
#define PS_DASHDOT                  3
#define PS_DASHDOTDOT               4
#define PS_NULL                     5
#define PS_INSIDEFRAME              6

#define TRANSPARENT                 1
#define OPAQUE                      2
#define BKMODE_LAST                 2

#define ETO_OPAQUE                  0x00000002
#define ETO_CLIPPED                 0x00000004
#define ETO_GLYPH_INDEX             0x00000080
#define ETO_IGNORELANGUAGE          0x00001000
#define ETO_PDY                     0x00002000
#define ETO_RTLREADING              0x00000800

#define DEFAULT_CHARSET             1
#define OEM_CHARSET                 255
#define ANSI_CHARSET                0
#define SYMBOL_CHARSET              2

#define CLIP_DEFAULT_PRECIS         0
#define CLIP_CHARACTER_PRECIS       1
#define CLIP_STROKE_PRECIS          2
#define CLIP_MASK                   0xF
#define CLIP_LH_ANGLES              1
#define CLIP_TT_ALWAYS              2
#define CLIP_EMBEDVED               4

#define OUT_DEFAULT_PRECIS          0
#define OUT_STRING_PRECIS           1
#define OUT_CHARACTER_PRECIS        2
#define OUT_STROKE_PRECIS           3
#define OUT_TT_PRECIS               4
#define OUT_DEVICE_PRECIS           5
#define OUT_RASTER_PRECIS           6
#define OUT_TT_ONLY_PRECIS          7
#define OUT_OUTLINE_PRECIS          8
#define OUT_SCREENONLY_PRECIS       9

#define LF_FACESIZE                 32

typedef struct _LOGFONTA {
    LONG lfHeight;
    LONG lfWidth;
    LONG lfEscapement;
    LONG lfOrientation;
    LONG lfWeight;
    UCHAR lfItalic;
    UCHAR lfUnderline;
    UCHAR lfStrikeOut;
    UCHAR lfCharSet;
    UCHAR lfOutPrecision;
    UCHAR lfClipPrecision;
    UCHAR lfQuality;
    UCHAR lfPitchAndFamily;
    CHAR lfFaceName[LF_FACESIZE];
} LOGFONTA, *PLOGFONTA;

typedef struct _LOGFONTW {
    LONG lfHeight;
    LONG lfWidth;
    LONG lfEscapement;
    LONG lfOrientation;
    LONG lfWeight;
    UCHAR lfItalic;
    UCHAR lfUnderline;
    UCHAR lfStrikeOut;
    UCHAR lfCharSet;
    UCHAR lfOutPrecision;
    UCHAR lfClipPrecision;
    UCHAR lfQuality;
    UCHAR lfPitchAndFamily;
    WCHAR lfFaceName[LF_FACESIZE];
} LOGFONTW, *PLOGFONTW;

typedef struct _TEXTMETRICA {
    LONG tmHeight;
    LONG tmAscent;
    LONG tmDescent;
    LONG tmInternalLeading;
    LONG tmExternalLeading;
    LONG tmAveCharWidth;
    LONG tmMaxCharWidth;
    LONG tmWeight;
    LONG tmOverhang;
    LONG tmDigitizedAspectX;
    LONG tmDigitizedAspectY;
    BYTE tmFirstChar;
    BYTE tmLastChar;
    BYTE tmDefaultChar;
    BYTE tmBreakChar;
    BYTE tmItalic;
    BYTE tmUnderlined;
    BYTE tmStruckOut;
    BYTE tmPitchAndFamily;
    BYTE tmCharSet;
} TEXTMETRICA, *PTEXTMETRICA;

typedef struct _TEXTMETRICW {
    LONG tmHeight;
    LONG tmAscent;
    LONG tmDescent;
    LONG tmInternalLeading;
    LONG tmExternalLeading;
    LONG tmAveCharWidth;
    LONG tmMaxCharWidth;
    LONG tmWeight;
    LONG tmOverhang;
    LONG tmDigitizedAspectX;
    LONG tmDigitizedAspectY;
    WCHAR tmFirstChar;
    WCHAR tmLastChar;
    WCHAR tmDefaultChar;
    WCHAR tmBreakChar;
    BYTE tmItalic;
    BYTE tmUnderlined;
    BYTE tmStruckOut;
    BYTE tmPitchAndFamily;
    BYTE tmCharSet;
} TEXTMETRICW, *PTEXTMETRICW;

typedef struct _BITMAP {
    LONG bmType;
    LONG bmWidth;
    LONG bmHeight;
    LONG bmWidthBytes;
    WORD bmPlanes;
    WORD bmBitsPixel;
    PVOID bmBits;
} BITMAP, *PBITMAP;

typedef struct _BITMAPINFOHEADER {
    ULONG biSize;
    LONG biWidth;
    LONG biHeight;
    WORD biPlanes;
    WORD biBitCount;
    ULONG biCompression;
    ULONG biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
    ULONG biClrUsed;
    ULONG biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER;

typedef struct _RGBQUAD {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
} RGBQUAD;

typedef struct _BITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[1];
} BITMAPINFO, *PBITMAPINFO;

#define BI_RGB        0
#define BI_RLE8       1
#define BI_RLE4       2
#define BI_BITFIELDS  3
#define BI_JPEG       4
#define BI_PNG        5

#define GDI_ERROR                     0xFFFFFFFF
#define CLR_INVALID                   0xFFFFFFFF

NTSTATUS APIENTRY NtGdiBitBlt(HDC hDCDest, LONG XOriginDest, LONG YOriginDest,
                               ULONG WidthDest, ULONG HeightDest,
                               HDC hDCSrc, LONG XOriginSrc, LONG YOriginSrc,
                               ULONG WidthSrc, ULONG HeightSrc,
                               ULONG RasterOp, ULONG MixMode, ULONG BrushBitmap,
                               ULONG BrushOrigin, HRGN RgnClip, ULONG MetaRop);

NTSTATUS APIENTRY NtGdiCreateCompatibleDC(HDC hDC);
NTSTATUS APIENTRY NtGdiCreateCompatibleBitmap(HDC hDC, ULONG Width, ULONG Height);
NTSTATUS APIENTRY NtGdiSelectBitmap(HDC hDC, HBITMAP hBitmap);
NTSTATUS APIENTRY NtGdiDeleteObjectApp(HANDLE hObj);
NTSTATUS APIENTRY NtGdiFlush(VOID);
NTSTATUS APIENTRY NtGdiGetDCObject(HDC hDC, ULONG ObjectType);
NTSTATUS APIENTRY NtGdiCreatePen(ULONG PenStyle, ULONG Width, COLORREF Color, ULONG BrushStyle, ULONG BrushHatch, ULONG BrushColor);
NTSTATUS APIENTRY NtGdiCreateSolidBrush(COLORREF Color, ULONG BrushHatch);
NTSTATUS APIENTRY NtGdiSelectFont(HDC hDC, HFONT hFont);
NTSTATUS APIENTRY NtGdiExtTextOutW(HDC hDC, LONG X, LONG Y, ULONG Options, PRECTL Rect, LPWSTR String, ULONG Length, PLONG Positions, ULONG MixMode);
NTSTATUS APIENTRY NtGdiGetTextMetricsW(HDC hDC, PTEXTMETRICW TextMetric, ULONG BufferSize, ULONG Flags);
NTSTATUS APIENTRY NtGdiGetTextExtent(HDC hDC, LPWSTR TextString, ULONG Length, ULONG Count, PPOINT Extent, ULONG Flags);
NTSTATUS APIENTRY NtGdiRectangle(HDC hDC, LONG Left, LONG Top, LONG Right, LONG Bottom);
NTSTATUS APIENTRY NtGdiPatBlt(HDC hDC, LONG Left, LONG Top, LONG Width, LONG Height, ULONG RasterOp);
NTSTATUS APIENTRY NtGdiLineTo(HDC hDC, LONG x, LONG y);
NTSTATUS APIENTRY NtGdiSetPixel(HDC hDC, LONG X, LONG Y, COLORREF Color);
NTSTATUS APIENTRY NtGdiGetPixel(HDC hDC, LONG X, LONG Y);
NTSTATUS APIENTRY NtGdiCreateRectRgn(LONG Left, LONG Top, LONG Right, LONG Bottom);
NTSTATUS APIENTRY NtGdiCombineRgn(HRGN hrgnDest, HRGN hrgnSrc1, HRGN hrgnSrc2, ULONG iMode);
NTSTATUS APIENTRY NtGdiOffsetRgn(HRGN hrgn, LONG x, LONG y);
NTSTATUS APIENTRY NtGdiGetRgnBox(HRGN hrgn, PRECTL Rect);
NTSTATUS APIENTRY NtGdiExtSelectClipRgn(HDC hDC, HRGN hrgn, ULONG Mode);
NTSTATUS APIENTRY NtGdiIntersectClipRect(HDC hDC, LONG Left, LONG Top, LONG Right, LONG Bottom);
NTSTATUS APIENTRY NtGdiSaveDC(HDC hDC);
NTSTATUS APIENTRY NtGdiRestoreDC(HDC hDC, LONG Level);
NTSTATUS APIENTRY NtGdiStretchBlt(HDC hDCDest, LONG XOriginDest, LONG YOriginDest, LONG WidthDest, LONG HeightDest,
                                    HDC hDCSrc, LONG XOriginSrc, LONG YOriginSrc, LONG WidthSrc, LONG HeightSrc,
                                    ULONG RasterOp, ULONG MixMode, ULONG BrushBitmap, ULONG BrushOrigin);
NTSTATUS APIENTRY NtGdiMaskBlt(HDC hDCDest, LONG XDest, LONG YDest, LONG WidthDest, LONG HeightDest,
                                HDC hDCSrc, LONG XSrc, LONG YSrc, LONG WidthSrc, LONG HeightSrc,
                                HRGN BrushBitmap, LONG BrushOrigin, ULONG RasterOp, ULONG MixMode,
                                ULONG PatternBrushBitmap, ULONG PatternBrushOrigin);
NTSTATUS APIENTRY NtGdiAlphaBlend(HDC hDCDest, LONG XOriginDest, LONG YOriginDest, LONG WidthDest, LONG HeightDest,
                                   HDC hDCSrc, LONG XOriginSrc, LONG YOriginSrc, LONG WidthSrc, LONG HeightSrc,
                                   BLENDFUNCTION BlendFunction, ULONG flags);
NTSTATUS APIENTRY NtGdiGetAndSetDCDword(HDC hDC, ULONG Index, ULONG Value, PULONG OldValue);
NTSTATUS APIENTRY NtGdiGetDCDword(HDC hDC, ULONG Index, ULONG Value);
NTSTATUS APIENTRY NtGdiGetDCPoint(HDC hDC, ULONG Index, PPOINT Point);
NTSTATUS APIENTRY NtGdiGetAppClipBox(HDC hDC, PRECT Rect);
NTSTATUS APIENTRY NtGdiTransformPoints(HDC hDC, PPOINT Source, PPOINT Dest, ULONG Count, ULONG Mode);

#endif /* _NTGDI_H_ */