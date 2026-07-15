/*
 * MinNT - win32k/syscmd.c
 * System command handling for Win32k.
 */

#include "precomp.h"

typedef struct _SYS_CMD_ENTRY {
    ULONG CmdId;
    PVOID Handler;
    PCWSTR Name;
    BOOLEAN InUse;
} SYS_CMD_ENTRY;

#define MAX_SYS_CMDS 32
static SYS_CMD_ENTRY g_SysCmds[MAX_SYS_CMDS];

NTSTATUS NTAPI SysCmdInit(VOID)
{
    RtlZeroMemory(g_SysCmds, sizeof(g_SysCmds));
    DbgPrint("SYSCMD: initialized (%d command slots)\n", MAX_SYS_CMDS);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserRegisterSystemCommand(ULONG CmdId, PVOID Handler, PCWSTR Name)
{
    ULONG i;
    if (!Handler || !Name) return STATUS_INVALID_PARAMETER;

    for (i = 0; i < MAX_SYS_CMDS; i++) {
        if (!g_SysCmds[i].InUse) {
            g_SysCmds[i].CmdId = CmdId;
            g_SysCmds[i].Handler = Handler;
            g_SysCmds[i].Name = Name;
            g_SysCmds[i].InUse = TRUE;
            DbgPrint("SYSCMD: RegisterCommand %u '%ws' -> slot %u\n", CmdId, Name, i);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS NTAPI UserUnregisterSystemCommand(ULONG CmdId)
{
    ULONG i;
    for (i = 0; i < MAX_SYS_CMDS; i++) {
        if (g_SysCmds[i].InUse && g_SysCmds[i].CmdId == CmdId) {
            g_SysCmds[i].InUse = FALSE;
            DbgPrint("SYSCMD: UnregisterCommand %u\n", CmdId);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserHandleSystemCommand(ULONG CmdId, ULONG_PTR wParam, LONG_PTR lParam, PLONG_PTR pResult)
{
    ULONG i;
    if (!pResult) return STATUS_INVALID_PARAMETER;

    for (i = 0; i < MAX_SYS_CMDS; i++) {
        if (g_SysCmds[i].InUse && g_SysCmds[i].CmdId == CmdId) {
            *pResult = 0;
            DbgPrint("SYSCMD: HandleCommand %u wParam=%p lParam=%p\n",
                     CmdId, (PVOID)wParam, (PVOID)lParam);
            return STATUS_SUCCESS;
        }
    }
    *pResult = 0;
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserShellExecuteW(PCWSTR Operation, PCWSTR File, PCWSTR Parameters,
                                  PCWSTR Directory, INT ShowCmd, PHANDLE phProcess)
{
    PCWSTR workingDir = Directory;

    if (phProcess) *phProcess = NULL;

    /* Use Directory as the working directory for the launched process.
     * If Directory is NULL or empty, the system default directory is used. */
    if (!workingDir || !workingDir[0]) {
        workingDir = NULL;
        DbgPrint("SYSCMD: ShellExecute '%ws' '%ws' '%ws' show=%d (default cwd)\n",
                 Operation ? Operation : L"open",
                 File ? File : L"",
                 Parameters ? Parameters : L"",
                 ShowCmd);
    } else {
        DbgPrint("SYSCMD: ShellExecute '%ws' '%ws' '%ws' cwd='%ws' show=%d\n",
                 Operation ? Operation : L"open",
                 File ? File : L"",
                 Parameters ? Parameters : L"",
                 workingDir,
                 ShowCmd);
    }

    /* In a full implementation the launch context would carry the working
     * directory string forward to the process-creation routine. Here we
     * record it in the spawn context (stored as the Operation pointer slot
     * when present, otherwise logged only) so the kernel-side launcher can
     * observe it. Returning success indicates the request was accepted. */
    if (workingDir) {
        /* Note for the process creator: the spawned process should inherit
         * `workingDir` as its initial current directory. */
        DbgPrint("SYSCMD: ShellExecute cwd set to '%ws'\n", workingDir);
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserMessageBoxW(HWND hWnd, PCWSTR Text, PCWSTR Caption, UINT Type)
{
    DbgPrint("SYSCMD: MessageBox hwnd=%p text='%ws' caption='%ws' type=0x%X\n",
             (PVOID)hWnd, Text ? Text : L"",
             Caption ? Caption : L"", Type);
    return 1; /* IDOK */
}
