#!/usr/bin/env python3
"""
MinNT ReactOS → NT-6-BASE Conversion Pipeline
Converts ReactOS source files to MinNT-compatible code by:
1. Stripping ReactOS headers/includes
2. Replacing ReactOS debug macros with DbgPrint
3. Replacing proprietary types/functions with MinNT equivalents
4. Adding MinNT header includes
5. Marking unresolvable dependencies with TODO markers
"""

import os
import re
import sys
import json
from pathlib import Path
from typing import Optional

# Configuration
REACTOS_ROOT = Path("/home/dheavy/molecular-ai-factory/Server-V2/REACT-WINDOWS-BASE")
OUTPUT_ROOT = Path("/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint")
CONTEXT_ROOT = Path("/home/dheavy/ai_context/reactos-conversion")

# MinNT includes needed per subsystem
MINNT_HEADERS = {
    "kernel": [
        "<nt/ke.h>",
        "<nt/mm.h>",
        "<nt/ex.h>",
        "<nt/ob.h>",
        "<nt/ps.h>",
        "<nt/cm.h>",
        "<nt/se.h>",
        "<nt/lpc.h>",
        "<nt/rtl.h>",
        "<nt/hal.h>",
        "<nt/dispatcher.h>",
    ],
    "smss": [
        "<nt/ke.h>",
        "<nt/mm.h>",
        "<nt/ex.h>",
        "<nt/ob.h>",
        "<nt/ps.h>",
        "<nt/cm.h>",
        "<nt/se.h>",
        "<nt/lpc.h>",
        "<nt/rtl.h>",
        "<nt/hal.h>",
        "<nt/dispatcher.h>",
        "<ndk/obfuncs.h>",
        "<ndk/cmfuncs.h>",
        "<ndk/lpcfuncs.h>",
        "<ndk/psfuncs.h>",
        "<ndk/setypes.h>",
        "<ndk/rtlfuncs.h>",
    ],
    "csrss": [
        "<nt/ke.h>",
        "<nt/mm.h>",
        "<nt/ex.h>",
        "<nt/ob.h>",
        "<nt/ps.h>",
        "<nt/cm.h>",
        "<nt/se.h>",
        "<nt/lpc.h>",
        "<nt/rtl.h>",
        "<nt/hal.h>",
        "<nt/dispatcher.h>",
        "<ndk/obfuncs.h>",
        "<ndk/cmfuncs.h>",
        "<ndk/lpcfuncs.h>",
        "<ndk/psfuncs.h>",
        "<ndk/setypes.h>",
        "<ndk/rtlfuncs.h>",
    ],
    "winlogon": [
        "<nt/ke.h>",
        "<nt/mm.h>",
        "<nt/ex.h>",
        "<nt/ob.h>",
        "<nt/ps.h>",
        "<nt/cm.h>",
        "<nt/se.h>",
        "<nt/lpc.h>",
        "<nt/rtl.h>",
        "<nt/hal.h>",
        "<nt/dispatcher.h>",
        "<ndk/obfuncs.h>",
        "<ndk/cmfuncs.h>",
        "<ndk/psfuncs.h>",
        "<ndk/setypes.h>",
    ],
}

# ReactOS headers to strip
REACTOS_HEADERS_TO_STRIP = [
    r'#include\s*<reactos\.h>',
    r'#include\s*<windef\.h>',
    r'#include\s*<winbase\.h>',
    r'#include\s*<wingdi\.h>',
    r'#include\s*<winuser\.h>',
    r'#include\s*<winreg\.h>',
    r'#include\s*<winnt\.h>',
    r'#include\s*<winnls\.h>',
    r'#include\s*<winerror\.h>',
    r'#include\s*<winsock2\.h>',
    r'#include\s*<ws2tcpip\.h>',
    r'#include\s*<ndk\.h>',
    r'#include\s*<ntndk\.h>',
    r'#include\s*<ntddk\.h>',
    r'#include\s*<wdm\.h>',
    r'#include\s*<mountmgr\.h>',
    r'#include\s*<rtl\.h>',
    r'#include\s*<reactos/debug\.h>',
    r'#include\s*<reactos/resource\.h>',
    r'#include\s*<reactos/undoc\.h>',
    r'#include\s*<reactos/roscompat\.h>',
    r'#include\s*<reactos/subsys/sm/api\.h>',
    r'#include\s*<winreg\.h>',
    r'#include\s*<winuser\.h>',
    r'#include\s*<winbase\.h>',
    r'#include\s*<windef\.h>',
    r'#include\s*<winreg\.h>',
    r'#include\s*<winsock2\.h>',
    r'#include\s*<ws2tcpip\.h>',
    r'#include\s*<ndk/cmfuncs\.h>',
    r'#include\s*<ndk/obfuncs\.h>',
    r'#include\s*<ndk/psfuncs\.h>',
    r'#include\s*<ndk/kefuncs\.h>',
    r'#include\s*<ndk/mmfuncs\.h>',
    r'#include\s*<ndk/exfuncs\.h>',
    r'#include\s*<ndk/lpcfuncs\.h>',
]

# Debug macro replacements
DEBUG_REPLACEMENTS = [
    # DPRINT1(format, ...) → DbgPrint("CSRSS: " format, ...)
    (r'DPRINT1\(', 'DbgPrint("SMSS: "'),
    # DPRINT(format, ...) → DbgPrint("CSRSS: " format, ...)
    (r'DPRINT\(', 'DbgPrint("SMSS: "'),
]

# Function/type replacements (pattern, replacement)
REACTOS_REPLACEMENTS = [
    # Pool allocations
    (r'ExAllocatePool\(([^,]+),\s*', r'ExAllocatePoolWithTag(\1, '),
    (r'ExAllocatePoolWithTag\(([^,]+),\s*([^,]+)\)', r'ExAllocatePoolWithTag(\1, \2, 0)'),
    
    # ReactOS-specific functions
    (r'RtlInitUnicodeString\(', r'RtlInitUnicodeString('),
    (r'NT_SUCCESS\(', r'NT_SUCCESS('),
    (r'NT_ERROR\(', r'NT_ERROR('),
    (r'ASSERT\(', r'/* ASSERT */ DbgPrint("ASSERT: "'),
    (r'UNREFERENCED_PARAMETER\(', r'UNREFERENCED_PARAMETER('),
    
    # String functions
    (r'_wcsicmp\(', r'_wcsicmp('),
    (r'_stricmp\(', r'_stricmp('),
    (r'wcslen\(', r'wcslen('),
    (r'wcscpy\(', r'wcscpy('),
    (r'wcsncpy\(', r'wcsncpy('),
    
    # Registry
    (r'RtlQueryRegistryValues\(', r'RtlQueryRegistryValues('),
    (r'RtlCheckRegistryKey\(', r'RtlCheckRegistryKey('),
    
    # Memory
    (r'RtlZeroMemory\(', r'RtlZeroMemory('),
    (r'RtlCopyMemory\(', r'RtlCopyMemory('),
    (r'RtlMoveMemory\(', r'RtlMoveMemory('),
    
    # Unicode
    (r'RtlInitUnicodeString\(', r'RtlInitUnicodeString('),
    (r'RtlUnicodeStringToAnsiString\(', r'RtlUnicodeStringToAnsiString('),
    (r'RtlAnsiStringToUnicodeString\(', r'RtlAnsiStringToUnicodeString('),
    
    # Object manager
    (r'ObReferenceObjectByHandle\(', r'ObReferenceObjectByHandle('),
    (r'ObDereferenceObject\(', r'ObDereferenceObject('),
    
    # Process
    (r'PsCreateSystemThread\(', r'PsCreateSystemThread('),
    (r'PsTerminateSystemThread\(', r'PsTerminateSystemThread('),
    
    # LPC
    (r'NtCreatePort\(', r'NtCreatePort('),
    (r'NtConnectPort\(', r'NtConnectPort('),
    (r'NtListenPort\(', r'NtListenPort('),
    (r'NtAcceptConnectPort\(', r'NtAcceptConnectPort('),
    (r'NtRequestPort\(', r'NtRequestPort('),
    (r'NtReplyPort\(', r'NtReplyPort('),
    
    # Critical sections
    (r'RtlInitializeCriticalSection\(', r'RtlInitializeCriticalSection('),
    (r'RtlEnterCriticalSection\(', r'RtlEnterCriticalSection('),
    (r'RtlLeaveCriticalSection\(', r'RtlLeaveCriticalSection('),
]

# ReactOS-specific patterns to mark as TODO
TODO_PATTERNS = [
    (r'CONTAINING_RECORD\(', 'CONTAINING_RECORD (needs MinNT implementation)'),
    (r'InitializeListHead\(', 'InitializeListHead (needs MinNT implementation)'),
    (r'InsertTailList\(', 'InsertTailList (needs MinNT implementation)'),
    (r'RemoveHeadList\(', 'RemoveHeadList (needs MinNT implementation)'),
    (r'IsListEmpty\(', 'IsListEmpty (needs MinNT implementation)'),
    (r'RtlCreateTagHeap\(', 'RtlCreateTagHeap (needs MinNT implementation)'),
    (r'RtlQueryRegistryValues\(', 'RtlQueryRegistryValues (needs MinNT implementation)'),
    (r'RtlCheckRegistryKey\(', 'RtlCheckRegistryKey (needs MinNT implementation)'),
    (r'RtlAdjustPrivilege\(', 'RtlAdjustPrivilege (needs MinNT implementation)'),
    (r'NtSetSecurityObject\(', 'NtSetSecurityObject (needs MinNT implementation)'),
    (r'NtSetInformationProcess\(', 'NtSetInformationProcess (needs MinNT implementation)'),
    (r'NtCurrentPeb\(', 'NtCurrentPeb (needs MinNT implementation)'),
    (r'NtCurrentProcess\(', 'NtCurrentProcess (needs MinNT implementation)'),
    (r'NtCurrentThread\(', 'NtCurrentThread (needs MinNT implementation)'),
    (r'RtlFreeHeap\(', 'RtlFreeHeap (needs MinNT implementation)'),
    (r'RtlAllocateHeap\(', 'RtlAllocateHeap (needs MinNT implementation)'),
    (r'RtlSizeHeap\(', 'RtlSizeHeap (needs MinNT implementation)'),
    (r'NtDelayExecution\(', 'NtDelayExecution (needs MinNT implementation)'),
    (r'NtSetEvent\(', 'NtSetEvent (needs MinNT implementation)'),
    (r'NtCreateEvent\(', 'NtCreateEvent (needs MinNT implementation)'),
    (r'NtOpenProcessToken\(', 'NtOpenProcessToken (needs MinNT implementation)'),
    (r'NtQueryInformationToken\(', 'NtQueryInformationToken (needs MinNT implementation)'),
    (r'NtSetInformationToken\(', 'NtSetInformationToken (needs MinNT implementation)'),
    (r'LdrLoadDll\(', 'LdrLoadDll (needs MinNT implementation)'),
    (r'LdrGetProcedureAddress\(', 'LdrGetProcedureAddress (needs MinNT implementation)'),
    (r'NtCreateSection\(', 'NtCreateSection (needs MinNT implementation)'),
    (r'NtMapViewOfSection\(', 'NtMapViewOfSection (needs MinNT implementation)'),
    (r'NtUnmapViewOfSection\(', 'NtUnmapViewOfSection (needs MinNT implementation)'),
    (r'NtOpenSection\(', 'NtOpenSection (needs MinNT implementation)'),
    (r'NtCreateSymbolicLinkObject\(', 'NtCreateSymbolicLinkObject (needs MinNT implementation)'),
    (r'NtOpenSymbolicLinkObject\(', 'NtOpenSymbolicLinkObject (needs MinNT implementation)'),
    (r'NtMakeTemporaryObject\(', 'NtMakeTemporaryObject (needs MinNT implementation)'),
    (r'NtQueryDirectoryObject\(', 'NtQueryDirectoryObject (needs MinNT implementation)'),
]


class ReactOSConverter:
    def __init__(self, subsystem: str):
        self.subsystem = subsystem
        self.headers = MINNT_HEADERS.get(subsystem, MINNT_HEADERS["kernel"])
        self.stats = {
            "files_processed": 0,
            "headers_stripped": 0,
            "debug_replaced": 0,
            "functions_replaced": 0,
            "todos_added": 0,
            "errors": 0,
        }
    
    def strip_reactos_headers(self, content: str) -> str:
        """Remove ReactOS-specific includes."""
        lines = content.split('\n')
        result = []
        for line in lines:
            stripped = False
            for pattern in REACTOS_HEADERS_TO_STRIP:
                if re.search(pattern, line):
                    self.stats["headers_stripped"] += 1
                    stripped = True
                    break
            if not stripped:
                result.append(line)
        return '\n'.join(result)
    
    def replace_debug_macros(self, content: str) -> str:
        """Replace ReactOS debug macros with DbgPrint."""
        for pattern, replacement in DEBUG_REPLACEMENTS:
            old_count = len(re.findall(pattern, content))
            content = re.sub(pattern, replacement, content)
            self.stats["debug_replaced"] += old_count
        return content
    
    def replace_reactos_functions(self, content: str) -> str:
        """Replace ReactOS-specific functions with MinNT equivalents."""
        for pattern, replacement in REACTOS_REPLACEMENTS:
            old_count = len(re.findall(pattern, content))
            content = re.sub(pattern, replacement, content)
            self.stats["functions_replaced"] += old_count
        return content
    
    def add_todo_markers(self, content: str) -> str:
        """Add TODO markers for unresolvable dependencies."""
        for pattern, description in TODO_PATTERNS:
            if re.search(pattern, content):
                # Add a comment marker before the line
                content = re.sub(
                    pattern,
                    f'/* TODO: {description} */ {pattern}',
                    content
                )
                self.stats["todos_added"] += 1
        return content
    
    def add_minnt_includes(self, content: str) -> str:
        """Add MinNT headers after stripping ReactOS headers."""
        # Find the first #include or #define after copyright
        insert_pos = 0
        lines = content.split('\n')
        for i, line in enumerate(lines):
            if re.match(r'^#(include|define)', line):
                insert_pos = i
                break
        
        # Add MinNT includes
        minnt_includes = '\n'.join(f'#include {h}' for h in self.headers)
        
        lines.insert(insert_pos, f'\n/* MinNT includes */\n{minnt_includes}\n')
        return '\n'.join(lines)
    
    def fix_reactos_specific_patterns(self, content: str) -> str:
        """Fix ReactOS-specific code patterns."""
        # Replace ReactOS ASSERT with DbgPrint
        content = re.sub(
            r'ASSERT\(([^)]+)\);',
            r'if (!(\1)) DbgPrint("ASSERT FAILED: %s\\n", #\1);',
            content
        )
        
        # Replace _swprintf with DbgPrint
        content = re.sub(
            r'_swprintf\(([^,]+),\s*L"([^"]+)"',
            r'DbgPrint("\2"',
            content
        )
        
        # Replace CONTAINING_RECORD with direct cast
        content = re.sub(
            r'CONTAINING_RECORD\(([^,]+),\s*([^,]+),\s*([^)]+)\)',
            r'((\2*)((char*)(\1) - offsetof(\2, \3)))',
            content
        )
        
        return content
    
    def convert_file(self, source_path: Path, output_path: Path) -> bool:
        """Convert a single ReactOS source file."""
        try:
            with open(source_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
            
            # Apply conversions
            content = self.strip_reactos_headers(content)
            content = self.replace_debug_macros(content)
            content = self.replace_reactos_functions(content)
            content = self.fix_reactos_specific_patterns(content)
            content = self.add_minnt_includes(content)
            
            # Ensure output directory exists
            output_path.parent.mkdir(parents=True, exist_ok=True)
            
            # Write output
            with open(output_path, 'w', encoding='utf-8') as f:
                f.write(content)
            
            self.stats["files_processed"] += 1
            return True
            
        except Exception as e:
            print(f"Error converting {source_path}: {e}")
            self.stats["errors"] += 1
            return False
    
    def convert_directory(self, source_dir: Path, output_dir: Path, extensions: list = None):
        """Convert all files in a directory."""
        if extensions is None:
            extensions = ['.c', '.h']
        
        for ext in extensions:
            for source_file in source_dir.rglob(f'*{ext}'):
                relative_path = source_file.relative_to(source_dir)
                output_file = output_dir / relative_path
                self.convert_file(source_file, output_file)
    
    def get_stats(self) -> dict:
        """Return conversion statistics."""
        return self.stats


def main():
    """Main conversion pipeline."""
    print("MinNT ReactOS Conversion Pipeline")
    print("=" * 50)
    
    # Define subsystem source directories
    subsystems = {
        "smss": REACTOS_ROOT / "base/system/smss",
        "csrss": REACTOS_ROOT / "subsystems/csr/csrsrv",
        "winlogon": REACTOS_ROOT / "base/system/winlogon",
    }
    
    # Output directories
    output_dirs = {
        "smss": OUTPUT_ROOT / "boot/chain/smss_real",
        "csrss": OUTPUT_ROOT / "boot/chain/csrss_real",
        "winlogon": OUTPUT_ROOT / "boot/chain/winlogon_real",
    }
    
    all_stats = {}
    
    for subsystem, source_dir in subsystems.items():
        if not source_dir.exists():
            print(f"Warning: Source directory not found: {source_dir}")
            continue
        
        print(f"\nConverting {subsystem}...")
        print(f"  Source: {source_dir}")
        print(f"  Output: {output_dirs[subsystem]}")
        
        converter = ReactOSConverter(subsystem)
        converter.convert_directory(source_dir, output_dirs[subsystem])
        
        stats = converter.get_stats()
        all_stats[subsystem] = stats
        
        print(f"  Files processed: {stats['files_processed']}")
        print(f"  Headers stripped: {stats['headers_stripped']}")
        print(f"  Debug macros replaced: {stats['debug_replaced']}")
        print(f"  Functions replaced: {stats['functions_replaced']}")
        print(f"  TODO markers added: {stats['todos_added']}")
        print(f"  Errors: {stats['errors']}")
    
    # Save stats
    stats_file = CONTEXT_ROOT / "conversion_stats.json"
    with open(stats_file, 'w') as f:
        json.dump(all_stats, f, indent=2)
    
    print(f"\nStats saved to: {stats_file}")
    print("\nConversion complete!")
    
    # Summary
    total_files = sum(s['files_processed'] for s in all_stats.values())
    total_errors = sum(s['errors'] for s in all_stats.values())
    print(f"\nTotal files processed: {total_files}")
    print(f"Total errors: {total_errors}")


if __name__ == "__main__":
    main()
