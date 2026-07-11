/*
 * MinNT - win32k/ntuser.h
 * NT User interface types and declarations
 *
 * These mirror the ReactOS win32ss user/ntuser types.
 */

#ifndef _NTUSER_H_
#define _NTUSER_H_

#include <nt/ntdef.h>

#define APIENTRY __attribute__((stdcall))

typedef ULONG_PTR HWND;
typedef ULONG_PTR HMODULE;
typedef ULONG_PTR HINSTANCE;
typedef ULONG_PTR HICON;
typedef ULONG_PTR HCURSOR;
typedef ULONG_PTR HKL;
typedef ULONG_PTR HWINSTA;
typedef ULONG_PTR HDESK;
typedef ULONG_PTR HMENU;
typedef ULONG_PTR HPALETTE;
typedef ULONG_PTR HRGN;
typedef ULONG_PTR HBITMAP;
typedef ULONG_PTR HEMF;
typedef ULONG_PTR HENHMETAFILE;
typedef ULONG_PTR HCOLORSPACE;
typedef ULONG_PTR HPEN;
typedef ULONG_PTR HBRUSH;
typedef ULONG_PTR HFONT;
typedef ULONG_PTR HRGN;
typedef ULONG_PTR HDWP;
typedef ULONG_PTR HAMC;
typedef ULONG_PTR HHOOK;
typedef ULONG_PTR HANDLE;

#define GWL_WNDPROC         (-4)
#define GWL_HINSTANCE       (-6)
#define GWL_HWNDPARENT      (-8)
#define GWL_STYLE           (-16)
#define GWL_EXSTYLE         (-20)
#define GWL_USERDATA        (-21)
#define GWL_ID              (-12)

#define WM_NULL                        0x0000
#define WM_CREATE                      0x0001
#define WM_DESTROY                     0x0002
#define WM_MOVE                        0x0003
#define WM_SIZE                        0x0005
#define WM_ACTIVATE                    0x0006
#define WM_SETFOCUS                    0x0007
#define WM_KILLFOCUS                   0x0008
#define WM_ENABLE                      0x000A
#define WM_SETREDRAW                   0x000B
#define WM_SETTEXT                     0x000C
#define WM_GETTEXT                     0x000D
#define WM_GETTEXTLENGTH               0x000E
#define WM_PAINT                       0x000F
#define WM_CLOSE                       0x0010
#define WM_QUIT                        0x0012
#define WM_QUERYENDSESSION             0x0011
#define WM_ERASEBKGND                  0x0014
#define WM_SYSCOLORCHANGE              0x0015
#define WM_SHOWWINDOW                  0x0018
#define WM_CTLCOLOR                    0x0019
#define WM_WININICHANGE                0x001A
#define WM_SETTINGCHANGE               0x001A
#define WM_DEVMODECHANGE               0x001B
#define WM_ACTIVATEAPP                 0x001C
#define WM_FONTCHANGE                  0x001D
#define WM_TIMECHANGE                  0x001E
#define WM_CANCELMODE                  0x001F
#define WM_SETCURSOR                   0x0020
#define WM_MOUSEACTIVATE               0x0021
#define WM_CHILDACTIVATE               0x0022
#define WM_QUEUESYNC                   0x0023
#define WM_GETMINMAXINFO               0x0024
#define WM_PAINTICON                   0x0026
#define WM_ICONERASEBKGND              0x0027
#define WM_NEXTDLGCTL                  0x0028
#define WM_SPOOLERSTATUS               0x002A
#define WM_DRAWITEM                    0x002B
#define WM_MEASUREITEM                0x002C
#define WM_DELETEITEM                  0x002D
#define WM_VKEYTOITEM                  0x002E
#define WM_CHARTOITEM                  0x002F
#define WM_SETFONT                     0x0030
#define WM_GETFONT                     0x0031
#define WM_SETHOTKEY                   0x0032
#define WM_GETHOTKEY                   0x0033
#define WM_QUERYDRAGICON               0x0037
#define WM_DROPFILES                   0x0038
#define WM_QUERYOPEN                   0x0039
#define WM_ENDSESSION                  0x0010
#define WM_QUIT                        0x0012
#define WM_ERASEBKGND                  0x0014

#define VK_LBUTTON        0x01
#define VK_RBUTTON        0x02
#define VK_CANCEL         0x03
#define VK_MBUTTON        0x04
#define VK_BACK           0x08
#define VK_TAB            0x09
#define VK_CLEAR          0x0C
#define VK_RETURN         0x0D
#define VK_SHIFT          0x10
#define VK_CONTROL        0x11
#define VK_MENU           0x12
#define VK_PAUSE          0x13
#define VK_CAPITAL        0x14
#define VK_KANA           0x15
#define VK_HANGEUL        0x15
#define VK_HANGUL         0x15
#define VK_JUNJA          0x17
#define VK_FINAL          0x18
#define VK_HANJA          0x19
#define VK_KANJI          0x19
#define VK_ESCAPE         0x1B
#define VK_CONVERT        0x1C
#define VK_NONCONVERT     0x1D
#define VK_ACCEPT         0x1E
#define VK_MODECHANGE     0x1F
#define VK_SPACE          0x20
#define VK_PRIOR          0x21
#define VK_NEXT           0x22
#define VK_END            0x23
#define VK_HOME           0x24
#define VK_LEFT           0x25
#define VK_UP             0x26
#define VK_RIGHT          0x27
#define VK_DOWN           0x28
#define VK_SELECT         0x29
#define VK_PRINT          0x2A
#define VK_EXECUTE        0x2B
#define VK_SNAPSHOT       0x2C
#define VK_INSERT         0x2D
#define VK_DELETE         0x2E
#define VK_HELP          0x2F
#define VK_0             0x30
#define VK_1             0x31
#define VK_2             0x32
#define VK_3             0x33
#define VK_4             0x34
#define VK_5             0x35
#define VK_6             0x36
#define VK_7             0x37
#define VK_8             0x38
#define VK_9             0x39
#define VK_A             0x41
#define VK_B             0x42
#define VK_C             0x43
#define VK_D             0x44
#define VK_E             0x45
#define VK_F             0x46
#define VK_G             0x47
#define VK_H             0x48
#define VK_I             0x49
#define VK_J             0x4A
#define VK_K             0x4B
#define VK_L             0x4C
#define VK_M             0x4D
#define VK_N             0x4E
#define VK_O             0x4F
#define VK_P             0x50
#define VK_Q             0x51
#define VK_R             0x52
#define VK_S             0x53
#define VK_T             0x54
#define VK_U             0x55
#define VK_V             0x56
#define VK_W             0x57
#define VK_X             0x58
#define VK_Y             0x59
#define VK_Z             0x5A

#define MB_OK                       0x00000000
#define MB_OKCANCEL                 0x00000001
#define MB_ABORTRETRYIGNORE         0x00000002
#define MB_YESNOCANCEL              0x00000003
#define MB_YESNO                     0x00000004
#define MB_RETRYCANCEL              0x00000005
#define MB_CANCELTRYCONTINUE        0x00000006
#define MB_ICONHAND                 0x00000010
#define MB_ICONQUESTION             0x00000020
#define MB_ICONASTERISK             0x00000040
#define MB_ICONWARNING             0x00000030
#define MB_ICONERROR               0x00000010
#define MB_ICONINFORMATION         0x00000040

#define CW_USEDEFAULT       0x80000000

#define WS_OVERLAPPED       0x00000000
#define WS_POPUP            0x80000000
#define WS_CHILD            0x40000000
#define WS_MINIMIZE         0x20000000
#define WS_VISIBLE          0x10000000
#define WS_DISABLED         0x08000000
#define WS_CLIPSIBLINGS     0x04000000
#define WS_CLIPCHILDREN     0x02000000
#define WS_MAXIMIZE         0x01000000
#define WS_CAPTION          0x00C00000
#define WS_BORDER           0x00800000
#define WS_DLGFRAME         0x00400000
#define WS_VSCROLL          0x00200000
#define WS_HSCROLL          0x00100000
#define WS_SYSMENU          0x00080000
#define WS_THICKFRAME       0x00040000
#define WS_GROUP            0x00020000
#define WS_TABSTOP          0x00010000
#define WS_MINIMIZEBOX      0x00020000
#define WS_MAXIMIZEBOX      0x00010000

#define SWP_NOSIZE          0x0001
#define SWP_NOMOVE          0x0002
#define SWP_NOZORDER        0x0004
#define SWP_NOREDRAW       0x0008
#define SWP_NOACTIVATE     0x0010
#define SWP_FRAMECHANGED   0x0020
#define SWP_SHOWWINDOW     0x0040
#define SWP_HIDEWINDOW     0x0080
#define SWP_NOCOPYBITS     0x0100
#define SWP_NOOWNERZORDER  0x0200
#define SWP_NOSENDCHANGING 0x0400

#define VK_LEFT  0x25
#define VK_UP    0x26
#define VK_RIGHT 0x27
#define VK_DOWN  0x28

typedef struct _MSG {
    HWND hwnd;
    ULONG message;
    ULONG_PTR wParam;
    LONG_PTR lParam;
    ULONG time;
    POINT pt;
} MSG, *PMSG;

typedef struct _WNDCLASS {
    ULONG style;
    ULONG_PTR lpfnWndProc;
    LONG cbClsExtra;
    LONG cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPWSTR lpszMenuName;
    LPWSTR lpszClassName;
} WNDCLASSW, *PWNDCLASSW;

typedef struct _RECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *PRECT;

typedef struct _POINT {
    LONG x;
    LONG y;
} POINT, *PPOINT;

typedef struct _PAINTSTRUCT {
    HDC hdc;
    ULONG fErase;
    RECT rcPaint;
    ULONG fRestore;
    ULONG fIncUpdate;
    BYTE rgbReserved[32];
} PAINTSTRUCT, *PPAINTSTRUCT;

typedef struct _CREATESTRUCTW {
    HINSTANCE hInstance;
    HINSTANCE hPrevInstance;
    LPWSTR lpszClassName;
    LPWSTR lpszMenuName;
    ULONG dwStyle;
    LONG x;
    LONG y;
    LONG cx;
    LONG cy;
    HWND hwndParent;
    HMENU hMenu;
    HDC hdc;
    PVOID lpCreateParams;
} CREATESTRUCTW, *PCREATESTRUCTW;

typedef struct _WINDOWPLACEMENT {
    ULONG length;
    ULONG flags;
    ULONG showCmd;
    POINT ptMinPosition;
    POINT ptMaxPosition;
    RECT rcNormalPosition;
} WINDOWPLACEMENT, *PWINDOWPLACEMENT;

#define PM_REMOVE              0x0001
#define PM_NOYIELD              0x0002
#define PM_NOREMOVE             0x0000

NTSTATUS APIENTRY NtUserGetThreadState(ULONG_PTR Arg1);
NTSTATUS APIENTRY NtUserPeekMessage(PMSG Msg, HWND hWnd, ULONG MsgFilterMin, ULONG MsgFilterMax, ULONG RemoveMsg);
NTSTATUS APIENTRY NtUserGetMessage(PMSG Msg, HWND hWnd, ULONG MsgFilterMin, ULONG MsgFilterMax);
NTSTATUS APIENTRY NtUserDispatchMessage(PMSG Msg);
NTSTATUS APIENTRY NtUserPostMessage(HWND hWnd, ULONG Msg, ULONG_PTR wParam, LONG_PTR lParam);
NTSTATUS APIENTRY NtUserTranslateMessage(PMSG Msg, ULONG Flags);
NTSTATUS APIENTRY NtUserCreateWindowEx(ULONG dwExStyle, LPWSTR lpszClassName, LPWSTR lpszWindowName,
                                       ULONG dwStyle, LONG x, LONG y, LONG cx, LONG cy,
                                       HWND hwndParent, HMENU hMenu, HINSTANCE hInstance,
                                       PVOID lpCreateParams, ULONG Param);
NTSTATUS APIENTRY NtUserDestroyWindow(HWND hWnd);
NTSTATUS APIENTRY NtUserShowWindow(HWND hWnd, LONG nCmdShow);
NTSTATUS APIENTRY NtUserSetWindowPos(HWND hWnd, HWND hWndInsertAfter, LONG x, LONG y, LONG cx, LONG cy, ULONG Flags);
NTSTATUS APIENTRY NtUserGetDC(HWND hWnd);
NTSTATUS APIENTRY NtUserReleaseDC(HDC hDC);
NTSTATUS APIENTRY NtUserDefWindowProc(HWND hWnd, ULONG Msg, ULONG_PTR wParam, LONG_PTR lParam);
NTSTATUS APIENTRY NtUserBeginPaint(HWND hWnd, PPAINTSTRUCT PaintStruct);
NTSTATUS APIENTRY NtUserEndPaint(HWND hWnd, PPAINTSTRUCT PaintStruct);
NTSTATUS APIENTRY NtUserGetAsyncKeyState(ULONG Key);
NTSTATUS APIENTRY NtUserSetFocus(HWND hWnd);
NTSTATUS APIENTRY NtUserInvalidateRect(HWND hWnd, PRECT Rect, ULONG Erase);
NTSTATUS APIENTRY NtUserGetForegroundWindow(VOID);
NTSTATUS APIENTRY NtUserSetForegroundWindow(HWND hWnd);
NTSTATUS APIENTRY NtUserSetCapture(HWND hWnd);
NTSTATUS APIENTRY NtUserReleaseCapture(VOID);
NTSTATUS APIENTRY NtUserGetWindowRect(HWND hWnd, PRECT Rect);
NTSTATUS APIENTRY NtUserGetClientRect(HWND hWnd, PRECT Rect);
NTSTATUS APIENTRY NtUserRegisterClassEx(PWNDCLASSW Class, ULONG Version);

#endif /* _NTUSER_H_ */