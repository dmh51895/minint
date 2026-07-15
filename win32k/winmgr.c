/*
 * MinNT - win32k/winmgr.c
 * High-level window manager operations for Win32k.
 */

#include "precomp.h"

#define MAX_TOPLEVEL_WINDOWS 128

typedef struct _TOPLEVEL_ENTRY {
    ULONG_PTR Hwnd;
    LONG      zOrder;
    BOOLEAN   InUse;
} TOPLEVEL_ENTRY;

static TOPLEVEL_ENTRY g_TopWindows[MAX_TOPLEVEL_WINDOWS];
static LONG g_NextZOrder = 0;

NTSTATUS NTAPI WinMgrInit(VOID)
{
    RtlZeroMemory(g_TopWindows, sizeof(g_TopWindows));
    g_NextZOrder = 0;
    DbgPrint("WINMGR: initialized (%d top-level slots)\n", MAX_TOPLEVEL_WINDOWS);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserAddTopLevelWindow(ULONG_PTR Hwnd)
{
    ULONG i;
    for (i = 0; i < MAX_TOPLEVEL_WINDOWS; i++) {
        if (!g_TopWindows[i].InUse) {
            g_TopWindows[i].Hwnd = Hwnd;
            g_TopWindows[i].zOrder = g_NextZOrder++;
            g_TopWindows[i].InUse = TRUE;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS NTAPI UserRemoveTopLevelWindow(ULONG_PTR Hwnd)
{
    ULONG i;
    for (i = 0; i < MAX_TOPLEVEL_WINDOWS; i++) {
        if (g_TopWindows[i].InUse && g_TopWindows[i].Hwnd == Hwnd) {
            g_TopWindows[i].InUse = FALSE;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserBringWindowToTop(ULONG_PTR Hwnd)
{
    ULONG i;
    LONG maxZ = -1;

    for (i = 0; i < MAX_TOPLEVEL_WINDOWS; i++) {
        if (g_TopWindows[i].InUse && g_TopWindows[i].zOrder > maxZ)
            maxZ = g_TopWindows[i].zOrder;
    }

    for (i = 0; i < MAX_TOPLEVEL_WINDOWS; i++) {
        if (g_TopWindows[i].InUse && g_TopWindows[i].Hwnd == Hwnd) {
            g_TopWindows[i].zOrder = maxZ + 1;
            DbgPrint("WINMGR: BringWindowToTop(%p) zOrder=%d\n", (PVOID)Hwnd, maxZ + 1);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserSetWindowZOrder(ULONG_PTR Hwnd, ULONG_PTR HwndInsertAfter, int zOrder)
{
    ULONG i, targetIdx = MAX_TOPLEVEL_WINDOWS;
    LONG minZ = 0x7FFFFFFF, maxZ = -1, newZ, insertZ;
    WINDOW *pWnd;

    for (i = 0; i < MAX_TOPLEVEL_WINDOWS; i++) {
        if (g_TopWindows[i].InUse) {
            if (g_TopWindows[i].Hwnd == Hwnd)
                targetIdx = i;
            if (g_TopWindows[i].zOrder < minZ)
                minZ = g_TopWindows[i].zOrder;
            if (g_TopWindows[i].zOrder > maxZ)
                maxZ = g_TopWindows[i].zOrder;
        }
    }

    if (targetIdx == MAX_TOPLEVEL_WINDOWS)
        return STATUS_NOT_FOUND;

    if (zOrder != 0) {
        g_TopWindows[targetIdx].zOrder = zOrder;
        return STATUS_SUCCESS;
    }

    if (HwndInsertAfter == (ULONG_PTR)0) {
        g_TopWindows[targetIdx].zOrder = maxZ + 1;
    } else if (HwndInsertAfter == (ULONG_PTR)1) {
        g_TopWindows[targetIdx].zOrder = minZ - 1;
    } else if (HwndInsertAfter == (ULONG_PTR)-1) {
        if (Hwnd >= 0x1000) {
            pWnd = (WINDOW *)Hwnd;
            pWnd->dwExStyle |= WS_EX_TOPMOST;
        }
        g_TopWindows[targetIdx].zOrder = maxZ + 1;
    } else if (HwndInsertAfter == (ULONG_PTR)-2) {
        if (Hwnd >= 0x1000) {
            pWnd = (WINDOW *)Hwnd;
            pWnd->dwExStyle &= ~WS_EX_TOPMOST;
        }
    } else {
        insertZ = maxZ;
        for (i = 0; i < MAX_TOPLEVEL_WINDOWS; i++) {
            if (g_TopWindows[i].InUse &&
                g_TopWindows[i].Hwnd == HwndInsertAfter) {
                insertZ = g_TopWindows[i].zOrder;
                break;
            }
        }
        newZ = insertZ + 1;
        for (i = 0; i < MAX_TOPLEVEL_WINDOWS; i++) {
            if (i != targetIdx && g_TopWindows[i].InUse &&
                g_TopWindows[i].zOrder >= newZ) {
                g_TopWindows[i].zOrder++;
            }
        }
        g_TopWindows[targetIdx].zOrder = newZ;
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetWindowZOrder(ULONG_PTR Hwnd, PINT pZOrder)
{
    ULONG i;
    if (!pZOrder) return STATUS_INVALID_PARAMETER;

    for (i = 0; i < MAX_TOPLEVEL_WINDOWS; i++) {
        if (g_TopWindows[i].InUse && g_TopWindows[i].Hwnd == Hwnd) {
            *pZOrder = g_TopWindows[i].zOrder;
            return STATUS_SUCCESS;
        }
    }
    *pZOrder = 0;
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserWindowFromPoint(LONG x, LONG y, PULONG_PTR pHwnd)
{
    ULONG i;
    LONG bestZ = -1;
    if (!pHwnd) return STATUS_INVALID_PARAMETER;

    *pHwnd = 0;
    for (i = 0; i < MAX_TOPLEVEL_WINDOWS; i++) {
        if (g_TopWindows[i].InUse && g_TopWindows[i].zOrder > bestZ) {
            WINDOW *pWnd = (WINDOW *)g_TopWindows[i].Hwnd;
            if (pWnd && (ULONG_PTR)pWnd >= 0x1000) {
                if (x >= pWnd->x && x < pWnd->x + pWnd->cx &&
                    y >= pWnd->y && y < pWnd->y + pWnd->cy) {
                    *pHwnd = g_TopWindows[i].Hwnd;
                    bestZ = g_TopWindows[i].zOrder;
                }
            }
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserChildWindowFromPoint(ULONG_PTR HwndParent, LONG x, LONG y, PULONG_PTR pHwnd)
{
    WINDOW *pParent;
    ULONG_PTR hChild;

    if (!pHwnd) return STATUS_INVALID_PARAMETER;

    *pHwnd = HwndParent;

    if (!HwndParent || HwndParent < 0x1000)
        return STATUS_SUCCESS;

    pParent = (WINDOW *)HwndParent;

    hChild = pParent->hwndChild;
    while (hChild && hChild >= 0x1000) {
        WINDOW *pChild = (WINDOW *)hChild;

        if (x >= pChild->x && x < pChild->x + pChild->cx &&
            y >= pChild->y && y < pChild->y + pChild->cy) {
            *pHwnd = hChild;
            return STATUS_SUCCESS;
        }

        hChild = pChild->hwndNext;
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserIsWindow(ULONG_PTR Hwnd, PBOOL pIsWindow)
{
    ULONG i;
    if (!pIsWindow) return STATUS_INVALID_PARAMETER;

    *pIsWindow = FALSE;
    for (i = 0; i < MAX_TOPLEVEL_WINDOWS; i++) {
        if (g_TopWindows[i].InUse && g_TopWindows[i].Hwnd == Hwnd) {
            *pIsWindow = TRUE;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserIsChild(ULONG_PTR HwndParent, ULONG_PTR Hwnd, PBOOL pIsChild)
{
    WINDOW *pWnd;
    if (!pIsChild) return STATUS_INVALID_PARAMETER;

    *pIsChild = FALSE;
    if (Hwnd != 0 && (ULONG_PTR)Hwnd >= 0x1000) {
        pWnd = (WINDOW *)Hwnd;
        if (pWnd->hwndParent == HwndParent) {
            *pIsChild = TRUE;
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetParent(ULONG_PTR Hwnd, PULONG_PTR pParent)
{
    WINDOW *pWnd;
    if (!pParent) return STATUS_INVALID_PARAMETER;

    *pParent = 0;
    if (Hwnd != 0 && (ULONG_PTR)Hwnd >= 0x1000) {
        pWnd = (WINDOW *)Hwnd;
        *pParent = pWnd->hwndParent;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserSetParent(ULONG_PTR HwndChild, ULONG_PTR HwndNewParent, PULONG_PTR pHwndOldParent)
{
    WINDOW *pWnd;
    if (!pHwndOldParent) return STATUS_INVALID_PARAMETER;

    *pHwndOldParent = 0;
    if (HwndChild != 0 && (ULONG_PTR)HwndChild >= 0x1000) {
        pWnd = (WINDOW *)HwndChild;
        *pHwndOldParent = pWnd->hwndParent;
        pWnd->hwndParent = HwndNewParent;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserEnableWindow(ULONG_PTR Hwnd, BOOL bEnable, PBOOL pWasEnabled)
{
    WINDOW *pWnd;
    if (!pWasEnabled) return STATUS_INVALID_PARAMETER;

    *pWasEnabled = FALSE;
    if (Hwnd != 0 && (ULONG_PTR)Hwnd >= 0x1000) {
        pWnd = (WINDOW *)Hwnd;
        *pWasEnabled = !(pWnd->dwStyle & WS_DISABLED);
        if (bEnable) {
            pWnd->dwStyle &= ~WS_DISABLED;
        } else {
            pWnd->dwStyle |= WS_DISABLED;
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserIsWindowEnabled(ULONG_PTR Hwnd, PBOOL pEnabled)
{
    WINDOW *pWnd;
    if (!pEnabled) return STATUS_INVALID_PARAMETER;

    *pEnabled = TRUE;
    if (Hwnd != 0 && (ULONG_PTR)Hwnd >= 0x1000) {
        pWnd = (WINDOW *)Hwnd;
        *pEnabled = !(pWnd->dwStyle & WS_DISABLED);
    }
    return STATUS_SUCCESS;
}
