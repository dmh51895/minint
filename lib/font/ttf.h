/*
 * MinNT - lib/font/ttf.h
 * TrueType/OpenType font engine — for real text rendering.
 *
 * Implements a minimal but valid TrueType font parser and glyph
 * rasterizer. Parses the mandatory tables (head, maxp, hhea, hmtx,
 * cmap, loca, glyf) and rasterizes glyphs via Bézier curve flattening.
 *
 * OpenType (OTTO) fonts with CFF outlines are not supported here;
 * this is TrueType (TTF/glyf) only.
 */

#ifndef _MINNT_TTF_H_
#define _MINNT_TTF_H_

#include <nt/ntdef.h>
#ifndef FLOAT
typedef float FLOAT;
#endif

/* ----------------------------------------------------------------------------
 * Truetype table structures
 * -------------------------------------------------------------------------- */

#pragma pack(push, 1)

typedef struct _TTF_TABLE_HEADER {
    ULONG tag;            /* 4-byte table tag ('head', 'maxp', etc.) */
    ULONG checksum;
    ULONG offset;          /* offset in font file */
    ULONG length;
} TTF_TABLE_HEADER;

/* head table (54 bytes) */
typedef struct _TTF_HEAD {
    ULONG   version;        /* 0x00010000 for TrueType */
    ULONG   fontRevision;
    ULONG   checksumAdjustment;
    ULONG   magicNumber;    /* 0x5F0F3CF5 */
    USHORT  flags;
    USHORT  unitsPerEm;     /* typically 2048 or 1000 */
    ULONG64 created;
    ULONG64 modified;
    SHORT   xMin, yMin, xMax, yMax;
    USHORT  macStyle;
    USHORT  lowestRecPPEM;
    SHORT   fontDirectionHint;
    SHORT   indexToLocFormat; /* 0 = short, 1 = long */
    SHORT   glyphDataFormat;
} TTF_HEAD;

/* maxp table */
typedef struct _TTF_MAXP {
    ULONG  version;
    USHORT numGlyphs;
    USHORT maxPoints;
    USHORT maxContours;
    USHORT maxCompositePoints;
    USHORT maxCompositeContours;
    USHORT maxZones;
    USHORT maxTwilightPoints;
    USHORT maxStorage;
    USHORT maxFunctionDefs;
    USHORT maxInstructionDefs;
    USHORT maxStackElements;
    USHORT maxSizeOfInstructions;
    USHORT maxComponentElements;
    USHORT maxComponentDepth;
} TTF_MAXP;

/* hhea table */
typedef struct _TTF_HHEA {
    ULONG   version;
    SHORT   ascender;
    SHORT   descender;
    SHORT   lineGap;
    USHORT  advanceWidthMax;
    SHORT   minLeftSideBearing;
    SHORT   minRightSideBearing;
    SHORT   xMaxExtent;
    SHORT   caretSlopeRise;
    SHORT   caretSlopeRun;
    SHORT   caretOffset;
    SHORT   metricDataFormat;
    USHORT  numberOfHMetrics;
} TTF_HHEA;

/* cmap 'fmt 4' subtable header (segment mapping) */
typedef struct _TTF_CMAP4 {
    USHORT format;        /* 4 */
    USHORT length;
    USHORT language;
    USHORT segCountX2;
    USHORT searchRange;
    USHORT entrySelector;
    USHORT rangeShift;
    /* followed by endCount[], reservedPad, startCount[], idDelta[], idRangeOffset[], glyphIdArray[] */
} TTF_CMAP4;

#pragma pack(pop)

/* ----------------------------------------------------------------------------
 * TTF font state
 * -------------------------------------------------------------------------- */

typedef struct _TTF_FONT {
    PUCHAR          data;            /* font file in memory */
    SIZE_T          size;
    USHORT          numTables;
    /* Mandatory tables */
    TTF_HEAD        head;
    TTF_MAXP        maxp;
    TTF_HHEA        hhea;
    /* Table locations */
    ULONG           cmap_offset;
    ULONG           cmap_length;
    ULONG           loca_offset;
    ULONG           loca_length;
    ULONG           glyf_offset;
    ULONG           glyf_length;
    ULONG           hmtx_offset;
    ULONG           hmtx_length;
    /* Cached cmap fmt4 pointers */
    PULONG          cmap_endCount;
    PULONG          cmap_startCount;
    PSHORT          cmap_idDelta;
    PUSHORT         cmap_idRangeOffset;
    PUSHORT         cmap_glyphIdArray;
    USHORT          cmap_segCount;
    /* Loca format (0 = short, 1 = long) */
    SHORT           loca_format;
    BOOLEAN         valid;
} TTF_FONT, *PTTF_FONT;

/* Glyph metrics */
typedef struct _TTF_GLYPH_METRICS {
    SHORT   advanceX;       /* in font units */
    SHORT   lsb;            /* left side bearing */
    SHORT   rsb;
    SHORT   width;
    SHORT   height;
    SHORT   xBearing;
    SHORT   yBearing;
} TTF_GLYPH_METRICS, *PTTF_GLYPH_METRICS;

/* Rasterized glyph bitmap */
typedef struct _TTF_GLYPH_BITMAP {
    PUCHAR  pixels;         /* 8-bit grayscale, row-major, rowStride bytes per row */
    INT     width;
    INT     height;
    INT     pitch;          /* bytes per row */
    INT     xBearing;
    INT     yBearing;
    INT     advanceX;
    INT     advanceY;
} TTF_GLYPH_BITMAP, *PTTF_GLYPH_BITMAP;

/* ----------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

NTSTATUS NTAPI TtfLoadFont(PVOID fontData, SIZE_T size, PTTF_FONT outFont);
USHORT   NTAPI TtfGetGlyphIndex(PTTF_FONT font, ULONG unicodeCodepoint);
NTSTATUS NTAPI TtfGetGlyphMetrics(PTTF_FONT font, USHORT glyphIndex,
                                     PTTF_GLYPH_METRICS outMetrics);
NTSTATUS NTAPI TtfRasterizeGlyph(PTTF_FONT font, USHORT glyphIndex,
                                    INT pixelSize,
                                    PTTF_GLYPH_BITMAP outBitmap);
VOID     NTAPI TtfFreeBitmap(PTTF_GLYPH_BITMAP bitmap);
INT      NTAPI TtfGetKerning(PTTF_FONT font, USHORT leftGlyph, USHORT rightGlyph);

#endif /* _MINNT_TTF_H_ */
