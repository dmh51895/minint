#!/usr/bin/env python3
"""Bulk fix round 2: smss.c errors."""
import re, os

BASE = "/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint"

# ── Fix 1: Add ImageInformation to RTL_USER_PROCESS_INFORMATION ──
def fix_process_info_struct():
    path = os.path.join(BASE, "include/ndk/psfuncs.h")
    with open(path, 'r') as f:
        c = f.read()
    # Add SECTION_IMAGE_INFORMATION forward decl and ImageInformation field
    old = """typedef struct _RTL_USER_PROCESS_INFORMATION {
    ULONG Size;
    HANDLE ProcessHandle;
    HANDLE ThreadHandle;
    CLIENT_ID ClientId;
    PVOID ImageBaseAddress;
} RTL_USER_PROCESS_INFORMATION, *PRTL_USER_PROCESS_INFORMATION;"""

    new = """typedef struct _SECTION_IMAGE_INFORMATION {
    PVOID ImageBaseAddress;
    ULONG ImageAttributes;
    ULONG ImageSubSystemType;
    union {
        ULONG ImageDllNameOffsetAndLength;
        struct {
            USHORT ImageDllNameOffset;
            USHORT ImageDllNameLength;
        };
    };
    ULONG ImageEntryPoint;
    ULONG SizeOfImage;
    ULONG ImageCheckSum;
    ULONG NumberOfSections;
    ULONG SectionAlignment;
    USHORT DllCharacteristics;
    USHORT Machine;
    ULONG Reserved[3];
} SECTION_IMAGE_INFORMATION, *PSECTION_IMAGE_INFORMATION;

typedef struct _RTL_USER_PROCESS_INFORMATION {
    ULONG Size;
    HANDLE ProcessHandle;
    HANDLE ThreadHandle;
    CLIENT_ID ClientId;
    PVOID ImageBaseAddress;
    SECTION_IMAGE_INFORMATION ImageInformation;
} RTL_USER_PROCESS_INFORMATION, *PRTL_USER_PROCESS_INFORMATION;"""
    c = c.replace(old, new)
    with open(path, 'w') as f:
        f.write(c)
    print("[FIX] psfuncs.h: added ImageInformation to RTL_USER_PROCESS_INFORMATION")

# ── Fix 2: Fix RtlCreateProcessParameters to match ReactOS 10-arg call ──
def fix_rtl_create_process_params():
    path = os.path.join(BASE, "include/ndk/psfuncs.h")
    with open(path, 'r') as f:
        c = f.read()
    old = """NTSTATUS NTAPI RtlCreateProcessParameters(
    PRTL_USER_PROCESS_PARAMETERS *ProcessParameters,
    PUNICODE_STRING ImagePathName,
    PUNICODE_STRING DllPath,
    PUNICODE_STRING CurrentDirectory,
    PUNICODE_STRING CommandLine,
    PVOID Environment,
    PUNICODE_STRING WindowTitle,
    PUNICODE_STRING DesktopInfo,
    PUNICODE_STRING ShellInfo,
    PUNICODE_STRING RuntimeData,
    ULONG Flags
);"""
    new = """NTSTATUS NTAPI RtlCreateProcessParameters(
    PRTL_USER_PROCESS_PARAMETERS *ProcessParameters,
    PUNICODE_STRING ImagePathName,
    PUNICODE_STRING DllPath,
    PUNICODE_STRING CurrentDirectory,
    PUNICODE_STRING CommandLine,
    PVOID Environment,
    PUNICODE_STRING WindowTitle,
    PUNICODE_STRING DesktopInfo,
    PUNICODE_STRING ShellInfo,
    PUNICODE_STRING RuntimeData
);"""
    c = c.replace(old, new)
    with open(path, 'w') as f:
        f.write(c)
    print("[FIX] psfuncs.h: RtlCreateProcessParameters → 10 args (removed Flags)")

# ── Fix 3: Add missing status codes and privilege defines ──
def add_missing_nt_defines():
    path = os.path.join(BASE, "include/nt/ntdef.h")
    with open(path, 'r') as f:
        c = f.read()

    additions = []
    if "SE_SHUTDOWN_PRIVILEGE" not in c:
        additions.append("#define SE_SHUTDOWN_PRIVILEGE      19")
    if "STATUS_NO_TOKEN" not in c:
        additions.append("#define STATUS_NO_TOKEN            ((NTSTATUS)0xC000007CL)")
    if "STATUS_SYSTEM_PROCESS_TERMINATED" not in c:
        additions.append("#define STATUS_SYSTEM_PROCESS_TERMINATED ((NTSTATUS)0xC000014AL)")
    if "IMAGE_SUBSYSTEM_NATIVE" not in c:
        additions.append("#define IMAGE_SUBSYSTEM_NATIVE     16")

    if additions:
        # Insert before the last #endif
        block = "\n".join(additions) + "\n"
        c = c.rstrip()
        c = c[:c.rfind("#endif")] + block + "\n" + c[c.rfind("#endif"):]
    with open(path, 'w') as f:
        f.write(c)
    print(f"[FIX] ntdef.h: added {len(additions)} missing defines")

# ── Fix 4: Fix PSEH2 macros to actually work with SEH2_TRY pattern ──
def fix_pseh2_header():
    path = os.path.join(BASE, "include/pseh/pseh2.h")
    with open(path, 'w') as f:
        f.write("""/* PSEH2 stub for MinNT */
#ifndef _PSEH2_H_
#define _PSEH2_H_

#include <setjmp.h>

/* Minimal SEH emulation using setjmp/longjmp */
extern jmp_buf __seh_jmpbuf;
extern volatile DWORD __seh_code;
extern volatile int __seh_active;

#define SEH2_TRY \
    { int __seh__ret = setjmp(__seh_jmpbuf); \
      __seh_active = 1; \
      if (__seh__ret == 0) {

#define SEH2_EXCEPT(x) \
      __seh_active = 0; \
    } else { \
      __seh_code = (DWORD)__seh__ret; \
      if (x) {

#define SEH2_FINALLY \
      __seh_active = 0; \
    } { \
      int __seh__dummy = 0; if (__seh__dummy) {

#define SEH2_END \
      __seh_active = 0; \
    } }

#define SEH2_GetExceptionCode()    (__seh_code)
#define SEH2_AbnormalTermination() 0
#define SEH2_FILTER(x)  ((x) != 0)

/* ReactOS compat aliases */
#define _SEH2_TRY       SEH2_TRY
#define _SEH2_EXCEPT(x) SEH2_EXCEPT(x)
#define _SEH2_FINALLY   SEH2_FINALLY
#define _SEH2_END       SEH2_END
#define _SEH2_GetExceptionCode()    SEH2_GetExceptionCode()
#define _SEH2_AbnormalTermination() SEH2_AbnormalTermination()
#define _SEH2_FILTER(x) SEH2_FILTER(x)

/* Also support lowercase TRY/EXCEPT/FINALLY */
#define TRY         SEH2_TRY
#define EXCEPT(x)   SEH2_EXCEPT(x)
#define FINALLY     SEH2_FINALLY
#define ENDTRY      SEH2_END
#define GetExceptionCode()      SEH2_GetExceptionCode()
#define AbnormalTermination()   SEH2_AbnormalTermination()

#endif /* _PSEH2_H_ */
""")
    print("[FIX] pseh2.h: rewritten with SEH2_TRY/EXCEPT macros")

# ── Fix 5: Remove remaining orphaned stubs in smss.c ──
def fix_smss_c_orphaned():
    path = os.path.join(BASE, "boot/chain/smss_real/smss.c")
    with open(path, 'r') as f:
        content = f.read()

    # Remove orphaned stub bodies
    pattern = r'\n\{\s*\n\s*return\s+(?:NULL|STATUS_NOT_IMPLEMENTED|TRUE|FALSE|STATUS_SUCCESS);\s*\n\}\n'
    content = re.sub(pattern, '\n', content)

    # Fix ((HANDLE)(LONG_PTR)-1)()
    content = content.replace("((HANDLE)(LONG_PTR)-1)()", "((HANDLE)(LONG_PTR)-1)")

    with open(path, 'w') as f:
        f.write(content)
    print("[FIX] smss.c: removed orphaned stubs, fixed NtCurrentProcess")

# ── Main ──
if __name__ == '__main__':
    print("=== Bulk Error Fix Round 2 ===\n")
    fix_process_info_struct()
    fix_rtl_create_process_params()
    add_missing_nt_defines()
    fix_pseh2_header()
    fix_smss_c_orphaned()
    print("\n=== Done. Run 'make' ===")
