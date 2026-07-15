/*
 * MinNT - win32k/libmgmt.c
 * Library management for Win32k.
 */

#include "precomp.h"

#define MAX_LOADED_LIBS 16

typedef struct _LOADED_LIBRARY {
    WCHAR       Name[64];
    ULONG_PTR   BaseAddress;
    ULONG       Size;
    PVOID       EntryPoint;
    ULONG       RefCount;
    BOOLEAN     InUse;
} LOADED_LIBRARY, *PLOADED_LIBRARY;

static LOADED_LIBRARY g_LoadedLibs[MAX_LOADED_LIBS];

NTSTATUS NTAPI LibMgmtInit(VOID)
{
    RtlZeroMemory(g_LoadedLibs, sizeof(g_LoadedLibs));
    g_LoadedLibs[0].InUse = TRUE;
    g_LoadedLibs[0].BaseAddress = 0;
    g_LoadedLibs[0].Size = 0x100000;
    g_LoadedLibs[0].RefCount = 1;
    RtlCopyMemory(g_LoadedLibs[0].Name, L"win32k.sys", 10 * sizeof(WCHAR));
    DbgPrint("LIBMGMT: initialized\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserLoadLibraryW(PCWSTR LibraryName, PHANDLE phModule)
{
    ULONG i, nameLen;
    if (!LibraryName || !phModule) return STATUS_INVALID_PARAMETER;
    nameLen = 0;
    while (nameLen < 63 && LibraryName[nameLen]) nameLen++;

    for (i = 0; i < MAX_LOADED_LIBS; i++) {
        if (g_LoadedLibs[i].InUse) {
            BOOLEAN match = TRUE;
            ULONG j;
            for (j = 0; j < nameLen; j++) {
                if (g_LoadedLibs[i].Name[j] != LibraryName[j]) { match = FALSE; break; }
            }
            if (match && g_LoadedLibs[i].Name[nameLen] == 0) {
                g_LoadedLibs[i].RefCount++;
                *phModule = (HANDLE)g_LoadedLibs[i].BaseAddress;
                return STATUS_SUCCESS;
            }
        }
    }

    for (i = 0; i < MAX_LOADED_LIBS; i++) {
        if (!g_LoadedLibs[i].InUse) {
            RtlCopyMemory(g_LoadedLibs[i].Name, LibraryName, nameLen * sizeof(WCHAR));
            g_LoadedLibs[i].Name[nameLen] = 0;
            g_LoadedLibs[i].InUse = TRUE;
            g_LoadedLibs[i].RefCount = 1;
            g_LoadedLibs[i].BaseAddress = 0;
            g_LoadedLibs[i].Size = 0x10000;
            *phModule = (HANDLE)0;
            DbgPrint("LIBMGMT: LoadLibrary '%ws' -> slot %u\n", LibraryName, i);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS NTAPI UserFreeLibrary(HANDLE hModule)
{
    ULONG i;
    for (i = 0; i < MAX_LOADED_LIBS; i++) {
        if (g_LoadedLibs[i].InUse && (HANDLE)g_LoadedLibs[i].BaseAddress == hModule) {
            g_LoadedLibs[i].RefCount--;
            if (g_LoadedLibs[i].RefCount == 0) {
                g_LoadedLibs[i].InUse = FALSE;
            }
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserGetProcAddress(HANDLE hModule, PCSTR ProcName, PVOID *pAddr)
{
    if (!ProcName || !pAddr) return STATUS_INVALID_PARAMETER;
    *pAddr = NULL;
    DbgPrint("LIBMGMT: GetProcAddress(%p, '%s')\n", hModule, ProcName);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetModuleHandleW(PCWSTR ModuleName, PHANDLE phModule)
{
    ULONG i, nameLen;
    if (!phModule) return STATUS_INVALID_PARAMETER;
    if (!ModuleName) { *phModule = NULL; return STATUS_SUCCESS; }

    nameLen = 0;
    while (nameLen < 63 && ModuleName[nameLen]) nameLen++;

    for (i = 0; i < MAX_LOADED_LIBS; i++) {
        if (g_LoadedLibs[i].InUse) {
            BOOLEAN match = TRUE;
            ULONG j;
            for (j = 0; j < nameLen; j++) {
                if (g_LoadedLibs[i].Name[j] != ModuleName[j]) { match = FALSE; break; }
            }
            if (match && g_LoadedLibs[i].Name[nameLen] == 0) {
                *phModule = (HANDLE)g_LoadedLibs[i].BaseAddress;
                return STATUS_SUCCESS;
            }
        }
    }
    *phModule = NULL;
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserGetModuleFileNameW(HANDLE hModule, PWCHAR Buffer, ULONG BufferLen)
{
    ULONG i, nameLen;
    if (!Buffer || BufferLen == 0) return STATUS_INVALID_PARAMETER;

    for (i = 0; i < MAX_LOADED_LIBS; i++) {
        if (g_LoadedLibs[i].InUse && (HANDLE)g_LoadedLibs[i].BaseAddress == hModule) {
            nameLen = 0;
            while (nameLen < 63 && g_LoadedLibs[i].Name[nameLen]) nameLen++;
            if (nameLen + 1 > BufferLen) return STATUS_BUFFER_TOO_SMALL;
            RtlCopyMemory(Buffer, g_LoadedLibs[i].Name, (nameLen + 1) * sizeof(WCHAR));
            return STATUS_SUCCESS;
        }
    }
    Buffer[0] = 0;
    return STATUS_NOT_FOUND;
}
