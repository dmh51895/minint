#!/usr/bin/env python3
"""Bulk fix round 5: PORT_MESSAGE u1/u2, complete SB structs, remaining errors."""
import re, os

BASE = "/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint"

# ── Fix 1: PORT_MESSAGE with u1/u2 ──
def fix_port_message():
    path = os.path.join(BASE, "include/ndk/lpcfuncs.h")
    with open(path, 'r') as f:
        c = f.read()
    old = """typedef struct _PORT_MESSAGE {
    USHORT DataLength;
    USHORT TotalLength;
    union {
        USHORT Type;
        USHORT DataInfoOffset;
    };
    USHORT CallbackId;
    CLIENT_ID ClientId;
    ULONG MessageId;
    ULONG_PTR CallbackView;
} PORT_MESSAGE, *PPORT_MESSAGE;"""
    new = """typedef struct _PORT_MESSAGE {
    union {
        struct {
            CSHORT DataLength;
            CSHORT TotalLength;
        } s1;
        ULONG Length;
    } u1;
    union {
        struct {
            CLIENT_ID ClientId;
            ULONG DataSize;
            ULONG MessageId;
        } s2;
        struct {
            CSHORT ZeroInit;
            CSHORT Type;
        };
        double DoNotUseThisField;
    } u2;
    union {
        UCHAR Data[1];
        struct {
            UCHAR Data[1];
        };
    };
} PORT_MESSAGE, *PPORT_MESSAGE;"""
    c = c.replace(old, new)
    with open(path, 'w') as f:
        f.write(c)
    print("[FIX] lpcfuncs.h: PORT_MESSAGE now has u1/u2 unions")

# ── Fix 2: Complete SB structs in smmsg.h ──
def fix_sb_structs():
    path = os.path.join(BASE, "include/sm/smmsg.h")
    with open(path, 'r') as f:
        c = f.read()

    # Replace the SB_CREATE_PROCESS structs
    old_in = """typedef struct _SB_CREATE_PROCESS_IN {
    UNICODE_STRING ImageName;
    UNICODE_STRING CommandLine;
    ULONG DbgUiClientId_Count;
    CLIENT_ID DbgUiClientId;
    HANDLE DebugPort;
} SB_CREATE_PROCESS_IN, *PSB_CREATE_PROCESS_IN;

typedef struct _SB_CREATE_PROCESS_OUT {
    NTSTATUS Status;
    HANDLE ProcessHandle;
    HANDLE ThreadHandle;
    CLIENT_ID ClientId;
    SECTION_IMAGE_INFORMATION ImageInformation;
} SB_CREATE_PROCESS_OUT, *PSB_CREATE_PROCESS_OUT;

typedef struct _SB_CREATE_PROCESS_MSG {
    SB_CREATE_PROCESS_IN In;
    SB_CREATE_PROCESS_OUT Out;
} SB_CREATE_PROCESS_MSG, *PSB_CREATE_PROCESS_MSG;"""

    new_in = """typedef struct _SB_CREATE_PROCESS_IN {
    UNICODE_STRING ImageName;
    PUNICODE_STRING CurrentDirectory;
    PUNICODE_STRING CommandLine;
    PUNICODE_STRING DllPath;
    ULONG Flags;
    ULONG DebugFlags;
} SB_CREATE_PROCESS_IN, *PSB_CREATE_PROCESS_IN;

typedef struct _SB_CREATE_PROCESS_OUT {
    NTSTATUS Status;
    HANDLE ProcessHandle;
    HANDLE ThreadHandle;
    CLIENT_ID ClientId;
    ULONG SubsystemType;
} SB_CREATE_PROCESS_OUT, *PSB_CREATE_PROCESS_OUT;

typedef struct _SB_CREATE_PROCESS_MSG {
    SB_CREATE_PROCESS_IN In;
    SB_CREATE_PROCESS_OUT Out;
} SB_CREATE_PROCESS_MSG, *PSB_CREATE_PROCESS_MSG;"""
    c = c.replace(old_in, new_in)

    # Replace SB_CREATE_SESSION
    old_sess = """typedef struct _SB_CREATE_SESSION_IN {
    ULONG SessionId;
    ULONG DbgSessionId;
    ULONGLONG DbgUiClientId;
} SB_CREATE_SESSION_IN, *PSB_CREATE_SESSION_IN;

typedef struct _SB_CREATE_SESSION_OUT {
    NTSTATUS Status;
    RTL_USER_PROCESS_INFORMATION ProcessInfo;
} SB_CREATE_SESSION_OUT, *PSB_CREATE_SESSION_OUT;

typedef struct _SB_CREATE_SESSION_MSG {
    SB_CREATE_SESSION_IN In;
    SB_CREATE_SESSION_OUT Out;
} SB_CREATE_SESSION_MSG, *PSB_CREATE_SESSION_MSG;"""
    new_sess = """typedef struct _SB_CREATE_SESSION_IN {
    ULONG SessionId;
} SB_CREATE_SESSION_IN, *PSB_CREATE_SESSION_IN;

typedef struct _SB_CREATE_SESSION_OUT {
    NTSTATUS Status;
} SB_CREATE_SESSION_OUT, *PSB_CREATE_SESSION_OUT;

typedef struct _SB_CREATE_SESSION_MSG {
    SB_CREATE_SESSION_IN In;
    union {
        SB_CREATE_SESSION_OUT Out;
        RTL_USER_PROCESS_INFORMATION ProcessInfo;
        ULONG DbgSessionId;
        ULONGLONG DbgUiClientId;
    };
} SB_CREATE_SESSION_MSG, *PSB_CREATE_SESSION_MSG;"""
    c = c.replace(old_sess, new_sess)

    # Add SbpCreateProcess/SbpCreateSession API numbers
    if "SbpCreateProcess" not in c:
        c = c.replace("#define _SEH2_LEAVE",
            "#define SbpCreateProcess 0x0001\n#define SbpCreateSession 0x0002\n\n#define _SEH2_LEAVE")

    with open(path, 'w') as f:
        f.write(c)
    print("[FIX] smmsg.h: expanded SB structs, added SbpCreate API numbers")

# ── Fix 3: Fix 'called object is not a function' - usually macro mangling ──
def fix_called_object_errors():
    # Check smsubsys.c for the specific lines
    path = os.path.join(BASE, "boot/chain/smss_real/smsubsys.c")
    with open(path, 'r') as f:
        lines = f.readlines()

    for line_no in [334, 492]:
        idx = line_no - 1
        if idx < len(lines):
            line = lines[idx]
            print(f"  smsubsys.c:{line_no}: {line.rstrip()}")

# ── Fix 4: Fix mangled ASSERT lines ──
def fix_mangled_asserts_all():
    """Fix patterns like: /* ((void)0) */ DbgPrint("((void)0): "NT_SUCCESS(Status));"""
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
            # Pattern: /* ((void)0) */ DbgPrint("((void)0): "SOMETHING);
            c = re.sub(r'/\* \(\(void\)0\) \*/\s*DbgPrint\([^;]*\);\s*\n', '', c)
            if c != orig:
                with open(fp, 'w') as f:
                    f.write(c)
                count = len(re.findall(r'/\* \(\(void\)0\) \*/', orig))
                print(f"[FIX] {d}/{fn}: removed {count} mangled ASSERT lines")

if __name__ == '__main__':
    print("=== Bulk Error Fix Round 5 ===\n")
    fix_port_message()
    fix_sb_structs()
    fix_called_object_errors()
    fix_mangled_asserts_all()
    print("\n=== Done. Run 'make' ===")
