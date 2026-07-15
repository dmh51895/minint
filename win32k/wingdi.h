/*
 * MinNT - win32k/wingdi.h
 * Windows GDI definitions (Wine compatibility)
 */

#ifndef _WINGDI_H_
#define _WINGDI_H_

#include <nt/ntdef.h>

#define GDI_ERROR           0xFFFFFFFF
#define CLR_INVALID        0xFFFFFFFF

#define R2_BLACK            1
#define R2_WHITE            0
#define R2_NOT              2
#define R2_NOTMERGEPENS    3
#define R2_XORPEN           4
#define R2_NOTXORPEN        5
#define R2_NOTERASE        6
#define R2_INVERT           7
#define R2_OUT              8
#define R2_COPY             13
#define R2_PATCOPY         11

#define SRCCOPY            0x00CC0020
#define SRCPAINT           0x00EE0086
#define SRCAND             0x008800C6
#define SRCINVERT          0x00660046
#define SRCERASE           0x00440328
#define NOTSRCCOPY         0x00330066
#define NOTSRCERASE        0x001100A9
#define MERGECOPY          0x00C000C8
#define MERGEPAINT         0x00BB0227
#define PATCOPY           0x00F00021
#define PATPAINT           0x00FB0A09
#define PATINVERT          0x005A0049
#define DSTINVERT          0x00550009
#define BLACKONWHITE       0x00010003
#define WHITEONBLACK       0x00020004

#define BLACKNESS          0x00000042
#define WHITENESS          0x000000FF
#define TRANSPARENT        1
#define OPAQUE             2

#define PS_SOLID           0
#define PS_DASH            1
#define PS_DOT             2
#define PS_DASHDOT         3
#define PS_DASHDOTDOT      4
#define PS_NULL            5
#define PS_INSIDEFRAME     6

#define PS_COSMETIC        0x00000000
#define PS_ENDCAP_ROUND    0x00000000
#define PS_ENDCAP_SQUARE   0x00000100
#define PS_JOIN_ROUND     0x00000000
#define PS_JOIN_BEVEL      0x00001000

#define BS_SOLID           0
#define BS_NULL            1
#define BS_HATCHED          2
#define BS_PATTERN          3
#define BS_INDEXED          4
#define BS_DIBPATTERN       5
#define BS_DIBPATTERN8      8
#define BS_MONOPATTERN      9

#define HS_HORIZONTAL      0
#define HS_VERTICAL        1
#define HS_FDIAGONAL       2
#define HS_BDIAGONAL       3
#define HS_CROSS           4
#define HS_DIAGCROSS       5

#define DC_BRUSH           18
#define DC_PEN             19
#define DC_EXTPEN          114
#define SIZEF_FULLSCREEN  1
#define SIZEF_PALETTE     2
#define SIZEF_RESTORE      3
#define SIZEF_NOVIRTUALSCREEN 4

#define ETO_OPAQUE         0x00000002
#define ETO_CLIPPED        0x00000004
#define ETO_GLYPH_INDEX    0x00000080
#define ETO_IGNORELANGUAGE 0x00001000
#define ETO_PDY            0x00002000
#define ETO_RTLREADING     0x00000800

#define CLIP_DEFAULT_PRECIS 0
#define CLIP_CHARACTER_PRECIS 1
#define CLIP_STROKE_PRECIS 2
#define CLIP_LH_ANGLES     1
#define CLIP_TT_ALWAYS     2
#define CLIP_EMBEDDED     4

#define OUT_DEFAULT_PRECIS 0
#define OUT_STRING_PRECIS 1
#define OUT_CHARACTER_PRECIS 2
#define OUT_STROKE_PRECIS 3
#define OUT_TT_PRECIS     4
#define OUT_DEVICE_PRECIS 5
#define OUT_RASTER_PRECIS 6
#define OUT_TT_ONLY_PRECIS 7
#define OUT_OUTLINE_PRECIS 8
#define OUT_SCREENONLY_PRECIS 9

#define DEFAULT_CHARSET    1
#define OEM_CHARSET        255
#define ANSI_CHARSET       0
#define SYMBOL_CHARSET     2

#define LF_FACESIZE        32

#define DEFAULT_PITCH      0
#define FIXED_PITCH        1
#define VARIABLE_PITCH     2
#define MONO_FONT          8

#define FF_DONTCARE       0
#define FF_ROMAN           1
#define FF_SWISS          2
#define FF_SCRIPT          4
#define FF_MODERN          5
#define FF_BOLD           8
#define FF_REGULAR        16

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

typedef struct _RGBQUAD {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
} RGBQUAD;

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
} BITMAPINFOHEADER;

typedef struct _BITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[1];
} BITMAPINFO;

#define BI_RGB        0
#define BI_RLE8       1
#define BI_RLE4       2
#define BI_BITFIELDS  3

#define DIB_RGB_COLORS    0
#define DIB_PAL_COLORS    1

#define HBITMAP         ULONG_PTR
#define HDC             ULONG_PTR
#define HRGN            ULONG_PTR
#define HPEN            ULONG_PTR
#define HBRUSH          ULONG_PTR
#define HPALETTE        ULONG_PTR
#define HFONT           ULONG_PTR
#define HMETAFILE       ULONG_PTR
#define HENHMETAFILE    ULONG_PTR
#define HRGN            ULONG_PTR

#define CreateBitmap(x,y,c,z,b) 0
#define CreateCompatibleBitmap(x,y) 0
#define CreateCompatibleDC(x) 0
#define GetDC(x) 0
#define ReleaseDC(x) 0
#define DeleteDC(x) 0
#define DeleteObject(x) 0
#define SelectObject(x,y) 0
#define GetObject(x,y,z) 0

#define TRANSPARENT         1
#define OPAQUE              2

#define AC_SRC_OVER         0x00
#define AC_SRC_ALPHA        0x01

#endif /* _WINGDI_H_ */
