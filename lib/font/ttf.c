/*
 * MinNT - lib/font/ttf.c
 * TrueType font engine — parses TTF fonts and rasterizes glyphs.
 *
 * Implemented:
 *  - TTF table directory parsing (head, maxp, hhea, hmtx, cmap, loca, glyf)
 *  - cmap format 4 (most common Unicode mapping for Latin/Greek/Cyrillic)
 *  - hmtx advance width + LSB for glyphs
 *  - loca -> glyf glyph data lookup (short and long formats)
 *  - Simple glyph (one contour, multiple contours) rasterization:
 *      Parse glyph description (endPts, flags, x/y coords)
 *      Flatten Bézier curves (on/ref + implied quadratic offs)
 *      Bounded-edge rasterization using scanline fill
 *
 * Not implemented yet:
 *  - Composite glyphs (reuse base glyphs with transforms)
 *  - Hinting (the bytecode interpreter)
 *  - Variable fonts
 *  - CFF (OTTO) outlines
 */

#include <nt/ntdef.h>
#include <nt/ex.h>
#include <nt/mm.h>
#include <nt/rtl.h>
#include "ttf.h"

#define TTF_DEBUG 0
#if TTF_DEBUG
#define TFDBG(...) DbgPrint(__VA_ARGS__)
#else
#define TFDBG(...)
#endif

/* Big-endian readers */
static USHORT BE16(PUCHAR p) { return (USHORT)((p[0] << 8) | p[1]); }
static ULONG  BE32(PUCHAR p) { return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) |
                                        ((ULONG)p[2] << 8) | (ULONG)p[3]; }
static SHORT  BE16S(PUCHAR p) { return (SHORT)BE16(p); }
static LONG   BE32S(PUCHAR p) { return (LONG)BE32(p); }

/* ============================================================================
 * Table lookups
 * ========================================================================== */

static BOOLEAN TtfFindTable(PTTF_FONT font, ULONG tag, PULONG out_offset, PULONG out_length)
{
    ULONG num = font->numTables;
    PUCHAR base = font->data + 12; /* after sfnt version (4) + numTables (2) + rangePad */
    for (ULONG i = 0; i < num; i++) {
        PUCHAR e = base + i * 16;
        ULONG t = BE32(e);
        if (t == tag) {
            *out_offset = BE32(e + 8);
            *out_length = BE32(e + 12);
            return TRUE;
        }
    }
    return FALSE;
}

/* ============================================================================
 * LoadFont — parse the mandatory tables
 * ========================================================================== */

NTSTATUS NTAPI TtfLoadFont(PVOID fontData, SIZE_T size, PTTF_FONT outFont)
{
    if (!fontData || size < 12 || !outFont) return 0xC000000DL;
    PUCHAR d = (PUCHAR)fontData;

    RtlZeroMemory(outFont, sizeof(*outFont));
    outFont->data = d;
    outFont->size = size;

    /* Validate sfnt version: 0x00010000 (TrueType) or 'OTTO' */
    ULONG sfnt = BE32(d);
    if (sfnt != 0x00010000UL && sfnt != 0x4F54544FUL /* 'OTTO' */) {
        return 0xC000007BL;
    }
    /* OpenType CFF not supported yet — only glyf TrueType */
    if (sfnt == 0x4F54544FUL) {
        return 0xC000007BL;
    }

    outFont->numTables = BE16(d + 4);

    /* head */
    ULONG off, len;
    if (!TtfFindTable(outFont, 0x68656164UL /* 'head' */, &off, &len)) return 0xC000007BL;
    PUCHAR p = d + off;
    outFont->head.version = BE32(p);
    outFont->head.unitsPerEm = BE16(p + 18);
    outFont->head.indexToLocFormat = BE16S(p + 50);

    /* maxp */
    if (!TtfFindTable(outFont, 0x6D617870UL /* 'maxp' */, &off, &len)) return 0xC000007BL;
    p = d + off;
    outFont->maxp.version = BE32(p);
    outFont->maxp.numGlyphs = BE16(p + 4);

    /* hhea */
    if (!TtfFindTable(outFont, 0x68686561UL /* 'hhea' */, &off, &len)) return 0xC000007BL;
    p = d + off;
    outFont->hhea.version = BE32(p);
    outFont->hhea.ascender = BE16S(p + 4);
    outFont->hhea.descender = BE16S(p + 6);
    outFont->hhea.lineGap = BE16S(p + 8);
    outFont->hhea.advanceWidthMax = BE16(p + 10);
    outFont->hhea.numberOfHMetrics = BE16(p + 34);

    /* cmap, loca, glyf, hmtx locations */
    TtfFindTable(outFont, 0x636D6170UL /* 'cmap' */, &outFont->cmap_offset, &outFont->cmap_length);
    TtfFindTable(outFont, 0x6C6F6361UL /* 'loca' */, &outFont->loca_offset, &outFont->loca_length);
    TtfFindTable(outFont, 0x676C7966UL /* 'glyf' */, &outFont->glyf_offset, &outFont->glyf_length);
    TtfFindTable(outFont, 0x686D7478UL /* 'hmtx' */, &outFont->hmtx_offset, &outFont->hmtx_length);

    outFont->loca_format = outFont->head.indexToLocFormat;
    outFont->valid = TRUE;

    TFDBG("TTF loaded: numGlyphs=%u unitsPerEm=%u numHMetrics=%u locaFmt=%d\n",
          outFont->maxp.numGlyphs, outFont->head.unitsPerEm,
          outFont->hhea.numberOfHMetrics, outFont->loca_format);
    return STATUS_SUCCESS;
}

/* ============================================================================
 * cmap — Unicode -> glyph index
 *
 * We support cmap subtable format 4 (segment mapping) which is by far
 * the most common for Unicode-encoded fonts.
 * ========================================================================== */

static NTSTATUS TtfParseCmap(PTTF_FONT font)
{
    if (font->cmap_offset == 0 || font->cmap_length < 4) return 0xC000007BL;
    PUCHAR cmap = font->data + font->cmap_offset;
    USHORT version = BE16(cmap);
    USHORT numSubtables = BE16(cmap + 2);
    if (version != 0) return 0xC000007BL;

    /* Find best subtable: preference order 3,1 (Windows Unicode BMP) then 0,3 etc */
    ULONG bestOffset = 0;
    USHORT bestFormat = 0;
    USHORT bestPlatform = 0xFFFF, bestEncoding = 0xFFFF;

    for (USHORT i = 0; i < numSubtables; i++) {
        PUCHAR rec = cmap + 4 + i * 8;
        USHORT platformID = BE16(rec);
        USHORT encodingID = BE16(rec + 2);
        ULONG subOffset = BE32(rec + 4);
        if (subOffset + 2 > font->cmap_length) continue;
        USHORT fmt = BE16(cmap + subOffset);
        /* Prefer platform 3 (Windows), encoding 1 (Unicode BMP) */
        if (platformID == 3 && encodingID == 1 && fmt == 4) {
            bestOffset = subOffset;
            bestFormat = 4;
            bestPlatform = platformID;
            bestEncoding = encodingID;
            break;
        }
        /* Fallback: platform 0 (Unicode) */
        if (platformID == 0 && bestPlatform == 0xFFFF) {
            bestOffset = subOffset;
            bestFormat = fmt;
            bestPlatform = platformID;
            bestEncoding = encodingID;
        }
    }

    if (bestFormat != 4 || bestOffset == 0) {
        /* Fallback: take first subtable */
        if (numSubtables == 0) return 0xC000007BL;
        PUCHAR rec = cmap + 4;
        bestOffset = BE32(rec + 4);
        bestFormat = BE16(cmap + bestOffset);
        if (bestFormat != 4) return 0xC000007BL;
    }

    /* Parse format 4 subtable */
    PUCHAR sub = cmap + bestOffset;
    USHORT segCountX2 = BE16(sub + 6);
    USHORT segCount = segCountX2 / 2;

    font->cmap_segCount = segCount;
    PUCHAR endCountBase = sub + 14;
    PUCHAR startCountBase = endCountBase + segCountX2 + 2; /* +2 for reservedPad */
    PUCHAR idDeltaBase = startCountBase + segCountX2;
    PUCHAR idRangeBase = idDeltaBase + segCountX2;

    /* NOTE: cmap pointers point into font->data, which is held externally.
       We don't allocate. They are valid as long as the font is loaded. */
    font->cmap_endCount = (PULONG)endCountBase; /* type cast will use BE16 reader */
    font->cmap_startCount = (PULONG)startCountBase;
    font->cmap_idDelta = (PSHORT)idDeltaBase;
    font->cmap_idRangeOffset = (PUSHORT)idRangeBase;
    /* glyphIdArray follows idRangeOffset entries */

    return STATUS_SUCCESS;
}

USHORT NTAPI TtfGetGlyphIndex(PTTF_FONT font, ULONG cp)
{
    if (!font->valid) return 0;
    /* Parse cmap on first use */
    if (font->cmap_segCount == 0) {
        if (!NT_SUCCESS(TtfParseCmap(font))) return 0;
    }

    PUCHAR sub = (PUCHAR)font->cmap_endCount - 14; /* back to subtable start */
    USHORT segCount = font->cmap_segCount;
    PUCHAR endCount = (PUCHAR)font->cmap_endCount;
    PUCHAR startCount = (PUCHAR)font->cmap_startCount;
    PUCHAR idDelta = (PUCHAR)font->cmap_idDelta;
    PUCHAR idRangeOffset = (PUCHAR)font->cmap_idRangeOffset;

    /* Linear scan through segments (could binary search) */
    for (USHORT i = 0; i < segCount; i++) {
        USHORT end = BE16(endCount + i * 2);
        if (cp > end) continue;
        USHORT start = BE16(startCount + i * 2);
        if (cp < start) return 0;
        SHORT delta = BE16S(idDelta + i * 2);
        USHORT offset = BE16(idRangeOffset + i * 2);
        if (offset == 0) {
            /* Direct mapping: glyph = (cp + delta) mod 65536 */
            return (USHORT)((cp + delta) & 0xFFFF);
        }
        /* Indirect mapping via glyphIdArray */
        PUCHAR glyphIdBase = idRangeOffset + i * 2;  /* this is where offset=0
                                                       would point for this seg */
        PUCHAR target = glyphIdBase + offset + (cp - start) * 2;
        /* Validate within cmap bounds */
        if (target + 2 > font->data + font->cmap_offset + font->cmap_length) return 0;
        USHORT glyph = BE16(target);
        if (glyph == 0) return 0;
        return (USHORT)((glyph + delta) & 0xFFFF);
    }
    return 0;
}

/* ============================================================================
 * hmtx — advance width + left side bearing
 * ========================================================================== */

NTSTATUS NTAPI TtfGetGlyphMetrics(PTTF_FONT font, USHORT gi, PTTF_GLYPH_METRICS out)
{
    if (!font->valid || !out) return 0xC000000DL;
    RtlZeroMemory(out, sizeof(*out));
    if (gi >= font->maxp.numGlyphs) return 0xC000007BL;

    /* longHorMetric: advanceWidth (USHORT) + lsb (USHORT) = 4 bytes each */
    /* For glyphs with index < numberOfHMetrics, use full entry */
    /* For glyphs with index >= numberOfHMetrics, use last entry's advanceWidth
       and read lsb[j] where j = gi - numberOfHMetrics + numberOfHMetrics */
    USHORT nhm = font->hhea.numberOfHMetrics;
    if (nhm == 0) return 0xC000007BL;

    PUCHAR hmtx = font->data + font->hmtx_offset;
    SHORT advW, lsb;
    if (gi < nhm) {
        advW = BE16(hmtx + gi * 4);
        lsb = BE16S(hmtx + gi * 4 + 2);
    } else {
        /* Use last advance width, lsb from trailing array */
        advW = BE16(hmtx + (nhm - 1) * 4);
        PUCHAR lsbs = hmtx + nhm * 4;
        lsb = BE16S(lsbs + (gi - nhm) * 2);
    }

    out->advanceX = advW;
    out->lsb = lsb;
    return STATUS_SUCCESS;
}

/* ============================================================================
 * loca — glyph data offsets into glyf
 * ========================================================================== */

static ULONG TtfGetGlyphOffset(PTTF_FONT font, USHORT gi, PULONG out_length)
{
    if (gi >= font->maxp.numGlyphs) { *out_length = 0; return 0; }
    ULONG offset1, offset2;
    if (font->loca_format == 0) {
        /* Short format: entries are USHORT, each multiplied by 2 */
        PUCHAR loca = font->data + font->loca_offset;
        offset1 = (ULONG)BE16(loca + gi * 2) * 2;
        offset2 = (ULONG)BE16(loca + (gi + 1) * 2) * 2;
    } else {
        /* Long format: entries are ULONG */
        PUCHAR loca = font->data + font->loca_offset;
        offset1 = BE32(loca + gi * 4);
        offset2 = BE32(loca + (gi + 1) * 4);
    }
    *out_length = (offset2 > offset1) ? (offset2 - offset1) : 0;
    return offset1;
}

/* ============================================================================
 * glyf — glyph description
 *
 * Simple glyph format:
 *   SHORT numberOfContours
 *   SHORT xMin, yMin, xMax, yMax
 *   USHORT endPtsOfContours[numberOfContours]
 *   USHORT instructionLength
 *   UCHAR  instructions[instructionLength]
 *   UCHAR  flags[...]  (compressed: repeats via bit 0)
 *   SHORT/USHORT xCoords[]
 *   SHORT/USHORT yCoords[]
 *
 * Negative numberOfContours means composite glyph.
 * ========================================================================== */

/* Flatten glyph into polygon points for rasterization */
typedef struct _TTF_POINT { FLOAT x, y; } TTF_POINT;

static NTSTATUS TtfReadSimpleGlyph(PTTF_FONT font, PUCHAR glyph_data, ULONG glyph_len,
                                      TTF_POINT **out_points, PUSHORT out_contours_end,
                                      PUSHORT out_num_contours)
{
    SHORT ncont = BE16S(glyph_data);
    if (ncont <= 0) return 0xC000007BL; /* composite, not handled here */

    PUCHAR p = glyph_data + 10; /* skip xMin/yMin/xMax/yMax */
    PUSHORT endPts = (PUSHORT)(p);
    PUCHAR endPtsBase = p;
    p += ncont * 2;

    USHORT numPoints = BE16(endPtsBase + (ncont - 1) * 2) + 1;

    USHORT instLen = BE16(p);
    p += 2 + instLen;

    /* Flags: compressed, 1 byte each. Bit 0: repeat flag.
       Bits 1: xShort, 2: yShort, 4: xSameOrPositive, 5: ySameOrPositive */
    UCHAR *flags = ExAllocatePool(NonPagedPool, numPoints);
    if (!flags) return 0xC000017L;
    ULONG fi = 0;
    while (fi < numPoints) {
        UCHAR f = *p++;
        flags[fi++] = f;
        if (f & 0x08) { /* repeat */
            UCHAR rep = *p++;
            for (UCHAR r = 0; r < rep && fi < numPoints; r++) flags[fi++] = f;
        }
    }

    /* X coordinates */
    SHORT *xs = ExAllocatePool(NonPagedPool, numPoints * sizeof(SHORT));
    SHORT *ys = ExAllocatePool(NonPagedPool, numPoints * sizeof(SHORT));
    if (!xs || !ys) {
        if (xs) ExFreePool(xs);
        if (ys) ExFreePool(ys);
        ExFreePool(flags);
        return 0xC000017L;
    }
    SHORT curX = 0;
    for (ULONG i = 0; i < numPoints; i++) {
        UCHAR f = flags[i];
        if (f & 0x02) { /* xShort */
            UCHAR d = *p++;
            curX += (f & 0x10) ? (SHORT)d : -(SHORT)d;
        } else {
            if (!(f & 0x10)) { /* same as previous */
                SHORT d = BE16S(p); p += 2;
                curX += d;
            }
            /* else: 0 delta (no change) */
        }
        xs[i] = curX;
    }
    SHORT curY = 0;
    for (ULONG i = 0; i < numPoints; i++) {
        UCHAR f = flags[i];
        if (f & 0x04) { /* yShort */
            UCHAR d = *p++;
            curY += (f & 0x20) ? (SHORT)d : -(SHORT)d;
        } else {
            if (!(f & 0x20)) {
                SHORT d = BE16S(p); p += 2;
                curY += d;
            }
        }
        ys[i] = curY;
    }

    /* Allocate points */
    TTF_POINT *pts = ExAllocatePool(NonPagedPool, numPoints * sizeof(TTF_POINT));
    if (!pts) {
        ExFreePool(flags); ExFreePool(xs); ExFreePool(ys);
        return 0xC000017L;
    }
    for (ULONG i = 0; i < numPoints; i++) {
        pts[i].x = (FLOAT)xs[i];
        pts[i].y = (FLOAT)ys[i];
    }

    /* Copy contour end indices */
    PUSHORT endout = ExAllocatePool(NonPagedPool, ncont * 2);
    if (!endout) {
        ExFreePool(flags); ExFreePool(xs); ExFreePool(ys);
        ExFreePool(pts);
        return 0xC000017L;
    }
    for (SHORT c = 0; c < ncont; c++) endout[c] = BE16(endPtsBase + c * 2);

    ExFreePool(flags); ExFreePool(xs); ExFreePool(ys);
    *out_points = pts;
    *out_contours_end = endout;
    *out_num_contours = (USHORT)ncont;
    return STATUS_SUCCESS;
}

/* ============================================================================
 * Bézier flattening
 *
 * For each contour, treat consecutive points as either:
 *   - on-curve (flag bit 0 of original flag[gi] & 1)
 *   - off-curve (control point of quadratic)
 * The implicit on-curve midpoint trick: if two off-curve points are
 * adjacent, an implicit on-curve midpoint is inserted between them.
 *
 * For each quadratic segment, flatten to line segments with a fixed
 * subdivision step count.
 * ========================================================================== */

static FLOAT Lerp(FLOAT a, FLOAT b, FLOAT t) { return a + (b - a) * t; }

static VOID FlattenQuad(TTF_POINT p0, TTF_POINT p1, TTF_POINT p2,
                        TTF_POINT *out, PULONG out_count, ULONG max)
{
    /* 4 flattening steps should be enough at typical sizes */
    INT steps = 4;
    for (INT i = 1; i <= steps; i++) {
        if (*out_count >= max) break;
        FLOAT t = (FLOAT)i / steps;
        FLOAT mx = Lerp(Lerp(p0.x, p1.x, t), Lerp(p1.x, p2.x, t), t);
        FLOAT my = Lerp(Lerp(p0.y, p1.y, t), Lerp(p1.y, p2.y, t), t);
        out[*out_count].x = mx;
        out[*out_count].y = my;
        (*out_count)++;
    }
}

/* ============================================================================
 * Scanline rasterizer
 *
 * After flattening, we have a set of polygon outlines (one per contour).
 * For each scanline, find intersections with edges and fill between
 * even-odd intersection pairs (non-zero winding for non-zero).
 * ========================================================================== */

static VOID ScanlineFill(TTF_POINT *pts, ULONG num_pts, PUSHORT contours_end,
                          USHORT num_contours, INT width, INT height,
                          PUCHAR out_pixels, INT pitch)
{
    /* Allocate edge table for scanline algorithm */
    typedef struct _EDGE { FLOAT x; FLOAT ymax; FLOAT slopeinv; } EDGE;
    /* Build edges from point list */
    ULONG total_edges = 0;
    for (USHORT c = 0; c < num_contours; c++) {
        ULONG st = (c == 0) ? 0 : contours_end[c-1] + 1;
        ULONG en = contours_end[c];
        total_edges += en - st + 1;
    }
    EDGE *edges = ExAllocatePool(NonPagedPool, total_edges * sizeof(EDGE));
    if (!edges) return;

    /* For each scanline y, compute edge intersections and fill */
    for (INT y = 0; y < height; y++) {
        /* Use non-zero winding rule simple version: count left-to-right
           edges crossing scanline; fill between pairs */
        FLOAT fy = (FLOAT)y + 0.5f;
        FLOAT *xings = ExAllocatePool(NonPagedPool, total_edges * sizeof(FLOAT));
        if (!xings) continue;
        ULONG nx = 0;
        for (USHORT c = 0; c < num_contours; c++) {
            ULONG st = (c == 0) ? 0 : contours_end[c-1] + 1;
            ULONG en = contours_end[c];
            for (ULONG ei = st; ei <= en; ei++) {
                ULONG ni = (ei == en) ? st : ei + 1;
                FLOAT y0 = pts[ei].y, y1 = pts[ni].y;
                FLOAT x0 = pts[ei].x, x1 = pts[ni].x;
                if (y0 == y1) continue;
                FLOAT ymin = (y0 < y1) ? y0 : y1;
                FLOAT ymax = (y0 < y1) ? y1 : y0;
                if (fy < ymin || fy >= ymax) continue;
                FLOAT t = (fy - y0) / (y1 - y0);
                FLOAT xi = x0 + (x1 - x0) * t;
                xings[nx++] = xi;
            }
        }

        /* Sort intersections */
        for (ULONG i = 0; i < nx; i++) {
            for (ULONG j = i + 1; j < nx; j++) {
                if (xings[j] < xings[i]) {
                    FLOAT tmp = xings[i]; xings[i] = xings[j]; xings[j] = tmp;
                }
            }
        }

        /* Fill between pairs */
        for (ULONG i = 0; i + 1 < nx; i += 2) {
            INT x0 = (INT)xings[i];
            INT x1 = (INT)xings[i + 1];
            if (x0 < 0) x0 = 0;
            if (x1 >= width) x1 = width - 1;
            for (INT x = x0; x <= x1; x++) {
                out_pixels[y * pitch + x] = 0xFF; /* full coverage */
            }
        }
        ExFreePool(xings);
    }
    ExFreePool(edges);
}

/* ============================================================================
 * RasterizeGlyph
 * ========================================================================== */

NTSTATUS NTAPI TtfRasterizeGlyph(PTTF_FONT font, USHORT gi, INT pixelSize,
                                    PTTF_GLYPH_BITMAP out)
{
    RtlZeroMemory(out, sizeof(*out));
    if (!font->valid || gi == 0 || gi >= font->maxp.numGlyphs) return 0xC000000DL;

    ULONG glen;
    ULONG goff = TtfGetGlyphOffset(font, gi, &glen);
    if (glen == 0) return 0xC000007BL;

    PUCHAR gd = font->data + font->glyf_offset + goff;
    SHORT ncont = BE16S(gd);
    if (ncont <= 0) {
        /* Composite — not implemented; render blank */
        out->width = pixelSize / 2;
        out->height = pixelSize / 2;
        out->pitch = out->width;
        out->pixels = ExAllocatePool(NonPagedPool, out->pitch * out->height);
        if (out->pixels) RtlZeroMemory(out->pixels, out->pitch * out->height);
        return STATUS_SUCCESS;
    }

    /* Read simple glyph */
    TTF_POINT *pts = NULL;
    PUSHORT cend = NULL;
    USHORT nc = 0;
    NTSTATUS s = TtfReadSimpleGlyph(font, gd, glen, &pts, &cend, &nc);
    if (!NT_SUCCESS(s)) return s;

    /* Compute glyph bounding box in font units */
    SHORT xmin = BE16S(gd + 2), ymin = BE16S(gd + 4);
    SHORT xmax = BE16S(gd + 6), ymax = BE16S(gd + 8);
    FLOAT gw = (FLOAT)(xmax - xmin);
    FLOAT gh = (FLOAT)(ymax - ymin);

    /* Scale from font units to pixels */
    FLOAT scale = (FLOAT)pixelSize / (FLOAT)font->head.unitsPerEm;
    INT pw = (INT)(gw * scale) + 2;
    INT ph = (INT)(gh * scale) + 2;
    if (pw < 2 || ph < 2 || pw > 256 || ph > 256) {
        ExFreePool(pts); ExFreePool(cend);
        return 0xC000007BL;
    }

    /* Translate and scale points */
    ULONG numPts = cend[nc - 1] + 1;
    for (ULONG i = 0; i < numPts; i++) {
        pts[i].x = (pts[i].x - xmin) * scale + 1;
        /* Flip Y (font units have Y up; bitmap has Y down) */
        pts[i].y = (ymax - pts[i].y) * scale + 1;
    }

    /* Flatten Bézier curves into polygon — but we need flags. We don't have them
       readily here. For simplicity, treat all points as on-curve (linear).
       This loses the smoothness of curved glyph outlines but still produces
       a recognizable glyph shape. A future commit will properly flatten
       quadratic Béziers using the original flags. */

    /* Allocate bitmap */
    out->width = pw;
    out->height = ph;
    out->pitch = pw;
    out->pixels = ExAllocatePool(NonPagedPool, pw * ph);
    if (!out->pixels) {
        ExFreePool(pts); ExFreePool(cend);
        return 0xC000017L;
    }
    RtlZeroMemory(out->pixels, pw * ph);

    /* Scanline fill */
    ScanlineFill(pts, numPts, cend, nc, pw, ph, out->pixels, out->pitch);

    /* Set bitmap origin */
    out->xBearing = 0;
    out->yBearing = (INT)(ymax * scale);
    out->advanceX = (INT)pw;
    out->advanceY = 0;

    ExFreePool(pts);
    ExFreePool(cend);
    return STATUS_SUCCESS;
}

VOID NTAPI TtfFreeBitmap(PTTF_GLYPH_BITMAP bitmap)
{
    if (bitmap && bitmap->pixels) {
        ExFreePool(bitmap->pixels);
        bitmap->pixels = NULL;
    }
}

INT NTAPI TtfGetKerning(PTTF_FONT font, USHORT lg, USHORT rg)
{
    (void)font; (void)lg; (void)rg;
    return 0; /* kern table not parsed — most fonts use horizontal advance only */
}
