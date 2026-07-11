/*
 * MinNT - win32k/win32k.h
 * Win32k.sys internal header - clean version
 */

#ifndef _WIN32K_H_
#define _WIN32K_H_

#include <nt/ntdef.h>
#include <nt/ke.h>
#include <nt/io.h>
#include <nt/mm.h>
#include <nt/ex.h>

#define APIENTRY NTAPI

typedef ULONG_PTR HWINSTA;
typedef ULONG_PTR HDESK;
typedef ULONG_PTR HSEMAPHORE;
typedef ULONG_PTR HPALETTE;
typedef ULONG_PTR HRGN;
typedef ULONG_PTR HDC;
typedef ULONG_PTR HBITMAP;
typedef LONG_PTR LRESULT;

typedef struct _W32K_POINT {
    LONG x;
    LONG y;
} W32K_POINT, *PW32K_POINT;

#define POINTL W32K_POINT

typedef struct _W32K_RECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} W32K_RECT, *PW32K_RECT;

typedef struct _W32K_SIZE {
    LONG cx;
    LONG cy;
} W32K_SIZE, *PW32K_SIZE;

typedef struct _W32K_TEXTMETRIC {
    LONG tmHeight;
    LONG tmAscent;
    LONG tmDescent;
    LONG tmInternalLeading;
    LONG tmExternalLeading;
    LONG tmAveCharWidth;
    LONG tmMaxCharWidth;
    ULONG tmWeight;
    LONG tmOverhang;
    LONG tmDigitizedAspectX;
    LONG tmDigitizedAspectY;
    ULONG tmFirstChar;
    ULONG tmLastChar;
    ULONG tmDefaultChar;
    ULONG tmBreakChar;
    ULONG tmCharSet;
    ULONG tmItalic;
    ULONG tmUnderlined;
    ULONG tmStrikeOut;
    ULONG tmPitchAndFamily;
} W32K_TEXTMETRIC, *PW32K_TEXTMETRIC;

#define OPAQUE 2
#define TRANSPARENT 1

#define R2_COPYPEN 13
#define R2_XORPEN 7
#define PATCOPY 0x00F00021
#define BLACKNESS 0x00000042
#define WHITENESS 0x000000FF

#define DC_FLAG_MEMORY      0x00000001
#define DC_FLAG_SCREEN     0x00000002
#define DC_FLAG_DIRTY_RASTER_CAPS 0x00008000

typedef ULONG FLONG;

/* ---- USER (window/message) types — Phase 2 message pump --------------- */

typedef ULONG_PTR HWND;

typedef struct _W32K_MSG {
    HWND      hwnd;
    ULONG     message;
    ULONG_PTR wParam;
    LONG_PTR  lParam;
    ULONG     time;
    W32K_POINT pt;
} W32K_MSG, *PW32K_MSG;

typedef LONG_PTR (NTAPI *W32K_WNDPROC)(HWND hwnd, ULONG msg, ULONG_PTR wParam, LONG_PTR lParam);

/* Forward declarations for USER functions in gdikernel.c / usermsg.c */
NTSTATUS APIENTRY UserGetDC(ULONG_PTR hWnd);
NTSTATUS APIENTRY UserReleaseDC(ULONG_PTR hWnd, ULONG_PTR hdc);
NTSTATUS APIENTRY UserPostMessage(ULONG_PTR hWnd, ULONG Msg, ULONG_PTR wParam, LONG_PTR lParam);
NTSTATUS Win32kRegisterWindowProc(HWND hwnd, W32K_WNDPROC wndproc);
typedef struct _W32K_WNDCLASS W32K_WNDCLASS, *PW32K_WNDCLASS;
NTSTATUS APIENTRY UserRegisterClassEx(ULONG_PTR hWndParent, PW32K_WNDCLASS pWndClass);
NTSTATUS APIENTRY UserCreateWindowEx(ULONG dwExStyle, ULONG_PTR plstrClassName,
                                     ULONG_PTR plstrClsVersion, ULONG_PTR plstrWindowName,
                                     ULONG dwStyle, LONG x, LONG y, LONG nWidth, LONG nHeight,
                                     ULONG_PTR hWndParent, ULONG_PTR hMenu, ULONG_PTR hInstance,
                                     ULONG_PTR lpParam, ULONG dwFlags, ULONG_PTR acbiBuffer);
NTSTATUS APIENTRY UserShowWindow(ULONG_PTR hWnd, LONG nCmdShow);
NTSTATUS APIENTRY UserSetForegroundWindow(ULONG_PTR hWnd);
NTSTATUS APIENTRY UserDestroyWindow(ULONG_PTR hWnd);
NTSTATUS APIENTRY UserSetThreadState(ULONG Routine, ULONG_PTR Flags);

/* PeekMessage remove flags (values match real Win32 for future user32.dll compat) */
#define PM_NOREMOVE     0x0000
#define PM_REMOVE       0x0001
#define PM_NOYIELD      0x0002

/* Only the messages Phase 2 actually needs. The full WM_* table belongs to
 * whichever future phase needs it (window mgmt, painting, etc). */
#define WM_NULL         0x0000
#define WM_QUIT         0x0012
#define WM_KEYDOWN      0x0100
#define WM_KEYUP        0x0101
#define WM_CHAR         0x0102
#define WM_SYSKEYDOWN   0x0104
#define WM_SYSKEYUP     0x0105
#define WM_SYSCHAR      0x0106

/* Virtual-key codes needed by UserTranslateMessage's VK -> char mapping */
#define VK_BACK         0x08
#define VK_TAB          0x09
#define VK_RETURN       0x0D
#define VK_ESCAPE       0x1B
#define VK_SPACE        0x20
#define VK_0            0x30
#define VK_9            0x39
#define VK_A            0x41
#define VK_Z            0x5A

/* ---- USER (window) types — Phase 3 window management ------------------- */

typedef ULONG_PTR HICON;
typedef ULONG_PTR HCURSOR;
typedef ULONG_PTR HBRUSH;
typedef ULONG_PTR HMENU;
#define HFONT ULONG_PTR

#define WM_CREATE           0x0001
#define WM_DESTROY          0x0002
#define WM_SIZE             0x0005
#define WM_MOVE             0x0003
#define WM_SETFOCUS         0x0007
#define WM_KILLFOCUS        0x0008
#define WM_ERASEBKGND       0x0014
#define WM_PAINT            0x000F
#define WM_CLOSE            0x0010
#define WM_LBUTTONDOWN      0x0201
#define WM_LBUTTONUP        0x0202
#define WM_MOUSEMOVE        0x0200
#define WM_MOUSEWHEEL       0x020A
#define WM_MOUSELEAVE       0x0203
#define WM_NCMOUSEMOVE      0x00A0

#define WS_VISIBLE          0x10000000
#define WS_CHILD            0x40000000
#define WS_POPUP            0x80000000
#define WS_OVERLAPPED       0x00000000
#define WS_CAPTION          0x00C00000
#define WS_SYSMENU          0x00080000
#define WS_THICKFRAME       0x00040000
#define WS_MINIMIZE         0x20000000
#define WS_MAXIMIZE         0x00010000
#define WS_DISABLED         0x08000000
#define WS_EX_TOPMOST       0x00000008
#define WS_EX_WINDOWEDGE    0x00000100

#define SW_HIDE             0
#define SW_SHOW             5
#define SW_MINIMIZE         6
#define SW_MAXIMIZE         3
#define SW_RESTORE          9

#define SWP_NOSIZE           0x0001
#define SWP_NOMOVE           0x0002
#define SWP_NOZORDER         0x0004
#define SWP_NOACTIVATE       0x0010
#define SWP_SHOWWINDOW       0x0040
#define SWP_HIDEWINDOW       0x0080

#define GCL_HBRBACKGROUND   -10
#define GCL_HCURSOR         -12
#define GCL_HICON           -14
#define GCL_WNDPROC         -24

#define CW_USEDEFAULT        0x80000000

typedef struct _W32K_WNDCLASS {
    ULONG     style;
    W32K_WNDPROC lpfnWndProc;
    INT       cbClsExtra;
    INT       cbWndExtra;
    ULONG_PTR hInstance;
    HICON     hIcon;
    HCURSOR   hCursor;
    HBRUSH    hbrBackground;
    PCWSTR    lpszMenuName;
    PCWSTR    lpszClassName;
} W32K_WNDCLASS, *PW32K_WNDCLASS;

typedef struct _WINDOW {
    ULONG_PTR hwnd;
    ULONG     dwStyle;
    ULONG     dwExStyle;
    LONG      x;
    LONG      y;
    LONG      cx;
    LONG      cy;
    ULONG_PTR hwndParent;
    ULONG_PTR hInstance;
    W32K_WNDPROC lpfnWndProc;
    BOOLEAN   visible;
    BOOLEAN   dirty;
    BOOLEAN   erasebg;
} WINDOW, *PWINDOW;

typedef struct _W32K_CLASS_ENTRY {
    W32K_WNDPROC lpfnWndProc;
    HBRUSH    hbrBackground;
    ULONG_PTR hInstance;
    WCHAR     szClassName[64];
    BOOLEAN   inuse;
} W32K_CLASS_ENTRY;

/* ---- USER (thread/paint) types — Phase 4 ------------------------------- */

#define THREADSTATE_GETTHREADINFO       0x00
#define THREADSTATE_FOCUSWINDOW         0x01
#define THREADSTATE_CAPTUREWINDOW       0x02
#define THREADSTATE_ACTIVEWINDOW        0x03
#define THREADSTATE_GETCURSOR           0x04
#define THREADSTATE_GETMESSAGETIME     0x05
#define THREADSTATE_INSENDMESSAGE      0x06
#define THREADSTATE_GETINPUTSTATE       0x07
#define THREADSTATE_FOREGROUNDTHREAD   0x08
#define THREADSTATE_GETMESSAGEEXTRAINFO 0x09
#define THREADSTATE_DEFAULTIMEWINDOW   0x0A
#define THREADSTATE_TASKMANWINDOW       0x0B
#define THREADSTATE_PROGMANWINDOW      0x0C
#define THREADSTATE_CHANGEBITS          0x0D
#define THREADSTATE_UPTIMELASTREAD     0x0E
#define THREADSTATE_CHECKCONIME        0x0F

#define THREAD_STATE_RUNNING            0x00000001
#define THREAD_STATE_BLOCKED            0x00000002
#define THREAD_STATE_IDLE               0x00000004
#define THREAD_STATE_TERMINATED         0x00000008
#define THREAD_STATE_READY              0x00000010

#define ISMEX_NOSEND    0
#define ISMEX_SEND      1
#define ISMEX_CALLBACK  2
#define ISMEX_NOTIFY    3
#define ISMEX_REPLIED   0x80

typedef struct _W32K_PAINTSTRUCT {
    HDC      hdc;
    BOOL     fErase;
    LONG     rcPaint_left;
    LONG     rcPaint_top;
    LONG     rcPaint_right;
    LONG     rcPaint_bottom;
    BOOL     fRestore;
    BOOL     fIncUpdate;
    ULONG    rgbReserved[7];
} W32K_PAINTSTRUCT, *PW32K_PAINTSTRUCT;

typedef struct _WIN32K_GLOBALS {
    HANDLE        hiShellDevice;
    HANDLE        ahsysMem[559];
} WIN32K_GLOBALS;

#endif /* _WIN32K_H_ */