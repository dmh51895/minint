#!/usr/bin/env python3
"""
MinNT Win32k Code Analyzer
Extracts types, functions, and constants from stripped ReactOS code.
Maps them to MinNT equivalents or marks as CODENAME needed.
"""

import os
import re
from pathlib import Path
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, Set, List

# ── Paths ──────────────────────────────────────────────────────────────────────
WIN32K_STRIP = Path("/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint/win32k-stripped")
MINNT_INC = Path("/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint/include")
OUTPUT = Path("/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint/win32k-stripped/ANALYSIS.md")

# ── MinNT Known Types (we have these) ─────────────────────────────────────────
# These are already defined in MinNT headers
MINNT_TYPES: Set[str] = {
    # Base NT types
    "VOID", "PVOID", "LPVOID", "void",
    "BOOLEAN", "PBOOLEAN",
    "CHAR", "PCHAR", "UCHAR", "PUCHAR",
    "SHORT", "PSHORT", "USHORT", "PUSHORT",
    "LONG", "PLONG", "ULONG", "PULONG", "LONG32", "ULONG32",
    "LONG64", "ULONG64", "LONGLONG", "ULONGLONG",
    "INT", "PINT", "UINT", "PUINT", "UINT32", "UINT64",
    "SIZE_T", "PSIZE_T", "SSIZE_T",
    "WCHAR", "PWCHAR",
    
    # Pointer types
    "PVOID", "LPVOID",
    "PULONG", "PULONG64",
    "PULONG_PTR", "PULONG64",
    
    # NT-specific types we have
    "NTSTATUS", "PNTSTATUS",
    "HANDLE", "PHANDLE",
    "UNICODE_STRING", "PUNICODE_STRING", "PCUNICODE_STRING",
    "LIST_ENTRY", "PLIST_ENTRY",
    "KIRQL", "PKIRQL",
    "KSPIN_LOCK", "PKSPIN_LOCK",
    "OBJECT_ATTRIBUTES", "POBJECT_ATTRIBUTES",
    "PHYSICAL_ADDRESS",
    
    # Structures we have
    "KPCR", "PKPCR", "KPRCB", "PKPRCB",
    "KTHREAD", "PKTHREAD", "ETHREAD", "PETHREAD",
    "KPROCESS", "PKPROCESS", "EPROCESS", "PEPROCESS",
    "KEVENT", "PKEVENT", "KSEMAPHORE", "PKSEMAPHORE",
    "KMUTANT", "PKMUTANT", "KMUTEX", "PKMUTEX",
    "KTRAP_FRAME", "PKTRAP_FRAME",
    "DISPATCHER_HEADER", "PDISPATCHER_HEADER",
    "LARGE_INTEGER", "PLARGE_INTEGER",
    "RTL_AVL_TABLE", "PRTL_AVL_TABLE",
    "SECURITY_DESCRIPTOR", "PACL",
    
    # HAL types
    "KDPC", "PKDPC",
    
    # Pool tags
    "ULONG_PTR", "LONG_PTR",
    
    # Return types
    "TRUE", "FALSE", "NULL",
    "STATUS_SUCCESS", "STATUS_UNSUCCESSFUL",
    "STATUS_NO_MEMORY", "STATUS_ACCESS_DENIED",
    "STATUS_INVALID_HANDLE", "STATUS_OBJECT_NAME_NOT_FOUND",
}

# Functions we have in MinNT
MINNT_FUNCTIONS: Set[str] = {
    # Kernel
    "KeInitializeGdt", "KeInitializeIdt", "KeBugCheckEx", "KeGetPcr",
    "KfRaiseIrql", "KfLowerIrql", "KeStallExecutionProcessor",
    "KeInitializeSpinLock", "KeAcquireSpinLockRaiseToDpc", "KeReleaseSpinLock",
    "KeGetCurrentThread", "KeGetCurrentPrcb", "KeGetCurrentIrql",
    "KeDisableInterrupts", "KeEnableInterrupts", "KeHaltProcessor",
    "KeInitializeEvent", "KeSetEvent", "KeResetEvent", "KeWaitForSingleObject",
    "KeInitializeMutex", "KeReleaseMutex",
    "KeInitializeSemaphore", "KeReleaseSemaphore",
    "KeTickCount", "KeInitializeDispatcher",
    
    # Memory
    "MmInitSystem", "MmAllocatePhysicalPage", "MmFreePhysicalPage",
    "MmGetFreePages", "MmGetTotalPages", "MmMapPage",
    "MmAllocateVirtualMemory", "MmFreeVirtualMemory",
    
    # Object Manager
    "ObInitSystem", "ObCreateObject", "ObCreateObjectType",
    "ObReferenceObject", "ObDereferenceObject",
    "ObInsertHandle", "ObCloseHandle", "ObReferenceObjectByHandle",
    "ObLookupObjectByName",
    
    # Process/Thread
    "PsInitSystem", "PsCreateSystemThread", "PsTerminateThread",
    "PsGetCurrentProcess", "PsGetCurrentThread",
    "KiSwapContext", "KiDispatchNextThread", "KiDispatchInterrupt",
    "KiSetTssRsp0", "KiSystemCall64", "KiEnterUserMode",
    
    # Executive
    "ExInitializePoolManager", "ExAllocatePoolWithTag", "ExFreePoolWithTag",
    "ExAllocatePool", "ExFreePool",
    
    # I/O
    "IoInitSystem", "IoCreateDevice", "IoDeleteDevice",
    "IoCallDriver", "IoCompleteRequest",
    
    # RTL
    "RtlInitUnicodeString", "RtlCopyUnicodeString", "RtlCompareUnicodeString",
    "RtlEqualUnicodeString", "RtlZeroMemory", "RtlCopyMemory",
    "RtlAnsiStringToUnicodeString", "RtlUnicodeStringToAnsiString",
    "RtlCreateUnicodeString", "RtlFreeUnicodeString",
    "RtlAllocateAndInitializeSid", "RtlFreeSid",
    "DbgPrint",
    
    # Cm (Registry)
    "CmInitSystem", "CmCreateKey", "CmOpenKey", "CmSetValue", "CmQueryValue",
    
    # Se (Security)  
    "SeInitSystem", "SeAccessCheck",
    
    # Lpc
    "LpcInitSystem", "LpcConnectPort", "LpcSendRequest",
    
    # Fs
    "FsInitSystem", "FsCreateRamDisk", "FsMountFat16",
}

# ── Win32k/GDI types we need (MISSING in MinNT) ────────────────────────────────
# These need codenames because they're Windows GUI specific
MISSING_TYPES: Set[str] = {
    # DC (Device Context) types
    "HDC", "PDC", "HDC__",  # Handle to Device Context
    "HBITMAP", "PHBITMAP",
    "HPEN", "HPEN__",
    "HBRUSH", "HBRUSH__",
    "HRGN", "HRGN__",
    "HFONT", "HFONT__",
    "HPALETTE", "HPALETTE__",
    "HRGN", "HRGN__",
    "HICON", "HICON__",
    "HCURSOR", "HCURSOR__",
    "HMENU", "HMENU__",
    "HWND", "HWND__",  # Window handle
    "HINSTANCE", "HINSTANCE__",
    "HMODULE", "HMODULE__",
    "WPARAM", "LPARAM", "LRESULT",  # Window message params
    "ATOM",  # Atom (window class registration)
    
    # DC state structures
    "PDC_ATTR", "Dcattr_t",
    "DCLEVEL", "DCP_LEVEL",
    "BRUSHOBJ", "PBRUSHOBJ", "BRUSHOBJ_DENamed",
    "PALOBJ", "PPALOBJ",
    "FONTOBJ", "LFONTOBJ", "PFONTOBJ",
    "PENOBJ", "PPENOBJ",
    "SURFOBJ", "PSURFOBJ", "SURF_MAXB", 
    "BASEOBJECT", "PBASEOBJECT",
    "XFORMOBJ", "PXFORMOBJ",
    "CLIPOBJ", "PCLIPOBJ",
    "REGION", "PREGION",
    "RECTL", "PRECTL", "LPRECTL",  # RECTL = long RECT
    "POINTL", "PPOINTL",
    "SIZEL", "PSIZEL",
    "XLATEOBJ", "PXLATEOBJ",
    "COLORREF",  # 0x00BBGGRR
    "COLOR16",  # 16-bit color component
    
    # GDI functions return types
    "GetGlyphIndicesW", "GetGlyphIndicesA",
    "CreateDCW", "CreateICA", "CreateCompatibleDC",
    "DeleteDC", "DeleteObject",
    "SelectObject", "GetDC", "ReleaseDC",
    "BitBlt", "StretchBlt", "PatBlt",
    "TransparentBlt", "AlphaBlend",
    "GetDIBits", "SetDIBits",
    "CreateCompatibleBitmap", "CreateBitmap",
    "CreatePen", "CreateSolidBrush", "CreatePatternBrush",
    "ExtCreatePen", "GetObjectW", "GetObjectA",
    "SetBkColor", "SetTextColor", "SetBkMode",
    "GetTextExtentPoint32W", "GetTextExtentPoint32A",
    "ExtTextOutW", "ExtTextOutA",
    "DrawTextW", "DrawTextA",
    "CreateFontW", "CreateFontExW",
    "SetMapMode", "SetViewportOrgEx", "SetWindowOrgEx",
    "SaveDC", "RestoreDC",
    
    # Window/Message types
    "MSG", "PMSG", "LPMSG",
    "WNDCLASSEXW", "WNDCLASSEXW__",
    "CREATESTRUCTW", "PCREATESTRUCTW",
    "NMHDR", "PNMHDR",
    "RECT", "PRECT", "LPRECT", "PRECT",  # Regular RECT (ints)
    "POINT", "PPOINT", "LPPOINT",
    "SIZE", "PSIZE", "LPSIZE",
    "PAINTSTRUCT", "PPAINTSTRUCT",
    "WINDOWPLACEMENT", "PWINDOWPLACEMENT",
    "MONITORINFOEXW", "PMONITORINFOEXW",
    
    # Constants we need
    "WM_PAINT", "WM_CREATE", "WM_DESTROY", "WM_CLOSE", "WM_QUIT",
    "WM_SIZE", "WM_MOVE", "WM_SHOWWINDOW",
    "WM_KEYDOWN", "WM_KEYUP", "WM_CHAR", "WM_DEADCHAR",
    "WM_MOUSEMOVE", "WM_LBUTTONDOWN", "WM_LBUTTONUP", "WM_RBUTTONDOWN", "WM_RBUTTONUP",
    "BN_CLICKED", "BN_DBLCLK", "BN_DISABLE",
    "CBS_DROPDOWN", "CBS_DROPDOWNLIST", "CBS_SIMPLE", "CBS_HASSTRINGS",
    "SS_LEFT", "SS_CENTER", "SS_RIGHT", "SS_ICON", "SS_BITMAP",
    "ES_LEFT", "ES_CENTER", "ES_RIGHT", "ES_MULTILINE", "ES_AUTOHSCROLL",
    "WS_VISIBLE", "WS_DISABLED", "WS_CHILD", "WS_POPUP", "WS_OVERLAPPED",
    "WS_MINIMIZE", "WS_MAXIMIZE", "WS_CAPTION", "WS_BORDER", "WS_THICKFRAME",
    "WS_HSCROLL", "WS_VSCROLL", "WS_SYSMENU", "WS_MINIMIZEBOX", "WS_MAXIMIZEBOX",
    "CBS_DROPDOWN", "CBS_DROPDOWNLIST", "CBS_SIMPLE", "CBS_HASSTRINGS",
    "DIB_RGB_COLORS", "DIB_PAL_COLORS",
    "SRCCOPY", "SRCPAINT", "SRCAND", "SRCINVERT", "SRCERASE",
    "PATCOPY", "PATPAINT", "PATAND", "PATINVERT", "MERGECOPY", "MERGEPAINT",
    "CAPTUREBLT",
    "BLACKNESS", "WHITENESS", "NOMIRRORBITMAP",
    "GMEM_MOVEABLE", "GMEM_ZEROINIT", "GHND", "GPTR",
    
    # GDI attribute flags
    "DC_BRUSH", "DC_PEN",
    "DIRTY_FILL", "DIRTY_LINE", "DIRTY_TEXT", "DIRTY_BACKGROUND",
    "DC_DIRTY_RAO",
    "RAO_TOP", "RAO_BOTTOM", "RAO_BOTH",
    
    # Stock objects
    "WHITE_BRUSH", "LTGRAY_BRUSH", "GRAY_BRUSH", "DKGRAY_BRUSH", "BLACK_BRUSH",
    "NULL_BRUSH", "HOLLOW_BRUSH",
    "WHITE_PEN", "BLACK_PEN", "NULL_PEN",
    "OEM_FIXED_FONT", "ANSI_FIXED_FONT", "ANSI_VAR_FONT",
    "SYSTEM_FONT", "DEVICE_DEFAULT_FONT", "DEFAULT_PALETTE",
    
    # Error codes
    "ERROR_INVALID_HANDLE", "ERROR_NOT_ENOUGH_MEMORY",
    "ERROR_INVALID_WINDOW_STYLE", "ERROR_CLASS_HAS_WINDOWS",
}

def extract_symbols(filepath: Path) -> dict:
    """Extract type references, function calls, and constants from a C file."""
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    
    # Remove comments
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    content = re.sub(r'//.*?$', '', content, flags=re.MULTILINE)
    
    symbols = {
        'types': set(),
        'functions': set(),
        'constants': set(),
        'structs': set(),
    }
    
    # Find types (capitalized identifiers that look like types)
    # Patterns: struct _X, typedef enum _X, typedef struct _X, etc.
    type_pattern = r'\b(?:struct|enum|union|typedef\s+(?:struct|enum|union))?\s+_?([A-Z][a-zA-Z0-9_]+)\s*[\[{]'
    for match in re.finditer(type_pattern, content):
        symbols['types'].add(match.group(1))
    
    # Find function definitions (ReturnType FuncName(...) { or FuncName(args))
    func_def_pattern = r'\b([a-zA-Z_][a-zA-Z0-9_]*)\s*\([^)]*\)\s*(?:{|;)'
    for match in re.finditer(func_def_pattern, content):
        name = match.group(1)
        # Filter out likely non-functions
        if not name.startswith('_') and len(name) > 2:
            symbols['functions'].add(name)
    
    # Find function calls (already called functions)
    func_call_pattern = r'\b([a-z][a-zA-Z0-9_]*)\s*\('
    for match in re.finditer(func_call_pattern, content):
        name = match.group(1)
        if len(name) > 2 and not name.startswith('_'):
            symbols['functions'].add(name)
    
    # Find constants (ALL_CAPS identifiers)
    const_pattern = r'\b([A-Z][A-Z0-9_]{2,})\b'
    for match in re.finditer(const_pattern, content):
        symbols['constants'].add(match.group(1))
    
    return symbols

def categorize_symbols(symbols: dict, minnt_types: set, minnt_funcs: set, missing_types: set) -> dict:
    """Categorize symbols into known, missing, or unknown."""
    categorized = {
        'known_types': [],
        'missing_types': [],
        'unknown_types': [],
        'known_funcs': [],
        'missing_funcs': [],
        'unknown_funcs': [],
        'known_constants': [],
        'missing_constants': [],
        'unknown_constants': [],
    }
    
    for t in symbols['types']:
        if t in minnt_types or t in missing_types:
            categorized['known_types'].append(t)
        else:
            categorized['unknown_types'].append(t)
    
    for f in symbols['functions']:
        if f in minnt_funcs:
            categorized['known_funcs'].append(f)
        else:
            categorized['unknown_funcs'].append(f)
    
    for c in symbols['constants']:
        # Most ALL_CAPS are either #defines or known constants
        if c in minnt_types:
            categorized['known_constants'].append(c)
        else:
            categorized['unknown_constants'].append(c)
    
    return categorized

def analyze_all():
    """Analyze all stripped files."""
    all_symbols = defaultdict(set)
    file_results = []
    
    for category_dir in WIN32K_STRIP.rglob("*"):
        if category_dir.is_dir():
            for filepath in category_dir.rglob("*.c"):
                symbols = extract_symbols(filepath)
                categorized = categorize_symbols(symbols, MINNT_TYPES, MINNT_FUNCTIONS, MISSING_TYPES)
                
                result = {
                    'file': str(filepath.relative_to(WIN32K_STRIP)),
                    'symbols': symbols,
                    'categorized': categorized,
                }
                file_results.append(result)
                
                # Aggregate
                for key in all_symbols:
                    all_symbols[key].update(symbols[key])
    
    return file_results, all_symbols

def generate_analysis_report(file_results: list, all_symbols: dict):
    """Generate the ANALYSIS.md report."""
    with open(OUTPUT, 'w') as f:
        f.write("# MinNT Win32k Code Analysis\n\n")
        f.write("**Source:** ReactOS win32ss (stripped of headers)\n\n")
        f.write("## Philosophy\n\n")
        f.write("\"WORKS OR IT DOESN'T. NOT WORKING IS IN FACT AN ACCEPTABLE OPTION.\"\n\n")
        f.write("---\n\n")
        
        f.write("## Summary Statistics\n\n")
        f.write(f"- Files analyzed: {len(file_results)}\n")
        f.write(f"- Unique types found: {len(all_symbols['types'])}\n")
        f.write(f"- Unique functions found: {len(all_symbols['functions'])}\n")
        f.write(f"- Unique constants found: {len(all_symbols['constants'])}\n\n")
        
        # Count known vs missing
        known_types = len([t for t in all_symbols['types'] if t in MINNT_TYPES or t in MISSING_TYPES])
        unknown_types = len([t for t in all_symbols['types'] if t not in MINNT_TYPES and t not in MISSING_TYPES])
        
        known_funcs = len([fn for fn in all_symbols['functions'] if fn in MINNT_FUNCTIONS])
        unknown_funcs = len([fn for fn in all_symbols['functions'] if fn not in MINNT_FUNCTIONS])
        
        f.write("## Type Coverage\n\n")
        f.write(f"| Category | Count |\n")
        f.write(f"|----------|-------|\n")
        f.write(f"| Known (MinNT or Win32k) | {known_types} |\n")
        f.write(f"| Unknown (need research) | {unknown_types} |\n")
        f.write(f"| **Total** | {len(all_symbols['types'])} |\n\n")
        
        f.write("## Function Coverage\n\n")
        f.write(f"| Category | Count |\n")
        f.write(f"|----------|-------|\n")
        f.write(f"| Known (MinNT) | {known_funcs} |\n")
        f.write(f"| Win32k (need port) | {unknown_funcs} |\n")
        f.write(f"| **Total** | {len(all_symbols['functions'])} |\n\n")
        
        # Show sample of unknown types (first 50)
        f.write("## Unknown Types (Need Investigation)\n\n")
        unknown_list = sorted([t for t in all_symbols['types'] 
                              if t not in MINNT_TYPES and t not in MISSING_TYPES])[:100]
        for t in unknown_list:
            f.write(f"- `{t}`\n")
        if len(unknown_list) > 100:
            f.write(f"- ... and {len(unknown_list) - 100} more\n")
        f.write("\n")
        
        # Show sample of unknown functions (first 50)
        f.write("## Win32k Functions (Need Implementation)\n\n")
        unknown_funcs = sorted([fn for fn in all_symbols['functions'] 
                               if fn not in MINNT_FUNCTIONS])[:100]
        for fn in unknown_funcs:
            f.write(f"- `{fn}()`\n")
        if len(unknown_funcs) > 100:
            f.write(f"- ... and {len(unknown_funcs) - 100} more\n")
        f.write("\n")
        
        # Top unknown constants
        f.write("## Key Constants Found\n\n")
        constant_sample = sorted(list(all_symbols['constants']))[:50]
        for c in constant_sample:
            f.write(f"- `{c}`\n")
    
    print(f"✓ Generated: {OUTPUT}")
    return file_results, all_symbols

def main():
    print("=" * 70)
    print("  MinNT Win32k Code Analyzer")
    print("=" * 70)
    
    file_results, all_symbols = analyze_all()
    generate_analysis_report(file_results, all_symbols)
    
    print(f"\n✓ Analyzed {len(file_results)} files")
    print(f"  Types: {len(all_symbols['types'])}")
    print(f"  Functions: {len(all_symbols['functions'])}")
    print(f"  Constants: {len(all_symbols['constants'])}")
    
    print("\n" + "=" * 70)
    print("  DONE")
    print("=" * 70)

if __name__ == "__main__":
    main()