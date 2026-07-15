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

/* Word extraction macros */
#ifndef LOWORD
#define LOWORD(l)   ((USHORT)((ULONG_PTR)(l) & 0xFFFF))
#endif
#ifndef HIWORD
#define HIWORD(l)   ((USHORT)((ULONG_PTR)(l) >> 16))
#endif
#ifndef MAKEINTRESOURCE
#define MAKEINTRESOURCE(i) ((ULONG_PTR)((USHORT)(i)))
#endif
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp)   ((LONG)(SHORT)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp)   ((LONG)(SHORT)HIWORD(lp))
#endif

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

/* ---- Additional type definitions for new modules --------------- */

#ifndef PLONG_PTR
typedef LONG_PTR *PLONG_PTR;
#endif

#ifndef PBOOL
typedef BOOL *PBOOL;
#endif

#ifndef PUINT
typedef ULONG *PUINT;
#endif

#ifndef UINT
typedef ULONG UINT;
#endif

#ifndef ATOM
typedef USHORT ATOM;
#endif

#ifndef PCSTR
typedef const char *PCSTR;
#endif

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

/* TranslateMessage flags (NtUserTranslateMessage Flags argument) */
#define TM_POSTCHARCHARS 0x00000001  /* post the synthesized WM_CHAR/WM_SYSCHAR */
#define TM_KEYDOWN       0x00000002  /* translate WM_KEYDOWN, not just WM_SYSKEYDOWN */

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
#define VK_SHIFT        0x10
#define VK_CONTROL      0x11
#define VK_MENU         0x12
#define VK_PAUSE        0x13
#define VK_CAPITAL      0x14
#define VK_ESCAPE       0x1B
#define VK_SPACE        0x20
#define VK_PRIOR        0x21
#define VK_NEXT         0x22
#define VK_END          0x23
#define VK_HOME         0x24
#define VK_LEFT         0x25
#define VK_UP           0x26
#define VK_RIGHT        0x27
#define VK_DOWN         0x28
#define VK_INSERT       0x2D
#define VK_DELETE        0x2E
#define VK_0            0x30
#define VK_9            0x39
#define VK_A            0x41
#define VK_Z            0x5A
#define VK_F1           0x70
#define VK_F2           0x71
#define VK_F3           0x72
#define VK_F4           0x73
#define VK_F5           0x74
#define VK_F6           0x75
#define VK_F7           0x76
#define VK_F8           0x77
#define VK_F9           0x78
#define VK_F10          0x79
#define VK_F11          0x7A
#define VK_F12          0x7B
#define VK_NUMLOCK      0x90
#define VK_SCROLL       0x91

/* ---- USER (window) types — Phase 3 window management ------------------- */

typedef ULONG_PTR HICON;
typedef ULONG_PTR HCURSOR;
typedef ULONG_PTR HBRUSH;
typedef ULONG_PTR HMENU;
#define HFONT ULONG_PTR

/* Pointer types for icons and cursors */
#ifndef PHICON
typedef HICON *PHICON;
#endif
#ifndef PHCURSOR
typedef HCURSOR *PHCURSOR;
#endif

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
#define WM_LBUTTONDBLCLK    0x0203
#define WM_RBUTTONDOWN      0x0204
#define WM_RBUTTONUP        0x0205
#define WM_RBUTTONDBLCLK    0x0206
#define WM_MBUTTONDOWN      0x0207
#define WM_MBUTTONUP        0x0208
#define WM_MBUTTONDBLCLK    0x0209
#define WM_TIMER            0x0113
#define WM_NCCREATE         0x0081
#define WM_NCCALCSIZE       0x0083
#define WM_NCPAINT          0x0085
#define WM_NCHITTEST        0x0084
#define WM_GETMINMAXINFO    0x0024
#define WM_QUERYENDSESSION  0x0011
#define WM_ENDSESSION       0x0016
#define WM_SYSCOMMAND       0x0112
#define WM_SETCURSOR        0x0020
#define WM_SETTEXT          0x000C
#define WM_GETTEXT          0x000D
#define WM_GETTEXTLENGTH    0x000E

/* RedrawWindow flags */
#define RDW_INVALIDATE      0x0001
#define RDW_ERASE           0x0004
#define RDW_ALLCHILDREN     0x0080
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
#define SWP_NOOWNER          0x0200
#define SWP_NOREDRAW         0x0008
#define SWP_FRAMECHANGED     0x0020

/* SystemParametersInfo actions */
#define SPI_GETNONCLIENTMETRICS    0x0029
#define SPI_SETNONCLIENTMETRICS    0x002A
#define SPI_GETWORKAREA            0x0030
#define SPI_SETWORKAREA            0x0031
#define SPI_GETICONTITLELOGFONT    0x001F
#define SPI_GETMENUANIMATION       0x1003
#define SPI_GETFONTSMOOTHING       0x004A
#define SPI_SETFONTSMOOTHING       0x004B
#define SPI_GETSCREENSAVETIMEOUT   0x000E
#define SPI_SETSCREENSAVETIMEOUT   0x000F
#define SPI_GETBEEP                0x0001
#define SPI_SETBEEP                0x0002
#define SPI_GETMOUSE               0x0003
#define SPI_SETMOUSE               0x0004
#define SPI_GETBORDER              0x0005
#define SPI_SETBORDER              0x0006
#define SPI_GETKEYBOARDSPEED       0x000A
#define SPI_SETKEYBOARDSPEED       0x000B
#define SPI_GETKEYBOARDDELAY       0x0016
#define SPI_SETKEYBOARDDELAY       0x0017
#define SPI_GETDOUBLECLKTIME       0x0020
#define SPI_SETDOUBLECLKTIME       0x0021
#define SPI_GETDRAGFULLWINDOWS     0x0026
#define SPI_SETDRAGFULLWINDOWS     0x0025
#define SPI_GETSHOWSOUNDS          0x0038
#define SPI_SETSHOWSOUNDS          0x0039
#define SPI_GETFASTTASKSWITCH      0x0023

/* Drag-drop effects */
#define DROPEFFECT_NONE    0
#define DROPEFFECT_COPY    1
#define DROPEFFECT_MOVE    2
#define DROPEFFECT_LINK    4
#define DROPEFFECT_SCROLL  0x80000000

/* ChildWindowFromPointEx flags */
#define CWP_ALL            0x0000
#define CWP_SKIPINVISIBLE  0x0001
#define CWP_SKIPDISABLED   0x0002
#define CWP_SKIPTRANSPARENT 0x0004

/* ScrollWindowEx flags */
#define SW_SCROLLCHILDREN  0x0001
#define SW_INVALIDATE      0x0002
#define SW_ERASE           0x0004
#define SW_SMOOTH_SCROLL   0x0010

/* MessageBox return values */
#define IDOK               1
#define IDCANCEL           2
#define IDABORT            3
#define IDRETRY            4
#define IDIGNORE           5
#define IDYES              6
#define IDNO               7

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

/* Forward declaration for W32K_CLASS_ENTRY (defined below) */
typedef struct _W32K_CLASS_ENTRY W32K_CLASS_ENTRY, *PW32K_CLASS_ENTRY;

/* Window properties */
#define MAX_PROPS 32
typedef struct _W32K_PROPERTY {
    ATOM    Atom;
    HANDLE  Value;
    BOOLEAN InUse;
} W32K_PROPERTY, *PW32K_PROPERTY;

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
    /* Extended fields */
    ULONG_PTR ThreadId;             /* owner thread ID */
    ULONG_PTR ProcessId;            /* owner process ID */
    BOOLEAN   Unicode;              /* created with RegisterClassW */
    WCHAR     Text[256];            /* window text (WM_SETTEXT/WM_GETTEXT) */
    W32K_PROPERTY Props[MAX_PROPS]; /* per-window property list */
    ULONG_PTR hwndChild;            /* first child window */
    ULONG_PTR hwndNext;             /* next sibling */
    LONG      Id;                   /* window ID (GWLP_ID) */
    LONG_PTR  UserData;             /* GWLP_USERDATA */
    ULONG     showCmd;              /* SW_SHOW/SW_HIDE/SW_MINIMIZE/SW_MAXIMIZE */
    W32K_RECT rcNormal;             /* normal (restored) position */
    PW32K_CLASS_ENTRY pClass;       /* class entry pointer */
} WINDOW, *PWINDOW;

/* Window placement structure */
typedef struct _W32K_WINDOWPLACEMENT {
    ULONG  length;
    ULONG  flags;
    ULONG  showCmd;
    W32K_RECT ptMinPosition;
    W32K_RECT ptMaxPosition;
    W32K_RECT rcNormalPosition;
} W32K_WINDOWPLACEMENT, *PW32K_WINDOWPLACEMENT;

/* ---- GDI internal types (shared across win32k) -------------------------- */

typedef struct _GDICOEFF {
    ULONG Type;
    PVOID UserPtr;
    LONG RefCount;
} GDICOEFF;

typedef struct _BRUSHOBJ {
    GDICOEFF Header;
    ULONG SolidColor;
} BRUSHOBJ;

typedef struct _PENOBJ {
    GDICOEFF Header;
    ULONG Style;
    ULONG Width;
    ULONG Color;
} PENOBJ;

typedef struct _RGNOBJ {
    GDICOEFF Header;
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RGNOBJ;

typedef struct _DC_ATTR {
    ULONG_PTR hbrush;
    ULONG_PTR hpen;
    ULONG_PTR hfont;
    ULONG_PTR hPalette;
    ULONG TextColor;
    ULONG BackColor;
    ULONG BkMode;
    ULONG old_rop2;
    ULONG poly_fill_mode;
    ULONG stretch_blt_mode;
} DC_ATTR;

typedef struct _SURFACE {
    GDICOEFF Header;
    HANDLE  hdev;
    LONG    sizlBitmap_cx;
    LONG    sizlBitmap_cy;
    POINTL  ptlOrigin;
    ULONG   ulComposition;
    ULONG   ulColorTable;
    PVOID   ColorTable;
    FLONG   flHooks;
    PVOID   pvScan0;
    LONG    lDelta;
    USHORT  iFormat;
    USHORT  fjBitmap;
    PVOID   pvBits;
    HBITMAP hBitmap;
} SURFACE;

typedef struct _BASEDC {
    GDICOEFF Header;
    SURFACE *psurface;
    HDC hdcParent;
    DC_ATTR *pdcattr;
    FLONG fl;
    HANDLE hmgr;
    LONG SaveDepth;
    ULONG_PTR hClipRgn;
    LONG clipLeft;
    LONG clipTop;
    LONG clipRight;
    LONG clipBottom;
} BASEDC;

struct _W32K_CLASS_ENTRY {
    W32K_WNDPROC lpfnWndProc;
    HBRUSH    hbrBackground;
    HCURSOR   hCursor;
    HICON     hIcon;
    ULONG_PTR hInstance;
    WCHAR     szClassName[64];
    ULONG_PTR cbWndExtra;
    ULONG_PTR cbClsExtra;
    BOOLEAN   inuse;
};

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

/* GDI functions */
NTSTATUS APIENTRY GdiPatBlt(ULONG_PTR hdc, LONG Left, LONG Top, LONG Width, LONG Height, ULONG RasterOp);
NTSTATUS APIENTRY GdiExtTextOutW(ULONG_PTR hdc, INT X, INT Y, ULONG Options, ULONG_PTR ClipRect, ULONG_PTR String, INT Count, ULONG_PTR Dx);
NTSTATUS APIENTRY GdiRectangle(ULONG_PTR hdc, LONG Left, LONG Top, LONG Right, LONG Bottom);

/* USER functions */
NTSTATUS APIENTRY UserBeginPaint(ULONG_PTR hWnd, PW32K_PAINTSTRUCT pPs);
NTSTATUS APIENTRY UserEndPaint(ULONG_PTR hWnd, PW32K_PAINTSTRUCT pPs);

/* ---- atom.c ---- */
NTSTATUS NTAPI GlobalAddAtomW(PCWSTR lpString, PUSHORT lpAtom);
NTSTATUS NTAPI GlobalDeleteAtom(USHORT nAtom);
NTSTATUS NTAPI GlobalFindAtomW(PCWSTR lpString, PUSHORT lpAtom);
NTSTATUS NTAPI GlobalGetAtomNameW(USHORT nAtom, PWCHAR lpBuffer, int nMaxCount);
VOID NTAPI AtomInit(VOID);

/* ---- clipboard.c ---- */
NTSTATUS NTAPI UserOpenClipboard(ULONG_PTR hWnd);
NTSTATUS NTAPI UserCloseClipboard(VOID);
NTSTATUS NTAPI UserEmptyClipboard(VOID);
NTSTATUS NTAPI UserSetClipboardData(ULONG Format, HANDLE hMem, ULONG Size);
NTSTATUS NTAPI UserGetClipboardData(ULONG Format, PVOID *ppData, PULONG pSize);
NTSTATUS NTAPI UserCountClipboardFormats(PULONG pCount);
NTSTATUS NTAPI UserEnumClipboardFormats(ULONG PrevFormat, PULONG pNextFormat);
NTSTATUS NTAPI UserRegisterClipboardFormatW(PCWSTR lpFormatName, PULONG lpFormat);
NTSTATUS NTAPI UserGetClipboardFormatNameW(ULONG Format, PWCHAR lpBuf, int cchMax);
NTSTATUS NTAPI UserIsClipboardFormatAvailable(ULONG Format, PULONG pAvailable);
VOID NTAPI ClipboardInit(VOID);

/* ---- base.c ---- */
PVOID NTAPI BaseAllocateHeap(ULONG Size, ULONG Tag);
VOID NTAPI BaseFreeHeap(PVOID Pointer);
PVOID NTAPI BaseReAllocateHeap(PVOID Pointer, ULONG NewSize, ULONG Tag);
PWSTR NTAPI BaseDuplicateString(PCWSTR Source);
NTSTATUS NTAPI BaseStringCchCopyW(PWSTR Dest, ULONG DestMax, PCWSTR Src);
NTSTATUS NTAPI BaseIntToStringW(int Value, PWCHAR Buf, ULONG BufLen);
NTSTATUS NTAPI BaseInitializeHeap(VOID);

/* ---- capture.c ---- */
NTSTATUS NTAPI UserSetCapture2(ULONG_PTR hWnd);
NTSTATUS NTAPI UserReleaseCapture(VOID);
NTSTATUS NTAPI UserGetCapture(PULONG_PTR pHwnd);
NTSTATUS NTAPI UserTrackMouseEvent(ULONG_PTR HwndTrack, ULONG HoverTime);

/* ---- desktop.c ---- */
NTSTATUS NTAPI UserCreateDesktopW(PCWSTR DesktopName, ULONG Flags);
NTSTATUS NTAPI UserOpenDesktopW(PCWSTR DesktopName, ULONG Flags, PULONG pHandle);
NTSTATUS NTAPI UserCloseDesktop(ULONG Handle);
NTSTATUS NTAPI UserSwitchDesktop(ULONG Handle);
NTSTATUS NTAPI UserGetThreadDesktop(PULONG pHandle);
NTSTATUS NTAPI UserSetThreadDesktop(ULONG Handle);
NTSTATUS NTAPI DesktopInit(VOID);

/* ---- dirs.c ---- */
NTSTATUS NTAPI UserGetWindowsDirectoryW(PWCHAR Buffer, ULONG BufferLen);
NTSTATUS NTAPI UserGetSystemDirectoryW(PWCHAR Buffer, ULONG BufferLen);
NTSTATUS NTAPI UserGetTempPathW(ULONG BufferLen, PWCHAR Buffer);
NTSTATUS NTAPI UserGetCurrentDirectoryW(ULONG BufferLen, PWCHAR Buffer);
NTSTATUS NTAPI UserSetCurrentDirectoryW(PCWSTR Path);
NTSTATUS NTAPI UserGetFullPathNameW(PCWSTR FileName, ULONG BufferLen, PWCHAR Buffer, PWSTR *pFilePart);

/* ---- dragdrop.c ---- */
NTSTATUS NTAPI UserRegisterDragDrop(ULONG_PTR Hwnd, ULONG_PTR Callback);
NTSTATUS NTAPI UserRevokeDragDrop(ULONG_PTR Hwnd);
NTSTATUS NTAPI UserDoDragDrop(ULONG_PTR HwndSource, ULONG DataFormat, ULONG AllowedEffects, PULONG pEffect);
NTSTATUS NTAPI DragDropInit(VOID);

/* ---- event.c ---- */
NTSTATUS NTAPI UserInjectKeyboardEvent(ULONG VirtualKey, ULONG ScanCode, ULONG Flags);
NTSTATUS NTAPI UserWaitMessage(VOID);
NTSTATUS NTAPI UserFlushInputEvents(VOID);
NTSTATUS NTAPI EventInit(VOID);

/* ---- ex.c ---- */
NTSTATUS NTAPI UserGetWindowLongPtr(ULONG_PTR hWnd, int Index, PLONG_PTR pValue);
NTSTATUS NTAPI UserSetWindowLongPtr(ULONG_PTR hWnd, int Index, LONG_PTR Value, PLONG_PTR pOldValue);
NTSTATUS NTAPI UserGetClassLongPtr(ULONG_PTR hWnd, int Index, PLONG_PTR pValue);
NTSTATUS NTAPI UserSetClassLongPtr(ULONG_PTR hWnd, int Index, LONG_PTR Value, PLONG_PTR pOldValue);
NTSTATUS NTAPI UserGetClassName(ULONG_PTR hWnd, PWCHAR ClassName, int MaxCount);
NTSTATUS NTAPI UserSetProp(ULONG_PTR hWnd, ATOM Atom, HANDLE Value);
NTSTATUS NTAPI UserGetProp(ULONG_PTR hWnd, ATOM Atom, PHANDLE pValue);
NTSTATUS NTAPI UserRemoveProp(ULONG_PTR hWnd, ATOM Atom);

/* ---- icons.c ---- */
NTSTATUS NTAPI UserLoadIconW(ULONG_PTR hInstance, PCWSTR lpIconName, PHICON phIcon);
NTSTATUS NTAPI UserLoadCursorW(ULONG_PTR hInstance, PCWSTR lpCursorName, PHCURSOR phCursor);
NTSTATUS NTAPI UserSetCursor(HCURSOR hCursor, PHCURSOR phOldCursor);
NTSTATUS NTAPI UserGetCursor(PHCURSOR phCursor);
NTSTATUS NTAPI UserShowCursor(BOOL bShow, PBOOL pOldVisible);
NTSTATUS NTAPI UserGetCursorPos(PW32K_POINT pPoint);
NTSTATUS NTAPI UserSetCursorPos(LONG X, LONG Y);
NTSTATUS NTAPI UserDrawIcon(ULONG_PTR hdc, LONG X, LONG Y, HICON hIcon);
NTSTATUS NTAPI IconsInit(VOID);

/* ---- keyboard.c ---- */
NTSTATUS NTAPI UserGetKeyState(int vKey, SHORT *pState);
NTSTATUS NTAPI UserGetKeyboardState(PUCHAR pKeyState);
NTSTATUS NTAPI UserSetKeyboardState(const PUCHAR pKeyState);
NTSTATUS NTAPI UserMapVirtualKey(UINT uCode, UINT uMapType, PUINT pResult);
VOID KeyboardUpdateKeyState(int vKey, BOOL Down);
NTSTATUS NTAPI KeyboardInit(VOID);

/* ---- libmgmt.c ---- */
NTSTATUS NTAPI UserLoadLibraryW(PCWSTR LibraryName, PHANDLE phModule);
NTSTATUS NTAPI UserFreeLibrary(HANDLE hModule);
NTSTATUS NTAPI UserGetProcAddress(HANDLE hModule, PCSTR ProcName, PVOID *pAddr);
NTSTATUS NTAPI UserGetModuleHandleW(PCWSTR ModuleName, PHANDLE phModule);
NTSTATUS NTAPI LibMgmtInit(VOID);

/* ---- loadbits.c ---- */
NTSTATUS NTAPI UserLoadBitmap(ULONG_PTR hInstance, PCWSTR lpBitmapName, PULONG phBitmap, PULONG pWidth, PULONG pHeight);
NTSTATUS NTAPI UserCreateBitmap(ULONG Width, ULONG Height, ULONG BitsPerPixel, PVOID pData, ULONG DataSize, PULONG phBitmap);
NTSTATUS NTAPI UserDeleteBitmap(ULONG hBitmap);
NTSTATUS NTAPI LoadBitsInit(VOID);

/* ---- logon.c ---- */
NTSTATUS NTAPI UserLogonUserW(PCWSTR UserName, PCWSTR Domain, PCWSTR Password, PULONG pSessionId);
NTSTATUS NTAPI UserLogoffUser(ULONG SessionId);
NTSTATUS NTAPI UserLockWorkStation(VOID);
NTSTATUS NTAPI LogonInit(VOID);

/* ---- queue.c ---- */
NTSTATUS NTAPI UserPostThreadMessage(ULONG ThreadId, ULONG Msg, ULONG_PTR wParam, LONG_PTR lParam);
NTSTATUS NTAPI UserDestroyThreadQueue(ULONG ThreadId);
NTSTATUS NTAPI QueueInit(VOID);

/* ---- movesizs.c ---- */
NTSTATUS NTAPI UserMoveWindow(ULONG_PTR hWnd, LONG x, LONG y, LONG cx, LONG cy, BOOL bRepaint);
NTSTATUS NTAPI UserBeginMoveSize(ULONG_PTR hWnd, BOOL fMove);
NTSTATUS NTAPI UserEndMoveSize(VOID);
NTSTATUS NTAPI UserUpdateWindow(ULONG_PTR hWnd);
NTSTATUS NTAPI MoveSizeInit(VOID);

/* ---- profile.c ---- */
NTSTATUS NTAPI UserGetProfileIntW(PCWSTR AppName, PCWSTR KeyName, INT Default, PINT pResult);
NTSTATUS NTAPI UserGetProfileStringW(PCWSTR AppName, PCWSTR KeyName, PCWSTR Default, PWCHAR ReturnedString, ULONG cchReturned, PULONG pcchReturned);
NTSTATUS NTAPI UserWriteProfileStringW(PCWSTR AppName, PCWSTR KeyName, PCWSTR String);
NTSTATUS NTAPI ProfileInit(VOID);

/* ---- syscmd.c ---- */
NTSTATUS NTAPI UserShellExecuteW(PCWSTR Operation, PCWSTR File, PCWSTR Parameters, PCWSTR Directory, INT ShowCmd, PHANDLE phProcess);
NTSTATUS NTAPI UserMessageBoxW(HWND hWnd, PCWSTR Text, PCWSTR Caption, UINT Type);
NTSTATUS NTAPI SysCmdInit(VOID);

/* ---- taskman.c ---- */
NTSTATUS NTAPI UserGetWindowTitle(ULONG_PTR Hwnd, PWCHAR Title, ULONG MaxLen);
NTSTATUS NTAPI UserSetWindowTitle(ULONG_PTR Hwnd, PCWSTR Title);
NTSTATUS NTAPI UserGetWindowTextW(ULONG_PTR Hwnd, PWCHAR Buffer, int MaxCount);
NTSTATUS NTAPI UserEnumWindows(PULONG pCount, PULONG_PTR pHwnds, ULONG MaxCount);
NTSTATUS NTAPI UserGetWindowThreadProcessId(ULONG_PTR Hwnd, PULONG pProcessId, PULONG pThreadId);
NTSTATUS NTAPI TaskManInit(VOID);

/* ---- timers.c ---- */
NTSTATUS NTAPI UserSetTimer(ULONG_PTR hWnd, ULONG_PTR TimerId, ULONG Elapse, ULONG_PTR TimerFunc);
NTSTATUS NTAPI UserKillTimer(ULONG_PTR hWnd, ULONG_PTR TimerId);
NTSTATUS NTAPI UserCheckTimers(VOID);
NTSTATUS NTAPI UserGetTickCount(PULONG pTicks);
NTSTATUS NTAPI TimersInit(VOID);

/* ---- winmgr.c ---- */
NTSTATUS NTAPI UserBringWindowToTop(ULONG_PTR Hwnd);
NTSTATUS NTAPI UserWindowFromPoint(LONG x, LONG y, PULONG_PTR pHwnd);
NTSTATUS NTAPI UserIsWindow(ULONG_PTR Hwnd, PBOOL pIsWindow);
NTSTATUS NTAPI UserGetParent(ULONG_PTR Hwnd, PULONG_PTR pParent);
NTSTATUS NTAPI UserEnableWindow(ULONG_PTR Hwnd, BOOL bEnable, PBOOL pWasEnabled);
NTSTATUS NTAPI WinMgrInit(VOID);

/* ---- winable.c ---- */
NTSTATUS NTAPI UserClientToScreen(ULONG_PTR Hwnd, PW32K_POINT pPoint);
NTSTATUS NTAPI UserScreenToClient(ULONG_PTR Hwnd, PW32K_POINT pPoint);
NTSTATUS NTAPI UserMapWindowPoints(ULONG_PTR HwndFrom, ULONG_PTR HwndTo, PW32K_POINT Points, ULONG Count);
NTSTATUS NTAPI UserGetSystemMetrics(int nIndex, PINT pValue);
NTSTATUS NTAPI UserSystemParametersInfoW(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni);
NTSTATUS NTAPI WinableInit(VOID);

/* ---- validate.c ---- */
NTSTATUS NTAPI UserValidateHwnd(ULONG_PTR Hwnd, PWINDOW *ppWnd);
NTSTATUS NTAPI UserValidateHdc(ULONG_PTR Hdc, PVOID *ppDc);
NTSTATUS NTAPI UserRegisterHandle(PVOID Object, ULONG ObjectType, ULONG Access, PULONG pHandle);
NTSTATUS NTAPI ValidateInit(VOID);

/* ---- winwhere.c ---- */
NTSTATUS NTAPI UserDefWindowProc(ULONG_PTR Hwnd, ULONG Msg, ULONG_PTR wParam, LONG_PTR lParam, PLONG_PTR pResult);
NTSTATUS NTAPI UserIsZoomed(ULONG_PTR Hwnd, PBOOL pZoomed);
NTSTATUS NTAPI UserIsIconic(ULONG_PTR Hwnd, PBOOL pIconic);
NTSTATUS NTAPI WinWhereInit(VOID);

/* ---- update.c ---- */
NTSTATUS NTAPI UserInvalidateRect2(ULONG_PTR hWnd, PW32K_RECT pRect, BOOL bErase);
NTSTATUS NTAPI UserInvalidateRect(ULONG_PTR hWnd, ULONG_PTR lpRect, BOOL bErase);
VOID NTAPI UpdateValidateRectPartial(ULONG_PTR Hwnd, PW32K_RECT pRect);
NTSTATUS NTAPI UserValidateRgn(ULONG_PTR Hwnd);
NTSTATUS NTAPI UserRedrawWindow(ULONG_PTR Hwnd, PW32K_RECT pRect, ULONG_PTR hRgnUpdate, ULONG uFlags);
NTSTATUS NTAPI UpdateInit(VOID);

/* ---- Settings subsystem (win32k/settings.c) ---------------------------- */

NTSTATUS NTAPI SettingsInit(VOID);

NTSTATUS NTAPI SettingsGetMasterVolume(PULONG pValue);
NTSTATUS NTAPI SettingsSetMasterVolume(ULONG Value);
NTSTATUS NTAPI SettingsGetMute(PBOOL pValue);
NTSTATUS NTAPI SettingsSetMute(BOOL Value);

NTSTATUS NTAPI SettingsGetResolution(PULONG pWidth, PULONG pHeight);
NTSTATUS NTAPI SettingsSetResolution(ULONG Width, ULONG Height);
NTSTATUS NTAPI SettingsGetRefreshRate(PULONG pValue);
NTSTATUS NTAPI SettingsSetRefreshRate(ULONG Value);
NTSTATUS NTAPI SettingsGetDpiScale(PULONG pValue);
NTSTATUS NTAPI SettingsSetDpiScale(ULONG Value);
NTSTATUS NTAPI SettingsGetNightLight(PBOOL pValue);
NTSTATUS NTAPI SettingsSetNightLight(BOOL Value);
NTSTATUS NTAPI SettingsGetHdrEnabled(PBOOL pValue);
NTSTATUS NTAPI SettingsSetHdrEnabled(BOOL Value);

NTSTATUS NTAPI SettingsGetKeyboardRepeatRate(PULONG pValue);
NTSTATUS NTAPI SettingsSetKeyboardRepeatRate(ULONG Value);
NTSTATUS NTAPI SettingsGetKeyboardRepeatDelay(PULONG pValue);
NTSTATUS NTAPI SettingsSetKeyboardRepeatDelay(ULONG Value);
NTSTATUS NTAPI SettingsGetMouseSpeed(PULONG pValue);
NTSTATUS NTAPI SettingsSetMouseSpeed(ULONG Value);
NTSTATUS NTAPI SettingsGetDoubleClickTime(PULONG pValue);
NTSTATUS NTAPI SettingsSetDoubleClickTime(ULONG Value);
NTSTATUS NTAPI SettingsGetSwapMouseButtons(PBOOL pValue);
NTSTATUS NTAPI SettingsSetSwapMouseButtons(BOOL Value);

NTSTATUS NTAPI SettingsGetSleepTimeoutAC(PULONG pValue);
NTSTATUS NTAPI SettingsSetSleepTimeoutAC(ULONG Value);
NTSTATUS NTAPI SettingsGetSleepTimeoutDC(PULONG pValue);
NTSTATUS NTAPI SettingsSetSleepTimeoutDC(ULONG Value);
NTSTATUS NTAPI SettingsGetScreenTimeoutAC(PULONG pValue);
NTSTATUS NTAPI SettingsSetScreenTimeoutAC(ULONG Value);
NTSTATUS NTAPI SettingsGetScreenTimeoutDC(PULONG pValue);
NTSTATUS NTAPI SettingsSetScreenTimeoutDC(ULONG Value);
NTSTATUS NTAPI SettingsGetPowerButtonAction(PULONG pValue);
NTSTATUS NTAPI SettingsSetPowerButtonAction(ULONG Value);
NTSTATUS NTAPI SettingsGetFastStartup(PBOOL pValue);
NTSTATUS NTAPI SettingsSetFastStartup(BOOL Value);

NTSTATUS NTAPI SettingsGetTimeZoneBias(PULONG pValue);
NTSTATUS NTAPI SettingsSetTimeZoneBias(ULONG Value);
NTSTATUS NTAPI SettingsGetTimeFormat(PULONG pValue);
NTSTATUS NTAPI SettingsSetTimeFormat(ULONG Value);
NTSTATUS NTAPI SettingsGetDateFormat(PULONG pValue);
NTSTATUS NTAPI SettingsSetDateFormat(ULONG Value);
NTSTATUS NTAPI SettingsGetAutoTimeSync(PBOOL pValue);
NTSTATUS NTAPI SettingsSetAutoTimeSync(BOOL Value);

NTSTATUS NTAPI SettingsGetHighContrast(PBOOL pValue);
NTSTATUS NTAPI SettingsSetHighContrast(BOOL Value);
NTSTATUS NTAPI SettingsGetStickyKeys(PBOOL pValue);
NTSTATUS NTAPI SettingsSetStickyKeys(BOOL Value);
NTSTATUS NTAPI SettingsGetFilterKeys(PBOOL pValue);
NTSTATUS NTAPI SettingsSetFilterKeys(BOOL Value);
NTSTATUS NTAPI SettingsGetToggleKeys(PBOOL pValue);
NTSTATUS NTAPI SettingsSetToggleKeys(BOOL Value);
NTSTATUS NTAPI SettingsGetMouseKeys(PBOOL pValue);
NTSTATUS NTAPI SettingsSetMouseKeys(BOOL Value);

NTSTATUS NTAPI SettingsGetTelemetryEnabled(PBOOL pValue);
NTSTATUS NTAPI SettingsSetTelemetryEnabled(BOOL Value);
NTSTATUS NTAPI SettingsGetAdvertisingId(PBOOL pValue);
NTSTATUS NTAPI SettingsSetAdvertisingId(BOOL Value);
NTSTATUS NTAPI SettingsGetLocationService(PBOOL pValue);
NTSTATUS NTAPI SettingsSetLocationService(BOOL Value);

NTSTATUS NTAPI SettingsGetNotificationsEnabled(PBOOL pValue);
NTSTATUS NTAPI SettingsSetNotificationsEnabled(BOOL Value);
NTSTATUS NTAPI SettingsGetDndMode(PBOOL pValue);
NTSTATUS NTAPI SettingsSetDndMode(BOOL Value);

NTSTATUS NTAPI SettingsGetWallpaperPath(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetWallpaperPath(PCWSTR Value);
NTSTATUS NTAPI SettingsGetDesktopColor(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetDesktopColor(PCWSTR Value);
NTSTATUS NTAPI SettingsGetComputerName(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetComputerName(PCWSTR Value);
NTSTATUS NTAPI SettingsGetRegisteredOwner(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetRegisteredOwner(PCWSTR Value);
NTSTATUS NTAPI SettingsGetRegisteredOrg(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetRegisteredOrg(PCWSTR Value);
NTSTATUS NTAPI SettingsGetProductId(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetProductId(PCWSTR Value);

/* ---- Screensaver ---- */
NTSTATUS NTAPI SettingsGetScreensaverPath(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetScreensaverPath(PCWSTR Value);
NTSTATUS NTAPI SettingsGetScreensaverTimeout(PULONG pValue);
NTSTATUS NTAPI SettingsSetScreensaverTimeout(ULONG Value);
NTSTATUS NTAPI SettingsGetScreensaverSecure(PBOOL pValue);
NTSTATUS NTAPI SettingsSetScreensaverSecure(BOOL Value);
NTSTATUS NTAPI SettingsGetScreensaverEnabled(PBOOL pValue);
NTSTATUS NTAPI SettingsSetScreensaverEnabled(BOOL Value);

/* ---- User profile / avatar ---- */
NTSTATUS NTAPI SettingsGetUserProfilePicture(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetUserProfilePicture(PCWSTR Value);
NTSTATUS NTAPI SettingsGetUserDisplayName(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetUserDisplayName(PCWSTR Value);

/* ---- Taskbar / Start menu ---- */
NTSTATUS NTAPI SettingsGetTaskbarPosition(PULONG pValue);
NTSTATUS NTAPI SettingsSetTaskbarPosition(ULONG Value);
NTSTATUS NTAPI SettingsGetTaskbarAutoHide(PULONG pValue);
NTSTATUS NTAPI SettingsSetTaskbarAutoHide(ULONG Value);
NTSTATUS NTAPI SettingsGetTaskbarSmallIcons(PULONG pValue);
NTSTATUS NTAPI SettingsSetTaskbarSmallIcons(ULONG Value);
NTSTATUS NTAPI SettingsGetTaskbarCombine(PULONG pValue);
NTSTATUS NTAPI SettingsSetTaskbarCombine(ULONG Value);
NTSTATUS NTAPI SettingsGetShowStartButton(PBOOL pValue);
NTSTATUS NTAPI SettingsSetShowStartButton(BOOL Value);
NTSTATUS NTAPI SettingsGetStartButtonLabel(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetStartButtonLabel(PCWSTR Value);
NTSTATUS NTAPI SettingsGetStartMenuStyle(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetStartMenuStyle(PCWSTR Value);
NTSTATUS NTAPI SettingsGetShowRunCommand(PBOOL pValue);
NTSTATUS NTAPI SettingsSetShowRunCommand(BOOL Value);
NTSTATUS NTAPI SettingsGetShowSearchBox(PBOOL pValue);
NTSTATUS NTAPI SettingsSetShowSearchBox(BOOL Value);
NTSTATUS NTAPI SettingsGetShowMyComputer(PBOOL pValue);
NTSTATUS NTAPI SettingsSetShowMyComputer(BOOL Value);
NTSTATUS NTAPI SettingsGetShowMyDocuments(PBOOL pValue);
NTSTATUS NTAPI SettingsSetShowMyDocuments(BOOL Value);
NTSTATUS NTAPI SettingsGetShowControlPanel(PBOOL pValue);
NTSTATUS NTAPI SettingsSetShowControlPanel(BOOL Value);
NTSTATUS NTAPI SettingsGetShowRecycleBin(PBOOL pValue);
NTSTATUS NTAPI SettingsSetShowRecycleBin(BOOL Value);

/* ---- File Explorer ---- */
NTSTATUS NTAPI SettingsGetShowHiddenFiles(PBOOL pValue);
NTSTATUS NTAPI SettingsSetShowHiddenFiles(BOOL Value);
NTSTATUS NTAPI SettingsGetShowFileExtensions(PBOOL pValue);
NTSTATUS NTAPI SettingsSetShowFileExtensions(BOOL Value);
NTSTATUS NTAPI SettingsGetShowProtectedOSFiles(PBOOL pValue);
NTSTATUS NTAPI SettingsSetShowProtectedOSFiles(BOOL Value);
NTSTATUS NTAPI SettingsGetLaunchFolderWindows(PBOOL pValue);
NTSTATUS NTAPI SettingsSetLaunchFolderWindows(BOOL Value);
NTSTATUS NTAPI SettingsGetDefaultFolderView(PULONG pValue);
NTSTATUS NTAPI SettingsSetDefaultFolderView(ULONG Value);

/* ---- Terminal / shell defaults ---- */
NTSTATUS NTAPI SettingsGetDefaultTerminal(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetDefaultTerminal(PCWSTR Value);
NTSTATUS NTAPI SettingsGetDefaultShell(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetDefaultShell(PCWSTR Value);
NTSTATUS NTAPI SettingsGetPowerShellPath(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetPowerShellPath(PCWSTR Value);

/* ---- Theme ---- */
NTSTATUS NTAPI SettingsGetThemeName(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetThemeName(PCWSTR Value);
NTSTATUS NTAPI SettingsGetAccentColor(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetAccentColor(PCWSTR Value);
NTSTATUS NTAPI SettingsGetEnableTransparency(PBOOL pValue);
NTSTATUS NTAPI SettingsSetEnableTransparency(BOOL Value);

/* ---- Customization (PowerToys-style) ---- */
NTSTATUS NTAPI SettingsGetFancyZonesEnabled(PBOOL pValue);
NTSTATUS NTAPI SettingsSetFancyZonesEnabled(BOOL Value);
NTSTATUS NTAPI SettingsGetAlwaysOnTopEnabled(PBOOL pValue);
NTSTATUS NTAPI SettingsSetAlwaysOnTopEnabled(BOOL Value);
NTSTATUS NTAPI SettingsGetColorPickerEnabled(PBOOL pValue);
NTSTATUS NTAPI SettingsSetColorPickerEnabled(BOOL Value);
NTSTATUS NTAPI SettingsGetPowerRenameEnabled(PBOOL pValue);
NTSTATUS NTAPI SettingsSetPowerRenameEnabled(BOOL Value);
NTSTATUS NTAPI SettingsGetKeyboardManagerEnabled(PBOOL pValue);
NTSTATUS NTAPI SettingsSetKeyboardManagerEnabled(BOOL Value);
NTSTATUS NTAPI SettingsGetLightSwitchEnabled(PBOOL pValue);
NTSTATUS NTAPI SettingsSetLightSwitchEnabled(BOOL Value);

/* ---- Security / privacy ---- */
NTSTATUS NTAPI SettingsGetTpmRequired(PBOOL pValue);
NTSTATUS NTAPI SettingsSetTpmRequired(BOOL Value);
NTSTATUS NTAPI SettingsGetSecureBootRequired(PBOOL pValue);
NTSTATUS NTAPI SettingsSetSecureBootRequired(BOOL Value);
NTSTATUS NTAPI SettingsGetRequireOnlineAccount(PBOOL pValue);
NTSTATUS NTAPI SettingsSetRequireOnlineAccount(BOOL Value);
NTSTATUS NTAPI SettingsGetCortanaEnabled(PBOOL pValue);
NTSTATUS NTAPI SettingsSetCortanaEnabled(BOOL Value);
NTSTATUS NTAPI SettingsGetShowSyncNotifications(PBOOL pValue);
NTSTATUS NTAPI SettingsSetShowSyncNotifications(BOOL Value);

/* ---- Cosmetic: title bars, window borders ---- */
NTSTATUS NTAPI SettingsGetWindowBorderWidth(PULONG pValue);
NTSTATUS NTAPI SettingsSetWindowBorderWidth(ULONG Value);
NTSTATUS NTAPI SettingsGetWindowCaptionHeight(PULONG pValue);
NTSTATUS NTAPI SettingsSetWindowCaptionHeight(ULONG Value);
NTSTATUS NTAPI SettingsGetMenuHeight(PULONG pValue);
NTSTATUS NTAPI SettingsSetMenuHeight(ULONG Value);
NTSTATUS NTAPI SettingsGetCursorScheme(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetCursorScheme(PCWSTR Value);
NTSTATUS NTAPI SettingsGetSoundScheme(PWCHAR pBuf, ULONG BufLen);
NTSTATUS NTAPI SettingsSetSoundScheme(PCWSTR Value);

/* ---- Diagnostic / feedback ---- */
NTSTATUS NTAPI SettingsGetErrorReporting(PBOOL pValue);
NTSTATUS NTAPI SettingsSetErrorReporting(BOOL Value);
NTSTATUS NTAPI SettingsGetFeedbackFrequency(PBOOL pValue);
NTSTATUS NTAPI SettingsSetFeedbackFrequency(BOOL Value);
NTSTATUS NTAPI SettingsGetActivityHistory(PBOOL pValue);
NTSTATUS NTAPI SettingsSetActivityHistory(BOOL Value);
NTSTATUS NTAPI SettingsGetDiagnosticData(PBOOL pValue);
NTSTATUS NTAPI SettingsSetDiagnosticData(BOOL Value);
NTSTATUS NTAPI SettingsGetTailoredExperiences(PBOOL pValue);
NTSTATUS NTAPI SettingsSetTailoredExperiences(BOOL Value);

#endif /* _WIN32K_H_ */