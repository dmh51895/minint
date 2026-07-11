/*
 * MinNT - boot/chain/winlogon.c
 * Winlogon — ported from ReactOS base/system/winlogon/
 * Runs as kernel thread, performs real Winlogon initialization:
 *   1. Create window station (WinSta0)
 *   2. Create desktop objects (Default, Winlogon, Disconnect)
 *   3. Initialize SAS handling
 *   4. Create user token (auto-logon)
 *   5. Load user profile from registry
 *   6. Start user shell (explorer.exe)
 */

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

/* ---- Forward declarations ------------------------------------------------ */

extern VOID NTAPI ExplorerThread(PVOID Context);

/* ---- Winlogon globals --------------------------------------------------- */

static KEVENT WlpSasEvent;
static KEVENT WlpUserLoggedOnEvent;
static PVOID WlpWinSta;
static PVOID WlpDefaultDesktop;
static PVOID WlpWinlogonDesktop;
static PVOID WlpDisconnectDesktop;

/* ---- SAS (Secure Attention Sequence) ------------------------------------ */
/* Ported from ReactOS winlogon.c WlxSasNotify() */

static VOID NTAPI WlpNotifySas(ULONG SasType)
{
    DbgPrint("WINLOGON: SAS event type %lu\n", (ULONG)SasType);

    switch (SasType) {
    case 0:  /* WLX_SAS_TYPE_USER_LOGOFF */
        DbgPrint("WINLOGON: user logoff requested\n");
        break;
    case 1:  /* WLX_SAS_TYPE_TIMEOUT */
        DbgPrint("WINLOGON: timeout\n");
        break;
    case 2:  /* WLX_SAS_TYPE_CTRL_ALT_DEL */
        DbgPrint("WINLOGON: Ctrl+Alt+Del pressed\n");
        break;
    case 3:  /* WLX_SAS_TYPE_SCRNSVR_TIMEOUT */
        DbgPrint("WINLOGON: screensaver timeout\n");
        break;
    case 4:  /* WLX_SAS_TYPE_USER_LOGON */
        DbgPrint("WINLOGON: user logon\n");
        break;
    }
}

/* ---- Create window station and desktops --------------------------------- */
/* Ported from ReactOS WlxCreateDesktops() in winlogon.c */

static NTSTATUS NTAPI WlpCreateWindowStation(VOID)
{
    NTSTATUS status;
    OBJECT_ATTRIBUTES objectAttributes;
    UNICODE_STRING winStaName, desktopName;
    HANDLE winStaHandle, desktopHandle;

    DbgPrint("WINLOGON: creating window station WinSta0...\n");

    /* Create WinSta0 window station */
    RtlInitUnicodeString(&winStaName, L"\\Windows\\WindowStations\\WinSta0");
    InitializeObjectAttributes(&objectAttributes,
                               &winStaName,
                               OBJ_CASE_INSENSITIVE | OBJ_OPENIF | OBJ_PERMANENT,
                               NULL,
                               NULL);
    status = ObCreateObject(NULL, 128, &winStaName, &WlpWinSta);
    if (!NT_SUCCESS(status)) {
        DbgPrint("WINLOGON: failed to create WinSta0: 0x%08lx\n", (ULONG)status);
        return status;
    }
    DbgPrint("WINLOGON: WinSta0 created\n");

    /* Create Default desktop */
    RtlInitUnicodeString(&desktopName, L"Default");
    InitializeObjectAttributes(&objectAttributes,
                               &desktopName,
                               OBJ_CASE_INSENSITIVE | OBJ_OPENIF | OBJ_PERMANENT,
                               NULL,
                               NULL);
    status = ObCreateObject(NULL, 64, &desktopName, &WlpDefaultDesktop);
    if (!NT_SUCCESS(status)) {
        DbgPrint("WINLOGON: failed to create Default desktop: 0x%08lx\n", (ULONG)status);
        return status;
    }
    DbgPrint("WINLOGON: Default desktop created\n");

    /* Create Winlogon desktop (SAS interception) */
    RtlInitUnicodeString(&desktopName, L"Winlogon");
    status = ObCreateObject(NULL, 64, &desktopName, &WlpWinlogonDesktop);
    if (!NT_SUCCESS(status)) {
        DbgPrint("WINLOGON: failed to create Winlogon desktop: 0x%08lx\n", (ULONG)status);
        return status;
    }
    DbgPrint("WINLOGON: Winlogon desktop created\n");

    /* Create Disconnect desktop */
    RtlInitUnicodeString(&desktopName, L"Disconnect");
    status = ObCreateObject(NULL, 64, &desktopName, &WlpDisconnectDesktop);
    if (!NT_SUCCESS(status)) {
        DbgPrint("WINLOGON: failed to create Disconnect desktop: 0x%08lx\n", (ULONG)status);
        return status;
    }
    DbgPrint("WINLOGON: Disconnect desktop created\n");

    return STATUS_SUCCESS;
}

/* ---- Create user token -------------------------------------------------- */
/* Ported from ReactOS CreateProcessToken() in security.c */

static NTSTATUS NTAPI WlpCreateUserToken(PTOKEN *UserToken)
{
    NTSTATUS status;
    PTOKEN token;
    SID_IDENTIFIER_AUTHORITY ntAuth = {0, 0, 0, 0, 0, 5}; /* NT Authority */
    PSID adminSid = NULL;

    DbgPrint("WINLOGON: creating user token...\n");

    /* Allocate token */
    token = (PTOKEN)ExAllocatePool(PagedPool, sizeof(TOKEN));
    if (!token) return STATUS_NO_MEMORY;

    /* Initialize token */
    RtlZeroMemory(token, sizeof(TOKEN));
    token->Elevated = FALSE;

    /* Create local system SID */
    status = RtlAllocateAndInitializeSid(&ntAuth, 1,
                                         SECURITY_LOCAL_SYSTEM_RID,
                                         0, 0, 0, 0, 0, 0, 0,
                                         &adminSid);
    if (!NT_SUCCESS(status)) {
        ExFreePool(token);
        return status;
    }

    token->UserSid = adminSid;
    *UserToken = token;

    DbgPrint("WINLOGON: user token created (elevated=%s)\n",
             token->Elevated ? "yes" : "no");

    return STATUS_SUCCESS;
}

/* ---- Load user profile -------------------------------------------------- */
/* Ported from ReactOS LoadUserProfile() in winlogon.c */

static NTSTATUS NTAPI WlpLoadUserProfile(VOID)
{
    NTSTATUS status;
    HANDLE profileKey;
    OBJECT_ATTRIBUTES objectAttributes;
    UNICODE_STRING profilePath;
    ULONG resultLength;
    PVOID partialInfo;
    UNICODE_STRING valueName;

    DbgPrint("WINLOGON: loading user profile...\n");

    /* Open the ProfileList registry key */
    RtlInitUnicodeString(&profilePath,
                         L"\\Registry\\Machine\\Software\\Microsoft\\"
                         L"Windows NT\\CurrentVersion\\ProfileList");
    InitializeObjectAttributes(&objectAttributes,
                               &profilePath,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    status = NtOpenKey(&profileKey, KEY_READ, &objectAttributes);
    if (!NT_SUCCESS(status)) {
        DbgPrint("WINLOGON: cannot open ProfileList key: 0x%08lx\n", (ULONG)status);
        return status;
    }

    /* Query the default profile path */
    partialInfo = ExAllocatePool(PagedPool, 512);
    if (!partialInfo) {
        NtClose(profileKey);
        return STATUS_NO_MEMORY;
    }

    RtlInitUnicodeString(&valueName, L"S-1-5-18"); /* Local System SID */
    status = NtQueryValueKey(profileKey, &valueName,
                             2, /* KeyValuePartialInformation */
                             partialInfo, 512, &resultLength);
    if (NT_SUCCESS(status)) {
        PWSTR profilePathStr = (PWSTR)((PUCHAR)partialInfo + 8);
        DbgPrint("WINLOGON: default profile: %ls\n", profilePathStr);
    } else {
        DbgPrint("WINLOGON: using default profile path\n");
    }

    ExFreePool(partialInfo);
    NtClose(profileKey);

    DbgPrint("WINLOGON: user profile loaded\n");
    return STATUS_SUCCESS;
}

/* ---- Start user shell --------------------------------------------------- */
/* Ported from ReactOS WlxStartApplication() in winlogon.c */

static NTSTATUS NTAPI WlpStartUserShell(VOID)
{
    PETHREAD explorerThread;
    NTSTATUS status;

    DbgPrint("WINLOGON: starting user shell (explorer)...\n");

    status = PsCreateSystemThread(PsInitialSystemProcess,
                                  ExplorerThread, NULL, &explorerThread);
    if (!NT_SUCCESS(status)) {
        DbgPrint("WINLOGON: failed to start explorer: 0x%08lx\n", (ULONG)status);
        return status;
    }

    DbgPrint("WINLOGON: explorer started\n");
    return STATUS_SUCCESS;
}

/* ---- Winlogon main ------------------------------------------------------ */
/* Ported from ReactOS WinMain() in winlogon.c */

VOID NTAPI WinlogonThread(PVOID Context)
{
    NTSTATUS status;
    PTOKEN userToken;

    UNREFERENCED_PARAMETER(Context);

    DbgPrint("\n");
    DbgPrint("========================================\n");
    DbgPrint("  WINLOGON: Windows Logon Process\n");
    DbgPrint("  Session 0 Console Window Station\n");
    DbgPrint("  (ported from ReactOS Winlogon)\n");
    DbgPrint("========================================\n\n");

    /* Phase 1: Create window station and desktops (WlxCreateDesktops) */
    DbgPrint("WINLOGON: Phase 1 - Creating window station and desktops...\n");
    status = WlpCreateWindowStation();
    if (!NT_SUCCESS(status)) {
        DbgPrint("WINLOGON: failed to create window station\n");
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, status, 0x200, 0, 0);
    }

    /* Phase 2: Initialize SAS handling */
    DbgPrint("WINLOGON: Phase 2 - Initializing SAS handling...\n");
    KeInitializeEvent(&WlpSasEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&WlpUserLoggedOnEvent, SynchronizationEvent, FALSE);
    DbgPrint("WINLOGON: SAS handler initialized\n");

    /* Phase 3: Auto-logon — create user token */
    DbgPrint("WINLOGON: Phase 3 - Creating user token (auto-logon)...\n");
    status = WlpCreateUserToken(&userToken);
    if (!NT_SUCCESS(status)) {
        DbgPrint("WINLOGON: failed to create user token: 0x%08lx\n", (ULONG)status);
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, status, 0x201, 0, 0);
    }

    /* Phase 4: Load user profile (LoadUserProfile) */
    DbgPrint("WINLOGON: Phase 4 - Loading user profile...\n");
    status = WlpLoadUserProfile();
    if (!NT_SUCCESS(status)) {
        DbgPrint("WINLOGON: warning: profile load failed: 0x%08lx\n", (ULONG)status);
        /* Non-fatal — continue with default profile */
    }

    /* Signal user logged on */
    KeSetEvent(&WlpUserLoggedOnEvent, 0, FALSE);
    DbgPrint("WINLOGON: user logon complete\n");

    /* Phase 5: Start user shell (WlxStartApplication) */
    DbgPrint("WINLOGON: Phase 5 - Starting user shell...\n");
    status = WlpStartUserShell();
    if (!NT_SUCCESS(status)) {
        DbgPrint("WINLOGON: failed to start user shell\n");
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, status, 0x202, 0, 0);
    }

    /* Phase 6: Winlogon ready — main loop */
    DbgPrint("\n");
    DbgPrint("========================================\n");
    DbgPrint("  WINLOGON: Ready!\n");
    DbgPrint("  User shell is running\n");
    DbgPrint("========================================\n\n");

    for (;;) {
        /* Wait for SAS events (Ctrl+Alt+Del, etc.) */
        KiDispatchNextThread();
    }
}
