#!/usr/bin/env python3
"""
MinNT Win32k Header Stripper
Strips ALL #include lines from ReactOS win32ss C files.
Tracks dependencies for codename mapping.
"""

import os
import re
from pathlib import Path
from dataclasses import dataclass, field
from typing import Set, Dict, List
import json

# ── Paths ──────────────────────────────────────────────────────────────────────
REACTOS_WIN32SS = Path("/home/dheavy/molecular-ai-factory/Server-V2/reactos-master/win32ss")
MINNT_WIN32K_STRIP = Path("/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint/win32k-stripped")
OUTPUT_DEPS = Path("/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint/win32k-stripped/dependencies.json")
CODENAMES_TEMPLATE = Path("/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint/win32k-stripped/CODENAMES_TEMPLATE.md")
ORIG_CODENAMES = Path("/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint/win32k-stripped/ORIG_CODENAMES.md")
UPDATED_CODENAMES = Path("/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint/win32k-stripped/UPDATED_CODENAMES.md")

# ── File categories to process ───────────────────────────────────────────────
SUBDIRS = {
    "ntgdi": REACTOS_WIN32SS / "gdi" / "ntgdi",
    "ntuser": REACTOS_WIN32SS / "user" / "ntuser",
    "eng": REACTOS_WIN32SS / "gdi" / "eng",
    "gdi32": REACTOS_WIN32SS / "gdi" / "gdi32",
    "user32": REACTOS_WIN32SS / "user" / "user32",
    "winsrv": REACTOS_WIN32SS / "user" / "winsrv",
}

# ── MinNT known equivalents ────────────────────────────────────────────────────
# These headers EXIST in MinNT - we can map to them
MINNT_EQUIVALENTS = {
    # Core NT types
    "ntdef.h": "include/nt/ntdef.h",
    "ntstatus.h": "include/nt/ntdef.h",
    "ke.h": "include/nt/ke.h",
    "hal.h": "include/nt/hal.h",
    "mm.h": "include/nt/mm.h",
    "ex.h": "include/nt/ex.h",
    "ob.h": "include/nt/ob.h",
    "ps.h": "include/nt/ps.h",
    "io.h": "include/nt/io.h",
    "rtl.h": "include/nt/rtl.h",
    "cm.h": "include/nt/cm.h",
    "se.h": "include/nt/se.h",
    "fs.h": "include/nt/fs.h",
    "lpc.h": "include/nt/lpc.h",
    "usb.h": "include/nt/usb.h",
    "pe.h": "include/nt/pe.h",
    "dispatcher.h": "include/nt/dispatcher.h",
    
    # NDK headers  
    "exfuncs.h": "include/ndk/exfuncs.h",
    "iofuncs.h": "include/ndk/iofuncs.h",
    "kefuncs.h": "include/ndk/kefuncs.h",
    "obfuncs.h": "include/ndk/obfuncs.h",
    "psfuncs.h": "include/ndk/psfuncs.h",
    "sefuncs.h": "include/ndk/sefuncs.h",
    "rtlfuncs.h": "include/ndk/rtlfuncs.h",
    "cmfuncs.h": "include/ndk/cmfuncs.h",
    "lpcfuncs.h": "include/ndk/lpcfuncs.h",
    
    # ReactOS NDK extensions we have
    "setypes.h": "include/ndk/setypes.h",
}

# ── Headers that are MISSING (need codenames) ─────────────────────────────────
# These are ReactOS/Windows SDK headers we DON'T have
MISSING_HEADERS = {
    # DDK/NT status headers
    "ntifs.h": "NTIFS_DDK_FUNCTIONS",
    "ntddk.h": "NTIFS_DDK_FUNCTIONS", 
    "ntifs.h": "NTIFS_DDK_FUNCTIONS",
    "ntifs.h": "NTIFS_DDK_FUNCTIONS",
    "ntifs.h": "NTIFS_DDK_FUNCTIONS",
    
    # Windows SDK / Win32 headers
    "windef.h": "WINDEF_TYPES",
    "wingdi.h": "WINGDI_TYPES",
    "winuser.h": "WINUSER_TYPES",
    "winbase.h": "WINBASE_TYPES",
    "winerror.h": "WINERROR_CODES",
    "winnt.h": "WINNT_TYPES",
    "winioctl.h": "WINIOCTL_CODES",
    "dbt.h": "DEV_BROADCAST",
    "imm.h": "IMM32_TYPES",
    "immdev.h": "IMM32_TYPES",
    "ddrawi.h": "DDRAW_INTERNAL",
    "d3dkmddi.h": "D3DKM_DDI",
    
    # Display/Graphics headers  
    "winddi.h": "WINDDI_ENGINE",
    "ntgdityp.h": "NTGDI_TYPES",
    "ntgdi.h": "NTGDI_TYPES",
    "ntgdihdl.h": "NTGDI_HANDLES",
    "ntgdibad.h": "NTGDI_BAD",
    "ntusrtyp.h": "NTUSER_TYPES",
    "ntuser.h": "NTUSER_TYPES",
    "callback.h": "WIN32K_CALLBACKS",
    "undocuser.h": "UNDOC_USER",
    "win32k.h": "WIN32K_MAIN",
    "win32kp.h": "WIN32K_PRIVATE",
    
    # DDI/Driver interface
    "d3dkmddi.h": "D3DKM_DDI",
    "d3ddui.h": "D3DDDI_TYPES",
    "dxp64def.h": "D3D_TYPES",
    "dxgthread.h": "D3D_THREAD",
    
    # FreeType
    "ft2build.h": "FREETYPE_TYPES",
    "ftimage.h": "FREETYPE_IMAGE",
    
    # NDK/PSDK mix
    "ntstrsafe.h": "NTSTRSAFE_LIB",
    "ntintsafe.h": "NTINTSAFE_LIB",
    "ntddkbd.h": "NTDDKBD_TYPES",
    "ntddmou.h": "NTDDMOU_TYPES",
    "ntddvdeo.h": "NTDDVDO_TYPES",
    "ntdsdef.h": "NTDSS_DEF",
    "ndk/rtltypes.h": "RTL_TYPES_NDK",
    "ndk/exfuncs.h": "EXFUNCS_NDK",
    "ndk/iofuncs.h": "IOFUNCS_NDK",
    "ndk/kdfuncs.h": "KDFUNCS_NDK",
    "ndk/kefuncs.h": "KEFUNCS_NDK",
    "ndk/mmfuncs.h": "MMFUNCS_NDK",
    "ndk/obfuncs.h": "OBFUNCS_NDK",
    "ndk/psfuncs.h": "PSFUNCS_NDK",
    "ndk/sefuncs.h": "SEFUNCS_NDK",
    "ndk/rtlfuncs.h": "RTLFUNCS_NDK",
    "prntfont.h": "PRINT_FONT",
    
    # Exception handling
    "pseh/pseh2.h": "PSEH2_EXCEPTION",
    
    # ReactOS specific  
    "reactos/probe.h": "REACTOS_PROBE",
    "reactos/undoc.h": "REACTOS_UNDOC",
    "reactos/w32看电影 tv now.h": "REACTOS_W32",
    "reactos/地震.h": "REACTOS_MISC",
}

@dataclass
class FileInfo:
    original_path: Path
    stripped_path: Path
    category: str
    includes_removed: List[str] = field(default_factory=list)
    missing_includes: List[str] = field(default_factory=list)
    has_known_equiv: List[str] = field(default_factory=list)
    functions_found: List[str] = field(default_factory=list)
    structs_found: List[str] = field(default_factory=list)
    codenames_needed: List[str] = field(default_factory=list)

def strip_includes(content: str) -> tuple[List[str], List[str], List[str]]:
    """Strip #include lines. Returns (all_includes, missing_includes, known_equivs)."""
    all_includes = []
    missing_includes = []
    known_equivs = []
    
    # Match #include <something> or #include "something"
    include_pattern = re.compile(r'^\s*#\s*include\s+[<\"]', re.MULTILINE)
    
    for line in content.split('\n'):
        if line.strip().startswith('#include'):
            # Extract the header name
            match = re.search(r'#include\s+[<"]([^>"]+)[>"]', line)
            if match:
                header = match.group(1)
                all_includes.append(header)
                
                # Check if we have an equivalent
                header_basename = os.path.basename(header)
                if header_basename in MINNT_EQUIVALENTS:
                    known_equivs.append(header)
                else:
                    missing_includes.append(header)
    
    # Remove all #include lines
    stripped = re.sub(r'^\s*#\s*include\s+[<\"].*[>\"]\s*$\n', '', content, flags=re.MULTILINE)
    
    return all_includes, missing_includes, known_equivs

def extract_defines(content: str) -> List[str]:
    """Extract #define constants."""
    defines = []
    for line in content.split('\n'):
        line = line.strip()
        if line.startswith('#define ') and not line.startswith('#define WIN32'):
            parts = line.split(None, 2)
            if len(parts) >= 2:
                defines.append(parts[1])
    return defines

def extract_structs(content: str) -> List[str]:
    """Extract struct type names."""
    structs = []
    # Match typedef struct _Name { or struct _Name {
    patterns = [
        r'typedef\s+struct\s+_(\w+)\s*\{',
        r'struct\s+_(\w+)\s*\{',
        r'typedef\s+struct\s+(\w+)\s*\{',
    ]
    for p in patterns:
        matches = re.findall(p, content)
        structs.extend(matches)
    return list(set(structs))

def extract_functions(content: str) -> List[str]:
    """Extract function definitions (basic pattern)."""
    funcs = []
    # Match return_type func_name(args) or func_name(args)
    patterns = [
        r'\b(\w+)\s*\((?:[^)]*)\)\s*\{',  # function definition with body
        r'\b(\w+)\s+NTAPI\s+\w+\s*\([^)]*\)',  # NTAPI function
        r'\b(\w+)\s+CDECL\s+\w+\s*\([^)]*\)',  # CDECL function
    ]
    for p in patterns:
        matches = re.findall(p, content)
        funcs.extend(matches)
    return list(set(funcs))

def process_file(filepath: Path, category: str) -> FileInfo:
    """Process a single C file - strip headers and extract metadata."""
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    
    all_incl, missing_incl, known_equiv = strip_includes(content)
    defines = extract_defines(content)
    structs = extract_structs(content)
    funcs = extract_functions(content)
    
    # Determine output path
    rel_path = filepath.relative_to(REACTOS_WIN32SS)
    out_path = MINNT_WIN32K_STRIP / category / filepath.name
    
    return FileInfo(
        original_path=filepath,
        stripped_path=out_path,
        category=category,
        includes_removed=all_incl,
        missing_includes=missing_incl,
        has_known_equiv=known_equiv,
        functions_found=funcs[:50],  # limit for sanity
        structs_found=structs[:30],
    )

def process_all():
    """Process all win32ss C files."""
    all_files: List[FileInfo] = []
    all_missing_includes: Set[str] = set()
    all_known_equivs: Set[str] = set()
    
    for category, dirpath in SUBDIRS.items():
        if not dirpath.exists():
            print(f"  SKIP: {dirpath} does not exist")
            continue
        
        print(f"\nProcessing {category}...")
        
        for filepath in dirpath.rglob("*.c"):
            try:
                info = process_file(filepath, category)
                all_files.append(info)
                
                all_missing_includes.update(info.missing_includes)
                all_known_equivs.update(info.has_known_equiv)
                
                # Write stripped file
                with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                    content = f.read()
                all_incl, _, _ = strip_includes(content)
                stripped = re.sub(r'^\s*#\s*include\s+[<\"].*[>\"]\s*$\n', '', content, flags=re.MULTILINE)
                
                info.stripped_path.parent.mkdir(parents=True, exist_ok=True)
                with open(info.stripped_path, 'w', encoding='utf-8') as f:
                    f.write(stripped)
                    
                print(f"  ✓ {filepath.name} ({len(all_incl)} includes removed)")
                
            except Exception as e:
                print(f"  ERROR: {filepath}: {e}")
    
    return all_files, all_missing_includes, all_known_equivs

def generate_codenames_md(missing: Set[str], equivs: Set[str]):
    """Generate the CODENAMES_TEMPLATE.md file."""
    with open(CODENAMES_TEMPLATE, 'w') as f:
        f.write("# WIN32K Codenames — Missing Equivalents\n\n")
        f.write("**Generated:** Auto-stripped from ReactOS win32ss\n\n")
        f.write("## Philosophy\n\n")
        f.write("\"WORKS OR IT DOESN'T. NOT WORKING IS IN FACT AN ACCEPTABLE OPTION.\"\n\n")
        f.write("This file documents ALL headers that have NO equivalent in MinNT.\n")
        f.write("Files that depend on these headers will reference the codename.\n")
        f.write("As equivalents are built, codenames are replaced via batch.\n\n")
        f.write("---\n\n")
        
        f.write("## Missing Headers (Need Codename)\n\n")
        f.write("| Header | Codename | Status | Notes |\n")
        f.write("|--------|----------|--------|-------|\n")
        
        for header in sorted(missing):
            codename = f"CODENAME_{header.replace('.', '_').upper()}"
            f.write(f"| `{header}` | `{codename}` | NEEDED | - |\n")
        
        f.write("\n## Known Equivalents (MinNT Has)\n\n")
        f.write("| Header | MinNT Path | Status |\n")
        f.write("|--------|------------|--------|\n")
        
        for header in sorted(equivs):
            equiv = MINNT_EQUIVALENTS.get(os.path.basename(header), "UNKNOWN")
            f.write(f"| `{header}` | `{equiv}` | ✅ EXISTS |\n")
    
    print(f"\n✓ Generated: {CODENAMES_TEMPLATE}")

def generate_orig_codenames():
    """Generate ORIG_CODENAMES.md — original state (no equivalents built yet)."""
    with open(ORIG_CODENAMES, 'w') as f:
        f.write("# ORIG_CODENAMES — Initial State\n\n")
        f.write("**Status:** NOTHING BUILT YET\n\n")
        f.write("All codenames below represent MISSING functionality.\n")
        f.write("This file is the BASELINE — it should NEVER be modified.\n")
        f.write("As equivalents are built, they get added to UPDATED_CODENAMES.\n\n")
        f.write("---\n\n")
        
        # Write all missing headers
        missing = sorted(set(MISSING_HEADERS.keys()))
        f.write("## Phase 0: Initial Codenames\n\n")
        for header in missing:
            codename = MISSING_HEADERS[header]
            f.write(f"### {header}\n")
            f.write(f"- **Codename:** `{codename}`\n")
            f.write(f"- **Status:** NOT BUILT\n")
            f.write(f"- **Files depending:** (scan later)\n\n")
    
    print(f"✓ Generated: {ORIG_CODENAMES}")

def main():
    print("=" * 70)
    print("  MinNT Win32k Header Stripper")
    print("=" * 70)
    
    # Create output directory
    MINNT_WIN32K_STRIP.mkdir(parents=True, exist_ok=True)
    
    # Process all files
    all_files, missing, known = process_all()
    
    print(f"\n✓ Processed {len(all_files)} files")
    print(f"  Missing includes: {len(missing)}")
    print(f"  Known equivalents: {len(known)}")
    
    # Generate codenames files
    generate_codenames_md(missing, known)
    generate_orig_codenames()
    
    # Save dependency map as JSON
    deps = {
        "missing_includes": sorted(list(missing)),
        "known_equivalents": sorted(list(known)),
        "files": [
            {
                "original": str(f.original_path),
                "stripped": str(f.stripped_path),
                "category": f.category,
                "includes_removed": f.includes_removed,
                "missing_includes": f.missing_includes,
                "known_equiv": f.has_known_equiv,
            }
            for f in all_files
        ]
    }
    with open(OUTPUT_DEPS, 'w') as f:
        json.dump(deps, f, indent=2)
    print(f"✓ Saved deps: {OUTPUT_DEPS}")
    
    print("\n" + "=" * 70)
    print("  DONE")
    print("=" * 70)

if __name__ == "__main__":
    main()