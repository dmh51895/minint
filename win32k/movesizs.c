/*
 * MinNT - win32k/movesizs.c
 * Window move and size operations for Win32k.
 *
 * Implements DeferWindowPos, EndDeferWindowPos, MoveWindow,
 * BeginMoveSize, EndMoveSize, UpdateWindow, GetUpdateRect,
 * ValidateRect, ScrollWindowEx.
 */

#include "precomp.h"

typedef struct _MOVESIZE_STATE {
    ULONG_PTR HwndMoveSize;
    BOOL      Moving;
    BOOL      Sizing;
    LONG      OffsetX;
    LONG      OffsetY;
    LONG      OrigX;
    LONG      OrigY;
    LONG      OrigCx;
    LONG      OrigCy;
} MOVESIZE_STATE;

static MOVESIZE_STATE g_MoveSizeState;

/* Deferred window position structure */
typedef struct _DEFER_ENTRY {
    ULONG_PTR hWnd;
    ULONG_PTR hWndInsertAfter;
    LONG x, y, cx, cy;
    ULONG uFlags;
    BOOLEAN InUse;
} DEFER_ENTRY;

typedef struct _DEFER_INFO {
    ULONG       MaxCount;
    ULONG       Count;
    DEFER_ENTRY Entries[1]; /* variable-size array */
} DEFER_INFO, *PDEFER_INFO;

NTSTATUS NTAPI MoveSizeInit(VOID)
{
    RtlZeroMemory(&g_MoveSizeState, sizeof(g_MoveSizeState));
    DbgPrint("MOVESIZE: initialized\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserBeginDeferWindowPos(ULONG NumWindows, PULONG_PTR phDeferInfo)
{
    PDEFER_INFO pInfo;
    ULONG allocSize;

    if (!phDeferInfo) return STATUS_INVALID_PARAMETER;
    if (NumWindows == 0) NumWindows = 1;

    allocSize = sizeof(DEFER_INFO) + (NumWindows - 1) * sizeof(DEFER_ENTRY);
    pInfo = (PDEFER_INFO)ExAllocatePool(NonPagedPool, allocSize);
    if (!pInfo) return STATUS_NO_MEMORY;

    RtlZeroMemory(pInfo, allocSize);
    pInfo->MaxCount = NumWindows;
    pInfo->Count = 0;

    *phDeferInfo = (ULONG_PTR)pInfo;
    DbgPrint("MOVESIZE: BeginDeferWindowPos count=%u\n", NumWindows);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserDeferWindowPos(ULONG_PTR hDeferInfo, ULONG_PTR hWnd,
                                    ULONG_PTR hWndInsertAfter,
                                    LONG x, LONG y, LONG cx, LONG cy, ULONG uFlags)
{
    PDEFER_INFO pInfo;
    DEFER_ENTRY *pEntry;

    W32K_VALIDATE_HWND(hWnd);

    if (!hDeferInfo || hDeferInfo < 0x1000) return STATUS_INVALID_PARAMETER;

    pInfo = (PDEFER_INFO)hDeferInfo;

    if (pInfo->Count >= pInfo->MaxCount) return STATUS_INSUFFICIENT_RESOURCES;

    /* Record the position change in the defer buffer */
    pEntry = &pInfo->Entries[pInfo->Count];
    pEntry->hWnd = hWnd;
    pEntry->hWndInsertAfter = hWndInsertAfter;
    pEntry->x = x;
    pEntry->y = y;
    pEntry->cx = cx;
    pEntry->cy = cy;
    pEntry->uFlags = uFlags;
    pEntry->InUse = TRUE;
    pInfo->Count++;

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserEndDeferWindowPos(ULONG_PTR hDeferInfo)
{
    PDEFER_INFO pInfo;
    ULONG i;

    if (!hDeferInfo || hDeferInfo < 0x1000) return STATUS_INVALID_PARAMETER;

    pInfo = (PDEFER_INFO)hDeferInfo;

    /* Apply all deferred window position changes */
    for (i = 0; i < pInfo->Count; i++) {
        DEFER_ENTRY *pE = &pInfo->Entries[i];
        WINDOW *pWnd;

        if (!pE->InUse || !pE->hWnd || pE->hWnd < 0x1000) continue;

        pWnd = (WINDOW *)pE->hWnd;

        if (!(pE->uFlags & SWP_NOMOVE)) {
            pWnd->x = pE->x;
            pWnd->y = pE->y;
        }
        if (!(pE->uFlags & SWP_NOSIZE)) {
            pWnd->cx = pE->cx;
            pWnd->cy = pE->cy;
        }

        /* Handle z-order via hWndInsertAfter */
        if (!(pE->uFlags & SWP_NOZORDER) && pE->hWndInsertAfter) {
            /* HWND_TOP=0, HWND_BOTTOM=1, HWND_TOPMOST=-1, HWND_NOTOPMOST=-2 */
            if (pE->hWndInsertAfter == (ULONG_PTR)-1) {
                pWnd->dwExStyle |= WS_EX_TOPMOST;
            } else if (pE->hWndInsertAfter == (ULONG_PTR)-2) {
                pWnd->dwExStyle &= ~WS_EX_TOPMOST;
            }
            UserSetWindowZOrder(pE->hWnd, pE->hWndInsertAfter, 0);
        }

        if (pE->uFlags & SWP_SHOWWINDOW) pWnd->visible = TRUE;
        if (pE->uFlags & SWP_HIDEWINDOW) pWnd->visible = FALSE;

        if (!(pE->uFlags & SWP_NOREDRAW)) {
            UserPostMessage(pE->hWnd, WM_PAINT, 0, 0);
        }
    }

    ExFreePool(pInfo);
    DbgPrint("MOVESIZE: EndDeferWindowPos applied %u entries\n", pInfo->Count);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserMoveWindow(ULONG_PTR hWnd, LONG x, LONG y, LONG cx, LONG cy, BOOL bRepaint)
{
    W32K_VALIDATE_HWND(hWnd);
    WINDOW *pWnd = (WINDOW *)hWnd;
    LONG oldX, oldY, oldCx, oldCy;

    oldX = pWnd->x; oldY = pWnd->y;
    oldCx = pWnd->cx; oldCy = pWnd->cy;
    pWnd->x = x; pWnd->y = y;
    pWnd->cx = cx; pWnd->cy = cy;

    if (oldX != x || oldY != y)
        UserPostMessage(hWnd, WM_MOVE, 0, (LONG_PTR)((y << 16) | (x & 0xFFFF)));

    if (oldCx != cx || oldCy != cy)
        UserPostMessage(hWnd, WM_SIZE, 0, (LONG_PTR)((cy << 16) | (cx & 0xFFFF)));

    if (bRepaint) UserInvalidateRect(hWnd, 0, TRUE);

    DbgPrint("MOVESIZE: MoveWindow(%p, %d,%d %dx%d repaint=%d)\n",
             (PVOID)hWnd, x, y, cx, cy, bRepaint);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserBeginMoveSize(ULONG_PTR hWnd, BOOL fMove)
{
    WINDOW *pWnd;
    W32K_POINT pt;

    W32K_VALIDATE_HWND(hWnd);
    pWnd = (WINDOW *)hWnd;

    UserGetCursorPos(&pt);
    g_MoveSizeState.HwndMoveSize = hWnd;
    g_MoveSizeState.Moving = fMove;
    g_MoveSizeState.Sizing = !fMove;
    g_MoveSizeState.OffsetX = pt.x - pWnd->x;
    g_MoveSizeState.OffsetY = pt.y - pWnd->y;
    g_MoveSizeState.OrigX = pWnd->x;
    g_MoveSizeState.OrigY = pWnd->y;
    g_MoveSizeState.OrigCx = pWnd->cx;
    g_MoveSizeState.OrigCy = pWnd->cy;

    DbgPrint("MOVESIZE: BeginMoveSize(%p, move=%d)\n", (PVOID)hWnd, fMove);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserEndMoveSize(VOID)
{
    g_MoveSizeState.HwndMoveSize = 0;
    g_MoveSizeState.Moving = FALSE;
    g_MoveSizeState.Sizing = FALSE;
    DbgPrint("MOVESIZE: EndMoveSize\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserUpdateWindow(ULONG_PTR hWnd)
{
    W32K_VALIDATE_HWND(hWnd);
    WINDOW *pWnd = (WINDOW *)hWnd;

    if (pWnd->dirty) {
        UserPostMessage(hWnd, WM_PAINT, 0, 0);
        pWnd->dirty = FALSE;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetUpdateRect(ULONG_PTR hWnd, PW32K_RECT pRect, PBOOL pErase)
{
    W32K_VALIDATE_HWND(hWnd);
    WINDOW *pWnd = (WINDOW *)hWnd;

    if (pRect) {
        if (pWnd->dirty) {
            pRect->left = pWnd->x;
            pRect->top = pWnd->y;
            pRect->right = pWnd->x + pWnd->cx;
            pRect->bottom = pWnd->y + pWnd->cy;
        } else {
            RtlZeroMemory(pRect, sizeof(W32K_RECT));
        }
    }
    if (pErase) *pErase = pWnd->erasebg;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserValidateRect(ULONG_PTR hWnd, PW32K_RECT pRect)
{
    W32K_VALIDATE_HWND(hWnd);
    WINDOW *pWnd = (WINDOW *)hWnd;

    pWnd->dirty = FALSE;
    pWnd->erasebg = FALSE;

    /* If pRect is NULL, validate the entire window.
     * If pRect is provided, only validate that specific rectangle. */
    if (pRect) {
        UpdateValidateRectPartial(hWnd, pRect);
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserScrollWindowEx(ULONG_PTR hWnd, LONG dx, LONG dy,
                                    PW32K_RECT pRectScroll, PW32K_RECT pRectClip,
                                    ULONG_PTR hrgnUpdate, PW32K_RECT pRectUpdate,
                                    ULONG uFlags)
{
    W32K_VALIDATE_HWND(hWnd);
    WINDOW *pWnd = (WINDOW *)hWnd;

    /* Determine scroll area: use pRectScroll if provided, else full window */
    {
        LONG scrollLeft, scrollTop, scrollRight, scrollBottom;

        if (pRectScroll) {
            scrollLeft = pRectScroll->left;
            scrollTop = pRectScroll->top;
            scrollRight = pRectScroll->right;
            scrollBottom = pRectScroll->bottom;
        } else {
            scrollLeft = pWnd->x;
            scrollTop = pWnd->y;
            scrollRight = pWnd->x + pWnd->cx;
            scrollBottom = pWnd->y + pWnd->cy;
        }

        /* Clip to pRectClip if provided */
        if (pRectClip) {
            if (scrollLeft < pRectClip->left) scrollLeft = pRectClip->left;
            if (scrollTop < pRectClip->top) scrollTop = pRectClip->top;
            if (scrollRight > pRectClip->right) scrollRight = pRectClip->right;
            if (scrollBottom > pRectClip->bottom) scrollBottom = pRectClip->bottom;
        }

        DbgPrint("MOVESIZE: ScrollWindowEx(%p, dx=%d, dy=%d, area=(%d,%d)-(%d,%d) flags=0x%X)\n",
                 (PVOID)hWnd, dx, dy, scrollLeft, scrollTop, scrollRight, scrollBottom, uFlags);
    }

    /* Compute the update region after scrolling */
    if (pRectUpdate) {
        /* The area uncovered by the scroll needs repainting */
        if (dx > 0) {
            pRectUpdate->left = pWnd->x;
            pRectUpdate->right = pWnd->x + dx;
        } else if (dx < 0) {
            pRectUpdate->left = pWnd->x + pWnd->cx + dx;
            pRectUpdate->right = pWnd->x + pWnd->cx;
        } else {
            pRectUpdate->left = pWnd->x;
            pRectUpdate->right = pWnd->x;
        }
        if (dy > 0) {
            pRectUpdate->top = pWnd->y;
            pRectUpdate->bottom = pWnd->y + dy;
        } else if (dy < 0) {
            pRectUpdate->top = pWnd->y + pWnd->cy + dy;
            pRectUpdate->bottom = pWnd->y + pWnd->cy;
        } else {
            pRectUpdate->top = pWnd->y;
            pRectUpdate->bottom = pWnd->y;
        }
    }

    /* Mark the uncovered area for repainting if SW_INVALIDATE is set */
    if (uFlags & SW_INVALIDATE) {
        pWnd->dirty = TRUE;
        if (uFlags & SW_ERASE) pWnd->erasebg = TRUE;
    }

    /* If hrgnUpdate is provided, store the update region bounding box in it */
    if (hrgnUpdate && hrgnUpdate >= 0x1000 && pRectUpdate) {
        W32K_RECT *pRgnRect = (W32K_RECT *)hrgnUpdate;
        pRgnRect->left = pRectUpdate->left;
        pRgnRect->top = pRectUpdate->top;
        pRgnRect->right = pRectUpdate->right;
        pRgnRect->bottom = pRectUpdate->bottom;
    }

    /* SW_SCROLLCHILDREN: scroll child windows */
    if (uFlags & SW_SCROLLCHILDREN) {
        WINDOW *pChild = (pWnd->hwndChild && pWnd->hwndChild >= 0x1000)
                         ? (WINDOW *)pWnd->hwndChild : NULL;
        while (pChild) {
            pChild->x += dx;
            pChild->y += dy;
            pChild = (pChild->hwndNext && pChild->hwndNext >= 0x1000)
                     ? (WINDOW *)pChild->hwndNext : NULL;
        }
    }

    return STATUS_SUCCESS;
}
