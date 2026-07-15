#!/usr/bin/env python3
"""
Generate win32k internal headers based on dependency analysis.
Usage: python3 tools/generate_win32k_headers.py
"""

import os
from pathlib import Path

WIN32K_DIR = Path(__file__).parent.parent / "win32k"
WIN32K_DIR.mkdir(exist_ok=True)

# ============================================================================
# win32k.h - THE CORE HEADER (138 files need it)
# ============================================================================

WIN32K_H = r'''/*
 * MinNT - win32k/win32k.h
 * Win32k.sys internal header
 * Generated from dependency analysis of ReactOS win32ss
 */

#ifndef _WIN32K_H_
#define _WIN32K_H_

#include <nt/ntdef.h>
#include <nt/ke.h>
#include <nt/io.h>

/*** GDIDEVICES ***/
typedef struct _DEVINFO {
    ULONG          出的 ulang ulCharSet;
    ULONG           ulDACOMPRESSION; // Compression types supported
    ULONG           ul着头Compression; // What's been done
    DEVMODEW        dm;
    GDIINFO         GdiInfo;
    GDIEXTRA        GdiExt;
    CLSIGNATURE     clsSig[CLINE_COUNT];
    BRUSHSIGNATURE  bmsig DevData.Cur Shelton;
    PENSignature    pensig DevInfo.BrushData.LongtlyCurBrush;
    RBUSHigon       brushsig BrushInfo.P看你懂不懂;
} DEVINFO, *PDEVINFO;

typedef struct _W32THREADINFO
{
    ULONG       ptr_Fatal藻;
    struct _W32PROCESS *Process;
    PVOID       ptr_Desktop;
    PVOID       ptr_Terminal;
    HWINSTA     hwinsta;
    HDESK       hdesk;
    HANDLE      hThread;
    HANDLE      hTask;
    ULONG       idProcess;
    ULONG       idThread;
    SIZE_T      cNum_Windows;
    PVOID       pW32Thread;
    SHARED_INFO SharedInfo;
} W32THREADINFO;

typedef struct _W32PROCESS
{
    PEVENT_INFO ptr_Terminal;
    struct _ETHREAD *ptr_Thread;
    HANDLE       UniqueProcess;
    HANDLE       ptr_W32PID;
    W32THREADINFO *ptr_ThreadInfo;
    HWINSTA      hwinsta;
    DWORD        Process ID;
    BOOLEAN     fGlobalS了我喔 big World;
    PRTL_BITMAP  pgdiD awesome;
    PVOID        puddpResident;
} W32PROCESS;

#define GDI_CATEGORY_BITBLT       0x00000001
#define GDI_CATEGORY_FONT         0x00000002
#define GDI_CATEGORY_PATH         0x00000004
#define GDI_CATEGORY_PALETTE      0x00000008
#define GDI_CATEGORY_CLIPING      0x00000010
#define GDI_CATEGORY_POLYLINE    0x00000020

/*** Debug macros ***/
#define WIN32K_ASSERT(exp) \
    do { if (!(exp)) KeBugCheckEx(0xDEAD0001, __LINE__, 0, 0, 0); } while(0)

#define WIN32K_TRACE(fmt, ...) \
    DbgPrint("WIN32K: " fmt, ##__VA_ARGS__)

/*** Shared User/GDI Data (KUSER_SHARED_DATA) ***/
#define USER_SHARED_DATA          ((KUSER_SHARED_DATA *)0x7FFE0000)

typedef struct _WIN32K_GLOBALS
{
    HANDLE        hiShellDevice;
    DEVINFO       IniDevInfo;
    PALETTE       DefaultPalette[20];
    HSEMAPHORE    hpdcoEnable;
    HSEMAPHORE    hpevtEnable;
    W32PROCESS   *pRootProcess;
    HANDLE        ahsysMemjasper[559];
} WIN32K_GLOBALS;

extern WIN32K_GLOBALS gWin32kData;

/*** DC States ***/
typedef enum _DC_STATE
{
    DC_STATE_UNDEFINED,
    DC_STATE_MEMORY,
    DC_STATE_SCREEN,
    DC_STATE_DEVICE,
    DC_STATE_INFO
} DC_STATE;

#endif /* _WIN32K_H_ */
'''

# ============================================================================
# debug.h - Debug macros (125 files need it)
# ============================================================================

DEBUG_H = r'''/*
 * MinNT - win32k/debug.h
 * Debug macros for win32k subsystem
 * Generated from ReactOS win32ss debug.h
 */

#ifndef _WIN32K_DEBUG_H_
#define _WIN32K_DEBUG_H_

#include <nt/ke.h>

enum
{
    CHM_FATAL = 0,
    CHM_INTERN = 1,
    CHM_APICALLS = 2,
    CHM_GDI = 3,
    CHM_USER = 4,
    CHM_CONSOLE = 5,
    CHM_HOOKS = 6,
    CHM_PNP = 7,
    CHM_SYNCH = 8,
    CHM_DDE = 9,
    CHM_IMM = 10,
    CHM_INIT = 11,
    CHM_HANDLE = 12,
    CHM_OBJCACHE = 13
};

#define ASSERT(x) \
    do { if (!(x)) KeBugCheckEx(0xDEAD0001, (ULONG_PTR)__FILE__, __LINE__, (ULONG_PTR)#x, 0); } while(0)

#define DPRINT(fmt, ...) \
    DbgPrint("WIN32K[%d]: " fmt, Channel, ##__VA_ARGS__)

#define DPRINT1(fmt, ...) \
    DbgPrint("WIN32K[FATAL]: " fmt, ##__VA_ARGS__)

#define WIN32K_DBG_CH_ENABLE(ch) ((DbgPrint("WIN32K: Enabling channel %d\n", ch)), 1)

#define STATIC_ASSERT(expr) typedef int static_assert_[(expr) ? 1 : -1]

#define ASSERTMSG(msg, expr) \
    do { if (!(expr)) KeBugCheckEx(0xDEAD0002, (ULONG_PTR)__FILE__, __LINE__, (ULONG_PTR)#msg, 0); } while(0)

#endif /* _WIN32K_DEBUG_H_ */
'''

# ============================================================================
# user32.h - User32 public header (54 files need it)
# ============================================================================

USER32_H = r'''/*
 * MinNT - win32k/user32.h
 * USER32.DLL public interface
 */

#ifndef _USER32_H_
#define _USER32_H_

#include <nt/ntdef.h>

typedef struct _USER_MSG {
    HWND    hwnd;
    UINT    message;
    WPARAM  wParam;
    LPARAM  lParam;
    DWORD   time;
    POINT   pt;
} USER_MSG, *PUSER_MSG;

#define APIENTRY __attribute__((stdcall))

/*** Window Messages ***/
#define WM_NULL                         0x0000
#define WM_CREATE                       0x0001
#define WM_DESTROY                      0x0002
#define WM_MOVE                         0x0003
#define WM_SIZE                         0x0005
#define WM_ACTIVATE                     0x0006
#define WM_SETFOCUS                     0x0007
#define WM_KILLFOCUS                    0x0008
#define WM_ENABLE                       0x000A
#define WM_SETREDRAW                    0x000B
#define WM_SETTEXT                      0x000C
#define WM_GETTEXT                      0x000D
#define WM_GETTEXTLENGTH                0x000E
#define WM_PAINT                        0x000F
#define WM_CLOSE                        0x0010
#define WM_QUIT                         0x0012
#define WM_QUERYENDSESSION              0x0011
#define WM_ERASEBKGND                   0x0014
#define WM_SYSCOLORCHANGE               0x0015
#define WM_SHOWWINDOW                   0x0018
#define WM_WININICHANGE                 0x001A
#define WM_SETTINGCHANGE                0x001A
#define WM_DEVMODECHANGE                0x001B
#define WM_ACTIVATEAPP                  0x001C
#define WM_FONTCHANGE                   0x001D
#define WM_TIMECHANGE                   0x001E
#define WM_CANCELMODE                   0x001F
#define WM_SETCURSOR                    0x0020
#define WM_MOUSEACTIVATE                0x0021
#define WM_CHILDACTIVATE                0x0022
#define WM_QUEUESYNC                    0x0023
#define WM_GETMINMAXINFO                0x0024
#define WM_PAINTICON                    0x0026
#define WM_ICONERASEBKGND               0x0027
#define WM_NEXTDLGCTL                   0x0028
#define WM_SPOOLERSTATUS                0x002A
#define WM_DRAWITEM                     0x002B
#define WM_MEASUREITEM                  0x002C
#define WM_DELETEITEM                    0x002D
#define WM_VKEYTOITEM                   0x002E
#define WM_CHARTOITEM                   0x002F
#define WM_SETFONT                      0x0030
#define WM_GETFONT                      0x0031
#define WM_SETHOTKEY                    0x0032
#define WM_GETHOTKEY                    0x0033
#define WM_QUERYDRAGICON                0x0037
#define WM_DROPFILES                    0x0038
#define WM_QUERYOPEN                     0x0039
#define WM_ENDSESSION                   0x0010

#define WM_NCLBUTTONDOWN                0x00A1
#define WM_NCLBUTTONUP                  0x00A2
#define WM_NCLBUTTONDBLCLK             0x00A3
#define WM_NCRBUTTONDOWN                0x00A4
#define WM_NCRBUTTONUP                  0x00A5
#define WM_NCRBUTTONDBLCLK             0x00A6
#define WM_NCMBUTTONDOWN                0x00A7
#define WM_NCMBUTTONUP                  0x00A8
#define WM_NCMBUTTONDBLCLK             0x00A9

#define WM_KEYDOWN                      0x0100
#define WM_KEYUP                        0x0101
#define WM_CHAR                         0x0102
#define WM_DEADCHAR                     0x0103
#define WM_SYSKEYDOWN                   0x0104
#define WM_SYSKEYUP                     0x0105
#define WM_SYSCHAR                      0x0106
#define WM_SYSDEADCHAR                  0x0107
#define WM_KEYLAST                      0x0108
#define WM_IME_STARTCOMPOSITION         0x0109
#define WM_IME_ENDCOMPOSITION           0x010D
#define WM_IME_COMPOSITION              0x010F
#define WM_IME_KEYLAST                  0x010F

#define WM_INITDIALOG                   0x0110
#define WM_COMMAND                      0x0111
#define WM_SYSCOMMAND                   0x0112
#define WM_TIMER                        0x0113
#define WM_HSCROLL                      0x0114
#define WM_VSCROLL                      0x0115
#define WM_INITMENU                     0x0116
#define WM_INITMENUPOPUP                0x0117
#define WM_MENUSELECT                   0x011F
#define WM_MENUCHAR                     0x0120
#define WM_ENTERIDLE                    0x0121

#define WM_MOUSEMOVE                    0x0200
#define WM_LBUTTONDOWN                  0x0201
#define WM_LBUTTONUP                    0x0202
#define WM_LBUTTONDBLCLK               0x0203
#define WM_RBUTTONDOWN                  0x0204
#define WM_RBUTTONUP                    0x0205
#define WM_RBUTTONDBLCLK               0x0206
#define WM_MBUTTONDOWN                  0x0207
#define WM_MBUTTONUP                    0x0208
#define WM_MBUTTONDBLCLK               0x0209
#define WM_MOUSEWHEEL                   0x020A
#define WM_MOUSEHWHEEL                  0x020E

#define WM_SIZING                        0x0214
#define WM_CAPTURECHANGED                0x0215
#define WM_MOVING                        0x0216
#define WM_POWER                        0x0218
#define WM_DEVICECHANGE                  0x0219

#define WM_USER                         0x0400

/*** Virtual Keys ***/
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
#define VK_PRINT          0x2A
#define VK_EXECUTE        0x2B
#define VK_SNAPSHOT       0x2C
#define VK_INSERT         0x2D
#define VK_DELETE         0x2E
#define VK_HELP           0x2F
#define VK_0              0x30
#define VK_1              0x31
#define VK_2              0x32
#define VK_3              0x33
#define VK_4              0x34
#define VK_5              0x35
#define VK_6              0x36
#define VK_7              0x37
#define VK_8              0x38
#define VK_9              0x39
#define VK_A              0x41
#define VK_B              0x42
#define VK_C              0x43
#define VK_D              0x44
#define VK_E              0x45
#define VK_F              0x46
#define VK_G              0x47
#define VK_H              0x48
#define VK_I              0x49
#define VK_J              0x4A
#define VK_K              0x4B
#define VK_L              0x4C
#define VK_M              0x4D
#define VK_N              0x4E
#define VK_O              0x4F
#define VK_P              0x50
#define VK_Q              0x51
#define VK_R              0x52
#define VK_S              0x53
#define VK_T              0x54
#define VK_U              0x55
#define VK_V              0x56
#define VK_W              0x57
#define VK_X              0x58
#define VK_Y              0x59
#define VK_Z              0x5A

/*** Window Styles ***/
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

#define WS_EX_DLGMODALFRAME    0x00000001
#define WS_EX_NOPARENTNOTIFY   0x00000004
#define WS_EX_TOPMOST          0x00000008
#define WS_EX_ACCEPTFILES      0x00000010
#define WS_EX_TRANSPARENT      0x00000020
#define WS_EX_MDICHILD         0x00000040
#define WS_EX_TOOLWINDOW       0x00000080
#define WS_EX_WINDOWEDGE       0x00000100
#define WS_EX_CLIENTEDGE      0x00000200
#define WS_EX_CONTEXTHELP      0x00000400
#define WS_EX_RIGHT           0x00001000
#define WS_EX_LEFT             0x00000000
#define WS_EX_RTLREADING       0x00002000
#define WS_EX_LTRREADING       0x00000000
#define WS_EX_LEFTSCROLLBAR    0x00004000
#define WS_EX_RIGHTScollBAR    0x00000000
#define WS_EX_CONTROLPARENT    0x00010000
#define WS_EX_STATICEDGE       0x00020000
#define WS_EX_APPWINDOW        0x00040000

/*** Common Controls ***/
#define CCM_FIRST           0x2000
#define CCM_LAST            0x2000 + 0x200
#define CCM_SETBKCOLOR      (CCM_FIRST + 1)
#define CCM_SETCOLORSCHEME  (CCM_FIRST + 2)
#define CCM_GETCOLORSCHEME  (CCM_FIRST + 3)
#define CCM_GETDROPTARGET   (CCM_FIRST + 4)
#define CCM_SETUNICODEFORMAT (CCM_FIRST + 5)
#define CCM_GETUNICODEFORMAT (CCM_FIRST + 6)

/*** Private Window Messages ***/
#define WM_HELPHITTEST      0x020C
#define WM_ENTERMENULOOP    0x0211
#define WM_EXITMENULOOP     0x0212
#define WM_NEXTMENU         0x0213
#define WM_UNINITMENUPOPUP  0x0125
#define WM_WTSSESSION_CHANGE 0x02B1
#define WM_THEMECHANGED     0x031A

/*** ComboBox Messages ***/
#define CB_ERR             ((UINT)-1)
#define CB_HASSTRINGS      0x0163

/*** Edit Messages ***/
#define EN_ERRSPACE        0x150
#define EN_HSCROLL         0x151
#define EN_VSCROLL         0x152

/*** Button Styles ***/
#define BS_PUSHBUTTON      0x00
#define BS_DEFPUSHBUTTON   0x01
#define BS_CHECKBOX        0x02
#define BS_AUTOCHECKBOX    0x03
#define BS_RADIOBUTTON     0x04
#define BS_3STATE          0x05
#define BS_AUTO3STATE      0x06
#define BS_GROUPBOX        0x07
#define BS_USERBUTTON      0x08
#define BS_AUTORADIOBUTTON 0x09
#define BS_OWNERDRAW       0x0B

/*** Scrollbar Styles ***/
#define SBS_HORZ                    0x0000
#define SBS_VERT                    0x0001
#define SBS_TOPALIGN                0x0002
#define SBS_LEFTALIGN               0x0002
#define SBS_BOTTOMALIGN             0x0004
#define SBS_RIGHTALIGN              0x0004
#define SBS_SIZEBOX                  0x0008
#define SBS_SIZEBOXBOTTPMRIGHT       0x0008
#define SBS_SIZEBOXTOPLEFTALIGN      0x0002
#define SBS_SIZEBOXBOTTPMLEFTALIGN   0x0004

/*** Static Control Styles ***/
#define SS_LEFT             0x0000
#define SS_CENTER          0x0001
#define SS_RIGHT           0x0002
#define SS_ICON            0x0003
#define SS_BLACKRECT        0x0004
#define SS_GRAYRECT         0x0005
#define SS_WHITERECT        0x0006
#define SS_BLACKFRAME       0x0007
#define SS_GRAYFRAME        0x0008
#define SS_WHITEFRAME       0x0009
#define SS_USERITEM         0x000A
#define SS_SIMPLE           0x000B
#define SS_LEFTNOWORDWRAP  0x000C
#define SS_BITMAP           0x000E
#define SS_ETCHEDHORZ       0x0010
#define SS_ETCHEDVERT       0x0011
#define SS_ETCHEDFRAME      0x0012

/*** Menu Flags ***/
#define MFT_SEPARATOR        0x00000800
#define MFT_END              0x00000080
#define MF_ENABLED          0x00000000
#define MF_GRAYED           0x00000001
#define MF_DISABLED          0x00000002
#define MF_MENUBREAK        0x00000040
#define MF_MENUBARBREAK     0x00000080
#define MF_UNCHECKED        0x00000000
#define MF_CHECKED          0x00000008
#define MF_POPUP            0x00000010
#define MF_HILITE           0x00000080
#define MF_DEFAULT          0x00001000
#define MF_SYSMENU          0x00002000
#define MF_HELP             0x00004000
#define MF_MOUSEUSE         0x00008000
#define MF_BITMAP           0x00000004
#define MF_OWNERDRAW        0x00000100
#define MF_STRING           0x00000000
#define MF_SEIZURE          0x00000002

/*** Window Placement ***/
#define WPF_SETMINPOSITION   0x01
#define WPF_RESTORETOMAXIMIZED 0x02
#define WPF_ASYNCRONOUS     0x04

#endif /* _USER32_H_ */
'''

# ============================================================================
# precomp.h - Precompiled header for gdi32 (33 files need it)
# ============================================================================

PRECOMP_H = r'''/*
 * MinNT - win32k/gdi32/precomp.h
 * GDI32 precompiled header
 */

#ifndef _GDI32_PRECOMP_H_
#define _GDI32_PRECOMP_H_

#include <nt/ntdef.h>
#include <nt/ke.h>
#include <nt/io.h>
#include <nt/mm.h>

#include "win32k.h"
#include "debug.h"
#include "gdi_private.h"

#include <string.h>
#include <stdlib.h>

#define WIN32K_GDI_ENTRY NTSTATUS APIENTRY

typedef NTSTATUS (APIENTRY *GDI_ENTRY_PROC)(void);

#endif /* _GDI32_PRECOMP_H_ */
'''

# ============================================================================
# gdi_private.h - GDI private header (11 files need it)
# ============================================================================

GDI_PRIVATE_H = r'''/*
 * MinNT - win32k/gdi32/gdi_private.h
 * GDI private declarations
 */

#ifndef _GDI_PRIVATE_H_
#define _GDI_PRIVATE_H_

#include "../win32k.h"
#include "../debug.h"

#define GDI_HANDLE_LOCAL   0x40000000
#define GDI_HANDLE_REMOTE 0x80000000

typedef struct _GDI_SHARED_MEMORY
{
    LONG                    locked;
    PVOID                   base address;
    HANDLE                  section handle;
    SIZE_T                  size;
    PHYSICAL_ADDRESS        physical;
} GDI_SHARED_MEMORY;

typedef struct _GDI_BATCH {
    ULONG                   count;
    PVOID                   proc;
    ULONG                   args[16];
} GDI_BATCH;

typedef struct _GDILOCALMEM {
    ULONG                   size;
    ULONG                   flags;
    HANDLE                 handle;
    PVOID                   base address;
} GDILOCALMEM;

typedef struct _BRUSHDATA {
    ULONG                   style;
    ULONG                   color;
    ULONG                   hatch;
    HANDLE                 handle;
} BRUSHDATA;

typedef struct _PENDATA {
    ULONG                   style;
    ULONG                   width;
    ULONG                   color;
    HANDLE                 handle;
} PENDATA;

typedef struct _FONTDIFF {
    BYTE                    match;
    BYTE                    weight;
    SHORT                   yHeight;
    SHORT                   yCharOffset;
    BYTE                    panose;
    BYTE                    charset;
} FONTDIFF;

#define GDI_OBJ_HMGR        0x01
#define GDI_OBJ_OWNERDEV    0x02
#define GDI_OBJ_DEVPRIVATE  0x04
#define GDI_OBJ_SHARED      0x08

extern HANDLE gdiSharedMemoryHandle;

GDI_HANDLE_ALLOC();
GDI_HANDLE_LOCK();
GDI_HANDLE_UNLOCK();

#define GDI_MAX_HANDLE      0x4000
#define GDI_HANDLE_INVALID  0

#define GDI_IS_HANDLE(x)    (((ULONG_PTR)(x) & 0x80000000) != 0)

#endif /* _GDI_PRIVATE_H_ */
'''

# ============================================================================
# consrv.h - Console server header (28 files need it)
# ============================================================================

CONSRV_H = r'''/*
 * MinNT - win32k/consrv/consrv.h
 * Console server internal header
 */

#ifndef _CONSRV_H_
#define _CONSRV_H_

#include <nt/ntdef.h>
#include <nt/ke.h>
#include <nt/io.h>
#include <nt/mm.h>
#include <nt/conio.h>

typedef struct _CONSOLE {
    ULONG               type;
    HANDLE              hProcess;
    HANDLE              ScreenBuffer;
    HANDLE              ActiveBuffer;
    ULONG               OutputCP;
    ULONG               InputCP;
    ULONG               ConsoleCP;
    COORD               ConsoleSize;
    COORD               BufferSize;
    SMALL_RECT          VisibleBounds;
    COORD               CurrentVirtualY;
    POINTL              CursorPosition;
    BOOL                CursorVisible;
    ULONG               CursorSize;
    ULONG               ScreenAttributes;
    ULONG               PopupAttributes;
    BOOL                QuickEdit;
    BOOL                AutoPosition;
    ULONG               HistoryBufferCount;
    ULONG               HistoryEditBufSize;
    HANDLE              HistoryBuffer;
    CONSOLE_SCREEN_BUFFER_INFO Info;
} CONSOLE;

typedef struct _CONSOLE_SCREEN_BUFFER {
    ULONG               type;
    CONSOLE            *Console;
    COORD              dwSize;
    COORD              dwCursorPosition;
    ULONG              wAttributes;
    SMALL_RECT         srWindow;
    COORD              dwMaximumWindowSize;
} CONSOLE_SCREEN_BUFFER;

typedef struct _INPUT_RECORD {
    USHORT              EventType;
    union {
        KEY_EVENT_RECORD KeyEvent;
        MOUSE_EVENT_RECORD MouseEvent;
        WINDOW_BUFFER_SIZE_RECORD WindowBufferSizeEvent;
        FOCUS_EVENT_RECORD FocusEvent;
        MENU_EVENT_RECORD MenuEvent;
    } Event;
} INPUT_RECORD;

typedef struct _CHAR_INFO {
    WCHAR              UnicodeChar;
    USHORT             Attributes;
} CHAR_INFO;

#define EVENT_FOCUS        0x0010
#define EVENT_KEY          0x0001
#define EVENT_MOUSE        0x0002
#define EVENT_MOUSE_MOVE   0x0004
#define EVENT_BUFFER_SIZE  0x0008
#define EVENT_MENU         0x0080

#define CONSRV_MODE_ANSI     0x01
#define CONSRV_MODE_OEM      0x02
#define CONSRV_MODE_LINE_INPUT 0x04

extern CONSOLE *gConsoleListHead;

#endif /* _CONSRV_H_ */
'''

# ============================================================================
# Write all headers
# ============================================================================

def write_header(filename, content):
    filepath = WIN32K_DIR / filename
    filepath.parent.mkdir(parents=True, exist_ok=True)
    with open(filepath, 'w') as f:
        f.write(content)
    print(f"Generated: {filepath}")

def main():
    # Core win32k internal headers
    write_header("win32k.h", WIN32K_H)
    write_header("debug.h", DEBUG_H)
    write_header("user32.h", USER32_H)
    
    # GDI32 headers
    write_header("gdi32/precomp.h", PRECOMP_H)
    write_header("gdi32/gdi_private.h", GDI_PRIVATE_H)
    
    # Console server headers
    write_header("consrv/consrv.h", CONSRV_H)
    
    # Wine compatibility debug (wine/debug.h)
    write_header("wine/debug.h", r'''/*
 * MinNT - win32k/wine/debug.h
 * Wine debug compatibility header
 */

#ifndef WINE_DEBUG_H
#define WINE_DEBUG_H

#include <nt/ke.h>

#define WINEPREFIX __FILE__, __LINE__

typedef struct _DEBUG_INFO {
    ULONG_PTR prefix;
    ULONG flags;
    ULONG layer;
} DEBUG_INFO;

#define TRACE(fmt, ...) DbgPrint("WINE: " fmt, ##__VA_ARGS__)
#define WARN(fmt, ...) DbgPrint("WINE: " fmt, ##__VA_ARGS__)
#define ERR(fmt, ...) DbgPrint("WINE ERROR: " fmt, ##__VA_ARGS__)

#define DPRINTF TRACE

#endif /* WINE_DEBUG_H */
''')

    # Additional headers needed
    write_header("wingdi.h", r'''/*
 * MinNT - win32k/wingdi.h
 * Windows GDI definitions (Wine compatibility)
 */

#ifndef _WINGDI_H_
#define _WINGDI_H_

#include <nt/ntdef.h>

#define GDI_ERROR           0xFFFFFFFF
#define CLR_INVALID        0xFFFFFFFF

#define R2_BLACK            1
#define R2_WHITE            0
#define R2_NOT              2
#define R2_NOTMERGEPENS    3
#define R2_XORPEN           4
#define R2_NOTXORPEN        5
#define R2_NOTERASE        6
#define R2_INVERT           7
#define R2_OUT              8
#define R2_COPY             13
#define R2_PATCOPY         11

#define SRCCOPY            0x00CC0020
#define SRCPAINT           0x00EE0086
#define SRCAND             0x008800C6
#define SRCINVERT          0x00660046
#define SRCERASE           0x00440328
#define NOTSRCCOPY         0x00330066
#define NOTSRCERASE        0x001100A9
#define MERGECOPY          0x00C000C8
#define MERGEPAINT         0x00BB0227
#define PATCOPY           0x00F00021
#define PATPAINT           0x00FB0A09
#define PATINVERT          0x005A0049
#define DSTINVERT          0x00550009
#define BLACKONWHITE       0x00010003
#define WHITEONBLACK       0x00020004

#define BLACKNESS          0x00000042
#define WHITENESS          0x000000FF
#define TRANSPARENT        1
#define OPAQUE             2

#define PS_SOLID           0
#define PS_DASH            1
#define PS_DOT             2
#define PS_DASHDOT         3
#define PS_DASHDOTDOT      4
#define PS_NULL            5
#define PS_INSIDEFRAME     6

#define PS_COSMETIC        0x00000000
#define PS_ENDCAP_ROUND    0x00000000
#define PS_ENDCAP_SQUARE   0x00000100
#define PS_JOIN_ROUND     0x00000000
#define PS_JOIN_BEVEL      0x00001000

#define BS_SOLID           0
#define BS_NULL            1
#define BS_HATCHED          2
#define BS_PATTERN          3
#define BS_INDEXED          4
#define BS_DIBPATTERN       5
#define BS_DIBPATTERN8      8
#define BS_MONOPATTERN      9

#define HS_HORIZONTAL      0
#define HS_VERTICAL        1
#define HS_FDIAGONAL       2
#define HS_BDIAGONAL       3
#define HS_CROSS           4
#define HS_DIAGCROSS       5

#define DC_BRUSH           18
#define DC_PEN             19
#define DC_EXTPEN          114
#define SIZEF_FULLSCREEN  1
#define SIZEF_PALETTE     2
#define SIZEF_RESTORE      3
#define SIZEF_NOVIRTUALSCREEN 4

#define ETO_OPAQUE         0x00000002
#define ETO_CLIPPED        0x00000004
#define ETO_GLYPH_INDEX    0x00000080
#define ETO_IGNORELANGUAGE 0x00001000
#define ETO_PDY            0x00002000
#define ETO_RTLREADING     0x00000800

#define CLIP_DEFAULT_PRECIS 0
#define CLIP_CHARACTER_PRECIS 1
#define CLIP_STROKE_PRECIS 2
#define CLIP_LH_ANGLES     1
#define CLIP_TT_ALWAYS     2
#define CLIP_EMBEDDED     4

#define OUT_DEFAULT_PRECIS 0
#define OUT_STRING_PRECIS 1
#define OUT_CHARACTER_PRECIS 2
#define OUT_STROKE_PRECIS 3
#define OUT_TT_PRECIS     4
#define OUT_DEVICE_PRECIS 5
#define OUT_RASTER_PRECIS 6
#define OUT_TT_ONLY_PRECIS 7
#define OUT_OUTLINE_PRECIS 8
#define OUT_SCREENONLY_PRECIS 9

#define DEFAULT_CHARSET    1
#define OEM_CHARSET        255
#define ANSI_CHARSET       0
#define SYMBOL_CHARSET     2

#define LF_FACESIZE        32

#define DEFAULT_PITCH      0
#define FIXED_PITCH        1
#define VARIABLE_PITCH     2
#define MONO_FONT          8

#define FF_DONTCARE       0
#define FF_ROMAN           1
#define FF_SWISS          2
#define FF_SCRIPT          4
#define FF_MODERN          5
#define FF_BOLD           8
#define FF_REGULAR        16

typedef struct _POINT {
    LONG x;
    LONG y;
} POINT, *PPOINT;

typedef struct _POINTL {
    LONG x;
    LONG y;
} POINTL, *PPOINTL;

typedef struct _SIZE {
    LONG cx;
    LONG cy;
} SIZE, *PSIZE;

typedef struct _SIZEL {
    LONG cx;
    LONG cy;
} SIZEL, *PSIZEL;

typedef struct _RECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *PRECT;

typedef struct _RECTL {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECTL, *PRECTL;

typedef struct _RGBQUAD {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
} RGBQUAD;

typedef struct _BITMAPINFOHEADER {
    ULONG biSize;
    LONG biWidth;
    LONG biHeight;
    WORD biPlanes;
    WORD biBitCount;
    ULONG biCompression;
    ULONG biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
    ULONG biClrUsed;
    ULONG biClrImportant;
} BITMAPINFOHEADER;

typedef struct _BITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[1];
} BITMAPINFO;

#define BI_RGB        0
#define BI_RLE8       1
#define BI_RLE4       2
#define BI_BITFIELDS  3

#define DIB_RGB_COLORS    0
#define DIB_PAL_COLORS    1

#define HBITMAP         ULONG_PTR
#define HDC             ULONG_PTR
#define HRGN            ULONG_PTR
#define HPEN            ULONG_PTR
#define HBRUSH          ULONG_PTR
#define HPALETTE        ULONG_PTR
#define HFONT           ULONG_PTR
#define HMETAFILE       ULONG_PTR
#define HENHMETAFILE    ULONG_PTR
#define HRGN            ULONG_PTR

#define CreateBitmap(x,y,c,z,b) 0
#define CreateCompatibleBitmap(x,y) 0
#define CreateCompatibleDC(x) 0
#define GetDC(x) 0
#define ReleaseDC(x) 0
#define DeleteDC(x) 0
#define DeleteObject(x) 0
#define SelectObject(x,y) 0
#define GetObject(x,y,z) 0

#define TRANSPARENT         1
#define OPAQUE              2

#define AC_SRC_OVER         0x00
#define AC_SRC_ALPHA        0x01

#endif /* _WINGDI_H_ */
''')

    write_header("winuser.h", r'''/*
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
''')

    write_header("winbase.h", r'''/*
 * MinNT - win32k/winbase.h
 * Windows base API definitions (Wine compatibility)
 */

#ifndef _WINBASE_H_
#define _WINBASE_H_

#include <nt/ntdef.h>

typedef struct _SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME;

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

#define CREATE_NEW         1
#define CREATE_ALWAYS      2
#define OPEN_EXISTING      3
#define OPEN_ALWAYS        4
#define TRUNCATE_EXISTING  5

#define GENERIC_READ       0x80000000
#define GENERIC_WRITE      0x40000000
#define GENERIC_EXECUTE     0x20000000
#define GENERIC_ALL        0x10000000

#define FILE_ATTRIBUTE_NORMAL        0x00000080
#define FILE_ATTRIBUTE_HIDDEN        0x00000002
#define FILE_ATTRIBUTE_SYSTEM        0x00000004
#define FILE_ATTRIBUTE_DIRECTORY      0x00000010
#define FILE_ATTRIBUTE_ARCHIVE       0x00000020
#define FILE_ATTRIBUTE_READONLY     0x00000001

#define MB_OK                    0x00000000
#define MB_OKCANCEL              0x00000001
#define MB_ABORTRETRYIGNORE      0x00000002
#define MB_YESNOCANCEL           0x00000003
#define MB_YESNO                 0x00000004
#define MB_RETRYCANCEL          0x00000005

#define IDOK         1
#define IDCANCEL     2
#define IDABORT      3
#define IDRETRY      4
#define IDIGNORE     5
#define IDYES        6
#define IDNO         7
#define IDCLOSE      8
#define IDHELP       9

#define HANDLE WIN32K_POOL_HANDLE

typedef struct _OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    ULONG Offset;
    ULONG OffsetHigh;
    HANDLE hEvent;
} OVERLAPPED;

typedef struct _SECURITY_ATTRIBUTES {
    ULONG nLength;
    PVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES;

typedef struct _WIN32_FIND_DATAW {
    ULONG dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    ULONG nFileSizeHigh;
    ULONG nFileSizeLow;
    ULONG dwReserved0;
    ULONG dwReserved1;
    WCHAR cFileName[260];
    WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW;

#define FindFirstFileW(a,b) ((HANDLE)0)
#define FindNextFileW(a,b) 0
#define FindClose(a) 0
#define CreateFileW(a,b,c,d,e,f,g) ((HANDLE)-1)
#define ReadFile(a,b,c,d,e,f) 0
#define WriteFile(a,b,c,d,e,f) 0
#define CloseHandle(a) 1
#define GetLastError() 0

#endif /* _WINBASE_H_ */
''')

    write_header("winerror.h", r'''/*
 * MinNT - win32k/winerror.h
 * Windows error codes (Wine compatibility)
 */

#ifndef _WINERROR_H_
#define _WINERROR_H_

#define ERROR_SUCCESS              0x00000000
#define ERROR_INVALID_FUNCTION    0x00000001
#define ERROR_FILE_NOT_FOUND       0x00000002
#define ERROR_PATH_NOT_FOUND      0x00000003
#define ERROR_ACCESS_DENIED       0x00000005
#define ERROR_INVALID_HANDLE       0x00000006
#define ERROR_NOT_ENOUGH_MEMORY   0x00000008
#define ERROR_INVALID_ACCESS       0x0000000C
#define ERROR_OUTOFMEMORY          0x0000000E
#define ERROR_INVALID_PARAMETER    0x00000057
#define ERROR_INSUFFICIENT_BUFFER 0x0000007A
#define ERROR_INVALID_FLAGS        0x000003ED
#define ERROR_INVALID_PARAMETER   0x00000057
#define ERROR_NOGP                0x00000203

#define STATUS_SUCCESS            0x00000000
#define STATUS_NOT_IMPLEMENTED    0xC0000002
#define STATUS_INVALID_PARAMETER  0xC000000D
#define STATUS_NO_MEMORY         0xC0000017
#define STATUS_ACCESS_DENIED     0xC0000022

#endif /* _WINERROR_H_ */
''')

    print("\nAll headers generated!")

if __name__ == "__main__":
    main()