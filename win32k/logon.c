/*
 * MinNT - win32k/logon.c
 * Logon UI support for Win32k.
 */

#include "precomp.h"
#include <nt/ke.h>

/* Forward declaration: defined in usermsg.c, used here for synchronous
 * WM_QUERYENDSESSION response checking. */
LONG_PTR APIENTRY UserSendMessage(ULONG_PTR hWnd, ULONG Msg,
                                  ULONG_PTR wParam, LONG_PTR lParam);

/* Forward declaration: defined in hal/hal.c */
VOID NTAPI HalpRebootSystem(VOID);

#define MAX_SHUTDOWN_WINDOWS 64

typedef struct _LOGON_STATE {
    BOOL    LoggedIn;
    WCHAR   UserName[64];
    WCHAR   Domain[64];
    WCHAR   Password[64];
    ULONG   SessionId;
    ULONG   LogonTime;
} LOGON_STATE;

typedef struct _SHUTDOWN_STATE {
    BOOL    InProgress;
    ULONG   Timeout;
    BOOL    ForceApps;
    BOOL    RebootAfterShutdown;
    BOOL    RemoteRequested;
    WCHAR   MachineName[64];
    WCHAR   Message[256];
} SHUTDOWN_STATE;

static LOGON_STATE g_LogonState;
static SHUTDOWN_STATE g_ShutdownState;

/* Timer + DPC used to actually perform the shutdown after Timeout seconds.
 * When the timer fires, this DPC executes (on the timer scan thread) and
 * either calls HalpRebootSystem() (for RebootAfterShutdown) or HalpHalt. */
static KTIMER g_ShutdownTimer;
static KDPC g_ShutdownDpc;

/* DPC routine invoked when the shutdown timer expires.
 *
 * Parameters:
 *   Dpc     - the KDPC that fired (from the timer)
 *   Context - user-supplied context (we passed NULL)
 *   Arg1/Arg2 - system arguments (unused in our setup)
 */
static VOID NTAPI ShutdownTimerDpc(PKDPC Dpc, PVOID Context,
                                    PVOID Arg1, PVOID Arg2)
{
    BOOLEAN reboot = g_ShutdownState.RebootAfterShutdown;

    DbgPrint("LOGON: Shutdown timer fired (Dpc=%p, Context=%p, Arg1=%p, Arg2=%p) "
             "- %s\n",
             (PVOID)Dpc, Context, Arg1, Arg2,
             reboot ? "rebooting" : "halting");

    /* Mark shutdown as no longer in progress. */
    g_ShutdownState.InProgress = FALSE;

    if (reboot) {
        HalpRebootSystem();
    } else {
        /* Shutdown sequence: try the ACPI soft-off, then halt as fallback. */
        KeDisableInterrupts();
        for (;;) {
            KeHaltProcessor();
        }
    }
}

NTSTATUS NTAPI LogonInit(VOID)
{
    RtlZeroMemory(&g_LogonState, sizeof(g_LogonState));
    g_LogonState.LoggedIn = TRUE;
    g_LogonState.SessionId = 1;
    RtlCopyMemory(g_LogonState.UserName, L"User", 5 * sizeof(WCHAR));
    RtlCopyMemory(g_LogonState.Domain, L"WORKGROUP", 10 * sizeof(WCHAR));

    /* Initialize shutdown timer/DPC. */
    KeInitializeTimer(&g_ShutdownTimer);
    KeInitializeDpc(&g_ShutdownDpc, ShutdownTimerDpc, NULL);

    DbgPrint("LOGON: initialized (session %u, user '%ws')\n",
             g_LogonState.SessionId, g_LogonState.UserName);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserLogonUserW(PCWSTR UserName, PCWSTR Domain, PCWSTR Password,
                                PULONG pSessionId)
{
    ULONG len;

    if (!UserName || !pSessionId) return STATUS_INVALID_PARAMETER;

    /* Validate the credential: a non-null, non-empty password is required. */
    if (!Password || Password[0] == 0) {
        DbgPrint("LOGON: UserLogon '%ws' rejected - empty password\n", UserName);
        return STATUS_WRONG_PASSWORD;
    }

    len = 0;
    while (len < 63 && UserName[len]) len++;
    RtlCopyMemory(g_LogonState.UserName, UserName, (len + 1) * sizeof(WCHAR));

    if (Domain) {
        len = 0;
        while (len < 63 && Domain[len]) len++;
        RtlCopyMemory(g_LogonState.Domain, Domain, (len + 1) * sizeof(WCHAR));
    }

    /* Store the supplied credential in the logon session credential store. */
    len = 0;
    while (len < 63 && Password[len]) len++;
    RtlCopyMemory(g_LogonState.Password, Password, (len + 1) * sizeof(WCHAR));

    g_LogonState.LoggedIn = TRUE;
    g_LogonState.LogonTime = (ULONG)KeTickCount;
    *pSessionId = g_LogonState.SessionId;

    DbgPrint("LOGON: UserLogon '%ws\\%ws' cred=%u chars -> session %u\n",
             g_LogonState.Domain, g_LogonState.UserName, len,
             g_LogonState.SessionId);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserLogoffUser(ULONG SessionId)
{
    g_LogonState.LoggedIn = FALSE;
    DbgPrint("LOGON: Logoff session %u\n", SessionId);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetLoggedOnUser(PWCHAR UserName, ULONG UserNameLen,
                                    PWCHAR Domain, ULONG DomainLen)
{
    ULONG nameLen, domLen;
    if (!UserName || UserNameLen == 0) return STATUS_INVALID_PARAMETER;

    nameLen = 0;
    while (nameLen < 63 && g_LogonState.UserName[nameLen]) nameLen++;
    if (nameLen + 1 > UserNameLen) return STATUS_BUFFER_TOO_SMALL;
    RtlCopyMemory(UserName, g_LogonState.UserName, (nameLen + 1) * sizeof(WCHAR));

    if (Domain && DomainLen > 0) {
        domLen = 0;
        while (domLen < 63 && g_LogonState.Domain[domLen]) domLen++;
        if (domLen + 1 > DomainLen) return STATUS_BUFFER_TOO_SMALL;
        RtlCopyMemory(Domain, g_LogonState.Domain, (domLen + 1) * sizeof(WCHAR));
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserIsUserLoggedOn(PULONG pLoggedIn)
{
    if (!pLoggedIn) return STATUS_INVALID_PARAMETER;
    *pLoggedIn = g_LogonState.LoggedIn ? 1 : 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetLogonSessionId(PULONG pSessionId)
{
    if (!pSessionId) return STATUS_INVALID_PARAMETER;
    *pSessionId = g_LogonState.SessionId;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserLockWorkStation(VOID)
{
    DbgPrint("LOGON: LockWorkStation\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserExitWindowsEx(ULONG Flags, ULONG Reason)
{
    DbgPrint("LOGON: ExitWindowsEx flags=0x%X reason=0x%X\n", Flags, Reason);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserInitiateSystemShutdownW(PCWSTR MachineName, PCWSTR Message,
                                             ULONG Timeout, BOOL ForceApps,
                                             BOOL RebootAfterShutdown)
{
    NTSTATUS Status;
    ULONG count = 0, i, msgLen = 0, mnLen = 0;
    ULONG_PTR Hwnds[MAX_SHUTDOWN_WINDOWS];
    BOOL anyVeto = FALSE;
    LONG_PTR resp;
    LARGE_INTEGER dueTime;

    /* --- Capture MachineName (local-only build) --- */
    RtlZeroMemory(&g_ShutdownState, sizeof(g_ShutdownState));
    g_ShutdownState.InProgress = TRUE;
    g_ShutdownState.Timeout = Timeout;
    g_ShutdownState.ForceApps = ForceApps;
    g_ShutdownState.RebootAfterShutdown = RebootAfterShutdown;

    if (MachineName) {
        g_ShutdownState.RemoteRequested = TRUE;
        while (mnLen < 63 && MachineName[mnLen]) mnLen++;
        RtlCopyMemory(g_ShutdownState.MachineName, MachineName,
                      (mnLen + 1) * sizeof(WCHAR));
        DbgPrint("LOGON: InitiateSystemShutdown on '%ws' requested - "
                 "this build is local-only, applying locally\n",
                 g_ShutdownState.MachineName);
    }

    /* --- Capture shutdown message --- */
    if (Message) {
        while (msgLen < 255 && Message[msgLen]) msgLen++;
        RtlCopyMemory(g_ShutdownState.Message, Message,
                      (msgLen + 1) * sizeof(WCHAR));
        DbgPrint("LOGON: Shutdown message: '%ws'\n", g_ShutdownState.Message);
    }

    DbgPrint("LOGON: InitiateSystemShutdown timeout=%u force=%d reboot=%d\n",
             Timeout, ForceApps, RebootAfterShutdown);

    /* --- Enumerate top-level windows --- */
    Status = UserEnumWindows(&count, Hwnds, MAX_SHUTDOWN_WINDOWS);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("LOGON: UserEnumWindows failed 0x%08X\n", Status);
        count = 0;
    }
    DbgPrint("LOGON: Shutdown: querying %u top-level window(s)\n", count);

    /* --- Phase 1: WM_QUERYENDSESSION to every top-level window --- */
    for (i = 0; i < count; i++) {
        resp = UserSendMessage(Hwnds[i], WM_QUERYENDSESSION, 0, 0);
        /* A window returning FALSE (0) vetoes the shutdown. */
        if (resp == 0) {
            anyVeto = TRUE;
            DbgPrint("LOGON:   hwnd %p vetoed WM_QUERYENDSESSION\n",
                     (PVOID)Hwnds[i]);
        }
    }

    /* ForceApps ignores vetoes and ends the session regardless. */
    if (anyVeto && !ForceApps) {
        DbgPrint("LOGON: Shutdown aborted - application(s) vetoed and "
                 "ForceApps is FALSE\n");
        g_ShutdownState.InProgress = FALSE;
        return STATUS_REQUEST_ABORTED;
    }

    /* --- Phase 2: broadcast WM_ENDSESSION to all top-level windows --- */
    DbgPrint("LOGON: Broadcasting WM_ENDSESSION (force=%d) to %u window(s)\n",
             ForceApps, count);
    for (i = 0; i < count; i++) {
        /* wParam = TRUE: the session is ending. lParam carries the shutdown
         * message pointer so applications can display it. */
        UserSendMessage(Hwnds[i], WM_ENDSESSION, (ULONG_PTR)TRUE,
                        (LONG_PTR)(msgLen ? g_ShutdownState.Message : NULL));
    }
    /* Also post a best-effort broadcast (HWND_BROADCAST == 0) for any
     * listeners not tracked in the task table. */
    UserPostMessage(0, WM_ENDSESSION, (ULONG_PTR)TRUE,
                    (LONG_PTR)(msgLen ? g_ShutdownState.Message : NULL));

    /* --- Phase 3: actually schedule the shutdown via the kernel timer --- */
    /* Cancel any previous shutdown timer in case this is a retry. */
    KeCancelTimer(&g_ShutdownTimer);

    /* Timeout is in seconds. Convert to 100ns units (NT convention). */
    if (Timeout == 0) {
        /* Immediate shutdown */
        dueTime.QuadPart = -10000LL; /* 1ms relative */
    } else {
        dueTime.QuadPart = -((LONGLONG)Timeout) * 10000000LL; /* seconds -> 100ns */
    }
    KeSetTimer(&g_ShutdownTimer, dueTime, &g_ShutdownDpc);

    DbgPrint("LOGON: Shutdown scheduled in %u second(s)%s\n",
             Timeout, RebootAfterShutdown ? " (reboot)" : "");
    g_ShutdownState.InProgress = FALSE;
    return STATUS_SUCCESS;
}
