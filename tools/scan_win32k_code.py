#!/usr/bin/env python3
"""
MinNT Win32k Grep Scanner
Uses grep to find Win32k-specific types, functions, and constants.
"""

import subprocess
import re
from pathlib import Path
from collections import defaultdict

# ── Paths ──────────────────────────────────────────────────────────────────────
WIN32K_STRIP = Path("/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint/win32k-stripped")
OUTPUT = Path("/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint/win32k-stripped/SCAN_RESULTS.md")

# ── Key Win32k types to detect ──────────────────────────────────────────────
WIN32_TYPES = [
    "HDC", "PDC", "HDC__",
    "HBITMAP", "PHBITMAP", 
    "HPEN", "HBRUSH", "HRGN", "HFONT", "HPALETTE",
    "HWND", "HWND__", "HWND_",
    "HICON", "HCURSOR", "HMENU", "HINSTANCE", "HMODULE", "HACCEL",
    "WPARAM", "LPARAM", "LRESULT", "ATOM",
    "MSG", "PMSG", "LPMSG",
    "RECT", "PRECT", "LPRECT", "RECTL", "PRECTL",
    "POINT", "PPOINT", "LPPOINT", "POINTL", "PPOINTL",
    "SIZE", "PSIZE", "LPSIZE", "SIZEL", "PSIZEL",
    "PAINTSTRUCT", "PPAINTSTRUCT",
    "WNDCLASSEXW", "WNDCLASSEXW__",
    "CREATESTRUCTW", "NMHDR", "PNMHDR",
    "COLORREF",
    "BRUSHOBJ", "PBRUSHOBJ", "PENOBJ", "PPENOBJ",
    "FONTOBJ", "LFONTOBJ", "PFONTOBJ",
    "SURFOBJ", "PSURFOBJ", "XLATEOBJ", "PXLATEOBJ",
    "CLIPOBJ", "PCLIPOBJ", "REGION", "PREGION",
    "BASEOBJECT", "PBASEOBJECT",
    "XFORMOBJ", "PXFORMOBJ",
    "DCP_LEVEL", "PDC_ATTR",
    "GDILoObjType_", "BASEFLAG_", "GDI_OBJ_HMGR_",
    "STOCK_OBJECTS", "StockObjects",
    "GDIOBJ_", "BRUSH_", "PEN_", "SURF_", "DC_",
    "LFONT_", "PAL_", "RGN_", "CLIP_",
    "MATRIX", "FLOATOBJ_",
]

# ── Key Win32k functions ──────────────────────────────────────────────────────
WIN32_FUNCTIONS = [
    # GDI DC functions
    "GetDC", "ReleaseDC", "GetDCEx",
    "CreateDC", "CreateICA", "CreateCompatibleDC", "DeleteDC",
    "SaveDC", "RestoreDC", "ResetDC",
    "GetDeviceCaps", "GetBoundsRect", "SetBoundsRect",
    
    # GDI Object functions
    "CreateCompatibleBitmap", "CreateBitmap", "CreateSolidBrush", "CreatePatternBrush",
    "CreatePen", "ExtCreatePen", "CreateBrushIndirect", "CreateDIBPatternBrush",
    "CreateFont", "CreateFontEx", "CreateFontIndirect",
    "DeleteObject", "SelectObject", "GetObject", "GetObjectType",
    "GetStockObject", "RealizePalette", "SelectPalette",
    
    # GDI Drawing functions
    "BitBlt", "StretchBlt", "PatBlt", "MaskBlt", "PlgBlt",
    "TransparentBlt", "AlphaBlend", "GradientFill",
    "GetDIBits", "SetDIBits", "GetDIBitsToDevice", "StretchDIBits",
    "SetPixel", "GetPixel", "SetPixelV",
    "ExtTextOut", "ExtTextOutW", "ExtTextOutA",
    "DrawText", "DrawTextW", "DrawTextA", "DrawTextEx", "DrawTextExW",
    "GetTextExtentPoint", "GetTextExtentPointW", "GetTextExtentPointA",
    "GetCharWidth", "GetCharWidth32", "GetGlyphIndices",
    
    # GDI Attribute functions
    "GetBkColor", "SetBkColor", "GetBkMode", "SetBkMode",
    "GetTextColor", "SetTextColor",
    "GetPolyFillMode", "SetPolyFillMode",
    "GetROP2", "SetROP2",
    "GetStretchBltMode", "SetStretchBltMode",
    "GetMapMode", "SetMapMode",
    "GetViewportOrgEx", "SetViewportOrgEx", "GetViewportExtEx", "SetViewportExtEx",
    "GetWindowOrgEx", "SetWindowOrgEx", "GetWindowExtEx", "SetWindowExtEx",
    "GetBrushOrgEx", "SetBrushOrgEx",
    "GetMiterLimit", "SetMiterLimit",
    
    # Window functions
    "CreateWindow", "CreateWindowEx", "CreateWindowExW", "CreateWindowExA",
    "DestroyWindow", "GetWindowRect", "GetClientRect",
    "MoveWindow", "SetWindowPos", "GetWindowPos",
    "ShowWindow", "ShowWindowAsync",
    "EnableWindow", "IsWindowEnabled",
    "SetFocus", "GetFocus", "GetActiveWindow",
    "GetForegroundWindow", "SetForegroundWindow",
    "SetActiveWindow",
    "GetWindowText", "SetWindowText", "GetWindowTextLength",
    "SetWindowLong", "GetWindowLong", "SetWindowLongPtr", "GetWindowLongPtr",
    "CallWindowProc",
    
    # Message functions
    "GetMessage", "PeekMessage", "TranslateMessage", "DispatchMessage",
    "PostMessage", "PostThreadMessage", "SendMessage", "SendMessageTimeout",
    "WaitMessage",
    "TranslateAccelerator",
    "GetMessagePos", "GetMessageTime",
    "GetCursorPos", "SetCursorPos", "SetCursor", "GetCursor",
    
    # Class functions
    "RegisterClass", "RegisterClassEx", "RegisterClassExW", "UnregisterClass",
    "GetClassInfo", "GetClassInfoEx", "GetClassName",
    "SetClassLong", "GetClassLong", "SetClassLongPtr", "GetClassLongPtr",
    
    # DC attribute functions
    "GetDCBrush", "GetDCPen", "GetStockObject",
    "SetICMMode", "GetICMMode", "CheckColors",
    "ExtEscape",
    
    # Palette functions
    "CreatePalette", "GetPaletteEntries", "SetPaletteEntries",
    "AnimatePalette", "ResizePalette", "GetNearestColor",
    "GetNearestPaletteIndex", "GetSystemPaletteEntries",
    
    # Region functions
    "CreateRectRgn", "CreateRectRgnIndirect", "CreateEllipticRgn", 
    "CreateEllipticRgnIndirect", "CreatePolygonRgn",
    "CombineRgn", "EqualRgn", "OffsetRgn", "GetRgnBox",
    "RectInRegion", "PtInRegion", "FillRgn", "FrameRgn",
    "InvertRgn", "PaintRgn",
    "SelectClipRgn", "ExtSelectClipRgn", "GetClipRgn",
    
    # BitMap functions
    "CreateCompatibleBitmap", "CreateBitmap", "CreateDIBSection",
    "SetBitmapBits", "GetBitmapBits",
    "BitBlt", "StretchBlt",
]

# ── Key Win32 Constants ────────────────────────────────────────────────────────
WIN32_CONSTANTS = [
    # Window messages
    "WM_", "BN_", "CN_", "TVN_", "LVN_", "HDN_", "NM_",
    # Window styles
    "WS_", "WS_EX_",
    # Class styles
    "CS_",
    # DC constants
    "DC_", "DCT_",
    # GDI functions (newer API)
    "GDI_", "GRE_", "Eng_",
    # ROP codes
    "SRCCOPY", "SRCPAINT", "SRCAND", "SRCINVERT", "SRCERASE", "MERGECOPY", "MERGEPAINT",
    "PATCOPY", "PATPAINT", "PATAND", "PATINVERT", "BLACKNESS", "WHITENESS",
    # Stock objects
    "WHITE_BRUSH", "LTGRAY_BRUSH", "GRAY_BRUSH", "DKGRAY_BRUSH", "BLACK_BRUSH",
    "NULL_BRUSH", "HOLLOW_BRUSH", "WHITE_PEN", "BLACK_PEN", "NULL_PEN",
    "OEM_FIXED_FONT", "ANSI_FIXED_FONT", "ANSI_VAR_FONT",
    "SYSTEM_FONT", "DEVICE_DEFAULT_FONT", "DEFAULT_GUI_FONT", "DEFAULT_PALETTE",
    "DEFAULT_BITMAP",
    # Object types
    "GDILoObjType_", "GDI_OBJ_HMGR_", "BASEFLAG_", "GDI_STOCK_",
    # DIRTY flags
    "DIRTY_", "DC_DIRTY_", "RAO_",
    # Brush styles
    "BS_SOLID", "BS_HATCHED", "BS_PATTERN", "BS_NULL",
    # Pen styles
    "PS_SOLID", "PS_DASH", "PS_DOT", "PS_DASHDOT", "PS_DASHDOTDOT", "PS_NULL",
    # Hatch styles  
    "HS_", "HS_DIAGCROSS", "HS_FDIAGONAL", "HS_BDIAGONAL", "HS_HORIZONTAL", "HS_VERTICAL",
    # Map modes
    "MM_", "MM_TEXT", "MM_LOMETRIC", "MM_HIMETRIC", "MM_LOENGLISH", 
    "MM_HIENGLISH", "MM_TWIPS", "MM_ISOTROPIC", "MM_ANISOTROPIC",
    # Background modes
    "TRANSPARENT", "OPAQUE",
    # ROP2
    "R2_", "R2_BLACK", "R2_WHITE", "R2_NOT", "R2_NOTCOPYPEN", "R2_MASKPEN",
    "R2_MASKNOTPEN", "R2_MERGPEN", "R2_NOTMERGEPEN", "R2_MASKSRC",
    # Bitmap info header types
    "BI_", "DIB_", "BMF_",
    # Error codes
    "ERROR_",
]

def grep_scan(pattern: str, directory: Path) -> dict:
    """Run grep for a pattern and return file matches."""
    try:
        result = subprocess.run(
            ['grep', '-r', '-l', pattern, str(directory)],
            capture_output=True, text=True, timeout=30
        )
        files = [line.strip() for line in result.stdout.strip().split('\n') if line.strip()]
        return {pattern: files}
    except Exception as e:
        return {pattern: []}

def main():
    print("=" * 70)
    print("  MinNT Win32k Grep Scanner")
    print("=" * 70)
    
    results = {
        'types': defaultdict(list),
        'functions': defaultdict(list),
        'constants': defaultdict(list),
    }
    
    # Scan for Win32 types
    print("\nScanning for Win32 types...")
    for t in WIN32_TYPES[:30]:  # Limit to avoid timeout
        files = grep_scan(t, WIN32K_STRIP)
        if files:
            results['types'][t] = files[t]
    
    # Scan for Win32 functions
    print("Scanning for Win32 functions...")
    for fn in WIN32_FUNCTIONS[:50]:
        files = grep_scan(fn, WIN32K_STRIP)
        if files:
            results['functions'][fn] = files[fn]
    
    # Scan for Win32 constants (patterns)
    print("Scanning for Win32 constants...")
    for const in WIN32_CONSTANTS[:40]:
        files = grep_scan(const, WIN32K_STRIP)
        if files:
            results['constants'][const] = files[fn]
    
    # Generate report
    with open(OUTPUT, 'w') as f:
        f.write("# MinNT Win32k Scan Results\n\n")
        f.write("**Source:** Grep scan of stripped ReactOS win32ss\n\n")
        f.write("---\n\n")
        
        f.write("## Type References\n\n")
        f.write(f"| Type | Files Using |\n")
        f.write(f"|------|-------------|\n")
        for t, files in sorted(results['types'].items()):
            f.write(f"| `{t}` | {len(files)} |\n")
        f.write("\n")
        
        f.write(f"Total types detected: {len(results['types'])}\n\n")
        
        f.write("## Function References\n\n")
        f.write(f"| Function | Files Using |\n")
        f.write(f"|----------|-------------|\n")
        for fn, files in sorted(results['functions'].items()):
            f.write(f"| `{fn}` | {len(files)} |\n")
        f.write("\n")
        
        f.write(f"Total functions detected: {len(results['functions'])}\n\n")
        
        f.write("## Constant Pattern References\n\n")
        f.write(f"| Pattern | Files Using |\n")
        f.write(f"|---------|-------------|\n")
        for const, files in sorted(results['constants'].items()):
            f.write(f"| `{const}` | {len(files)} |\n")
        f.write("\n")
        
    print(f"\n✓ Generated: {OUTPUT}")
    print(f"  Types found: {len(results['types'])}")
    print(f"  Functions found: {len(results['functions'])}")
    print(f"  Constant patterns found: {len(results['constants'])}")
    
    print("\n" + "=" * 70)
    print("  DONE")
    print("=" * 70)

if __name__ == "__main__":
    main()