/*
 * MinNT - ke/exports/user32_exports.c
 * user32.dll exports — window management, message pump, input, menus.
 *
 * Routes to win32k kernel functions. All __attribute__((ms_abi)).
 */

#include <nt/ntdef.h>
#include <nt/ex.h>
#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/exe.h>
#include "../win32k/win32k.h"
#include <ndk/obfuncs.h>
#ifndef UINT
typedef unsigned int UINT;
#endif
typedef ULONG_PTR UINT_PTR;
#ifndef SWP_NOREDRAW
#define SWP_NOREDRAW 0x0008
#endif

/* ============================================================================
 * Window Class Registration
 * ========================================================================== */

__attribute__((ms_abi))
static ULONG_PTR RegisterClassExA_msabi(const void *pWndClass)
{
    /* WNDCLASSEXA layout: cbSize, style, lpfnWndProc, cbClsExtra, cbWndExtra,
       hInstance, hIcon, hCursor, hbrBackground, lpszMenuName, lpszClassName,
       hIconSm */
    const ULONG_PTR *wc = (const ULONG_PTR *)pWndClass;
    W32K_WNDCLASS kwc;
    kwc.style = (ULONG)wc[1];
    kwc.lpfnWndProc = (W32K_WNDPROC)wc[2];
    kwc.cbClsExtra = (INT)wc[3];
    kwc.cbWndExtra = (INT)wc[4];
    kwc.hInstance = wc[5];
    kwc.hIcon = wc[6];
    kwc.hCursor = wc[7];
    kwc.hbrBackground = wc[8];
    kwc.lpszMenuName = (PCWSTR)wc[9];
    /* lpszClassName is a CHAR* for A version — convert to WCHAR */
    static WCHAR wname[64];
    const CHAR *aname = (const CHAR *)wc[10];
    if (aname) {
        int i;
        for (i = 0; aname[i] && i < 63; i++) wname[i] = (WCHAR)aname[i];
        wname[i] = 0;
        kwc.lpszClassName = wname;
    } else {
        kwc.lpszClassName = NULL;
    }
    NTSTATUS s = UserRegisterClassEx(0, &kwc);
    return NT_SUCCESS(s) ? 0x10000 : 0; /* fake atom */
}

__attribute__((ms_abi))
static ULONG_PTR RegisterClassExW_msabi(const void *pWndClass)
{
    const ULONG_PTR *wc = (const ULONG_PTR *)pWndClass;
    W32K_WNDCLASS kwc;
    kwc.style = (ULONG)wc[1];
    kwc.lpfnWndProc = (W32K_WNDPROC)wc[2];
    kwc.cbClsExtra = (INT)wc[3];
    kwc.cbWndExtra = (INT)wc[4];
    kwc.hInstance = wc[5];
    kwc.hIcon = wc[6];
    kwc.hCursor = wc[7];
    kwc.hbrBackground = wc[8];
    kwc.lpszMenuName = (PCWSTR)wc[9];
    kwc.lpszClassName = (PCWSTR)wc[10];
    NTSTATUS s = UserRegisterClassEx(0, &kwc);
    return NT_SUCCESS(s) ? 0x10000 : 0;
}

__attribute__((ms_abi))
static ULONG_PTR RegisterClassA_msabi(const void *pWndClass)
{
    return RegisterClassExA_msabi(pWndClass);
}

__attribute__((ms_abi))
static ULONG_PTR RegisterClassW_msabi(const void *pWndClass)
{
    return RegisterClassExW_msabi(pWndClass);
}

__attribute__((ms_abi))
static BOOL UnregisterClassA_msabi(const CHAR *lpClassName, ULONG_PTR hInstance)
{
    (void)lpClassName; (void)hInstance;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL UnregisterClassW_msabi(const WCHAR *lpClassName, ULONG_PTR hInstance)
{
    (void)lpClassName; (void)hInstance;
    return TRUE;
}

/* ============================================================================
 * Window Creation
 * ========================================================================== */

__attribute__((ms_abi))
static HWND CreateWindowExA_msabi(
    ULONG dwExStyle, const CHAR *lpClassName, const CHAR *lpWindowName,
    ULONG dwStyle, INT x, INT y, INT nWidth, INT nHeight,
    HWND hWndParent, HMENU hMenu, ULONG_PTR hInstance, PVOID lpParam)
{
    static WCHAR wcls[64], wname[128];
    UINT i;
    if (lpClassName) { for (i = 0; lpClassName[i] && i < 63; i++) wcls[i] = (WCHAR)lpClassName[i]; wcls[i] = 0; }
    else wcls[0] = 0;
    if (lpWindowName) { for (i = 0; lpWindowName[i] && i < 127; i++) wname[i] = (WCHAR)lpWindowName[i]; wname[i] = 0; }
    else wname[0] = 0;
    return (HWND)UserCreateWindowEx(dwExStyle, (ULONG_PTR)wcls, 0, (ULONG_PTR)wname,
                                     dwStyle, x, y, nWidth, nHeight,
                                     hWndParent, hMenu, hInstance, (ULONG_PTR)lpParam, 0, 0);
}

__attribute__((ms_abi))
static HWND CreateWindowExW_msabi(
    ULONG dwExStyle, const WCHAR *lpClassName, const WCHAR *lpWindowName,
    ULONG dwStyle, INT x, INT y, INT nWidth, INT nHeight,
    HWND hWndParent, HMENU hMenu, ULONG_PTR hInstance, PVOID lpParam)
{
    return (HWND)UserCreateWindowEx(dwExStyle, (ULONG_PTR)lpClassName, 0, (ULONG_PTR)lpWindowName,
                                     dwStyle, x, y, nWidth, nHeight,
                                     hWndParent, hMenu, hInstance, (ULONG_PTR)lpParam, 0, 0);
}

__attribute__((ms_abi))
static HWND CreateWindowA_msabi(
    const CHAR *lpClassName, const CHAR *lpWindowName, ULONG dwStyle,
    INT x, INT y, INT nWidth, INT nHeight, HWND hWndParent, HMENU hMenu,
    ULONG_PTR hInstance, PVOID lpParam)
{
    return CreateWindowExA_msabi(0, lpClassName, lpWindowName, dwStyle, x, y,
                                   nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}

__attribute__((ms_abi))
static HWND CreateWindowW_msabi(
    const WCHAR *lpClassName, const WCHAR *lpWindowName, ULONG dwStyle,
    INT x, INT y, INT nWidth, INT nHeight, HWND hWndParent, HMENU hMenu,
    ULONG_PTR hInstance, PVOID lpParam)
{
    return CreateWindowExW_msabi(0, lpClassName, lpWindowName, dwStyle, x, y,
                                   nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}

/* ============================================================================
 * Window Management
 * ========================================================================== */

__attribute__((ms_abi))
static BOOL ShowWindow_msabi(HWND hWnd, INT nCmdShow)
{
    return NT_SUCCESS(UserShowWindow(hWnd, nCmdShow));
}

__attribute__((ms_abi))
static BOOL UpdateWindow_msabi(HWND hWnd)
{
    /* Send WM_PAINT */
    if (hWnd) UserInvalidateRect(hWnd, NULL, FALSE);
    return TRUE;
}

__attribute__((ms_abi))
static BOOL DestroyWindow_msabi(HWND hWnd)
{
    return NT_SUCCESS(UserDestroyWindow(hWnd));
}

__attribute__((ms_abi))
static BOOL IsWindow_msabi(HWND hWnd)
{
    return hWnd != 0;
}

__attribute__((ms_abi))
static BOOL IsWindowVisible_msabi(HWND hWnd)
{
    (void)hWnd;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL IsWindowEnabled_msabi(HWND hWnd)
{
    (void)hWnd;
    return TRUE;
}

__attribute__((ms_abi))
static HWND GetParent_msabi(HWND hWnd)
{
    (void)hWnd;
    return 0;
}

__attribute__((ms_abi))
static HWND SetParent_msabi(HWND hWndChild, HWND hWndNewParent)
{
    (void)hWndChild; (void)hWndNewParent;
    return 0;
}

__attribute__((ms_abi))
static BOOL GetWindowRect_msabi(HWND hWnd, PVOID lpRect)
{
    return UserGetWindowRect(hWnd, (ULONG_PTR)lpRect);
}

__attribute__((ms_abi))
static BOOL GetClientRect_msabi(HWND hWnd, PVOID lpRect)
{
    return UserGetClientRect(hWnd, (ULONG_PTR)lpRect);
}

__attribute__((ms_abi))
static BOOL MoveWindow_msabi(HWND hWnd, INT X, INT Y, INT nWidth, INT nHeight, BOOL bRepaint)
{
    return NT_SUCCESS(UserSetWindowPos(hWnd, 0, X, Y, nWidth, nHeight,
                                         SWP_NOZORDER | (bRepaint ? 0 : SWP_NOREDRAW)));
}

__attribute__((ms_abi))
static BOOL SetWindowPos_msabi(HWND hWnd, HWND hWndInsertAfter,
    INT X, INT Y, INT cx, INT cy, UINT uFlags)
{
    return NT_SUCCESS(UserSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags));
}

__attribute__((ms_abi))
static HWND GetForegroundWindow_msabi(VOID)
{
    return (HWND)UserGetForegroundWindow();
}

__attribute__((ms_abi))
static BOOL SetForegroundWindow_msabi(HWND hWnd)
{
    return NT_SUCCESS(UserSetForegroundWindow(hWnd));
}

__attribute__((ms_abi))
static HWND SetFocus_msabi(HWND hWnd)
{
    return (HWND)UserSetFocus(hWnd);
}

__attribute__((ms_abi))
static HWND SetCapture_msabi(HWND hWnd)
{
    return (HWND)UserSetCapture(hWnd);
}

__attribute__((ms_abi))
static BOOL ReleaseCapture_msabi(VOID)
{
    UserSetCapture(0);
    return TRUE;
}

__attribute__((ms_abi))
static BOOL InvalidateRect_msabi(HWND hWnd, PVOID lpRect, BOOL bErase)
{
    return NT_SUCCESS(UserInvalidateRect(hWnd, (ULONG_PTR)lpRect, bErase));
}

__attribute__((ms_abi))
static BOOL ValidateRect_msabi(HWND hWnd, PVOID lpRect)
{
    (void)hWnd; (void)lpRect;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL RedrawWindow_msabi(HWND hWnd, PVOID lprcUpdate, HRGN hrgnUpdate, UINT flags)
{
    (void)hWnd; (void)lprcUpdate; (void)hrgnUpdate; (void)flags;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL ShowCursor_msabi(BOOL bShow)
{
    (void)bShow;
    return TRUE;
}

__attribute__((ms_abi))
static HCURSOR SetCursor_msabi(HCURSOR hCursor)
{
    return hCursor;
}

__attribute__((ms_abi))
static HCURSOR LoadCursorA_msabi(ULONG_PTR hInstance, const CHAR *lpCursorName)
{
    (void)hInstance; (void)lpCursorName;
    return (HCURSOR)1;
}

__attribute__((ms_abi))
static HCURSOR LoadCursorW_msabi(ULONG_PTR hInstance, const WCHAR *lpCursorName)
{
    (void)hInstance; (void)lpCursorName;
    return (HCURSOR)1;
}

__attribute__((ms_abi))
static HICON LoadIconA_msabi(ULONG_PTR hInstance, const CHAR *lpIconName)
{
    (void)hInstance; (void)lpIconName;
    return (HICON)1;
}

__attribute__((ms_abi))
static HICON LoadIconW_msabi(ULONG_PTR hInstance, const WCHAR *lpIconName)
{
    (void)hInstance; (void)lpIconName;
    return (HICON)1;
}

/* ============================================================================
 * Message Pump
 * ========================================================================== */

__attribute__((ms_abi))
static BOOL GetMessageA_msabi(PVOID lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    W32K_MSG *msg = (W32K_MSG *)lpMsg;
    NTSTATUS s = UserGetMessage(msg, hWnd, wMsgFilterMin, wMsgFilterMax);
    return NT_SUCCESS(s) ? (msg->message != WM_QUIT) : 0;
}

__attribute__((ms_abi))
static BOOL GetMessageW_msabi(PVOID lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    return GetMessageA_msabi(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
}

__attribute__((ms_abi))
static BOOL PeekMessageA_msabi(PVOID lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    W32K_MSG *msg = (W32K_MSG *)lpMsg;
    NTSTATUS s = UserPeekMessage(msg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    return NT_SUCCESS(s);
}

__attribute__((ms_abi))
static BOOL PeekMessageW_msabi(PVOID lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    return PeekMessageA_msabi(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
}

__attribute__((ms_abi))
static BOOL TranslateMessage_msabi(PVOID lpMsg)
{
    return NT_SUCCESS(UserTranslateMessage((PW32K_MSG)lpMsg, TM_POSTCHARCHARS | TM_KEYDOWN));
}

__attribute__((ms_abi))
static LONG_PTR DispatchMessageA_msabi(PVOID lpMsg)
{
    return (LONG_PTR)UserDispatchMessage(*((PW32K_MSG)lpMsg));
}

__attribute__((ms_abi))
static LONG_PTR DispatchMessageW_msabi(PVOID lpMsg)
{
    return (LONG_PTR)UserDispatchMessage(*((PW32K_MSG)lpMsg));
}

__attribute__((ms_abi))
static LONG_PTR SendMessageA_msabi(HWND hWnd, UINT Msg, ULONG_PTR wParam, LONG_PTR lParam)
{
    return (LONG_PTR)UserSendMessage(hWnd, Msg, wParam, lParam);
}

__attribute__((ms_abi))
static LONG_PTR SendMessageW_msabi(HWND hWnd, UINT Msg, ULONG_PTR wParam, LONG_PTR lParam)
{
    return (LONG_PTR)UserSendMessage(hWnd, Msg, wParam, lParam);
}

__attribute__((ms_abi))
static BOOL PostMessageA_msabi(HWND hWnd, UINT Msg, ULONG_PTR wParam, LONG_PTR lParam)
{
    return NT_SUCCESS(UserPostMessage(hWnd, Msg, wParam, lParam));
}

__attribute__((ms_abi))
static BOOL PostMessageW_msabi(HWND hWnd, UINT Msg, ULONG_PTR wParam, LONG_PTR lParam)
{
    return NT_SUCCESS(UserPostMessage(hWnd, Msg, wParam, lParam));
}

__attribute__((ms_abi))
static VOID PostQuitMessage_msabi(INT nExitCode)
{
    W32K_MSG msg;
    msg.hwnd = 0;
    msg.message = WM_QUIT;
    msg.wParam = (ULONG_PTR)nExitCode;
    msg.lParam = 0;
    UserPostMessage(0, WM_QUIT, (ULONG_PTR)nExitCode, 0);
}

__attribute__((ms_abi))
static LONG_PTR DefWindowProcA_msabi(HWND hWnd, UINT Msg, ULONG_PTR wParam, LONG_PTR lParam)
{
    /* Default processing: just return 0 for most messages */
    if (Msg == WM_DESTROY) {
        PostQuitMessage_msabi(0);
    }
    (void)hWnd; (void)wParam; (void)lParam;
    return 0;
}

__attribute__((ms_abi))
static LONG_PTR DefWindowProcW_msabi(HWND hWnd, UINT Msg, ULONG_PTR wParam, LONG_PTR lParam)
{
    return DefWindowProcA_msabi(hWnd, Msg, wParam, lParam);
}

/* ============================================================================
 * Painting
 * ========================================================================== */

__attribute__((ms_abi))
static HDC BeginPaint_msabi(HWND hWnd, PVOID lpPaint)
{
    UserBeginPaint(hWnd, (PW32K_PAINTSTRUCT)lpPaint);
    return (HDC)1; /* fake DC */
}

__attribute__((ms_abi))
static BOOL EndPaint_msabi(HWND hWnd, PVOID lpPaint)
{
    return NT_SUCCESS(UserEndPaint(hWnd, (PW32K_PAINTSTRUCT)lpPaint));
}

__attribute__((ms_abi))
static HDC GetDC_msabi(HWND hWnd)
{
    return (HDC)UserGetDC(hWnd);
}

__attribute__((ms_abi))
static INT ReleaseDC_msabi(HWND hWnd, HDC hDC)
{
    return NT_SUCCESS(UserReleaseDC(hWnd, hDC)) ? 1 : 0;
}

__attribute__((ms_abi))
static HDC GetWindowDC_msabi(HWND hWnd)
{
    return GetDC_msabi(hWnd);
}

/* ============================================================================
 * Window Text
 * ========================================================================== */

__attribute__((ms_abi))
static BOOL SetWindowTextA_msabi(HWND hWnd, const CHAR *lpString)
{
    (void)hWnd; (void)lpString;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL SetWindowTextW_msabi(HWND hWnd, const WCHAR *lpString)
{
    (void)hWnd; (void)lpString;
    return TRUE;
}

__attribute__((ms_abi))
static INT GetWindowTextA_msabi(HWND hWnd, CHAR *lpString, INT nMaxCount)
{
    if (lpString && nMaxCount > 0) lpString[0] = 0;
    return 0;
}

__attribute__((ms_abi))
static INT GetWindowTextW_msabi(HWND hWnd, WCHAR *lpString, INT nMaxCount)
{
    if (lpString && nMaxCount > 0) lpString[0] = 0;
    return 0;
}

__attribute__((ms_abi))
static INT GetWindowTextLengthA_msabi(HWND hWnd)
{
    (void)hWnd;
    return 0;
}

/* ============================================================================
 * Misc Functions
 * ========================================================================== */

__attribute__((ms_abi))
static INT MessageBoxA_msabi(HWND hWnd, const CHAR *lpText, const CHAR *lpCaption, UINT uType)
{
    (void)hWnd; (void)uType;
    DbgPrint("EXE: [%s] %s\n", lpCaption ? lpCaption : "", lpText ? lpText : "");
    return 1; /* IDOK */
}

__attribute__((ms_abi))
static INT MessageBoxW_msabi(HWND hWnd, const WCHAR *lpText, const WCHAR *lpCaption, UINT uType)
{
    (void)hWnd; (void)uType;
    /* Convert to ANSI and print */
    CHAR atext[256], acap[256];
    if (lpText) { UINT i; for (i = 0; lpText[i] && i < 255; i++) atext[i] = (CHAR)lpText[i]; atext[i] = 0; } else atext[0] = 0;
    if (lpCaption) { UINT i; for (i = 0; lpCaption[i] && i < 255; i++) acap[i] = (CHAR)lpCaption[i]; acap[i] = 0; } else acap[0] = 0;
    DbgPrint("EXE: [%s] %s\n", acap, atext);
    return 1;
}

__attribute__((ms_abi))
static ULONG GetTickCount_user32_msabi(VOID) /* User32's GetTickCount */
{
    extern volatile ULONG64 KeTickCount;
    return (ULONG)KeTickCount;
}

__attribute__((ms_abi))
static UINT_PTR SetTimer_msabi(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, PVOID lpTimerFunc)
{
    (void)hWnd; (void)nIDEvent; (void)uElapse; (void)lpTimerFunc;
    return 1; /* fake timer ID */
}

__attribute__((ms_abi))
static BOOL KillTimer_msabi(HWND hWnd, UINT_PTR nIDEvent)
{
    (void)hWnd; (void)nIDEvent;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL AdjustWindowRect_msabi(PVOID lpRect, ULONG dwStyle, BOOL bMenu)
{
    (void)lpRect; (void)dwStyle; (void)bMenu;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL AdjustWindowRectEx_msabi(PVOID lpRect, ULONG dwStyle, BOOL bMenu, ULONG dwExStyle)
{
    (void)lpRect; (void)dwStyle; (void)bMenu; (void)dwExStyle;
    return TRUE;
}

__attribute__((ms_abi))
static ULONG GetWindowLongA_msabi(HWND hWnd, INT nIndex)
{
    (void)hWnd; (void)nIndex;
    return 0;
}

__attribute__((ms_abi))
static ULONG_PTR SetWindowLongPtrA_msabi(HWND hWnd, INT nIndex, ULONG_PTR dwNewLong)
{
    (void)hWnd; (void)nIndex;
    return dwNewLong;
}

__attribute__((ms_abi))
static ULONG_PTR GetWindowLongPtrA_msabi(HWND hWnd, INT nIndex)
{
    (void)hWnd; (void)nIndex;
    return 0;
}

__attribute__((ms_abi))
static BOOL SetWindowRgn_msabi(HWND hWnd, HRGN hRgn, BOOL bRedraw)
{
    (void)hWnd; (void)hRgn; (void)bRedraw;
    return TRUE;
}

__attribute__((ms_abi))
static USHORT GetAsyncKeyState_msabi(INT vKey)
{
    return (USHORT)UserGetAsyncKeyState(vKey);
}

__attribute__((ms_abi))
static BOOL GetCursorPos_msabi(PVOID lpPoint)
{
    SHORT mx = HalpMouseGetX();
    SHORT my = HalpMouseGetY();
    if (lpPoint) {
        LONG *pt = (LONG *)lpPoint;
        pt[0] = mx;
        pt[1] = my;
    }
    return TRUE;
}

__attribute__((ms_abi))
static HWND WindowFromPoint_msabi(LONG x, LONG y)
{
    (void)x; (void)y;
    return 0;
}

__attribute__((ms_abi))
static ULONG GetSystemMetrics_msabi(INT nIndex)
{
    switch (nIndex) {
    case 0: return (INT)HalpFbGetWidth();   /* SM_CXSCREEN */
    case 1: return (INT)HalpFbGetHeight();  /* SM_CYSCREEN */
    case 4: return 16;  /* SM_CXFRAME */
    case 5: return 32;  /* SM_CYCAPTION */
    case 15: return 16; /* SM_CYMENU */
    case 32: return 1;  /* SM_CXCURSOR */
    case 33: return 1;  /* SM_CYCURSOR */
    default: return 0;
    }
}

__attribute__((ms_abi))
static BOOL GetKeyState_msabi(INT nVirtKey)
{
    (void)nVirtKey;
    return FALSE;
}

__attribute__((ms_abi))
static BOOL EnableWindow_msabi(HWND hWnd, BOOL bEnable)
{
    (void)hWnd; (void)bEnable;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL BringWindowToTop_msabi(HWND hWnd)
{
    (void)hWnd;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL ScreenToClient_msabi(HWND hWnd, PVOID lpPoint)
{
    (void)hWnd;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL ClientToScreen_msabi(HWND hWnd, PVOID lpPoint)
{
    (void)hWnd;
    return TRUE;
}

/* ============================================================================
 * Registration
 * ========================================================================== */

VOID NTAPI User32RegisterExports(VOID)
{
#define UREG(name, ptr) ExeRegisterExport("user32.dll", name, ptr)

    /* Class Registration */
    UREG("RegisterClassExA", RegisterClassExA_msabi);
    UREG("RegisterClassExW", RegisterClassExW_msabi);
    UREG("RegisterClassA", RegisterClassA_msabi);
    UREG("RegisterClassW", RegisterClassW_msabi);
    UREG("UnregisterClassA", UnregisterClassA_msabi);
    UREG("UnregisterClassW", UnregisterClassW_msabi);

    /* Window Creation */
    UREG("CreateWindowExA", CreateWindowExA_msabi);
    UREG("CreateWindowExW", CreateWindowExW_msabi);
    UREG("CreateWindowA", CreateWindowA_msabi);
    UREG("CreateWindowW", CreateWindowW_msabi);

    /* Window Management */
    UREG("ShowWindow", ShowWindow_msabi);
    UREG("UpdateWindow", UpdateWindow_msabi);
    UREG("DestroyWindow", DestroyWindow_msabi);
    UREG("IsWindow", IsWindow_msabi);
    UREG("IsWindowVisible", IsWindowVisible_msabi);
    UREG("IsWindowEnabled", IsWindowEnabled_msabi);
    UREG("GetParent", GetParent_msabi);
    UREG("SetParent", SetParent_msabi);
    UREG("GetWindowRect", GetWindowRect_msabi);
    UREG("GetClientRect", GetClientRect_msabi);
    UREG("MoveWindow", MoveWindow_msabi);
    UREG("SetWindowPos", SetWindowPos_msabi);
    UREG("GetForegroundWindow", GetForegroundWindow_msabi);
    UREG("SetForegroundWindow", SetForegroundWindow_msabi);
    UREG("SetFocus", SetFocus_msabi);
    UREG("SetCapture", SetCapture_msabi);
    UREG("ReleaseCapture", ReleaseCapture_msabi);
    UREG("InvalidateRect", InvalidateRect_msabi);
    UREG("ValidateRect", ValidateRect_msabi);
    UREG("RedrawWindow", RedrawWindow_msabi);
    UREG("ShowCursor", ShowCursor_msabi);
    UREG("SetCursor", SetCursor_msabi);
    UREG("LoadCursorA", LoadCursorA_msabi);
    UREG("LoadCursorW", LoadCursorW_msabi);
    UREG("LoadIconA", LoadIconA_msabi);
    UREG("LoadIconW", LoadIconW_msabi);

    /* Message Pump */
    UREG("GetMessageA", GetMessageA_msabi);
    UREG("GetMessageW", GetMessageW_msabi);
    UREG("PeekMessageA", PeekMessageA_msabi);
    UREG("PeekMessageW", PeekMessageW_msabi);
    UREG("TranslateMessage", TranslateMessage_msabi);
    UREG("DispatchMessageA", DispatchMessageA_msabi);
    UREG("DispatchMessageW", DispatchMessageW_msabi);
    UREG("SendMessageA", SendMessageA_msabi);
    UREG("SendMessageW", SendMessageW_msabi);
    UREG("PostMessageA", PostMessageA_msabi);
    UREG("PostMessageW", PostMessageW_msabi);
    UREG("PostQuitMessage", PostQuitMessage_msabi);
    UREG("DefWindowProcA", DefWindowProcA_msabi);
    UREG("DefWindowProcW", DefWindowProcW_msabi);

    /* Painting */
    UREG("BeginPaint", BeginPaint_msabi);
    UREG("EndPaint", EndPaint_msabi);
    UREG("GetDC", GetDC_msabi);
    UREG("ReleaseDC", ReleaseDC_msabi);
    UREG("GetWindowDC", GetWindowDC_msabi);

    /* Window Text */
    UREG("SetWindowTextA", SetWindowTextA_msabi);
    UREG("SetWindowTextW", SetWindowTextW_msabi);
    UREG("GetWindowTextA", GetWindowTextA_msabi);
    UREG("GetWindowTextW", GetWindowTextW_msabi);
    UREG("GetWindowTextLengthA", GetWindowTextLengthA_msabi);

    /* Misc */
    UREG("MessageBoxA", MessageBoxA_msabi);
    UREG("MessageBoxW", MessageBoxW_msabi);
    UREG("SetTimer", SetTimer_msabi);
    UREG("KillTimer", KillTimer_msabi);
    UREG("AdjustWindowRect", AdjustWindowRect_msabi);
    UREG("AdjustWindowRectEx", AdjustWindowRectEx_msabi);
    UREG("GetWindowLongA", GetWindowLongA_msabi);
    UREG("SetWindowLongPtrA", SetWindowLongPtrA_msabi);
    UREG("GetWindowLongPtrA", GetWindowLongPtrA_msabi);
    UREG("SetWindowRgn", SetWindowRgn_msabi);
    UREG("GetAsyncKeyState", GetAsyncKeyState_msabi);
    UREG("GetCursorPos", GetCursorPos_msabi);
    UREG("WindowFromPoint", WindowFromPoint_msabi);
    UREG("GetSystemMetrics", GetSystemMetrics_msabi);
    UREG("GetKeyState", GetKeyState_msabi);
    UREG("EnableWindow", EnableWindow_msabi);
    UREG("BringWindowToTop", BringWindowToTop_msabi);
    UREG("ScreenToClient", ScreenToClient_msabi);
    UREG("ClientToScreen", ClientToScreen_msabi);

    DbgPrint("EXE: user32.dll exports registered (%lu total)\n", g_ExportCount);
}
