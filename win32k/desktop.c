/*
 * MinNT - win32k/desktop.c
 * Desktop and Window Station management for Win32k.
 *
 * Implements CreateDesktop, OpenDesktop, CloseDesktop, SwitchDesktop,
 * EnumDesktops, GetThreadDesktop, SetThreadDesktop. Manages the hierarchy:
 * WindowStation -> Desktop -> Windows. One active desktop per station.
 */

#include "precomp.h"

#define MAX_DESKTOPS     16
#define MAX_WINSTATIONS  4
#define MAX_DESKTOP_NAME 64

typedef struct _W32K_DESKTOP {
    WCHAR       Name[MAX_DESKTOP_NAME];
    ULONG       Flags;
    ULONG_PTR   HwndDesktop;
    BOOLEAN     InUse;
    BOOLEAN     Active;
    ULONG       WindowCount;
} W32K_DESKTOP, *PW32K_DESKTOP;

typedef struct _W32K_WINSTATION {
    WCHAR       Name[MAX_DESKTOP_NAME];
    ULONG       Flags;
    W32K_DESKTOP Desks[MAX_DESKTOPS];
    ULONG       DesktopCount;
    ULONG       ActiveDesktop;
    BOOLEAN     InUse;
} W32K_WINSTATION, *PW32K_WINSTATION;

/* Per-thread desktop mapping table. Maps a thread ID to its associated
 * desktop handle. Updated by UserSetThreadDesktop and queried by
 * UserGetThreadDesktop so that each thread tracks its own desktop. */
typedef struct _THREAD_DESKTOP_MAP {
    ULONG_PTR ThreadId;
    ULONG     DesktopHandle;
    ULONG     WinStationIndex;
    BOOLEAN   InUse;
} THREAD_DESKTOP_MAP, *PTHREAD_DESKTOP_MAP;

#define MAX_THREAD_DESKTOP_MAPS 64

static W32K_WINSTATION g_WinStations[MAX_WINSTATIONS];
static ULONG g_ActiveWinStation = 0;
static THREAD_DESKTOP_MAP g_ThreadDesktopMap[MAX_THREAD_DESKTOP_MAPS];

/* Desktop flags (compatible with Windows DF_*) */
#define DF_ALLOWOTHERACCOUNTHOOK  0x0001

/* Internal: get thread's desktop index */
static ULONG GetThreadDesktopIndex(VOID)
{
    PETHREAD thread = (PETHREAD)KeGetCurrentThread();
    ULONG_PTR tid = (ULONG_PTR)thread->UniqueThreadId;
    ULONG i;

    /* Look up the per-thread desktop mapping table first so that
     * UserSetThreadDesktop associations take precedence. */
    for (i = 0; i < MAX_THREAD_DESKTOP_MAPS; i++) {
        if (g_ThreadDesktopMap[i].InUse &&
            g_ThreadDesktopMap[i].ThreadId == tid &&
            g_ThreadDesktopMap[i].WinStationIndex == g_ActiveWinStation) {
            ULONG idx = g_ThreadDesktopMap[i].DesktopHandle ?
                        (g_ThreadDesktopMap[i].DesktopHandle - 1) : 0;
            return idx;
        }
    }

    /* Fall back to a stable hash of the thread ID for un-associated threads */
    return (ULONG)(tid % MAX_DESKTOPS);
}

/* Internal: store/update a thread's desktop association */
static NTSTATUS SetThreadDesktopIndex(ULONG_PTR ThreadId, ULONG WinStationIndex,
                                       ULONG DesktopHandle)
{
    ULONG i, freeSlot = MAX_THREAD_DESKTOP_MAPS;

    /* Update existing entry for this thread+station if present */
    for (i = 0; i < MAX_THREAD_DESKTOP_MAPS; i++) {
        if (g_ThreadDesktopMap[i].InUse &&
            g_ThreadDesktopMap[i].ThreadId == ThreadId &&
            g_ThreadDesktopMap[i].WinStationIndex == WinStationIndex) {
            g_ThreadDesktopMap[i].DesktopHandle = DesktopHandle;
            return STATUS_SUCCESS;
        }
        if (!g_ThreadDesktopMap[i].InUse && freeSlot == MAX_THREAD_DESKTOP_MAPS) {
            freeSlot = i;
        }
    }

    if (freeSlot == MAX_THREAD_DESKTOP_MAPS) return STATUS_INSUFFICIENT_RESOURCES;

    g_ThreadDesktopMap[freeSlot].InUse = TRUE;
    g_ThreadDesktopMap[freeSlot].ThreadId = ThreadId;
    g_ThreadDesktopMap[freeSlot].WinStationIndex = WinStationIndex;
    g_ThreadDesktopMap[freeSlot].DesktopHandle = DesktopHandle;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI DesktopInit(VOID)
{
    RtlZeroMemory(g_WinStations, sizeof(g_WinStations));
    RtlZeroMemory(g_ThreadDesktopMap, sizeof(g_ThreadDesktopMap));

    /* Create default window station */
    g_WinStations[0].InUse = TRUE;
    g_WinStations[0].DesktopCount = 1;
    g_WinStations[0].ActiveDesktop = 0;
    RtlCopyMemory(g_WinStations[0].Name, L"WinSta0", 8 * sizeof(WCHAR));

    /* Create default desktop */
    g_WinStations[0].Desks[0].InUse = TRUE;
    g_WinStations[0].Desks[0].Active = TRUE;
    RtlCopyMemory(g_WinStations[0].Desks[0].Name, L"Default", 8 * sizeof(WCHAR));

    DbgPrint("DESKTOP: initialized (station 0, desktop 'Default')\n");
    return STATUS_SUCCESS;
}

/* CreateDesktopW: create a new desktop in the current window station */
NTSTATUS NTAPI UserCreateDesktopW(PCWSTR DesktopName, ULONG Flags)
{
    PW32K_WINSTATION ws = &g_WinStations[g_ActiveWinStation];
    ULONG i, nameLen;

    if (!DesktopName) return STATUS_INVALID_PARAMETER;
    if (!ws->InUse) return STATUS_DEVICE_DOES_NOT_EXIST;

    nameLen = 0;
    while (nameLen < MAX_DESKTOP_NAME - 1 && DesktopName[nameLen]) nameLen++;

    /* Find empty desktop slot */
    for (i = 0; i < MAX_DESKTOPS; i++) {
        if (!ws->Desks[i].InUse) {
            RtlCopyMemory(ws->Desks[i].Name, DesktopName, nameLen * sizeof(WCHAR));
            ws->Desks[i].Name[nameLen] = 0;
            ws->Desks[i].Flags = Flags;
            ws->Desks[i].InUse = TRUE;
            ws->Desks[i].Active = FALSE;
            ws->Desks[i].HwndDesktop = 0;
            ws->Desks[i].WindowCount = 0;
            ws->DesktopCount++;

            DbgPrint("DESKTOP: CreateDesktop '%ws' slot %u\n", DesktopName, i);
            return STATUS_SUCCESS;
        }
    }

    return STATUS_INSUFFICIENT_RESOURCES;
}

/* OpenDesktopW: open an existing desktop by name */
NTSTATUS NTAPI UserOpenDesktopW(PCWSTR DesktopName, ULONG Flags, PULONG pHandle)
{
    PW32K_WINSTATION ws = &g_WinStations[g_ActiveWinStation];
    ULONG i, nameLen;

    if (!DesktopName || !pHandle) return STATUS_INVALID_PARAMETER;

    nameLen = 0;
    while (nameLen < MAX_DESKTOP_NAME - 1 && DesktopName[nameLen]) nameLen++;

    for (i = 0; i < MAX_DESKTOPS; i++) {
        if (ws->Desks[i].InUse) {
            BOOLEAN match = TRUE;
            ULONG j;
            for (j = 0; j < nameLen; j++) {
                if (ws->Desks[i].Name[j] != DesktopName[j]) { match = FALSE; break; }
            }
            if (match && ws->Desks[i].Name[nameLen] == 0) {
                /* Verify that the caller-requested flags are compatible with
                 * the desktop's stored flags. DF_ALLOWOTHERACCOUNTHOOK is a
                 * restrictive flag: callers who do not request it cannot
                 * open a desktop that was created with it. Other flag bits
                 * (e.g. generic access) are passed through and merged into
                 * the desktop's Flags so subsequent queries can see them. */
                if ((ws->Desks[i].Flags & DF_ALLOWOTHERACCOUNTHOOK) &&
                    !(Flags & DF_ALLOWOTHERACCOUNTHOOK)) {
                    DbgPrint("DESKTOP: OpenDesktop '%ws' denied (DF_ALLOWOTHERACCOUNTHOOK)\n",
                             DesktopName);
                    return STATUS_ACCESS_DENIED;
                }

                /* Merge caller-supplied flags with the desktop's flags. We
                 * preserve the restrictive bit but allow the caller to add
                 * new access bits for bookkeeping. */
                ws->Desks[i].Flags |= (Flags & ~DF_ALLOWOTHERACCOUNTHOOK);

                *pHandle = i + 1; /* 1-based handle */
                DbgPrint("DESKTOP: OpenDesktop '%ws' flags=0x%X -> handle %u\n",
                         DesktopName, Flags, *pHandle);
                return STATUS_SUCCESS;
            }
        }
    }

    return STATUS_NOT_FOUND;
}

/* CloseDesktop: close a desktop handle */
NTSTATUS NTAPI UserCloseDesktop(ULONG Handle)
{
    PW32K_WINSTATION ws = &g_WinStations[g_ActiveWinStation];
    ULONG idx = Handle - 1;

    if (idx >= MAX_DESKTOPS) return STATUS_INVALID_PARAMETER;
    if (!ws->Desks[idx].InUse) return STATUS_INVALID_HANDLE;

    DbgPrint("DESKTOP: CloseDesktop '%ws'\n", ws->Desks[idx].Name);

    /* Don't close the active desktop */
    if (ws->Desks[idx].Active) return STATUS_DEVICE_BUSY;

    ws->Desks[idx].InUse = FALSE;
    ws->Desks[idx].WindowCount = 0;
    ws->DesktopCount--;

    return STATUS_SUCCESS;
}

/* SwitchDesktop: switch to a different desktop */
NTSTATUS NTAPI UserSwitchDesktop(ULONG Handle)
{
    PW32K_WINSTATION ws = &g_WinStations[g_ActiveWinStation];
    ULONG idx = Handle - 1;

    if (idx >= MAX_DESKTOPS) return STATUS_INVALID_PARAMETER;
    if (!ws->Desks[idx].InUse) return STATUS_INVALID_HANDLE;

    /* Deactivate current */
    if (ws->ActiveDesktop < MAX_DESKTOPS) {
        ws->Desks[ws->ActiveDesktop].Active = FALSE;
    }

    /* Activate new */
    ws->Desks[idx].Active = TRUE;
    ws->ActiveDesktop = idx;

    DbgPrint("DESKTOP: SwitchDesktop -> '%ws'\n", ws->Desks[idx].Name);
    return STATUS_SUCCESS;
}

/* GetThreadDesktop: get the desktop handle for the current thread */
NTSTATUS NTAPI UserGetThreadDesktop(PULONG pHandle)
{
    ULONG deskIdx = GetThreadDesktopIndex();

    if (!pHandle) return STATUS_INVALID_PARAMETER;

    *pHandle = deskIdx + 1;

    return STATUS_SUCCESS;
}

/* SetThreadDesktop: set the desktop for the current thread */
NTSTATUS NTAPI UserSetThreadDesktop(ULONG Handle)
{
    PW32K_WINSTATION ws = &g_WinStations[g_ActiveWinStation];
    PETHREAD thread = (PETHREAD)KeGetCurrentThread();
    ULONG_PTR tid;
    ULONG idx;

    /* Validate the Handle: it must be a 1-based index into the current
     * window station's desktop table, and the target desktop must be
     * marked InUse. */
    if (Handle == 0) return STATUS_INVALID_PARAMETER;

    idx = Handle - 1;
    if (idx >= MAX_DESKTOPS) return STATUS_INVALID_HANDLE;
    if (!ws->InUse) return STATUS_DEVICE_DOES_NOT_EXIST;
    if (!ws->Desks[idx].InUse) return STATUS_INVALID_HANDLE;

    tid = (ULONG_PTR)thread->UniqueThreadId;

    /* Record the thread->desktop association. GetThreadDesktopIndex()
     * will read this mapping on subsequent queries. */
    SetThreadDesktopIndex(tid, g_ActiveWinStation, Handle);

    DbgPrint("DESKTOP: SetThreadDesktop(%u) -> '%ws' for tid=%p\n",
             Handle, ws->Desks[idx].Name, (PVOID)tid);
    return STATUS_SUCCESS;
}

/* EnumDesktopsW: enumerate desktops in current window station */
NTSTATUS NTAPI UserEnumDesktopsW(ULONG Index, PWCHAR Name, ULONG NameLen)
{
    PW32K_WINSTATION ws = &g_WinStations[g_ActiveWinStation];
    ULONG count = 0, i, nameLen;

    if (!Name || NameLen == 0) return STATUS_INVALID_PARAMETER;

    for (i = 0; i < MAX_DESKTOPS; i++) {
        if (ws->Desks[i].InUse) {
            if (count == Index) {
                nameLen = 0;
                while (nameLen < MAX_DESKTOP_NAME - 1 && ws->Desks[i].Name[nameLen]) nameLen++;
                if (nameLen >= NameLen) return STATUS_BUFFER_TOO_SMALL;
                RtlCopyMemory(Name, ws->Desks[i].Name, (nameLen + 1) * sizeof(WCHAR));
                return STATUS_SUCCESS;
            }
            count++;
        }
    }

    return STATUS_NO_MORE_ENTRIES;
}

/* GetInputDesktop: get the currently active desktop */
NTSTATUS NTAPI UserGetInputDesktop(PULONG pHandle)
{
    PW32K_WINSTATION ws = &g_WinStations[g_ActiveWinStation];

    if (!pHandle) return STATUS_INVALID_PARAMETER;

    *pHandle = ws->ActiveDesktop + 1;
    return STATUS_SUCCESS;
}
