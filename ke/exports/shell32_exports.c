/*
 * MinNT - ke/exports/shell32_exports.c
 * shell32.dll exports — shell functions, folder paths, file operations.
 */

#include <nt/ntdef.h>
#include <nt/ex.h>
#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/exe.h>
#include <nt/rtl.h>
#include <ndk/obfuncs.h>

typedef LONG HRESULT;
#ifndef UINT
typedef unsigned int UINT;
#endif
/* HINSTANCE comes from ntdef.h */
typedef ULONG_PTR UINT_PTR;
typedef ULONG_PTR HWND;
typedef ULONG_PTR HMENU;
typedef ULONG_PTR LPCITEMIDLIST;

#define S_OK_shell    ((HRESULT)0)
#define S_FALSE_shell ((HRESULT)1)

/* CSIDL constants */
#define CSIDL_DESKTOP           0x00
#define CSIDL_PROGRAMS          0x02
#define CSIDL_PERSONAL          0x05
#define CSIDL_FAVORITES         0x06
#define CSIDL_STARTUP           0x07
#define CSIDL_RECENT            0x08
#define CSIDL_SENDTO            0x09
#define CSIDL_BITBUCKET         0x0A
#define CSIDL_STARTMENU         0x0B
#define CSIDL_DESKTOPDIRECTORY  0x10
#define CSIDL_DRIVES            0x11
#define CSIDL_NETWORK           0x12
#define CSIDL_NETHOOD           0x13
#define CSIDL_FONTS             0x14
#define CSIDL_TEMPLATES         0x15
#define CSIDL_COMMON_STARTMENU  0x16
#define CSIDL_COMMON_PROGRAMS   0x17
#define CSIDL_COMMON_STARTUP    0x18
#define CSIDL_COMMON_DESKTOPDIR 0x19
#define CSIDL_APPDATA           0x1A
#define CSIDL_PRINTHOOD         0x1B
#define CSIDL_FLAG_CREATE       0x8000

__attribute__((ms_abi))
static HRESULT SHGetFolderPathA_msabi(HWND hwnd, int csidl, HINSTANCE hInst, ULONG dwFlags, CHAR *pszPath)
{
    (void)hwnd; (void)hInst; (void)dwFlags;
    if (!pszPath) return 0x80070057L;
    const CHAR *path;
    switch (csidl & 0xFF) {
    case CSIDL_DESKTOP:           path = "C:\\Desktop"; break;
    case CSIDL_PROGRAMS:         path = "C:\\Programs"; break;
    case CSIDL_PERSONAL:         path = "C:\\My Documents"; break;
    case CSIDL_STARTMENU:        path = "C:\\Start Menu"; break;
    case CSIDL_DESKTOPDIRECTORY: path = "C:\\Desktop"; break;
    case CSIDL_DRIVES:           path = "C:"; break;
    case CSIDL_FONTS:            path = "C:\\Fonts"; break;
    case CSIDL_APPDATA:          path = "C:\\AppData"; break;
    case CSIDL_TEMPLATES:        path = "C:\\Temp"; break;
    case CSIDL_FAVORITES:        path = "C:\\Favorites"; break;
    case CSIDL_STARTUP:          path = "C:\\Startup"; break;
    case CSIDL_RECENT:           path = "C:\\Recent"; break;
    case CSIDL_SENDTO:           path = "C:\\SendTo"; break;
    case CSIDL_NETHOOD:          path = "C:\\Nethood"; break;
    case CSIDL_PRINTHOOD:        path = "C:\\Printhood"; break;
    case CSIDL_NETWORK:          path = "C:\\Network"; break;
    case CSIDL_COMMON_STARTMENU: path = "C:\\Common Start Menu"; break;
    case CSIDL_COMMON_PROGRAMS:  path = "C:\\Common Programs"; break;
    case CSIDL_COMMON_STARTUP:   path = "C:\\Common Startup"; break;
    case CSIDL_COMMON_DESKTOPDIR: path = "C:\\Common Desktop"; break;
    default:                     path = "C:\\"; break;
    }
    UINT i = 0;
    while (path[i] && i < 259) { pszPath[i] = path[i]; i++; }
    pszPath[i] = 0;
    return S_OK_shell;
}

__attribute__((ms_abi))
static HRESULT SHGetFolderPathW_msabi(HWND hwnd, int csidl, HINSTANCE hInst, ULONG dwFlags, WCHAR *pszPath)
{
    (void)hwnd; (void)hInst; (void)dwFlags;
    CHAR pathA[260];
    HRESULT hr = SHGetFolderPathA_msabi(0, csidl, 0, dwFlags, pathA);
    if (!NT_SUCCESS((NTSTATUS)hr)) return hr;
    UINT i = 0;
    while (pathA[i]) { pszPath[i] = (WCHAR)pathA[i]; i++; }
    pszPath[i] = 0;
    return S_OK_shell;
}

__attribute__((ms_abi))
static HRESULT SHGetSpecialFolderPathA_msabi(HWND hwnd, CHAR *pszPath, int csidl, BOOL fCreate)
{
    (void)fCreate;
    return SHGetFolderPathA_msabi(hwnd, csidl, 0, 0, pszPath);
}

__attribute__((ms_abi))
static HRESULT SHGetSpecialFolderPathW_msabi(HWND hwnd, WCHAR *pszPath, int csidl, BOOL fCreate)
{
    (void)fCreate;
    return SHGetFolderPathW_msabi(hwnd, csidl, 0, 0, pszPath);
}

__attribute__((ms_abi))
static UINT_PTR SHGetFileInfoA_msabi(const CHAR *pszPath, ULONG dwFileAttributes,
    PVOID psfi, UINT cbFileInfo, UINT uFlags)
{
    (void)pszPath; (void)dwFileAttributes; (void)uFlags;
    if (psfi && cbFileInfo >= 4) RtlZeroMemory(psfi, cbFileInfo);
    return 0;
}

__attribute__((ms_abi))
static ULONG_PTR SHBrowseForFolderA_msabi(PVOID lpbi)
{
    (void)lpbi;
    return 0; /* No folder browser for now */
}

__attribute__((ms_abi))
static HRESULT SHGetPathFromIDListA_msabi(LPCITEMIDLIST pidl, CHAR *pszPath)
{
    if (pszPath) { pszPath[0] = 'C'; pszPath[1] = ':'; pszPath[2] = '\\'; pszPath[3] = 0; }
    return S_OK_shell;
}

__attribute__((ms_abi))
static HINSTANCE ShellExecuteA_msabi(HWND hwnd, const CHAR *lpOperation,
    const CHAR *lpFile, const CHAR *lpParameters, const CHAR *lpDirectory,
    INT nShowCmd)
{
    (void)hwnd; (void)lpOperation; (void)lpParameters; (void)lpDirectory; (void)nShowCmd;
    if (lpFile) DbgPrint("EXE: ShellExecuteA('%s')\n", lpFile);
    return (HINSTANCE)42; /* success handle > 32 */
}

__attribute__((ms_abi))
static HINSTANCE ShellExecuteW_msabi(HWND hwnd, const WCHAR *lpVerb,
    const WCHAR *lpFile, const WCHAR *lpParameters, const WCHAR *lpDirectory, INT nShowCmd)
{
    (void)hwnd; (void)lpVerb; (void)lpParameters; (void)lpDirectory; (void)nShowCmd;
    if (lpFile) {
        CHAR path[260];
        UINT i;
        for (i = 0; lpFile[i] && i < 259; i++) path[i] = (CHAR)lpFile[i];
        path[i] = 0;
        DbgPrint("EXE: ShellExecuteW('%s')\n", path);
    }
    return (HINSTANCE)42;
}

__attribute__((ms_abi))
static BOOL Shell_NotifyIconA_msabi(ULONG dwMessage, PVOID lpData)
{
    (void)dwMessage; (void)lpData;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL Shell_NotifyIconW_msabi(ULONG dwMessage, PVOID lpData)
{
    (void)dwMessage; (void)lpData;
    return TRUE;
}

__attribute__((ms_abi))
static HRESULT SHGetDesktopFolder_msabi(void *ppshf)
{
    if (ppshf) *(PVOID *)ppshf = (PVOID)0x90000000LL;
    return S_OK_shell;
}

__attribute__((ms_abi))
static INT SHFileOperationA_msabi(PVOID lpFileOp)
{
    /* SHFILEOPSTRUCTA — preserves shell file operations semantics */
    (void)lpFileOp;
    return 0; /* success */
}

__attribute__((ms_abi))
static BOOL IsUserAnAdmin_msabi(VOID)
{
    return TRUE; /* Everyone's admin in MinNT */
}

__attribute__((ms_abi))
static HRESULT CoInitialize_msabi_shell32(PVOID pvReserved)
{
    (void)pvReserved;
    return S_OK_shell; /* also exported by shell32, route through ole32 CoInitialize */
}

/* ============================================================================
 * Registration
 * ========================================================================== */

VOID NTAPI Shell32RegisterExports(VOID)
{
    ExeRegisterExport("shell32.dll", "SHGetFolderPathA",         SHGetFolderPathA_msabi);
    ExeRegisterExport("shell32.dll", "SHGetFolderPathW",         SHGetFolderPathW_msabi);
    ExeRegisterExport("shell32.dll", "SHGetSpecialFolderPathA", SHGetSpecialFolderPathA_msabi);
    ExeRegisterExport("shell32.dll", "SHGetSpecialFolderPathW", SHGetSpecialFolderPathW_msabi);
    ExeRegisterExport("shell32.dll", "SHGetFileInfoA",           SHGetFileInfoA_msabi);
    ExeRegisterExport("shell32.dll", "SHBrowseForFolderA",       SHBrowseForFolderA_msabi);
    ExeRegisterExport("shell32.dll", "SHGetPathFromIDListA",    SHGetPathFromIDListA_msabi);
    ExeRegisterExport("shell32.dll", "ShellExecuteA",            ShellExecuteA_msabi);
    ExeRegisterExport("shell32.dll", "ShellExecuteW",            ShellExecuteW_msabi);
    ExeRegisterExport("shell32.dll", "Shell_NotifyIconA",        Shell_NotifyIconA_msabi);
    ExeRegisterExport("shell32.dll", "Shell_NotifyIconW",        Shell_NotifyIconW_msabi);
    ExeRegisterExport("shell32.dll", "SHGetDesktopFolder",      SHGetDesktopFolder_msabi);
    ExeRegisterExport("shell32.dll", "SHFileOperationA",          SHFileOperationA_msabi);
    ExeRegisterExport("shell32.dll", "IsUserAnAdmin",            IsUserAnAdmin_msabi);
    ExeRegisterExport("shell32.dll", "CoInitialize",              CoInitialize_msabi_shell32);

    DbgPrint("EXE: shell32.dll exports registered (%lu total)\n", g_ExportCount);
}
