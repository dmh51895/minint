#!/usr/bin/env python3
"""Bulk fix round 4: smsubsys.c errors - SB_API_MSG fields, missing types, orphaned stubs."""
import re, os

BASE = "/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint"

# ── Fix 1: Complete SB_API_MSG struct with proper members ──
def fix_smmsg():
    path = os.path.join(BASE, "include/sm/smmsg.h")
    with open(path, 'r') as f:
        c = f.read()

    # Remove the simple stub and replace with a proper one
    old = """#ifndef SB_API_MSG_DEFINED
#define SB_API_MSG_DEFINED
typedef struct _SB_API_MSG {
    PORT_MESSAGE Header;
    ULONG ApiIndex;
    NTSTATUS Status;
    ULONG Unknown[4];
} SB_API_MSG, *PSB_API_MSG;
#endif"""

    new = """#ifndef SB_API_MSG_DEFINED
#define SB_API_MSG_DEFINED

typedef struct _SB_CREATE_PROCESS_MSG {
    ULONG Size;
    HANDLE ProcessHandle;
    ULONG SubsystemId;
    NTSTATUS Status;
} SB_CREATE_PROCESS_MSG, *PSB_CREATE_PROCESS_MSG;

typedef struct _SB_CREATE_SESSION_MSG {
    ULONG Size;
    ULONG SessionId;
    NTSTATUS Status;
} SB_CREATE_SESSION_MSG, *PSB_CREATE_SESSION_MSG;

/* SB API message header union */
typedef struct _SB_API_MSG {
    PORT_MESSAGE h;
    union {
        ULONG ApiNumber;
        NTSTATUS ReturnValue;
    };
    union {
        SB_CREATE_PROCESS_MSG CreateProcess;
        SB_CREATE_SESSION_MSG CreateSession;
        ULONG Data[16];
    } u;
} SB_API_MSG, *PSB_API_MSG;
#endif"""

    c = c.replace(old, new)

    # Add missing types/defines
    additions = []
    if "PULONGLONG" not in c:
        additions.append("#define PULONGLONG ULONGLONG*")
    if "STATUS_DELETE_PENDING" not in c:
        additions.append("#define STATUS_DELETE_PENDING ((NTSTATUS)0xC0000056L)")
    if "STATUS_NO_SUCH_PACKAGE" not in c:
        additions.append("#define STATUS_NO_SUCH_PACKAGE ((NTSTATUS)0xC000015AL)")
    if "STATUS_OBJECT_PATH_SYNTAX_BAD" not in c:
        additions.append("#define STATUS_OBJECT_PATH_SYNTAX_BAD ((NTSTATUS)0xC000003BL)")
    if "SystemSessionCreate" not in c:
        additions.append("#define SystemSessionCreate 0x101")
    if "SystemExtendServiceTableInformation" not in c:
        additions.append("#define SystemExtendServiceTableInformation 0x102")

    if additions:
        block = "\n".join(additions) + "\n"
        c = c.rstrip()
        idx = c.rfind("#endif")
        c = c[:idx] + block + "\n" + c[idx:]

    with open(path, 'w') as f:
        f.write(c)
    print(f"[FIX] smmsg.h: expanded SB_API_MSG, added {len(additions)} defines")

# ── Fix 2: Remove orphaned stubs in smsubsys.c ──
def fix_smsubsys():
    path = os.path.join(BASE, "boot/chain/smss_real/smsubsys.c")
    with open(path, 'r') as f:
        c = f.read()
    orig = c

    # Remove orphaned stub bodies (bare { return VALUE; } blocks at end of file)
    c = re.sub(r'\n\{\s*\n\s*return\s+(?:NULL|STATUS_NOT_IMPLEMENTED|TRUE|FALSE|STATUS_SUCCESS);\s*\n\}\n', '\n', c)
    # Fix ((HANDLE)(LONG_PTR)-1)()
    c = c.replace("((HANDLE)(LONG_PTR)-1)()", "((HANDLE)(LONG_PTR)-1)")
    # Remove __cdecl
    c = re.sub(r'\b__cdecl\b\s*', '', c)

    if c != orig:
        with open(path, 'w') as f:
            f.write(c)
        print("[FIX] smsubsys.c: removed orphaned stubs, cleaned")
    else:
        print("[SKIP] smsubsys.c: nothing to fix")

# ── Fix 3: Apply fixes to ALL converted files ──
def fix_all_files():
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
            c = re.sub(r'\n\{\s*\n\s*return\s+(?:NULL|STATUS_NOT_IMPLEMENTED|TRUE|FALSE|STATUS_SUCCESS);\s*\n\}\n', '\n', c)
            c = c.replace("((HANDLE)(LONG_PTR)-1)()", "((HANDLE)(LONG_PTR)-1)")
            c = re.sub(r'\b__cdecl\b\s*', '', c)
            if c != orig:
                with open(fp, 'w') as f:
                    f.write(c)
                print(f"[FIX] {d}/{fn}: cleaned")

if __name__ == '__main__':
    print("=== Bulk Error Fix Round 4 ===\n")
    fix_smmsg()
    fix_smsubsys()
    fix_all_files()
    print("\n=== Done. Run 'make' ===")
