/*
 * MinNT - win32k/dirs.c
 * Directory and path operations for Win32k.
 *
 * Implements GetCurrentDirectory, SetCurrentDirectory, GetWindowsDirectory,
 * GetSystemDirectory, GetTempPath, GetPathName. These provide the shell
 * and applications with standard directory information.
 */

#include "precomp.h"
#include <nt/fs.h>

/* Forward decls: filesystem APIs from fs/fs.c. */
NTSTATUS NTAPI FsOpenFile(PCWSTR FileName, PFILE_OBJECT *OutFile);
NTSTATUS NTAPI NtClose(HANDLE Handle);

#define MAX_DIR_PATH 260

/* Internal static buffers for directory strings */
static WCHAR g_WindowsDir[MAX_DIR_PATH] = L"C:\\WINDOWS";
static WCHAR g_SystemDir[MAX_DIR_PATH] = L"C:\\WINDOWS\\SYSTEM32";
static WCHAR g_TempPath[MAX_DIR_PATH] = L"C:\\WINDOWS\\TEMP";
static WCHAR g_CurrentDir[MAX_DIR_PATH] = L"C:\\";
static ULONG g_WindowsDirLen = 10;
static ULONG g_SystemDirLen = 20;
static ULONG g_TempPathLen = 17;
static ULONG g_CurrentDirLen = 3;

/* UserSetWindowsDirectory: set the Windows directory path */
NTSTATUS NTAPI UserSetWindowsDirectory(PCWSTR Path)
{
    ULONG len = 0;

    if (!Path) return STATUS_INVALID_PARAMETER;

    while (len < MAX_DIR_PATH - 1 && Path[len]) len++;
    RtlCopyMemory(g_WindowsDir, Path, (len + 1) * sizeof(WCHAR));
    g_WindowsDir[len] = 0;
    g_WindowsDirLen = len;

    DbgPrint("DIRS: WindowsDirectory = '%ws'\n", g_WindowsDir);
    return STATUS_SUCCESS;
}

/* UserSetSystemDirectory: set the System32 directory path */
NTSTATUS NTAPI UserSetSystemDirectory(PCWSTR Path)
{
    ULONG len = 0;

    if (!Path) return STATUS_INVALID_PARAMETER;

    while (len < MAX_DIR_PATH - 1 && Path[len]) len++;
    RtlCopyMemory(g_SystemDir, Path, (len + 1) * sizeof(WCHAR));
    g_SystemDir[len] = 0;
    g_SystemDirLen = len;

    DbgPrint("DIRS: SystemDirectory = '%ws'\n", g_SystemDir);
    return STATUS_SUCCESS;
}

/* UserGetWindowsDirectoryW: get Windows directory */
NTSTATUS NTAPI UserGetWindowsDirectoryW(PWCHAR Buffer, ULONG BufferLen)
{
    if (!Buffer || BufferLen == 0) return STATUS_INVALID_PARAMETER;
    if (g_WindowsDirLen + 1 > BufferLen) return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(Buffer, g_WindowsDir, (g_WindowsDirLen + 1) * sizeof(WCHAR));
    return STATUS_SUCCESS;
}

/* UserGetSystemDirectoryW: get System32 directory */
NTSTATUS NTAPI UserGetSystemDirectoryW(PWCHAR Buffer, ULONG BufferLen)
{
    if (!Buffer || BufferLen == 0) return STATUS_INVALID_PARAMETER;
    if (g_SystemDirLen + 1 > BufferLen) return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(Buffer, g_SystemDir, (g_SystemDirLen + 1) * sizeof(WCHAR));
    return STATUS_SUCCESS;
}

/* UserGetTempPathW: get temporary directory path */
NTSTATUS NTAPI UserGetTempPathW(ULONG BufferLen, PWCHAR Buffer)
{
    if (!Buffer || BufferLen == 0) return STATUS_INVALID_PARAMETER;
    if (g_TempPathLen + 1 > BufferLen) return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(Buffer, g_TempPath, (g_TempPathLen + 1) * sizeof(WCHAR));
    return STATUS_SUCCESS;
}

/* UserGetCurrentDirectoryW: get current working directory */
NTSTATUS NTAPI UserGetCurrentDirectoryW(ULONG BufferLen, PWCHAR Buffer)
{
    if (!Buffer || BufferLen == 0) return STATUS_INVALID_PARAMETER;
    if (g_CurrentDirLen + 1 > BufferLen) return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(Buffer, g_CurrentDir, (g_CurrentDirLen + 1) * sizeof(WCHAR));
    return STATUS_SUCCESS;
}

/* UserSetCurrentDirectoryW: set current working directory */
NTSTATUS NTAPI UserSetCurrentDirectoryW(PCWSTR Path)
{
    ULONG len = 0;

    if (!Path) return STATUS_INVALID_PARAMETER;

    while (len < MAX_DIR_PATH - 1 && Path[len]) len++;
    RtlCopyMemory(g_CurrentDir, Path, (len + 1) * sizeof(WCHAR));
    g_CurrentDir[len] = 0;
    g_CurrentDirLen = len;

    DbgPrint("DIRS: CurrentDirectory = '%ws'\n", g_CurrentDir);
    return STATUS_SUCCESS;
}

/* UserGetFullPathNameW: get full path from relative path */
NTSTATUS NTAPI UserGetFullPathNameW(PCWSTR FileName, ULONG BufferLen, PWCHAR Buffer, PWSTR *pFilePart)
{
    ULONG nameLen, totalLen;

    if (!FileName || !Buffer || BufferLen == 0) return STATUS_INVALID_PARAMETER;

    nameLen = 0;
    while (nameLen < MAX_DIR_PATH - 1 && FileName[nameLen]) nameLen++;

    /* If absolute path, just copy */
    if (nameLen >= 2 && FileName[1] == L':') {
        if (nameLen + 1 > BufferLen) return STATUS_BUFFER_TOO_SMALL;
        RtlCopyMemory(Buffer, FileName, (nameLen + 1) * sizeof(WCHAR));
        if (pFilePart) {
            /* Find last backslash */
            LONG i;
            for (i = (LONG)nameLen - 1; i >= 0; i--) {
                if (Buffer[i] == L'\\' || Buffer[i] == L'/') {
                    *pFilePart = &Buffer[i + 1];
                    return STATUS_SUCCESS;
                }
            }
            *pFilePart = Buffer;
        }
        return STATUS_SUCCESS;
    }

    /* Relative path: prepend current directory */
    totalLen = g_CurrentDirLen;
    if (totalLen > 0 && g_CurrentDir[totalLen - 1] != L'\\') totalLen++;
    totalLen += nameLen;

    if (totalLen + 1 > BufferLen) return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(Buffer, g_CurrentDir, g_CurrentDirLen * sizeof(WCHAR));
    if (g_CurrentDir[g_CurrentDirLen - 1] != L'\\') {
        Buffer[g_CurrentDirLen] = L'\\';
        RtlCopyMemory(Buffer + g_CurrentDirLen + 1, FileName, (nameLen + 1) * sizeof(WCHAR));
    } else {
        RtlCopyMemory(Buffer + g_CurrentDirLen, FileName, (nameLen + 1) * sizeof(WCHAR));
    }

    if (pFilePart) {
        LONG i;
        for (i = (LONG)totalLen - 1; i >= 0; i--) {
            if (Buffer[i] == L'\\' || Buffer[i] == L'/') {
                *pFilePart = &Buffer[i + 1];
                return STATUS_SUCCESS;
            }
        }
        *pFilePart = Buffer;
    }

    return STATUS_SUCCESS;
}

/* UserPathAppendW: append a path component */
NTSTATUS NTAPI UserPathAppendW(PWCHAR Base, PCWSTR Append, ULONG BufferLen)
{
    ULONG baseLen, appLen, totalLen;

    if (!Base || !Append || BufferLen == 0) return STATUS_INVALID_PARAMETER;

    baseLen = 0;
    while (baseLen < BufferLen && Base[baseLen]) baseLen++;

    appLen = 0;
    while (appLen < BufferLen && Append[appLen]) appLen++;

    totalLen = baseLen;
    if (totalLen > 0 && Base[totalLen - 1] != L'\\') {
        if (totalLen + 1 >= BufferLen) return STATUS_BUFFER_TOO_SMALL;
        Base[totalLen] = L'\\';
        totalLen++;
    }

    if (totalLen + appLen + 1 > BufferLen) return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(Base + totalLen, Append, (appLen + 1) * sizeof(WCHAR));

    return STATUS_SUCCESS;
}

/* UserPathFileExistsW: check if a path exists.
 * Uses FsOpenFile to attempt to open the file - if it succeeds, the
 * file exists; if it returns STATUS_OBJECT_NAME_NOT_FOUND, it doesn't. */
NTSTATUS NTAPI UserPathFileExistsW(PCWSTR Path, PULONG pExists)
{
    PFILE_OBJECT pFile;
    NTSTATUS status;

    if (!Path || !pExists) return STATUS_INVALID_PARAMETER;

    *pExists = 0;

    /* Empty path is invalid */
    if (Path[0] == 0) return STATUS_INVALID_PARAMETER;

    status = FsOpenFile(Path, &pFile);
    if (NT_SUCCESS(status)) {
        *pExists = 1;
        NtClose((HANDLE)pFile);
        return STATUS_SUCCESS;
    }

    /* Common "not found" status codes: file/directory don't exist. */
    if (status == STATUS_OBJECT_NAME_NOT_FOUND ||
        status == STATUS_NO_SUCH_FILE ||
        status == STATUS_OBJECT_PATH_NOT_FOUND ||
        status == STATUS_NOT_A_DIRECTORY) {
        *pExists = 0;
        return STATUS_SUCCESS;
    }

    /* Other errors: treat as not found but propagate status. */
    return status;
}

/* UserPathFindFileNameW: get pointer to filename component */
NTSTATUS NTAPI UserPathFindFileNameW(PCWSTR Path, PWSTR *ppFileName)
{
    ULONG len;
    LONG i;

    if (!Path || !ppFileName) return STATUS_INVALID_PARAMETER;

    len = 0;
    while (Path[len]) len++;

    for (i = (LONG)len - 1; i >= 0; i--) {
        if (Path[i] == L'\\' || Path[i] == L'/') {
            *ppFileName = (PWSTR)&Path[i + 1];
            return STATUS_SUCCESS;
        }
    }

    *ppFileName = (PWSTR)Path;
    return STATUS_SUCCESS;
}
