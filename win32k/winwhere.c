/*
 * MinNT - win32k/winwhere.c
 * Window location and hit-testing for Win32k.
 */

#include "precomp.h"

/* Non-client area hit test codes */
#define HTERROR       (-2)
#define HTTRANSPARENT (-1)
#define HTNOWHERE     0
#define HTCLIENT      1
#define HTCAPTION     2
#define HTSYSMENU     3
#define HTGROWBOX     4
#define HTMENU        5
#define HTHSCROLL     6
#define HTVSCROLL     7
#define HTMINBUTTON   8
#define HTMAXBUTTON   9
#define HTLEFT        10
#define HTRIGHT       11
#define HTTOP         12
#define HTTOPLEFT     13
#define HTTOPRIGHT    14
#define HTBOTTOM      15
#define HTBOTTOMLEFT  16
#define HTBOTTOMRIGHT 17
#define HTBORDER      18

#define NC_BORDER_SIZE  4
#define NC_CAPTION_SIZE 24
#define NC_BUTTON_SIZE  16

/* Extended styles / system commands not yet in win32k.h */
#ifndef WS_EX_TRANSPARENT
#define WS_EX_TRANSPARENT 0x00000020L
#endif

#ifndef SC_CLOSE
#define SC_CLOSE 0xF060
#endif

NTSTATUS NTAPI WinWhereInit(VOID)
{
    DbgPrint("WINWHERE: initialized\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserWindowFromPoint2(LONG x, LONG y, PULONG_PTR pHwnd)
{
    return UserWindowFromPoint(x, y, pHwnd);
}

NTSTATUS NTAPI UserChildWindowFromPointEx(ULONG_PTR HwndParent, LONG x, LONG y,
                                            ULONG uFlags, PULONG_PTR pHwnd)
{
    WINDOW *pParent;
    ULONG_PTR hChild;

    if (!pHwnd) return STATUS_INVALID_PARAMETER;

    /* Default: no matching child - return the parent. */
    *pHwnd = HwndParent;

    if (!HwndParent || HwndParent < 0x1000) return STATUS_SUCCESS;

    pParent = (WINDOW *)HwndParent;

    /* Walk the child list: hwndChild (topmost) -> hwndNext (lower z-order).
     * The first (highest z-order) child whose bounding rectangle contains
     * the point (and that is not filtered out by uFlags) wins. */
    hChild = pParent->hwndChild;
    while (hChild && hChild >= 0x1000) {
        WINDOW *pChild = (WINDOW *)hChild;

        if (x >= pChild->x && x < pChild->x + pChild->cx &&
            y >= pChild->y && y < pChild->y + pChild->cy) {

            /* Apply skip filters requested by the caller. */
            if ((uFlags & CWP_SKIPINVISIBLE) && !pChild->visible) {
                /* skip - window is not visible */
            } else if ((uFlags & CWP_SKIPDISABLED) &&
                       (pChild->dwStyle & WS_DISABLED)) {
                /* skip - window is disabled */
            } else if ((uFlags & CWP_SKIPTRANSPARENT) &&
                       (pChild->dwExStyle & WS_EX_TRANSPARENT)) {
                /* skip - window is transparent to mouse messages */
            } else {
                *pHwnd = hChild;
                return STATUS_SUCCESS;
            }
        }

        hChild = pChild->hwndNext;
    }

    /* No child contained the point - leave *pHwnd == HwndParent. */
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserRealChildWindowFromPoint(ULONG_PTR HwndParent, LONG x, LONG y,
                                              PULONG_PTR pHwnd)
{
    /* Same as ChildWindowFromPointEx but tests every child regardless of
     * visibility / enabled / transparency state (uFlags == CWP_ALL). */
    return UserChildWindowFromPointEx(HwndParent, x, y, CWP_ALL, pHwnd);
}

NTSTATUS NTAPI UserDefWindowProc(ULONG_PTR Hwnd, ULONG Msg, ULONG_PTR wParam,
                                   LONG_PTR lParam, PLONG_PTR pResult)
{
    WINDOW *pWnd;

    if (!pResult) return STATUS_INVALID_PARAMETER;
    *pResult = 0;

    if (!Hwnd || Hwnd < 0x1000) return STATUS_INVALID_HANDLE;
    pWnd = (WINDOW *)Hwnd;

    switch (Msg) {
        case WM_NCCREATE:
        case WM_NCCALCSIZE:
            *pResult = 1;
            break;

        case WM_NCPAINT:
            /* Draw window border and title bar */
            *pResult = 0;
            break;

        case WM_NCHITTEST: {
            LONG hx = (LONG)(lParam & 0xFFFF);
            LONG hy = (LONG)((lParam >> 16) & 0xFFFF);
            LONG relX = hx - pWnd->x;
            LONG relY = hy - pWnd->y;

            /* Border hit testing */
            if (relX < NC_BORDER_SIZE && relY < NC_BORDER_SIZE) {
                *pResult = HTTOPLEFT;
            } else if (relX >= pWnd->cx - NC_BORDER_SIZE && relY < NC_BORDER_SIZE) {
                *pResult = HTTOPRIGHT;
            } else if (relX < NC_BORDER_SIZE && relY >= pWnd->cy - NC_BORDER_SIZE) {
                *pResult = HTBOTTOMLEFT;
            } else if (relX >= pWnd->cx - NC_BORDER_SIZE && relY >= pWnd->cy - NC_BORDER_SIZE) {
                *pResult = HTBOTTOMRIGHT;
            } else if (relX < NC_BORDER_SIZE) {
                *pResult = HTLEFT;
            } else if (relX >= pWnd->cx - NC_BORDER_SIZE) {
                *pResult = HTRIGHT;
            } else if (relY < NC_BORDER_SIZE) {
                *pResult = HTTOP;
            } else if (relY >= pWnd->cy - NC_BORDER_SIZE) {
                *pResult = HTBOTTOM;
            } else if (relY < NC_CAPTION_SIZE) {
                *pResult = HTCAPTION;
            } else {
                *pResult = HTCLIENT;
            }
            break;
        }

        case WM_GETMINMAXINFO:
            *pResult = 0;
            break;

        case WM_CLOSE:
            UserDestroyWindow(Hwnd);
            *pResult = 0;
            break;

        case WM_QUERYENDSESSION:
            *pResult = 1;
            break;

        case WM_ENDSESSION:
            *pResult = 0;
            break;

        case WM_SYSCOMMAND: {
            /* wParam carries the system command. The low four bits are
             * used internally by Windows for mouse position, so mask them
             * off before comparing (standard WM_SYSCOMMAND convention). */
            ULONG uCmd = ((ULONG)wParam) & 0xFFF0;
            if (uCmd == SC_CLOSE) {
                UserDestroyWindow(Hwnd);
                *pResult = 0;
            } else {
                *pResult = 0;
            }
            break;
        }

        case WM_SETCURSOR:
            /* wParam is the hwnd of the window that contains the cursor.
             * Returning TRUE tells the system we have handled the cursor
             * (it will not fall through to the class/default cursor). */
            if (wParam == Hwnd || wParam == 0) {
                *pResult = TRUE;
            } else {
                *pResult = TRUE;
            }
            break;

        case WM_SETTEXT: {
            /* wParam is reserved (0); lParam points to the null-terminated
             * wide string to install as the window text. */
            PCWSTR lpsz = (PCWSTR)lParam;
            ULONG i;
            if (lpsz) {
                for (i = 0; i < 255 && lpsz[i]; i++)
                    pWnd->Text[i] = lpsz[i];
                pWnd->Text[i] = L'\0';
            } else {
                pWnd->Text[0] = L'\0';
            }
            *pResult = TRUE;
            break;
        }

        case WM_GETTEXT: {
            /* wParam = max number of characters (including terminator);
             * lParam = destination buffer. Return the count of chars copied
             * (excluding the terminator). */
            PWCHAR  lpBuf = (PWCHAR)lParam;
            ULONG   nMaxCount = (ULONG)wParam;
            ULONG   len = 0, i;
            while (len < 255 && pWnd->Text[len]) len++;
            if (!lpBuf || nMaxCount == 0) {
                *pResult = 0;
                break;
            }
            for (i = 0; i < len && (i + 1) < nMaxCount; i++)
                lpBuf[i] = pWnd->Text[i];
            lpBuf[i] = L'\0';
            *pResult = (LONG_PTR)i;
            break;
        }

        case WM_GETTEXTLENGTH: {
            /* Return the length, in characters, of the window text. */
            ULONG len = 0;
            while (len < 255 && pWnd->Text[len]) len++;
            *pResult = (LONG_PTR)len;
            break;
        }

        default:
            break;
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserDefFrameProc(ULONG_PTR HwndClient, ULONG_PTR HwndFrame,
                                  ULONG Msg, ULONG_PTR wParam, LONG_PTR lParam,
                                  PLONG_PTR pResult)
{
    /* For MDI frame windows, HwndClient identifies the MDI client window.
     * On WM_CREATE the client window has just been created and the frame
     * must forward the message to it so the MDI client gets initialized.
     * All other messages are handled on the frame window itself. */
    if (Msg == WM_CREATE && HwndClient && HwndClient >= 0x1000) {
        return UserDefWindowProc(HwndClient, Msg, wParam, lParam, pResult);
    }

    return UserDefWindowProc(HwndFrame, Msg, wParam, lParam, pResult);
}

NTSTATUS NTAPI UserCallWindowProc(ULONG_PTR WndProc, ULONG_PTR Hwnd, ULONG Msg,
                                    ULONG_PTR wParam, LONG_PTR lParam, PLONG_PTR pResult)
{
    W32K_WNDPROC proc = (W32K_WNDPROC)WndProc;
    if (!pResult) return STATUS_INVALID_PARAMETER;
    if (!proc) return UserDefWindowProc(Hwnd, Msg, wParam, lParam, pResult);

    *pResult = proc((HWND)Hwnd, Msg, wParam, lParam);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserIsZoomed(ULONG_PTR Hwnd, PBOOL pZoomed)
{
    WINDOW *pWnd;
    if (!pZoomed) return STATUS_INVALID_PARAMETER;

    *pZoomed = FALSE;
    if (Hwnd != 0 && Hwnd >= 0x1000) {
        pWnd = (WINDOW *)Hwnd;
        *pZoomed = (pWnd->dwStyle & WS_MAXIMIZE) ? TRUE : FALSE;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserIsIconic(ULONG_PTR Hwnd, PBOOL pIconic)
{
    WINDOW *pWnd;
    if (!pIconic) return STATUS_INVALID_PARAMETER;

    *pIconic = FALSE;
    if (Hwnd != 0 && Hwnd >= 0x1000) {
        pWnd = (WINDOW *)Hwnd;
        *pIconic = (pWnd->dwStyle & WS_MINIMIZE) ? TRUE : FALSE;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserIsWindowUnicode2(ULONG_PTR Hwnd, PBOOL pIsUnicode)
{
    WINDOW *pWnd;
    if (!pIsUnicode) return STATUS_INVALID_PARAMETER;

    /* Default to Unicode if we cannot resolve the window. */
    *pIsUnicode = TRUE;
    if (Hwnd && Hwnd >= 0x1000) {
        pWnd = (WINDOW *)Hwnd;
        *pIsUnicode = pWnd->Unicode ? TRUE : FALSE;
    }
    return STATUS_SUCCESS;
}
