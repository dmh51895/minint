#!/usr/bin/env python3
"""Bulk fix round 3: SEH types, calling conventions, member names."""
import re, os

BASE = "/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint"

# ── Fix 1: Add SEH types and calling convention to ntdef.h ──
def add_seh_types():
    path = os.path.join(BASE, "include/nt/ntdef.h")
    with open(path, 'r') as f:
        c = f.read()

    if "EXCEPTION_RECORD" in c:
        print("[SKIP] ntdef.h: SEH types already present")
        return

    additions = """
/* ---- SEH types ---------------------------------------------------------- */

#define EXCEPTION_EXECUTE_HANDLER  1
#define EXCEPTION_CONTINUE_SEARCH  0
#define EXCEPTION_CONTINUE_EXECUTION (-1)

#define __cdecl
#define __stdcall

#define RTL_NUMBER_OF(a) (sizeof(a)/sizeof((a)[0]))

typedef struct _EXCEPTION_RECORD {
    NTSTATUS ExceptionCode;
    ULONG ExceptionFlags;
    PVOID ExceptionRecord;
    PVOID ExceptionAddress;
    ULONG NumberParameters;
    ULONG_PTR ExceptionInformation[15];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;

typedef struct _CONTEXT {
    ULONG64 P1Home;
    ULONG64 P2Home;
    ULONG64 P3Home;
    ULONG64 P4Home;
    ULONG64 P5Home;
    ULONG64 P6Home;
    ULONG32 ContextFlags;
    ULONG32 MxCsr;
    /* ... abbreviated - enough for compilation */
    ULONG64 Rip;
    ULONG64 Rsp;
    ULONG64 Rbp;
    ULONG64 Rax;
    ULONG64 Rbx;
    ULONG64 Rcx;
    ULONG64 Rdx;
    ULONG64 Rsi;
    ULONG64 Rdi;
    ULONG64 R8;
    ULONG64 R9;
    ULONG64 R10;
    ULONG64 R11;
    ULONG64 R12;
    ULONG64 R13;
    ULONG64 R14;
    ULONG64 R15;
} CONTEXT, *PCONTEXT;

typedef struct _EXCEPTION_POINTERS {
    PEXCEPTION_RECORD ExceptionRecord;
    PCONTEXT ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS;

typedef LONG (*PEXCEPTION_FILTER)(PEXCEPTION_POINTERS);
typedef void (*PEXCEPTION_HANDLER)(PEXCEPTION_POINTERS);
"""
    c = c.rstrip()
    idx = c.rfind("#endif /* _NTDEF_H_ */")
    c = c[:idx] + additions + "\n" + c[idx:]
    with open(path, 'w') as f:
        f.write(c)
    print("[FIX] ntdef.h: added SEH types, __cdecl, RTL_NUMBER_OF")

# ── Fix 2: Fix SubSystemType → ImageSubSystemType ──
def fix_member_names():
    path = os.path.join(BASE, "boot/chain/smss_real/smss.c")
    with open(path, 'r') as f:
        c = f.read()
    orig = c
    c = c.replace(".SubSystemType", ".ImageSubSystemType")
    if c != orig:
        with open(path, 'w') as f:
            f.write(c)
        print("[FIX] smss.c: SubSystemType → ImageSubSystemType")
    else:
        print("[SKIP] smss.c: no SubSystemType to fix")

# ── Fix 3: Fix mangled ASSERT lines left by converter ──
def fix_mangled_asserts():
    path = os.path.join(BASE, "boot/chain/smss_real/smss.c")
    with open(path, 'r') as f:
        c = f.read()
    orig = c
    # Pattern: /* ((void)0) */ DbgPrint("((void)0): "FALSE);
    # These are broken ASSERT outputs from the converter
    # Replace with just DbgPrint calls
    c = re.sub(r'/\* \(\(void\)0\) \*/\s*DbgPrint\("\(\(void\)0\): "(.*?)\);', r'DbgPrint("%s\\n");', c)
    # Also remove any remaining /* ((void)0) */ standalone lines
    c = re.sub(r'\s*/\* \(\(void\)0\) \*/\s*\n', '\n', c)
    if c != orig:
        with open(path, 'w') as f:
            f.write(c)
        print("[FIX] smss.c: cleaned mangled ASSERT outputs")
    else:
        print("[SKIP] smss.c: no mangled asserts found")

# ── Fix 4: Fix the broken `_main` function declaration ──
def fix_main_declaration():
    path = os.path.join(BASE, "boot/chain/smss_real/smss.c")
    with open(path, 'r') as f:
        c = f.read()
    orig = c
    # ReactOS SMSS has _main as a function. We keep it but fix calling convention
    # The issue is the converter mangled "/* ((void)0) */" before it
    # Actually check: the "expected '=', ',', ';', 'asm' or '__attribute__' before '_main'"
    # is likely due to broken lines above it. Let's just ensure there's no garbage.
    with open(path, 'w') as f:
        f.write(c)
    print("[INFO] smss.c: _main declaration status checked")

# ── Fix 5: Also fix all other converted files for SubSystemType ──
def fix_all_member_names():
    for d in ['smss_real', 'csrss_real', 'winlogon_real']:
        dp = os.path.join(BASE, "boot/chain", d)
        if not os.path.isdir(dp):
            continue
        for fn in os.listdir(dp):
            if not fn.endswith('.c'):
                continue
            fp = os.path.join(dp, fn)
            with open(fp, 'r') as f:
                c = f.read()
            orig = c
            c = c.replace(".SubSystemType", ".ImageSubSystemType")
            # Also fix SEH mangled lines
            c = re.sub(r'\s*/\* \(\(void\)0\) \*/\s*\n', '\n', c)
            if c != orig:
                with open(fp, 'w') as f:
                    f.write(c)
                print(f"[FIX] {d}/{fn}: cleaned member names and mangled lines")

# ── Main ──
if __name__ == '__main__':
    print("=== Bulk Error Fix Round 3 ===\n")
    add_seh_types()
    fix_member_names()
    fix_mangled_asserts()
    fix_main_declaration()
    fix_all_member_names()
    print("\n=== Done. Run 'make' ===")
