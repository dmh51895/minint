/*
 * MinNT - win32k/winuser.h
 * Windows USER definitions (Wine compatibility)
 */

#ifndef _WINUSER_H_
#define _WINUSER_H_

#include <nt/ntdef.h>

#define HANDLE UINT_PTR
#define HWND UINT_PTR
#define HMODULE UINT_PTR
#define HINSTANCE UINT_PTR
#define HICON UINT_PTR
#define HCURSOR UINT_PTR
#define HKL UINT_PTR
#define HACCEL UINT_PTR
#define HMENU UINT_PTR
#define HDESK UINT_PTR
#define HWINSTA UINT_PTR
#define HHOOK UINT_PTR

#define INPUT_KEYBOARD  1
#define INPUT_MOUSE    0
#define INPUT_HARDWARE 2

#define PM_REMOVE      0x0001

#define MB_OK                    0x00000000
#define MB_OKCANCEL              0x00000001
#define MB_ABORTRETRYIGNORE      0x00000002
#define MB_YESNOCANCEL           0x00000003
#define MB_YESNO                 0x00000004
#define MB_RETRYCANCEL          0x00000005

#define MB_ICONHAND              0x00000010
#define MB_ICONQUESTION          0x00000020
#define MB_ICONASTERISK          0x00000040
#define MB_ICONWARNING          0x00000030
#define MB_ICONERROR            0x00000010
#define MB_ICONINFORMATION      0x00000040

#define CW_USEDEFAULT           0x80000000

#define SW_HIDE                 0
#define SW_SHOWNORMAL           1
#define SW_NORMAL               1
#define SW_SHOWMINIMIZED        2
#define SW_SHOWMAXIMIZED        3
#define SW_MAXIMIZE             3
#define SW_SHOWNOACTIVATE       4
#define SW_SHOW                 5
#define SW_MINIMIZE             6
#define SW_SHOWMINNOACTIVE      7
#define SW_SHOWNA               8
#define SW_RESTORE              9
#define SW_SHOWDEFAULT          10

#define SWP_NOSIZE              0x0001
#define SWP_NOMOVE              0x0002
#define SWP_NOZORDER            0x0004
#define SWP_NOREDRAW            0x0008
#define SWP_NOACTIVATE          0x0010
#define SWP_FRAMECHANGED        0x0020
#define SWP_SHOWWINDOW          0x0040
#define SWP_HIDEWINDOW          0x0080

#define GWL_WNDPROC             (-4)
#define GWL_HINSTANCE          (-6)
#define GWL_HWNDPARENT         (-8)
#define GWL_STYLE              (-16)
#define GWL_EXSTYLE            (-20)
#define GWL_USERDATA           (-21)
#define GWL_ID                 (-12)

typedef struct _MSG {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD  time;
    POINT  pt;
} MSG, *PMSG;

typedef struct _WNDCLASSW {
    UINT style;
    WNDPROC lpfnWndProc;
    LONG cbClsExtra;
    LONG cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCWSTR lpszMenuName;
    LPCWSTR lpszClassName;
} WNDCLASSW;

typedef struct _CREATESTRUCTW {
    LPVOID lpCreateParams;
    HINSTANCE hInstance;
    HINSTANCE hPrevInstance;
    LPCWSTR lpszClass;
    LPCWSTR lpszName;
    DWORD dwStyle;
    int x;
    int y;
    int cx;
    int cy;
    HWND hwndParent;
    HMENU hMenu;
    HANDLE hInstance;
    LPVOID lpParam;
} CREATESTRUCTW;

typedef struct _PAINTSTRUCT {
    HDC hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore;
    BOOL fIncUpdate;
    BYTE rgbReserved[32];
} PAINTSTRUCT;

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

typedef LONG WPARAM;
typedef LONG LPARAM;
typedef LONG_PTR LRESULT;
typedef LRESULT (CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

#define IsWindow(x) 1
#define IsDialogMessage(x,y) 0
#define ShowWindow(x,y) 1
#define UpdateWindow(x) 1
#define DestroyWindow(x) 1
#define CreateWindowExW(a,b,c,d,e,f,g,h,i,j,k,l) ((HWND)0)
#define GetMessageW(a,b,c,d) 0
#define DispatchMessageW(a) 0
#define TranslateMessage(a) 1
#define DefWindowProcW(a,b,c,d) 0

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
#define VK_ESCAPE         0x1B
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
#define VK_EXECUTE        0x2B
#define VK_SNAPSHOT       0x2C
#define VK_INSERT         0x2D
#define VK_DELETE         0x2E
#define VK_HELP           0x2F

#define WH_MOUSE_LL       14
#define WH_KEYBOARD_LL    13

#define HC_ACTION         0
#define HC_GETNEXT        1
#define HC_SKIP           2
#define HC_NOREMOVE       3
#define HC_NOREMOVE       3

#endif /* _WINUSER_H_ */
