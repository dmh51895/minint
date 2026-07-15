/*
 * MinNT - win32k/winable.c
 * Accessibility (winable) and coordinate mapping for Win32k.
 *
 * Implements accessible object management, NotifyWinEvent,
 * IsWindowUnicode, GetWindowThread/ProcessId, coordinate mapping
 * (ClientToScreen, ScreenToClient, MapWindowPoints), window placement
 * (GetWindowPlacement, SetWindowPlacement), GetSystemMetrics,
 * SystemParametersInfo, GetDoubleClickTime.
 */

#include "precomp.h"

#define MAX_ACCESSIBLE_OBJECTS 32

typedef struct _ACCESSIBLE_OBJECT {
    ULONG_PTR Hwnd;
    ULONG     ObjectType;
    LONG      ChildId;
    ULONG_PTR Callback;
    BOOLEAN   InUse;
} ACCESSIBLE_OBJECT, *PACCESSIBLE_OBJECT;

static ACCESSIBLE_OBJECT g_AccessibleObjs[MAX_ACCESSIBLE_OBJECTS];

/* System parameters storage */
static struct {
    BOOL  Beep;
    BOOL  FontSmoothing;
    ULONG Border;
    ULONG KeyboardSpeed;
    ULONG KeyboardDelay;
    ULONG DoubleClickTime;
    BOOL  DragFullWindows;
    BOOL  ShowSounds;
    ULONG ScreenSaveTimeout;
} g_SysParams = {
    TRUE,   /* Beep */
    FALSE,  /* FontSmoothing */
    1,      /* Border */
    31,     /* KeyboardSpeed */
    1,      /* KeyboardDelay */
    500,    /* DoubleClickTime */
    FALSE,  /* DragFullWindows */
    FALSE,  /* ShowSounds */
    300     /* ScreenSaveTimeout */
};

NTSTATUS NTAPI WinableInit(VOID)
{
    RtlZeroMemory(g_AccessibleObjs, sizeof(g_AccessibleObjs));
    DbgPrint("WINABLE: initialized (%d accessible object slots)\n", MAX_ACCESSIBLE_OBJECTS);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetAccessibleObject(ULONG_PTR Hwnd, ULONG ObjectType, LONG ChildId,
                                        PULONG_PTR pCallback)
{
    ULONG i;
    if (!pCallback) return STATUS_INVALID_PARAMETER;

    for (i = 0; i < MAX_ACCESSIBLE_OBJECTS; i++) {
        if (g_AccessibleObjs[i].InUse &&
            g_AccessibleObjs[i].Hwnd == Hwnd &&
            g_AccessibleObjs[i].ObjectType == ObjectType &&
            g_AccessibleObjs[i].ChildId == ChildId) {
            *pCallback = g_AccessibleObjs[i].Callback;
            return STATUS_SUCCESS;
        }
    }
    *pCallback = 0;
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserSetAccessibleObject(ULONG_PTR Hwnd, ULONG ObjectType, LONG ChildId,
                                        ULONG_PTR Callback)
{
    ULONG i;
    for (i = 0; i < MAX_ACCESSIBLE_OBJECTS; i++) {
        if (g_AccessibleObjs[i].InUse &&
            g_AccessibleObjs[i].Hwnd == Hwnd &&
            g_AccessibleObjs[i].ObjectType == ObjectType &&
            g_AccessibleObjs[i].ChildId == ChildId) {
            g_AccessibleObjs[i].Callback = Callback;
            return STATUS_SUCCESS;
        }
    }

    for (i = 0; i < MAX_ACCESSIBLE_OBJECTS; i++) {
        if (!g_AccessibleObjs[i].InUse) {
            g_AccessibleObjs[i].Hwnd = Hwnd;
            g_AccessibleObjs[i].ObjectType = ObjectType;
            g_AccessibleObjs[i].ChildId = ChildId;
            g_AccessibleObjs[i].Callback = Callback;
            g_AccessibleObjs[i].InUse = TRUE;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS NTAPI UserRemoveAccessibleObject(ULONG_PTR Hwnd, ULONG ObjectType, LONG ChildId)
{
    ULONG i;
    for (i = 0; i < MAX_ACCESSIBLE_OBJECTS; i++) {
        if (g_AccessibleObjs[i].InUse &&
            g_AccessibleObjs[i].Hwnd == Hwnd &&
            g_AccessibleObjs[i].ObjectType == ObjectType &&
            g_AccessibleObjs[i].ChildId == ChildId) {
            g_AccessibleObjs[i].InUse = FALSE;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserNotifyWinEvent(ULONG Event, ULONG_PTR Hwnd, LONG ObjectType, LONG ChildId)
{
    DbgPrint("WINABLE: NotifyWinEvent event=%u hwnd=%p obj=%u child=%d\n",
             Event, (PVOID)Hwnd, ObjectType, ChildId);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserIsWindowUnicode(ULONG_PTR Hwnd, PBOOL pIsUnicode)
{
    WINDOW *pWnd;

    if (!pIsUnicode) return STATUS_INVALID_PARAMETER;

    *pIsUnicode = TRUE; /* default to Unicode */

    if (Hwnd != 0 && Hwnd >= 0x1000) {
        pWnd = (WINDOW *)Hwnd;
        *pIsUnicode = pWnd->Unicode;
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetWindowThread(ULONG_PTR Hwnd, PULONG pThreadId)
{
    WINDOW *pWnd;

    if (!pThreadId) return STATUS_INVALID_PARAMETER;

    /* Look up the owning thread from the window structure */
    if (Hwnd != 0 && Hwnd >= 0x1000) {
        pWnd = (WINDOW *)Hwnd;
        *pThreadId = (ULONG)pWnd->ThreadId;
    } else {
        *pThreadId = (ULONG)(ULONG_PTR)PsGetCurrentThreadId();
    }

    return STATUS_SUCCESS;
}

/* UserGetWindowThreadProcessId is implemented in taskman.c */

NTSTATUS NTAPI UserMapWindowPoints(ULONG_PTR HwndFrom, ULONG_PTR HwndTo,
                                     PW32K_POINT Points, ULONG Count)
{
    ULONG i;
    WINDOW *pWndFrom = (WINDOW *)HwndFrom;
    WINDOW *pWndTo = (WINDOW *)HwndTo;

    for (i = 0; i < Count; i++) {
        if ((ULONG_PTR)pWndFrom >= 0x1000) {
            Points[i].x -= pWndFrom->x;
            Points[i].y -= pWndFrom->y;
        }
        if ((ULONG_PTR)pWndTo >= 0x1000) {
            Points[i].x += pWndTo->x;
            Points[i].y += pWndTo->y;
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserClientToScreen(ULONG_PTR Hwnd, PW32K_POINT pPoint)
{
    WINDOW *pWnd;
    if (!pPoint) return STATUS_INVALID_PARAMETER;

    if (Hwnd != 0 && (ULONG_PTR)Hwnd >= 0x1000) {
        pWnd = (WINDOW *)Hwnd;
        pPoint->x += pWnd->x;
        pPoint->y += pWnd->y;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserScreenToClient(ULONG_PTR Hwnd, PW32K_POINT pPoint)
{
    WINDOW *pWnd;
    if (!pPoint) return STATUS_INVALID_PARAMETER;

    if (Hwnd != 0 && (ULONG_PTR)Hwnd >= 0x1000) {
        pWnd = (WINDOW *)Hwnd;
        pPoint->x -= pWnd->x;
        pPoint->y -= pWnd->y;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetWindowPlacement(ULONG_PTR Hwnd, PW32K_WINDOWPLACEMENT pPlacement)
{
    WINDOW *pWnd;

    W32K_VALIDATE_HWND(Hwnd);
    if (!pPlacement) return STATUS_INVALID_PARAMETER;

    pWnd = (WINDOW *)Hwnd;

    pPlacement->length = sizeof(W32K_WINDOWPLACEMENT);
    pPlacement->flags = 0;
    pPlacement->showCmd = pWnd->showCmd;

    if (pWnd->dwStyle & WS_MINIMIZE) {
        pPlacement->showCmd = SW_MINIMIZE;
    } else if (pWnd->dwStyle & WS_MAXIMIZE) {
        pPlacement->showCmd = SW_MAXIMIZE;
    } else if (pWnd->visible) {
        pPlacement->showCmd = SW_SHOW;
    } else {
        pPlacement->showCmd = SW_HIDE;
    }

    /* Minimized position (if minimized, where it appears) */
    if (pPlacement->showCmd == SW_MINIMIZE) {
        pPlacement->ptMinPosition.left = pWnd->x;
        pPlacement->ptMinPosition.top = pWnd->y;
    } else {
        pPlacement->ptMinPosition.left = 0;
        pPlacement->ptMinPosition.top = 0;
    }

    /* Maximized position */
    if (pPlacement->showCmd == SW_MAXIMIZE) {
        pPlacement->ptMaxPosition.left = pWnd->x;
        pPlacement->ptMaxPosition.top = pWnd->y;
    } else {
        pPlacement->ptMaxPosition.left = 0;
        pPlacement->ptMaxPosition.top = 0;
    }

    /* Normal (restored) position */
    pPlacement->rcNormalPosition.left = pWnd->rcNormal.left;
    pPlacement->rcNormalPosition.top = pWnd->rcNormal.top;
    pPlacement->rcNormalPosition.right = pWnd->rcNormal.right;
    pPlacement->rcNormalPosition.bottom = pWnd->rcNormal.bottom;

    /* If rcNormal is zero (not set yet), use current position */
    if (pPlacement->rcNormalPosition.right == 0 && pPlacement->rcNormalPosition.bottom == 0) {
        pPlacement->rcNormalPosition.left = pWnd->x;
        pPlacement->rcNormalPosition.top = pWnd->y;
        pPlacement->rcNormalPosition.right = pWnd->x + pWnd->cx;
        pPlacement->rcNormalPosition.bottom = pWnd->y + pWnd->cy;
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserSetWindowPlacement(ULONG_PTR Hwnd, PW32K_WINDOWPLACEMENT pPlacement)
{
    WINDOW *pWnd;

    W32K_VALIDATE_HWND(Hwnd);
    if (!pPlacement) return STATUS_INVALID_PARAMETER;

    pWnd = (WINDOW *)Hwnd;

    /* Save current position as normal (restored) position */
    pWnd->rcNormal.left = pWnd->x;
    pWnd->rcNormal.top = pWnd->y;
    pWnd->rcNormal.right = pWnd->x + pWnd->cx;
    pWnd->rcNormal.bottom = pWnd->y + pWnd->cy;

    /* Apply the normal position from the placement */
    if (pPlacement->rcNormalPosition.right > pPlacement->rcNormalPosition.left &&
        pPlacement->rcNormalPosition.bottom > pPlacement->rcNormalPosition.top) {
        pWnd->x = pPlacement->rcNormalPosition.left;
        pWnd->y = pPlacement->rcNormalPosition.top;
        pWnd->cx = pPlacement->rcNormalPosition.right - pPlacement->rcNormalPosition.left;
        pWnd->cy = pPlacement->rcNormalPosition.bottom - pPlacement->rcNormalPosition.top;
    }

    /* Apply show command */
    pWnd->showCmd = pPlacement->showCmd;
    switch (pPlacement->showCmd) {
        case SW_HIDE:
            pWnd->visible = FALSE;
            break;
        case SW_SHOW:
            pWnd->visible = TRUE;
            pWnd->dwStyle &= ~(WS_MINIMIZE | WS_MAXIMIZE);
            break;
        case SW_MINIMIZE:
            pWnd->dwStyle |= WS_MINIMIZE;
            pWnd->dwStyle &= ~WS_MAXIMIZE;
            break;
        case SW_MAXIMIZE:
            pWnd->dwStyle |= WS_MAXIMIZE;
            pWnd->dwStyle &= ~WS_MINIMIZE;
            break;
        case SW_RESTORE:
            pWnd->dwStyle &= ~(WS_MINIMIZE | WS_MAXIMIZE);
            pWnd->visible = TRUE;
            break;
        default:
            break;
    }

    UserPostMessage(Hwnd, WM_SIZE, 0, (LONG_PTR)((pWnd->cy << 16) | (pWnd->cx & 0xFFFF)));

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetSystemMetrics(int nIndex, PINT pValue)
{
    if (!pValue) return STATUS_INVALID_PARAMETER;

    switch (nIndex) {
        case 0: *pValue = 0; break;       /* SM_CXSCREEN */
        case 1: *pValue = 0; break;       /* SM_CYSCREEN */
        case 5: *pValue = 8; break;       /* SM_CXVSCROLL */
        case 6: *pValue = 8; break;       /* SM_CYHSCROLL */
        case 7: *pValue = 19; break;      /* SM_CYCAPTION */
        case 8: *pValue = 2; break;       /* SM_CXBORDER */
        case 9: *pValue = 2; break;       /* SM_CYBORDER */
        case 11: *pValue = 36; break;     /* SM_CXFIXEDFRAME */
        case 12: *pValue = 38; break;     /* SM_CYFIXEDFRAME */
        case 13: *pValue = 48; break;     /* SM_CXDLGFRAME */
        case 14: *pValue = 34; break;     /* SM_CYDLGFRAME */
        case 17: *pValue = 16; break;     /* SM_CYVTHUMB */
        case 18: *pValue = 16; break;     /* SM_CXHTHUMB */
        case 19: *pValue = 48; break;     /* SM_CXICON */
        case 20: *pValue = 48; break;     /* SM_CYICON */
        case 31: *pValue = 4; break;      /* SM_MOUSEWHEELPRESENT */
        default: *pValue = 0; break;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetDoubleClickTime(PULONG pTime)
{
    if (!pTime) return STATUS_INVALID_PARAMETER;
    *pTime = g_SysParams.DoubleClickTime;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserSystemParametersInfoW(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni)
{
    switch (uiAction) {
        case SPI_GETBEEP:
            if (pvParam) *(PULONG)pvParam = g_SysParams.Beep;
            break;
        case SPI_SETBEEP:
            g_SysParams.Beep = uiParam ? TRUE : FALSE;
            break;
        case SPI_GETBORDER:
            if (pvParam) *(PULONG)pvParam = g_SysParams.Border;
            break;
        case SPI_SETBORDER:
            g_SysParams.Border = uiParam;
            break;
        case SPI_GETKEYBOARDSPEED:
            if (pvParam) *(PULONG)pvParam = g_SysParams.KeyboardSpeed;
            break;
        case SPI_SETKEYBOARDSPEED:
            g_SysParams.KeyboardSpeed = uiParam;
            break;
        case SPI_GETKEYBOARDDELAY:
            if (pvParam) *(PULONG)pvParam = g_SysParams.KeyboardDelay;
            break;
        case SPI_SETKEYBOARDDELAY:
            g_SysParams.KeyboardDelay = uiParam;
            break;
        case SPI_GETDOUBLECLKTIME:
            if (pvParam) *(PULONG)pvParam = g_SysParams.DoubleClickTime;
            break;
        case SPI_SETDOUBLECLKTIME:
            g_SysParams.DoubleClickTime = uiParam;
            break;
        case SPI_GETDRAGFULLWINDOWS:
            if (pvParam) *(PULONG)pvParam = g_SysParams.DragFullWindows;
            break;
        case SPI_SETDRAGFULLWINDOWS:
            g_SysParams.DragFullWindows = uiParam ? TRUE : FALSE;
            break;
        case SPI_GETSHOWSOUNDS:
            if (pvParam) *(PULONG)pvParam = g_SysParams.ShowSounds;
            break;
        case SPI_SETSHOWSOUNDS:
            g_SysParams.ShowSounds = uiParam ? TRUE : FALSE;
            break;
        case SPI_GETFONTSMOOTHING:
            if (pvParam) *(PULONG)pvParam = g_SysParams.FontSmoothing;
            break;
        case SPI_SETFONTSMOOTHING:
            g_SysParams.FontSmoothing = uiParam ? TRUE : FALSE;
            break;
        case SPI_GETSCREENSAVETIMEOUT:
            if (pvParam) *(PULONG)pvParam = g_SysParams.ScreenSaveTimeout;
            break;
        case SPI_SETSCREENSAVETIMEOUT:
            g_SysParams.ScreenSaveTimeout = uiParam;
            break;
        case SPI_GETWORKAREA:
            if (pvParam) {
                W32K_RECT *pRc = (W32K_RECT *)pvParam;
                pRc->left = 0;
                pRc->top = 0;
                pRc->right = 1920;
                pRc->bottom = 1080;
            }
            break;
        default:
            DbgPrint("WINABLE: SystemParametersInfo unhandled action=0x%X\n", uiAction);
            break;
    }

    /* fWinIni: if set, broadcast WM_SETTINGCHANGE to all top-level windows */
    if (fWinIni & 0x02 /* SPIF_SENDWININICHANGE */) {
        UserPostMessage(0, 0x001A /* WM_SETTINGCHANGE */, uiAction, 0);
    }

    return STATUS_SUCCESS;
}
