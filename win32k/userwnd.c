/*
 * MinNT - win32k/userwnd.c
 * Win32k USER window management implementations
 *
 * Phase 3: UserRegisterClassEx, UserCreateWindowEx, UserDestroyWindow,
 *          UserShowWindow, UserSetWindowPos
 *
 * Phase 4: UserGetThreadState, UserBeginPaint, UserEndPaint
 *
 * Phase 5: UserInvalidateRect, UserGetForegroundWindow, UserSetForegroundWindow
 *
 * Phase 6: UserSetCapture, UserSetFocus, UserGetAsyncKeyState
 *
 * Phase 7: UserGetWindowRect, UserGetClientRect (GetRect family)
 *
 * Design: Class registry (64-slot fixed table), WINDOW objects as heap-
 * allocated struct pointers (HWND = WINDOW*), matching the GDI handle
 * convention established in gdikernel.c. Window procedure is stored
 * directly on the WINDOW struct for O(1) DispatchMessage lookup.
 */

#include "win32k.h"
#include <nt/ps.h>

#define MAX_CLASSES 64

static W32K_CLASS_ENTRY g_ClassTable[MAX_CLASSES];
static KSPIN_LOCK g_ClassSpinLock;
static ULONG_PTR g_hwndCapture = 0;
static ULONG_PTR g_hwndFocus = 0;
static ULONG_PTR g_hwndForeground = 0;

VOID NTAPI Win32kInitClassTable(VOID)
{
    KIRQL Irql;
    int i;
    KeInitializeSpinLock(&g_ClassSpinLock);
    Irql = KeAcquireSpinLockRaiseToDpc(&g_ClassSpinLock);
    for (i = 0; i < MAX_CLASSES; i++)
        g_ClassTable[i].inuse = FALSE;
    KeReleaseSpinLock(&g_ClassSpinLock, Irql);
    DbgPrint("WIN32K: Class table initialized (%d slots)\n", MAX_CLASSES);
}

static W32K_CLASS_ENTRY *FindClassByName(PCWSTR lpClassName)
{
    KIRQL Irql;
    int i;
    UNICODE_STRING us, usClass;
    if (!lpClassName) return NULL;
    Irql = KeAcquireSpinLockRaiseToDpc(&g_ClassSpinLock);
    for (i = 0; i < MAX_CLASSES; i++) {
        if (g_ClassTable[i].inuse) {
            us.Buffer = g_ClassTable[i].szClassName;
            us.Length = 0;
            us.MaximumLength = sizeof(g_ClassTable[i].szClassName);
            usClass.Buffer = (PWSTR)lpClassName;
            usClass.Length = 0;
            usClass.MaximumLength = 0;
            if (RtlEqualUnicodeString(&us, &usClass, TRUE)) {
                KeReleaseSpinLock(&g_ClassSpinLock, Irql);
                return &g_ClassTable[i];
            }
        }
    }
    KeReleaseSpinLock(&g_ClassSpinLock, Irql);
    return NULL;
}

NTSTATUS APIENTRY
UserRegisterClassEx(ULONG_PTR hWndParent, PW32K_WNDCLASS pWndClass)
{
    KIRQL Irql;
    INT i, slot = -1;
    PW32K_WNDCLASS pCls;
    W32K_CLASS_ENTRY *pEntry;
    PCWSTR name;
    ULONG nameLen;

    if (!pWndClass) return STATUS_INVALID_PARAMETER;
    name = pWndClass->lpszClassName;
    if (!name) return STATUS_INVALID_PARAMETER;

    pEntry = FindClassByName(name);
    if (pEntry) {
        DbgPrint("WIN32K: UserRegisterClassEx class '%ws' already registered\n", name);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    slot = -1;
    Irql = KeAcquireSpinLockRaiseToDpc(&g_ClassSpinLock);
    for (i = 0; i < MAX_CLASSES; i++) {
        if (!g_ClassTable[i].inuse) { slot = i; break; }
    }
    KeReleaseSpinLock(&g_ClassSpinLock, Irql);

    if (slot == -1) {
        DbgPrint("WIN32K: UserRegisterClassEx full (%d classes)\n", MAX_CLASSES);
        return STATUS_NO_MEMORY;
    }

    pCls = (PW32K_WNDCLASS)pWndClass;
    pEntry = &g_ClassTable[slot];

    nameLen = 0;
    while (nameLen < 63 && name[nameLen]) nameLen++;
    RtlCopyMemory(pEntry->szClassName, name, nameLen * sizeof(WCHAR));
    pEntry->szClassName[nameLen] = 0;
    pEntry->lpfnWndProc = pCls->lpfnWndProc;
    pEntry->hbrBackground = pCls->hbrBackground;
    pEntry->hInstance = pCls->hInstance;
    pEntry->inuse = TRUE;

    DbgPrint("WIN32K: UserRegisterClassEx '%ws' -> slot %d\n", name, slot);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
UserCreateWindowEx(ULONG dwExStyle, ULONG_PTR plstrClassName, ULONG_PTR plstrClsVersion,
                   ULONG_PTR plstrWindowName, ULONG dwStyle, LONG x, LONG y,
                   LONG nWidth, LONG nHeight, ULONG_PTR hWndParent, ULONG_PTR hMenu,
                   ULONG_PTR hInstance, ULONG_PTR lpParam, ULONG dwFlags,
                   ULONG_PTR acbiBuffer)
{
    WINDOW *pWnd;
    W32K_CLASS_ENTRY *pClass;
    PCWSTR lpszClassName;
    W32K_MSG msg;
    LONG_PTR wndResult = 0;

    lpszClassName = (PCWSTR)plstrClassName;
    if (!lpszClassName) return STATUS_INVALID_PARAMETER;

    pClass = FindClassByName(lpszClassName);
    if (!pClass) {
        DbgPrint("WIN32K: UserCreateWindowEx class '%ws' not found\n", lpszClassName);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    pWnd = ExAllocatePool(NonPagedPool, sizeof(WINDOW));
    if (!pWnd) return STATUS_NO_MEMORY;

    RtlZeroMemory(pWnd, sizeof(WINDOW));
    pWnd->dwStyle = dwStyle;
    pWnd->dwExStyle = dwExStyle;
    pWnd->x = (x == CW_USEDEFAULT) ? 0 : x;
    pWnd->y = (y == CW_USEDEFAULT) ? 0 : y;
    pWnd->cx = (nWidth == CW_USEDEFAULT) ? 1024 : nWidth;
    pWnd->cy = (nHeight == CW_USEDEFAULT) ? 768 : nHeight;
    pWnd->hwndParent = hWndParent;
    pWnd->hInstance = hInstance;
    pWnd->lpfnWndProc = pClass->lpfnWndProc;
    pWnd->pClass = pClass;        /* link to class entry for class-wide data */
    pWnd->ThreadId = (ULONG_PTR)PsGetCurrentThreadId();
    pWnd->ProcessId = (ULONG_PTR)PsGetCurrentProcessId();
    pWnd->Unicode = TRUE;
    pWnd->showCmd = SW_SHOW;
    pWnd->visible = (dwStyle & WS_VISIBLE) ? TRUE : FALSE;

    if (hWndParent == 0 && (dwStyle & (WS_CHILD | WS_POPUP)) == 0)
        pWnd->visible = TRUE;

    DbgPrint("WIN32K: UserCreateWindowEx '%ws' style=0x%X visible=%d\n",
             lpszClassName, dwStyle, pWnd->visible);

    RtlZeroMemory(&msg, sizeof(W32K_MSG));
    msg.hwnd = (HWND)pWnd;
    msg.message = WM_CREATE;
    msg.wParam = 0;
    msg.lParam = (LONG_PTR)lpParam;
    msg.time = (ULONG)KeTickCount;

    if (pClass->lpfnWndProc) {
        wndResult = pClass->lpfnWndProc((HWND)pWnd, WM_CREATE, 0, (LONG_PTR)lpParam);
    }

    Win32kRegisterWindowProc((HWND)pWnd, pWnd->lpfnWndProc);

    return (NTSTATUS)pWnd;
}

NTSTATUS APIENTRY
UserShowWindow(ULONG_PTR hWnd, LONG nCmdShow)
{
    WINDOW *pWnd = (WINDOW *)hWnd;

    if (!pWnd || (ULONG_PTR)pWnd < 0x1000) return STATUS_INVALID_HANDLE;

    if (nCmdShow == SW_HIDE) {
        pWnd->visible = FALSE;
    } else {
        pWnd->visible = TRUE;
    }

    DbgPrint("WIN32K: UserShowWindow(%p, SW_%d) visible=%d\n",
             hWnd, nCmdShow, pWnd->visible);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
UserSetWindowPos(ULONG_PTR hWnd, ULONG_PTR hWndInsertAfter, LONG x, LONG y,
                 LONG cx, LONG cy, ULONG fFlags)
{
    WINDOW *pWnd = (WINDOW *)hWnd;

    if (!pWnd || (ULONG_PTR)pWnd < 0x1000) return STATUS_INVALID_HANDLE;

    if (!(fFlags & SWP_NOMOVE)) pWnd->x = x;
    if (!(fFlags & SWP_NOSIZE)) { pWnd->cx = cx; pWnd->cy = cy; }

    DbgPrint("WIN32K: UserSetWindowPos(%p, flags=0x%X) pos=%d,%d size=%dx%d\n",
             hWnd, fFlags, pWnd->x, pWnd->y, pWnd->cx, pWnd->cy);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
UserDestroyWindow(ULONG_PTR hWnd)
{
    WINDOW *pWnd = (WINDOW *)hWnd;
    W32K_MSG msg;
    LONG_PTR result;

    if (!pWnd || (ULONG_PTR)pWnd < 0x1000) return STATUS_INVALID_HANDLE;

    DbgPrint("WIN32K: UserDestroyWindow(%p)\n", hWnd);

    RtlZeroMemory(&msg, sizeof(W32K_MSG));
    msg.hwnd = (HWND)pWnd;
    msg.message = WM_DESTROY;
    msg.wParam = 0;
    msg.lParam = 0;
    msg.time = (ULONG)KeTickCount;

    if (pWnd->lpfnWndProc) {
        volatile LONG_PTR r = pWnd->lpfnWndProc((HWND)pWnd, WM_DESTROY, 0, 0);
        (void)r;
    }

    ExFreePool(pWnd);
    return STATUS_SUCCESS;
}

ULONG_PTR NTAPI
UserGetThreadState(ULONG Routine)
{
    ULONG_PTR ret = 0;

    switch (Routine) {
        case THREADSTATE_GETTHREADINFO:
            ret = TRUE;
            break;
        case THREADSTATE_FOCUSWINDOW:
            ret = (ULONG_PTR)g_hwndFocus;
            break;
        case THREADSTATE_CAPTUREWINDOW:
            ret = (ULONG_PTR)g_hwndCapture;
            break;
        case THREADSTATE_ACTIVEWINDOW:
            ret = (ULONG_PTR)g_hwndForeground;
            break;
        case THREADSTATE_GETCURSOR:
            ret = 0;
            break;
        case THREADSTATE_GETMESSAGETIME:
            ret = (ULONG_PTR)KeTickCount;
            break;
        case THREADSTATE_INSENDMESSAGE:
            ret = ISMEX_NOSEND;
            break;
        case THREADSTATE_GETINPUTSTATE:
            ret = 0;
            break;
        case THREADSTATE_FOREGROUNDTHREAD:
            ret = (g_hwndForeground != 0) ? TRUE : FALSE;
            break;
        case THREADSTATE_TASKMANWINDOW:
            ret = 0;
            break;
        case THREADSTATE_PROGMANWINDOW:
            ret = 0;
            break;
        case THREADSTATE_GETMESSAGEEXTRAINFO:
            ret = 0;
            break;
        case THREADSTATE_DEFAULTIMEWINDOW:
            ret = 0;
            break;
        case THREADSTATE_CHANGEBITS:
            ret = 0;
            break;
        case THREADSTATE_UPTIMELASTREAD:
            break;
        case THREADSTATE_CHECKCONIME:
            ret = 0;
            break;
        default:
            break;
    }

    return ret;
}

NTSTATUS APIENTRY
UserSetThreadState(ULONG Routine, ULONG_PTR Flags)
{
    PETHREAD CurrentThread;
    ULONG oldState;

    CurrentThread = (PETHREAD)KeGetCurrentThread();

    switch (Routine) {
        case THREAD_STATE_RUNNING:
            oldState = CurrentThread->UserThreadState;
            CurrentThread->UserThreadState |= THREAD_STATE_RUNNING;
            CurrentThread->UserThreadState &= ~(THREAD_STATE_BLOCKED | THREAD_STATE_IDLE);
            DbgPrint("WIN32K: UserSetThreadState RUNNING tid=%llu old=0x%X new=0x%X\n",
                     (ULONG64)(ULONG_PTR)CurrentThread->UniqueThreadId,
                     oldState, CurrentThread->UserThreadState);
            break;
        case THREAD_STATE_BLOCKED:
            oldState = CurrentThread->UserThreadState;
            CurrentThread->UserThreadState |= THREAD_STATE_BLOCKED;
            CurrentThread->UserThreadState &= ~(THREAD_STATE_RUNNING | THREAD_STATE_IDLE);
            DbgPrint("WIN32K: UserSetThreadState BLOCKED tid=%llu old=0x%X new=0x%X\n",
                     (ULONG64)(ULONG_PTR)CurrentThread->UniqueThreadId,
                     oldState, CurrentThread->UserThreadState);
            break;
        case THREAD_STATE_IDLE:
            oldState = CurrentThread->UserThreadState;
            CurrentThread->UserThreadState |= THREAD_STATE_IDLE;
            CurrentThread->UserThreadState &= ~(THREAD_STATE_RUNNING | THREAD_STATE_BLOCKED);
            DbgPrint("WIN32K: UserSetThreadState IDLE tid=%llu old=0x%X new=0x%X\n",
                     (ULONG64)(ULONG_PTR)CurrentThread->UniqueThreadId,
                     oldState, CurrentThread->UserThreadState);
            break;
        case THREAD_STATE_TERMINATED:
            oldState = CurrentThread->UserThreadState;
            CurrentThread->UserThreadState = THREAD_STATE_TERMINATED;
            DbgPrint("WIN32K: UserSetThreadState TERMINATED tid=%llu old=0x%X new=0x%X\n",
                     (ULONG64)(ULONG_PTR)CurrentThread->UniqueThreadId,
                     oldState, CurrentThread->UserThreadState);
            break;
        case THREAD_STATE_READY:
            oldState = CurrentThread->UserThreadState;
            CurrentThread->UserThreadState |= THREAD_STATE_READY;
            CurrentThread->UserThreadState &= ~(THREAD_STATE_BLOCKED | THREAD_STATE_IDLE | THREAD_STATE_TERMINATED);
            DbgPrint("WIN32K: UserSetThreadState READY tid=%llu old=0x%X new=0x%X\n",
                     (ULONG64)(ULONG_PTR)CurrentThread->UniqueThreadId,
                     oldState, CurrentThread->UserThreadState);
            break;
        default:
            DbgPrint("WIN32K: UserSetThreadState unknown routine 0x%X\n", Routine);
            break;
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
UserBeginPaint(ULONG_PTR hWnd, PW32K_PAINTSTRUCT pPs)
{
    WINDOW *pWnd = (WINDOW *)hWnd;
    HDC hdc;

    if (!pWnd || (ULONG_PTR)pWnd < 0x1000) return STATUS_INVALID_HANDLE;
    if (!pPs) return STATUS_INVALID_PARAMETER;

    hdc = UserGetDC(hWnd);

    RtlZeroMemory(pPs, sizeof(W32K_PAINTSTRUCT));
    pPs->hdc = (HDC)hdc;
    pPs->fErase = FALSE;
    pPs->rcPaint_left = pWnd->x;
    pPs->rcPaint_top = pWnd->y;
    pPs->rcPaint_right = pWnd->x + pWnd->cx;
    pPs->rcPaint_bottom = pWnd->y + pWnd->cy;

    DbgPrint("WIN32K: UserBeginPaint(%p) hdc=%p rc=(%d,%d-%d,%d)\n",
             hWnd, hdc, pPs->rcPaint_left, pPs->rcPaint_top,
             pPs->rcPaint_right, pPs->rcPaint_bottom);
    return (NTSTATUS)hdc;
}

NTSTATUS APIENTRY
UserEndPaint(ULONG_PTR hWnd, PW32K_PAINTSTRUCT pPs)
{
    if (!pPs) return STATUS_INVALID_PARAMETER;

    DbgPrint("WIN32K: UserEndPaint(%p) hdc=%p\n", hWnd, pPs->hdc);
    UserReleaseDC(hWnd, (ULONG_PTR)pPs->hdc);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
UserInvalidateRect(ULONG_PTR hWnd, ULONG_PTR lpRect, BOOL bErase)
{
    WINDOW *pWnd = (WINDOW *)hWnd;

    if (!pWnd || (ULONG_PTR)pWnd < 0x1000) return STATUS_INVALID_HANDLE;

    if (bErase) {
        pWnd->erasebg = TRUE;
        UserPostMessage(hWnd, WM_ERASEBKGND, (ULONG_PTR)hWnd, 0);
    }

    pWnd->dirty = TRUE;
    UserPostMessage(hWnd, WM_PAINT, 0, 0);

    DbgPrint("WIN32K: UserInvalidateRect(%p, erase=%d) dirty=%d posted WM_PAINT\n",
             hWnd, bErase, pWnd->dirty);
    return STATUS_SUCCESS;
}

ULONG_PTR NTAPI
UserGetForegroundWindow(VOID)
{
    DbgPrint("WIN32K: UserGetForegroundWindow() -> %p\n", g_hwndForeground);
    return g_hwndForeground;
}

NTSTATUS APIENTRY
UserSetForegroundWindow(ULONG_PTR hWnd)
{
    WINDOW *pWnd = (WINDOW *)hWnd;

    if (hWnd != 0 && (!pWnd || (ULONG_PTR)pWnd < 0x1000))
        return STATUS_INVALID_HANDLE;

    g_hwndForeground = hWnd;
    DbgPrint("WIN32K: UserSetForegroundWindow(%p) -> old=%p\n",
             hWnd, g_hwndForeground);
    return STATUS_SUCCESS;
}

ULONG_PTR NTAPI
UserSetCapture(ULONG_PTR hWnd)
{
    ULONG_PTR old = g_hwndCapture;
    WINDOW *pWnd = (WINDOW *)hWnd;

    if (hWnd != 0 && (!pWnd || (ULONG_PTR)pWnd < 0x1000)) {
        g_hwndCapture = 0;
    } else {
        g_hwndCapture = hWnd;
    }

    DbgPrint("WIN32K: UserSetCapture(%p) -> old=%p\n", hWnd, old);
    return old;
}

ULONG_PTR NTAPI
UserSetFocus(ULONG_PTR hWnd)
{
    ULONG_PTR old = g_hwndFocus;
    WINDOW *pWnd = (WINDOW *)hWnd;

    if (hWnd != 0 && (!pWnd || (ULONG_PTR)pWnd < 0x1000)) {
        g_hwndFocus = 0;
    } else {
        g_hwndFocus = hWnd;
    }

    DbgPrint("WIN32K: UserSetFocus(%p) -> old=%p\n", hWnd, old);
    return old;
}

ULONG NTAPI
UserGetAsyncKeyState(INT vKey)
{
    ULONG ret = 0;

    if (vKey >= 0 && vKey < 256)
        ret = 0;

    DbgPrint("WIN32K: UserGetAsyncKeyState(vk=0x%X) -> 0x%X\n", vKey, ret);
    return ret;
}

BOOL NTAPI
UserGetWindowRect(ULONG_PTR hWnd, ULONG_PTR lpRect)
{
    WINDOW *pWnd = (WINDOW *)hWnd;
    W32K_RECT *pRect = (W32K_RECT *)lpRect;

    if (!pWnd || (ULONG_PTR)pWnd < 0x1000) return FALSE;
    if (!pRect) return FALSE;

    pRect->left = pWnd->x;
    pRect->top = pWnd->y;
    pRect->right = pWnd->x + pWnd->cx;
    pRect->bottom = pWnd->y + pWnd->cy;

    DbgPrint("WIN32K: UserGetWindowRect(%p) -> (%d,%d-%d,%d)\n",
             hWnd, pRect->left, pRect->top, pRect->right, pRect->bottom);
    return TRUE;
}

BOOL NTAPI
UserGetClientRect(ULONG_PTR hWnd, ULONG_PTR lpRect)
{
    WINDOW *pWnd = (WINDOW *)hWnd;
    W32K_RECT *pRect = (W32K_RECT *)lpRect;

    if (!pWnd || (ULONG_PTR)pWnd < 0x1000) return FALSE;
    if (!pRect) return FALSE;

    pRect->left = 0;
    pRect->top = 0;
    pRect->right = pWnd->cx;
    pRect->bottom = pWnd->cy;

    DbgPrint("WIN32K: UserGetClientRect(%p) -> (0,0-%d,%d)\n",
             hWnd, pRect->right, pRect->bottom);
    return TRUE;
}