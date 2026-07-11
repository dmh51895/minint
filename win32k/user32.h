/*
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
