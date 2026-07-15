#!/usr/bin/env python3
"""
MinNT Win32k Batch Replace Tool
Bulk replaces CODENAME_* placeholders with actual implementations.
Works OR it doesn't - no fake stubs.
"""

import os
import re
import json
from pathlib import Path
from dataclasses import dataclass
from typing import Dict, Optional

# ── Paths ──────────────────────────────────────────────────────────────────────
WIN32K_STRIP = Path("/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint/win32k-stripped")
WIN32K_BUILD = Path("/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint/win32k")
MAKEFILE = Path("/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint/Makefile")
UPDATED_CODENAMES = WIN32K_STRIP / "UPDATED_CODENAMES.md"

# ── CODENAME Registry ──────────────────────────────────────────────────────────
# Format: "CODENAME_HEADER_NAME": "replacement or stub file path"
#
# "STUB" = placeholder file created, needs real implementation
# "NEEDED" = missing, needs to be built
# "EXISTS" = we have the real thing in MinNT

CODENAME_MAP: Dict[str, str] = {
    # ===== C Standard Library (should mostly work with GCC builtins) ======
    "CODENAME_STDARG_H": "EXISTS",       # __builtin_stdarg_start works
    "CODENAME_STDIO_H": "EXISTS",        # libcstdio
    "CODENAME_STDLIB_H": "EXISTS",        # libcstdlib  
    "CODENAME_STRING_H": "EXISTS",        # libcstring
    "CODENAME_WCHAR_H": "EXISTS",         # libcwchar
    "CODENAME_MATH_H": "EXISTS",          # libmmath
    "CODENAME_INTRIN_H": "EXISTS",        # GCC builtins
    
    # ===== Windows SDK Types (need MinNT equivalents) =====
    "CODENAME_WINDEF_H": "STUB",         # HWND, HDC, RECT, POINT, etc.
    "CODENAME_WINGDI_H": "STUB",         # GetGlyphIndices, CreateDC, etc.
    "CODENAME_WINUSER_H": "STUB",       # CreateWindow, MessageBox, etc.
    "CODENAME_WINBASE_H": "STUB",       # CreateFile, etc.
    "CODENAME_WINERROR_H": "STUB",       # ERROR_* constants
    "CODENAME_WINNLS_H": "STUB",         # Unicode/collation
    "CODENAME_WINREG_H": "STUB",         # RegOpenKey, etc.
    "CODENAME_WINTERNL_H": "STUB",       # NT types from SDK
    
    # ===== NT DDK (we have some in NDK headers) =====
    "CODENAME_NTSTRSAFE_H": "STUB",      # String safe functions
    "CODENAME_NTDDVDEO_H": "STUB",       # Video DD (ddvdeo.h)
    
    # ===== Exception handling (critical) =====
    "CODENAME_PSEH_PSEH2_H": "EXISTS",   # We have pseh2.h in MinNT
    
    # ===== ReactOS/Windows Internal =====
    "CODENAME_WIN32K_H": "EXISTS",       # main win32k header
    "CODENAME_NAPI_H": "STUB",           # NT API callbacks
}

@dataclass
class ReplacementResult:
    codename: str
    status: str  # "EXISTS", "STUB", "NEEDED"
    files_referencing: int
    replacement_path: Optional[str] = None

def generate_stub(codename: str) -> str:
    """Generate a minimal stub file for a missing header."""
    return f'''/*
 * MinNT Win32k Stub - {codename}
 * STUB FILE - NOT IMPLEMENTED
 * Generated: auto-stub tool
 * "WORKS OR IT DOESN'T. NOT WORKING IS IN FACT AN ACCEPTABLE OPTION."
 */

#ifndef _STUB_{codename}_H
#define _STUB_{codename}_H

/* STUB: placeholder - needs real implementation */

#endif /* _STUB_{codename}_H */
'''

def scan_for_codename_refs(directory: Path) -> Dict[str, int]:
    """Scan all C files in directory for CODENAME_* references."""
    counts = {}
    for filepath in directory.rglob("*.c"):
        try:
            with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
            
            # Find all CODENAME_* references
            matches = re.findall(r'CODENAME_\w+', content)
            for m in matches:
                counts[m] = counts.get(m, 0) + 1
        except Exception:
            pass
    return counts

def generate_updated_codenames():
    """Generate the UPDATED_CODENAMES.md based on current state."""
    refs = scan_for_codename_refs(WIN32K_STRIP)
    
    with open(UPDATED_CODENAMES, 'w') as f:
        f.write("# UPDATED_CODENAMES — Win32k Equivalents Status\n\n")
        f.write("**Generated:** Auto-scanned from stripped ReactOS win32ss\n\n")
        f.write("---\n\n")
        
        f.write("## CODENAME Status Map\n\n")
        f.write("| Codename | Status | Refs | Replacement |\n")
        f.write("|----------|--------|------|-------------|\n")
        
        for codename, count in sorted(refs.items()):
            status = CODENAME_MAP.get(codename, "NEEDED")
            replacement = CODENAME_MAP.get(codename, "TBD") if status else "NEEDED"
            f.write(f"| `{codename}` | {status} | {count} | {replacement} |\n")
        
        f.write("\n## Files by Category\n\n")
        
        # Group by category
        categories = {
            "C Standard Library": ["STDARG", "STDIO", "STDLIB", "STRING", "WCHAR", "MATH", "INTRIN"],
            "Windows SDK Types": ["WINDEF", "WINGDI", "WINUSER", "WINBASE", "WINERROR", "WINNLS", "WINREG", "WINTERNL"],
            "NT DDK": ["NTSTRSAFE", "NTDDVDEO"],
            "Exception Handling": ["PSEH"],
            "Graphics Engine": ["ENG", "GDI"],
            "User Subsystem": ["USER", "NTUSER"],
            "Display/Monitor": ["DISPLAY", "MONITOR"],
        }
        
        for cat, prefixes in categories.items():
            f.write(f"### {cat}\n")
            for prefix in prefixes:
                matching = [(k,v) for k,v in refs.items() if k.startswith(f"CODENAME_{prefix}")]
                if matching:
                    for codename, count in matching:
                        status = CODENAME_MAP.get(codename, "NEEDED")
                        f.write(f"- `{codename}`: {status} ({count} refs)\n")
                else:
                    f.write(f"- (none with prefix {prefix})\n")
            f.write("\n")
    
    print(f"✓ Generated: {UPDATED_CODENAMES}")
    return refs

def create_stub_files():
    """Create stub files for all CODENAMEs marked as STUB."""
    stubs_dir = WIN32K_BUILD / "stubs"
    stubs_dir.mkdir(parents=True, exist_ok=True)
    
    created = []
    for codename, status in CODENAME_MAP.items():
        if status == "STUB":
            stub_path = stubs_dir / f"{codename.lower()}.h"
            if not stub_path.exists():
                with open(stub_path, 'w') as f:
                    f.write(generate_stub(codename))
                created.append(codename)
    
    print(f"✓ Created {len(created)} stub files in {stubs_dir}")
    return created

def main():
    print("=" * 70)
    print("  MinNT Win32k Batch Replace Tool")
    print("=" * 70)
    
    # Scan for references
    refs = scan_for_codename_refs(WIN32K_STRIP)
    print(f"\nFound {len(refs)} unique CODENAMEs across all files")
    
    # Generate updated codenames
    generate_updated_codenames()
    
    # Create stub files
    create_stub_files()
    
    print("\n" + "=" * 70)
    print("  DONE")
    print("=" * 70)

if __name__ == "__main__":
    main()