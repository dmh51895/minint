#!/usr/bin/env python3
"""
Bulk fix compilation errors in converted ReactOS files.
Pattern-based: reads error list, applies fixes across all *_real/ files.
"""
import re
import sys
import os

BASE = "/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint"

# ── Fix 1: PEB.ProcessParameters: PVOID → PRTL_USER_PROCESS_PARAMETERS ──
def fix_peb_processparameters():
    path = os.path.join(BASE, "include/ndk/psfuncs.h")
    with open(path, 'r') as f:
        content = f.read()
    # Replace PVOID ProcessParameters with typed pointer
    content = content.replace(
        "    PVOID ProcessParameters;",
        "    struct _RTL_USER_PROCESS_PARAMETERS *ProcessParameters;"
    )
    with open(path, 'w') as f:
        f.write(content)
    print("[FIX] psfuncs.h: PEB.ProcessParameters typed as PRTL_USER_PROCESS_PARAMETERS")

# ── Fix 2: Missing SM/CM defines in smmsg.h ──
def fix_smss_missing_defines():
    path = os.path.join(BASE, "include/sm/smmsg.h")
    with open(path, 'r') as f:
        content = f.read()

    additions = """/* ---- Registry view names for RtlQueryRegistryValues ---- */
#ifndef RTL_REGISTRY_ABSOLUTE
#define RTL_REGISTRY_ABSOLUTE     0
#define RTL_REGISTRYServiCES      1
#define RTL_REGISTRY_CONTROL      2
#define RTL_REGISTRY_LATEST       3
#define RTL_REGISTRY_HANDLE       4
#define RTL_REGISTRY_BACKUP       5
#endif

/* ---- CM boot flags ---- */
#ifndef CM_BOOT_FLAG_SMSS
#define CM_BOOT_FLAG_SMSS         0x80000000
#endif

/* ---- SM port message types ---- */
#ifndef SB_CONNECTION_INFO_DEFINED
#define SB_CONNECTION_INFO_DEFINED
typedef struct _SB_CONNECTION_INFO {
    ULONG Length;
    ULONG Unknown[3];
} SB_CONNECTION_INFO, *PSB_CONNECTION_INFO;
#endif

#ifndef SM_API_MSG_DEFINED
#define SM_API_MSG_DEFINED
typedef struct _SM_API_MSG {
    PORT_MESSAGE Header;
    NTSTATUS Status;
    ULONG Unknown[4];
} SM_API_MSG, *PSM_API_MSG;
#endif

#ifndef SB_API_MSG_DEFINED
#define SB_API_MSG_DEFINED
typedef struct _SB_API_MSG {
    PORT_MESSAGE Header;
    ULONG ApiIndex;
    NTSTATUS Status;
    ULONG Unknown[4];
} SB_API_MSG, *PSB_API_MSG;
#endif

"""
    # Insert before final #endif or at end
    if "#endif /* _SMMSG_H_ */" in content:
        content = content.replace("#endif /* _SMMSG_H_ */", additions + "#endif /* _SMMSG_H_ */")
    else:
        content = content.rstrip() + "\n\n" + additions
    with open(path, 'w') as f:
        f.write(content)
    print("[FIX] smmsg.h: added RTL_REGISTRY_CONTROL, CM_BOOT_FLAG_SMSS, SB_CONNECTION_INFO, SM_API_MSG, SB_API_MSG")

# ── Fix 3: Remove auto-generated stubs that conflict with header declarations ──
def remove_conflicting_stubs(filepath):
    with open(filepath, 'r') as f:
        lines = f.readlines()

    # Functions that should NOT be auto-stubbed (declared in headers)
    stub_names = {
        'RtlCreateTagHeap', 'RtlQueryRegistryValues', 'RtlAdjustPrivilege',
        'RtlCreateEnvironment', 'RtlSetEnvironmentVariable',
        'RtlDosPathNameToNtPathName_U', 'RtlAllocateHeap', 'RtlFreeHeap',
        'RtlGetProcessHeap',
    }

    new_lines = []
    skip_block = False
    i = 0
    removed = 0
    while i < len(lines):
        line = lines[i]
        # Detect stub blocks: "/* Stub: FuncName */\nNTSTATUS NTAPI FuncName(...)"
        if line.strip().startswith("/* Stub:") and any(fn in line for fn in stub_names):
            # Skip this comment line + the function definition + the body + closing brace
            skip_block = True
            i += 1
            brace_depth = 0
            while i < len(lines):
                if '{' in lines[i]:
                    brace_depth += lines[i].count('{')
                if '}' in lines[i]:
                    brace_depth -= lines[i].count('}')
                i += 1
                if brace_depth <= 0 and '{' in ''.join(lines[max(0, i-20):i]):
                    break
            skip_block = False
            removed += 1
            continue
        new_lines.append(line)
        i += 1

    if removed > 0:
        with open(filepath, 'w') as f:
            f.writelines(new_lines)
        print(f"[FIX] {os.path.basename(filepath)}: removed {removed} conflicting stubs")
    return removed

# ── Fix 4: Fix ((HANDLE)(LONG_PTR)-1)() → NtCurrentProcess() ──
def fix_ntheap_call_pattern(filepath):
    with open(filepath, 'r') as f:
        content = f.read()
    # The pattern ((HANDLE)(LONG_PTR)-1)() with trailing () is wrong
    # It's trying to be NtCurrentProcess() but the cast result isn't callable
    # Replace with a proper NtCurrentProcess-like expression
    original = content
    content = content.replace("((HANDLE)(LONG_PTR)-1)()", "((HANDLE)(LONG_PTR)-1)")
    if content != original:
        with open(filepath, 'w') as f:
            f.write(content)
        print(f"[FIX] {os.path.basename(filepath)}: fixed ((HANDLE)(LONG_PTR)-1)() pattern")
        return True
    return False

# ── Fix 5: Bulk-fix undeclared identifiers across ALL converted files ──
def add_missing_includes_to_file(filepath):
    """Add missing #include or defines needed by specific files."""
    with open(filepath, 'r') as f:
        content = f.read()

    basename = os.path.basename(filepath)
    changed = False

    # SMSS needs smmsg.h for SM port types
    if 'sminit' in basename and '#include <sm/smmsg.h>' not in content:
        # Add after other includes
        content = content.replace(
            '#include <nt/ntdef.h>',
            '#include <nt/ntdef.h>\n#include <sm/smmsg.h>',
            1
        )
        changed = True
        print(f"[FIX] {basename}: added #include <sm/smmsg.h>")

    if changed:
        with open(filepath, 'w') as f:
            f.write(content)

# ── Main ──
if __name__ == '__main__':
    print("=== Bulk Error Fixer ===\n")

    # Fix headers
    fix_peb_processparameters()
    fix_smss_missing_defines()

    # Fix all converted files
    real_dirs = [
        os.path.join(BASE, "boot/chain/smss_real"),
        os.path.join(BASE, "boot/chain/csrss_real"),
        os.path.join(BASE, "boot/chain/winlogon_real"),
    ]

    for d in real_dirs:
        if not os.path.isdir(d):
            continue
        for fn in os.listdir(d):
            if fn.endswith('.c'):
                fp = os.path.join(d, fn)
                remove_conflicting_stubs(fp)
                fix_ntheap_call_pattern(fp)
                add_missing_includes_to_file(fp)

    print("\n=== Done. Run 'make' to check remaining errors. ===")
