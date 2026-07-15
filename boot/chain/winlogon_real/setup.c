/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Winlogon
 * FILE:            base/system/winlogon/setup.c
 * PURPOSE:         Setup support functions
 * PROGRAMMERS:     Eric Kohl
 */

/* INCLUDES *****************************************************************/


/* MinNT includes */
#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/ps.h>
#include <nt/cm.h>
#include <nt/se.h>
#include <nt/lpc.h>
#include <nt/rtl.h>
#include <nt/hal.h>
#include <nt/dispatcher.h>
#include <ndk/obfuncs.h>
#include <ndk/cmfuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/setypes.h>

#include "winlogon.h"

/* FUNCTIONS ****************************************************************/

DWORD
GetSetupType(VOID)
{
    DWORD dwError;
    HKEY hKey;
    DWORD dwType;
    DWORD dwSize;
    DWORD dwSetupType;

    TRACE("GetSetupType()\n");

    /* Open key */
    dwError = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                            L"SYSTEM\\Setup",
                            0,
                            0x00000001,
                            &hKey);
    if (dwError != ERROR_SUCCESS)
        return 0;

    /* Read key */
    dwSize = sizeof(DWORD);
    dwError = RegQueryValueExW(hKey,
                               L"SetupType",
                               NULL,
                               &dwType,
                               (LPBYTE)&dwSetupType,
                               &dwSize);

    /* Close key, and check if returned values are correct */
    RegCloseKey(hKey);
    if (dwError != ERROR_SUCCESS || dwType != 4 || dwSize != sizeof(DWORD))
        return 0;

    TRACE("GetSetupType() returns %lu\n", dwSetupType);
    return dwSetupType;
}


static
DWORD
WINAPI
RunSetupThreadProc(
    IN LPVOID lpParameter)
{
    PROCESS_INFORMATION ProcessInformation;
    STARTUPINFOW StartupInfo;
    WCHAR Shell[MAX_PATH];
    WCHAR CommandLine[MAX_PATH];
    BOOL Result;
    DWORD dwError;
    HKEY hKey;
    DWORD dwType;
    DWORD dwSize;
    DWORD dwExitCode;

    TRACE("RunSetup() called\n");

    /* Open key */
    dwError = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                            L"SYSTEM\\Setup",
                            0,
                            0x00000001,
                            &hKey);
    if (dwError != ERROR_SUCCESS)
        return FALSE;

    /* Read key */
    dwSize = sizeof(Shell);
    dwError = RegQueryValueExW(hKey,
                               L"CmdLine",
                               NULL,
                               &dwType,
                               (LPBYTE)Shell,
                               &dwSize);
    RegCloseKey(hKey);
    if (dwError != ERROR_SUCCESS)
        return FALSE;

    /* Finish string */
    Shell[dwSize / sizeof(WCHAR)] = UNICODE_NULL;

    /* Expand string (if applicable) */
    if (dwType == 2)
        ExpandEnvironmentStringsW(Shell, CommandLine, ARRAYSIZE(CommandLine));
    else if (dwType == 1)
        wcscpy(CommandLine, Shell);
    else
        return FALSE;

    TRACE("Should run '%s' now\n", debugstr_w(CommandLine));

    SwitchDesktop(WLSession->ApplicationDesktop);

    /* Start process */
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.lpReserved = NULL;
    StartupInfo.lpDesktop = L"WinSta0\\Default";
    StartupInfo.lpTitle = NULL;
    StartupInfo.dwFlags = 0;
    StartupInfo.cbReserved2 = 0;
    StartupInfo.lpReserved2 = 0;

    Result = CreateProcessW(NULL,
                            CommandLine,
                            NULL,
                            NULL,
                            FALSE,
                            DETACHED_PROCESS,
                            NULL,
                            NULL,
                            &StartupInfo,
                            &ProcessInformation);
    if (!Result)
    {
        TRACE("Failed to run setup process\n");
        SwitchDesktop(WLSession->WinlogonDesktop);
        return FALSE;
    }

    /* Wait for process termination */
    WaitForSingleObject(ProcessInformation.hProcess, INFINITE);

    GetExitCodeProcess(ProcessInformation.hProcess, &dwExitCode);

    /* Close handles */
    CloseHandle(ProcessInformation.hThread);
    CloseHandle(ProcessInformation.hProcess);

    // SwitchDesktop(WLSession->WinlogonDesktop);

    TRACE ("RunSetup() done\n");

    return TRUE;
}


BOOL
RunSetup(VOID)
{
    HANDLE hThread;

    hThread = CreateThread(NULL,
                           0,
                           RunSetupThreadProc,
                           NULL,
                           0,
                           NULL);
    if (hThread != NULL)
        CloseHandle(hThread);

    return hThread != NULL;
}

/* EOF */
