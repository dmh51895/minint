/*
 * MinNT - win32k/update.c
 * Window update and invalidation management for Win32k.
 *
 * Implements InvalidateRgn, InvalidateRect, ValidateRgn, ValidateRect,
 * BeginPaint, EndPaint, GetUpdateRgn, GetUpdateRect, RedrawWindow,
 * UpdateLayeredWindow, LockWindowUpdate, GetRandomRgn, ExcludeUpdateRgn.
 * Manages per-window dirty regions and paint cycles.
 */

#include "precomp.h"

#define MAX_UPDATE_REGIONS 64

typedef struct _UPDATE_REGION {
    ULONG_PTR Hwnd;
    W32K_RECT Rect;
    BOOL      NeedsErase;
    BOOLEAN   InUse;
} UPDATE_REGION, *PUPDATE_REGION;

KSPIN_LOCK g_UpdateLock;
static UPDATE_REGION g_UpdateRegions[MAX_UPDATE_REGIONS];
static ULONG_PTR g_LockedWindow = 0;

NTSTATUS NTAPI UpdateInit(VOID)
{
    RtlZeroMemory(g_UpdateRegions, sizeof(g_UpdateRegions));
    KeInitializeSpinLock(&g_UpdateLock);
    g_LockedWindow = 0;
    DbgPrint("UPDATE: initialized (%d region slots)\n", MAX_UPDATE_REGIONS);
    return STATUS_SUCCESS;
}

/* Helper: add an update region entry for a window */
static VOID AddUpdateRegion(ULONG_PTR Hwnd, PW32K_RECT pRect, BOOL bErase)
{
    ULONG i;
    KIRQL Irql;

    W32K_LOCK_SPIN(g_UpdateLock, Irql);
    for (i = 0; i < MAX_UPDATE_REGIONS; i++) {
        if (!g_UpdateRegions[i].InUse) {
            g_UpdateRegions[i].Hwnd = Hwnd;
            g_UpdateRegions[i].NeedsErase = bErase;
            if (pRect) {
                g_UpdateRegions[i].Rect = *pRect;
            } else {
                WINDOW *pWnd = (WINDOW *)Hwnd;
                g_UpdateRegions[i].Rect.left = pWnd->x;
                g_UpdateRegions[i].Rect.top = pWnd->y;
                g_UpdateRegions[i].Rect.right = pWnd->x + pWnd->cx;
                g_UpdateRegions[i].Rect.bottom = pWnd->y + pWnd->cy;
            }
            g_UpdateRegions[i].InUse = TRUE;
            break;
        }
    }
    W32K_UNLOCK_SPIN(g_UpdateLock, Irql);
}

/* Public: partially validate a window by removing overlapping update regions */
VOID NTAPI UpdateValidateRectPartial(ULONG_PTR Hwnd, PW32K_RECT pRect)
{
    ULONG i;
    KIRQL Irql;

    if (!pRect) return;

    W32K_LOCK_SPIN(g_UpdateLock, Irql);
    for (i = 0; i < MAX_UPDATE_REGIONS; i++) {
        if (g_UpdateRegions[i].InUse && g_UpdateRegions[i].Hwnd == Hwnd) {
            if (!(pRect->right <= g_UpdateRegions[i].Rect.left ||
                  pRect->left >= g_UpdateRegions[i].Rect.right ||
                  pRect->bottom <= g_UpdateRegions[i].Rect.top ||
                  pRect->top >= g_UpdateRegions[i].Rect.bottom)) {
                g_UpdateRegions[i].InUse = FALSE;
            }
        }
    }
    W32K_UNLOCK_SPIN(g_UpdateLock, Irql);
}

/* Helper: remove all update regions for a window */
static VOID ClearUpdateRegions(ULONG_PTR Hwnd)
{
    ULONG i;
    KIRQL Irql;

    W32K_LOCK_SPIN(g_UpdateLock, Irql);
    for (i = 0; i < MAX_UPDATE_REGIONS; i++) {
        if (g_UpdateRegions[i].InUse && g_UpdateRegions[i].Hwnd == Hwnd) {
            g_UpdateRegions[i].InUse = FALSE;
        }
    }
    W32K_UNLOCK_SPIN(g_UpdateLock, Irql);
}

/* Helper: check if window has pending update regions */
static BOOL HasUpdateRegions(ULONG_PTR Hwnd)
{
    ULONG i;
    BOOL result = FALSE;
    KIRQL Irql;

    W32K_LOCK_SPIN(g_UpdateLock, Irql);
    for (i = 0; i < MAX_UPDATE_REGIONS; i++) {
        if (g_UpdateRegions[i].InUse && g_UpdateRegions[i].Hwnd == Hwnd) {
            result = TRUE;
            break;
        }
    }
    W32K_UNLOCK_SPIN(g_UpdateLock, Irql);
    return result;
}

NTSTATUS NTAPI UserValidateRgn(ULONG_PTR Hwnd)
{
    WINDOW *pWnd;

    W32K_VALIDATE_HWND(Hwnd);
    pWnd = (WINDOW *)Hwnd;

    pWnd->dirty = FALSE;
    pWnd->erasebg = FALSE;

    ClearUpdateRegions(Hwnd);

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserInvalidateRgn(ULONG_PTR Hwnd, ULONG_PTR hRgn, BOOL bErase)
{
    WINDOW *pWnd;
    W32K_RECT rect;

    W32K_VALIDATE_HWND(Hwnd);
    pWnd = (WINDOW *)Hwnd;

    pWnd->dirty = TRUE;
    if (bErase) pWnd->erasebg = TRUE;

    /* If a region handle is provided, use its bounding box.
     * Region handles in this codebase are memory pointers to RGNOBJ-like
     * structures which store a bounding rect (see GdiCreateRectRgn). */
    if (hRgn && hRgn >= 0x1000) {
        W32K_RECT *pRgnRect = (W32K_RECT *)hRgn;
        rect = *pRgnRect;
        AddUpdateRegion(Hwnd, &rect, bErase);
    } else {
        AddUpdateRegion(Hwnd, NULL, bErase);
    }

    UserPostMessage(Hwnd, WM_PAINT, 0, 0);

    DbgPrint("UPDATE: InvalidateRgn(%p, hrgn=%p, erase=%d)\n", (PVOID)Hwnd, (PVOID)hRgn, bErase);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserExcludeUpdateRgn(ULONG_PTR hdc, ULONG_PTR Hwnd)
{
    WINDOW *pWnd;
    KIRQL Irql;
    ULONG i;

    if (!hdc || hdc < 0x1000) return STATUS_INVALID_PARAMETER;
    if (!Hwnd || Hwnd < 0x1000) return STATUS_INVALID_HANDLE;

    pWnd = (WINDOW *)Hwnd;

    if (!pWnd->dirty) {
        return STATUS_SUCCESS;
    }

    /* The DC's clip region (stored as clipLeft/clipTop/clipRight/clipBottom
     * in BASEDC) should exclude the window's pending update areas. */
    W32K_LOCK_SPIN(g_UpdateLock, Irql);
    for (i = 0; i < MAX_UPDATE_REGIONS; i++) {
        if (g_UpdateRegions[i].InUse && g_UpdateRegions[i].Hwnd == Hwnd) {
            /* Exclude this rect from the DC clip box by adjusting bounds.
             * We shrink the clip box to exclude the update area. */
            BASEDC *pDc = (BASEDC *)hdc;
            pDc->clipLeft = g_UpdateRegions[i].Rect.right;
            pDc->clipTop = g_UpdateRegions[i].Rect.top;
            pDc->clipRight = (pDc->clipRight != 0) ? pDc->clipRight : g_UpdateRegions[i].Rect.right + 1;
            pDc->clipBottom = (pDc->clipBottom != 0) ? pDc->clipBottom : g_UpdateRegions[i].Rect.bottom;
            break;
        }
    }
    W32K_UNLOCK_SPIN(g_UpdateLock, Irql);

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetUpdateRgn(ULONG_PTR Hwnd, ULONG_PTR hRgn, BOOL bErase)
{
    WINDOW *pWnd;
    ULONG i;
    KIRQL Irql;
    BOOL found = FALSE;

    W32K_VALIDATE_HWND(Hwnd);
    pWnd = (WINDOW *)Hwnd;

    /* Copy the window's update region into the provided region handle.
     * The region handle points to an RGNOBJ-like structure with a bounding rect. */
    if (hRgn && hRgn >= 0x1000) {
        W32K_RECT *pRgnRect = (W32K_RECT *)hRgn;

        W32K_LOCK_SPIN(g_UpdateLock, Irql);
        for (i = 0; i < MAX_UPDATE_REGIONS; i++) {
            if (g_UpdateRegions[i].InUse && g_UpdateRegions[i].Hwnd == Hwnd) {
                *pRgnRect = g_UpdateRegions[i].Rect;
                found = TRUE;
                if (bErase) {
                    pWnd->erasebg = TRUE;
                }
                break;
            }
        }
        W32K_UNLOCK_SPIN(g_UpdateLock, Irql);

        if (!found) {
            RtlZeroMemory(pRgnRect, sizeof(W32K_RECT));
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserInvalidateRect2(ULONG_PTR hWnd, PW32K_RECT pRect, BOOL bErase)
{
    WINDOW *pWnd;

    W32K_VALIDATE_HWND(hWnd);
    pWnd = (WINDOW *)hWnd;

    pWnd->dirty = TRUE;
    if (bErase) pWnd->erasebg = TRUE;

    AddUpdateRegion(hWnd, pRect, bErase);

    UserPostMessage(hWnd, WM_PAINT, 0, 0);

    DbgPrint("UPDATE: InvalidateRect2(%p, erase=%d)\n", (PVOID)hWnd, bErase);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserBeginPaint2(ULONG_PTR hWnd, PW32K_PAINTSTRUCT pPs)
{
    WINDOW *pWnd;
    HDC hdc;
    ULONG i;
    KIRQL Irql;
    BOOL found = FALSE;

    W32K_VALIDATE_HWND(hWnd);
    pWnd = (WINDOW *)hWnd;
    if (!pPs) return STATUS_INVALID_PARAMETER;

    hdc = UserGetDC(hWnd);
    RtlZeroMemory(pPs, sizeof(W32K_PAINTSTRUCT));
    pPs->hdc = hdc;
    pPs->fErase = pWnd->erasebg;

    /* Find the specific update region for this window */
    W32K_LOCK_SPIN(g_UpdateLock, Irql);
    for (i = 0; i < MAX_UPDATE_REGIONS; i++) {
        if (g_UpdateRegions[i].InUse && g_UpdateRegions[i].Hwnd == hWnd) {
            pPs->rcPaint_left = g_UpdateRegions[i].Rect.left;
            pPs->rcPaint_top = g_UpdateRegions[i].Rect.top;
            pPs->rcPaint_right = g_UpdateRegions[i].Rect.right;
            pPs->rcPaint_bottom = g_UpdateRegions[i].Rect.bottom;
            g_UpdateRegions[i].InUse = FALSE;
            found = TRUE;
            break;
        }
    }
    W32K_UNLOCK_SPIN(g_UpdateLock, Irql);

    if (!found) {
        pPs->rcPaint_left = pWnd->x;
        pPs->rcPaint_top = pWnd->y;
        pPs->rcPaint_right = pWnd->x + pWnd->cx;
        pPs->rcPaint_bottom = pWnd->y + pWnd->cy;
    }

    pWnd->dirty = FALSE;
    pWnd->erasebg = FALSE;

    return (NTSTATUS)hdc;
}

NTSTATUS NTAPI UserEndPaint2(ULONG_PTR hWnd, PW32K_PAINTSTRUCT pPs)
{
    if (!pPs) return STATUS_INVALID_PARAMETER;
    UserReleaseDC(hWnd, (ULONG_PTR)pPs->hdc);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserRedrawWindow(ULONG_PTR Hwnd, PW32K_RECT pRect, ULONG_PTR hRgnUpdate,
                                  ULONG uFlags)
{
    WINDOW *pWnd;
    W32K_RECT combinedRect;
    BOOL hasRect = FALSE;

    W32K_VALIDATE_HWND(Hwnd);
    pWnd = (WINDOW *)Hwnd;

    /* Determine the area to redraw from pRect or hRgnUpdate */
    if (pRect) {
        combinedRect = *pRect;
        hasRect = TRUE;
    } else if (hRgnUpdate && hRgnUpdate >= 0x1000) {
        W32K_RECT *pRgnRect = (W32K_RECT *)hRgnUpdate;
        combinedRect = *pRgnRect;
        hasRect = TRUE;
    }

    if (uFlags & RDW_INVALIDATE) {
        pWnd->dirty = TRUE;
        if (hasRect) {
            AddUpdateRegion(Hwnd, &combinedRect, (uFlags & RDW_ERASE) ? TRUE : FALSE);
        } else {
            AddUpdateRegion(Hwnd, NULL, (uFlags & RDW_ERASE) ? TRUE : FALSE);
        }
        UserPostMessage(Hwnd, WM_PAINT, 0, 0);
    }
    if (uFlags & RDW_ERASE) {
        pWnd->erasebg = TRUE;
    }
    if (!(uFlags & RDW_INVALIDATE)) {
        /* Validate (remove from update region) */
        pWnd->dirty = FALSE;
        pWnd->erasebg = FALSE;
        if (hasRect) {
            ULONG i;
            KIRQL Irql;
            W32K_LOCK_SPIN(g_UpdateLock, Irql);
            for (i = 0; i < MAX_UPDATE_REGIONS; i++) {
                if (g_UpdateRegions[i].InUse && g_UpdateRegions[i].Hwnd == Hwnd) {
                    g_UpdateRegions[i].InUse = FALSE;
                }
            }
            W32K_UNLOCK_SPIN(g_UpdateLock, Irql);
        } else {
            ClearUpdateRegions(Hwnd);
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserUpdateLayeredWindow(ULONG_PTR Hwnd, ULONG_PTR hdcDst,
                                         PW32K_POINT pptDst, PW32K_SIZE psize,
                                         ULONG_PTR hdcSrc, PW32K_POINT pptSrc,
                                         ULONG crKey, PVOID pblend, ULONG dwFlags)
{
    WINDOW *pWnd;

    W32K_VALIDATE_HWND(Hwnd);
    pWnd = (WINDOW *)Hwnd;

    /* ULW flags: 0x1 = ULW_ALPHA, 0x2 = ULW_COLORKEY,
     * 0x4 = ULW_OPAQUE, 0x8 = ULW_EX_NORESIZE */

    /* Update layered window destination position */
    if (pptDst) {
        pWnd->x = pptDst->x;
        pWnd->y = pptDst->y;
    }

    /* Update layered window size */
    if (psize) {
        pWnd->cx = psize->cx;
        pWnd->cy = psize->cy;
    }

    /* If colorkey flag is set, store the transparency color key */
    if ((dwFlags & 0x2) && crKey) {
        pWnd->UserData = crKey;
    }

    /* If source DC and blend parameters are provided, schedule repaint */
    if (hdcSrc && pblend) {
        pWnd->dirty = TRUE;
        pWnd->erasebg = TRUE;
        UserPostMessage(Hwnd, WM_PAINT, 0, 0);
    }

    /* If only destination DC, use it for direct paint */
    if (hdcDst && !hdcSrc) {
        pWnd->dirty = TRUE;
        UserPostMessage(Hwnd, WM_PAINT, (ULONG_PTR)hdcDst, 0);
    }

    /* pptSrc offset used when blitting from source DC */
    if (pptSrc && hdcSrc) {
        DbgPrint("UPDATE: LayeredWindow src offset (%d,%d)\n", pptSrc->x, pptSrc->y);
    }

    DbgPrint("UPDATE: UpdateLayeredWindow(%p, dst=%p, src=%p, flags=0x%X)\n",
             (PVOID)Hwnd, (PVOID)hdcDst, (PVOID)hdcSrc, dwFlags);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserLockWindowUpdate(ULONG_PTR HwndLock)
{
    if (g_LockedWindow != 0 && HwndLock != 0) {
        /* Already locked to a different window */
        return STATUS_DEVICE_BUSY;
    }

    if (HwndLock != 0) {
        /* Lock: prevent the window from painting */
        g_LockedWindow = HwndLock;
        DbgPrint("UPDATE: LockWindowUpdate(%p)\n", (PVOID)HwndLock);
    } else {
        /* Unlock: repaint the previously locked window */
        if (g_LockedWindow != 0) {
            WINDOW *pWnd = (WINDOW *)g_LockedWindow;
            if ((ULONG_PTR)pWnd >= 0x1000) {
                pWnd->dirty = TRUE;
                UserPostMessage(g_LockedWindow, WM_PAINT, 0, 0);
            }
            g_LockedWindow = 0;
            DbgPrint("UPDATE: LockWindowUpdate(unlocked)\n");
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserUnlockWindowUpdate(VOID)
{
    if (g_LockedWindow == 0) return STATUS_NOT_FOUND;

    {
        WINDOW *pWnd = (WINDOW *)g_LockedWindow;
        if ((ULONG_PTR)pWnd >= 0x1000) {
            pWnd->dirty = TRUE;
            UserPostMessage(g_LockedWindow, WM_PAINT, 0, 0);
        }
    }
    g_LockedWindow = 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetRandomRgn(ULONG_PTR hdc, ULONG_PTR hrgn, INT iNum)
{
    BASEDC *pDc;

    if (!hdc || hdc < 0x1000) return STATUS_INVALID_PARAMETER;
    if (!hrgn || hrgn < 0x1000) return STATUS_INVALID_PARAMETER;

    pDc = (BASEDC *)hdc;

    /* iNum identifies which system region to retrieve.
     * 1 = SYSRGN, 2 = METARGN, 3 = APIRGN, 4 = RGN_COPY */
    switch (iNum) {
        case 1: { /* SYSRGN - system clip region (desktop coordinates) */
            W32K_RECT *pR = (W32K_RECT *)hrgn;
            pR->left = pDc->clipLeft;
            pR->top = pDc->clipTop;
            pR->right = pDc->clipRight;
            pR->bottom = pDc->clipBottom;
            break;
        }
        case 2: { /* METARGN - meta region from the DC's surface */
            W32K_RECT *pR = (W32K_RECT *)hrgn;
            if (pDc->psurface) {
                pR->left = 0;
                pR->top = 0;
                pR->right = pDc->psurface->sizlBitmap_cx;
                pR->bottom = pDc->psurface->sizlBitmap_cy;
            } else {
                RtlZeroMemory(pR, sizeof(W32K_RECT));
            }
            break;
        }
        case 3: { /* APIRGN - API-applied clip region */
            W32K_RECT *pR = (W32K_RECT *)hrgn;
            if (pDc->hClipRgn && pDc->hClipRgn >= 0x1000) {
                W32K_RECT *pClipR = (W32K_RECT *)pDc->hClipRgn;
                *pR = *pClipR;
            } else {
                pR->left = pDc->clipLeft;
                pR->top = pDc->clipTop;
                pR->right = pDc->clipRight;
                pR->bottom = pDc->clipBottom;
            }
            break;
        }
        case 4: { /* RGN_COPY - copy current clip region */
            W32K_RECT *pR = (W32K_RECT *)hrgn;
            pR->left = pDc->clipLeft;
            pR->top = pDc->clipTop;
            pR->right = pDc->clipRight;
            pR->bottom = pDc->clipBottom;
            break;
        }
        default:
            return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}
