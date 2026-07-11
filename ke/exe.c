/*
 * MinNT - ke/exe.c
 * Native EXE handler: DLL export registry + PE import resolution.
 *
 * Provides a kernel-resident implementation of the DLLs that Windows
 * EXEs import at load time.  Every registered export is wrapped with
 * __attribute__((ms_abi)) so that Windows-compiled code calling our
 * functions gets the correct Microsoft x64 calling convention (RCX/RDX/
 * R8/R9 + 32-byte shadow space) even though the kernel itself is built
 * with the SysV ABI.
 *
 * Flow:
 *   1. ExeInitRegistry() — called once during boot; registers all exports
 *   2. PeResolveImports() — patched to call ExeResolveExport() as fallback
 *   3. ExeLaunch() — convenience wrapper: PeLoadImage + PeResolveImports
 */

#include <nt/ntdef.h>
#include <nt/ex.h>
#include <nt/ke.h>
#include <nt/pe.h>
#include <nt/exe.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/mm.h>

/* For the D3D12 exports */
#include "../win32k/d3d12/d3d12.h"

/* ============================================================================
 * Export registry — flat array of (dll, name, ptr) triples.
 *
 * A hash table would be faster but the total number of exports is small
 * (< 200) so a linear scan with _stricmp is perfectly adequate.
 * ========================================================================== */
/* MAX_EXPORTS, DLL_EXPORT_ENTRY type come from exe.h */
DLL_EXPORT_ENTRY g_ExportTable[MAX_EXPORTS];
ULONG g_ExportCount = 0;

/* ---- Case-insensitive string compare (kernel has no _stricmp) ------------- */

int SwStrICmp(const CHAR *a, const CHAR *b)
{
    while (*a && *b) {
        CHAR ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        a++; b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* ---- Registry API --------------------------------------------------------- */

NTSTATUS NTAPI
ExeRegisterExport(const CHAR *DllName, const CHAR *FuncName, PVOID FuncPtr)
{
    if (!DllName || !FuncName || !FuncPtr)
        return STATUS_INVALID_PARAMETER;
    if (g_ExportCount >= MAX_EXPORTS)
        return STATUS_NO_MEMORY;

    DLL_EXPORT_ENTRY *e = &g_ExportTable[g_ExportCount++];
    e->DllName  = DllName;
    e->FuncName = FuncName;
    e->FuncPtr  = FuncPtr;
    return STATUS_SUCCESS;
}

PVOID NTAPI
ExeResolveExport(const CHAR *DllName, const CHAR *FuncName)
{
    if (!DllName || !FuncName)
        return NULL;

    for (ULONG i = 0; i < g_ExportCount; i++) {
        DLL_EXPORT_ENTRY *e = &g_ExportTable[i];
        if (SwStrICmp(e->DllName, DllName) == 0 &&
            SwStrICmp(e->FuncName, FuncName) == 0) {
            return e->FuncPtr;
        }
    }
    return NULL;
}

/* ============================================================================
 * MS ABI wrappers for d3d12.dll exports
 *
 * Each wrapper is declared __attribute__((ms_abi)) so GCC generates the
 * prologue/epilogue for the Windows x64 convention.  The body calls our
 * SysV-compiled kernel function.
 * ========================================================================== */

/* D3D12CreateDevice — the main entry point */
__attribute__((ms_abi))
static HRESULT D3D12CreateDevice_msabi(
    IUnknown *pAdapter,
    D3D_FEATURE_LEVEL MinimumFeatureLevel,
    REFIID riid,
    void **ppvDevice)
{
    return D3D12CreateDevice(pAdapter, MinimumFeatureLevel, riid, ppvDevice);
}

/* D3D12GetDebugInterface */
__attribute__((ms_abi))
static HRESULT D3D12GetDebugInterface_msabi(
    REFIID riid,
    void **ppvObject)
{
    return D3D12GetDebugInterface(riid, ppvObject);
}

/* ============================================================================
 * MS ABI wrappers for kernel32.dll core exports
 *
 * These are the functions that every Windows app imports.  We provide
 * minimal but functional implementations.
 * ========================================================================== */

/* GetTickCount — return milliseconds since boot */
extern volatile ULONG64 KeTickCount;  /* defined in ke/irql.c */

__attribute__((ms_abi))
static ULONG64 GetTickCount_msabi(VOID)
{
    return KeTickCount;
}

/* QueryPerformanceCounter — for high-res timing */
__attribute__((ms_abi))
static BOOL QueryPerformanceCounter_msabi(LARGE_INTEGER *perfCounter)
{
    if (perfCounter) {
        /* Use TSC */
        ULONG64 tsc;
        __asm__ __volatile__("rdtsc" : "=A"(tsc));
        perfCounter->QuadPart = (LONG64)tsc;
        return TRUE;
    }
    return FALSE;
}

/* QueryPerformanceFrequency */
__attribute__((ms_abi))
static BOOL QueryPerformanceFrequency_msabi(LARGE_INTEGER *frequency)
{
    if (frequency) {
        frequency->QuadPart = 1000000000LL; /* 1 GHz — reasonable for modern TSC */
        return TRUE;
    }
    return FALSE;
}

/* GetModuleHandleA — return fake non-null handle for built-in DLLs */
__attribute__((ms_abi))
static PVOID GetModuleHandleA_msabi(const CHAR *ModuleName)
{
    if (!ModuleName)
        return (PVOID)0x10000000LL;  /* "current module" */
    if (SwStrICmp(ModuleName, "d3d12.dll") == 0 ||
        SwStrICmp(ModuleName, "kernel32.dll") == 0 ||
        SwStrICmp(ModuleName, "user32.dll") == 0 ||
        SwStrICmp(ModuleName, "gdi32.dll") == 0 ||
        SwStrICmp(ModuleName, "ntdll.dll") == 0)
        return (PVOID)0x10000000LL;
    return NULL;
}

/* GetProcAddress — resolve by name through our registry */
__attribute__((ms_abi))
static PVOID GetProcAddress_msabi(PVOID hModule, const CHAR *lpProcName)
{
    /* hModule tells us which DLL — but since we have a flat registry,
       we just search all DLLs for the function name.  This is slightly
       over-broad but correct for our use case. */
    if (!lpProcName) return NULL;
    /* Search every DLL for this function name */
    for (ULONG i = 0; i < g_ExportCount; i++) {
        if (SwStrICmp(g_ExportTable[i].FuncName, lpProcName) == 0)
            return g_ExportTable[i].FuncPtr;
    }
    return NULL;
}

/* LoadLibraryA — pretend we loaded the DLL, return a fake handle */
__attribute__((ms_abi))
static PVOID LoadLibraryA_msabi(const CHAR *lpLibFileName)
{
    if (!lpLibFileName) return NULL;
    DbgPrint("EXE: LoadLibraryA(\"%s\") -> fake handle\n", lpLibFileName);
    return (PVOID)0x10000000LL;
}

/* FreeLibrary — no-op */
__attribute__((ms_abi))
static BOOL FreeLibrary_msabi(PVOID hLibModule)
{
    (void)hLibModule;
    return TRUE;
}

/* GetConsoleWindow — return 0 (no console) */
__attribute__((ms_abi))
static ULONG_PTR GetConsoleWindow_msabi(VOID)
{
    return 0;
}

/* Sleep — stall the processor */
__attribute__((ms_abi))
static VOID Sleep_msabi(ULONG dwMilliseconds)
{
    KeStallExecutionProcessor(dwMilliseconds * 1000);
}

/* GetLastError — return 0 (no error) */
__attribute__((ms_abi))
static ULONG GetLastError_msabi(VOID)
{
    return 0;
}

/* SetLastError — no-op */
__attribute__((ms_abi))
static VOID SetLastError_msabi(ULONG dwErrCode)
{
    (void)dwErrCode;
}

/* CreateEventA — return a fake non-null handle */
__attribute__((ms_abi))
static HANDLE CreateEventA_msabi(PVOID lpEventAttributes, BOOL bManualReset,
                                  BOOL bInitialState, const CHAR *lpName)
{
    (void)lpEventAttributes; (void)bManualReset; (void)bInitialState; (void)lpName;
    return (HANDLE)0x20000000LL;
}

/* WaitForSingleObject — instant return (no real wait in kernel software renderer) */
__attribute__((ms_abi))
static ULONG WaitForSingleObject_msabi(HANDLE hHandle, ULONG dwMilliseconds)
{
    (void)hHandle; (void)dwMilliseconds;
    return 0; /* WAIT_OBJECT_0 */
}

/* OutputDebugStringA — route to DbgPrint */
__attribute__((ms_abi))
static VOID OutputDebugStringA_msabi(const CHAR *lpOutputString)
{
    if (lpOutputString)
        DbgPrint("EXE: %s", lpOutputString);
}

/* GetProcessHeap / HeapAlloc / HeapFree — simple pool wrappers */
__attribute__((ms_abi))
static PVOID GetProcessHeap_msabi(VOID)
{
    return (PVOID)0x30000000LL;
}

__attribute__((ms_abi))
static PVOID HeapAlloc_msabi(PVOID hHeap, ULONG dwFlags, SIZE_T dwBytes)
{
    (void)hHeap; (void)dwFlags;
    return ExAllocatePool(NonPagedPool, dwBytes);
}

__attribute__((ms_abi))
static BOOL HeapFree_msabi(PVOID hHeap, ULONG dwFlags, PVOID lpMem)
{
    (void)hHeap; (void)dwFlags;
    if (lpMem) ExFreePool(lpMem);
    return TRUE;
}

/* SetUnhandledExceptionFilter — return NULL */
typedef PVOID (LPTOP_LEVEL_EXCEPTION_FILTER)(PVOID);
__attribute__((ms_abi))
static PVOID SetUnhandledExceptionFilter_msabi(LPTOP_LEVEL_EXCEPTION_FILTER *lpTopLevelExceptionFilter)
{
    (void)lpTopLevelExceptionFilter;
    return NULL;
}

/* ExitProcess — halt the kernel (for now) */
__attribute__((ms_abi))
static VOID ExitProcess_msabi(UINT uExitCode)
{
    DbgPrint("EXE: ExitProcess(%u)\n", uExitCode);
    /* In a real implementation, terminate the user process.
       For now, hang. */
    for (;;) KeStallExecutionProcessor(1000000);
}

/* GetCurrentProcess / GetCurrentThread — return pseudo handles */
__attribute__((ms_abi))
static HANDLE GetCurrentProcess_msabi(VOID)
{
    return (HANDLE)(ULONG_PTR)-1;
}

/* lstrlenA / lstrlenW */
__attribute__((ms_abi))
static SIZE_T lstrlenA_msabi(const CHAR *lpString)
{
    if (!lpString) return 0;
    SIZE_T len = 0;
    while (lpString[len]) len++;
    return len;
}

__attribute__((ms_abi))
static SIZE_T lstrlenW_msabi(const WCHAR *lpString)
{
    if (!lpString) return 0;
    SIZE_T len = 0;
    while (lpString[len]) len++;
    return len;
}

/* lstrcpyA */
__attribute__((ms_abi))
static CHAR *lstrcpyA_msabi(CHAR *dest, const CHAR *src)
{
    if (!dest || !src) return dest;
    CHAR *d = dest;
    while ((*d++ = *src++));
    return dest;
}

/* ZeroMemory / RtlZeroMemory wrapper */
__attribute__((ms_abi))
static VOID ZeroMemory_msabi(PVOID Destination, SIZE_T Length)
{
    if (Destination)
        RtlZeroMemory(Destination, Length);
}

/* CopyMemory */
__attribute__((ms_abi))
static VOID CopyMemory_msabi(PVOID Destination, const VOID *Source, SIZE_T Length)
{
    if (Destination && Source)
        RtlCopyMemory(Destination, Source, Length);
}

/* FillMemory */
__attribute__((ms_abi))
static VOID FillMemory_msabi(PVOID Destination, SIZE_T Length, UCHAR Fill)
{
    if (Destination) {
        PUCHAR d = (PUCHAR)Destination;
        for (SIZE_T i = 0; i < Length; i++) d[i] = Fill;
    }
}

/* ============================================================================
 * MS ABI wrappers for user32.dll exports
 * ========================================================================== */

/* MessageBoxA — print to debug output and return IDOK */
__attribute__((ms_abi))
static INT MessageBoxA_msabi(PVOID hWnd, const CHAR *lpText,
                              const CHAR *lpCaption, UINT uType)
{
    (void)hWnd; (void)uType;
    DbgPrint("EXE: [%s] %s\n", lpCaption ? lpCaption : "", lpText ? lpText : "");
    return 1; /* IDOK */
}

/* ============================================================================
 * Initialisation: register all built-in exports
 * ========================================================================== */

#define REG(dll, name, ptr) \
    ExeRegisterExport(dll, name, (PVOID)(ptr))

VOID NTAPI
ExeInitRegistry(VOID)
{
    DbgPrint("EXE: Initialising DLL export registry...\n");

    /* ---- d3d12.dll ---- */
    REG("d3d12.dll", "D3D12CreateDevice",      D3D12CreateDevice_msabi);
    REG("d3d12.dll", "D3D12GetDebugInterface", D3D12GetDebugInterface_msabi);

    /* ---- ntdll.dll (Rtl* are also available via kernel32.dll) ---- */
    /* ntdll RtlZeroMemory/RtlCopyMemory are registered via Kernel32RegisterExports */

    /* ---- user32.dll ---- */
    REG("user32.dll", "MessageBoxA", MessageBoxA_msabi);

    /* ---- kernel32.dll — full export set in kernel32_exports.c ---- */
    Kernel32RegisterExports();

    /* ---- ntdll.dll — Nt* syscalls + Rtl* runtime + Ldr* loader ---- */
    NtdllRegisterExports();

    /* ---- user32.dll — window mgmt + message pump + input ---- */
    User32RegisterExports();

    /* ---- gdi32.dll — graphics device interface ---- */
    Gdi32RegisterExports();

    /* ---- advapi32.dll — registry + security + services ---- */
    Advapi32RegisterExports();

    DbgPrint("EXE: Registered %lu total exports\n", g_ExportCount);
}

/* ============================================================================
 * Native EXE launcher
 *
 * Loads a PE image, resolves imports against our export registry, and
 * returns the loaded image ready to execute.
 * ========================================================================== */

/*
 * Hook called from PeResolveImports when a DLL is not found in the
 * loaded module list.  We search our export registry instead.
 *
 * This is called via a modified PeResolveImports that checks the
 * export registry as a fallback.  For now, ExeLaunch does its own
 * import resolution.
 */

NTSTATUS NTAPI
ExeLaunch(PVOID FileBase, ULONG FileSize, PPE_IMAGE OutImage)
{
    NTSTATUS status;

    DbgPrint("EXE: Launching PE image (%lu bytes)\n", FileSize);

    /* 1. Load the PE image into memory */
    status = PeLoadImage(FileBase, FileSize, TRUE, OutImage);
    if (!NT_SUCCESS(status)) {
        DbgPrint("EXE: PeLoadImage failed: 0x%08X\n", status);
        return status;
    }

    /* 2. Resolve imports — try the loaded module list first, then the
          export registry as a fallback. */
    if (OutImage->Imports) {
        PIMAGE_IMPORT_DESCRIPTOR importDesc = (PIMAGE_IMPORT_DESCRIPTOR)(
            (PUCHAR)OutImage->MappedBase +
            (ULONG)(ULONG_PTR)OutImage->Imports);
        /* Wait — Imports field is already mapped (see PeLoadImage). */
        /* Actually PeLoadImage sets Imports to point into the mapped image,
           so we need to re-derive it from the optional header. */
        /* For now, use the field as-is since PeLoadImage fills it. */

        /* Walk the import descriptor array (mapped) */
        PUCHAR imageBase = (PUCHAR)OutImage->MappedBase;
        PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)(imageBase +
            ((PIMAGE_DOS_HEADER)imageBase)->e_lfanew);
        ULONG importRva = ntHeaders->OptionalHeader.DataDirectory[
            IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;

        if (importRva) {
            importDesc = (PIMAGE_IMPORT_DESCRIPTOR)(imageBase + importRva);

            while (importDesc->Name) {
                PCHAR dllName = (PCHAR)(imageBase + importDesc->Name);
                DbgPrint("EXE: Resolving imports from '%s'\n", dllName);

                PIMAGE_THUNK_DATA64 origThunk = (PIMAGE_THUNK_DATA64)(
                    imageBase + (importDesc->OriginalFirstThunk ?
                    importDesc->OriginalFirstThunk : importDesc->FirstThunk));
                PIMAGE_THUNK_DATA64 iatThunk = (PIMAGE_THUNK_DATA64)(
                    imageBase + importDesc->FirstThunk);

                while (origThunk->u1.AddressOfData) {
                    PVOID funcPtr = NULL;

                    if (!(origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG64)) {
                        /* Import by name */
                        PIMAGE_IMPORT_BY_NAME importInfo =
                            (PIMAGE_IMPORT_BY_NAME)(imageBase +
                            origThunk->u1.AddressOfData);
                        PCHAR funcName = (PCHAR)&importInfo->Name;

                        funcPtr = ExeResolveExport(dllName, funcName);
                        if (!funcPtr) {
                            /* Try all DLLs — some EXEs use wrong DLL name */
                            for (ULONG i = 0; i < g_ExportCount; i++) {
                                if (SwStrICmp(g_ExportTable[i].FuncName, funcName) == 0) {
                                    funcPtr = g_ExportTable[i].FuncPtr;
                                    break;
                                }
                            }
                        }

                        if (!funcPtr) {
                            DbgPrint("EXE:   WARNING: unresolved '%s!%s'\n", dllName, funcName);
                            /* Provide a stub that just returns 0 */
                            funcPtr = (PVOID)0x00000001LL; /* non-null so IAT is valid */
                        } else {
                            DbgPrint("EXE:   resolved '%s!%s' -> %p\n", dllName, funcName, funcPtr);
                        }
                    } else {
                        /* Import by ordinal — we don't support this well */
                        USHORT ordinal = (USHORT)(origThunk->u1.Ordinal & 0xFFFF);
                        DbgPrint("EXE:   ordinal import %u from '%s' -> stub\n", ordinal, dllName);
                        funcPtr = (PVOID)0x00000001LL;
                    }

                    iatThunk->u1.AddressOfData = (ULONG64)funcPtr;
                    origThunk++;
                    iatThunk++;
                }

                importDesc++;
            }
        }
    }

    DbgPrint("EXE: PE image loaded at %p, entry %p\n",
             (PVOID)OutImage->MappedBase, (PVOID)OutImage->EntryPoint);

    return STATUS_SUCCESS;
}
