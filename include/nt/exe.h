/*
 * MinNT - nt/exe.h
 * Native EXE handler: DLL export registry + PE import resolution.
 *
 * When a real Windows EXE is loaded by PeLoadImage, its imports reference
 * DLLs like d3d12.dll, kernel32.dll, user32.dll.  This module provides a
 * registry that maps those import names to kernel-resident function
 * pointers, wrapped with the MS x64 calling convention so the Windows
 * code can call them directly.
 */

#ifndef _EXE_H_
#define _EXE_H_

#include <nt/ntdef.h>
#include <nt/pe.h>

/* ---- DLL export registry --------------------------------------------------- */

/*
 * Register a single export:
 *   DllName  — e.g. "d3d12.dll"  (case-insensitive)
 *   FuncName — e.g. "D3D12CreateDevice"
 *   FuncPtr  — kernel function pointer (must be __attribute__((ms_abi)))
 *
 * Returns STATUS_SUCCESS or STATUS_NO_MEMORY.
 */
NTSTATUS NTAPI ExeRegisterExport(
    const CHAR *DllName,
    const CHAR *FuncName,
    PVOID FuncPtr);

/*
 * Look up an export:
 *   DllName  — e.g. "d3d12.dll"
 *   FuncName — e.g. "D3D12CreateDevice"
 *
 * Returns the function pointer, or NULL if not found.
 */
PVOID NTAPI ExeResolveExport(
    const CHAR *DllName,
    const CHAR *FuncName);

/* ---- Native EXE launcher --------------------------------------------------- */

/*
 * Load a PE32+ EXE from a memory buffer, resolve all its imports against
 * the export registry, apply relocations, and return the loaded image.
 *
 * On success, OutImage.EntryPoint is ready to be called (from user mode
 * or kernel mode) with the MS ABI calling convention.
 */
NTSTATUS NTAPI ExeLaunch(
    PVOID FileBase,
    ULONG FileSize,
    PPE_IMAGE OutImage);

/* ---- Initialisation -------------------------------------------------------- */

/*
 * Register all built-in DLL exports.  Called once during kernel init.
 * Registers exports for: d3d12.dll, kernel32.dll, user32.dll, gdi32.dll
 */
VOID NTAPI ExeInitRegistry(VOID);

/* Per-DLL registration functions (called by ExeInitRegistry) */
VOID NTAPI Kernel32RegisterExports(VOID);
VOID NTAPI NtdllRegisterExports(VOID);
VOID NTAPI User32RegisterExports(VOID);
VOID NTAPI Gdi32RegisterExports(VOID);
VOID NTAPI Advapi32RegisterExports(VOID);
VOID NTAPI Ole32RegisterExports(VOID);
VOID NTAPI DxgiRegisterExports(VOID);
VOID NTAPI D3dCompilerRegisterExports(VOID);
VOID NTAPI Shell32RegisterExports(VOID);
VOID NTAPI Ws2_32RegisterExports(VOID);

/* Export table access (for GetProcAddress implementation) */
extern PVOID NTAPI ExeResolveExport(const CHAR *DllName, const CHAR *FuncName);

/* Case-insensitive string compare used across export files */
int SwStrICmp(const CHAR *a, const CHAR *b);

/* Export table globals (accessible from export registration files) */
#define MAX_EXPORTS 256
typedef struct _DLL_EXPORT_ENTRY {
    const CHAR *DllName;
    const CHAR *FuncName;
    PVOID       FuncPtr;
} DLL_EXPORT_ENTRY;
extern DLL_EXPORT_ENTRY g_ExportTable[];
extern ULONG g_ExportCount;

#endif /* _EXE_H_ */
